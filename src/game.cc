#include "game.hh"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <tbb/tbb.h>
#include <thread>
#include <imgui/imgui.h>
#include "defines.hh"
#include "ecs/box_component.hh"
#include "resource_handler.hh"
#include "simulation/fluid_solver.hh"
#include "simulation/fluid_solver_3d.hh"
#include "simulation/mpm.hh"
#include "world.hh"

namespace
{
struct Grid_Domain
{
    int width = 100;
    int height = 100;
};

Grid_Domain grid_domain_from(Settings const& settings)
{
    double width = std::clamp(settings.GRID_WIDTH.load(std::memory_order_relaxed), 16.0, 400.0);
    double height = std::clamp(settings.GRID_HEIGHT.load(std::memory_order_relaxed), 16.0, 400.0);
    if (settings.FLUID_SCENE == TAYLOR_GREEN_VORTEX || settings.FLUID_SCENE == CONFINED_VORTEX)
        height = width;

    return Grid_Domain{
        .width = static_cast<int>(std::lround(width)),
        .height = static_cast<int>(std::lround(height)),
    };
}

std::unique_ptr<Simulation> make_simulation(World& world)
{
    Grid_Domain const domain = grid_domain_from(world.settings());

    if (world.settings().MPM)
        return std::make_unique<MPM>(domain.width, domain.height, 1.0, (1.0 / 60.0), world);

    double fluid_dx = std::clamp(world.settings().FLUID_DX.load(std::memory_order_relaxed), 0.25, 4.0);

    if (world.settings().DIMENSION == 3)
        return std::make_unique<Fluid_Solver_3D>(fluid_dx, domain.width, domain.height, domain.width, world, 0.001);

    return std::make_unique<Fluid_Solver_2D>(fluid_dx, domain.width, domain.height, world, 0.001);
}
} // namespace

template <int Dim>
static Dimension_Resources<Dim> make_dim_resources(
    ecs::Entity player_entity, Game_State& game_state, Window& window, World& world)
{
    Dimension_Resources<Dim> res;
    res.renderer = std::make_unique<Renderer<Dim>>(window.FB_WIDTH, window.FB_HEIGHT);
    res.player_controller = std::make_unique<Player_Controller<Dim>>(player_entity, game_state, window);
    res.camera = std::make_unique<Camera<Dim>>(world, player_entity, window);
    return res;
}

void Game::init_dimension_resources()
{
    if (world->settings().DIMENSION == 3)
        dim_resources = make_dim_resources<3>(player_entity, game_state, *window, *world);
    else
        dim_resources = make_dim_resources<2>(player_entity, game_state, *window, *world);
}

Game::Game(unsigned int width, unsigned int height)
{
    // Game
    game_state = GAME_ACTIVE;
    world = std::make_unique<World>();

    window = std::make_unique<Window>(world->settings().WINDOW_DIMENSIONS.first, world->settings().WINDOW_DIMENSIONS.second);
    dev_ui = window->dev_ui;

    Resource_Handler::load_texture("../../res/sprites/block.png", false, "block");
    Resource_Handler::load_texture("../../res/sprites/circle.png", true, "circle");

    // Player
    camera_entity = world->create_entity("Camera");
    world->add_component(camera_entity, Transform_Component{.position{0.f, 0.f, 0.f}});
    player_entity = world->create_entity("Player");
    world->add_component(player_entity, Transform_Component{.position = glm::vec3(0.f, 420.f, 0.f), .scale = glm::vec3(PLAYER_SIZE)});
    world->add_component(player_entity, Sprite_Component{.texture = Resource_Handler::get_texture("block"), .color = glm::vec3(1.0f, 0.f, 0.f)});
    world->add_component(player_entity, Physics_Component{.velocity = glm::vec2(0.0f), .is_dynamic = true});

    // Simulation container
    auto box_entity = world->create_entity("");
    Grid_Domain const domain = grid_domain_from(world->settings());
    world->add_component(box_entity, Box_Component{.size = glm::vec3(static_cast<float>(domain.width),
                                                                     static_cast<float>(domain.height),
                                                                     static_cast<float>(domain.width)),
                                                  .offset = glm::vec3(0.f)});

    init_dimension_resources();

    // Environment
    sim = make_simulation(*world);
};

Game::~Game() {}

void Game::reset()
{
    init_dimension_resources();

    sim.reset();
    sim = make_simulation(*world);
}

