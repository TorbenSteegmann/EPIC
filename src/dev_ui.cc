#include "dev_ui.hh"

#include "simulation/fluid_kernels/polypic.hh"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <string>
#include <vector>

namespace
{
template <typename Getter>
void plot_sample_metric(char const* label, std::vector<Diagnostics_Sample> const& samples, Getter getter)
{
    std::vector<float> values;
    values.reserve(samples.size());
    for (auto const& sample : samples)
        values.push_back(static_cast<float>(getter(sample)));

    if (values.empty())
    {
        ImGui::Text("%s: no samples", label);
        return;
    }

    ImGui::PlotLines(label, values.data(), static_cast<int>(values.size()), 0, nullptr, FLT_MAX, FLT_MAX, ImVec2(0, 70));
}

void apply_mpm_compressed_square_settings(Settings& settings)
{
    settings.DIMENSION = 2;
    settings.ADAPTIVE_TIMESTEP.store(false, std::memory_order_relaxed);
    settings.SIMULATION_DT.store(0.0000001, std::memory_order_relaxed);
    settings.GRID_WIDTH.store(64.0, std::memory_order_relaxed);
    settings.GRID_HEIGHT.store(64.0, std::memory_order_relaxed);
    settings.APPLY_GRAVITY.store(false, std::memory_order_relaxed);
}

// Selector for meaningful 2D Nr values. Fluid stays multilinear-complete at Nr=4;
// MPM can also choose the paper-aligned multiquadratic full basis at Nr=9.
void polypic_mode_selector_2d(Settings& settings, bool allow_mpm_nr9 = false)
{
    static constexpr int fluid_nr_values[] = {1, 3, 4};
    static char const* const fluid_nr_labels[] = {"Nr=1 (PIC)", "Nr=3 (APIC)", "Nr=4 (PolyPIC)"};
    static constexpr int mpm_nr_values[] = {1, 3, 4, 9};
    static char const* const mpm_nr_labels[] = {"Nr=1 (PIC)", "Nr=3 (APIC)", "Nr=4 (truncated)", "Nr=9 (PolyPIC)"};

    int const* nr_values = allow_mpm_nr9 ? mpm_nr_values : fluid_nr_values;
    char const* const* nr_labels = allow_mpm_nr9 ? mpm_nr_labels : fluid_nr_labels;
    int const nr_count = allow_mpm_nr9 ? 4 : 3;

    int current = nr_count - 1; // default to full PolyPIC for any unrecognized value
    for (int k = 0; k < nr_count; ++k)
        if (settings.POLYPIC_MODES == nr_values[k])
            current = k;

    if (settings.POLYPIC_MODES != nr_values[current])
        settings.POLYPIC_MODES = nr_values[current];
    if (ImGui::Combo("PolyPIC modes (Nr)", &current, nr_labels, nr_count))
        settings.POLYPIC_MODES = nr_values[current];
}
} // namespace

