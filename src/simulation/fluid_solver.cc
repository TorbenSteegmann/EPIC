#include "fluid_solver.hh"

#include "fluid_kernels/add_water_box.hh"
#include "fluid_kernels/add_taylor_green_vortex.hh"
#include "fluid_kernels/add_confined_vortex.hh"
#include "fluid_kernels/angular_momentum.hh"
#include "fluid_kernels/advect.hh"
#include "fluid_kernels/apply_forces.hh"
#include "fluid_kernels/enforce_boundary.hh"
#include "fluid_kernels/extrapolate_velocity.hh"
#include "fluid_kernels/grid_to_particle.hh"
#include "fluid_kernels/particle_to_grid.hh"
#include "fluid_kernels/projection.hh"
#include "core/helpers.hh"
#include "../profile_timer.hh"
#include "../resource_handler.hh"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

namespace
{
void add_settling_pool_obstacle(World& world, MAC_Grid_2D& grid, double pool_top)
{
    int obstacle_width = std::max(grid.nx() / 5, 1);
    if ((grid.nx() - obstacle_width) % 2 != 0 && obstacle_width > 1)
        --obstacle_width;

    int const min_i = (grid.nx() - obstacle_width) / 2;
    int const max_i = min_i + obstacle_width;
    int const min_j = 1;
    int const max_j = std::clamp(static_cast<int>(std::floor(pool_top / grid.dx())), min_j + 1, grid.ny() - 1);

    for (int j = min_j; j < max_j; ++j)
        for (int i = min_i; i < max_i; ++i)
            grid.cell_type(i, j) = SOLID;

    if (!world.settings().CREATE_BOUNDARY_VISUALS)
        return;

    ecs::Entity obstacle = world.create_entity("Settling Pool Center Obstacle", ecs::Registry_Type::Local);
    Transform_Component transform{};
    transform.position = glm::vec3(min_i * grid.dx(), min_j * grid.dx(), 0.0);
    transform.scale = glm::vec3((max_i - min_i) * grid.dx(), (max_j - min_j) * grid.dx(), 1.0);
    world.add_component(obstacle, transform);

    Sprite_Component sprite{};
    sprite.texture = Resource_Handler::get_texture("block");
    sprite.color = glm::vec3(0.0f);
    world.add_component(obstacle, sprite);
}

void add_settling_pool(World& world, MAC_Grid_2D& grid, std::uint32_t jitter_seed, bool center_obstacle)
{
    double const dx = grid.dx();
    double const width = static_cast<double>(grid.nx()) * dx;
    double const height = static_cast<double>(grid.ny()) * dx;
    double const pool_top = height * 0.38;
    double const plug_min_y = height * 0.56;
    double const plug_max_y = height * 0.72;

    if (center_obstacle)
        add_settling_pool_obstacle(world, grid, pool_top);

    Fluid_Simulation_2D::add_water_box(world, grid, glm::dvec2(dx, dx), glm::dvec2(width - 2.0 * dx, pool_top), true, jitter_seed);
    std::uint32_t const plug_seed = jitter_seed == 0 ? 0 : jitter_seed ^ 0x53504c53u;
    Fluid_Simulation_2D::add_water_box(world, grid, glm::dvec2(width * 0.43, plug_min_y), glm::dvec2(width * 0.57, plug_max_y), true, plug_seed);

    double const surface_band_bottom = pool_top - height * 0.10;
    double const surface_band_height = std::max(pool_top - surface_band_bottom, dx);
    double const interior_width = std::max(width - 3.0 * dx, dx);
    double const two_pi = 6.28318530717958647692;

    for (auto const entity : grid.particles())
    {
        auto* particle = world.get_component<Particle_Component>(entity);
        if (!particle)
            continue;

        if (particle->position.y >= plug_min_y - dx)
            particle->velocity.y -= 2.5;

        if (particle->position.y < surface_band_bottom || particle->position.y > pool_top + dx)
            continue;

        double const x_phase = std::clamp((particle->position.x - dx) / interior_width, 0.0, 1.0);
        double const surface_weight = std::clamp((particle->position.y - surface_band_bottom) / surface_band_height, 0.0, 1.0);
        particle->velocity.x += 2.0 * std::sin(two_pi * x_phase) * surface_weight;
        particle->velocity.y += 0.75 * std::cos(two_pi * x_phase) * surface_weight;
    }
}
} // namespace

