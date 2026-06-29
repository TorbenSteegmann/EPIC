#pragma once

#include "../../world.hh"
#include "../core/mac_grid.hh"
#include "../core/simulation.hh"

namespace Fluid_Simulation_2D
{
Projection_Diagnostics projection_2d(World& world, MAC_Grid_2D& grid, double dt);
void pressure_gradiant_update(World& world, MAC_Grid_2D& grid, double dt);
}; // namespace Fluid_Simulation_2D