static void apply_style()
{
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = 8.f;
    s.FrameRounding     = 5.f;
    s.PopupRounding     = 5.f;
    s.ScrollbarRounding = 5.f;
    s.GrabRounding      = 4.f;
    s.TabRounding       = 4.f;
    s.WindowBorderSize  = 0.f;
    s.FrameBorderSize   = 0.f;
    s.WindowPadding     = {12.f, 10.f};
    s.FramePadding      = {8.f,  4.f};
    s.ItemSpacing       = {8.f,  6.f};
    s.ScrollbarSize     = 12.f;
    s.GrabMinSize       = 10.f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]          = {0.11f, 0.11f, 0.13f, 0.92f};
    c[ImGuiCol_TitleBg]           = {0.08f, 0.08f, 0.10f, 1.00f};
    c[ImGuiCol_TitleBgActive]     = {0.14f, 0.14f, 0.18f, 1.00f};
    c[ImGuiCol_FrameBg]           = {0.18f, 0.18f, 0.22f, 1.00f};
    c[ImGuiCol_FrameBgHovered]    = {0.24f, 0.24f, 0.30f, 1.00f};
    c[ImGuiCol_FrameBgActive]     = {0.28f, 0.28f, 0.36f, 1.00f};
    c[ImGuiCol_Button]            = {0.26f, 0.45f, 0.78f, 1.00f};
    c[ImGuiCol_ButtonHovered]     = {0.35f, 0.55f, 0.90f, 1.00f};
    c[ImGuiCol_ButtonActive]      = {0.20f, 0.38f, 0.68f, 1.00f};
    c[ImGuiCol_Header]            = {0.26f, 0.45f, 0.78f, 0.60f};
    c[ImGuiCol_HeaderHovered]     = {0.26f, 0.45f, 0.78f, 0.80f};
    c[ImGuiCol_HeaderActive]      = {0.26f, 0.45f, 0.78f, 1.00f};
    c[ImGuiCol_SliderGrab]        = {0.40f, 0.62f, 0.95f, 1.00f};
    c[ImGuiCol_SliderGrabActive]  = {0.52f, 0.72f, 1.00f, 1.00f};
    c[ImGuiCol_CheckMark]         = {0.40f, 0.62f, 0.95f, 1.00f};
    c[ImGuiCol_Separator]         = {0.28f, 0.28f, 0.34f, 1.00f};
    c[ImGuiCol_Text]              = {0.90f, 0.90f, 0.95f, 1.00f};
    c[ImGuiCol_TextDisabled]      = {0.50f, 0.50f, 0.58f, 1.00f};
    c[ImGuiCol_ScrollbarBg]       = {0.08f, 0.08f, 0.10f, 1.00f};
    c[ImGuiCol_ScrollbarGrab]     = {0.30f, 0.30f, 0.38f, 1.00f};
    c[ImGuiCol_Tab]               = {0.16f, 0.16f, 0.20f, 1.00f};
    c[ImGuiCol_TabHovered]        = {0.26f, 0.45f, 0.78f, 1.00f};
    c[ImGuiCol_TabActive]         = {0.20f, 0.35f, 0.65f, 1.00f};
    c[ImGuiCol_PopupBg]           = {0.12f, 0.12f, 0.15f, 0.96f};
}

void Dev_UI::Init(GLFWwindow* window, char const* glsl_version)
{
    this->window = window;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    io.Fonts->AddFontDefault();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
    apply_style();
    last_time = (float)glfwGetTime();
    last_sps_time = last_time;
    current_sps = 0.0;
    last_sps_completed_steps = 0;
}

