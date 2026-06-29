#pragma once

#include "../../world.hh"
#include "grid.hh"

struct Timestep_Decision
{
    int substeps = 1;
    double sub_dt = 0.0;
    double frame_dt = 0.0;
    double estimated_frame_cfl = 0.0;
    double max_particle_speed = 0.0;
};

class Timestep_Controller
{
public:
    Timestep_Decision decide(World& world, Grid& grid, double frame_dt) const;

private:
    double target_cfl_ = 0.5;
    int max_substeps_ = 8;
};
