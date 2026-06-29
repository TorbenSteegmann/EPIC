#include "add_water_box.hh"

#include "../../ecs/particle_component.hh"

#include <random>

namespace Fluid_Simulation_2D
{
void add_water_box(World& world, MAC_Grid_2D& grid, glm::dvec2 min_corner, glm::dvec2 max_corner, bool jitter_particles, std::uint32_t jitter_seed)
{
    std::uint32_t const seed = jitter_particles ? (jitter_seed != 0 ? jitter_seed : std::random_device{}()) : 0u;
    std::mt19937 gen(seed);
    std::uniform_real_distribution<double> jitter(-0.25, 0.25);

    for (int i = 0; i < grid.nx(); ++i)
    {
        for (int j = 0; j < grid.ny(); ++j)
        {
            glm::dvec2 cell_pos = (glm::dvec2(i + 0.5, j + 0.5)) * grid.dx();

            if (cell_pos.x < min_corner.x || cell_pos.x > max_corner.x || cell_pos.y < min_corner.y || cell_pos.y > max_corner.y)
            {
                continue;
            }

            if (grid.cell_type(i, j) == SOLID)
                continue;

            grid.cell_type(i, j) = FLUID;

            for (int x = 0; x < 2; ++x)
            {
                for (int y = 0; y < 2; ++y)
                {
                    double jitter_x = jitter_particles ? jitter(gen) : 0.0;
                    double jitter_y = jitter_particles ? jitter(gen) : 0.0;
                    glm::dvec2 particle_pos_world = (glm::dvec2(i + 0.25 + 0.5 * x + jitter_x, j + 0.25 + 0.5 * y + jitter_y)) * grid.dx();

                    ecs::Entity particle = world.create_entity();

                    Particle_Component particle_component;
                    particle_component.position = glm::dvec3(particle_pos_world, 0.0);
                    particle_component.radius = static_cast<float>(0.25 * grid.dx());

                    world.add_component(particle, particle_component);
                    grid.particles().emplace_back(particle);
                }
            }
        }
    }
}
} // namespace Fluid_Simulation_2D

namespace Fluid_Simulation_3D
{
void add_water_box(World& world, MAC_Grid_3D& grid, glm::dvec3 min_corner, glm::dvec3 max_corner, bool jitter_particles, std::uint32_t jitter_seed)
{
    std::uint32_t const seed = jitter_particles ? (jitter_seed != 0 ? jitter_seed : std::random_device{}()) : 0u;
    std::mt19937 gen(seed);
    std::uniform_real_distribution<double> jitter(-0.25, 0.25);

    for (int i = 0; i < grid.nx(); ++i)
    {
        for (int j = 0; j < grid.ny(); ++j)
        {
            for (int k = 0; k < grid.nz(); ++k)
            {
                glm::dvec3 cell_pos = glm::dvec3(i + 0.5, j + 0.5, k + 0.5) * grid.dx();

                if (cell_pos.x < min_corner.x || cell_pos.x > max_corner.x ||
                    cell_pos.y < min_corner.y || cell_pos.y > max_corner.y ||
                    cell_pos.z < min_corner.z || cell_pos.z > max_corner.z)
                {
                    continue;
                }

                if (grid.cell_type(i, j, k) == SOLID)
                    continue;

                grid.cell_type(i, j, k) = FLUID;

                for (int x = 0; x < 2; ++x)
                {
                    for (int y = 0; y < 2; ++y)
                    {
                        for (int z = 0; z < 2; ++z)
                        {
                            double jitter_x = jitter_particles ? jitter(gen) : 0.0;
                            double jitter_y = jitter_particles ? jitter(gen) : 0.0;
                            double jitter_z = jitter_particles ? jitter(gen) : 0.0;
                            glm::dvec3 particle_pos_world = glm::dvec3(i + 0.25 + 0.5 * x + jitter_x,
                                                                        j + 0.25 + 0.5 * y + jitter_y,
                                                                        k + 0.25 + 0.5 * z + jitter_z) *
                                                             grid.dx();

                            ecs::Entity particle = world.create_entity();

                            Particle_Component particle_component;
                            particle_component.position = particle_pos_world;
                            particle_component.radius = static_cast<float>(0.25 * grid.dx());

                            world.add_component(particle, particle_component);
                            grid.particles().emplace_back(particle);
                        }
                    }
                }
            }
        }
    }
}
} // namespace Fluid_Simulation_3D
