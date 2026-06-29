#include "fluid_solver_3d.hh"

#include "../ecs/box_component.hh"
#include "fluid_kernels/add_water_box.hh"
#include "fluid_kernels/advect_3d.hh"
#include "fluid_kernels/apply_forces.hh"
#include "fluid_kernels/enforce_boundary_3d.hh"
#include "fluid_kernels/extrapolate_velocity_3d.hh"
#include "fluid_kernels/grid_to_particle_3d.hh"
#include "fluid_kernels/helpers_3d.hh"
#include "fluid_kernels/particle_to_grid_3d.hh"
#include "fluid_kernels/projection_3d.hh"
#include "../profile_timer.hh"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace
{
Box_Component make_fluid_domain_box(MAC_Grid_3D const& grid)
{
    glm::dvec3 fluid_lo = grid.fluid_domain_min();
    glm::dvec3 fluid_hi = grid.fluid_domain_max();
    return Box_Component{.size = glm::vec3(fluid_hi - fluid_lo), .offset = glm::vec3(fluid_lo)};
}

void sync_fluid_domain_box(World& world, MAC_Grid_3D const& grid)
{
    Box_Component box = make_fluid_domain_box(grid);
    auto& boxes = world.get_array<Box_Component>().data();
    if (boxes.empty())
    {
        ecs::Entity box_entity = world.create_entity("");
        world.add_component(box_entity, box);
        return;
    }

    boxes.front() = box;
}
} // namespace

Fluid_Solver_3D::Fluid_Solver_3D(double dx, int nx, int ny, int nz, World& world, double dt) : world_(world), grid_(dx, nx, ny, nz, world)
{
    sync_fluid_domain_box(world_, grid_);

    if (world.settings().FLUID_SCENE == TAYLOR_GREEN_VORTEX)
        throw std::invalid_argument("Taylor-Green vortex is a 2D-only fluid scene");
    if (world.settings().FLUID_SCENE == CONFINED_VORTEX)
        throw std::invalid_argument("Confined vortex is a 2D-only fluid scene");
    if (world.settings().FLUID_SCENE == SETTLING_POOL || world.settings().FLUID_SCENE == SETTLING_POOL_OBSTACLE)
        throw std::invalid_argument("Settling pool is a 2D-only fluid scene");

    std::uint32_t const jitter_seed = world.settings().FLUID_JITTER_SEED;
    if (world.settings().FLUID_SCENE == SOLID_BLOCK)
        Fluid_Simulation_3D::add_water_box(world_, grid_, glm::dvec3(34.0, 18.0, 34.0), glm::dvec3(46.0, 42.0, 46.0), false);
    else if (world.settings().FLUID_SCENE == DAM_BREAK)
    {
        glm::dvec3 const fluid_min = grid_.fluid_domain_min();
        glm::dvec3 const fluid_max = grid_.fluid_domain_max();
        double const interior_width = fluid_max.x - fluid_min.x;
        glm::dvec3 const dam_max{
            fluid_min.x + interior_width / 3.0,
            fluid_max.y - grid_.dx(),
            fluid_max.z,
        };
        Fluid_Simulation_3D::add_water_box(world_, grid_, fluid_min, dam_max, true, jitter_seed);
    }
    else
        Fluid_Simulation_3D::add_water_box(world_, grid_, glm::dvec3(4.0, 4.0, 4.0), glm::dvec3(34.0, 78.0, 34.0), true, jitter_seed);
}

