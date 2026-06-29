#pragma once

#include "../core/mac_grid.hh"

namespace Fluid_Simulation_2D
{
void add_confined_vortex(World& world, MAC_Grid_2D& grid, double amplitude = 1.0);
double confined_vortex_support_radius(MAC_Grid_2D& grid);
}; // namespace Fluid_Simulation_2D
