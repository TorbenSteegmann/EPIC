#include "grid_to_particle_3d.hh"

#include "helpers_3d.hh"
#include "polypic.hh"
#include "../../profile_timer.hh"

#include <array>
#include <cmath>

inline double trilerp(double v000, double v100, double v010, double v110,
                      double v001, double v101, double v011, double v111,
                      double fx, double fy, double fz)
{
    double v00 = v000 * (1.0 - fx) + v100 * fx;
    double v10 = v010 * (1.0 - fx) + v110 * fx;
    double v01 = v001 * (1.0 - fx) + v101 * fx;
    double v11 = v011 * (1.0 - fx) + v111 * fx;

    double v0 = v00 * (1.0 - fy) + v10 * fy;
    double v1 = v01 * (1.0 - fy) + v11 * fy;
    return v0 * (1.0 - fz) + v1 * fz;
}

inline glm::dvec3 trilerp_gradient(double v000, double v100, double v010, double v110,
                                   double v001, double v101, double v011, double v111,
                                   double fx, double fy, double fz)
{
    glm::dvec3 f000(-(1.0 - fy) * (1.0 - fz),
                    -(1.0 - fx) * (1.0 - fz),
                    -(1.0 - fx) * (1.0 - fy));
    glm::dvec3 f100((1.0 - fy) * (1.0 - fz),
                    -fx * (1.0 - fz),
                    -fx * (1.0 - fy));
    glm::dvec3 f010(-fy * (1.0 - fz),
                    (1.0 - fx) * (1.0 - fz),
                    -(1.0 - fx) * fy);
    glm::dvec3 f110(fy * (1.0 - fz),
                    fx * (1.0 - fz),
                    -fx * fy);
    glm::dvec3 f001(-(1.0 - fy) * fz,
                    -(1.0 - fx) * fz,
                    (1.0 - fx) * (1.0 - fy));
    glm::dvec3 f101((1.0 - fy) * fz,
                    -fx * fz,
                    fx * (1.0 - fy));
    glm::dvec3 f011(-fy * fz,
                    (1.0 - fx) * fz,
                    (1.0 - fx) * fy);
    glm::dvec3 f111(fy * fz,
                    fx * fz,
                    fx * fy);

    return f000 * v000 + f100 * v100 + f010 * v010 + f110 * v110 +
           f001 * v001 + f101 * v101 + f011 * v011 + f111 * v111;
}

inline glm::dvec3 canonical_moment_fit_coefficient(double v000, double v100, double v010, double v110,
                                                   double v001, double v101, double v011, double v111,
                                                   double fx, double fy, double fz)
{
    std::array<glm::dvec3, 8> x_i_minus_x_p{
        glm::dvec3(-fx, -fy, -fz),
        glm::dvec3(1.0 - fx, -fy, -fz),
        glm::dvec3(-fx, 1.0 - fy, -fz),
        glm::dvec3(1.0 - fx, 1.0 - fy, -fz),
        glm::dvec3(-fx, -fy, 1.0 - fz),
        glm::dvec3(1.0 - fx, -fy, 1.0 - fz),
        glm::dvec3(-fx, 1.0 - fy, 1.0 - fz),
        glm::dvec3(1.0 - fx, 1.0 - fy, 1.0 - fz),
    };

    std::array<double, 8> w_ip{
        (1.0 - fx) * (1.0 - fy) * (1.0 - fz),
        fx * (1.0 - fy) * (1.0 - fz),
        (1.0 - fx) * fy * (1.0 - fz),
        fx * fy * (1.0 - fz),
        (1.0 - fx) * (1.0 - fy) * fz,
        fx * (1.0 - fy) * fz,
        (1.0 - fx) * fy * fz,
        fx * fy * fz,
    };

    std::array<double, 8> v_ai{
        v000,
        v100,
        v010,
        v110,
        v001,
        v101,
        v011,
        v111,
    };

    glm::dvec3 B_pa(0.0);
    glm::dmat3 D_p(0.0);

    for (int i = 0; i < 8; ++i)
    {
        B_pa += w_ip[i] * v_ai[i] * x_i_minus_x_p[i];
        D_p += w_ip[i] * glm::outerProduct(x_i_minus_x_p[i], x_i_minus_x_p[i]);
    }

    double determinant_D_p = glm::determinant(D_p);
    if (std::abs(determinant_D_p) <= 1.0e-12)
        return glm::dvec3(0.0);

    glm::dvec3 c_pa = glm::inverse(D_p) * B_pa;
    return c_pa;
}

namespace Fluid_Simulation_3D
{
void grid_to_particle_3d(World& world, MAC_Grid_3D& grid)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_3D::grid_to_particle_3d");

    double pic_flip_blend = world.settings().FLIP_PERCENT;
    auto particles = grid.particles();
    int nx = grid.nx();
    int ny = grid.ny();
    int nz = grid.nz();
    double dx = grid.dx();

