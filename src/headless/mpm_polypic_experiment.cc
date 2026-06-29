// MPM PolyPIC compressed-cube experiment (one method per invocation).
//
// Runs the initially-compressed hyperelastic square (the paper's Fig.10 scene) for
// a single transfer method and writes:
//   - diagnostics.csv     : energy / extent / mode-amplitude time series for plots
//   - particle_frames.bin : FLDPART1 export for video + snapshot rendering
//   - run.json            : config + final metrics
// The Python orchestrator (scripts/run_mpm_polypic_experiments.py) fans these out
// across methods in parallel, renders media, makes the comparison plots, then
// deletes the bulky particle_frames.bin.

#include "../particle_frame_export.hh"
#include "../simulation/core/helpers.hh"
#include "../simulation/fluid_kernels/polypic.hh"
#include "../simulation/mpm.hh"
#include "../simulation/mpm_kernels/collocated_grid.hh"
#include "../ecs/particle_component.hh"
#include "../world.hh"

#include <Eigen/Dense>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
struct Config
{
    std::string out_dir;
    int solver = POLYPIC;
    int nr = 9;
    float flip = 0.95f;
    double dt = 5.0e-4;
    int steps = 10000; // dt*steps = 5.0 s end time
    double particle_fps = 60.0;
    double reg = MPM_POLYPIC_DEFAULT_QUAD_REG;
    int domain = 64;
    double dx = 1.0;
    int square_cells = 12;
    int ppc = 2;
    double compression = 0.90;
    bool gravity = false;
    int diag_samples = 600; // number of CSV rows (downsampled from steps)
    bool diagnostics = true;
    bool export_particles = true;
};

int parse_solver(std::string const& s)
{
    if (s == "PIC" || s == "pic") return FLIP; // PIC = FLIP with flip=0
    if (s == "FLIP" || s == "flip") return FLIP;
    if (s == "APIC" || s == "apic") return APIC;
    if (s == "POLYPIC" || s == "polypic") return POLYPIC;
    throw std::runtime_error("unknown solver: " + s);
}

Config parse_args(int argc, char** argv)
{
    Config c;
    auto need = [&](int& i) -> std::string {
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + std::string(argv[i]));
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--out") c.out_dir = need(i);
        else if (a == "--solver") c.solver = parse_solver(need(i));
        else if (a == "--nr") c.nr = std::stoi(need(i));
        else if (a == "--flip") c.flip = std::stof(need(i));
        else if (a == "--dt") c.dt = std::stod(need(i));
        else if (a == "--steps") c.steps = std::stoi(need(i));
        else if (a == "--particle-fps") c.particle_fps = std::stod(need(i));
        else if (a == "--reg") c.reg = std::stod(need(i));
        else if (a == "--domain") c.domain = std::stoi(need(i));
        else if (a == "--dx") c.dx = std::stod(need(i));
        else if (a == "--square-cells") c.square_cells = std::stoi(need(i));
        else if (a == "--ppc") c.ppc = std::stoi(need(i));
        else if (a == "--compression") c.compression = std::stod(need(i));
        else if (a == "--gravity") c.gravity = true;
        else if (a == "--diag-samples") c.diag_samples = std::stoi(need(i));
        else if (a == "--export") c.diagnostics = true;
        else if (a == "--no-diagnostics") c.diagnostics = false;
        else if (a == "--export-particles") c.export_particles = true;
        else if (a == "--no-particles") c.export_particles = false;
        else throw std::runtime_error("unknown argument: " + a);
    }
    if (c.out_dir.empty()) throw std::runtime_error("--out DIR is required");
    if (c.particle_fps <= 0.0) throw std::runtime_error("--particle-fps must be positive");
    if (c.domain <= 0 || c.dx <= 0.0 || c.dt <= 0.0 || c.steps <= 0)
        throw std::runtime_error("domain, dx, dt, and steps must be positive");
    if (c.diag_samples <= 0) throw std::runtime_error("--diag-samples must be positive");
    return c;
}