void Fluid_Solver_3D::step(double dt)
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_3D::step");

    stage_diagnostics_ = {};
    stage_diagnostics_.before_step_particle_energy = particle_mechanical_energy();
    stage_diagnostics_.before_step_particle_kinetic_energy = particle_kinetic_energy();

    Fluid_Simulation_3D::particle_to_grid_3d(world_, grid_);
    stage_diagnostics_.after_particle_to_grid_max_grid_speed = max_grid_speed();
    stage_diagnostics_.after_particle_to_grid_grid_kinetic_energy = grid_kinetic_energy();

    Fluid_Simulation::apply_forces(grid_, dt, world_.settings());
    stage_diagnostics_.after_forces_max_grid_speed = max_grid_speed();
    stage_diagnostics_.after_forces_grid_kinetic_energy = grid_kinetic_energy();

    Fluid_Simulation_3D::extrapolate_velocity_3d(grid_, dt);
    stage_diagnostics_.after_first_extrapolate_max_grid_speed = max_grid_speed();
    stage_diagnostics_.after_first_extrapolate_grid_kinetic_energy = grid_kinetic_energy();

    Fluid_Simulation_3D::enforce_boundary_3d(grid_);
    stage_diagnostics_.after_boundary_max_grid_speed = max_grid_speed();
    stage_diagnostics_.after_boundary_grid_kinetic_energy = grid_kinetic_energy();
    {
        auto [max_div, rms_div] = divergence_stats();
        stage_diagnostics_.before_projection_max_divergence = max_div;
        stage_diagnostics_.before_projection_rms_divergence = rms_div;
    }

    stage_diagnostics_.projection = Fluid_Simulation_3D::projection_3d(world_, grid_, dt);
    stage_diagnostics_.after_projection_max_grid_speed = max_grid_speed();
    stage_diagnostics_.after_projection_grid_kinetic_energy = grid_kinetic_energy();
    {
        auto [max_div, rms_div] = divergence_stats();
        stage_diagnostics_.after_projection_max_divergence = max_div;
        stage_diagnostics_.after_projection_rms_divergence = rms_div;
    }

    Fluid_Simulation_3D::extrapolate_velocity_3d(grid_, dt);
    stage_diagnostics_.after_second_extrapolate_max_grid_speed = max_grid_speed();
    stage_diagnostics_.after_second_extrapolate_grid_kinetic_energy = grid_kinetic_energy();

    Fluid_Simulation_3D::grid_to_particle_3d(world_, grid_);
    stage_diagnostics_.after_grid_to_particle_max_particle_speed = max_particle_speed();
    stage_diagnostics_.after_grid_to_particle_particle_energy = particle_mechanical_energy();
    stage_diagnostics_.after_grid_to_particle_particle_kinetic_energy = particle_kinetic_energy();

    Fluid_Simulation_3D::advect_3d(world_, grid_, dt);
    stage_diagnostics_.after_advect_max_particle_speed = max_particle_speed();
    stage_diagnostics_.after_advect_particle_energy = particle_mechanical_energy();
    stage_diagnostics_.after_advect_particle_kinetic_energy = particle_kinetic_energy();
    update_particle_grid_residual_diagnostics();
}

