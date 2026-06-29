#include "helpers_3d.hh"

#include <cmath>

namespace Fluid_Simulation_3D
{
double sample_span(std::span<double const> data, int i, int j, int k, int dim_i, int dim_j, int dim_k)
{
    return data[i + dim_i * (j + dim_j * k)];
}

double interpolate_component(MAC_Grid_3D& grid, glm::dvec3 pos, std::span<double const> data,
                             int dim_i, int dim_j, int dim_k, double ox, double oy, double oz)
{
    double dx = grid.dx();

    double fx = pos.x / dx + ox;
    double fy = pos.y / dx + oy;
    double fz = pos.z / dx + oz;

    int i = static_cast<int>(std::floor(fx)), j = static_cast<int>(std::floor(fy)), k = static_cast<int>(std::floor(fz));
    double wx = fx - i, wy = fy - j, wz = fz - k;

    double result = 0.0;

    for (int di = 0; di < 2; ++di)
    {
        for (int dj = 0; dj < 2; ++dj)
        {
            for (int dk = 0; dk < 2; ++dk)
            {
                int ii = i + di, jj = j + dj, kk = k + dk;

                if (ii < 0 || ii >= dim_i || jj < 0 || jj >= dim_j || kk < 0 || kk >= dim_k)
                    continue;

                double w = (1 - std::abs(wx - di)) * (1 - std::abs(wy - dj)) * (1 - std::abs(wz - dk));

                result += w * sample_span(data, ii, jj, kk, dim_i, dim_j, dim_k);
            }
        }
    }

    return result;
}

glm::dvec3 interpolate_grid_velocity(MAC_Grid_3D& grid, glm::dvec3 pos)
{
    glm::dvec3 v;
    v.x = interpolate_component(grid, pos, grid.velocity().u_component, grid.nx() + 1, grid.ny(), grid.nz(), 0.0, -0.5, -0.5);
    v.y = interpolate_component(grid, pos, grid.velocity().v_component, grid.nx(), grid.ny() + 1, grid.nz(), -0.5, 0.0, -0.5);
    v.z = interpolate_component(grid, pos, grid.velocity().w_component, grid.nx(), grid.ny(), grid.nz() + 1, -0.5, -0.5, 0.0);

    return v;
}
} // namespace Fluid_Simulation_3D
