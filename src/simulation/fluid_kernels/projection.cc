#include "projection.hh"
#include "../../profile_timer.hh"

#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <stdexcept>

template <typename T>
T dot_product(std::vector<T> const& v1, std::vector<T> const& v2)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_2D::projection_dot_product");

    if (v1.size() != v2.size())
        throw std::invalid_argument("Vectors must have the same size");

    T result = 0;
    for (size_t i = 0; i < v1.size(); ++i)
        result += v1[i] * v2[i];

    return result;
};

std::vector<double> setup_rhs(MAC_Grid_2D& grid, std::vector<std::pair<int, int>> const& fluid_cell_map, double dt, double fluid_density)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_2D::setup_rhs");

    int N = (int)fluid_cell_map.size();
    std::vector<double> rhs(N, 0.0);
    double const scale = fluid_density / dt;

    for (int k = 0; k < N; ++k)
    {
        auto [i, j] = fluid_cell_map[k];

        double div = 0.0;

        div += grid.u().u(i + 1, j) - grid.u().u(i, j);
        div += grid.u().v(i, j + 1) - grid.u().v(i, j);

        div /= grid.dx();

        rhs[k] = -div;

        double solid_velocity = 0.0;
        if (i - 1 >= 0 && grid.cell_type(i - 1, j) == SOLID)
            rhs[k] -= ((grid.u().u(i, j) - solid_velocity) / grid.dx());
        if (i + 1 < grid.nx() && grid.cell_type(i + 1, j) == SOLID)
            rhs[k] += ((grid.u().u(i + 1, j) - solid_velocity) / grid.dx());
        if (j - 1 >= 0 && grid.cell_type(i, j - 1) == SOLID)
            rhs[k] -= ((grid.u().v(i, j) - solid_velocity) / grid.dx());
        if (j + 1 < grid.ny() && grid.cell_type(i, j + 1) == SOLID)
            rhs[k] += ((grid.u().v(i, j + 1) - solid_velocity) / grid.dx());

        rhs[k] *= scale;
    }
    return rhs;
}

