#pragma once

#include "core/simulation.hh"
#include "mpm_kernels/collocated_grid.hh"

class MPM : public Simulation
{
public:
    MPM(int nx, int ny, double dx, double dt, World& world);

    void step(double dt_) override;
    Grid& grid() override { return grid_; };

private:
    int nx_, ny_;

    World& world_;
    Collocated_Grid grid_;

    int num_particles_ = 0;

    void particle_to_grid();
    void compute_volumes_and_densities();
    void compute_grid_forces(double dt);
    void update_grid_velocities(double dt);
    void grid_body_collisions();
    void solve_linear_system();
    void update_deformation_gradient(double dt);
    void update_particle_velocities();
    void particle_collisions();
    void update_particle_positions(double dt);
};
