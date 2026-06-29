#include "extrapolate_velocity_3d.hh"

#include <array>
#include <limits>
#include <tuple>
#include <vector>
#include "../../profile_timer.hh"

namespace Fluid_Simulation_3D
{
void init_marker_vectors(MAC_Grid_3D& grid, std::vector<int>& marker_vector_u, std::vector<int>& marker_vector_v, std::vector<int>& marker_vector_w)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_3D::init_marker_vectors");

    int nx = grid.nx();
    int ny = grid.ny();
    int nz = grid.nz();

    for (int k = 0; k < nz; ++k)
    {
        for (int j = 0; j < ny; ++j)
        {
            for (int i = 0; i <= nx; ++i)
            {
                bool left_is_fluid = i > 0 && grid.cell_type(i - 1, j, k) == FLUID;
                bool right_is_fluid = i < nx && grid.cell_type(i, j, k) == FLUID;

                if (left_is_fluid || right_is_fluid)
                    marker_vector_u[grid_index_3d::u(i, j, k, nx, ny, nz)] = 0;
            }
        }
    }

    for (int k = 0; k < nz; ++k)
    {
        for (int j = 0; j <= ny; ++j)
        {
            for (int i = 0; i < nx; ++i)
            {
                bool bot_is_fluid = j > 0 && grid.cell_type(i, j - 1, k) == FLUID;
                bool top_is_fluid = j < ny && grid.cell_type(i, j, k) == FLUID;

                if (bot_is_fluid || top_is_fluid)
                    marker_vector_v[grid_index_3d::v(i, j, k, nx, ny, nz)] = 0;
            }
        }
    }

    for (int k = 0; k <= nz; ++k)
    {
        for (int j = 0; j < ny; ++j)
        {
            for (int i = 0; i < nx; ++i)
            {
                bool back_is_fluid = k > 0 && grid.cell_type(i, j, k - 1) == FLUID;
                bool front_is_fluid = k < nz && grid.cell_type(i, j, k) == FLUID;

                if (back_is_fluid || front_is_fluid)
                    marker_vector_w[grid_index_3d::w(i, j, k, nx, ny, nz)] = 0;
            }
        }
    }
}

