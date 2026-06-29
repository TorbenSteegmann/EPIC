#include "timestep_controller.hh"

#include "mac_grid.hh"
#include "mac_grid_3d.hh"

#include <algorithm>
#include <cmath>

namespace
{
double max_particle_speed(World& world)
{
    double max_speed = 0.0;
    auto& particles = world.get_array<Particle_Component>().data();
    for (auto const& particle : particles)
    {
        if (!std::isfinite(particle.velocity.x) || !std::isfinite(particle.velocity.y) || !std::isfinite(particle.velocity.z))
            continue;

        max_speed = std::max(max_speed, glm::length(particle.velocity));
    }

    return max_speed;
}

double grid_dx(Grid& grid)
{
    if (auto* mac_grid = dynamic_cast<MAC_Grid_2D*>(&grid))
        return mac_grid->dx();

    if (auto* mac_grid = dynamic_cast<MAC_Grid_3D*>(&grid))
        return mac_grid->dx();

    return 1.0;
}
} // namespace

Timestep_Decision Timestep_Controller::decide(World& world, Grid& grid, double frame_dt) const
{
    Timestep_Decision decision;
    decision.frame_dt = frame_dt;
    decision.sub_dt = frame_dt;

    if (world.settings().MPM || !world.settings().ADAPTIVE_TIMESTEP.load(std::memory_order_relaxed))
        return decision;

    double dx = grid_dx(grid);
    decision.max_particle_speed = max_particle_speed(world);
    if (dx <= 0.0 || decision.max_particle_speed <= 0.0)
        return decision;

    decision.estimated_frame_cfl = decision.max_particle_speed * frame_dt / dx;
    int substeps = static_cast<int>(std::ceil(decision.estimated_frame_cfl / target_cfl_));
    decision.substeps = std::clamp(substeps, 1, max_substeps_);
    decision.sub_dt = frame_dt / static_cast<double>(decision.substeps);

    return decision;
}
