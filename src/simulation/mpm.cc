#include "mpm.hh"
#include "core/helpers.hh"
#include "fluid_kernels/polypic.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <print>
#include <span>

namespace
{
constexpr int MPM_SUPPORT_WIDTH = 3;

std::pair<int, int> support_base_index(Collocated_Grid& grid, glm::dvec2 pos)
{
    double const inv_dx = 1.0 / grid.dx();
    return {static_cast<int>(std::floor(pos.x * inv_dx - 0.5)), static_cast<int>(std::floor(pos.y * inv_dx - 0.5))};
}

glm::dvec2 particle_offset_from_support(Collocated_Grid& grid, glm::dvec2 pos, int base_i, int base_j)
{
    double const inv_dx = 1.0 / grid.dx();
    return pos * inv_dx - glm::dvec2(base_i + 1, base_j + 1);
}
} // namespace

MPM::MPM(int nx, int ny, double dx, double dt, World& world) : nx_(nx), ny_(ny), world_(world), grid_(nx, ny, dx, world)
{
    if (world_.settings().MPM_SCENE == MPM_COMPRESSED_SQUARE)
        grid_.add_compressed_square();
}

void MPM::step(double dt)
{
    // 1. Rasterize Particles to grid
    particle_to_grid();
    // 2. Compute Particle volumes and desities once
    if (grid_.particles().size() > num_particles_)
    {
        num_particles_ = grid_.particles().size();
        compute_volumes_and_densities();
    }
    // 3. Compute Grid Forces
    compute_grid_forces(dt);
    // 4. Update velocities on grid
    update_grid_velocities(dt);
    // 5. Grid-based body collisions
    grid_body_collisions();
    // 6. Solve the linear system
    // solve_linear_system(); // OPTIONAL
    // 7. Update deformation gradient
    update_deformation_gradient(dt);
    // 8. Update Particle velocities
    update_particle_velocities();
    // 9. Particle-based collisions
    particle_collisions();
    // 10. Update Particle positions
    update_particle_positions(dt);
}

void MPM::particle_to_grid()
{
    grid_.clear();
    auto particles = grid_.particles();

    for (auto p : particles)
    {
        auto pc = world_.get_component<Particle_Component>(p);
        auto mass = world_.get_component<Mass_Component>(p)->mass;
        glm::dvec2 pos = glm::dvec2(pc->position.x, pc->position.y);
        auto [i0, j0] = grid_.cell_index_from_pos(pos);

        if (!grid_.in_bounds(i0, j0))
            continue;

        grid_.cell_type(i0, j0) = grid_.cell_type(i0, j0) == SOLID ? SOLID : FLUID;
        grid_.mpm_cells().emplace_back(grid_.cell_index(i0, j0));

        auto [base_i, base_j] = support_base_index(grid_, pos);
        for (int j = base_j; j < base_j + MPM_SUPPORT_WIDTH; ++j)
        {
            for (int i = base_i; i < base_i + MPM_SUPPORT_WIDTH; ++i)
            {
                if (!grid_.in_bounds(i, j))
                    continue;
                double weight = grid_.cell_weight_from_pos(i, j, pos);
                grid_.cell_mass(i, j) += mass * weight;
            }
        }
    }

    double inv_D = 4.0 / (grid_.dx() * grid_.dx());

    for (auto p : particles)
    {
        auto pc = world_.get_component<Particle_Component>(p);
        auto mass = world_.get_component<Mass_Component>(p)->mass;
        glm::dvec2 pos = glm::dvec2(pc->position.x, pc->position.y);
        auto [i0, j0] = grid_.cell_index_from_pos(pos);

        if (!grid_.in_bounds(i0, j0))
            continue;

        auto [base_i, base_j] = support_base_index(grid_, pos);
        glm::dvec2 const particle_offset = particle_offset_from_support(grid_, pos, base_i, base_j);
        for (int j = base_j; j < base_j + MPM_SUPPORT_WIDTH; ++j)
        {
            for (int i = base_i; i < base_i + MPM_SUPPORT_WIDTH; ++i)
            {
                if (!grid_.in_bounds(i, j))
                    continue;

                double weight = grid_.cell_weight_from_pos(i, j, pos);
                double grid_mass = grid_.cell_mass(i, j);

                if (grid_mass > 1e-12)
                {
                    glm::dvec2 dist = grid_.cell_position(i, j) - pos;

                    glm::dvec2 v_p = glm::dvec2(pc->velocity.x, pc->velocity.y);
                    if (world_.settings().FLUID_SOLVER == APIC)
                    {
                        v_p.x += inv_D * glm::dot(pc->c_u, dist);
                        v_p.y += inv_D * glm::dot(pc->c_v, dist);
                    }
                    else if (world_.settings().FLUID_SOLVER == POLYPIC)
                    {
                        // PolyPIC reconstruction: v_p(z) = sum_r s_r(z) c_r, z = (node - particle)/dx.
                        // Mode math lives in polypic.hh; the constant mode is read from pc->velocity.
                        int nr = std::clamp(world_.settings().POLYPIC_MODES, 1, MPM_POLYPIC_MAX_MODES_2D);
                        glm::dvec2 z = dist / grid_.dx();
                        v_p = mpm_polypic_p2g_node_value(*pc, z, particle_offset, nr);
                    }

                    grid_.cell_velocity(i, j) += (v_p * mass * weight) / grid_mass;
                }
            }
        }
    }

    grid_.save();
}

