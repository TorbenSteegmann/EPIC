#pragma once

#include "../core/mac_grid_3d.hh"
#include "../core/simulation.hh"
#include "../../world.hh"

namespace Fluid_Simulation_3D
{
Projection_Diagnostics projection_3d(World& world, MAC_Grid_3D& grid, double dt);
} // namespace Fluid_Simulation_3D
