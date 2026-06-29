#include "projection_3d.hh"
#include "../../profile_timer.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace Fluid_Simulation_3D
{
template <typename T>
T dot_product(std::vector<T> const& v1, std::vector<T> const& v2)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_3D::projection_dot_product");

    if (v1.size() != v2.size())
        throw std::invalid_argument("Vectors must have the same size");

    T result = 0;
    for (size_t i = 0; i < v1.size(); ++i)
        result += v1[i] * v2[i];

    return result;
}

std::vector<double> setup_rhs(MAC_Grid_3D& grid, std::vector<std::tuple<int, int, int>> const& fluid_cell_map, double dt, double fluid_density)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_3D::setup_rhs");

    int N = static_cast<int>(fluid_cell_map.size());
    std::vector<double> rhs(N, 0.0);
    double const scale = fluid_density / dt;

    for (int cell = 0; cell < N; ++cell)
    {
        auto [i, j, k] = fluid_cell_map[cell];

        double div = 0.0;

        div += grid.velocity().u(i + 1, j, k) - grid.velocity().u(i, j, k);
        div += grid.velocity().v(i, j + 1, k) - grid.velocity().v(i, j, k);
        div += grid.velocity().w(i, j, k + 1) - grid.velocity().w(i, j, k);

        div /= grid.dx();

        rhs[cell] = -div;

        double solid_velocity = 0.0;
        if (i - 1 >= 0 && grid.cell_type(i - 1, j, k) == SOLID)
            rhs[cell] -= ((grid.velocity().u(i, j, k) - solid_velocity) / grid.dx());
        if (i + 1 < grid.nx() && grid.cell_type(i + 1, j, k) == SOLID)
            rhs[cell] += ((grid.velocity().u(i + 1, j, k) - solid_velocity) / grid.dx());
        if (j - 1 >= 0 && grid.cell_type(i, j - 1, k) == SOLID)
            rhs[cell] -= ((grid.velocity().v(i, j, k) - solid_velocity) / grid.dx());
        if (j + 1 < grid.ny() && grid.cell_type(i, j + 1, k) == SOLID)
            rhs[cell] += ((grid.velocity().v(i, j + 1, k) - solid_velocity) / grid.dx());
        if (k - 1 >= 0 && grid.cell_type(i, j, k - 1) == SOLID)
            rhs[cell] -= ((grid.velocity().w(i, j, k) - solid_velocity) / grid.dx());
        if (k + 1 < grid.nz() && grid.cell_type(i, j, k + 1) == SOLID)
            rhs[cell] += ((grid.velocity().w(i, j, k + 1) - solid_velocity) / grid.dx());

        rhs[cell] *= scale;
    }

    return rhs;
}