void MPM::compute_volumes_and_densities()
{
    auto particles = grid_.particles();

    for (auto const& particle : particles)
    {
        auto pc = world_.get_component<Particle_Component>(particle);
        auto dc = world_.get_component<Deformable_Component>(particle);

        if (!dc->is_new)
            continue;
        dc->is_new = false;

        double mass = world_.get_component<Mass_Component>(particle)->mass;
        glm::dvec2 position = glm::dvec2(pc->position.x, pc->position.y);
        auto [base_i, base_j] = support_base_index(grid_, position);

        double p0 = 0.0;

        for (int j = base_j; j < base_j + MPM_SUPPORT_WIDTH; ++j)
        {
            for (int i = base_i; i < base_i + MPM_SUPPORT_WIDTH; ++i)
            {
                if (!grid_.in_bounds(i, j))
                    continue;

                double weight = grid_.cell_weight_from_pos(i, j, position);
                double cell_density = grid_.cell_density(i, j);
                p0 += cell_density * weight;
            }
        }

        if (p0 == 0)
        {
            std::println("WARNING: Particle did not gather cell densities (compute_volumes_and_densities())");
            p0 = 1.0;
        }

        dc->V_0 = mass / p0;
    }
}

void MPM::compute_grid_forces(double dt)
{
    auto particles = grid_.particles();
    if (particles.empty())
        return;

    for (auto const& particle : particles)
    {
        auto dc = world_.get_component<Deformable_Component>(particle);
        auto pc = world_.get_component<Particle_Component>(particle);
        auto mu = dc->mu;
        auto lambda = dc->lambda;

        auto F_E = dc->F_E;

        auto J_E = Fluid_Simulation_2D::determinant_2D(F_E);
        auto FT_E = glm::transpose(F_E);

        Eigen::Matrix2d F_E_EIGEN = Fluid_Simulation_2D::glm_to_eigen(F_E);
        Eigen::JacobiSVD<Eigen::Matrix2d> svd(F_E_EIGEN, Eigen::ComputeFullU | Eigen::ComputeFullV);

        glm::dmat2 R_E = Fluid_Simulation_2D::eigen_to_glm(svd.matrixU() * svd.matrixV().transpose());

        auto sigma = 2.0 * mu * (F_E - R_E) * FT_E + lambda * (J_E - 1.0) * J_E * glm::dmat2(1.0);

        double V = dc->V_0; // we were able to eliminate J from the equation here

        glm::dvec2 position(pc->position.x, pc->position.y);
        auto [base_i, base_j] = support_base_index(grid_, position);

        for (int i = base_i; i < base_i + MPM_SUPPORT_WIDTH; ++i)
        {
            for (int j = base_j; j < base_j + MPM_SUPPORT_WIDTH; ++j)
            {
                if (i < 0 || i >= nx_ || j < 0 || j >= ny_)
                    continue;

                glm::dvec2 grad_weight = grid_.cell_weight_gradient_from_pos(i, j, position);

                glm::dvec2 force_contribution = -V * (sigma * grad_weight);

                grid_.cell_force(i, j) += force_contribution;
            }
        }
    }

    if (world_.settings().APPLY_GRAVITY.load(std::memory_order_relaxed))
    {
        glm::dvec2 gravity{0, -9.81};
        for (int j = 0; j < ny_; ++j)
        {
            for (int i = 0; i < nx_; ++i)
            {
                double cell_mass = grid_.cell_mass(i, j);
                grid_.cell_force(i, j) += gravity * cell_mass;
            }
        }
    }
}

void MPM::update_grid_velocities(double dt)
{
    for (int j = 0; j < ny_; ++j)
    {
        for (int i = 0; i < nx_; ++i)
        {
            double cell_mass = grid_.cell_mass(i, j);
            if (cell_mass == 0.0)
                continue;

            else
                grid_.cell_velocity(i, j) += dt * (grid_.cell_force(i, j) / cell_mass);
        }
    }
}

