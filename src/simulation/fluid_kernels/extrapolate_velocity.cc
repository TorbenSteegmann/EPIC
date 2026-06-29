#include "extrapolate_velocity.hh"
#include "../../profile_timer.hh"

#include <array>

void init_marker_vectors(MAC_Grid_2D& grid, std::vector<int>& marker_vector_u, std::vector<int>& marker_vector_v)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_2D::init_marker_vectors");

    int nx = grid.nx();
    int ny = grid.ny();

    for (int j = 0; j < ny; ++j)
    {
        for (int i = 0; i < nx + 1; ++i)
        {
            bool left_is_fluid = i > 0 && grid.cell_type(i - 1, j) == FLUID;
            bool right_is_fluid = i < nx && grid.cell_type(i, j) == FLUID;

            if (left_is_fluid || right_is_fluid)
                marker_vector_u[grid_index::u(i, j, nx, ny)] = 0;
        }
    }

    for (int j = 0; j < ny + 1; ++j)
    {
        for (int i = 0; i < nx; ++i)
        {
            bool bot_is_fluid = j > 0 && grid.cell_type(i, j - 1) == FLUID;
            bool top_is_fluid = j < ny && grid.cell_type(i, j) == FLUID;
            if (bot_is_fluid || top_is_fluid)
                marker_vector_v[grid_index::v(i, j, nx, ny)] = 0;
        }
    }
}

void Fluid_Simulation_2D::extrapolate_velocity(MAC_Grid_2D& grid, double dt)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_2D::extrapolate_velocity");

    std::vector<int> marker_vector_u((grid.nx() + 1) * grid.ny(), std::numeric_limits<int>::max());
    std::vector<int> marker_vector_v(grid.nx() * (grid.ny() + 1), std::numeric_limits<int>::max());

    init_marker_vectors(grid, marker_vector_u, marker_vector_v);

    std::vector<std::pair<int, int>> grid_indices_W_u;
    std::vector<std::pair<int, int>> grid_indices_W_v;

    std::array<int, 4> di{-1, 0, 1, 0};
    std::array<int, 4> dj{0, 1, 0, -1};

    int nx = grid.nx();
    int ny = grid.ny();

    for (int j = 0; j < ny; ++j)
    {
        for (int i = 0; i <= nx; ++i)
        {
            if (marker_vector_u[grid_index::u(i, j, nx, ny)] == 0)
                continue;

            for (int k = 0; k < 4; ++k)
            {
                int ii = i + di[k];
                int jj = j + dj[k];

                if (ii < 0 || ii > nx || jj < 0 || jj >= ny)
                    continue;

                if (marker_vector_u[grid_index::u(ii, jj, nx, ny)] == 0)
                {
                    marker_vector_u[grid_index::u(i, j, nx, ny)] = 1;
                    grid_indices_W_u.emplace_back(i, j);
                    break;
                }
            }
        }
    }

    for (int j = 0; j <= ny; ++j)
    {
        for (int i = 0; i < nx; ++i)
        {
            if (marker_vector_v[grid_index::v(i, j, nx, ny)] == 0)
                continue;

            for (int k = 0; k < 4; ++k)
            {
                int ii = i + di[k];
                int jj = j + dj[k];

                if (ii < 0 || ii >= nx || jj < 0 || jj > ny)
                    continue;

                if (marker_vector_v[grid_index::v(ii, jj, nx, ny)] == 0)
                {
                    marker_vector_v[grid_index::v(i, j, nx, ny)] = 1;
                    grid_indices_W_v.emplace_back(i, j);
                    break;
                }
            }
        }
    }


    int t = 0;
    int const maximum_marker = 3;
    while (t < grid_indices_W_u.size())
    {
        auto [i, j] = grid_indices_W_u[t];
        int current_index = grid_index::u(i, j, nx, ny);

        if (marker_vector_u[current_index] == maximum_marker)
            break;

        double sum = 0.0;
        int count = 0;
        for (int k = 0; k < 4; ++k)
        {
            int ii = i + di[k];
            int jj = j + dj[k];

            if (ii < 0 || jj < 0 || ii > nx || jj >= ny)
                continue;

            int neighbor_index = grid_index::u(ii, jj, nx, ny);

            if (marker_vector_u[neighbor_index] == std::numeric_limits<int>::max())
            {
                marker_vector_u[neighbor_index] = marker_vector_u[current_index] + 1;
                grid_indices_W_u.emplace_back(ii, jj);
                continue;
            }

            if (marker_vector_u[neighbor_index] < marker_vector_u[current_index])
            {
                sum += grid.u().u(ii, jj);
                ++count;
            }
        }
        if (count > 0)
            grid.u().u(i, j) = sum / static_cast<double>(count);

        ++t;
    }

    t = 0;
    while (t < grid_indices_W_v.size())
    {
        auto [i, j] = grid_indices_W_v[t];
        int current_index = grid_index::v(i, j, nx, ny);

        if (marker_vector_v[current_index] == maximum_marker)
            break;

        double sum = 0.0;
        int count = 0;
        for (int k = 0; k < 4; ++k)
        {
            int ii = i + di[k];
            int jj = j + dj[k];

            if (ii < 0 || jj < 0 || ii >= nx || jj > ny)
                continue;

            int neighbor_index = grid_index::v(ii, jj, nx, ny);

            if (marker_vector_v[neighbor_index] == std::numeric_limits<int>::max())
            {
                marker_vector_v[neighbor_index] = marker_vector_v[current_index] + 1;
                grid_indices_W_v.emplace_back(ii, jj);
                continue;
            }

            if (marker_vector_v[neighbor_index] < marker_vector_v[current_index])
            {
                sum += grid.u().v(ii, jj);
                ++count;
            }
        }
        if (count > 0)
            grid.u().v(i, j) = sum / static_cast<double>(count);

        ++t;
    }
}
