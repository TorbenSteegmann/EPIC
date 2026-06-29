#pragma once

#include "../core/mac_grid_3d.hh"

#include <span>

namespace Fluid_Simulation_3D
{
double sample_span(std::span<double const> data, int i, int j, int k, int dim_i, int dim_j, int dim_k);

double interpolate_component(MAC_Grid_3D& grid, glm::dvec3 pos, std::span<double const> data,
                             int dim_i, int dim_j, int dim_k, double ox, double oy, double oz);

glm::dvec3 interpolate_grid_velocity(MAC_Grid_3D& grid, glm::dvec3 pos);
} // namespace Fluid_Simulation_3D