Fluid_Solver_2D::Fluid_Solver_2D(double dx, int nx, int ny, World& world, double dt) : world_(world), grid_(dx, nx, ny, world)
{
    std::uint32_t const jitter_seed = world.settings().FLUID_JITTER_SEED;
    switch (world.settings().FLUID_SCENE)
    {
    case DAM_BREAK:
        Fluid_Simulation_2D::add_water_box(world, grid_, glm::dvec2(1, 1), glm::dvec2(nx / 3, ny - 2), true, jitter_seed);
        break;
    case SOLID_BLOCK:
        Fluid_Simulation_2D::add_water_box(world, grid_, glm::dvec2(nx * 0.35, ny * 0.35), glm::dvec2(nx * 0.65, ny * 0.65), false);
        break;
    case FULL_GRID:
        Fluid_Simulation_2D::add_water_box(world, grid_, glm::dvec2(1, 1), glm::dvec2(nx, ny), true, jitter_seed);
        break;
    case CONSTANT_STREAM:
        Fluid_Simulation_2D::add_water_box(world, grid_, glm::dvec2(nx * 0.5 - 1, ny - 2), glm::dvec2(nx * 0.5 + 1, ny - 1), true, jitter_seed);
        break;
    case TAYLOR_GREEN_VORTEX:
        Fluid_Simulation_2D::add_taylor_green_vortex(world, grid_);
        break;
    case CONFINED_VORTEX:
        Fluid_Simulation_2D::add_confined_vortex(world, grid_);
        break;
    case SETTLING_POOL:
        add_settling_pool(world, grid_, jitter_seed, false);
        break;
    case SETTLING_POOL_OBSTACLE:
        add_settling_pool(world, grid_, jitter_seed, true);
        break;
    default:
        throw("Fluid_Solver_2D (Constructor): Scene not defined");
    }
}

void Fluid_Solver_2D::step(double dt)
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_2D::step");

    stage_diagnostics_ = {};
    stage_diagnostics_.before_step_particle_energy = particle_mechanical_energy();
    stage_diagnostics_.before_step_particle_kinetic_energy = particle_kinetic_energy();
    stage_diagnostics_.before_step_particle_angular_momentum = Fluid_Simulation_2D::particle_represented_angular_momentum(world_, grid_);

    Fluid_Simulation_2D::particle_to_grid(world_, grid_);
    stage_diagnostics_.after_particle_to_grid_max_grid_speed = max_grid_speed();
    stage_diagnostics_.after_particle_to_grid_grid_kinetic_energy = grid_kinetic_energy();
    stage_diagnostics_.after_particle_to_grid_grid_angular_momentum = Fluid_Simulation_2D::grid_angular_momentum(world_, grid_);

    Fluid_Simulation::apply_forces(grid_, dt, world_.settings());
    stage_diagnostics_.after_forces_max_grid_speed = max_grid_speed();
    stage_diagnostics_.after_forces_grid_kinetic_energy = grid_kinetic_energy();

    Fluid_Simulation_2D::extrapolate_velocity(grid_, dt);
    stage_diagnostics_.after_first_extrapolate_max_grid_speed = max_grid_speed();
    stage_diagnostics_.after_first_extrapolate_grid_kinetic_energy = grid_kinetic_energy();

    Fluid_Simulation_2D::enforce_boundary(grid_);
    stage_diagnostics_.after_boundary_max_grid_speed = max_grid_speed();
    stage_diagnostics_.after_boundary_grid_kinetic_energy = grid_kinetic_energy();
    {
        auto [max_div, rms_div] = divergence_stats();
        stage_diagnostics_.before_projection_max_divergence = max_div;
        stage_diagnostics_.before_projection_rms_divergence = rms_div;
    }

    stage_diagnostics_.projection = Fluid_Simulation_2D::projection_2d(world_, grid_, dt);
    stage_diagnostics_.after_projection_max_grid_speed = max_grid_speed();
    stage_diagnostics_.after_projection_grid_kinetic_energy = grid_kinetic_energy();
    stage_diagnostics_.after_projection_grid_angular_momentum = Fluid_Simulation_2D::grid_angular_momentum(world_, grid_);
    {
        auto [max_div, rms_div] = divergence_stats();
        stage_diagnostics_.after_projection_max_divergence = max_div;
        stage_diagnostics_.after_projection_rms_divergence = rms_div;
    }

    Fluid_Simulation_2D::extrapolate_velocity(grid_, dt);
    stage_diagnostics_.after_second_extrapolate_max_grid_speed = max_grid_speed();
    stage_diagnostics_.after_second_extrapolate_grid_kinetic_energy = grid_kinetic_energy();
    stage_diagnostics_.after_second_extrapolate_grid_angular_momentum = Fluid_Simulation_2D::grid_angular_momentum(world_, grid_);

    Fluid_Simulation_2D::grid_to_particle(world_, grid_);
    stage_diagnostics_.after_grid_to_particle_max_particle_speed = max_particle_speed();
    stage_diagnostics_.after_grid_to_particle_particle_energy = particle_mechanical_energy();
    stage_diagnostics_.after_grid_to_particle_particle_kinetic_energy = particle_kinetic_energy();
    stage_diagnostics_.after_grid_to_particle_particle_angular_momentum =
        Fluid_Simulation_2D::particle_represented_angular_momentum(world_, grid_);

    Fluid_Simulation_2D::advect(world_, grid_, dt);
    stage_diagnostics_.after_advect_max_particle_speed = max_particle_speed();
    stage_diagnostics_.after_advect_particle_energy = particle_mechanical_energy();
    stage_diagnostics_.after_advect_particle_kinetic_energy = particle_kinetic_energy();
    stage_diagnostics_.after_advect_particle_angular_momentum = Fluid_Simulation_2D::particle_represented_angular_momentum(world_, grid_);
    update_particle_grid_residual_diagnostics();
}

