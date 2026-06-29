#pragma once

#include "../core/mac_grid_3d.hh"

namespace Fluid_Simulation_3D
{
void extrapolate_velocity_3d(MAC_Grid_3D& grid, double dt);
} // namespace Fluid_Simulation_3D