void MPM::grid_body_collisions()
{
    auto mpm_cells = grid_.mpm_cells();
    auto collision_bodies = grid_.collision_bodies();

    for (auto const& mpm_cell : mpm_cells)
    {
        auto [i, j] = grid_.cell_index(mpm_cell);
        auto cell_position = grid_.cell_position(i, j);

        for (auto const& collision_body : collision_bodies)
        {
            auto cc = world_.get_component<Collision_Component>(collision_body);
            double phi = cc->sdf(cell_position);
            double are_colliding = phi <= 0;

            if (!are_colliding)
                continue;

            auto co_normal = cc->sdf_gradient(cell_position);
            auto co_velocity = cc->velocity;

            auto rel_velocity = grid_.cell_velocity(i, j) - co_velocity;
            double v_n = glm::dot(rel_velocity, co_normal);

            bool are_seperating = (v_n >= 0);

            if (are_seperating)
                continue;

            glm::dvec2 tangential_velocity = rel_velocity - co_normal * v_n;

            // TODO mu just set to 0.5 lazy testing
            double mu = 300.422;
            auto tan_vel_length = glm::length(tangential_velocity);
            glm::dvec2 v_p_rel;

            if (tan_vel_length <= -mu * v_n)
                v_p_rel = {0.0, 0.0};
            else
                v_p_rel = tangential_velocity + mu * v_n * glm::normalize(tangential_velocity);

            glm::dvec2 v_p = v_p_rel + co_velocity;

            grid_.cell_velocity(i, j) = v_p;
        }
    }
}

void MPM::solve_linear_system()
{
    // this is optional, skip for now
}

void MPM::update_deformation_gradient(double dt)
{
    auto particles = grid_.particles();

    for (auto particle : particles)
    {
        auto pc = world_.get_component<Particle_Component>(particle);
        auto dc = world_.get_component<Deformable_Component>(particle);

        glm::dvec2 position(pc->position.x, pc->position.y);
        auto [base_i, base_j] = support_base_index(grid_, position);

        glm::dmat2 grad_v(0.0);

        for (int i = base_i; i < base_i + MPM_SUPPORT_WIDTH; ++i)
        {
            for (int j = base_j; j < base_j + MPM_SUPPORT_WIDTH; ++j)
            {
                if (!grid_.in_bounds(i, j))
                    continue;

                auto g_vel = grid_.cell_velocity(i, j);
                auto w_grad = grid_.cell_weight_gradient_from_pos(i, j, position);

                grad_v[0][0] += g_vel[0] * w_grad[0];
                grad_v[1][0] += g_vel[1] * w_grad[0];
                grad_v[0][1] += g_vel[0] * w_grad[1];
                grad_v[1][1] += g_vel[1] * w_grad[1];
            }
        }


        auto F_E_temp = (glm::dmat2(1.0) + dt * grad_v) * dc->F_E;

        // since we dont care about plasticity right now we omit it here
        // TODO

        dc->F_E = F_E_temp;
        dc->F = F_E_temp * dc->F_P;
    }
}

