#pragma once

#include "../core/mac_grid.hh"

namespace Fluid_Simulation_2D
{
glm::dvec3 orbital_angular_momentum(World& world, MAC_Grid_2D& grid);
glm::dvec3 core_angular_momentum(World& world, MAC_Grid_2D& grid, double radius);
glm::dvec3 particle_represented_angular_momentum(World& world, MAC_Grid_2D& grid);
glm::dvec3 grid_angular_momentum(World& world, MAC_Grid_2D& grid);
}
