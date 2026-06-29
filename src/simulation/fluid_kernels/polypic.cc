#include "polypic.hh"

// PolyPIC transfer helpers. PolyPIC was added later as an extension and kept in
// its own file so it does not further enlarge the shared transfer kernels. Those
// kernels own the stencils, weights, grid layout and boundary/valid-cell
// handling; these helpers own only the PolyPIC mode math.

double polypic_p2g_face_value(Particle_Component const& particle, int component, glm::dvec2 z, int nr)
{
    nr = std::clamp(nr, 1, POLYPIC_MAX_MODES_2D);
    double value = particle.velocity[component]; // constant mode (always present)
    for (int r = 1; r < nr; ++r)
        value += polypic_mode_value(r, z) * particle.poly_c[r][component];
    return value;
}

double polypic_p2g_face_value_3d(Particle_Component const& particle, int component, glm::dvec3 z, int nr)
{
    nr = std::clamp(nr, 1, POLYPIC_MAX_MODES_3D);
    double value = particle.velocity[component]; // constant mode (always present)
    for (int r = 1; r < nr; ++r)
        value += polypic_mode_value_3d(r, z) * particle.poly_c_3d[r][component];
    return value;
}

glm::dvec2 mpm_polypic_p2g_node_value(Particle_Component const& particle, glm::dvec2 z, glm::dvec2 particle_offset, int nr)
{
    nr = std::clamp(nr, 1, MPM_POLYPIC_MAX_MODES_2D);
    glm::dvec2 value(particle.velocity.x, particle.velocity.y); // constant mode (always present)
    for (int r = 1; r < nr; ++r)
        value += mpm_polypic_mode_value(r, z, particle_offset) * particle.mpm_poly_c[r];
    return value;
}

std::array<double, POLYPIC_MAX_MODES_2D> polypic_g2p_solve_component(double v00, double v10, double v01, double v11, double fx, double fy, int nr)
{
    nr = std::clamp(nr, 1, POLYPIC_MAX_MODES_2D);
    std::array<double, POLYPIC_MAX_MODES_2D> coefficients{};

    glm::dvec2 const z[4] = {
        glm::dvec2(-fx, -fy),
        glm::dvec2(1.0 - fx, -fy),
        glm::dvec2(-fx, 1.0 - fy),
        glm::dvec2(1.0 - fx, 1.0 - fy),
    };
    double const w[4] = {
        (1.0 - fx) * (1.0 - fy),
        fx * (1.0 - fy),
        (1.0 - fx) * fy,
        fx * fy,
    };
    double const v[4] = {v00, v10, v01, v11};

    for (int r = 0; r < POLYPIC_MAX_MODES_2D; ++r)
    {
        if (r >= nr) // inactive mode
        {
            coefficients[r] = 0.0;
            continue;
        }
        double numer = 0.0;
        double denom = 0.0;
        for (int i = 0; i < 4; ++i)
        {
            double s = polypic_mode_value(r, z[i]);
            numer += w[i] * s * v[i];
            denom += w[i] * s * s;
        }
        coefficients[r] = denom > 1.0e-12 ? numer / denom : 0.0;
    }

    return coefficients;
}

std::array<double, POLYPIC_MAX_MODES_3D> polypic_g2p_solve_component_3d(double v000, double v100, double v010, double v110,
                                                                        double v001, double v101, double v011, double v111,
                                                                        double fx, double fy, double fz, int nr)
{
    nr = std::clamp(nr, 1, POLYPIC_MAX_MODES_3D);
    std::array<double, POLYPIC_MAX_MODES_3D> coefficients{};

    // Offsets z_i = node - particle (grid units), trilinear weights w_i, samples v_i.
    glm::dvec3 const z[8] = {
        glm::dvec3(-fx, -fy, -fz),
        glm::dvec3(1.0 - fx, -fy, -fz),
        glm::dvec3(-fx, 1.0 - fy, -fz),
        glm::dvec3(1.0 - fx, 1.0 - fy, -fz),
        glm::dvec3(-fx, -fy, 1.0 - fz),
        glm::dvec3(1.0 - fx, -fy, 1.0 - fz),
        glm::dvec3(-fx, 1.0 - fy, 1.0 - fz),
        glm::dvec3(1.0 - fx, 1.0 - fy, 1.0 - fz),
    };
    double const w[8] = {
        (1.0 - fx) * (1.0 - fy) * (1.0 - fz),
        fx * (1.0 - fy) * (1.0 - fz),
        (1.0 - fx) * fy * (1.0 - fz),
        fx * fy * (1.0 - fz),
        (1.0 - fx) * (1.0 - fy) * fz,
        fx * (1.0 - fy) * fz,
        (1.0 - fx) * fy * fz,
        fx * fy * fz,
    };
    double const v[8] = {v000, v100, v010, v110, v001, v101, v011, v111};

    for (int r = 0; r < POLYPIC_MAX_MODES_3D; ++r)
    {
        if (r >= nr)
        {
            coefficients[r] = 0.0;
            continue;
        }
        double numer = 0.0;
        double denom = 0.0;
        for (int i = 0; i < 8; ++i)
        {
            double s = polypic_mode_value_3d(r, z[i]);
            numer += w[i] * s * v[i];
            denom += w[i] * s * s;
        }
        coefficients[r] = denom > 1.0e-12 ? numer / denom : 0.0;
    }

    return coefficients;
}