    for (auto& p : particles)
    {
        auto particle = world.get_component<Particle_Component>(p);
        if (!particle)
            continue;

        particle->c_u = glm::dvec2(0.0);
        particle->c_v = glm::dvec2(0.0);
        particle->c_u_3d = glm::dvec3(0.0);
        particle->c_v_3d = glm::dvec3(0.0);
        particle->c_w_3d = glm::dvec3(0.0);
        polypic_clear_coefficients(*particle);

        glm::dvec3 delta(0.0);
        glm::dvec3 pos = particle->position;

        // u
        {
            glm::dvec3 pos_in_grid = pos / dx;
            pos_in_grid.y -= 0.5;
            pos_in_grid.z -= 0.5;

            int i = int(std::floor(pos_in_grid.x));
            int j = int(std::floor(pos_in_grid.y));
            int k = int(std::floor(pos_in_grid.z));
            double fx = pos_in_grid.x - i;
            double fy = pos_in_grid.y - j;
            double fz = pos_in_grid.z - k;

            if (i >= 0 && i + 1 <= nx && j >= 0 && j + 1 < ny && k >= 0 && k + 1 < nz)
            {
                double v000_new = grid.velocity().u(i, j, k);
                double v100_new = grid.velocity().u(i + 1, j, k);
                double v010_new = grid.velocity().u(i, j + 1, k);
                double v110_new = grid.velocity().u(i + 1, j + 1, k);
                double v001_new = grid.velocity().u(i, j, k + 1);
                double v101_new = grid.velocity().u(i + 1, j, k + 1);
                double v011_new = grid.velocity().u(i, j + 1, k + 1);
                double v111_new = grid.velocity().u(i + 1, j + 1, k + 1);

                double v000_old = grid.velocity_old().u(i, j, k);
                double v100_old = grid.velocity_old().u(i + 1, j, k);
                double v010_old = grid.velocity_old().u(i, j + 1, k);
                double v110_old = grid.velocity_old().u(i + 1, j + 1, k);
                double v001_old = grid.velocity_old().u(i, j, k + 1);
                double v101_old = grid.velocity_old().u(i + 1, j, k + 1);
                double v011_old = grid.velocity_old().u(i, j + 1, k + 1);
                double v111_old = grid.velocity_old().u(i + 1, j + 1, k + 1);

                double u_new = trilerp(v000_new, v100_new, v010_new, v110_new,
                                       v001_new, v101_new, v011_new, v111_new,
                                       fx, fy, fz);
                double u_old = trilerp(v000_old, v100_old, v010_old, v110_old,
                                       v001_old, v101_old, v011_old, v111_old,
                                       fx, fy, fz);

                delta.x = u_new - u_old;

                if (world.settings().FLUID_SOLVER == APIC)
                    particle->c_u_3d = trilerp_gradient(v000_new, v100_new, v010_new, v110_new, v001_new, v101_new, v011_new, v111_new, fx, fy, fz);
                else if (world.settings().FLUID_SOLVER == POLYPIC)
                {
                    auto poly_c_u = polypic_g2p_solve_component_3d(v000_new, v100_new, v010_new, v110_new,
                                                                    v001_new, v101_new, v011_new, v111_new,
                                                                    fx, fy, fz, world.settings().POLYPIC_MODES);
                    for (int r = 0; r < POLYPIC_MAX_MODES_3D; ++r)
                        particle->poly_c_3d[r].x = poly_c_u[r];
                }
            }
        }

        // v
        {
            glm::dvec3 pos_in_grid = pos / dx;
            pos_in_grid.x -= 0.5;
            pos_in_grid.z -= 0.5;

            int i = int(std::floor(pos_in_grid.x));
            int j = int(std::floor(pos_in_grid.y));
            int k = int(std::floor(pos_in_grid.z));
            double fx = pos_in_grid.x - i;
            double fy = pos_in_grid.y - j;
            double fz = pos_in_grid.z - k;

            if (i >= 0 && i + 1 < nx && j >= 0 && j + 1 <= ny && k >= 0 && k + 1 < nz)
            {
                double v000_new = grid.velocity().v(i, j, k);
                double v100_new = grid.velocity().v(i + 1, j, k);
                double v010_new = grid.velocity().v(i, j + 1, k);
                double v110_new = grid.velocity().v(i + 1, j + 1, k);
                double v001_new = grid.velocity().v(i, j, k + 1);
                double v101_new = grid.velocity().v(i + 1, j, k + 1);
                double v011_new = grid.velocity().v(i, j + 1, k + 1);
                double v111_new = grid.velocity().v(i + 1, j + 1, k + 1);

                double v000_old = grid.velocity_old().v(i, j, k);
                double v100_old = grid.velocity_old().v(i + 1, j, k);
                double v010_old = grid.velocity_old().v(i, j + 1, k);
                double v110_old = grid.velocity_old().v(i + 1, j + 1, k);
                double v001_old = grid.velocity_old().v(i, j, k + 1);
                double v101_old = grid.velocity_old().v(i + 1, j, k + 1);
                double v011_old = grid.velocity_old().v(i, j + 1, k + 1);
                double v111_old = grid.velocity_old().v(i + 1, j + 1, k + 1);

                double v_new = trilerp(v000_new, v100_new, v010_new, v110_new,
                                       v001_new, v101_new, v011_new, v111_new,
                                       fx, fy, fz);
                double v_old = trilerp(v000_old, v100_old, v010_old, v110_old,
                                       v001_old, v101_old, v011_old, v111_old,
                                       fx, fy, fz);
                delta.y = v_new - v_old;

                if (world.settings().FLUID_SOLVER == APIC)
                    particle->c_v_3d = trilerp_gradient(v000_new, v100_new, v010_new, v110_new, v001_new, v101_new, v011_new, v111_new, fx, fy, fz);
                else if (world.settings().FLUID_SOLVER == POLYPIC)
                {
                    auto poly_c_v = polypic_g2p_solve_component_3d(v000_new, v100_new, v010_new, v110_new,
                                                                    v001_new, v101_new, v011_new, v111_new,
                                                                    fx, fy, fz, world.settings().POLYPIC_MODES);
                    for (int r = 0; r < POLYPIC_MAX_MODES_3D; ++r)
                        particle->poly_c_3d[r].y = poly_c_v[r];
                }
            }
        }

        // w
        {
            glm::dvec3 pos_in_grid = pos / dx;
            pos_in_grid.x -= 0.5;
            pos_in_grid.y -= 0.5;

            int i = int(std::floor(pos_in_grid.x));
            int j = int(std::floor(pos_in_grid.y));
            int k = int(std::floor(pos_in_grid.z));
            double fx = pos_in_grid.x - i;
            double fy = pos_in_grid.y - j;
            double fz = pos_in_grid.z - k;

            if (i >= 0 && i + 1 < nx && j >= 0 && j + 1 < ny && k >= 0 && k + 1 <= nz)
            {
                double v000_new = grid.velocity().w(i, j, k);
                double v100_new = grid.velocity().w(i + 1, j, k);
                double v010_new = grid.velocity().w(i, j + 1, k);
                double v110_new = grid.velocity().w(i + 1, j + 1, k);
                double v001_new = grid.velocity().w(i, j, k + 1);
                double v101_new = grid.velocity().w(i + 1, j, k + 1);
                double v011_new = grid.velocity().w(i, j + 1, k + 1);
                double v111_new = grid.velocity().w(i + 1, j + 1, k + 1);

                double v000_old = grid.velocity_old().w(i, j, k);
                double v100_old = grid.velocity_old().w(i + 1, j, k);
                double v010_old = grid.velocity_old().w(i, j + 1, k);
                double v110_old = grid.velocity_old().w(i + 1, j + 1, k);
                double v001_old = grid.velocity_old().w(i, j, k + 1);
                double v101_old = grid.velocity_old().w(i + 1, j, k + 1);
                double v011_old = grid.velocity_old().w(i, j + 1, k + 1);
                double v111_old = grid.velocity_old().w(i + 1, j + 1, k + 1);

                double w_new = trilerp(v000_new, v100_new, v010_new, v110_new,
                                       v001_new, v101_new, v011_new, v111_new,
                                       fx, fy, fz);
                double w_old = trilerp(v000_old, v100_old, v010_old, v110_old,
                                       v001_old, v101_old, v011_old, v111_old,
                                       fx, fy, fz);
                delta.z = w_new - w_old;

                if (world.settings().FLUID_SOLVER == APIC)
                    particle->c_w_3d = trilerp_gradient(v000_new, v100_new, v010_new, v110_new, v001_new, v101_new, v011_new, v111_new, fx, fy, fz);
                else if (world.settings().FLUID_SOLVER == POLYPIC)
                {
                    auto poly_c_w = polypic_g2p_solve_component_3d(v000_new, v100_new, v010_new, v110_new,
                                                                    v001_new, v101_new, v011_new, v111_new,
                                                                    fx, fy, fz, world.settings().POLYPIC_MODES);
                    for (int r = 0; r < POLYPIC_MAX_MODES_3D; ++r)
                        particle->poly_c_3d[r].z = poly_c_w[r];
                }
            }
        }

        glm::dvec3 v_pic = interpolate_grid_velocity(grid, pos);

        if (world.settings().FLUID_SOLVER == APIC || world.settings().FLUID_SOLVER == POLYPIC)
        {
            particle->velocity = v_pic;
            continue;
        }

        glm::dvec3 v_flip = particle->velocity + delta;
        particle->velocity = (1.0 - pic_flip_blend) * v_pic + pic_flip_blend * v_flip;
    }
}
} // namespace Fluid_Simulation_3D