void Dev_UI::NewFrame()
{
    // feed inputs to dear imgui, start new frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Dev_UI::Update(World& world, Grid& grid, Diagnostics& diagnostics, double sim_time, std::uint64_t completed_sim_steps)
{
    float current_time = (float)glfwGetTime();
    float dt = current_time - last_time;
    last_time = current_time;
    int fps = (int)(1.f / dt);
    double sps_elapsed = static_cast<double>(current_time) - last_sps_time;
    if (completed_sim_steps < last_sps_completed_steps)
    {
        last_sps_completed_steps = completed_sim_steps;
        last_sps_time = current_time;
        current_sps = 0.0;
    }
    else if (sps_elapsed >= 1.0)
    {
        std::uint64_t completed_steps = completed_sim_steps - last_sps_completed_steps;
        current_sps = static_cast<double>(completed_steps) / sps_elapsed;
        last_sps_completed_steps = completed_sim_steps;
        last_sps_time = current_time;
    }
    double mouse_x{0.};
    double mouse_y{0.};
    glfwGetCursorPos(window, &mouse_x, &mouse_y);
    ImGui::Begin("Settings"); // Create a window called "NAME" and append into it.
    ImGui::Text("fps/sps: %d/%.0f", fps, current_sps);
    ImGui::Text("Simulation Time: %f", float(sim_time));
    ImGui::Text("Paused: %s", world.settings().PAUSED ? "true" : "false");

    bool const taylor_green_scene = !world.settings().MPM && world.settings().FLUID_SCENE == TAYLOR_GREEN_VORTEX;
    bool const confined_vortex_scene = !world.settings().MPM && world.settings().FLUID_SCENE == CONFINED_VORTEX;
    bool const settling_pool_scene = !world.settings().MPM &&
                                     (world.settings().FLUID_SCENE == SETTLING_POOL ||
                                      world.settings().FLUID_SCENE == SETTLING_POOL_OBSTACLE);
    bool const square_no_force_scene = taylor_green_scene || confined_vortex_scene;
    bool const two_d_only_scene = square_no_force_scene || settling_pool_scene;
    bool const compressed_square_scene = world.settings().MPM && world.settings().MPM_SCENE == MPM_COMPRESSED_SQUARE;
    if (compressed_square_scene)
        apply_mpm_compressed_square_settings(world.settings());

    bool const fixed_timestep_scene = compressed_square_scene;
    if (fixed_timestep_scene)
        ImGui::BeginDisabled();
    bool adaptive_timestep = world.settings().ADAPTIVE_TIMESTEP.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Adaptive timestep", &adaptive_timestep))
        world.settings().ADAPTIVE_TIMESTEP.store(adaptive_timestep, std::memory_order_relaxed);

    float simulation_dt = static_cast<float>(world.settings().SIMULATION_DT.load(std::memory_order_relaxed));
    if (ImGui::SliderFloat("Simulation dt", &simulation_dt, 0.0001f, 0.1f, "%.5f"))
        world.settings().SIMULATION_DT.store(static_cast<double>(simulation_dt), std::memory_order_relaxed);
    if (fixed_timestep_scene)
        ImGui::EndDisabled();

    std::string is_paused_label = world.settings().PAUSED ? "Start Simulation" : "Pause Simulation";
    if (ImGui::Button(is_paused_label.c_str()))
    {
        world.settings().PAUSED = !world.settings().PAUSED;
    }
    if (ImGui::Button("Reset Simulation"))
    {
        world.settings().RESET = true;
    }

    std::string dim_label = world.settings().DIMENSION == 2 ? "Switch to 3D" : "Switch to 2D";
    if (two_d_only_scene || compressed_square_scene)
        ImGui::BeginDisabled();
    if (ImGui::Button(dim_label.c_str()))
    {
        world.settings().DIMENSION = world.settings().DIMENSION == 2 ? 3 : 2;
        world.settings().RESET = true;
    }
    if (two_d_only_scene || compressed_square_scene)
        ImGui::EndDisabled();

    std::string fluid_mpm_label = world.settings().MPM ? "Switch to Fluids" : "Switch to MPM";
    if (ImGui::Button(fluid_mpm_label.c_str()))
    {
        world.settings().MPM = !world.settings().MPM;
        if (world.settings().MPM)
        {
            world.settings().DIMENSION = 2;
            if (world.settings().FLUID_SOLVER == FLIP && world.settings().FLIP_PERCENT > 0.0f)
                world.settings().FLIP_PERCENT = 0.95f;
            if (world.settings().FLUID_SOLVER == POLYPIC)
                world.settings().POLYPIC_MODES = 9;
        }
        world.settings().RESET = true;
    }

    if (!world.settings().MPM)
    {
        bool paused = world.settings().PAUSED.load(std::memory_order_relaxed);
        float fluid_dx = static_cast<float>(world.settings().FLUID_DX.load(std::memory_order_relaxed));
        float grid_width = static_cast<float>(world.settings().GRID_WIDTH.load(std::memory_order_relaxed));
        float grid_height = static_cast<float>(world.settings().GRID_HEIGHT.load(std::memory_order_relaxed));
        if (square_no_force_scene)
            grid_height = grid_width;
        if (!paused)
            ImGui::BeginDisabled();
        if (ImGui::SliderFloat("Fluid dx", &fluid_dx, 0.25f, 4.0f, "%.2f"))
        {
            world.settings().FLUID_DX.store(static_cast<double>(fluid_dx), std::memory_order_relaxed);
            world.settings().RESET.store(true, std::memory_order_relaxed);
        }
        if (ImGui::SliderFloat("Grid width", &grid_width, 16.0f, 400.0f, "%.0f"))
        {
            world.settings().GRID_WIDTH.store(static_cast<double>(grid_width), std::memory_order_relaxed);
            if (square_no_force_scene)
                world.settings().GRID_HEIGHT.store(static_cast<double>(grid_width), std::memory_order_relaxed);
            world.settings().RESET.store(true, std::memory_order_relaxed);
        }
        if (square_no_force_scene)
        {
            ImGui::Text("Grid height: %.0f (square %s domain)", grid_height, taylor_green_scene ? "Taylor-Green" : "confined-vortex");
        }
        else if (ImGui::SliderFloat("Grid height", &grid_height, 16.0f, 400.0f, "%.0f"))
        {
            world.settings().GRID_HEIGHT.store(static_cast<double>(grid_height), std::memory_order_relaxed);
            world.settings().RESET.store(true, std::memory_order_relaxed);
        }
        if (!paused)
            ImGui::EndDisabled();

        char const* fluid_scenes[] = {
          "Random dam break",
          "Stable block",
          "Full Grid",
          "Constant Stream",
          "Taylor-Green Vortex",
          "Confined Vortex",
          "Settling Pool",
          "Settling Pool + Center Obstacle"};
        int fluid_scene = world.settings().FLUID_SCENE;
        if (ImGui::Combo("Fluid Scene", &fluid_scene, fluid_scenes, IM_ARRAYSIZE(fluid_scenes)))
        {
            world.settings().FLUID_SCENE = fluid_scene;
            if (fluid_scene == TAYLOR_GREEN_VORTEX || fluid_scene == CONFINED_VORTEX)
            {
                world.settings().DIMENSION = 2;
                world.settings().GRID_HEIGHT.store(world.settings().GRID_WIDTH.load(std::memory_order_relaxed), std::memory_order_relaxed);
                world.settings().APPLY_GRAVITY.store(false, std::memory_order_relaxed);
                world.settings().APPLY_TOP_FORCE.store(false, std::memory_order_relaxed);
            }
            else if (fluid_scene == SETTLING_POOL || fluid_scene == SETTLING_POOL_OBSTACLE)
            {
                world.settings().DIMENSION = 2;
                world.settings().APPLY_GRAVITY.store(true, std::memory_order_relaxed);
                world.settings().APPLY_TOP_FORCE.store(false, std::memory_order_relaxed);
            }
            world.settings().RESET = true;
        }

        ImGui::SeparatorText("Forces");
        if (square_no_force_scene)
        {
            world.settings().APPLY_GRAVITY.store(false, std::memory_order_relaxed);
            world.settings().APPLY_TOP_FORCE.store(false, std::memory_order_relaxed);
            ImGui::BeginDisabled();
        }
        bool apply_gravity = world.settings().APPLY_GRAVITY.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Gravity", &apply_gravity))
            world.settings().APPLY_GRAVITY.store(apply_gravity, std::memory_order_relaxed);

        bool apply_top_force = world.settings().APPLY_TOP_FORCE.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Top force", &apply_top_force))
            world.settings().APPLY_TOP_FORCE.store(apply_top_force, std::memory_order_relaxed);

        float top_force_speed = static_cast<float>(world.settings().TOP_FORCE_MAX_SPEED.load(std::memory_order_relaxed));
        if (ImGui::SliderFloat("Top speed", &top_force_speed, 0.0f, 50.0f, "%.2f"))
            world.settings().TOP_FORCE_MAX_SPEED.store(static_cast<double>(top_force_speed), std::memory_order_relaxed);
        if (square_no_force_scene)
            ImGui::EndDisabled();

    }

    if (world.settings().MPM)
    {
        char const* mpm_scenes[] = {"Empty / manual", "Compressed square (PolyPIC justification)"};
        int mpm_scene = world.settings().MPM_SCENE;
        if (ImGui::Combo("MPM Scene", &mpm_scene, mpm_scenes, IM_ARRAYSIZE(mpm_scenes)))
        {
            world.settings().MPM_SCENE = mpm_scene;
            if (mpm_scene == MPM_COMPRESSED_SQUARE)
                apply_mpm_compressed_square_settings(world.settings());
            world.settings().RESET = true;
        }

        if (world.settings().MPM_SCENE == MPM_EMPTY && ImGui::Button("Add Quad"))
            grid.add_quad(50.0, 50.0, 5.0, 5.0);

        if (world.settings().MPM_SCENE == MPM_COMPRESSED_SQUARE)
        {
            ImGui::Text("64x64, dt=0.0005, gravity off");
            ImGui::Text("576 particles, initial F_E = 0.9 I");
        }
    }

    if (world.settings().DIMENSION == 2)
        ImGui::Checkbox("Render particle mesh", &world.settings().RENDER_PARTICLE_MESH_2D);

    if (world.settings().MPM)
    {
        int transfer_method = 0;
        if (world.settings().FLUID_SOLVER == FLIP)
            transfer_method = world.settings().FLIP_PERCENT <= 0.0f ? 0 : 1;
        else if (world.settings().FLUID_SOLVER == APIC)
            transfer_method = 2;
        else
            transfer_method = 3;

        char const* transfer_methods[] = {"PIC", "FLIP 0.95", "APIC", "PolyPIC"};
        if (ImGui::Combo("MPM Transfer", &transfer_method, transfer_methods, IM_ARRAYSIZE(transfer_methods)))
        {
            if (transfer_method == 0)
            {
                world.settings().FLUID_SOLVER = FLIP;
                world.settings().FLIP_PERCENT = 0.0f;
            }
            else if (transfer_method == 1)
            {
                world.settings().FLUID_SOLVER = FLIP;
                world.settings().FLIP_PERCENT = 0.95f;
            }
            else if (transfer_method == 2)
            {
                world.settings().FLUID_SOLVER = APIC;
            }
            else
            {
                world.settings().FLUID_SOLVER = POLYPIC;
                world.settings().POLYPIC_MODES = 9;
            }

            world.settings().RESET = true;
        }

        if (world.settings().FLUID_SOLVER == POLYPIC)
            polypic_mode_selector_2d(world.settings(), true);
    }
    else
    {
        char const* fluid_solvers[] = {"FLIP", "APIC", "POLYPIC"};
        int fluid_solver = world.settings().FLUID_SOLVER;
        if (ImGui::Combo("Fluid Solver", &fluid_solver, fluid_solvers, IM_ARRAYSIZE(fluid_solvers)))
            world.settings().FLUID_SOLVER = fluid_solver;

        if (world.settings().FLUID_SOLVER == FLIP)
            ImGui::SliderFloat("FLIP %", &world.settings().FLIP_PERCENT, 0.0, 1.0);
        if (world.settings().FLUID_SOLVER == POLYPIC)
        {
            if (world.settings().DIMENSION == 3)
            {
                // 3D keeps the full Nr slider (1..8); only 1/4/8 are PIC/APIC/PolyPIC (MAS-29).
                int const max_modes = polypic_max_modes_for_dimension(3);
                int modes = std::clamp(world.settings().POLYPIC_MODES, 1, max_modes);
                if (modes != world.settings().POLYPIC_MODES)
                    world.settings().POLYPIC_MODES = modes;
                if (ImGui::SliderInt("PolyPIC modes (Nr)", &modes, 1, max_modes))
                    world.settings().POLYPIC_MODES = modes;
                ImGui::Text("1=PIC  4=APIC  8=PolyPIC");
            }
            else
            {
                polypic_mode_selector_2d(world.settings());
            }
        }
    }

    ImGui::End();

    ImGui::Begin("Scene");
    auto cam_entity = world.get_entity("Camera");
    auto* cam_tf    = world.get_component<Transform_Component>(cam_entity);
    if (cam_tf)
        ImGui::Text("Camera  (%.2f, %.2f, %.2f)", cam_tf->position.x, cam_tf->position.y, cam_tf->position.z);
    ImGui::End();

    ImGui::Begin("Diagnostics");

    bool diagnostics_enabled = diagnostics.enabled();
    if (ImGui::Checkbox("Enable diagnostics", &diagnostics_enabled))
        diagnostics.set_enabled(diagnostics_enabled);

    bool pause_on_invalid = diagnostics.pause_on_invalid();
    if (ImGui::Checkbox("Pause on invalid state", &pause_on_invalid))
        diagnostics.set_pause_on_invalid(pause_on_invalid);

    bool pause_on_cfl = diagnostics.pause_on_cfl();
    if (ImGui::Checkbox("Pause on CFL threshold", &pause_on_cfl))
        diagnostics.set_pause_on_cfl(pause_on_cfl);

    float cfl_threshold = static_cast<float>(diagnostics.cfl_pause_threshold());
    if (ImGui::SliderFloat("CFL threshold", &cfl_threshold, 0.1f, 5.0f))
        diagnostics.set_cfl_pause_threshold(cfl_threshold);

    auto samples = diagnostics.samples();
    auto latest = diagnostics.latest_sample();

    ImGui::SeparatorText("Current");
    ImGui::Text("Samples: %zu", samples.size());
    ImGui::Text("Step: %llu", static_cast<unsigned long long>(latest.step));
    ImGui::Text("Step dt / frame dt: %.5f / %.5f", latest.dt, latest.frame_dt);
    ImGui::Text("Solver substeps: %d", latest.solver_substeps);
    ImGui::Text("Particles: %zu", latest.particle_count);
    ImGui::Text("Max speed: %.4f", latest.max_speed);
    ImGui::Text("Mean / RMS speed: %.4f / %.4f", latest.mean_speed, latest.rms_speed);
    ImGui::Text("Total mass: %.4f", latest.total_mass);
    ImGui::Text("Fluid cells: %zu", latest.fluid_cell_count);
    if (latest.fluid_measure_dimension == 2)
        ImGui::Text("Fluid area / unit-depth volume: %.4f / %.4f", latest.fluid_area_estimate, latest.fluid_volume_estimate);
    else if (latest.fluid_measure_dimension == 3)
        ImGui::Text("Fluid volume: %.4f", latest.fluid_volume_estimate);
    else
        ImGui::Text("Fluid volume: unavailable for this grid");
    ImGui::Text("Kinetic energy: %.4f", latest.kinetic_energy);
    ImGui::Text("Potential energy: %.4f", latest.potential_energy);
    ImGui::Text("Total energy: %.4f", latest.total_energy);
    ImGui::Text("Angular momentum orbital / represented (z): %.6e / %.6e",
                latest.orbital_angular_momentum.z, latest.represented_angular_momentum.z);
    ImGui::Text("Max PolyPIC coeff: %.4e", latest.max_poly_coeff);
    ImGui::SeparatorText("Fluid stages");
    ImGui::Text("Particle energy before/G2P/advect: %.4f / %.4f / %.4f",
                latest.stages.before_step_particle_energy,
                latest.stages.after_grid_to_particle_particle_energy,
                latest.stages.after_advect_particle_energy);
    ImGui::Text("Angular momentum before/P2G/projection/extrapolate/G2P/advect (z):");
    ImGui::Text("%.6e / %.6e / %.6e / %.6e / %.6e / %.6e",
                latest.stages.before_step_particle_angular_momentum.z,
                latest.stages.after_particle_to_grid_grid_angular_momentum.z,
                latest.stages.after_projection_grid_angular_momentum.z,
                latest.stages.after_second_extrapolate_grid_angular_momentum.z,
                latest.stages.after_grid_to_particle_particle_angular_momentum.z,
                latest.stages.after_advect_particle_angular_momentum.z);
    ImGui::Text("P2G grid max/energy: %.4f / %.4f",
                latest.stages.after_particle_to_grid_max_grid_speed,
                latest.stages.after_particle_to_grid_grid_kinetic_energy);
    ImGui::Text("Forces grid max/energy: %.4f / %.4f",
                latest.stages.after_forces_max_grid_speed,
                latest.stages.after_forces_grid_kinetic_energy);
    ImGui::Text("Extrapolate 1 grid max/energy: %.4f / %.4f",
                latest.stages.after_first_extrapolate_max_grid_speed,
                latest.stages.after_first_extrapolate_grid_kinetic_energy);
    ImGui::Text("Boundary grid max/energy: %.4f / %.4f",
                latest.stages.after_boundary_max_grid_speed,
                latest.stages.after_boundary_grid_kinetic_energy);
    ImGui::Text("Projection grid max/energy: %.4f / %.4f",
                latest.stages.after_projection_max_grid_speed,
                latest.stages.after_projection_grid_kinetic_energy);
    ImGui::Text("Extrapolate 2 grid max/energy: %.4f / %.4f",
                latest.stages.after_second_extrapolate_max_grid_speed,
                latest.stages.after_second_extrapolate_grid_kinetic_energy);
    ImGui::Text("G2P particle max: %.4f", latest.stages.after_grid_to_particle_max_particle_speed);
    ImGui::Text("Advect particle max: %.4f", latest.stages.after_advect_max_particle_speed);
    ImGui::Text("Divergence max before/after projection: %.4f / %.4f",
                latest.stages.before_projection_max_divergence,
                latest.stages.after_projection_max_divergence);
    ImGui::Text("Divergence RMS before/after projection: %.4f / %.4f",
                latest.stages.before_projection_rms_divergence,
                latest.stages.after_projection_rms_divergence);
    ImGui::Text("Pressure cells/iters/converged: %d / %d / %s",
                latest.stages.projection.fluid_cell_count,
                latest.stages.projection.iterations,
                latest.stages.projection.converged ? "true" : "false");
    ImGui::Text("Pressure residual initial/final/ratio: %.4e / %.4e / %.4e",
                latest.stages.projection.initial_residual,
                latest.stages.projection.final_residual,
                latest.stages.projection.residual_ratio);
    ImGui::Text("Pressure min/max/mean: %.4f / %.4f / %.4f",
                latest.stages.projection.pressure_min,
                latest.stages.projection.pressure_max,
                latest.stages.projection.pressure_mean);
    ImGui::Text("Pressure grad / velocity delta max: %.4f / %.4f",
                latest.stages.projection.max_pressure_gradient,
                latest.stages.projection.max_velocity_delta);
    ImGui::SeparatorText("CFL");
    ImGui::Text("CFL: %.4f", latest.cfl);
    ImGui::Text("CFL particle: #%zu / entity %u", latest.cfl_particle_index, latest.cfl_particle_entity);
    ImGui::Text("CFL particle pos: (%.3f, %.3f, %.3f)", latest.cfl_particle_position.x, latest.cfl_particle_position.y, latest.cfl_particle_position.z);
    ImGui::Text("CFL particle vel: (%.3f, %.3f, %.3f)", latest.cfl_particle_velocity.x, latest.cfl_particle_velocity.y, latest.cfl_particle_velocity.z);
    ImGui::Text("CFL particle near boundary: %s", latest.cfl_particle_near_boundary ? "true" : "false");
    ImGui::Text("CFL boundary distance: %.3f", latest.cfl_particle_boundary_distance);
    ImGui::Text("Boundary collisions: %d", latest.boundary_collision_count);
    ImGui::Text("CFL particle hit boundary: %s", latest.cfl_particle_boundary_collision ? "true" : "false");
    if (latest.cfl_particle_boundary_collision)
    {
        ImGui::Text("Boundary cell: (%d, %d, %d)", latest.cfl_boundary_cell.x, latest.cfl_boundary_cell.y, latest.cfl_boundary_cell.z);
        ImGui::Text("Boundary normal: (%.3f, %.3f, %.3f)", latest.cfl_boundary_normal.x, latest.cfl_boundary_normal.y, latest.cfl_boundary_normal.z);
        ImGui::Text("Velocity before: (%.3f, %.3f, %.3f)", latest.cfl_boundary_velocity_before.x, latest.cfl_boundary_velocity_before.y, latest.cfl_boundary_velocity_before.z);
        ImGui::Text("Velocity after:  (%.3f, %.3f, %.3f)", latest.cfl_boundary_velocity_after.x, latest.cfl_boundary_velocity_after.y, latest.cfl_boundary_velocity_after.z);
        ImGui::Text("Speed before/after: %.3f / %.3f", latest.cfl_boundary_speed_before, latest.cfl_boundary_speed_after);
        ImGui::Text("Normal velocity before/after: %.3f / %.3f", latest.cfl_boundary_normal_velocity_before, latest.cfl_boundary_normal_velocity_after);
        ImGui::Text("Boundary impulse: %.3f", latest.cfl_boundary_impulse);
    }
    ImGui::Text("Invalid / OOB: %d / %d", latest.invalid_count, latest.out_of_bounds_count);
    ImGui::Text("Center of mass: (%.3f, %.3f, %.3f)", latest.center_of_mass.x, latest.center_of_mass.y, latest.center_of_mass.z);

    ImGui::SeparatorText("Rolling plots");
    plot_sample_metric("Kinetic energy", samples, [](Diagnostics_Sample const& s) { return s.kinetic_energy; });
    plot_sample_metric("Total energy", samples, [](Diagnostics_Sample const& s) { return s.total_energy; });
    plot_sample_metric("Fluid volume", samples, [](Diagnostics_Sample const& s) { return s.fluid_volume_estimate; });
    plot_sample_metric("Max speed", samples, [](Diagnostics_Sample const& s) { return s.max_speed; });
    plot_sample_metric("CFL", samples, [](Diagnostics_Sample const& s) { return s.cfl; });
    plot_sample_metric("Invalid count", samples, [](Diagnostics_Sample const& s) { return s.invalid_count; });

    ImGui::SeparatorText("Particle inspector");
    auto selected = diagnostics.selected_particle();
    if (selected.valid_selection)
    {
        ImGui::Text("Index / entity: %zu / %u", selected.index, selected.entity);
        ImGui::Text("Position: (%.4f, %.4f, %.4f)", selected.position.x, selected.position.y, selected.position.z);
        ImGui::Text("Velocity: (%.4f, %.4f, %.4f)", selected.velocity.x, selected.velocity.y, selected.velocity.z);
        ImGui::Text("Speed: %.4f", selected.speed);
        ImGui::Text("Radius: %.4f", selected.radius);
        ImGui::Text("Nearest cell: (%d, %d, %d)", selected.nearest_cell.x, selected.nearest_cell.y, selected.nearest_cell.z);
        ImGui::Text("Boundary distance: %.4f", selected.distance_to_boundary);
        ImGui::Text("Finite / OOB: %s / %s", selected.finite ? "true" : "false", selected.out_of_bounds ? "true" : "false");
        if (world.settings().DIMENSION == 3)
        {
            ImGui::Text("APIC c_u: (%.4f, %.4f, %.4f)", selected.c_u_3d.x, selected.c_u_3d.y, selected.c_u_3d.z);
            ImGui::Text("APIC c_v: (%.4f, %.4f, %.4f)", selected.c_v_3d.x, selected.c_v_3d.y, selected.c_v_3d.z);
            ImGui::Text("APIC c_w: (%.4f, %.4f, %.4f)", selected.c_w_3d.x, selected.c_w_3d.y, selected.c_w_3d.z);
        }
        else
        {
            ImGui::Text("APIC c_u: (%.4f, %.4f)", selected.c_u.x, selected.c_u.y);
            ImGui::Text("APIC c_v: (%.4f, %.4f)", selected.c_v.x, selected.c_v.y);
        }
        if (world.settings().FLUID_SOLVER == POLYPIC)
        {
            if (world.settings().DIMENSION == 3)
            {
                char const* mode_labels[] = {"1", "x", "y", "z", "xy", "xz", "yz", "xyz"};
                for (int r = 0; r < POLYPIC_MAX_MODES_3D; ++r)
                {
                    glm::dvec3 const& c = selected.poly_c_3d[r];
                    ImGui::Text("PolyPIC c[%d] (%s): (%.4f, %.4f, %.4f)", r, mode_labels[r], c.x, c.y, c.z);
                }
            }
            else
            {
                ImGui::Text("PolyPIC c[0] (1):  (%.4f, %.4f)", selected.poly_c[0].x, selected.poly_c[0].y);
                ImGui::Text("PolyPIC c[1] (x):  (%.4f, %.4f)", selected.poly_c[1].x, selected.poly_c[1].y);
                ImGui::Text("PolyPIC c[2] (y):  (%.4f, %.4f)", selected.poly_c[2].x, selected.poly_c[2].y);
                ImGui::Text("PolyPIC c[3] (xy): (%.4f, %.4f)", selected.poly_c[3].x, selected.poly_c[3].y);
            }
        }
        if (ImGui::Button("Clear selection"))
            diagnostics.clear_selection();
    }
    else
    {
        ImGui::Text("Left-click a particle in the scene to inspect it.");
    }

    ImGui::SeparatorText("Export");
    if (ImGui::Button("Export CSV"))
        diagnostics.export_csv(world, grid, sim_time);

    std::string status = diagnostics.export_status();
    if (!status.empty())
        ImGui::TextWrapped("%s", status.c_str());

    ImGui::End();
}

void Dev_UI::Render()
{
    // Render dear imgui into screen
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_current_context);
    }
}

void Dev_UI::Shutdown()
{
    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
