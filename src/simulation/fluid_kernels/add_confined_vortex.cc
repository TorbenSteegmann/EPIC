#include "add_confined_vortex.hh"

#include "add_water_box.hh"

#include <cmath>

namespace Fluid_Simulation_2D
{
namespace
{
constexpr std::uint32_t CONFINED_VORTEX_JITTER_SEED = 0x434f4e56u; // "CONV"
// Vortex support radius as a fraction of the interior half-extent. Keeps the
// swirl well clear of the square walls so wall torque stays negligible and the
// outer fluid is quiescent.
constexpr double VORTEX_RADIUS_FRACTION = 0.7;
} // namespace

double confined_vortex_support_radius(MAC_Grid_2D& grid)
{
    double const dx = grid.dx();
    double const extent_x = static_cast<double>(grid.nx() - 2) * dx;
    double const extent_y = static_cast<double>(grid.ny() - 2) * dx;
    return VORTEX_RADIUS_FRACTION * 0.5 * std::min(extent_x, extent_y);
}

void add_confined_vortex(World& world, MAC_Grid_2D& grid, double amplitude)
{
    world.settings().APPLY_GRAVITY.store(false, std::memory_order_relaxed);
    world.settings().APPLY_TOP_FORCE.store(false, std::memory_order_relaxed);

    double const dx = grid.dx();
    glm::dvec2 const center(0.5 * static_cast<double>(grid.nx()) * dx, 0.5 * static_cast<double>(grid.ny()) * dx);
    double const radius = confined_vortex_support_radius(grid);

    Fluid_Simulation_2D::add_water_box(
        world,
        grid,
        glm::dvec2(0.0),
        glm::dvec2(static_cast<double>(grid.nx()) * dx, static_cast<double>(grid.ny()) * dx),
        true,
        CONFINED_VORTEX_JITTER_SEED);

    // psi(r) = A (1 - (r/R)^2)^2; peak azimuthal speed is 8A / (3 sqrt(3) R).
    double const r2 = radius * radius;
    double const A = amplitude * 3.0 * std::sqrt(3.0) * radius / 8.0;
    double const k1 = 4.0 * A / r2;       // scale of the velocity itself
    double const k2 = 2.0 * k1 / r2;      // scale of the first derivatives

    for (ecs::Entity const entity : grid.particles())
    {
        auto* particle = world.get_component<Particle_Component>(entity);
        if (!particle)
            continue;

        double const rx = particle->position.x - center.x;
        double const ry = particle->position.y - center.y;

        if (rx * rx + ry * ry >= r2)
        {
            particle->velocity = glm::dvec3(0.0);
            particle->c_u = glm::dvec2(0.0);
            particle->c_v = glm::dvec2(0.0);
            particle->poly_c[0] = glm::dvec2(0.0);
            particle->poly_c[1] = glm::dvec2(0.0);
            particle->poly_c[2] = glm::dvec2(0.0);
            particle->poly_c[3] = glm::dvec2(0.0);
            continue;
        }

        double const q = 1.0 - (rx * rx + ry * ry) / r2;

        double const u = -k1 * ry * q;
        double const v = k1 * rx * q;
        particle->velocity = glm::dvec3(u, v, 0.0);

        double const du_dx = k2 * rx * ry;
        double const du_dy = -k1 * (q - 2.0 * ry * ry / r2);
        double const dv_dx = k1 * (q - 2.0 * rx * rx / r2);
        double const dv_dy = -k2 * rx * ry;

        particle->c_u = dx * glm::dvec2(du_dx, du_dy);
        particle->c_v = dx * glm::dvec2(dv_dx, dv_dy);

        double const d2u_dxdy = k2 * rx;
        double const d2v_dxdy = -k2 * ry;
        particle->poly_c[0] = glm::dvec2(u, v);
        particle->poly_c[1] = dx * glm::dvec2(du_dx, dv_dx);
        particle->poly_c[2] = dx * glm::dvec2(du_dy, dv_dy);
        particle->poly_c[3] = dx * dx * glm::dvec2(d2u_dxdy, d2v_dxdy);
    }
}
} // namespace Fluid_Simulation_2D
