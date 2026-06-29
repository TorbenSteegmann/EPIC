#pragma once

#include <glm/glm.hpp>

#include <array>

struct Particle_Component
{
    glm::dvec3 position{0.0};
    float radius = 1.0;      // world units
    glm::dvec3 velocity{0.0, 0.0, 0.0}; // world units / s

    glm::dvec2 c_u{0.0};
    glm::dvec2 c_v{0.0};

    glm::dvec3 c_u_3d{0.0};
    glm::dvec3 c_v_3d{0.0};
    glm::dvec3 c_w_3d{0.0};

    // PolyPIC 2D multilinear mode coefficients, poly_c[r] = (u, v) for modes {1, x, y, xy}.
    // Storage only; all PolyPIC logic lives in simulation/fluid_kernels/polypic.hh/.cc.
    // Dedicated storage (not c_u/c_v) so the Nr=3 APIC-equivalence check stays a real test.
    std::array<glm::dvec2, 4> poly_c{};

    // MPM PolyPIC 2D quadratic-complete mode coefficients. The first four modes
    // mirror poly_c ({1, x, y, xy}); modes 4..8 are the additional quadratic modes.
    std::array<glm::dvec2, 9> mpm_poly_c{};

    // PolyPIC 3D trilinear mode coefficients, poly_c_3d[r] = (u, v, w) for
    // modes {1, x, y, z, xy, xz, yz, xyz}. Dedicated storage keeps the
    // Nr=4 APIC-equivalence check separate from c_u_3d/c_v_3d/c_w_3d.
    std::array<glm::dvec3, 8> poly_c_3d{};
};

struct Mass_Component
{
    double mass = 1.0;
};

struct Deformable_Component
{
    glm::dmat2 F_E = glm::dmat2(1.0); // Elastic part
    glm::dmat2 F_P = glm::dmat2(1.0); // Plastic part
    glm::dmat2 F = glm::dmat2(1.0);   // deformation gradient

    double V_0 = 0.0; // rest volume

    // double E = 4e4;
    // double nu = 0.49; // 0.5 is perfectly incompressible; 0.49 is a stable limit for explicit
    // double mu = E / (2.0 * (1.0 + nu));
    // double lambda = (E * nu) / ((1.0 + nu) * (1.0 - 2.0 * nu));

    double mu = 299.422;
    double lambda = 3000.718;

    bool is_new = true;
};