double grid_kinetic_energy(Collocated_Grid& grid)
{
    double e = 0.0;
    for (int j = 0; j < grid.ny(); ++j)
        for (int i = 0; i < grid.nx(); ++i)
            e += 0.5 * grid.cell_mass(i, j) * glm::dot(grid.cell_velocity(i, j), grid.cell_velocity(i, j));
    return e;
}

double elastic_potential_energy(World& world, std::vector<ecs::Entity> const& particles)
{
    double energy = 0.0;
    for (ecs::Entity const p : particles)
    {
        auto dc = world.get_component<Deformable_Component>(p);
        Eigen::Matrix2d F = Fluid_Simulation_2D::glm_to_eigen(dc->F_E);
        Eigen::JacobiSVD<Eigen::Matrix2d> svd(F, Eigen::ComputeFullU | Eigen::ComputeFullV);
        Eigen::Matrix2d R = svd.matrixU() * svd.matrixV().transpose();
        double J = Fluid_Simulation_2D::determinant_2D(dc->F_E);
        double psi = dc->mu * (F - R).squaredNorm() + 0.5 * dc->lambda * (J - 1.0) * (J - 1.0);
        energy += dc->V_0 * psi;
    }
    return energy;
}

double max_abs(glm::dvec2 v) { return std::max(std::abs(v.x), std::abs(v.y)); }

struct Row
{
    int step = 0;
    double dt = 0.0;
    double time, ke, pe, total, norm, width, height, max_speed;
    double rms_linear, rms_bilinear, rms_quadratic, max_coeff;
    std::size_t particle_count = 0;
    double total_mass = 0.0;
    double particle_ke = 0.0;
    double residual_rms = 0.0;
    double residual_energy_per_mass = 0.0;
    double residual_energy_fraction = 0.0;
    glm::dvec2 center_of_mass{0.0};
    glm::dvec2 total_momentum{0.0};
    double min_det_f = std::numeric_limits<double>::max();
    double max_det_f = std::numeric_limits<double>::lowest();
    std::size_t invalid_count = 0;
    std::size_t out_of_bounds_count = 0;
};

