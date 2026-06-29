#pragma once

#include "../core/mac_grid.hh"
#include "../core/mac_grid_3d.hh"

struct Settings;

namespace Fluid_Simulation
{
void apply_forces(MAC_Grid_2D& grid, double dt);
void apply_forces(MAC_Grid_3D& grid, double dt);
void apply_forces(MAC_Grid_2D& grid, double dt, Settings const& settings);
void apply_forces(MAC_Grid_3D& grid, double dt, Settings const& settings);
} // namespace Fluid_Simulation

namespace Fluid_Simulation_2D
{
void apply_forces_2d(MAC_Grid_2D& grid, double dt);
} // namespace Fluid_Simulation_2D

namespace Fluid_Simulation_3D
{
void apply_forces_3d(MAC_Grid_3D& grid, double dt);
} // namespace Fluid_Simulation_3D
