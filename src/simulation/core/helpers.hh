#pragma once

#include "mac_grid.hh"

#include <Eigen/Dense>

namespace Fluid_Simulation_2D
{
double sample_span(std::span<double const> data, int i, int j, int dim_i, int dim_j);

double interpolate_component(MAC_Grid_2D& grid, glm::dvec2 pos, std::span<double const> data, int dim_i, int dim_j, double ox, double oy);

glm::dvec2 interpolate_grid_velocity(MAC_Grid_2D& grid, glm::dvec2 pos);

double determinant_2D(glm::dmat2);

Eigen::Matrix2d glm_to_eigen(glm::dmat2 mat);

glm::dmat2 eigen_to_glm(Eigen::Matrix2d const& mat);
}; // namespace Fluid_Simulation_2D