void MPM::update_particle_velocities()
{
    if (world_.settings().FLUID_SOLVER == APIC)
    {
        for (auto const& particle : grid_.particles())
        {
            auto pc = world_.get_component<Particle_Component>(particle);
            glm::dvec2 position(pc->position.x, pc->position.y);
            auto [base_i, base_j] = support_base_index(grid_, position);

            pc->velocity = glm::dvec3(0.0);
            pc->c_u = glm::dvec2(0.0); // Affine x-velocity derivatives
            pc->c_v = glm::dvec2(0.0); // Affine y-velocity derivatives

            for (int j = base_j; j < base_j + MPM_SUPPORT_WIDTH; ++j)
            {
                for (int i = base_i; i < base_i + MPM_SUPPORT_WIDTH; ++i)
                {
                    if (!grid_.in_bounds(i, j))
                        continue;

                    double weight = grid_.cell_weight_from_pos(i, j, position);
                    glm::dvec2 v_i = grid_.cell_velocity(i, j);
                    glm::dvec2 dist = grid_.cell_position(i, j) - position;

                    // Standard PIC gather
                    pc->velocity += glm::dvec3(weight * v_i, 0.0);

                    // APIC Gather (Eq. 14):
                    // c_pa = sum( weight * grid_vel_a * dist )
                    pc->c_u += weight * v_i.x * dist;
                    pc->c_v += weight * v_i.y * dist;
                }
            }
        }
    }
    else if (world_.settings().FLUID_SOLVER == POLYPIC)
    {
        int nr = std::clamp(world_.settings().POLYPIC_MODES, 1, MPM_POLYPIC_MAX_MODES_2D);
        for (auto const& particle : grid_.particles())
        {
            auto pc = world_.get_component<Particle_Component>(particle);
            glm::dvec2 position = glm::dvec2(pc->position.x, pc->position.y);
            auto [base_i, base_j] = support_base_index(grid_, position);
            glm::dvec2 const particle_offset = particle_offset_from_support(grid_, position, base_i, base_j);

            std::array<glm::dvec2, MPM_POLYPIC_MAX_MODES_2D> values{};
            std::array<double, MPM_POLYPIC_MAX_MODES_2D> weights{};
            std::array<glm::dvec2, MPM_POLYPIC_MAX_MODES_2D> offsets{};
            int sample_count = 0;

            for (int j = base_j; j < base_j + MPM_SUPPORT_WIDTH; ++j)
            {
                for (int i = base_i; i < base_i + MPM_SUPPORT_WIDTH; ++i)
                {
                    if (!grid_.in_bounds(i, j))
                        continue;

                    weights[sample_count] = grid_.cell_weight_from_pos(i, j, position);
                    values[sample_count] = grid_.cell_velocity(i, j);
                    offsets[sample_count] = (grid_.cell_position(i, j) - position) / grid_.dx();
                    ++sample_count;
                }
            }

            auto coefficients = mpm_polypic_g2p_solve_vector(
                std::span<glm::dvec2 const>(values.data(), sample_count),
                std::span<double const>(weights.data(), sample_count),
                std::span<glm::dvec2 const>(offsets.data(), sample_count),
                particle_offset, nr, world_.settings().MPM_POLYPIC_QUAD_REG);

            pc->poly_c.fill(glm::dvec2(0.0));
            pc->mpm_poly_c.fill(glm::dvec2(0.0));
            pc->velocity = glm::dvec3(coefficients[0], 0.0); // constant mode -> particle velocity
            for (int r = 1; r < nr; ++r)
                pc->mpm_poly_c[r] = coefficients[r];
        }
    }
    else
    {
        double alpha = std::clamp(static_cast<double>(world_.settings().FLIP_PERCENT), 0.0, 1.0);
        for (auto const& particle : grid_.particles())
        {
            auto pc = world_.get_component<Particle_Component>(particle);
            glm::dvec2 position = glm::dvec2(pc->position.x, pc->position.y);
            auto [base_i, base_j] = support_base_index(grid_, position);

            glm::dvec2 pic_velocity{0.0};
            glm::dvec2 grid_delta{0.0};

            for (int j = base_j; j < base_j + MPM_SUPPORT_WIDTH; ++j)
            {
                for (int i = base_i; i < base_i + MPM_SUPPORT_WIDTH; ++i)
                {
                    if (!grid_.in_bounds(i, j))
                        continue;

                    double weight = grid_.cell_weight_from_pos(i, j, position);

                    pic_velocity += grid_.cell_velocity(i, j) * weight;

                    grid_delta += (grid_.cell_velocity(i, j) - grid_.old_cell_velocity(i, j)) * weight;
                }
            }

            glm::dvec2 flip_velocity = glm::dvec2(pc->velocity.x, pc->velocity.y) + grid_delta;

            pc->velocity = glm::dvec3((1.0 - alpha) * pic_velocity + alpha * flip_velocity, 0.0);
        }
    }
}

void MPM::particle_collisions()
{
    auto particles = grid_.particles();
    auto collision_bodies = grid_.collision_bodies();

    for (auto particle : particles)
    {
        auto pc = world_.get_component<Particle_Component>(particle);
        glm::dvec2 pos = glm::dvec2(pc->position.x, pc->position.y);

        for (auto const& collision_body : collision_bodies)
        {
            auto cc = world_.get_component<Collision_Component>(collision_body);

            double phi = cc->sdf(pos);
            if (phi > 0)
                continue; // Not colliding

            auto co_normal = cc->sdf_gradient(pos);
            auto co_velocity = cc->velocity;

            auto rel_velocity = glm::dvec2(pc->velocity.x, pc->velocity.y) - co_velocity;
            double v_n = glm::dot(rel_velocity, co_normal);

            if (v_n >= 0)
                continue;

            glm::dvec2 tangential_velocity = rel_velocity - co_normal * v_n;

            double mu = 300.422;
            double tan_vel_length = glm::length(tangential_velocity);
            glm::dvec2 v_p_rel;

            if (tan_vel_length <= -mu * v_n)
            {
                v_p_rel = {0.0, 0.0};
            }
            else
            {
                v_p_rel = tangential_velocity + mu * v_n * (tangential_velocity / tan_vel_length);
            }

            pc->velocity = glm::dvec3(v_p_rel + co_velocity, 0.0);
        }
    }
}

void MPM::update_particle_positions(double dt)
{
    for (auto const& particle : grid_.particles())
    {
        auto pc = world_.get_component<Particle_Component>(particle);

        pc->position += dt * pc->velocity;
    }
}
