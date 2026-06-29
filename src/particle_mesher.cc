#include "particle_mesher.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
size_t cell_index(glm::ivec2 cell, glm::ivec2 resolution)
{
    return static_cast<size_t>(cell.y * resolution.x + cell.x);
}

void append_quad(Particle_Mesh_2D& mesh, glm::vec2 min, glm::vec2 max)
{
    mesh.vertices.push_back({min.x, min.y});
    mesh.vertices.push_back({max.x, min.y});
    mesh.vertices.push_back({max.x, max.y});

    mesh.vertices.push_back({min.x, min.y});
    mesh.vertices.push_back({max.x, max.y});
    mesh.vertices.push_back({min.x, max.y});
}
} // namespace

Particle_Mesh_2D build_particle_mesh_2d(std::vector<Particle_Component> const& particles)
{
    Particle_Mesh_2D mesh;
    if (particles.empty())
        return mesh;

    glm::vec2 min_bound(std::numeric_limits<float>::max());
    glm::vec2 max_bound(std::numeric_limits<float>::lowest());
    float radius_sum = 0.0f;

    for (auto const& particle : particles)
    {
        glm::vec2 const position = glm::vec2(particle.position);
        float const radius = std::max(particle.radius, 0.001f);
        glm::vec2 const pad(radius * 2.0f);
        min_bound = glm::min(min_bound, position - pad);
        max_bound = glm::max(max_bound, position + pad);
        radius_sum += radius;
    }

    float const average_radius = radius_sum / static_cast<float>(particles.size());
    float const cell_size = std::max(average_radius * 0.75f, 0.2f);
    glm::ivec2 const resolution = glm::max(glm::ivec2(glm::ceil((max_bound - min_bound) / cell_size)), glm::ivec2(1));
    std::vector<std::uint8_t> occupied(static_cast<size_t>(resolution.x * resolution.y), 0);

    for (auto const& particle : particles)
    {
        glm::vec2 const position = glm::vec2(particle.position);
        float const radius = std::max(particle.radius, 0.001f);
        float const splat_radius = radius * 2.1f;
        glm::ivec2 min_cell = glm::clamp(glm::ivec2(glm::floor((position - min_bound - glm::vec2(splat_radius)) / cell_size)), glm::ivec2(0), resolution - glm::ivec2(1));
        glm::ivec2 max_cell = glm::clamp(glm::ivec2(glm::floor((position - min_bound + glm::vec2(splat_radius)) / cell_size)), glm::ivec2(0), resolution - glm::ivec2(1));

        for (int y = min_cell.y; y <= max_cell.y; ++y)
        {
            for (int x = min_cell.x; x <= max_cell.x; ++x)
            {
                glm::vec2 const cell_min = min_bound + glm::vec2(x, y) * cell_size;
                glm::vec2 const center = cell_min + glm::vec2(cell_size * 0.5f);
                if (glm::dot(center - position, center - position) <= splat_radius * splat_radius)
                    occupied[cell_index({x, y}, resolution)] = 1;
            }
        }
    }

    for (int pass = 0; pass < 2; ++pass)
    {
        std::vector<std::uint8_t> closed = occupied;
        for (int y = 1; y < resolution.y - 1; ++y)
        {
            for (int x = 1; x < resolution.x - 1; ++x)
            {
                glm::ivec2 const cell(x, y);
                if (occupied[cell_index(cell, resolution)] != 0)
                    continue;

                int neighbors = 0;
                for (int oy = -1; oy <= 1; ++oy)
                    for (int ox = -1; ox <= 1; ++ox)
                        if (ox != 0 || oy != 0)
                            neighbors += occupied[cell_index(cell + glm::ivec2(ox, oy), resolution)];

                if (neighbors >= 5)
                    closed[cell_index(cell, resolution)] = 1;
            }
        }

        occupied = closed;
    }

    mesh.vertices.reserve(static_cast<size_t>(resolution.x * resolution.y * 6 / 2));

    for (int y = 0; y < resolution.y; ++y)
    {
        for (int x = 0; x < resolution.x; ++x)
        {
            glm::vec2 const cell_min = min_bound + glm::vec2(x, y) * cell_size;
            glm::vec2 const cell_max = cell_min + glm::vec2(cell_size);
            if (occupied[cell_index({x, y}, resolution)] == 0)
                continue;

            append_quad(mesh, cell_min, cell_max);
        }
    }

    return mesh;
}