Row sample(World& world, Collocated_Grid& grid, int step, double dt, double time, double e0)
{
    Row r{};
    r.step = step;
    r.dt = dt;
    r.time = time;
    r.ke = grid_kinetic_energy(grid);
    r.pe = elastic_potential_energy(world, grid.particles());
    r.total = r.ke + r.pe;
    r.norm = e0 > 0.0 ? r.total / e0 : 0.0;

    glm::dvec2 lo(std::numeric_limits<double>::max());
    glm::dvec2 hi(std::numeric_limits<double>::lowest());
    double lin = 0, bil = 0, quad = 0;
    double residual_speed_sq_mass = 0.0;
    auto const& parts = grid.particles();
    r.particle_count = parts.size();
    for (ecs::Entity const p : parts)
    {
        auto pc = world.get_component<Particle_Component>(p);
        auto mc = world.get_component<Mass_Component>(p);
        auto dc = world.get_component<Deformable_Component>(p);
        if (!pc || !dc)
        {
            ++r.invalid_count;
            continue;
        }
        double const mass = mc ? mc->mass : 1.0;
        glm::dvec2 pos(pc->position.x, pc->position.y);
        glm::dvec2 velocity(pc->velocity.x, pc->velocity.y);

        bool finite = std::isfinite(pos.x) && std::isfinite(pos.y) &&
                      std::isfinite(velocity.x) && std::isfinite(velocity.y);
        for (int column = 0; finite && column < 2; ++column)
            for (int row = 0; row < 2; ++row)
                finite = finite && std::isfinite(dc->F_E[column][row]);
        for (auto const& coefficient : pc->mpm_poly_c)
            finite = finite && std::isfinite(coefficient.x) && std::isfinite(coefficient.y);
        if (!finite)
        {
            ++r.invalid_count;
            continue;
        }
        if (pos.x < 0.0 || pos.y < 0.0 || pos.x >= grid.nx() * grid.dx() || pos.y >= grid.ny() * grid.dx())
            ++r.out_of_bounds_count;

        lo = glm::min(lo, pos);
        hi = glm::max(hi, pos);
        r.max_speed = std::max(r.max_speed, glm::length(velocity));
        r.total_mass += mass;
        r.particle_ke += 0.5 * mass * glm::dot(velocity, velocity);
        r.total_momentum += mass * velocity;
        r.center_of_mass += mass * pos;

        auto const [base_i, base_j] = std::pair{
            static_cast<int>(std::floor(pos.x / grid.dx() - 0.5)),
            static_cast<int>(std::floor(pos.y / grid.dx() - 0.5)),
        };
        glm::dvec2 reconstructed_velocity{0.0};
        for (int j = base_j; j < base_j + 3; ++j)
            for (int i = base_i; i < base_i + 3; ++i)
                if (grid.in_bounds(i, j))
                    reconstructed_velocity += grid.cell_weight_from_pos(i, j, pos) * grid.cell_velocity(i, j);
        glm::dvec2 const residual = velocity - reconstructed_velocity;
        residual_speed_sq_mass += mass * glm::dot(residual, residual);

        double const det_f = Fluid_Simulation_2D::determinant_2D(dc->F_E);
        if (!std::isfinite(det_f))
        {
            ++r.invalid_count;
            continue;
        }
        r.min_det_f = std::min(r.min_det_f, det_f);
        r.max_det_f = std::max(r.max_det_f, det_f);
        lin += glm::dot(pc->mpm_poly_c[1], pc->mpm_poly_c[1]) + glm::dot(pc->mpm_poly_c[2], pc->mpm_poly_c[2]);
        bil += glm::dot(pc->mpm_poly_c[3], pc->mpm_poly_c[3]);
        for (int m = 4; m < MPM_POLYPIC_MAX_MODES_2D; ++m)
            quad += glm::dot(pc->mpm_poly_c[m], pc->mpm_poly_c[m]);
        for (auto const& c : pc->mpm_poly_c)
            r.max_coeff = std::max(r.max_coeff, max_abs(c));
    }
    double n = static_cast<double>(std::max<size_t>(1, parts.size()));
    r.width = hi.x - lo.x;
    r.height = hi.y - lo.y;
    r.rms_linear = std::sqrt(lin / n);
    r.rms_bilinear = std::sqrt(bil / n);
    r.rms_quadratic = std::sqrt(quad / n);
    if (r.total_mass > 0.0)
    {
        r.center_of_mass /= r.total_mass;
        r.residual_rms = std::sqrt(residual_speed_sq_mass / r.total_mass);
        r.residual_energy_per_mass = 0.5 * residual_speed_sq_mass / r.total_mass;
    }
    r.residual_energy_fraction = (0.5 * residual_speed_sq_mass) / (r.particle_ke + 1.0e-12);
    if (r.invalid_count == r.particle_count)
    {
        r.min_det_f = 0.0;
        r.max_det_f = 0.0;
    }
    return r;
}
} // namespace