void Fluid_Solver_2D::update_particle_grid_residual_diagnostics()
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_2D::update_particle_grid_residual_diagnostics");

    double total_mass = 0.0;
    double particle_speed_sq_sum = 0.0;
    double residual_sq_sum = 0.0;
    double residual_delta_sq_sum = 0.0;
    double residual_delta_mass = 0.0;
    double reversing_residual_sq_sum = 0.0;
    std::size_t active_residual_count = 0;
    std::size_t reversing_residual_count = 0;
    std::unordered_map<ecs::Entity, glm::dvec2> current_residuals;
    current_residuals.reserve(grid_.particles().size());

    constexpr double residual_epsilon_sq = 1.0e-20;
    for (auto const entity : grid_.particles())
    {
        auto const* particle = world_.get_component<Particle_Component>(entity);
        if (!particle || !std::isfinite(particle->velocity.x) || !std::isfinite(particle->velocity.y))
            continue;

        double mass = 1.0;
        if (auto const* mass_component = world_.get_component<Mass_Component>(entity))
            mass = mass_component->mass;

        glm::dvec2 const position(particle->position.x, particle->position.y);
        glm::dvec2 const particle_velocity(particle->velocity.x, particle->velocity.y);
        glm::dvec2 const grid_velocity = Fluid_Simulation_2D::interpolate_grid_velocity(grid_, position);
        glm::dvec2 const residual = particle_velocity - grid_velocity;
        double const residual_sq = glm::dot(residual, residual);

        total_mass += mass;
        particle_speed_sq_sum += mass * glm::dot(particle_velocity, particle_velocity);
        residual_sq_sum += mass * residual_sq;
        current_residuals.emplace(entity, residual);

        auto const previous = previous_particle_grid_residuals_.find(entity);
        if (previous == previous_particle_grid_residuals_.end())
            continue;

        glm::dvec2 const residual_delta = residual - previous->second;
        residual_delta_sq_sum += mass * glm::dot(residual_delta, residual_delta);
        residual_delta_mass += mass;

        double const previous_residual_sq = glm::dot(previous->second, previous->second);
        if (residual_sq <= residual_epsilon_sq || previous_residual_sq <= residual_epsilon_sq)
            continue;

        ++active_residual_count;
        if (glm::dot(residual, previous->second) >= 0.0)
            continue;

        ++reversing_residual_count;
        reversing_residual_sq_sum += mass * residual_sq;
    }

    if (total_mass > 0.0)
    {
        stage_diagnostics_.after_advect_grid_residual_rms = std::sqrt(residual_sq_sum / total_mass);
        stage_diagnostics_.after_advect_grid_residual_energy_per_mass = 0.5 * residual_sq_sum / total_mass;
        stage_diagnostics_.after_advect_grid_residual_delta_energy_per_mass = 0.5 * residual_delta_sq_sum / total_mass;
        stage_diagnostics_.after_advect_grid_residual_reversal_energy_per_mass = 0.5 * reversing_residual_sq_sum / total_mass;
    }
    if (particle_speed_sq_sum > 0.0)
    {
        stage_diagnostics_.after_advect_grid_residual_energy_fraction = residual_sq_sum / particle_speed_sq_sum;
        stage_diagnostics_.after_advect_grid_residual_delta_energy_fraction = residual_delta_sq_sum / particle_speed_sq_sum;
        stage_diagnostics_.after_advect_grid_residual_reversal_energy_fraction = reversing_residual_sq_sum / particle_speed_sq_sum;
    }
    if (residual_delta_mass > 0.0)
        stage_diagnostics_.after_advect_grid_residual_delta_rms = std::sqrt(residual_delta_sq_sum / residual_delta_mass);
    if (active_residual_count > 0)
        stage_diagnostics_.after_advect_grid_residual_reversal_fraction =
            static_cast<double>(reversing_residual_count) / static_cast<double>(active_residual_count);

    previous_particle_grid_residuals_ = std::move(current_residuals);
}

