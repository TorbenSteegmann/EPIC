#include "particle_to_grid_3d.hh"
#include "polypic.hh"
#include "../../profile_timer.hh"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace Fluid_Simulation_3D
{
void particle_to_grid_3d(World& world, MAC_Grid_3D& grid)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_3D::particle_to_grid_3d");

    grid.clear();

    auto particles = grid.particles();

    double dx = grid.dx();
    int nx = grid.nx();
    int ny = grid.ny();
    int nz = grid.nz();

    std::vector<double> u_weights((nx + 1) * ny * nz, 0.0);
    std::vector<double> v_weights(nx * (ny + 1) * nz, 0.0);
    std::vector<double> w_weights(nx * ny * (nz + 1), 0.0);

    for (auto const& p : particles)
    {
        auto particle = world.get_component<Particle_Component>(p);
        if (!particle)
            continue;

        glm::dvec3 pos = particle->position;
        glm::dvec3 vel = particle->velocity;

        // u
        {
            glm::dvec3 pos_in_grid = pos / dx;
            pos_in_grid.y -= 0.5;
            pos_in_grid.z -= 0.5;
            glm::ivec3 grid_cell(static_cast<int>(std::floor(pos_in_grid.x)),
                                 static_cast<int>(std::floor(pos_in_grid.y)),
                                 static_cast<int>(std::floor(pos_in_grid.z)));

            for (int dk = 0; dk < 2; ++dk)
            {
                for (int dj = 0; dj < 2; ++dj)
                {
                    for (int di = 0; di < 2; ++di)
                    {
                        int grid_x = grid_cell.x + di;
                        int grid_y = grid_cell.y + dj;
                        int grid_z = grid_cell.z + dk;

                        if (grid_x < 0 || grid_x > nx || grid_y < 0 || grid_y >= ny || grid_z < 0 || grid_z >= nz)
                            continue;

                        double weight_x = 1.0 - std::abs(pos_in_grid.x - grid_x);
                        double weight_y = 1.0 - std::abs(pos_in_grid.y - grid_y);
                        double weight_z = 1.0 - std::abs(pos_in_grid.z - grid_z);
                        double weight = std::max(0.0, weight_x) * std::max(0.0, weight_y) * std::max(0.0, weight_z);

                        glm::dvec3 distance_vec = glm::dvec3(grid_x, grid_y, grid_z) - pos_in_grid;
                        double face_value;
                        if (world.settings().FLUID_SOLVER == POLYPIC)
                            face_value = polypic_p2g_face_value_3d(*particle, 0, distance_vec, world.settings().POLYPIC_MODES);
                        else
                        {
                            double c_x = world.settings().FLUID_SOLVER == APIC ? glm::dot(particle->c_u_3d, distance_vec) : 0.0;
                            face_value = vel.x + c_x;
                        }

                        double vel_update = weight * face_value;

                        grid.velocity().u(grid_x, grid_y, grid_z) += vel_update;
                        u_weights[grid_index_3d::u(grid_x, grid_y, grid_z, nx, ny, nz)] += weight;
                    }
                }
            }
        }

        // v
        {
            glm::dvec3 pos_in_grid = pos / dx;
            pos_in_grid.x -= 0.5;
            pos_in_grid.z -= 0.5;
            glm::ivec3 grid_cell(static_cast<int>(std::floor(pos_in_grid.x)),
                                 static_cast<int>(std::floor(pos_in_grid.y)),
                                 static_cast<int>(std::floor(pos_in_grid.z)));

            for (int dk = 0; dk < 2; ++dk)
            {
                for (int dj = 0; dj < 2; ++dj)
                {
                    for (int di = 0; di < 2; ++di)
                    {
                        int grid_x = grid_cell.x + di;
                        int grid_y = grid_cell.y + dj;
                        int grid_z = grid_cell.z + dk;

                        if (grid_x < 0 || grid_x >= nx || grid_y < 0 || grid_y > ny || grid_z < 0 || grid_z >= nz)
                            continue;

                        double weight_x = 1.0 - std::abs(pos_in_grid.x - grid_x);
                        double weight_y = 1.0 - std::abs(pos_in_grid.y - grid_y);
                        double weight_z = 1.0 - std::abs(pos_in_grid.z - grid_z);
                        double weight = std::max(0.0, weight_x) * std::max(0.0, weight_y) * std::max(0.0, weight_z);

                        glm::dvec3 distance_vec = glm::dvec3(grid_x, grid_y, grid_z) - pos_in_grid;
                        double face_value;
                        if (world.settings().FLUID_SOLVER == POLYPIC)
                            face_value = polypic_p2g_face_value_3d(*particle, 1, distance_vec, world.settings().POLYPIC_MODES);
                        else
                        {
                            double c_y = world.settings().FLUID_SOLVER == APIC ? glm::dot(particle->c_v_3d, distance_vec) : 0.0;
                            face_value = vel.y + c_y;
                        }

                        double vel_update = weight * face_value;

                        grid.velocity().v(grid_x, grid_y, grid_z) += vel_update;
                        v_weights[grid_index_3d::v(grid_x, grid_y, grid_z, nx, ny, nz)] += weight;
                    }
                }
            }
        }

        // w
        {
            glm::dvec3 pos_in_grid = pos / dx;
            pos_in_grid.x -= 0.5;
            pos_in_grid.y -= 0.5;
            glm::ivec3 grid_cell(static_cast<int>(std::floor(pos_in_grid.x)),
                                 static_cast<int>(std::floor(pos_in_grid.y)),
                                 static_cast<int>(std::floor(pos_in_grid.z)));

            for (int dk = 0; dk < 2; ++dk)
            {
                for (int dj = 0; dj < 2; ++dj)
                {
                    for (int di = 0; di < 2; ++di)
                    {
                        int grid_x = grid_cell.x + di;
                        int grid_y = grid_cell.y + dj;
                        int grid_z = grid_cell.z + dk;

                        if (grid_x < 0 || grid_x >= nx || grid_y < 0 || grid_y >= ny || grid_z < 0 || grid_z > nz)
                            continue;

                        double weight_x = 1.0 - std::abs(pos_in_grid.x - grid_x);
                        double weight_y = 1.0 - std::abs(pos_in_grid.y - grid_y);
                        double weight_z = 1.0 - std::abs(pos_in_grid.z - grid_z);
                        double weight = std::max(0.0, weight_x) * std::max(0.0, weight_y) * std::max(0.0, weight_z);

                        glm::dvec3 distance_vec = glm::dvec3(grid_x, grid_y, grid_z) - pos_in_grid;
                        double face_value;
                        if (world.settings().FLUID_SOLVER == POLYPIC)
                            face_value = polypic_p2g_face_value_3d(*particle, 2, distance_vec, world.settings().POLYPIC_MODES);
                        else
                        {
                            double c_z = world.settings().FLUID_SOLVER == APIC ? glm::dot(particle->c_w_3d, distance_vec) : 0.0;
                            face_value = vel.z + c_z;
                        }

                        double vel_update = weight * face_value;

                        grid.velocity().w(grid_x, grid_y, grid_z) += vel_update;
                        w_weights[grid_index_3d::w(grid_x, grid_y, grid_z, nx, ny, nz)] += weight;
                    }
                }
            }
        }

        int ci = static_cast<int>(std::floor(pos.x / dx));
        int cj = static_cast<int>(std::floor(pos.y / dx));
        int ck = static_cast<int>(std::floor(pos.z / dx));

        if (ci >= 0 && ci < nx && cj >= 0 && cj < ny && ck >= 0 && ck < nz)
        {
            if (grid.cell_type(ci, cj, ck) == SOLID)
            {
                std::cout << "WARNING: 3D PARTICLE IN SOLID, CELL KEPT SOLID" << std::endl;
            }
            else
            {
                grid.cell_type(ci, cj, ck) = FLUID;
            }
        }
    }

    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i <= nx; ++i)
            {
                double& val = grid.velocity().u(i, j, k);
                double weight = u_weights[grid_index_3d::u(i, j, k, nx, ny, nz)];
                if (weight != 0.0)
                    val /= weight;
            }

    for (int k = 0; k < nz; ++k)
        for (int j = 0; j <= ny; ++j)
            for (int i = 0; i < nx; ++i)
            {
                double& val = grid.velocity().v(i, j, k);
                double weight = v_weights[grid_index_3d::v(i, j, k, nx, ny, nz)];
                if (weight != 0.0)
                    val /= weight;
            }

    for (int k = 0; k <= nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
            {
                double& val = grid.velocity().w(i, j, k);
                double weight = w_weights[grid_index_3d::w(i, j, k, nx, ny, nz)];
                if (weight != 0.0)
                    val /= weight;
            }

    grid.save_grid_velocity();
}
} // namespace Fluid_Simulation_3D
