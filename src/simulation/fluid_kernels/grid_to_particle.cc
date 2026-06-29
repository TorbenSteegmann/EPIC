#include "grid_to_particle.hh"
#include "polypic.hh"
#include "../../profile_timer.hh"

#include "../core/helpers.hh"

#include <cmath>

// Bilinear interpolation
inline double bilerp(double v00, double v10, double v01, double v11, double fx, double fy)
{
    double v0 = v00 * (1.0 - fx) + v10 * fx;
    double v1 = v01 * (1.0 - fx) + v11 * fx;
    return v0 * (1.0 - fy) + v1 * fy;
}

inline glm::dvec2 bilerp_gradient(double v00, double v10, double v01, double v11, double fx, double fy)
{
    glm::dvec2 f00(fy - 1.0, fx - 1.0);
    glm::dvec2 f10(1.0 - fy, -fx);
    glm::dvec2 f01(-fy, 1.0 - fx);
    glm::dvec2 f11(fy, fx);
    return f00 * v00 + f10 * v10 + f01 * v01 + f11 * v11;
}

inline glm::dvec2 canonical_moment_fit_coefficient(double v00, double v10, double v01, double v11, double fx, double fy)
{
    glm::dvec2 x00_minus_x_p(-fx, -fy);
    glm::dvec2 x10_minus_x_p(1.0 - fx, -fy);
    glm::dvec2 x01_minus_x_p(-fx, 1.0 - fy);
    glm::dvec2 x11_minus_x_p(1.0 - fx, 1.0 - fy);

    double w00_ip = (1.0 - fx) * (1.0 - fy);
    double w10_ip = fx * (1.0 - fy);
    double w01_ip = (1.0 - fx) * fy;
    double w11_ip = fx * fy;

    glm::dvec2 B_pa = w00_ip * v00 * x00_minus_x_p + w10_ip * v10 * x10_minus_x_p + w01_ip * v01 * x01_minus_x_p +
                      w11_ip * v11 * x11_minus_x_p;

    double D_p_xx = w00_ip * x00_minus_x_p.x * x00_minus_x_p.x + w10_ip * x10_minus_x_p.x * x10_minus_x_p.x +
                    w01_ip * x01_minus_x_p.x * x01_minus_x_p.x + w11_ip * x11_minus_x_p.x * x11_minus_x_p.x;
    double D_p_xy = w00_ip * x00_minus_x_p.x * x00_minus_x_p.y + w10_ip * x10_minus_x_p.x * x10_minus_x_p.y +
                    w01_ip * x01_minus_x_p.x * x01_minus_x_p.y + w11_ip * x11_minus_x_p.x * x11_minus_x_p.y;
    double D_p_yy = w00_ip * x00_minus_x_p.y * x00_minus_x_p.y + w10_ip * x10_minus_x_p.y * x10_minus_x_p.y +
                    w01_ip * x01_minus_x_p.y * x01_minus_x_p.y + w11_ip * x11_minus_x_p.y * x11_minus_x_p.y;

    double determinant_D_p = D_p_xx * D_p_yy - D_p_xy * D_p_xy;
    if (std::abs(determinant_D_p) <= 1.0e-12)
        return glm::dvec2(0.0);

    glm::dvec2 c_pa((B_pa.x * D_p_yy - B_pa.y * D_p_xy) / determinant_D_p,
                    (B_pa.y * D_p_xx - B_pa.x * D_p_xy) / determinant_D_p);
    return c_pa;
}

