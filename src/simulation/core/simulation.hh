#pragma once

#include "grid.hh"

#include <glm/glm.hpp>

struct Projection_Diagnostics
{
    int fluid_cell_count = 0;
    int iterations = 0;
    bool converged = false;
    double initial_residual = 0.0;
    double final_residual = 0.0;
    double residual_ratio = 0.0;
    double pressure_min = 0.0;
    double pressure_max = 0.0;
    double pressure_mean = 0.0;
    double max_pressure_gradient = 0.0;
    double max_velocity_delta = 0.0;
};

struct Stage_Diagnostics
{
    double before_step_particle_energy = 0.0;
    double before_step_particle_kinetic_energy = 0.0;
    glm::dvec3 before_step_particle_angular_momentum{0.0};
    double after_particle_to_grid_max_grid_speed = 0.0;
    double after_particle_to_grid_grid_kinetic_energy = 0.0;
    glm::dvec3 after_particle_to_grid_grid_angular_momentum{0.0};
    double after_forces_max_grid_speed = 0.0;
    double after_forces_grid_kinetic_energy = 0.0;
    double after_first_extrapolate_max_grid_speed = 0.0;
    double after_first_extrapolate_grid_kinetic_energy = 0.0;
    double after_boundary_max_grid_speed = 0.0;
    double after_boundary_grid_kinetic_energy = 0.0;
    double before_projection_max_divergence = 0.0;
    double before_projection_rms_divergence = 0.0;
    Projection_Diagnostics projection;
    double after_projection_max_grid_speed = 0.0;
    double after_projection_grid_kinetic_energy = 0.0;
    glm::dvec3 after_projection_grid_angular_momentum{0.0};
    double after_projection_max_divergence = 0.0;
    double after_projection_rms_divergence = 0.0;
    double after_second_extrapolate_max_grid_speed = 0.0;
    double after_second_extrapolate_grid_kinetic_energy = 0.0;
    glm::dvec3 after_second_extrapolate_grid_angular_momentum{0.0};
    double after_grid_to_particle_max_particle_speed = 0.0;
    double after_grid_to_particle_particle_energy = 0.0;
    double after_grid_to_particle_particle_kinetic_energy = 0.0;
    glm::dvec3 after_grid_to_particle_particle_angular_momentum{0.0};
    double after_advect_max_particle_speed = 0.0;
    double after_advect_particle_energy = 0.0;
    double after_advect_particle_kinetic_energy = 0.0;
    glm::dvec3 after_advect_particle_angular_momentum{0.0};
    double after_advect_grid_residual_rms = 0.0;
    double after_advect_grid_residual_energy_per_mass = 0.0;
    double after_advect_grid_residual_energy_fraction = 0.0;
    double after_advect_grid_residual_delta_rms = 0.0;
    double after_advect_grid_residual_delta_energy_per_mass = 0.0;
    double after_advect_grid_residual_delta_energy_fraction = 0.0;
    double after_advect_grid_residual_reversal_fraction = 0.0;
    double after_advect_grid_residual_reversal_energy_per_mass = 0.0;
    double after_advect_grid_residual_reversal_energy_fraction = 0.0;
};

struct Simulation
{
    virtual ~Simulation() = default;
    virtual void step(double dt) = 0;
    virtual Grid& grid() = 0;
    virtual Stage_Diagnostics stage_diagnostics() const { return {}; }
};

enum Cell_Type
{
    AIR,
    FLUID,
    SOLID
};
