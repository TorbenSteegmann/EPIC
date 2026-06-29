#pragma once

#include "../../ecs/particle_component.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <tuple>

#include <glm/glm.hpp>

inline constexpr int POLYPIC_MAX_MODES_2D = 4;
inline constexpr int POLYPIC_MAX_MODES_3D = 8;
inline constexpr int MPM_POLYPIC_MAX_MODES_2D = 9;

// Compatibility alias for existing 2D callers.
inline constexpr int POLYPIC_MAX_MODES = POLYPIC_MAX_MODES_2D;

static_assert(std::tuple_size_v<decltype(Particle_Component::poly_c)> == POLYPIC_MAX_MODES_2D,
              "Particle_Component::poly_c length must match POLYPIC_MAX_MODES_2D");
static_assert(std::tuple_size_v<decltype(Particle_Component::mpm_poly_c)> == MPM_POLYPIC_MAX_MODES_2D,
              "Particle_Component::mpm_poly_c length must match MPM_POLYPIC_MAX_MODES_2D");
static_assert(std::tuple_size_v<decltype(Particle_Component::poly_c_3d)> == POLYPIC_MAX_MODES_3D,
              "Particle_Component::poly_c_3d length must match POLYPIC_MAX_MODES_3D");

inline int polypic_max_modes_for_dimension(int dimension)
{
    return dimension == 3 ? POLYPIC_MAX_MODES_3D : POLYPIC_MAX_MODES_2D;
}

inline int mpm_polypic_max_modes_for_dimension(int dimension)
{
    return dimension == 2 ? MPM_POLYPIC_MAX_MODES_2D : 0;
}

struct Poly_Mode_2D
{
    int ex;
    int ey;
};

inline constexpr std::array<Poly_Mode_2D, POLYPIC_MAX_MODES_2D> POLYPIC_MODES_2D{{
    {0, 0}, // 1
    {1, 0}, // x
    {0, 1}, // y
    {1, 1}, // xy
}};

inline constexpr std::array<Poly_Mode_2D, MPM_POLYPIC_MAX_MODES_2D> MPM_POLYPIC_MODES_2D{{
    {0, 0}, // 1
    {1, 0}, // x
    {0, 1}, // y
    {1, 1}, // xy
    {2, 0}, // g_x(x)
    {0, 2}, // g_y(y)
    {2, 1}, // g_x(x)y
    {1, 2}, // x g_y(y)
    {2, 2}, // g_x(x)g_y(y)
}};

struct Poly_Mode_3D
{
    int ex;
    int ey;
    int ez;
};

inline constexpr std::array<Poly_Mode_3D, POLYPIC_MAX_MODES_3D> POLYPIC_MODES_3D{{
    {0, 0, 0}, // 1
    {1, 0, 0}, // x
    {0, 1, 0}, // y
    {0, 0, 1}, // z
    {1, 1, 0}, // xy
    {1, 0, 1}, // xz
    {0, 1, 1}, // yz
    {1, 1, 1}, // xyz
}};

inline double polypic_mode_value(int r, glm::dvec2 z)
{
    Poly_Mode_2D const& e = POLYPIC_MODES_2D[r];
    double s = 1.0;
    for (int k = 0; k < e.ex; ++k)
        s *= z.x;
    for (int k = 0; k < e.ey; ++k)
        s *= z.y;
    return s;
}

inline double mpm_polypic_axis_mode_value(int exponent, double z, double particle_offset)
{
    if (exponent == 0)
        return 1.0;
    if (exponent == 1)
        return z;
    if (exponent == 2)
        return z * z - particle_offset * (1.0 - 4.0 * particle_offset * particle_offset) * z - 0.25;
    return 0.0;
}

inline double mpm_polypic_mode_value(int r, glm::dvec2 z, glm::dvec2 particle_offset)
{
    Poly_Mode_2D const& e = MPM_POLYPIC_MODES_2D[r];
    return mpm_polypic_axis_mode_value(e.ex, z.x, particle_offset.x) *
           mpm_polypic_axis_mode_value(e.ey, z.y, particle_offset.y);
}

inline double polypic_mode_value_3d(int r, glm::dvec3 z)
{
    Poly_Mode_3D const& e = POLYPIC_MODES_3D[r];
    double s = 1.0;
    for (int k = 0; k < e.ex; ++k)
        s *= z.x;
    for (int k = 0; k < e.ey; ++k)
        s *= z.y;
    for (int k = 0; k < e.ez; ++k)
        s *= z.z;
    return s;
}

inline bool polypic_coefficients_finite(Particle_Component const& particle)
{
    for (glm::dvec2 const& c : particle.poly_c)
        if (!std::isfinite(c.x) || !std::isfinite(c.y))
            return false;
    for (glm::dvec2 const& c : particle.mpm_poly_c)
        if (!std::isfinite(c.x) || !std::isfinite(c.y))
            return false;
    for (glm::dvec3 const& c : particle.poly_c_3d)
        if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z))
            return false;
    return true;
}

inline void polypic_clear_coefficients(Particle_Component& particle)
{
    particle.poly_c.fill(glm::dvec2(0.0));
    particle.mpm_poly_c.fill(glm::dvec2(0.0));
    particle.poly_c_3d.fill(glm::dvec3(0.0));
}

inline double polypic_max_abs_coefficient(Particle_Component const& particle)
{
    double m = 0.0;
    for (glm::dvec2 const& c : particle.poly_c)
        m = std::max(m, std::max(std::abs(c.x), std::abs(c.y)));
    for (glm::dvec2 const& c : particle.mpm_poly_c)
        m = std::max(m, std::max(std::abs(c.x), std::abs(c.y)));
    for (glm::dvec3 const& c : particle.poly_c_3d)
        m = std::max(m, std::max(std::max(std::abs(c.x), std::abs(c.y)), std::abs(c.z)));
    return m;
}

double polypic_p2g_face_value(Particle_Component const& particle, int component, glm::dvec2 z, int nr);

double polypic_p2g_face_value_3d(Particle_Component const& particle, int component, glm::dvec3 z, int nr);

std::array<double, POLYPIC_MAX_MODES_2D> polypic_g2p_solve_component(double v00, double v10, double v01, double v11,
                                                                     double fx, double fy, int nr);

std::array<double, POLYPIC_MAX_MODES_3D> polypic_g2p_solve_component_3d(double v000, double v100, double v010, double v110,
                                                                        double v001, double v101, double v011, double v111,
                                                                        double fx, double fy, double fz, int nr);

glm::dvec2 mpm_polypic_p2g_node_value(Particle_Component const& particle, glm::dvec2 z,
                                      glm::dvec2 particle_offset, int nr);

inline constexpr double MPM_POLYPIC_DEFAULT_QUAD_REG = 0.02;

std::array<double, MPM_POLYPIC_MAX_MODES_2D> mpm_polypic_g2p_solve_component(std::span<double const> values,
                                                                             std::span<double const> weights,
                                                                             std::span<glm::dvec2 const> offsets,
                                                                             glm::dvec2 particle_offset, int nr,
                                                                             double quad_reg = MPM_POLYPIC_DEFAULT_QUAD_REG);
std::array<glm::dvec2, MPM_POLYPIC_MAX_MODES_2D> mpm_polypic_g2p_solve_vector(std::span<glm::dvec2 const> values,
                                                                              std::span<double const> weights,
                                                                              std::span<glm::dvec2 const> offsets,
                                                                              glm::dvec2 particle_offset, int nr,
                                                                              double quad_reg = MPM_POLYPIC_DEFAULT_QUAD_REG);