// Conditioning guard for the multiquadratic MPM modes.
namespace
{
constexpr std::array<double, MPM_POLYPIC_MAX_MODES_2D> MPM_MODE_DENOM_REF{
    1.0, 0.25, 0.25, 0.0625, 0.1875, 0.1875, 0.046875, 0.046875, 0.035156};

inline double mpm_effective_denom(int r, double denom, double quad_reg)
{
    return r >= 4 ? denom + quad_reg * MPM_MODE_DENOM_REF[r] : denom;
}
} // namespace

std::array<double, MPM_POLYPIC_MAX_MODES_2D> mpm_polypic_g2p_solve_component(std::span<double const> values,
                                                                             std::span<double const> weights,
                                                                             std::span<glm::dvec2 const> offsets,
                                                                             glm::dvec2 particle_offset, int nr,
                                                                             double quad_reg)
{
    nr = std::clamp(nr, 1, MPM_POLYPIC_MAX_MODES_2D);
    std::array<double, MPM_POLYPIC_MAX_MODES_2D> coefficients{};
    if (values.size() != weights.size() || values.size() != offsets.size())
        return coefficients;

    for (int r = 0; r < MPM_POLYPIC_MAX_MODES_2D; ++r)
    {
        if (r >= nr)
        {
            coefficients[r] = 0.0;
            continue;
        }

        double numer = 0.0;
        double denom = 0.0;
        for (size_t i = 0; i < values.size(); ++i)
        {
            double const s = mpm_polypic_mode_value(r, offsets[i], particle_offset);
            numer += weights[i] * s * values[i];
            denom += weights[i] * s * s;
        }
        double const eff = mpm_effective_denom(r, denom, quad_reg);
        coefficients[r] = eff > 1.0e-12 ? numer / eff : 0.0;
    }

    return coefficients;
}

std::array<glm::dvec2, MPM_POLYPIC_MAX_MODES_2D> mpm_polypic_g2p_solve_vector(std::span<glm::dvec2 const> values,
                                                                              std::span<double const> weights,
                                                                              std::span<glm::dvec2 const> offsets,
                                                                              glm::dvec2 particle_offset, int nr,
                                                                              double quad_reg)
{
    nr = std::clamp(nr, 1, MPM_POLYPIC_MAX_MODES_2D);
    std::array<glm::dvec2, MPM_POLYPIC_MAX_MODES_2D> coefficients{};
    if (values.size() != weights.size() || values.size() != offsets.size())
        return coefficients;

    for (int r = 0; r < MPM_POLYPIC_MAX_MODES_2D; ++r)
    {
        if (r >= nr)
        {
            coefficients[r] = glm::dvec2(0.0);
            continue;
        }

        glm::dvec2 numer(0.0);
        double denom = 0.0;
        for (size_t i = 0; i < values.size(); ++i)
        {
            double const s = mpm_polypic_mode_value(r, offsets[i], particle_offset);
            numer += weights[i] * s * values[i];
            denom += weights[i] * s * s;
        }
        double const eff = mpm_effective_denom(r, denom, quad_reg);
        coefficients[r] = eff > 1.0e-12 ? numer / eff : glm::dvec2(0.0);
    }

    return coefficients;
}
