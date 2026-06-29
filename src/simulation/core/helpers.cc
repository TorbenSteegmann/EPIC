#include "helpers.hh"

namespace Fluid_Simulation_2D
{
double sample_span(std::span<double const> data, int i, int j, int dim_i, int dim_j) { return data[i + dim_i * j]; }

double interpolate_component(MAC_Grid_2D& grid, glm::dvec2 pos, std::span<double const> data, int dim_i, int dim_j, double ox, double oy)
{
    double dx = grid.dx();

    double fx = pos.x / dx + ox;
    double fy = pos.y / dx + oy;

    int i = static_cast<int>(std::floor(fx)), j = static_cast<int>(std::floor(fy));
    double wx = fx - i, wy = fy - j;

    double result = 0.0;

    for (int di = 0; di < 2; ++di)
    {
        for (int dj = 0; dj < 2; ++dj)
        {
            int ii = i + di, jj = j + dj;

            if (ii < 0 || ii >= dim_i || jj < 0 || jj >= dim_j)
                continue;

            double w = (1 - std::abs(wx - di)) * (1 - std::abs(wy - dj));

            result += w * sample_span(data, ii, jj, dim_i, dim_j);
        }
    }

    return result;
}

glm::dvec2 interpolate_grid_velocity(MAC_Grid_2D& grid, glm::dvec2 pos)
{
    glm::dvec2 v;
    v.x = interpolate_component(grid, pos, grid.u().u_component, grid.nx() + 1, grid.ny(), 0.0, -0.5);
    v.y = interpolate_component(grid, pos, grid.u().v_component, grid.nx(), grid.ny() + 1, -0.5, 0.0);

    return v;
}

double determinant_2D(glm::dmat2 mat)
{
    double ad = mat[0][0] * mat[1][1];
    double bc = mat[0][1] * mat[1][0];

    return ad - bc;
}

Eigen::Matrix2d glm_to_eigen(glm::dmat2 mat)
{
    Eigen::Matrix2d res;
    for (int c = 0; c < 2; ++c)
        for (int r = 0; r < 2; ++r)
            res(r, c) = mat[c][r];

    return res;
}

glm::dmat2 eigen_to_glm(Eigen::Matrix2d const& mat)
{
    glm::dmat2 res;
    for (int c = 0; c < 2; ++c)
        for (int r = 0; r < 2; ++r)
            res[c][r] = mat(r, c);

    return res;
}
} // namespace Fluid_Simulation_2D
