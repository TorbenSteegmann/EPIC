#include "apply_forces.hh"
#include "../../profile_timer.hh"
#include "../../world.hh"

#include <algorithm>
#include <atomic>
#include <type_traits>

namespace
{
struct Force_Config
{
    bool apply_gravity = true;
    bool apply_top_force = false;
    double top_force_min_y = 0;
    double top_force_max_y = 0;
    double top_force_max_speed = 35.0;
};

Force_Config make_force_config(Settings const& settings)
{
    double grid_height = std::clamp(settings.GRID_HEIGHT.load(std::memory_order_relaxed), 16.0, 400.0);
    double fluid_dx = std::clamp(settings.FLUID_DX.load(std::memory_order_relaxed), 0.25, 4.0);
    double top_force_min_y = settings.TOP_FORCE_MIN_Y.load(std::memory_order_relaxed);
    double top_force_max_y = settings.TOP_FORCE_MAX_Y.load(std::memory_order_relaxed);
    if (top_force_min_y < 0.0 || top_force_max_y < 0.0)
    {
        double top_band_height = 2.0 * fluid_dx;
        top_force_min_y = std::max(0.0, grid_height - top_band_height);
        top_force_max_y = grid_height;
    }

    return Force_Config{
        .apply_gravity = settings.APPLY_GRAVITY.load(std::memory_order_relaxed),
        .apply_top_force = settings.APPLY_TOP_FORCE.load(std::memory_order_relaxed),
        .top_force_min_y = top_force_min_y,
        .top_force_max_y = top_force_max_y,
        .top_force_max_speed = settings.TOP_FORCE_MAX_SPEED.load(std::memory_order_relaxed),
    };
}

template <typename Grid_T>
bool lower_cell_is_fluid(Grid_T& grid, int i, int j, int k)
{
    if constexpr (std::is_same_v<Grid_T, MAC_Grid_3D>)
        return (j > 0) && (grid.cell_type(i, j - 1, k) == FLUID);
    else
        return (j > 0) && (grid.cell_type(i, j - 1) == FLUID);
}

template <typename Grid_T>
bool upper_cell_is_fluid(Grid_T& grid, int i, int j, int k)
{
    if constexpr (std::is_same_v<Grid_T, MAC_Grid_3D>)
        return (j < grid.ny()) && (grid.cell_type(i, j, k) == FLUID);
    else
        return (j < grid.ny()) && (grid.cell_type(i, j) == FLUID);
}

template <typename Grid_T>
bool left_cell_is_fluid(Grid_T& grid, int i, int j, int k)
{
    if constexpr (std::is_same_v<Grid_T, MAC_Grid_3D>)
        return (i > 0) && (grid.cell_type(i - 1, j, k) == FLUID);
    else
        return (i > 0) && (grid.cell_type(i - 1, j) == FLUID);
}

template <typename Grid_T>
bool right_cell_is_fluid(Grid_T& grid, int i, int j, int k)
{
    if constexpr (std::is_same_v<Grid_T, MAC_Grid_3D>)
        return (i < grid.nx()) && (grid.cell_type(i, j, k) == FLUID);
    else
        return (i < grid.nx()) && (grid.cell_type(i, j) == FLUID);
}

template <typename Grid_T>
void add_vertical_velocity(Grid_T& grid, int i, int j, int k, double gdt)
{
    if constexpr (std::is_same_v<Grid_T, MAC_Grid_3D>)
        grid.velocity().v(i, j, k) += gdt;
    else
        grid.u().v(i, j) += gdt;
}

template <typename Grid_T>
void set_horizontal_velocity(Grid_T& grid, int i, int j, int k, double target_speed)
{
    if constexpr (std::is_same_v<Grid_T, MAC_Grid_3D>)
        grid.velocity().u(i, j, k) = target_speed;
    else
        grid.u().u(i, j) = target_speed;
}

template <typename Grid_T>
double horizontal_face_y(Grid_T& grid, int j)
{
    return grid.origin().y + (static_cast<double>(j) + 0.5) * grid.dx();
}

template <typename Grid_T>
void apply_gravity(Grid_T& grid, double dt)
{
    double const gdt = -9.81 * dt;
    auto apply_xy_layer = [&](int k)
    {
        for (int j = 0; j <= grid.ny(); ++j)
            for (int i = 0; i < grid.nx(); ++i)
            {
                bool bot_is_fluid = lower_cell_is_fluid(grid, i, j, k);
                bool top_is_fluid = upper_cell_is_fluid(grid, i, j, k);

                if (bot_is_fluid || top_is_fluid)
                {
                    add_vertical_velocity(grid, i, j, k, gdt);
                }
            }
    };

    if constexpr (std::is_same_v<Grid_T, MAC_Grid_3D>)
    {
        for (int k = 0; k < grid.nz(); ++k)
            apply_xy_layer(k);

        return;
    }

    apply_xy_layer(0);
}

template <typename Grid_T>
void apply_top_force(Grid_T& grid, Force_Config const& force_config)
{
    if (force_config.top_force_max_y <= force_config.top_force_min_y)
        return;

    auto apply_xy_layer = [&](int k)
    {
        for (int j = 0; j < grid.ny(); ++j)
        {
            double const face_y = horizontal_face_y(grid, j);
            if (face_y < force_config.top_force_min_y || face_y > force_config.top_force_max_y)
                continue;

            for (int i = 0; i <= grid.nx(); ++i)
            {
                bool left_is_fluid = left_cell_is_fluid(grid, i, j, k);
                bool right_is_fluid = right_cell_is_fluid(grid, i, j, k);

                if (left_is_fluid || right_is_fluid)
                    set_horizontal_velocity(grid, i, j, k, force_config.top_force_max_speed);
            }
        }
    };

    if constexpr (std::is_same_v<Grid_T, MAC_Grid_3D>)
    {
        for (int k = 0; k < grid.nz(); ++k)
            apply_xy_layer(k);

        return;
    }

    apply_xy_layer(0);
}

// Shared across 2D and 3D: a small step kept unified as a demonstration of the
// intended longer-term direction. The other kernels stay dimension-separated
// because they remain tightly coupled to dimension-specific solver code.
template <typename Grid_T>
void apply_forces_impl(Grid_T& grid, double dt, Force_Config const& force_config)
{
    if (force_config.apply_gravity)
        apply_gravity(grid, dt);

    if (force_config.apply_top_force)
        apply_top_force(grid, force_config);
}
} // namespace

namespace Fluid_Simulation
{
void apply_forces(MAC_Grid_2D& grid, double dt)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation::apply_forces_2d");

    apply_forces_impl(grid, dt, Force_Config{});
}

void apply_forces(MAC_Grid_3D& grid, double dt)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation::apply_forces_3d");

    apply_forces_impl(grid, dt, Force_Config{});
}

void apply_forces(MAC_Grid_2D& grid, double dt, Settings const& settings)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation::apply_forces_2d_settings");

    apply_forces_impl(grid, dt, make_force_config(settings));
}

void apply_forces(MAC_Grid_3D& grid, double dt, Settings const& settings)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation::apply_forces_3d_settings");

    apply_forces_impl(grid, dt, make_force_config(settings));
}
} // namespace Fluid_Simulation

namespace Fluid_Simulation_2D
{
void apply_forces_2d(MAC_Grid_2D& grid, double dt)
{
    Fluid_Simulation::apply_forces(grid, dt);
}
} // namespace Fluid_Simulation_2D

namespace Fluid_Simulation_3D
{
void apply_forces_3d(MAC_Grid_3D& grid, double dt)
{
    Fluid_Simulation::apply_forces(grid, dt);
}
} // namespace Fluid_Simulation_3D
