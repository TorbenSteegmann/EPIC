#include "advect.hh"

#include "../core/helpers.hh"
#include "../../profile_timer.hh"

glm::dvec2 reflect_velocity_off_solid(glm::dvec2 const& v, glm::dvec2 const& normal) { return 2.0 * glm::dot(v, normal) * normal - v; }

glm::dvec2 estimate_solid_normal(MAC_Grid_2D& grid, int i, int j)
{
    glm::dvec2 normal(0.0);
    int nx = grid.nx();
    int ny = grid.ny();

    if (i == 0)
        normal.x = 1.0;
    else if (i == nx - 1)
        normal.x = -1.0;

    if (j == 0)
        normal.y = 1.0;
    else if (j == ny - 1)
        normal.y = -1.0;

    if (normal.length() > 0)
        glm::normalize(normal);

    return normal;
}

void Fluid_Simulation_2D::advect(World& world, MAC_Grid_2D& grid, double dt)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_2D::advect");

    int nx = grid.nx();
    int ny = grid.ny();
    double dx = grid.dx();

    auto particles = grid.particles();

    int const n_substeps = 5;
    double const CFL = 0.5;
    double const epsilon = 1e-6 * dx;

    for (auto& p : particles)
    {
        auto particle = world.get_component<Particle_Component>(p);
        glm::dvec2 x = glm::dvec2(particle->position.x, particle->position.y);

        for (int s = 0; s < n_substeps; ++s)
        {
            // Interpolate current velocity
            glm::dvec2 v0 = interpolate_grid_velocity(grid, x);
            double speed = glm::length(v0);
            double dt_p = std::min(CFL * dx / (speed + epsilon), dt / n_substeps);

            // Midpoint integration
            glm::dvec2 x_mid = x + 0.5 * dt_p * v0;
            glm::dvec2 v_mid = interpolate_grid_velocity(grid, x_mid);
            glm::dvec2 x_new = x + dt_p * v_mid;

            // Check boundaries
            int i = int(x_new.x / dx);
            int j = int(x_new.y / dx);
            bool in_bounds = (i >= 0 && i < nx && j >= 0 && j < ny);

            // Handle solid collisions
            if (in_bounds && grid.cell_type(i, j) == SOLID)
            {
                glm::dvec2 normal = estimate_solid_normal(grid, i, j);
                glm::dvec2 velocity_before = v_mid;

                // Reflect velocity
                v_mid = reflect_velocity_off_solid(v_mid, normal);

                // Push particle out of the solid a bit
                double penetration_depth = 0.5 * dx;
                x_new = x + normal * penetration_depth;

                world.record_boundary_collision(Boundary_Collision_Event{
                    .entity = p,
                    .position_before = glm::dvec3(x, 0.0),
                    .position_after = glm::dvec3(x_new, 0.0),
                    .velocity_before = glm::dvec3(velocity_before, 0.0),
                    .velocity_after = glm::dvec3(v_mid, 0.0),
                    .normal = glm::dvec3(normal, 0.0),
                    .solid_cell = glm::ivec3(i, j, 0),
                    .substep = s});
            }

            x = x_new;
        }

        particle->position = glm::dvec3(x, 0.0);
    }
}
