#pragma once

#include "camera.hh"
#include "diagnostics.hh"
#include "dev_ui.hh"
#include "game_state.hh"
#include "player_controller.hh"
#include "renderer.hh"
#include "simulation/core/simulation.hh"
#include "simulation/core/timestep_controller.hh"
#include "window.hh"
#include "world.hh"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <variant>

template <int Dim>
struct Dimension_Resources
{
    std::unique_ptr<Player_Controller<Dim>> player_controller;
    std::unique_ptr<Renderer<Dim>> renderer;
    std::unique_ptr<Camera<Dim>> camera;
};

using Dim_Variant = std::variant<Dimension_Resources<2>, Dimension_Resources<3>>;

class Game
{
public:
    void run();

    void reset();

    // game state
    Game_State game_state;
    // constructor/destructor
    Game(unsigned int width, unsigned int height);
    ~Game();

    // outward resources
    std::unique_ptr<Window> window;
    ecs::Entity camera_entity;
    ecs::Entity player_entity;
    std::shared_ptr<Dev_UI> dev_ui;
    std::unique_ptr<World> world;
    std::unique_ptr<Simulation> sim;
    Dim_Variant dim_resources;

    std::mutex world_mutex_;
    std::atomic<bool> sim_should_pause_{false};
    std::atomic<bool> sim_thread_idle_{true};
    std::atomic<double> sim_time_{0.0};
    std::atomic<std::uint64_t> completed_sim_steps_{0};
    Diagnostics diagnostics_;
    Timestep_Controller timestep_controller_;
    std::uint64_t diagnostics_step_ = 0;
    bool left_mouse_down_ = false;

    void init_dimension_resources();

    //  initialize game state (load all shaders/textures/levels)
    void init();
    // game loop
    void tick(float dt);
    void render();
    void collisions_detection();
    void reset_player();
};