Projection_Diagnostics Fluid_Simulation_2D::projection_2d(World& world, MAC_Grid_2D& grid, double dt)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_2D::projection_2d");

    Projection_Diagnostics diagnostics;
    double fluid_density = 1000.0; // water density

    std::vector<std::pair<int, int>> fluid_cell_map;
    std::map<std::pair<int, int>, int> fluid_index_map; // provide hash later
    int fluid_cell_num = 0;

    for (int i = 0; i < grid.nx(); ++i)
        for (int j = 0; j < grid.ny(); ++j)
        {
            if (grid.cell_type(i, j) != FLUID)
                continue;

            fluid_cell_map.emplace_back(std::make_pair(i, j));
            fluid_index_map[{i, j}] = fluid_cell_num++;
        }
    diagnostics.fluid_cell_count = fluid_cell_num;

    std::vector<double> rhs_divergence_b = setup_rhs(grid, fluid_cell_map, dt, fluid_density); // RVO guaranteed

    std::vector<double> solution_guess_x(fluid_cell_num, 0);

    std::vector<double> residual_vector = rhs_divergence_b;

    std::vector<double> lhs_pressure_p = residual_vector;

    std::vector<double> lhs_Ap(fluid_cell_num, 0);

    std::vector<double> z(fluid_cell_num, 0);

    double residual_norm = dot_product<double>(residual_vector, residual_vector);
    double const initial_residual_norm = residual_norm;
    diagnostics.initial_residual = std::sqrt(residual_norm);
    if (initial_residual_norm == 0.0)
        diagnostics.converged = true;

    //    double neighbor_scale = dt / (water_density * (grid.dx() * grid.dx()));
    double neighbor_scale = 1.0 / (grid.dx() * grid.dx());

    auto Ap_calc
    = [neighbor_scale, fluid_cell_num, &fluid_cell_map, &fluid_index_map, &grid](std::vector<double> const& lhs_pressure_p, std::vector<double>& lhs_Ap) -> void
    {
        FLUID_PROFILE_SCOPE("Fluid_Simulation_2D::projection_Ap_calc");

        for (int k = 0; k < fluid_cell_num; ++k)
        {
            auto [i, j] = fluid_cell_map[k];

            double neighbor_sum = 0.0;
            double diag_sum = 0.0;

            const std::array<std::pair<int, int>, 4> offsets{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
            for (auto [dx, dy] : offsets)
            {
                int ni = i + dx, nj = j + dy;
                if (ni < 0 || nj < 0 || ni >= grid.nx() || nj >= grid.ny() || grid.cell_type(ni, nj) == SOLID)
                    continue;

                diag_sum += neighbor_scale;

                if (grid.cell_type(ni, nj) == AIR)
                    continue;

                neighbor_sum += neighbor_scale * lhs_pressure_p[fluid_index_map[{ni, nj}]];
            }

            lhs_Ap[k] = diag_sum * lhs_pressure_p[k] - neighbor_sum;
        }
    };


    int const MAX_CG_ITERS = 2000;
    double const CG_TOLERANCE = 1e-8; // relative L2 residual

    // preconditioned cg (jacobi)
    std::vector<double> inv_diag(fluid_cell_num, 0.0);
    for (int k = 0; k < fluid_cell_num; ++k)
    {
        auto [i, j] = fluid_cell_map[k];
        double diag_sum = 0.0;

        const std::array<std::pair<int, int>, 4> offsets{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
        for (auto [dx, dy] : offsets)
        {
            int ni = i + dx, nj = j + dy;
            if (ni < 0 || nj < 0 || ni >= grid.nx() || nj >= grid.ny() || grid.cell_type(ni, nj) == SOLID)
                continue;
            diag_sum += neighbor_scale;
        }

        if (diag_sum > 0)
            inv_diag[k] = 1.0 / diag_sum;
    }
    for (int k = 0; k < MAX_CG_ITERS && !diagnostics.converged; ++k)
    {
        diagnostics.iterations = k + 1;
        // Apply preconditioner: z = M⁻¹ * r
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
        if (residual_norm_new < CG_TOLERANCE * CG_TOLERANCE * initial_residual_norm)
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
    for (int k = 0; k < fluid_cell_num; ++k)
    {
        auto [i, j] = fluid_cell_map[k];

        grid.pressure(i, j) = solution_guess_x[k];
        if (k == 0)
        {
            diagnostics.pressure_min = solution_guess_x[k];
            diagnostics.pressure_max = solution_guess_x[k];
        }
        diagnostics.pressure_min = std::min(diagnostics.pressure_min, solution_guess_x[k]);
        diagnostics.pressure_max = std::max(diagnostics.pressure_max, solution_guess_x[k]);
        pressure_sum += solution_guess_x[k];
    }
    if (fluid_cell_num > 0)
        diagnostics.pressure_mean = pressure_sum / static_cast<double>(fluid_cell_num);

    double const invRhoDx = dt / (fluid_density * grid.dx());

    for (int j = 0; j < grid.ny(); ++j)
    {
        for (int i = 1; i < grid.nx(); ++i)
        {
            if (grid.cell_type(i - 1, j) != SOLID && grid.cell_type(i, j) != SOLID)
            {
                double pL = grid.pressure(i - 1, j);
                double pR = grid.pressure(i, j);
                double delta = (pR - pL) * invRhoDx;
                diagnostics.max_pressure_gradient = std::max(diagnostics.max_pressure_gradient, std::abs((pR - pL) / grid.dx()));
                diagnostics.max_velocity_delta = std::max(diagnostics.max_velocity_delta, std::abs(delta));

                grid.u().u(i, j) -= delta;
            }
        }
    }

    for (int j = 1; j < grid.ny(); ++j)
    {
        for (int i = 0; i < grid.nx(); ++i)
        {
            if (grid.cell_type(i, j - 1) != SOLID && grid.cell_type(i, j) != SOLID)
            {
                double pB = grid.pressure(i, j - 1);
                double pT = grid.pressure(i, j);
                double delta = (pT - pB) * invRhoDx;
                diagnostics.max_pressure_gradient = std::max(diagnostics.max_pressure_gradient, std::abs((pT - pB) / grid.dx()));
                diagnostics.max_velocity_delta = std::max(diagnostics.max_velocity_delta, std::abs(delta));

                grid.u().v(i, j) -= delta;
            }
        }
    }

    return diagnostics;
}
