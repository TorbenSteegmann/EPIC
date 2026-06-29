#pragma once
#include "../core/mac_grid.hh"
#include "../core/mac_grid_3d.hh"

#include "glm/glm.hpp"

#include <cstdint>

namespace Fluid_Simulation_2D
{
void add_water_box(World& world, MAC_Grid_2D& grid, glm::dvec2 min_corner, glm::dvec2 max_corner, bool jitter_particles = true, std::uint32_t jitter_seed = 0);
};

namespace Fluid_Simulation_3D
{
void add_water_box(World& world, MAC_Grid_3D& grid, glm::dvec3 min_corner, glm::dvec3 max_corner, bool jitter_particles = true, std::uint32_t jitter_seed = 0);
};