int main(int argc, char** argv)
try
{
    Config cfg = parse_args(argc, argv);
    std::filesystem::create_directories(cfg.out_dir);

    World world;
    world.settings().MPM = true;
    world.settings().DIMENSION = 2;
    world.settings().FLUID_SOLVER = cfg.solver;
    world.settings().FLIP_PERCENT = cfg.flip;
    world.settings().POLYPIC_MODES = cfg.nr;
    world.settings().MPM_POLYPIC_QUAD_REG = cfg.reg;
    // MPM_EMPTY so the constructor does not auto-add a default square; we add our
    // own with the CLI-configured size/compression below.
    world.settings().MPM_SCENE = MPM_EMPTY;
    world.settings().CREATE_BOUNDARY_VISUALS = false;
    world.settings().APPLY_GRAVITY.store(cfg.gravity, std::memory_order_relaxed);

    ecs::Entity camera = world.create_entity("Camera");
    world.add_component(camera, Transform_Component{});

    MPM sim(cfg.domain, cfg.domain, cfg.dx, cfg.dt, world);
    auto& grid = static_cast<Collocated_Grid&>(sim.grid());
    grid.add_compressed_square(cfg.square_cells, cfg.ppc, cfg.compression);

    // Particle export (FLDPART1) at the requested playback fps, interpolated.
    std::unique_ptr<Particle_Frame_Writer> writer;
    Particle_Frame_Snapshot previous;
    if (cfg.export_particles)
    {
        Particle_Frame_Export_Config export_config{
            .dimension = 2,
            .scene = MPM_COMPRESSED_SQUARE,
            .solver = cfg.solver,
            .polypic_modes = cfg.nr,
            .flip_percent = cfg.flip,
            .fps = cfg.particle_fps,
            .domain_width = static_cast<double>(cfg.domain) * cfg.dx,
            .domain_height = static_cast<double>(cfg.domain) * cfg.dx,
            .domain_depth = 0.0,
            .dx = cfg.dx,
        };

        writer = std::make_unique<Particle_Frame_Writer>(std::filesystem::path(cfg.out_dir) / "particle_frames.bin", export_config);
        previous = capture_particle_frame(world);
        writer->write_frame(previous, 0.0, 0.0);
    }
    double next_frame_time = 1.0 / cfg.particle_fps;

    std::vector<Row> rows;
    int const diag_stride = std::max(1, cfg.steps / std::max(1, cfg.diag_samples));
    double sim_time = 0.0;

    // t=0 placeholder; the rest volume V_0 (hence elastic PE) is only known after
    // the first step, so the initial energy E0 is taken from step 1 and backfilled.
    rows.push_back(sample(world, grid, 0, cfg.dt, 0.0, 1.0));

    for (int s = 1; s <= cfg.steps; ++s)
    {
        double const prev_time = sim_time;
        sim.step(cfg.dt);
        sim_time += cfg.dt;

        if (writer)
        {
            Particle_Frame_Snapshot current = capture_particle_frame(world);
            while (next_frame_time <= sim_time + 1.0e-12)
            {
                double const alpha = cfg.dt > 0.0 ? (next_frame_time - prev_time) / cfg.dt : 1.0;
                writer->write_interpolated_frame(previous, current, alpha, next_frame_time, sim_time);
                next_frame_time += 1.0 / cfg.particle_fps;
            }
            previous = std::move(current);
        }

        if (s == 1 || s == cfg.steps || (cfg.diagnostics && s % diag_stride == 0))
            rows.push_back(sample(world, grid, s, cfg.dt, sim_time, 1.0));
    }
    if (writer)
        writer->close();

    // E0 = initial compressed energy (first post-step sample); backfill normals.
    double const e0 = rows.size() > 1 ? rows[1].total : rows.front().total;
    rows.front().ke = 0.0;
    rows.front().pe = e0;
    rows.front().total = e0;
    for (Row& r : rows)
        r.norm = e0 > 0.0 ? r.total / e0 : 0.0;

    // diagnostics.csv: the residual column names intentionally match the fluid
    // exporter so the shared viewer computes the same thesis ringing score.
    if (cfg.diagnostics)
    {
        std::ofstream out(std::filesystem::path(cfg.out_dir) / "diagnostics.csv");
        out << std::setprecision(17);
        out << "step,time,dt,kinetic_energy,elastic_energy,total_energy,normalized_total_energy,"
               "width,height,max_speed,rms_linear_mode,rms_bilinear_mode,rms_quadratic_mode,max_abs_coeff,"
               "particle_count,total_mass,stage_after_advect_particle_kinetic_energy,"
               "stage_after_advect_grid_residual_rms,stage_after_advect_grid_residual_energy_per_mass,"
               "stage_after_advect_grid_residual_energy_fraction,center_of_mass_x,center_of_mass_y,"
               "total_momentum_x,total_momentum_y,min_deformation_determinant,max_deformation_determinant,"
               "invalid_count,out_of_bounds_count\n";
        for (Row const& r : rows)
            out << r.step << ',' << r.time << ',' << r.dt << ','
                << r.ke << ',' << r.pe << ',' << r.total << ',' << r.norm << ','
                << r.width << ',' << r.height << ',' << r.max_speed << ',' << r.rms_linear << ','
                << r.rms_bilinear << ',' << r.rms_quadratic << ',' << r.max_coeff << ','
                << r.particle_count << ',' << r.total_mass << ',' << r.particle_ke << ','
                << r.residual_rms << ',' << r.residual_energy_per_mass << ',' << r.residual_energy_fraction << ','
                << r.center_of_mass.x << ',' << r.center_of_mass.y << ','
                << r.total_momentum.x << ',' << r.total_momentum.y << ','
                << r.min_det_f << ',' << r.max_det_f << ',' << r.invalid_count << ',' << r.out_of_bounds_count << '\n';
    }

    Row const& last = rows.back();
    double min_norm = 1.0, peak_coeff = 0.0, peak_speed = 0.0;
    bool finite = true;
    for (Row const& r : rows)
    {
        min_norm = std::min(min_norm, r.norm);
        peak_coeff = std::max(peak_coeff, r.max_coeff);
        peak_speed = std::max(peak_speed, r.max_speed);
        finite = finite && std::isfinite(r.total) && std::isfinite(r.max_coeff) &&
                 std::isfinite(r.residual_energy_per_mass) && r.invalid_count == 0;
    }

    // run.json
    {
        std::ofstream out(std::filesystem::path(cfg.out_dir) / "run.json");
        out << std::setprecision(10);
        out << "{\n";
        out << "  \"scene\": \"compressed_hyperelastic_square\",\n";
        out << "  \"solver\": " << cfg.solver << ",\n";
        out << "  \"nr\": " << cfg.nr << ",\n";
        out << "  \"flip_percent\": " << cfg.flip << ",\n";
        out << "  \"dt\": " << cfg.dt << ",\n";
        out << "  \"steps\": " << cfg.steps << ",\n";
        out << "  \"simulated_time\": " << cfg.steps * cfg.dt << ",\n";
        out << "  \"particle_fps\": " << cfg.particle_fps << ",\n";
        out << "  \"quad_reg\": " << cfg.reg << ",\n";
        out << "  \"domain\": " << cfg.domain << ",\n";
        out << "  \"dx\": " << cfg.dx << ",\n";
        out << "  \"square_cells\": " << cfg.square_cells << ",\n";
        out << "  \"particles_per_cell_axis\": " << cfg.ppc << ",\n";
        out << "  \"compression\": " << cfg.compression << ",\n";
        out << "  \"gravity\": " << (cfg.gravity ? "true" : "false") << ",\n";
        out << "  \"particles\": " << grid.particles().size() << ",\n";
        out << "  \"particle_frames\": " << (writer ? writer->frame_count() : 0) << ",\n";
        out << "  \"diagnostics\": " << (cfg.diagnostics ? "true" : "false") << ",\n";
        out << "  \"particle_export\": " << (cfg.export_particles ? "true" : "false") << ",\n";
        out << "  \"initial_energy\": " << e0 << ",\n";
        out << "  \"final_normalized_energy\": " << last.norm << ",\n";
        out << "  \"min_normalized_energy\": " << min_norm << ",\n";
        out << "  \"peak_max_abs_coeff\": " << peak_coeff << ",\n";
        out << "  \"peak_max_speed\": " << peak_speed << ",\n";
        out << "  \"final_residual_energy_per_mass\": " << last.residual_energy_per_mass << ",\n";
        out << "  \"final_residual_energy_fraction\": " << last.residual_energy_fraction << ",\n";
        out << "  \"invalid_count\": " << last.invalid_count << ",\n";
        out << "  \"out_of_bounds_count\": " << last.out_of_bounds_count << ",\n";
        out << "  \"finite\": " << (finite ? "true" : "false") << "\n";
        out << "}\n";
    }

    std::printf("%s: %d steps, %u frames, E_final/E0=%.4f, peak|coeff|=%.3e, finite=%s\n",
                cfg.out_dir.c_str(), cfg.steps, writer ? writer->frame_count() : 0, last.norm, peak_coeff,
                finite ? "yes" : "no");
    return finite ? 0 : 1;
}
catch (std::exception const& e)
{
    std::fprintf(stderr, "mpm_polypic_experiment error: %s\n", e.what());
    return 2;
}