void Fluid_Simulation_2D::grid_to_particle(World& world, MAC_Grid_2D& grid)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_2D::grid_to_particle");

    double pic_flip_blend = world.settings().FLIP_PERCENT;

    auto particles = grid.particles();
    int nx = grid.nx();
    int ny = grid.ny();
    double dx = grid.dx();

    for (auto& p : particles)
    {
        auto particle = world.get_component<Particle_Component>(p);
        particle->c_u = glm::dvec2(0.0);
        particle->c_v = glm::dvec2(0.0);
        polypic_clear_coefficients(*particle); // mirrors the c_u/c_v reset above;

        glm::dvec2 pos = glm::dvec2(particle->position.x, particle->position.y);
        glm::dvec2 delta(0.0);

        // ---------- u component ----------
        {
            glm::dvec2 pos_in_grid = pos / dx;
            pos_in_grid.y -= 0.5;

            int i = int(std::floor(pos_in_grid.x));
            int j = int(std::floor(pos_in_grid.y));
            double fx = pos_in_grid.x - i;
            double fy = pos_in_grid.y - j;

            if (i >= 0 && i + 1 <= nx && j >= 0 && j + 1 < ny)
            {
                double v00_new = grid.u().u(i, j);
                double v10_new = grid.u().u(i + 1, j);
                double v01_new = grid.u().u(i, j + 1);
                double v11_new = grid.u().u(i + 1, j + 1);

                double v00_old = grid.u_old().u(i, j);
                double v10_old = grid.u_old().u(i + 1, j);
                double v01_old = grid.u_old().u(i, j + 1);
                double v11_old = grid.u_old().u(i + 1, j + 1);

                double u_new = bilerp(v00_new, v10_new, v01_new, v11_new, fx, fy);
                double u_old = bilerp(v00_old, v10_old, v01_old, v11_old, fx, fy);


                delta.x = u_new - u_old;

                if (world.settings().FLUID_SOLVER == APIC)
                    particle->c_u = bilerp_gradient(v00_new, v10_new, v01_new, v11_new, fx, fy);
                else if (world.settings().FLUID_SOLVER == POLYPIC)
                {
                    auto poly_c_u = polypic_g2p_solve_component(v00_new, v10_new, v01_new, v11_new, fx, fy, world.settings().POLYPIC_MODES);
                    for (int r = 0; r < POLYPIC_MAX_MODES; ++r)
                        particle->poly_c[r].x = poly_c_u[r];
                }
            }
        }

        // ---------- v component ----------
        {
            glm::dvec2 pos_in_grid = pos / dx;
            pos_in_grid.x -= 0.5;

            int i = int(std::floor(pos_in_grid.x));
            int j = int(std::floor(pos_in_grid.y));
            double fx = pos_in_grid.x - i;
            double fy = pos_in_grid.y - j;

            if (i >= 0 && i + 1 < nx && j >= 0 && j + 1 <= ny)
            {
                double v00_new = grid.u().v(i, j);
                double v10_new = grid.u().v(i + 1, j);
                double v01_new = grid.u().v(i, j + 1);
                double v11_new = grid.u().v(i + 1, j + 1);

                double v00_old = grid.u_old().v(i, j);
                double v10_old = grid.u_old().v(i + 1, j);
                double v01_old = grid.u_old().v(i, j + 1);
                double v11_old = grid.u_old().v(i + 1, j + 1);

                double v_new = bilerp(v00_new, v10_new, v01_new, v11_new, fx, fy);
                double v_old = bilerp(v00_old, v10_old, v01_old, v11_old, fx, fy);
                delta.y = v_new - v_old;

                if (world.settings().FLUID_SOLVER == APIC)
                    particle->c_v = bilerp_gradient(v00_new, v10_new, v01_new, v11_new, fx, fy);
                else if (world.settings().FLUID_SOLVER == POLYPIC)
                {
                    auto poly_c_v = polypic_g2p_solve_component(v00_new, v10_new, v01_new, v11_new, fx, fy, world.settings().POLYPIC_MODES);
                    for (int r = 0; r < POLYPIC_MAX_MODES; ++r)
                        particle->poly_c[r].y = poly_c_v[r];
                }
            }
        }

        glm::dvec2 v_flip = glm::dvec2(particle->velocity.x, particle->velocity.y) + delta;
        glm::dvec2 v_pic = Fluid_Simulation_2D::interpolate_grid_velocity(grid, pos);

        if (world.settings().FLUID_SOLVER == APIC || world.settings().FLUID_SOLVER == POLYPIC)
            particle->velocity = glm::dvec3(v_pic, 0.0);
        else
            particle->velocity = glm::dvec3((1.0 - pic_flip_blend) * v_pic + pic_flip_blend * v_flip, 0.0);
    }
}