Projection_Diagnostics projection_3d(World& world, MAC_Grid_3D& grid, double dt)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_3D::projection_3d");

    Projection_Diagnostics diagnostics;
    double fluid_density = 1000.0;

    std::vector<std::tuple<int, int, int>> fluid_cell_map;
    std::vector<int> cell_to_fluid_index(grid.nx() * grid.ny() * grid.nz(), -1);
    int fluid_cell_num = 0;

    for (int k = 0; k < grid.nz(); ++k)
    {
        for (int j = 0; j < grid.ny(); ++j)
        {
            for (int i = 0; i < grid.nx(); ++i)
            {
                if (grid.cell_type(i, j, k) != FLUID)
                    continue;

                fluid_cell_map.emplace_back(i, j, k);
                int center_index = grid_index_3d::center(i, j, k, grid.nx(), grid.ny(), grid.nz());
                cell_to_fluid_index[center_index] = fluid_cell_num++;
            }
        }
    }
    diagnostics.fluid_cell_count = fluid_cell_num;

    std::vector<double> rhs_divergence_b = setup_rhs(grid, fluid_cell_map, dt, fluid_density);

    std::vector<double> solution_guess_x(fluid_cell_num, 0.0);
    std::vector<double> residual_vector = rhs_divergence_b;
    std::vector<double> lhs_pressure_p = residual_vector;
    std::vector<double> lhs_Ap(fluid_cell_num, 0.0);
    std::vector<double> z(fluid_cell_num, 0.0);

    double residual_norm = dot_product<double>(residual_vector, residual_vector);
    diagnostics.initial_residual = std::sqrt(residual_norm);

    double neighbor_scale = 1.0 / (grid.dx() * grid.dx());

    auto Ap_calc
    = [neighbor_scale, fluid_cell_num, &fluid_cell_map, &cell_to_fluid_index, &grid](std::vector<double> const& lhs_pressure_p, std::vector<double>& lhs_Ap) -> void
    {
        FLUID_PROFILE_SCOPE("Fluid_Simulation_3D::projection_Ap_calc");

        for (int cell = 0; cell < fluid_cell_num; ++cell)
        {
            auto [i, j, k] = fluid_cell_map[cell];

            double neighbor_sum = 0.0;
            double diag_sum = 0.0;

            const std::array<std::tuple<int, int, int>, 6> offsets{{{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}}};
            for (auto [dx, dy, dz] : offsets)
            {
                int ni = i + dx, nj = j + dy, nk = k + dz;

                if (ni < 0 || nj < 0 || nk < 0 || ni >= grid.nx() || nj >= grid.ny() || nk >= grid.nz() || grid.cell_type(ni, nj, nk) == SOLID)
                    continue;

                diag_sum += neighbor_scale;

                if (grid.cell_type(ni, nj, nk) == AIR)
                    continue;

                int neighbor_index = cell_to_fluid_index[grid_index_3d::center(ni, nj, nk, grid.nx(), grid.ny(), grid.nz())];
                neighbor_sum += neighbor_scale * lhs_pressure_p[neighbor_index];
            }

            lhs_Ap[cell] = diag_sum * lhs_pressure_p[cell] - neighbor_sum;
        }
    };

    int const MAX_CG_ITERS = 2000;
    double const CG_TOLERANCE = 1e-8;

    std::vector<double> inv_diag(fluid_cell_num, 0.0);
    for (int cell = 0; cell < fluid_cell_num; ++cell)
    {
        auto [i, j, k] = fluid_cell_map[cell];
        double diag_sum = 0.0;

        const std::array<std::tuple<int, int, int>, 6> offsets{{{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}}};
        for (auto [dx, dy, dz] : offsets)
        {
            int ni = i + dx, nj = j + dy, nk = k + dz;

            if (ni < 0 || nj < 0 || nk < 0 || ni >= grid.nx() || nj >= grid.ny() || nk >= grid.nz() || grid.cell_type(ni, nj, nk) == SOLID)
                continue;

            diag_sum += neighbor_scale;
        }

        if (diag_sum > 0.0)
            inv_diag[cell] = 1.0 / diag_sum;
    }

    for (int k = 0; k < MAX_CG_ITERS; ++k)
    {
        diagnostics.iterations = k + 1;

        for (int i = 0; i < fluid_cell_num; ++i)
            z[i] = residual_vector[i] * inv_diag[i];

        double rz = dot_product(residual_vector, z);

        if (k == 0)
            lhs_pressure_p = z;
        else
        {
            double beta = rz / residual_norm;
            for (int i = 0; i < fluid_cell_num; ++i)
                lhs_pressure_p[i] = z[i] + beta * lhs_pressure_p[i];
        }

        Ap_calc(lhs_pressure_p, lhs_Ap);
        double alpha = rz / (dot_product(lhs_pressure_p, lhs_Ap) + 1e-12);

        for (int i = 0; i < fluid_cell_num; ++i)
        {
            solution_guess_x[i] += alpha * lhs_pressure_p[i];
            residual_vector[i] -= alpha * lhs_Ap[i];
        }

        double residual_norm_new = dot_product(residual_vector, residual_vector);
        if (residual_norm_new < CG_TOLERANCE * CG_TOLERANCE)
        {
            diagnostics.converged = true;
            break;
        }

        residual_norm = rz;
    }
    diagnostics.final_residual = std::sqrt(dot_product<double>(residual_vector, residual_vector));
    if (diagnostics.initial_residual > 0.0)
        diagnostics.residual_ratio = diagnostics.final_residual / diagnostics.initial_residual;

    double pressure_sum = 0.0;
    for (int cell = 0; cell < fluid_cell_num; ++cell)
    {
        auto [i, j, k] = fluid_cell_map[cell];

        grid.pressure(i, j, k) = solution_guess_x[cell];
        if (cell == 0)
        {
            diagnostics.pressure_min = solution_guess_x[cell];
            diagnostics.pressure_max = solution_guess_x[cell];
        }
        diagnostics.pressure_min = std::min(diagnostics.pressure_min, solution_guess_x[cell]);
        diagnostics.pressure_max = std::max(diagnostics.pressure_max, solution_guess_x[cell]);
        pressure_sum += solution_guess_x[cell];
    }
    if (fluid_cell_num > 0)
        diagnostics.pressure_mean = pressure_sum / static_cast<double>(fluid_cell_num);

    double const invRhoDx = dt / (fluid_density * grid.dx());

    for (int k = 0; k < grid.nz(); ++k)
    {
        for (int j = 0; j < grid.ny(); ++j)
        {
            for (int i = 1; i < grid.nx(); ++i)
            {
                if (grid.cell_type(i - 1, j, k) != SOLID && grid.cell_type(i, j, k) != SOLID)
                {
                    double pL = grid.pressure(i - 1, j, k);
                    double pR = grid.pressure(i, j, k);
                    double delta = (pR - pL) * invRhoDx;
                    diagnostics.max_pressure_gradient = std::max(diagnostics.max_pressure_gradient, std::abs((pR - pL) / grid.dx()));
                    diagnostics.max_velocity_delta = std::max(diagnostics.max_velocity_delta, std::abs(delta));

                    grid.velocity().u(i, j, k) -= delta;
                }
            }
        }
    }

    for (int k = 0; k < grid.nz(); ++k)
    {
        for (int j = 1; j < grid.ny(); ++j)
        {
            for (int i = 0; i < grid.nx(); ++i)
            {
                if (grid.cell_type(i, j - 1, k) != SOLID && grid.cell_type(i, j, k) != SOLID)
                {
                    double pB = grid.pressure(i, j - 1, k);
                    double pT = grid.pressure(i, j, k);
                    double delta = (pT - pB) * invRhoDx;
                    diagnostics.max_pressure_gradient = std::max(diagnostics.max_pressure_gradient, std::abs((pT - pB) / grid.dx()));
                    diagnostics.max_velocity_delta = std::max(diagnostics.max_velocity_delta, std::abs(delta));

                    grid.velocity().v(i, j, k) -= delta;
                }
            }
        }
    }

    for (int k = 1; k < grid.nz(); ++k)
    {
        for (int j = 0; j < grid.ny(); ++j)
        {
            for (int i = 0; i < grid.nx(); ++i)
            {
                if (grid.cell_type(i, j, k - 1) != SOLID && grid.cell_type(i, j, k) != SOLID)
                {
                    double pB = grid.pressure(i, j, k - 1);
                    double pF = grid.pressure(i, j, k);
                    double delta = (pF - pB) * invRhoDx;
                    diagnostics.max_pressure_gradient = std::max(diagnostics.max_pressure_gradient, std::abs((pF - pB) / grid.dx()));
                    diagnostics.max_velocity_delta = std::max(diagnostics.max_velocity_delta, std::abs(delta));

                    grid.velocity().w(i, j, k) -= delta;
                }
            }
        }
    }

    return diagnostics;
}
} // namespace Fluid_Simulation_3D
