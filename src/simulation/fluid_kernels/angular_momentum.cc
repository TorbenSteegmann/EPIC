#include "angular_momentum.hh"

#include "polypic.hh"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Fluid_Simulation_2D
{
namespace
{
double particle_mass(World& world, ecs::Entity entity)
{
    if (auto const* mass = world.get_component<Mass_Component>(entity))
        return mass->mass;
    return 1.0;
}

glm::dvec2 particle_center_of_mass(World& world, MAC_Grid_2D& grid)
{
    glm::dvec2 weighted_position(0.0);
    double total_mass = 0.0;
    for (ecs::Entity const entity : grid.particles())
    {
        auto const* particle = world.get_component<Particle_Component>(entity);
        if (!particle)
            continue;

        double const mass = particle_mass(world, entity);
        weighted_position += mass * glm::dvec2(particle->position);
        total_mass += mass;
    }

    return total_mass > 0.0 ? weighted_position / total_mass : glm::dvec2(0.0);
}

double reconstructed_face_velocity(World& world, Particle_Component const& particle, int component, glm::dvec2 distance)
{
    if (world.settings().FLUID_SOLVER == POLYPIC)
        return polypic_p2g_face_value(particle, component, distance, world.settings().POLYPIC_MODES);

    double value = particle.velocity[component];
    if (world.settings().FLUID_SOLVER == APIC)
        value += glm::dot(component == 0 ? particle.c_u : particle.c_v, distance);
    return value;
}
} // namespace

glm::dvec3 orbital_angular_momentum(World& world, MAC_Grid_2D& grid)
{
    glm::dvec2 const center = particle_center_of_mass(world, grid);
    double angular_momentum = 0.0;
    for (ecs::Entity const entity : grid.particles())
    {
        auto const* particle = world.get_component<Particle_Component>(entity);
        if (!particle)
            continue;

        glm::dvec2 const offset = glm::dvec2(particle->position) - center;
        glm::dvec2 const velocity(particle->velocity);
        angular_momentum += particle_mass(world, entity) * (offset.x * velocity.y - offset.y * velocity.x);
    }
    return glm::dvec3(0.0, 0.0, angular_momentum);
}

glm::dvec3 core_angular_momentum(World& world, MAC_Grid_2D& grid, double radius)
{
    glm::dvec2 const center = particle_center_of_mass(world, grid);
    double const radius_sq = radius * radius;
    double angular_momentum = 0.0;
    for (ecs::Entity const entity : grid.particles())
    {
        auto const* particle = world.get_component<Particle_Component>(entity);
        if (!particle)
            continue;

        glm::dvec2 const offset = glm::dvec2(particle->position) - center;
        if (radius > 0.0 && glm::dot(offset, offset) > radius_sq)
            continue;

        glm::dvec2 const velocity(particle->velocity);
        angular_momentum += particle_mass(world, entity) * (offset.x * velocity.y - offset.y * velocity.x);
    }
    return glm::dvec3(0.0, 0.0, angular_momentum);
}

glm::dvec3 particle_represented_angular_momentum(World& world, MAC_Grid_2D& grid)
{
    glm::dvec2 const center = particle_center_of_mass(world, grid);
    double const dx = grid.dx();
    double angular_momentum = 0.0;

    for (ecs::Entity const entity : grid.particles())
    {
        auto const* particle = world.get_component<Particle_Component>(entity);
        if (!particle)
            continue;

        double const mass = particle_mass(world, entity);
        glm::dvec2 const position(particle->position);

        glm::dvec2 u_position = position / dx;
        u_position.y -= 0.5;
        glm::ivec2 const u_cell(glm::floor(u_position));
        for (int dj = 0; dj < 2; ++dj)
        {
            for (int di = 0; di < 2; ++di)
            {
                int const i = u_cell.x + di;
                int const j = u_cell.y + dj;
                if (i < 0 || i > grid.nx() || j < 0 || j >= grid.ny())
                    continue;

                double const weight = std::max(0.0, 1.0 - std::abs(u_position.x - i)) *
                                      std::max(0.0, 1.0 - std::abs(u_position.y - j));
                glm::dvec2 const distance = glm::dvec2(i, j) - u_position;
                double const velocity = reconstructed_face_velocity(world, *particle, 0, distance);
                double const face_y = grid.origin().y + (static_cast<double>(j) + 0.5) * dx;
                angular_momentum -= (face_y - center.y) * mass * weight * velocity;
            }
        }

        glm::dvec2 v_position = position / dx;
        v_position.x -= 0.5;
        glm::ivec2 const v_cell(glm::floor(v_position));
        for (int dj = 0; dj < 2; ++dj)
        {
            for (int di = 0; di < 2; ++di)
            {
                int const i = v_cell.x + di;
                int const j = v_cell.y + dj;
                if (i < 0 || i >= grid.nx() || j < 0 || j > grid.ny())
                    continue;

                double const weight = std::max(0.0, 1.0 - std::abs(v_position.x - i)) *
                                      std::max(0.0, 1.0 - std::abs(v_position.y - j));
                glm::dvec2 const distance = glm::dvec2(i, j) - v_position;
                double const velocity = reconstructed_face_velocity(world, *particle, 1, distance);
                double const face_x = grid.origin().x + (static_cast<double>(i) + 0.5) * dx;
                angular_momentum += (face_x - center.x) * mass * weight * velocity;
            }
        }
    }

    return glm::dvec3(0.0, 0.0, angular_momentum);
}

glm::dvec3 grid_angular_momentum(World& world, MAC_Grid_2D& grid)
{
    glm::dvec2 const center = particle_center_of_mass(world, grid);
    double const dx = grid.dx();
    std::vector<double> u_mass(static_cast<std::size_t>((grid.nx() + 1) * grid.ny()), 0.0);
    std::vector<double> v_mass(static_cast<std::size_t>(grid.nx() * (grid.ny() + 1)), 0.0);

    for (ecs::Entity const entity : grid.particles())
    {
        auto const* particle = world.get_component<Particle_Component>(entity);
        if (!particle)
            continue;

        double const mass = particle_mass(world, entity);
        glm::dvec2 const position(particle->position);

        glm::dvec2 u_position = position / dx;
        u_position.y -= 0.5;
        glm::ivec2 const u_cell(glm::floor(u_position));
        for (int dj = 0; dj < 2; ++dj)
        {
            for (int di = 0; di < 2; ++di)
            {
                int const i = u_cell.x + di;
                int const j = u_cell.y + dj;
                if (i < 0 || i > grid.nx() || j < 0 || j >= grid.ny())
                    continue;

                double const weight = std::max(0.0, 1.0 - std::abs(u_position.x - i)) *
                                      std::max(0.0, 1.0 - std::abs(u_position.y - j));
                u_mass[grid_index::u(i, j, grid.nx(), grid.ny())] += mass * weight;
            }
        }

        glm::dvec2 v_position = position / dx;
        v_position.x -= 0.5;
        glm::ivec2 const v_cell(glm::floor(v_position));
        for (int dj = 0; dj < 2; ++dj)
        {
            for (int di = 0; di < 2; ++di)
            {
                int const i = v_cell.x + di;
                int const j = v_cell.y + dj;
                if (i < 0 || i >= grid.nx() || j < 0 || j > grid.ny())
                    continue;

                double const weight = std::max(0.0, 1.0 - std::abs(v_position.x - i)) *
                                      std::max(0.0, 1.0 - std::abs(v_position.y - j));
                v_mass[grid_index::v(i, j, grid.nx(), grid.ny())] += mass * weight;
            }
        }
    }

    double angular_momentum = 0.0;
    for (int j = 0; j < grid.ny(); ++j)
    {
        double const face_y = grid.origin().y + (static_cast<double>(j) + 0.5) * dx;
        for (int i = 0; i <= grid.nx(); ++i)
            angular_momentum -= (face_y - center.y) * u_mass[grid_index::u(i, j, grid.nx(), grid.ny())] * grid.u().u(i, j);
    }
    for (int j = 0; j <= grid.ny(); ++j)
    {
        for (int i = 0; i < grid.nx(); ++i)
        {
            double const face_x = grid.origin().x + (static_cast<double>(i) + 0.5) * dx;
            angular_momentum += (face_x - center.x) * v_mass[grid_index::v(i, j, grid.nx(), grid.ny())] * grid.u().v(i, j);
        }
    }

    return glm::dvec3(0.0, 0.0, angular_momentum);
}
} // namespace Fluid_Simulation_2D