void extrapolate_velocity_3d(MAC_Grid_3D& grid, double dt)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_3D::extrapolate_velocity_3d");

    int nx = grid.nx();
    int ny = grid.ny();
    int nz = grid.nz();

    std::vector<int> marker_vector_u((nx + 1) * ny * nz, std::numeric_limits<int>::max());
    std::vector<int> marker_vector_v(nx * (ny + 1) * nz, std::numeric_limits<int>::max());
    std::vector<int> marker_vector_w(nx * ny * (nz + 1), std::numeric_limits<int>::max());

    init_marker_vectors(grid, marker_vector_u, marker_vector_v, marker_vector_w);

    std::vector<std::tuple<int, int, int>> grid_indices_W_u;
    std::vector<std::tuple<int, int, int>> grid_indices_W_v;
    std::vector<std::tuple<int, int, int>> grid_indices_W_w;

    std::array<int, 6> di{-1, 0, 1, 0, 0, 0};
    std::array<int, 6> dj{0, 1, 0, -1, 0, 0};
    std::array<int, 6> dk{0, 0, 0, 0, -1, 1};

    for (int k = 0; k < nz; ++k)
    {
        for (int j = 0; j < ny; ++j)
        {
            for (int i = 0; i <= nx; ++i)
            {
                if (marker_vector_u[grid_index_3d::u(i, j, k, nx, ny, nz)] == 0)
                    continue;

                for (int n = 0; n < 6; ++n)
                {
                    int ii = i + di[n];
                    int jj = j + dj[n];
                    int kk = k + dk[n];

                    if (ii < 0 || ii > nx || jj < 0 || jj >= ny || kk < 0 || kk >= nz)
                        continue;

                    if (marker_vector_u[grid_index_3d::u(ii, jj, kk, nx, ny, nz)] == 0)
                    {
                        marker_vector_u[grid_index_3d::u(i, j, k, nx, ny, nz)] = 1;
                        grid_indices_W_u.emplace_back(i, j, k);
                        break;
                    }
                }
            }
        }
    }

    for (int k = 0; k < nz; ++k)
    {
        for (int j = 0; j <= ny; ++j)
        {
            for (int i = 0; i < nx; ++i)
            {
                if (marker_vector_v[grid_index_3d::v(i, j, k, nx, ny, nz)] == 0)
                    continue;

                for (int n = 0; n < 6; ++n)
                {
                    int ii = i + di[n];
                    int jj = j + dj[n];
                    int kk = k + dk[n];

                    if (ii < 0 || ii >= nx || jj < 0 || jj > ny || kk < 0 || kk >= nz)
                        continue;

                    if (marker_vector_v[grid_index_3d::v(ii, jj, kk, nx, ny, nz)] == 0)
                    {
                        marker_vector_v[grid_index_3d::v(i, j, k, nx, ny, nz)] = 1;
                        grid_indices_W_v.emplace_back(i, j, k);
                        break;
                    }
                }
            }
        }
    }

    for (int k = 0; k <= nz; ++k)
    {
        for (int j = 0; j < ny; ++j)
        {
            for (int i = 0; i < nx; ++i)
            {
                if (marker_vector_w[grid_index_3d::w(i, j, k, nx, ny, nz)] == 0)
                    continue;

                for (int n = 0; n < 6; ++n)
                {
                    int ii = i + di[n];
                    int jj = j + dj[n];
                    int kk = k + dk[n];

                    if (ii < 0 || ii >= nx || jj < 0 || jj >= ny || kk < 0 || kk > nz)
                        continue;

                    if (marker_vector_w[grid_index_3d::w(ii, jj, kk, nx, ny, nz)] == 0)
                    {
                        marker_vector_w[grid_index_3d::w(i, j, k, nx, ny, nz)] = 1;
                        grid_indices_W_w.emplace_back(i, j, k);
                        break;
                    }
                }
            }
        }
    }

    int t = 0;
    int const maximum_marker = 3;
    while (t < grid_indices_W_u.size())
    {
        auto [i, j, k] = grid_indices_W_u[t];
        int current_index = grid_index_3d::u(i, j, k, nx, ny, nz);

        if (marker_vector_u[current_index] == maximum_marker)
            break;

        double sum = 0.0;
        int count = 0;
        for (int n = 0; n < 6; ++n)
        {
            int ii = i + di[n];
            int jj = j + dj[n];
            int kk = k + dk[n];

            if (ii < 0 || ii > nx || jj < 0 || jj >= ny || kk < 0 || kk >= nz)
                continue;

            int neighbor_index = grid_index_3d::u(ii, jj, kk, nx, ny, nz);

            if (marker_vector_u[neighbor_index] == std::numeric_limits<int>::max())
            {
                marker_vector_u[neighbor_index] = marker_vector_u[current_index] + 1;
                grid_indices_W_u.emplace_back(ii, jj, kk);
                continue;
            }

            if (marker_vector_u[neighbor_index] < marker_vector_u[current_index])
            {
                sum += grid.velocity().u(ii, jj, kk);
                ++count;
            }
        }
        if (count > 0)
            grid.velocity().u(i, j, k) = sum / static_cast<double>(count);

        ++t;
    }

    t = 0;
    while (t < grid_indices_W_v.size())
    {
        auto [i, j, k] = grid_indices_W_v[t];
        int current_index = grid_index_3d::v(i, j, k, nx, ny, nz);

        if (marker_vector_v[current_index] == maximum_marker)
            break;

        double sum = 0.0;
        int count = 0;
        for (int n = 0; n < 6; ++n)
        {
            int ii = i + di[n];
            int jj = j + dj[n];
            int kk = k + dk[n];

            if (ii < 0 || ii >= nx || jj < 0 || jj > ny || kk < 0 || kk >= nz)
                continue;

            int neighbor_index = grid_index_3d::v(ii, jj, kk, nx, ny, nz);

            if (marker_vector_v[neighbor_index] == std::numeric_limits<int>::max())
            {
                marker_vector_v[neighbor_index] = marker_vector_v[current_index] + 1;
                grid_indices_W_v.emplace_back(ii, jj, kk);
                continue;
            }

            if (marker_vector_v[neighbor_index] < marker_vector_v[current_index])
            {
                sum += grid.velocity().v(ii, jj, kk);
                ++count;
            }
        }
        if (count > 0)
            grid.velocity().v(i, j, k) = sum / static_cast<double>(count);

        ++t;
    }

    t = 0;
    while (t < grid_indices_W_w.size())
    {
        auto [i, j, k] = grid_indices_W_w[t];
        int current_index = grid_index_3d::w(i, j, k, nx, ny, nz);

        if (marker_vector_w[current_index] == maximum_marker)
            break;

        double sum = 0.0;
        int count = 0;
        for (int n = 0; n < 6; ++n)
        {
            int ii = i + di[n];
            int jj = j + dj[n];
            int kk = k + dk[n];

            if (ii < 0 || ii >= nx || jj < 0 || jj >= ny || kk < 0 || kk > nz)
                continue;

            int neighbor_index = grid_index_3d::w(ii, jj, kk, nx, ny, nz);

            if (marker_vector_w[neighbor_index] == std::numeric_limits<int>::max())
            {
                marker_vector_w[neighbor_index] = marker_vector_w[current_index] + 1;
                grid_indices_W_w.emplace_back(ii, jj, kk);
                continue;
            }

            if (marker_vector_w[neighbor_index] < marker_vector_w[current_index])
            {
                sum += grid.velocity().w(ii, jj, kk);
                ++count;
            }
        }
        if (count > 0)
            grid.velocity().w(i, j, k) = sum / static_cast<double>(count);

        ++t;
    }
}
} // namespace Fluid_Simulation_3D
