#pragma once

#include "../core/mac_grid_3d.hh"
#include "../../world.hh"

namespace Fluid_Simulation_3D
{
void particle_to_grid_3d(World& world, MAC_Grid_3D& grid);
} // namespace Fluid_Simulation_3D
