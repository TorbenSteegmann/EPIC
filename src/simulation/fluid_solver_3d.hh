#pragma once

#include "../world.hh"
#include "core/mac_grid_3d.hh"
#include "core/simulation.hh"

#include <unordered_map>

class Fluid_Solver_3D : public Simulation
{
public:
    Fluid_Solver_3D(double dx, int nx, int ny, int nz, World& world, double dt);

    void step(double dt) override;
    Grid& grid() override { return grid_; };
    Stage_Diagnostics stage_diagnostics() const override { return stage_diagnostics_; }

private:
    World& world_;
    MAC_Grid_3D grid_;
    Stage_Diagnostics stage_diagnostics_;
    std::unordered_map<ecs::Entity, glm::dvec3> previous_particle_grid_residuals_;

    double max_particle_speed();
    double max_grid_speed();
    double particle_kinetic_energy();
    double particle_mechanical_energy();
    double grid_kinetic_energy();
    std::pair<double, double> divergence_stats();
    void update_particle_grid_residual_diagnostics();
};