double Fluid_Solver_2D::max_particle_speed()
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_2D::max_particle_speed");

    double max_speed = 0.0;
    auto const& particles = grid_.particles();
    for (auto const& entity : particles)
    {
        auto const* particle = world_.get_component<Particle_Component>(entity);
        if (!particle)
            continue;

        if (!std::isfinite(particle->velocity.x) || !std::isfinite(particle->velocity.y) || !std::isfinite(particle->velocity.z))
            continue;

        max_speed = std::max(max_speed, glm::length(particle->velocity));
    }

    return max_speed;
}

double Fluid_Solver_2D::max_grid_speed()
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_2D::max_grid_speed");

    Velocity_Field velocity = grid_.u();
    double max_speed = 0.0;

    for (int j = 0; j < velocity.ny; ++j)
    {
        for (int i = 0; i <= velocity.nx; ++i)
        {
            double u = velocity.u(i, j);
            if (std::isfinite(u))
                max_speed = std::max(max_speed, std::abs(u));
        }
    }

    for (int j = 0; j <= velocity.ny; ++j)
    {
        for (int i = 0; i < velocity.nx; ++i)
        {
            double v = velocity.v(i, j);
            if (std::isfinite(v))
                max_speed = std::max(max_speed, std::abs(v));
        }
    }

    return max_speed;
}

double Fluid_Solver_2D::particle_kinetic_energy()
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_2D::particle_kinetic_energy");

    double energy = 0.0;
    auto const& particles = grid_.particles();
    for (auto const& entity : particles)
    {
        auto const* particle = world_.get_component<Particle_Component>(entity);
        if (!particle)
            continue;

        if (!std::isfinite(particle->velocity.x) || !std::isfinite(particle->velocity.y) || !std::isfinite(particle->velocity.z))
            continue;

        double mass = 1.0;
        if (auto const* mass_component = world_.get_component<Mass_Component>(entity))
            mass = mass_component->mass;

        double speed = glm::length(particle->velocity);
        energy += 0.5 * mass * speed * speed;
    }

    return energy;
}

double Fluid_Solver_2D::particle_mechanical_energy()
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_2D::particle_mechanical_energy");

    double const gravity_magnitude = world_.settings().APPLY_GRAVITY.load(std::memory_order_relaxed) ? 9.81 : 0.0;

    double energy = 0.0;
    double floor_y = grid_.origin().y + grid_.dx();
    auto const& particles = grid_.particles();
    for (auto const& entity : particles)
    {
        auto const* particle = world_.get_component<Particle_Component>(entity);
        if (!particle)
            continue;

        if (!std::isfinite(particle->position.y) || !std::isfinite(particle->velocity.x) || !std::isfinite(particle->velocity.y) ||
            !std::isfinite(particle->velocity.z))
            continue;

        double mass = 1.0;
        if (auto const* mass_component = world_.get_component<Mass_Component>(entity))
            mass = mass_component->mass;

        double speed = glm::length(particle->velocity);
        energy += 0.5 * mass * speed * speed;
        energy += mass * gravity_magnitude * (particle->position.y - floor_y);
    }

    return energy;
}

double Fluid_Solver_2D::grid_kinetic_energy()
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_2D::grid_kinetic_energy");

    Velocity_Field velocity = grid_.u();
    double energy = 0.0;

    for (double u : velocity.u_component)
    {
        if (std::isfinite(u))
            energy += 0.5 * u * u;
    }

    for (double v : velocity.v_component)
    {
        if (std::isfinite(v))
            energy += 0.5 * v * v;
    }

    return energy;
}

std::pair<double, double> Fluid_Solver_2D::divergence_stats()
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_2D::divergence_stats");

    double max_div = 0.0;
    double div_sq_sum = 0.0;
    int count = 0;

    for (int i = 1; i < grid_.nx() - 1; ++i)
    {
        for (int j = 1; j < grid_.ny() - 1; ++j)
        {
            if (grid_.cell_type(i, j) != FLUID)
                continue;

            double div = (grid_.u().u(i + 1, j) - grid_.u().u(i, j) +
                          grid_.u().v(i, j + 1) - grid_.u().v(i, j)) / grid_.dx();
            if (!std::isfinite(div))
                continue;

            double abs_div = std::abs(div);
            max_div = std::max(max_div, abs_div);
            div_sq_sum += div * div;
            ++count;
        }
    }

    if (count == 0)
        return {max_div, 0.0};

    return {max_div, std::sqrt(div_sq_sum / static_cast<double>(count))};
}