void Game::run()
{
    std::atomic<bool> should_quit{false};

    tbb::task_arena arena;
    arena.execute(
        [&]
        {
            tbb::task_group tg;

            // ---- Simulation thread ----
            tg.run(
                [&]()
                {
                    double last_step = glfwGetTime();
                    while (!should_quit.load(std::memory_order_relaxed))
                    {
                        if (sim_should_pause_.load(std::memory_order_relaxed) ||
                            world->settings().PAUSED.load(std::memory_order_relaxed))
                        {
                            sim_thread_idle_.store(true, std::memory_order_release);
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            last_step = glfwGetTime();
                            continue;
                        }

                        double now = glfwGetTime();
                        double target_dt = std::clamp(world->settings().SIMULATION_DT.load(std::memory_order_relaxed), 1.0e-5, 0.25);
                        if (now - last_step < target_dt)
                        {
                            std::this_thread::yield();
                            continue;
                        }

                        sim_thread_idle_.store(false, std::memory_order_release);
                        {
                            std::lock_guard<std::mutex> lock(world_mutex_);
                            Timestep_Decision timestep = timestep_controller_.decide(*world, sim->grid(), target_dt);

                            for (int s = 0; s < timestep.substeps; ++s)
                            {
                                sim->step(timestep.sub_dt);
                                completed_sim_steps_.fetch_add(1, std::memory_order_relaxed);
                                double next_time = sim_time_.load(std::memory_order_relaxed) + timestep.sub_dt;
                                auto sample = diagnostics_.record_step(*world, sim->grid(), sim->stage_diagnostics(), diagnostics_step_++, next_time,
                                                                        timestep.sub_dt, timestep.substeps, timestep.frame_dt);
                                sim_time_.store(next_time, std::memory_order_relaxed);

                                if (diagnostics_.should_pause_for(sample))
                                {
                                    world->settings().PAUSED.store(true, std::memory_order_relaxed);
                                    break;
                                }
                            }
                        }
                        last_step = now;
                        sim_thread_idle_.store(true, std::memory_order_release);
                    }
                });

            // ---- Main thread: input + render ----
            while (!glfwWindowShouldClose(window->glfw_window))
            {
                glfwPollEvents();
                window->clear();

                // Handle reset — pause sim thread, lock world, reset, resume
                if (world->settings().RESET.load())
                {
                    sim_should_pause_.store(true);
                    while (!sim_thread_idle_.load(std::memory_order_acquire))
                        std::this_thread::yield();
                    {
                        std::lock_guard<std::mutex> lock(world_mutex_);
                        reset();
                        sim_time_.store(0.0);
                        completed_sim_steps_.store(0);
                        diagnostics_.reset();
                        diagnostics_step_ = 0;
                        world->settings().RESET.store(false);
                    }
                    sim_should_pause_.store(false);
                    continue;
                }

                // Input
                std::visit([&](auto& res) { res.player_controller->tick(*world); }, dim_resources);

                // Camera + render
                std::visit(
                    [&](auto& res)
                    {
                        res.camera->tick();
                        res.renderer->tick(*world, *res.camera, diagnostics_);
                    },
                    dim_resources);

                dev_ui->NewFrame();

                bool left_mouse_now = glfwGetMouseButton(window->glfw_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                if (left_mouse_now && !left_mouse_down_ && !ImGui::GetIO().WantCaptureMouse)
                {
                    double mouse_x = 0.0;
                    double mouse_y = 0.0;
                    int win_width = 0;
                    int win_height = 0;
                    glfwGetCursorPos(window->glfw_window, &mouse_x, &mouse_y);
                    glfwGetWindowSize(window->glfw_window, &win_width, &win_height);

                    std::visit(
                        [&](auto& res)
                        {
                            diagnostics_.select_nearest_particle(*world, sim->grid(), res.camera->view_matrix(), res.camera->projection_matrix(),
                                                                  win_width, win_height, mouse_x, mouse_y, 14.0f);
                        },
                        dim_resources);
                }
                left_mouse_down_ = left_mouse_now;

                dev_ui->Update(*world, sim->grid(), diagnostics_, sim_time_.load(), completed_sim_steps_.load(std::memory_order_relaxed));
                dev_ui->Render();

                glfwSwapBuffers(window->glfw_window);
            }

            should_quit.store(true);
            tg.wait();
        });

    dev_ui->Shutdown();
}
