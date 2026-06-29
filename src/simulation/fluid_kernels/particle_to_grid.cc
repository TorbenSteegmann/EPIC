#include "particle_to_grid.hh"
#include "polypic.hh"
#include "../../profile_timer.hh"

#include <cmath>
#include <iostream>
#include <vector>

void Fluid_Simulation_2D::particle_to_grid(World& world, MAC_Grid_2D& grid)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_2D::particle_to_grid");

    grid.clear();

    auto particles = grid.particles();

    double dx = grid.dx();
    int nx = grid.nx();
    int ny = grid.ny();

    std::vector<double> fluid_fraction(nx * ny, 0.0);

    // weight accumulators
    std::vector<double> u_weights((nx + 1) * ny, 0.0);
    std::vector<double> v_weights(nx * (ny + 1), 0.0);

    // rasterize each particle’s vel to grid
    for (auto const& p : particles)
    {
        auto particle = world.get_component<Particle_Component>(p);

        glm::dvec2 pos = glm::dvec2(particle->position.x, particle->position.y);
        glm::dvec2 vel = glm::dvec2(particle->velocity.x, particle->velocity.y);

        // u
        {
            // get the grid cell, get the interpolation distance wxyz
            glm::dvec2 pos_in_grid = pos / dx;
            pos_in_grid.y -= 0.5; // staggered grid pos
            glm::ivec2 grid_cell(static_cast<int>(std::floor(pos_in_grid.x)), static_cast<int>(std::floor(pos_in_grid.y)));

            for (int dj = 0; dj < 2; ++dj)
            {
                for (int di = 0; di < 2; ++di)
                {
                    auto grid_x = grid_cell.x + di;
                    auto grid_y = grid_cell.y + dj;

                    if (grid_x < 0 || grid_x > nx || grid_y < 0 || grid_y >= ny)
                        continue;

                    double weight_x = 1.0 - std::abs(pos_in_grid.x - grid_x);
                    double weight_y = 1.0 - std::abs(pos_in_grid.y - grid_y);
                    double weight = glm::max(0.0, weight_x) * glm::max(0.0, weight_y);

                    glm::dvec2 distance_vec = glm::dvec2(grid_x, grid_y) - pos_in_grid;

                    double face_value;
                    if (world.settings().FLUID_SOLVER == POLYPIC)
                        face_value = polypic_p2g_face_value(*particle, 0, distance_vec, world.settings().POLYPIC_MODES);
                    else
                    {
                        double c_x = world.settings().FLUID_SOLVER == APIC ? glm::dot(particle->c_u, distance_vec) : 0.0;
                        face_value = vel.x + c_x;
                    }

                    double vel_update = weight * face_value;

                    grid.u().u(grid_x, grid_y) += vel_update;

                    u_weights[grid_index::u(grid_x, grid_y, nx, ny)] += weight;
                }
            }
        }

        // v
        {
            // get the grid cell, get the interpolation distance wxyz
            glm::dvec2 pos_in_grid = pos / dx;
            pos_in_grid.x -= 0.5; // staggered grid pos
            glm::ivec2 grid_cell(static_cast<int>(std::floor(pos_in_grid.x)), static_cast<int>(std::floor(pos_in_grid.y)));

            for (int dj = 0; dj < 2; ++dj)
            {
                for (int di = 0; di < 2; ++di)
                {
                    auto grid_x = grid_cell.x + di;
                    auto grid_y = grid_cell.y + dj;

                    if (grid_x < 0 || grid_x >= nx || grid_y < 0 || grid_y > ny)
                        continue;

                    double weight_x = 1.0 - std::abs(pos_in_grid.x - grid_x);
                    double weight_y = 1.0 - std::abs(pos_in_grid.y - grid_y);
                    double weight = glm::max(0.0, weight_x) * glm::max(0.0, weight_y);

                    glm::dvec2 distance_vec = glm::dvec2(grid_x, grid_y) - pos_in_grid;

                    double face_value;
                    if (world.settings().FLUID_SOLVER == POLYPIC)
                        face_value = polypic_p2g_face_value(*particle, 1, distance_vec, world.settings().POLYPIC_MODES);
                    else
                    {
                        double c_y = world.settings().FLUID_SOLVER == APIC ? glm::dot(particle->c_v, distance_vec) : 0.0;
                        face_value = vel.y + c_y;
                    }

                    double vel_update = weight * face_value;

                    grid.u().v(grid_x, grid_y) += vel_update;

                    v_weights[grid_index::v(grid_x, grid_y, nx, ny)] += weight;
                }
            }
        }

        // mark  cell containing particle as FLUID
        int ci = static_cast<int>(std::floor(pos.x / dx));
        int cj = static_cast<int>(std::floor(pos.y / dx));

        if (ci >= 0 && ci < nx && cj >= 0 && cj < ny)
        {
            if (grid.cell_type(ci, cj) == SOLID)
            {
                std::cout << "WARNING: PARTICLE IN SOLID, A CELL HAS BEEN CONVERTED" << std::endl;
            }
            grid.cell_type(ci, cj) = FLUID;
        }

        {
            double fx = pos.x / dx - 0.5;
            double fy = pos.y / dx - 0.5;
            int i = static_cast<int>(std::floor(fx));
            int j = static_cast<int>(std::floor(fy));
            double wx = fx - i;
            double wy = fy - j;

            double vp = (dx * dx) / 4.0; // assume 4 particles per full cell

            for (int di = 0; di < 2; ++di)
            {
                for (int dj = 0; dj < 2; ++dj)
                {
                    int ii = i + di;
                    int jj = j + dj;
                    if (ii < 0 || ii >= nx || jj < 0 || jj >= ny)
                        continue;
                    double w = (1 - std::fabs(wx - di)) * (1 - std::fabs(wy - dj));
                    fluid_fraction[ii + jj * nx] += vp * w / (dx * dx); // fraction of cell area
                }
            }
        }
    }

    // normalize
    // u
    for (int i = 0; i <= nx; ++i)
    {
        for (int j = 0; j < ny; ++j)
        {
            double& val = grid.u().u(i, j);
            double w = u_weights[grid_index::u(i, j, nx, ny)];
            if (w != 0)
                val /= w;
        }
    }

    // v
    for (int i = 0; i < nx; ++i)
    {
        for (int j = 0; j <= ny; ++j)
        {
            double& val = grid.u().v(i, j);
            double w = v_weights[grid_index::v(i, j, nx, ny)];
            if (w != 0)
                val /= w;
        }
    }

    grid.save_grid_velocity();
}
