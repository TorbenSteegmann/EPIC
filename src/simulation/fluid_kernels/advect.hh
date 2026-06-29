#pragma once

#include "../core/mac_grid.hh"

namespace Fluid_Simulation_2D
{
void advect(World& world, MAC_Grid_2D& grid, double dt);
};
