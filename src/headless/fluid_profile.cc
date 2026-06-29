#include "../diagnostics.hh"
#include "../particle_frame_export.hh"
#include "../profile_timer.hh"
#include "../simulation/fluid_solver.hh"
#include "../simulation/fluid_solver_3d.hh"
#include "../simulation/core/timestep_controller.hh"
#include "../simulation/fluid_kernels/polypic.hh"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

namespace
{
constexpr std::uint32_t DEFAULT_HEADLESS_JITTER_SEED = 0x44414d42u; // "DAMB"

struct Profile_Config
{
    int dimension = 3;
    int steps = 20;
    int domain = 100;
    double dx = 1.0;
    double frame_dt = 1.0 / 60.0;
    int scene = 0;
    int fluid_solver = FLIP;
    int polypic_modes = POLYPIC_MAX_MODES;
    std::uint32_t jitter_seed = DEFAULT_HEADLESS_JITTER_SEED;
    float flip_percent = 0.95f;
    bool adaptive = true;
    bool diagnostics = true;
    bool export_diagnostics = false;
    bool export_particles = false;
    double particle_fps = 27.0;
    std::string out_dir;
    std::string particle_out_dir;
};

char const* solver_name(int solver)
{
    return solver == POLYPIC ? "polypic" : solver == APIC ? "apic" : "flip";
}

void print_usage(char const* program)
{
    std::cout << "Usage:\n"
              << "  " << program << " [--dim 2|3] [--steps N] [--domain N] [--dx X] [--dt X]\n"
              << "  " << program << " [--scene 0..7] [--solver pic|flip|apic|polypic] [--polypic-modes N]\n"
              << "  " << program << " [--apic] [--flip-percent X] [--jitter-seed N] [--no-adaptive]\n"
              << "  " << program << " [--no-diagnostics] [--export] [--export-particles] [--particle-fps X] [--out DIR] [--particle-out DIR]\n"
              << "  (--polypic-modes is 1..4 in 2D and 1..8 in 3D)\n"
              << "  (--export-particles writes particle_frames.bin under --particle-out, or --out when omitted)\n"
              << "  (--solver pic == flip with alpha 0; --apic is a compatibility alias for --solver apic)\n";
}

int parse_int(char const* value, char const* name)
{
    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (!end || *end != '\0')
        throw std::runtime_error(std::string("Invalid integer for ") + name);
    return static_cast<int>(parsed);
}

std::uint32_t parse_uint32(char const* value, char const* name)
{
    int parsed = parse_int(value, name);
    if (parsed < 0)
        throw std::runtime_error(std::string("Invalid unsigned integer for ") + name);
    return static_cast<std::uint32_t>(parsed);
}

double parse_double(char const* value, char const* name)
{
    char* end = nullptr;
    double parsed = std::strtod(value, &end);
    if (!end || *end != '\0')
        throw std::runtime_error(std::string("Invalid number for ") + name);
    return parsed;
}

Profile_Config parse_args(int argc, char** argv)
{
    Profile_Config config;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        auto require_value = [&](char const* name) -> char const*
        {
            if (i + 1 >= argc)
                throw std::runtime_error(std::string("Missing value for ") + name);
            return argv[++i];
        };

        if (arg == "--dim")
            config.dimension = parse_int(require_value("--dim"), "--dim");
        else if (arg == "--steps")
            config.steps = parse_int(require_value("--steps"), "--steps");
        else if (arg == "--domain")
            config.domain = parse_int(require_value("--domain"), "--domain");
        else if (arg == "--dx")
            config.dx = parse_double(require_value("--dx"), "--dx");
        else if (arg == "--dt")
            config.frame_dt = parse_double(require_value("--dt"), "--dt");
        else if (arg == "--scene")
            config.scene = parse_int(require_value("--scene"), "--scene");
        else if (arg == "--solver")
        {
            std::string solver = require_value("--solver");
            if (solver == "pic")
            {
                config.fluid_solver = FLIP;
                config.flip_percent = 0.0f; // PIC == FLIP with alpha 0
            }
            else if (solver == "flip")
                config.fluid_solver = FLIP;
            else if (solver == "apic")
                config.fluid_solver = APIC;
            else if (solver == "polypic")
                config.fluid_solver = POLYPIC;
            else
                throw std::runtime_error("--solver must be pic, flip, apic, or polypic");
        }
        else if (arg == "--polypic-modes")
            config.polypic_modes = parse_int(require_value("--polypic-modes"), "--polypic-modes");
        else if (arg == "--jitter-seed")
            config.jitter_seed = parse_uint32(require_value("--jitter-seed"), "--jitter-seed");
        else if (arg == "--apic")
            config.fluid_solver = APIC; // backward-compatible alias for --solver apic
        else if (arg == "--flip-percent")
            config.flip_percent = static_cast<float>(parse_double(require_value("--flip-percent"), "--flip-percent"));
        else if (arg == "--no-adaptive")
            config.adaptive = false;
        else if (arg == "--no-diagnostics")
            config.diagnostics = false;
        else if (arg == "--export")
            config.export_diagnostics = true;
        else if (arg == "--export-particles")
            config.export_particles = true;
        else if (arg == "--particle-fps")
            config.particle_fps = parse_double(require_value("--particle-fps"), "--particle-fps");
        else if (arg == "--out")
            config.out_dir = require_value("--out");
        else if (arg == "--particle-out")
            config.particle_out_dir = require_value("--particle-out");
        else if (arg == "--help" || arg == "-h")
        {
            print_usage(argv[0]);
            std::exit(0);
        }
        else
        {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (config.dimension != 2 && config.dimension != 3)
        throw std::runtime_error("--dim must be 2 or 3");
    if (config.scene < DAM_BREAK || config.scene > SETTLING_POOL_OBSTACLE)
        throw std::runtime_error("--scene must be in [0, 7]");
    if (config.scene == TAYLOR_GREEN_VORTEX && config.dimension != 2)
        throw std::runtime_error("Taylor-Green vortex scene requires --dim 2");
    if (config.scene == CONFINED_VORTEX && config.dimension != 2)
        throw std::runtime_error("Confined vortex scene requires --dim 2");
    if ((config.scene == SETTLING_POOL || config.scene == SETTLING_POOL_OBSTACLE) && config.dimension != 2)
        throw std::runtime_error("Settling pool scene requires --dim 2");
    if (config.steps <= 0 || config.domain <= 0 || config.dx <= 0.0 || config.frame_dt <= 0.0)
        throw std::runtime_error("steps, domain, dx, and dt must be positive");
    if (config.flip_percent < 0.0f || config.flip_percent > 1.0f)
        throw std::runtime_error("--flip-percent must be in [0, 1]");
    int const max_polypic_modes = polypic_max_modes_for_dimension(config.dimension);
    if (config.polypic_modes < 1 || config.polypic_modes > max_polypic_modes)
    {
        throw std::runtime_error("--polypic-modes must be in [1, " + std::to_string(max_polypic_modes)
                                 + "] for --dim " + std::to_string(config.dimension));
    }
    if (config.export_diagnostics && !config.diagnostics)
        throw std::runtime_error("--export requires diagnostics to be enabled");
    if (config.export_particles && config.out_dir.empty() && config.particle_out_dir.empty())
        throw std::runtime_error("--export-particles requires --particle-out DIR or --out DIR");
    if (config.particle_fps <= 0.0)
        throw std::runtime_error("--particle-fps must be positive");

    return config;
}

void configure_world(World& world, Profile_Config const& config)
{
    ecs::Entity camera = world.create_entity("Camera");
    world.add_component(camera, Transform_Component{});

    world.settings().FLUID_SOLVER = config.fluid_solver;
    world.settings().POLYPIC_MODES = config.polypic_modes;
    world.settings().FLIP_PERCENT = config.flip_percent;
    world.settings().ADAPTIVE_TIMESTEP = config.adaptive;
    world.settings().SIMULATION_DT = config.frame_dt;
    world.settings().FLUID_DX = config.dx;
    world.settings().GRID_WIDTH = static_cast<double>(config.domain);
    world.settings().GRID_HEIGHT = static_cast<double>(config.domain);
    world.settings().PAUSED = false;
    world.settings().RESET = false;
    world.settings().MPM = false;
    world.settings().DIMENSION = config.dimension;
    world.settings().FLUID_SCENE = config.scene;
    world.settings().FLUID_JITTER_SEED = config.jitter_seed;
    world.settings().CREATE_BOUNDARY_VISUALS = false;
}

std::unique_ptr<Simulation> make_simulation(World& world, Profile_Config const& config)
{
    if (config.dimension == 3)
        return std::make_unique<Fluid_Solver_3D>(config.dx, config.domain, config.domain, config.domain, world, config.frame_dt);

    return std::make_unique<Fluid_Solver_2D>(config.dx, config.domain, config.domain, world, config.frame_dt);
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Profile_Config config = parse_args(argc, argv);

        World world;
        configure_world(world, config);
        auto simulation = make_simulation(world, config);

        Diagnostics diagnostics;
        Timestep_Controller timestep_controller;
        Fluid_Profile::reset();

        std::unique_ptr<Particle_Frame_Writer> particle_writer;
        Particle_Frame_Snapshot previous_particles;
        double next_particle_frame_time = 0.0;
        if (config.export_particles)
        {
            Particle_Frame_Export_Config export_config{
              .dimension = config.dimension,
              .scene = config.scene,
              .solver = config.fluid_solver,
              .polypic_modes = config.polypic_modes,
              .flip_percent = config.flip_percent,
              .fps = config.particle_fps,
              .domain_width = static_cast<double>(config.domain),
              .domain_height = static_cast<double>(config.domain),
              .domain_depth = config.dimension == 3 ? static_cast<double>(config.domain) : 0.0,
              .dx = config.dx,
            };
            std::filesystem::path particle_directory = config.particle_out_dir.empty() ? config.out_dir : config.particle_out_dir;
            std::filesystem::path particle_path = particle_directory / "particle_frames.bin";
            particle_writer = std::make_unique<Particle_Frame_Writer>(particle_path, export_config);
            previous_particles = capture_particle_frame(world);
            particle_writer->write_frame(previous_particles, 0.0, 0.0);
            next_particle_frame_time = 1.0 / config.particle_fps;
        }

        std::uint64_t diagnostics_step = 0;
        double sim_time = 0.0;
        auto wall_start = std::chrono::steady_clock::now();
        int simulated_substeps = 0;

        for (int frame = 0; frame < config.steps; ++frame)
        {
            Timestep_Decision timestep = timestep_controller.decide(world, simulation->grid(), config.frame_dt);
            for (int substep = 0; substep < timestep.substeps; ++substep)
            {
                double const previous_sim_time = sim_time;
                simulation->step(timestep.sub_dt);
                ++simulated_substeps;
                sim_time += timestep.sub_dt;

                if (config.diagnostics)
                {
                    diagnostics.record_step(world, simulation->grid(), simulation->stage_diagnostics(), diagnostics_step++, sim_time,
                                            timestep.sub_dt, timestep.substeps, timestep.frame_dt);
                }

                if (particle_writer)
                {
                    Particle_Frame_Snapshot current_particles = capture_particle_frame(world);
                    while (next_particle_frame_time <= sim_time + 1.0e-12)
                    {
                        double const alpha = timestep.sub_dt > 0.0 ? (next_particle_frame_time - previous_sim_time) / timestep.sub_dt : 1.0;
                        particle_writer->write_interpolated_frame(
                            previous_particles, current_particles, alpha, next_particle_frame_time, sim_time);
                        next_particle_frame_time += 1.0 / config.particle_fps;
                    }
                    previous_particles = std::move(current_particles);
                }
            }
        }

        if (particle_writer)
            particle_writer->close();

        auto wall_end = std::chrono::steady_clock::now();
        double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

        std::cout << "Fluid simulation profile\n"
                  << "dimension=" << config.dimension << '\n'
                  << "steps=" << config.steps << '\n'
                  << "simulated_substeps=" << simulated_substeps << '\n'
                  << "domain=" << config.domain << '\n'
                  << "dx=" << config.dx << '\n'
                  << "frame_dt=" << config.frame_dt << '\n'
                  << "scene=" << config.scene << '\n'
                  << "fluid_solver=" << solver_name(config.fluid_solver) << '\n'
                  << "polypic_modes=" << config.polypic_modes << '\n'
                  << "jitter_seed=" << config.jitter_seed << '\n'
                  << "flip_percent=" << config.flip_percent << '\n'
                  << "adaptive=" << (config.adaptive ? "true" : "false") << '\n'
                  << "diagnostics=" << (config.diagnostics ? "true" : "false") << '\n'
                  << "particle_export=" << (config.export_particles ? "true" : "false") << '\n'
                  << "particle_fps=" << config.particle_fps << '\n'
                  << "particle_frames=" << (particle_writer ? particle_writer->frame_count() : 0) << '\n'
                  << "particles=" << world.get_array<Particle_Component>().data().size() << '\n'
                  << "wall_ms=" << std::fixed << std::setprecision(3) << wall_ms << "\n\n";

        std::filesystem::path export_path;
        if (config.export_diagnostics)
        {
            bool exported = config.out_dir.empty()
                                ? diagnostics.export_csv(world, simulation->grid(), sim_time)
                                : diagnostics.export_csv_to(world, simulation->grid(), sim_time, config.out_dir);
            std::cout << diagnostics.export_status() << '\n';
            if (!exported)
                return 1;

            export_path = diagnostics.last_export_path();
        }
        else if (config.export_particles && config.particle_out_dir.empty())
        {
            export_path = config.out_dir;
        }

        if (!export_path.empty())
        {
            std::ofstream profile_notes(export_path / "profile.txt");
            profile_notes << "Fluid profile run\n";
            profile_notes << "dimension=" << config.dimension << '\n';
            profile_notes << "steps=" << config.steps << '\n';
            profile_notes << "simulated_substeps=" << simulated_substeps << '\n';
            profile_notes << "domain=" << config.domain << '\n';
            profile_notes << "dx=" << config.dx << '\n';
            profile_notes << "frame_dt=" << config.frame_dt << '\n';
            profile_notes << "scene=" << config.scene << '\n';
            profile_notes << "fluid_solver=" << solver_name(config.fluid_solver) << '\n';
            profile_notes << "polypic_modes=" << config.polypic_modes << '\n';
            profile_notes << "jitter_seed=" << config.jitter_seed << '\n';
            profile_notes << "flip_percent=" << config.flip_percent << '\n';
            profile_notes << "adaptive=" << (config.adaptive ? "true" : "false") << '\n';
            profile_notes << "diagnostics=" << (config.diagnostics ? "true" : "false") << '\n';
            profile_notes << "particle_export=" << config.export_particles << '\n';
            profile_notes << "particle_fps=" << config.particle_fps << '\n';
            profile_notes << "particle_frames=" << (particle_writer ? particle_writer->frame_count() : 0) << '\n';
            if (particle_writer)
                profile_notes << "particle_file=" << particle_writer->path().string() << '\n';
            profile_notes << "particles=" << world.get_array<Particle_Component>().data().size() << '\n';
            profile_notes << "wall_ms=" << std::fixed << std::setprecision(3) << wall_ms << '\n';
        }

        Fluid_Profile::print_report(std::cout, 60);
    }
    catch (std::exception const& e)
    {
        std::cerr << "profile error: " << e.what() << '\n';
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}
