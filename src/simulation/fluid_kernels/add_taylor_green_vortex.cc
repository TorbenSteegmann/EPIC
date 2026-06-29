#include "add_taylor_green_vortex.hh"

#include "add_water_box.hh"

#include <atomic>
#include <cmath>
#include <numbers>

namespace Fluid_Simulation_2D
{
namespace
{
constexpr std::uint32_t TAYLOR_GREEN_JITTER_SEED = 0x544756u;
}

void add_taylor_green_vortex(World& world, MAC_Grid_2D& grid, double amplitude)
{
    world.settings().APPLY_GRAVITY.store(false, std::memory_order_relaxed);
    world.settings().APPLY_TOP_FORCE.store(false, std::memory_order_relaxed);

    double const dx = grid.dx();
    double const fluid_min = dx;
    double const extent = static_cast<double>(grid.nx() - 2) * dx;
    double const wave_number = 2.0 * std::numbers::pi / extent;
    double const phase_offset = std::numbers::pi / 2.0;

    Fluid_Simulation_2D::add_water_box(
        world,
        grid,
        glm::dvec2(0.0),
        glm::dvec2(static_cast<double>(grid.nx()) * dx, static_cast<double>(grid.ny()) * dx),
        true,
        TAYLOR_GREEN_JITTER_SEED);

    for (ecs::Entity const entity : grid.particles())
    {
        auto* particle = world.get_component<Particle_Component>(entity);
        if (!particle)
            continue;

        double const x = wave_number * (particle->position.x - fluid_min) + phase_offset;
        double const y = wave_number * (particle->position.y - fluid_min) + phase_offset;
        double const sin_x = std::sin(x);
        double const cos_x = std::cos(x);
        double const sin_y = std::sin(y);
        double const cos_y = std::cos(y);

        double const u = amplitude * cos_x * sin_y;
        double const v = -amplitude * sin_x * cos_y;
        particle->velocity = glm::dvec3(u, v, 0.0);

        double const du_dx = -amplitude * wave_number * sin_x * sin_y;
        double const du_dy = amplitude * wave_number * cos_x * cos_y;
        double const dv_dx = -amplitude * wave_number * cos_x * cos_y;
        double const dv_dy = amplitude * wave_number * sin_x * sin_y;

        particle->c_u = dx * glm::dvec2(du_dx, du_dy);
        particle->c_v = dx * glm::dvec2(dv_dx, dv_dy);

        double const d2u_dxdy = -amplitude * wave_number * wave_number * sin_x * cos_y;
        double const d2v_dxdy = amplitude * wave_number * wave_number * cos_x * sin_y;
        particle->poly_c[0] = glm::dvec2(u, v);
        particle->poly_c[1] = dx * glm::dvec2(du_dx, dv_dx);
        particle->poly_c[2] = dx * glm::dvec2(du_dy, dv_dy);
        particle->poly_c[3] = dx * dx * glm::dvec2(d2u_dxdy, d2v_dxdy);
    }
}
} // namespace Fluid_Simulation_2D
