#pragma once

#include "ecs/ecs_common.hh"
#include "simulation/core/grid.hh"
#include "simulation/core/simulation.hh"
#include "world.hh"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

struct Diagnostics_Sample
{
    std::uint64_t step = 0;
    double time = 0.0;
    double dt = 0.0;
    int solver_substeps = 1;
    double frame_dt = 0.0;
    bool adaptive_timestep = false;
    double target_dt = 0.0;
    Stage_Diagnostics stages;
    std::size_t particle_count = 0;
    double max_speed = 0.0;
    double mean_speed = 0.0;
    double rms_speed = 0.0;
    double total_mass = 0.0;
    int fluid_measure_dimension = 0;
    std::size_t fluid_cell_count = 0;
    double fluid_area_estimate = 0.0;
    double fluid_volume_estimate = 0.0;
    double kinetic_energy = 0.0;
    double potential_energy = 0.0;
    double total_energy = 0.0;
    double velocity_delta_rms = 0.0;
    double ringing_index = 0.0;
    double velocity_reversal_fraction = 0.0;
    std::size_t ringing_particle_count = 0;
    std::size_t strong_reversal_count = 0;
    std::size_t active_ringing_candidate_count = 0;
    double strong_reversal_fraction = 0.0;
    std::size_t oscillating_particle_count = 0;
    double oscillating_particle_fraction = 0.0;
    double oscillation_velocity_delta_rms = 0.0;
    std::size_t alternating_acceleration_count = 0;
    std::size_t active_acceleration_candidate_count = 0;
    double alternating_acceleration_fraction = 0.0;
    std::size_t strict_ringing_particle_count = 0;
    double strict_ringing_particle_fraction = 0.0;
    double strict_ringing_velocity_delta_rms = 0.0;
    glm::dvec3 center_of_mass{0.0};
    glm::dvec3 total_momentum{0.0};
    glm::dvec3 orbital_angular_momentum{0.0};
    glm::dvec3 represented_angular_momentum{0.0};
    glm::dvec3 core_angular_momentum{0.0};
    double core_radius = 0.0;
    double cfl = 0.0;
    std::size_t cfl_particle_index = 0;
    ecs::Entity cfl_particle_entity = ecs::invalid_entity;
    glm::dvec3 cfl_particle_position{0.0};
    glm::dvec3 cfl_particle_velocity{0.0};
    bool cfl_particle_near_boundary = false;
    double cfl_particle_boundary_distance = 0.0;
    bool cfl_particle_boundary_collision = false;
    glm::ivec3 cfl_boundary_cell{0};
    glm::dvec3 cfl_boundary_normal{0.0};
    glm::dvec3 cfl_boundary_velocity_before{0.0};
    glm::dvec3 cfl_boundary_velocity_after{0.0};
    double cfl_boundary_speed_before = 0.0;
    double cfl_boundary_speed_after = 0.0;
    double cfl_boundary_normal_velocity_before = 0.0;
    double cfl_boundary_normal_velocity_after = 0.0;
    double cfl_boundary_impulse = 0.0;
    int invalid_count = 0;
    int out_of_bounds_count = 0;
    int boundary_collision_count = 0;
    double max_poly_coeff = 0.0; // max |PolyPIC coefficient| over valid particles (0 for non-PolyPIC)
};

struct Particle_Inspection
{
    bool valid_selection = false;
    std::size_t index = 0;
    ecs::Entity entity = 0;
    glm::dvec3 position{0.0};
    glm::dvec3 velocity{0.0};
    double speed = 0.0;
    float radius = 0.0f;
    glm::ivec3 nearest_cell{0};
    double distance_to_boundary = 0.0;
    bool finite = true;
    bool out_of_bounds = false;
    glm::dvec2 c_u{0.0};
    glm::dvec2 c_v{0.0};
    glm::dvec3 c_u_3d{0.0};
    glm::dvec3 c_v_3d{0.0};
    glm::dvec3 c_w_3d{0.0};
    std::array<glm::dvec2, 4> poly_c{}; // PolyPIC modes (u, v) for {1, x, y, xy}
    std::array<glm::dvec3, 8> poly_c_3d{}; // PolyPIC modes (u, v, w) for {1, x, y, z, xy, xz, yz, xyz}
};

struct Particle_Ringing_State
{
    static constexpr std::size_t window_size = 10;

    std::array<bool, window_size> strong_reversals{};
    std::array<bool, window_size> acceleration_reversals{};
    std::size_t next_index = 0;
    std::size_t sample_count = 0;
    std::size_t reversal_count = 0;
    std::size_t acceleration_next_index = 0;
    std::size_t acceleration_sample_count = 0;
    std::size_t acceleration_reversal_count = 0;
    glm::dvec3 previous_velocity_delta{0.0};
    bool has_previous_velocity_delta = false;
};

class Diagnostics
{
public:
    void reset();
    Diagnostics_Sample record_step(World& world, Grid& grid, Stage_Diagnostics const& stages,
                                   std::uint64_t step, double time, double dt, int solver_substeps = 1, double frame_dt = 0.0);
    std::vector<Diagnostics_Sample> samples() const;
    Diagnostics_Sample latest_sample() const;
    Particle_Inspection selected_particle() const;
    void clear_selection();
    ecs::Entity selected_particle_entity() const;
    ecs::Entity cfl_particle_entity() const;

    void select_nearest_particle(World& world, Grid& grid, glm::mat4 const& view, glm::mat4 const& projection, int window_width, int window_height,
                                 double mouse_x, double mouse_y, float max_distance_px);

    bool enabled() const { return enabled_.load(std::memory_order_relaxed); }
    void set_enabled(bool value) { enabled_.store(value, std::memory_order_relaxed); }

    bool pause_on_invalid() const { return pause_on_invalid_.load(std::memory_order_relaxed); }
    void set_pause_on_invalid(bool value) { pause_on_invalid_.store(value, std::memory_order_relaxed); }

    bool pause_on_cfl() const { return pause_on_cfl_.load(std::memory_order_relaxed); }
    void set_pause_on_cfl(bool value) { pause_on_cfl_.store(value, std::memory_order_relaxed); }

    double cfl_pause_threshold() const { return cfl_pause_threshold_.load(std::memory_order_relaxed); }
    void set_cfl_pause_threshold(double value) { cfl_pause_threshold_.store(value, std::memory_order_relaxed); }

    bool should_pause_for(Diagnostics_Sample const& sample) const;

    bool export_csv(World& world, Grid& grid, double sim_time);
    bool export_csv_to(World& world, Grid& grid, double sim_time, std::string const& out_dir);
    std::string export_status() const;
    std::string last_export_path() const;

private:
    mutable std::mutex mutex_;
    std::vector<Diagnostics_Sample> samples_;
    Particle_Inspection selected_particle_;
    std::unordered_map<ecs::Entity, glm::dvec3> previous_particle_velocities_;
    std::unordered_map<ecs::Entity, Particle_Ringing_State> particle_ringing_states_;
    std::string export_status_;
    std::string last_export_path_;

    std::atomic<bool> enabled_{true};
    std::atomic<bool> pause_on_invalid_{false};
    std::atomic<bool> pause_on_cfl_{false};
    std::atomic<double> cfl_pause_threshold_{1.0};

    static constexpr std::size_t max_samples_ = 4096;
};
