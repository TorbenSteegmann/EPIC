#pragma once

#include "ecs/particle_component.hh"

#include <glm/glm.hpp>
#include <vector>

struct Particle_Mesh_2D
{
    std::vector<glm::vec2> vertices;
};

Particle_Mesh_2D build_particle_mesh_2d(std::vector<Particle_Component> const& particles);
