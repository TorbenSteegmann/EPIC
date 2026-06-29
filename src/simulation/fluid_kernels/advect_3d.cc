#include "advect_3d.hh"

#include "helpers_3d.hh"
#include "../../profile_timer.hh"

#include <algorithm>

namespace Fluid_Simulation_3D
{
glm::dvec3 reflect_velocity_off_solid(glm::dvec3 const& v, glm::dvec3 const& normal) { return 2.0 * glm::dot(v, normal) * normal - v; }

glm::dvec3 estimate_solid_normal(MAC_Grid_3D& grid, int i, int j, int k)
{
    glm::dvec3 normal(0.0);
    int nx = grid.nx();
    int ny = grid.ny();
    int nz = grid.nz();

    if (i == 0)
        normal.x = 1.0;
    else if (i == nx - 1)
        normal.x = -1.0;

    if (j == 0)
        normal.y = 1.0;
    else if (j == ny - 1)
        normal.y = -1.0;

    if (k == 0)
        normal.z = 1.0;
    else if (k == nz - 1)
        normal.z = -1.0;

    if (glm::length(normal) > 0.0)
        glm::normalize(normal);

    return normal;
}

void advect_3d(World& world, MAC_Grid_3D& grid, double dt)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_3D::advect_3d");

    int nx = grid.nx();
    int ny = grid.ny();
    int nz = grid.nz();
    double dx = grid.dx();

    auto particles = grid.particles();

    int const n_substeps = 5;
    double const CFL = 0.5;
    double const epsilon = 1e-6 * dx;

    for (auto& p : particles)
    {
        auto particle = world.get_component<Particle_Component>(p);
        glm::dvec3 x = particle->position;

        for (int s = 0; s < n_substeps; ++s)
        {
            // Interpolate current velocity
            glm::dvec3 v0 = interpolate_grid_velocity(grid, x);
            double speed = glm::length(v0);
            double dt_p = std::min(CFL * dx / (speed + epsilon), dt / n_substeps);

            // Midpoint integration
            glm::dvec3 x_mid = x + 0.5 * dt_p * v0;
            glm::dvec3 v_mid = interpolate_grid_velocity(grid, x_mid);
            glm::dvec3 x_new = x + dt_p * v_mid;

            // Check boundaries
            int i = int(x_new.x / dx);
            int j = int(x_new.y / dx);
            int k = int(x_new.z / dx);
            bool in_bounds = (i >= 0 && i < nx && j >= 0 && j < ny && k >= 0 && k < nz);

            // Handle solid collisions
            if (in_bounds && grid.cell_type(i, j, k) == SOLID)
            {
                glm::dvec3 normal = estimate_solid_normal(grid, i, j, k);
                glm::dvec3 velocity_before = v_mid;

                // Reflect velocity
                v_mid = reflect_velocity_off_solid(v_mid, normal);

                // Push particle out of the solid a bit
                double penetration_depth = 0.5 * dx;
                x_new = x + normal * penetration_depth;

                world.record_boundary_collision(Boundary_Collision_Event{
                    .entity = p,
                    .position_before = x,
                    .position_after = x_new,
                    .velocity_before = velocity_before,
                    .velocity_after = v_mid,
                    .normal = normal,
                    .solid_cell = glm::ivec3(i, j, k),
                    .substep = s});
            }

            x = x_new;
        }

        particle->position = x;
    }
}
} // namespace Fluid_Simulation_3D