void Fluid_Solver_3D::update_particle_grid_residual_diagnostics()
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_3D::update_particle_grid_residual_diagnostics");

    double total_mass = 0.0;
    double particle_speed_sq_sum = 0.0;
    double residual_sq_sum = 0.0;
    double residual_delta_sq_sum = 0.0;
    double residual_delta_mass = 0.0;
    double reversing_residual_sq_sum = 0.0;
    std::size_t active_residual_count = 0;
    std::size_t reversing_residual_count = 0;
    std::unordered_map<ecs::Entity, glm::dvec3> current_residuals;
    current_residuals.reserve(grid_.particles().size());

    constexpr double residual_epsilon_sq = 1.0e-20;
    for (auto const entity : grid_.particles())
    {
        auto const* particle = world_.get_component<Particle_Component>(entity);
        if (!particle || !std::isfinite(particle->velocity.x) || !std::isfinite(particle->velocity.y) ||
            !std::isfinite(particle->velocity.z))
            continue;

        double mass = 1.0;
        if (auto const* mass_component = world_.get_component<Mass_Component>(entity))
            mass = mass_component->mass;

        glm::dvec3 const position(particle->position.x, particle->position.y, particle->position.z);
        glm::dvec3 const particle_velocity(particle->velocity.x, particle->velocity.y, particle->velocity.z);
        glm::dvec3 const grid_velocity = Fluid_Simulation_3D::interpolate_grid_velocity(grid_, position);
        glm::dvec3 const residual = particle_velocity - grid_velocity;
        double const residual_sq = glm::dot(residual, residual);

        total_mass += mass;
        particle_speed_sq_sum += mass * glm::dot(particle_velocity, particle_velocity);
        residual_sq_sum += mass * residual_sq;
        current_residuals.emplace(entity, residual);

        auto const previous = previous_particle_grid_residuals_.find(entity);
        if (previous == previous_particle_grid_residuals_.end())
            continue;

        glm::dvec3 const residual_delta = residual - previous->second;
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

double Fluid_Solver_3D::max_particle_speed()
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_3D::max_particle_speed");

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

double Fluid_Solver_3D::max_grid_speed()
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_3D::max_grid_speed");

    Velocity_Field_3D velocity = grid_.velocity();
    double max_speed = 0.0;

    for (int k = 0; k < velocity.nz; ++k)
    {
        for (int j = 0; j < velocity.ny; ++j)
        {
            for (int i = 0; i <= velocity.nx; ++i)
            {
                double u = velocity.u(i, j, k);
                if (std::isfinite(u))
                    max_speed = std::max(max_speed, std::abs(u));
            }
        }
    }

    for (int k = 0; k < velocity.nz; ++k)
    {
        for (int j = 0; j <= velocity.ny; ++j)
        {
            for (int i = 0; i < velocity.nx; ++i)
            {
                double v = velocity.v(i, j, k);
                if (std::isfinite(v))
                    max_speed = std::max(max_speed, std::abs(v));
            }
        }
    }

    for (int k = 0; k <= velocity.nz; ++k)
    {
        for (int j = 0; j < velocity.ny; ++j)
        {
            for (int i = 0; i < velocity.nx; ++i)
            {
                double w = velocity.w(i, j, k);
                if (std::isfinite(w))
                    max_speed = std::max(max_speed, std::abs(w));
            }
        }
    }

    return max_speed;
}

double Fluid_Solver_3D::particle_kinetic_energy()
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_3D::particle_kinetic_energy");

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

double Fluid_Solver_3D::particle_mechanical_energy()
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_3D::particle_mechanical_energy");

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

double Fluid_Solver_3D::grid_kinetic_energy()
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_3D::grid_kinetic_energy");

    Velocity_Field_3D velocity = grid_.velocity();
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

    for (double w : velocity.w_component)
    {
        if (std::isfinite(w))
            energy += 0.5 * w * w;
    }

    return energy;
}

std::pair<double, double> Fluid_Solver_3D::divergence_stats()
{
    FLUID_PROFILE_SCOPE("Fluid_Solver_3D::divergence_stats");

    double max_div = 0.0;
    double div_sq_sum = 0.0;
    int count = 0;

    for (int i = 1; i < grid_.nx() - 1; ++i)
    {
        for (int j = 1; j < grid_.ny() - 1; ++j)
        {
            for (int k = 1; k < grid_.nz() - 1; ++k)
            {
                if (grid_.cell_type(i, j, k) != FLUID)
                    continue;

                double div = (grid_.velocity().u(i + 1, j, k) - grid_.velocity().u(i, j, k) +
                              grid_.velocity().v(i, j + 1, k) - grid_.velocity().v(i, j, k) +
                              grid_.velocity().w(i, j, k + 1) - grid_.velocity().w(i, j, k)) / grid_.dx();
                if (!std::isfinite(div))
                    continue;

                double abs_div = std::abs(div);
                max_div = std::max(max_div, abs_div);
                div_sq_sum += div * div;
                ++count;
            }
        }
    }

    if (count == 0)
        return {max_div, 0.0};

    return {max_div, std::sqrt(div_sq_sum / static_cast<double>(count))};
}
