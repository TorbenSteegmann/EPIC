#include "diagnostics.hh"

#include "simulation/core/mac_grid.hh"
#include "simulation/core/mac_grid_3d.hh"
#include "simulation/fluid_kernels/angular_momentum.hh"
#include "simulation/fluid_kernels/add_confined_vortex.hh"
#include "simulation/fluid_kernels/polypic.hh"
#include "profile_timer.hh"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
struct Diagnostics_Bounds
{
    glm::dvec3 lo{0.0};
    glm::dvec3 hi{100.0, 100.0, 0.0};
    glm::dvec3 fluid_lo{0.0};
    glm::dvec3 fluid_hi{100.0, 100.0, 0.0};
    double dx = 1.0;
    int dimensions = 2;
};

struct Fluid_Measure
{
    int dimension = 0;
    std::size_t fluid_cell_count = 0;
    double area_estimate = 0.0;
    double volume_estimate = 0.0;
};

constexpr double ringing_speed_epsilon = 1.0e-12;
constexpr double ringing_delta_epsilon = 1.0e-12;
constexpr double ringing_velocity_floor = 1.0e-6;
constexpr std::size_t ringing_oscillation_min_reversals = 5;
constexpr double ringing_active_speed_floor = 0.05;
constexpr double ringing_active_speed_max_fraction = 0.01;
constexpr double ringing_delta_floor = 0.05;
constexpr double ringing_delta_max_fraction = 0.01;
constexpr double ringing_reversal_cosine_threshold = -0.25;
constexpr double ringing_acceleration_reversal_cosine_threshold = -0.35;

void push_strong_reversal(Particle_Ringing_State& state, bool strong_reversal)
{
    bool const overwritten = state.strong_reversals[state.next_index];
    if (state.sample_count == Particle_Ringing_State::window_size && overwritten)
        --state.reversal_count;

    state.strong_reversals[state.next_index] = strong_reversal;
    if (strong_reversal)
        ++state.reversal_count;

    state.next_index = (state.next_index + 1) % Particle_Ringing_State::window_size;
    if (state.sample_count < Particle_Ringing_State::window_size)
        ++state.sample_count;
}

void push_acceleration_reversal(Particle_Ringing_State& state, bool acceleration_reversal)
{
    bool const overwritten = state.acceleration_reversals[state.acceleration_next_index];
    if (state.acceleration_sample_count == Particle_Ringing_State::window_size && overwritten)
        --state.acceleration_reversal_count;

    state.acceleration_reversals[state.acceleration_next_index] = acceleration_reversal;
    if (acceleration_reversal)
        ++state.acceleration_reversal_count;

    state.acceleration_next_index = (state.acceleration_next_index + 1) % Particle_Ringing_State::window_size;
    if (state.acceleration_sample_count < Particle_Ringing_State::window_size)
        ++state.acceleration_sample_count;
}

Diagnostics_Bounds bounds_from(World& world, Grid& grid)
{
    FLUID_PROFILE_SCOPE("Diagnostics::bounds_from");

    Diagnostics_Bounds bounds;

    if (auto* mac_grid = dynamic_cast<MAC_Grid_2D*>(&grid))
    {
        bounds.lo = glm::dvec3(mac_grid->origin(), 0.0);
        bounds.hi = bounds.lo + glm::dvec3(mac_grid->nx() * mac_grid->dx(), mac_grid->ny() * mac_grid->dx(), 0.0);
        bounds.fluid_lo = bounds.lo + glm::dvec3(mac_grid->dx(), mac_grid->dx(), 0.0);
        bounds.fluid_hi = bounds.lo + glm::dvec3((mac_grid->nx() - 1) * mac_grid->dx(), (mac_grid->ny() - 1) * mac_grid->dx(), 0.0);
        bounds.dx = mac_grid->dx();
        bounds.dimensions = 2;
        return bounds;
    }

    if (auto* mac_grid = dynamic_cast<MAC_Grid_3D*>(&grid))
    {
        bounds.lo = mac_grid->domain_min();
        bounds.hi = mac_grid->domain_max();
        bounds.fluid_lo = mac_grid->fluid_domain_min();
        bounds.fluid_hi = mac_grid->fluid_domain_max();
        bounds.dx = mac_grid->dx();
        bounds.dimensions = 3;
        return bounds;
    }

    auto& boxes = world.get_array<Box_Component>().data();
    if (!boxes.empty())
    {
        bounds.lo = glm::dvec3(boxes.front().offset.x, boxes.front().offset.y, 0.0);
        bounds.hi = glm::dvec3(boxes.front().offset.x + boxes.front().size.x, boxes.front().offset.y + boxes.front().size.y, 0.0);
        bounds.fluid_lo = bounds.lo;
        bounds.fluid_hi = bounds.hi;
        bounds.dimensions = 2;
        return bounds;
    }

    bounds.fluid_lo = bounds.lo;
    bounds.fluid_hi = bounds.hi;

    return bounds;
}

Fluid_Measure fluid_measure_from(Grid& grid)
{
    FLUID_PROFILE_SCOPE("Diagnostics::fluid_measure_from");

    Fluid_Measure measure;

    if (auto* mac_grid = dynamic_cast<MAC_Grid_2D*>(&grid))
    {
        measure.dimension = 2;
        for (int j = 0; j < mac_grid->ny(); ++j)
            for (int i = 0; i < mac_grid->nx(); ++i)
                if (mac_grid->cell_type(i, j) == FLUID)
                    ++measure.fluid_cell_count;

        measure.area_estimate = static_cast<double>(measure.fluid_cell_count) * mac_grid->dx() * mac_grid->dx();
        measure.volume_estimate = measure.area_estimate;
        return measure;
    }

    if (auto* mac_grid = dynamic_cast<MAC_Grid_3D*>(&grid))
    {
        measure.dimension = 3;
        for (int k = 0; k < mac_grid->nz(); ++k)
            for (int j = 0; j < mac_grid->ny(); ++j)
                for (int i = 0; i < mac_grid->nx(); ++i)
                    if (mac_grid->cell_type(i, j, k) == FLUID)
                        ++measure.fluid_cell_count;

        measure.volume_estimate = static_cast<double>(measure.fluid_cell_count) * mac_grid->dx() * mac_grid->dx() * mac_grid->dx();
        return measure;
    }

    return measure;
}

bool finite_particle(Particle_Component const& particle)
{
    return std::isfinite(particle.position.x) && std::isfinite(particle.position.y) &&
           std::isfinite(particle.position.z) && std::isfinite(particle.velocity.x) &&
           std::isfinite(particle.velocity.y) && std::isfinite(particle.velocity.z) &&
           polypic_coefficients_finite(particle);
}

bool outside_bounds(glm::dvec3 const& position, Diagnostics_Bounds const& bounds)
{
    return position.x < bounds.lo.x || position.x > bounds.hi.x ||
           position.y < bounds.lo.y || position.y > bounds.hi.y ||
           (bounds.dimensions == 3 && (position.z < bounds.lo.z || position.z > bounds.hi.z));
}

double distance_to_boundary(glm::dvec3 const& position, Diagnostics_Bounds const& bounds)
{
    double distance = std::min(
        std::min(position.x - bounds.fluid_lo.x, bounds.fluid_hi.x - position.x),
        std::min(position.y - bounds.fluid_lo.y, bounds.fluid_hi.y - position.y));

    if (bounds.dimensions == 3)
        distance = std::min(distance, std::min(position.z - bounds.fluid_lo.z, bounds.fluid_hi.z - position.z));

    return distance;
}

Particle_Inspection inspect_particle(World& world, std::size_t index, Diagnostics_Bounds const& bounds)
{
    Particle_Inspection inspection;
    auto& particles = world.get_array<Particle_Component>().data();
    auto& entities = world.get_array<Particle_Component>().entities();
    if (index >= particles.size())
        return inspection;

    Particle_Component const& particle = particles[index];
    inspection.valid_selection = true;
    inspection.index = index;
    inspection.entity = entities[index];
    inspection.position = particle.position;
    inspection.velocity = particle.velocity;
    inspection.speed = glm::length(particle.velocity);
    inspection.radius = particle.radius;
    inspection.nearest_cell = glm::ivec3(glm::floor((particle.position - bounds.lo) / bounds.dx));
    if (bounds.dimensions == 2)
        inspection.nearest_cell.z = 0;
    inspection.distance_to_boundary = distance_to_boundary(particle.position, bounds);
    inspection.finite = finite_particle(particle);
    inspection.out_of_bounds = outside_bounds(particle.position, bounds);
    inspection.c_u = particle.c_u;
    inspection.c_v = particle.c_v;
    inspection.c_u_3d = particle.c_u_3d;
    inspection.c_v_3d = particle.c_v_3d;
    inspection.c_w_3d = particle.c_w_3d;
    inspection.poly_c = particle.poly_c;
    inspection.poly_c_3d = particle.poly_c_3d;

    return inspection;
}

std::string local_time_string(char const* format)
{
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream out;
    out << std::put_time(&tm, format);
    return out.str();
}
} // namespace

void Diagnostics::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    samples_.clear();
    selected_particle_ = Particle_Inspection{};
    previous_particle_velocities_.clear();
    particle_ringing_states_.clear();
    export_status_.clear();
    last_export_path_.clear();
}

Diagnostics_Sample Diagnostics::record_step(World& world, Grid& grid, Stage_Diagnostics const& stages,
                                            std::uint64_t step, double time, double dt, int solver_substeps, double frame_dt)
{
    FLUID_PROFILE_SCOPE("Diagnostics::record_step");

    Diagnostics_Sample sample;
    sample.step = step;
    sample.time = time;
    sample.dt = dt;
    sample.solver_substeps = solver_substeps;
    sample.frame_dt = frame_dt > 0.0 ? frame_dt : dt;
    sample.adaptive_timestep = world.settings().ADAPTIVE_TIMESTEP.load(std::memory_order_relaxed);
    sample.target_dt = world.settings().SIMULATION_DT.load(std::memory_order_relaxed);
    sample.stages = stages;

    if (!enabled())
        return sample;

    Diagnostics_Bounds bounds = bounds_from(world, grid);
    Fluid_Measure fluid_measure = fluid_measure_from(grid);
    sample.fluid_measure_dimension = fluid_measure.dimension;
    sample.fluid_cell_count = fluid_measure.fluid_cell_count;
    sample.fluid_area_estimate = fluid_measure.area_estimate;
    sample.fluid_volume_estimate = fluid_measure.volume_estimate;

    auto& particles = world.get_array<Particle_Component>().data();
    auto& entities = world.get_array<Particle_Component>().entities();
    sample.particle_count = particles.size();

    std::unordered_map<ecs::Entity, glm::dvec3> previous_particle_velocities;
    std::unordered_map<ecs::Entity, Particle_Ringing_State> particle_ringing_states;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        previous_particle_velocities = previous_particle_velocities_;
        particle_ringing_states = particle_ringing_states_;
    }

    double speed_sum = 0.0;
    double speed_sq_sum = 0.0;
    double velocity_delta_sq_sum = 0.0;
    glm::dvec3 position_sum{0.0};
    std::size_t valid_count = 0;
    std::size_t reversal_count = 0;
    std::unordered_map<ecs::Entity, glm::dvec3> current_particle_velocities;
    current_particle_velocities.reserve(particles.size());

    double const gravity_magnitude = world.settings().APPLY_GRAVITY.load(std::memory_order_relaxed) ? 9.81 : 0.0;

    for (std::size_t particle_index = 0; particle_index < particles.size(); ++particle_index)
    {
        auto const& particle = particles[particle_index];
        if (!finite_particle(particle))
        {
            ++sample.invalid_count;
            continue;
        }

        double mass = 1.0;
        if (auto* mass_component = world.get_component<Mass_Component>(entities[particle_index]))
            mass = mass_component->mass;

        double speed = glm::length(particle.velocity);
        ecs::Entity const entity = entities[particle_index];
        current_particle_velocities.emplace(entity, particle.velocity);
        auto const previous = previous_particle_velocities.find(entity);
        if (previous != previous_particle_velocities.end())
        {
            glm::dvec3 delta = particle.velocity - previous->second;
            velocity_delta_sq_sum += glm::dot(delta, delta);
            if (glm::dot(particle.velocity, previous->second) < 0.0)
                ++reversal_count;
            ++sample.ringing_particle_count;
        }

        ++valid_count;
        if (valid_count == 1 || speed > sample.max_speed)
        {
            sample.max_speed = speed;
            sample.cfl_particle_index = particle_index;
            sample.cfl_particle_entity = entities[sample.cfl_particle_index];
            sample.cfl_particle_position = particle.position;
            sample.cfl_particle_velocity = particle.velocity;
        }
        speed_sum += speed;
        speed_sq_sum += speed * speed;
        sample.total_mass += mass;
        sample.kinetic_energy += 0.5 * mass * speed * speed;
        sample.potential_energy += mass * gravity_magnitude * (particle.position.y - bounds.fluid_lo.y);
        sample.total_momentum += mass * particle.velocity;
        position_sum += mass * particle.position;
        sample.max_poly_coeff = std::max(sample.max_poly_coeff, polypic_max_abs_coefficient(particle));

        if (outside_bounds(particle.position, bounds))
            ++sample.out_of_bounds_count;
    }

    if (valid_count > 0)
    {
        double const count = static_cast<double>(valid_count);
        sample.mean_speed = speed_sum / count;
        sample.rms_speed = std::sqrt(speed_sq_sum / count);
        if (sample.total_mass > 0.0)
            sample.center_of_mass = position_sum / sample.total_mass;
    }
    if (sample.ringing_particle_count > 0)
    {
        double const count = static_cast<double>(sample.ringing_particle_count);
        sample.velocity_delta_rms = std::sqrt(velocity_delta_sq_sum / count);
        sample.velocity_reversal_fraction = static_cast<double>(reversal_count) / count;
        if (sample.rms_speed >= ringing_speed_epsilon || sample.velocity_delta_rms >= ringing_delta_epsilon)
            sample.ringing_index = sample.velocity_delta_rms / std::max(sample.rms_speed, ringing_velocity_floor);
    }

    double const active_speed_floor = std::max(ringing_active_speed_floor, ringing_active_speed_max_fraction * sample.max_speed);
    double const active_delta_floor = std::max(ringing_delta_floor, ringing_delta_max_fraction * sample.max_speed);
    std::unordered_map<ecs::Entity, Particle_Ringing_State> current_particle_ringing_states;
    current_particle_ringing_states.reserve(particles.size());
    double oscillation_delta_sq_sum = 0.0;
    std::size_t oscillation_delta_count = 0;
    double strict_ringing_delta_sq_sum = 0.0;
    std::size_t strict_ringing_delta_count = 0;
    for (std::size_t particle_index = 0; particle_index < particles.size(); ++particle_index)
    {
        auto const& particle = particles[particle_index];
        if (!finite_particle(particle))
            continue;

        ecs::Entity const entity = entities[particle_index];
        Particle_Ringing_State state;
        auto const state_previous = particle_ringing_states.find(entity);
        if (state_previous != particle_ringing_states.end())
            state = state_previous->second;

        bool strong_reversal = false;
        bool acceleration_reversal = false;
        auto const previous = previous_particle_velocities.find(entity);
        if (previous != previous_particle_velocities.end())
        {
            double const speed = glm::length(particle.velocity);
            double const previous_speed = glm::length(previous->second);
            glm::dvec3 const delta = particle.velocity - previous->second;
            double const delta_length = glm::length(delta);
            bool const active_candidate = speed >= active_speed_floor && previous_speed >= active_speed_floor && delta_length >= active_delta_floor;
            if (active_candidate)
            {
                ++sample.active_ringing_candidate_count;
                strong_reversal = glm::dot(particle.velocity, previous->second) <= ringing_reversal_cosine_threshold * speed * previous_speed;
                if (strong_reversal)
                    ++sample.strong_reversal_count;
            }

            if (state.has_previous_velocity_delta)
            {
                double const previous_delta_length = glm::length(state.previous_velocity_delta);
                bool const active_acceleration_candidate = active_candidate && previous_delta_length >= active_delta_floor;
                if (active_acceleration_candidate)
                {
                    ++sample.active_acceleration_candidate_count;
                    acceleration_reversal =
                        glm::dot(delta, state.previous_velocity_delta) <= ringing_acceleration_reversal_cosine_threshold * delta_length * previous_delta_length;
                    if (acceleration_reversal)
                        ++sample.alternating_acceleration_count;
                }
            }
            state.previous_velocity_delta = delta;
            state.has_previous_velocity_delta = true;
        }

        push_strong_reversal(state, strong_reversal);
        push_acceleration_reversal(state, acceleration_reversal);
        bool const oscillating = state.sample_count >= Particle_Ringing_State::window_size && state.reversal_count >= ringing_oscillation_min_reversals;
        if (oscillating)
        {
            ++sample.oscillating_particle_count;
            if (previous != previous_particle_velocities.end())
            {
                glm::dvec3 const delta = particle.velocity - previous->second;
                oscillation_delta_sq_sum += glm::dot(delta, delta);
                ++oscillation_delta_count;
            }
        }
        bool const strict_ringing = state.acceleration_sample_count >= Particle_Ringing_State::window_size &&
                                   state.acceleration_reversal_count >= ringing_oscillation_min_reversals;
        if (strict_ringing)
        {
            ++sample.strict_ringing_particle_count;
            if (previous != previous_particle_velocities.end())
            {
                glm::dvec3 const delta = particle.velocity - previous->second;
                strict_ringing_delta_sq_sum += glm::dot(delta, delta);
                ++strict_ringing_delta_count;
            }
        }
        current_particle_ringing_states.emplace(entity, state);
    }

    if (sample.active_ringing_candidate_count > 0)
        sample.strong_reversal_fraction = static_cast<double>(sample.strong_reversal_count) / static_cast<double>(sample.active_ringing_candidate_count);
    if (valid_count > 0)
        sample.oscillating_particle_fraction = static_cast<double>(sample.oscillating_particle_count) / static_cast<double>(valid_count);
    if (oscillation_delta_count > 0)
        sample.oscillation_velocity_delta_rms = std::sqrt(oscillation_delta_sq_sum / static_cast<double>(oscillation_delta_count));
    if (sample.active_acceleration_candidate_count > 0)
        sample.alternating_acceleration_fraction =
            static_cast<double>(sample.alternating_acceleration_count) / static_cast<double>(sample.active_acceleration_candidate_count);
    if (valid_count > 0)
        sample.strict_ringing_particle_fraction = static_cast<double>(sample.strict_ringing_particle_count) / static_cast<double>(valid_count);
    if (strict_ringing_delta_count > 0)
        sample.strict_ringing_velocity_delta_rms = std::sqrt(strict_ringing_delta_sq_sum / static_cast<double>(strict_ringing_delta_count));

    sample.total_energy = sample.kinetic_energy + sample.potential_energy;

    for (std::size_t particle_index = 0; particle_index < particles.size(); ++particle_index)
    {
        auto const& particle = particles[particle_index];
        if (!finite_particle(particle))
            continue;

        double mass = 1.0;
        if (auto* mass_component = world.get_component<Mass_Component>(entities[particle_index]))
            mass = mass_component->mass;
        sample.orbital_angular_momentum += mass * glm::cross(particle.position - sample.center_of_mass, particle.velocity);
    }
    sample.represented_angular_momentum = sample.orbital_angular_momentum;
    if (auto* mac_grid = dynamic_cast<MAC_Grid_2D*>(&grid))
    {
        sample.represented_angular_momentum = Fluid_Simulation_2D::particle_represented_angular_momentum(world, *mac_grid);
        if (world.settings().FLUID_SCENE == CONFINED_VORTEX)
            sample.core_radius = Fluid_Simulation_2D::confined_vortex_support_radius(*mac_grid);
        sample.core_angular_momentum = Fluid_Simulation_2D::core_angular_momentum(world, *mac_grid, sample.core_radius);
    }

    if (bounds.dx > 0.0)
        sample.cfl = sample.max_speed * dt / bounds.dx;

    if (sample.cfl_particle_entity != ecs::invalid_entity)
    {
        sample.cfl_particle_boundary_distance = distance_to_boundary(sample.cfl_particle_position, bounds);
        sample.cfl_particle_near_boundary = sample.cfl_particle_boundary_distance <= bounds.dx;
    }

    auto const& boundary_collisions = world.boundary_collisions();
    sample.boundary_collision_count = static_cast<int>(boundary_collisions.size());
    double max_cfl_particle_impulse = -1.0;
    for (auto const& event : boundary_collisions)
    {
        if (event.entity != sample.cfl_particle_entity)
            continue;

        double impulse = glm::length(event.velocity_after - event.velocity_before);
        if (impulse <= max_cfl_particle_impulse)
            continue;

        max_cfl_particle_impulse = impulse;
        sample.cfl_particle_boundary_collision = true;
        sample.cfl_boundary_cell = event.solid_cell;
        sample.cfl_boundary_normal = event.normal;
        sample.cfl_boundary_velocity_before = event.velocity_before;
        sample.cfl_boundary_velocity_after = event.velocity_after;
        sample.cfl_boundary_speed_before = glm::length(event.velocity_before);
        sample.cfl_boundary_speed_after = glm::length(event.velocity_after);
        sample.cfl_boundary_normal_velocity_before = glm::dot(event.velocity_before, event.normal);
        sample.cfl_boundary_normal_velocity_after = glm::dot(event.velocity_after, event.normal);
        sample.cfl_boundary_impulse = impulse;
    }
    world.clear_boundary_collisions();

    std::lock_guard<std::mutex> lock(mutex_);
    previous_particle_velocities_ = std::move(current_particle_velocities);
    particle_ringing_states_ = std::move(current_particle_ringing_states);
    samples_.push_back(sample);
    if (samples_.size() > max_samples_)
        samples_.erase(samples_.begin(), samples_.begin() + static_cast<std::ptrdiff_t>(samples_.size() - max_samples_));

    if (selected_particle_.valid_selection)
        selected_particle_ = inspect_particle(world, selected_particle_.index, bounds);

    return sample;
}

std::vector<Diagnostics_Sample> Diagnostics::samples() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_;
}

Diagnostics_Sample Diagnostics::latest_sample() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (samples_.empty())
        return {};
    return samples_.back();
}

Particle_Inspection Diagnostics::selected_particle() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return selected_particle_;
}

void Diagnostics::clear_selection()
{
    std::lock_guard<std::mutex> lock(mutex_);
    selected_particle_ = Particle_Inspection{};
}

ecs::Entity Diagnostics::selected_particle_entity() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!selected_particle_.valid_selection)
        return ecs::invalid_entity;

    return selected_particle_.entity;
}

ecs::Entity Diagnostics::cfl_particle_entity() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (samples_.empty())
        return ecs::invalid_entity;

    Diagnostics_Sample const& sample = samples_.back();
    if (sample.cfl <= cfl_pause_threshold())
        return ecs::invalid_entity;

    return sample.cfl_particle_entity;
}

void Diagnostics::select_nearest_particle(World& world, Grid& grid, glm::mat4 const& view, glm::mat4 const& projection, int window_width, int window_height,
                                          double mouse_x, double mouse_y, float max_distance_px)
{
    auto& particles = world.get_array<Particle_Component>().data();
    if (particles.empty() || window_width <= 0 || window_height <= 0)
        return;

    float best_dist_sq = max_distance_px * max_distance_px;
    int best_index = -1;

    for (std::size_t i = 0; i < particles.size(); ++i)
    {
        glm::vec4 world_pos(static_cast<float>(particles[i].position.x), static_cast<float>(particles[i].position.y),
                            static_cast<float>(particles[i].position.z), 1.0f);
        glm::vec4 clip = projection * view * world_pos;
        if (clip.w <= 0.0f)
            continue;

        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f || ndc.z < -1.0f || ndc.z > 1.0f)
            continue;

        float screen_x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(window_width);
        float screen_y = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(window_height);
        float dx = screen_x - static_cast<float>(mouse_x);
        float dy = screen_y - static_cast<float>(mouse_y);
        float dist_sq = dx * dx + dy * dy;
        if (dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            best_index = static_cast<int>(i);
        }
    }

    if (best_index < 0)
        return;

    Diagnostics_Bounds bounds = bounds_from(world, grid);
    Particle_Inspection inspection = inspect_particle(world, static_cast<std::size_t>(best_index), bounds);
    std::lock_guard<std::mutex> lock(mutex_);
    selected_particle_ = inspection;
}

bool Diagnostics::should_pause_for(Diagnostics_Sample const& sample) const
{
    if (pause_on_invalid() && sample.invalid_count > 0)
        return true;

    if (pause_on_cfl() && sample.cfl > cfl_pause_threshold())
        return true;

    return false;
}

bool Diagnostics::export_csv(World& world, Grid& grid, double sim_time)
{
    std::filesystem::path out_dir = std::filesystem::path("outputs") /
                                    (local_time_string("%Y-%m-%d") + "-diagnostics") /
                                    local_time_string("%H%M%S");
    return export_csv_to(world, grid, sim_time, out_dir.string());
}

bool Diagnostics::export_csv_to(World& world, Grid& grid, double sim_time, std::string const& out_dir)
{
    std::vector<Diagnostics_Sample> copy = samples();
    if (copy.empty())
    {
        std::lock_guard<std::mutex> lock(mutex_);
        export_status_ = "No diagnostics samples to export.";
        return false;
    }

    std::filesystem::path out_path = out_dir;
    if (out_path.empty())
    {
        std::lock_guard<std::mutex> lock(mutex_);
        export_status_ = "No diagnostics output directory provided.";
        return false;
    }

    std::filesystem::create_directories(out_path);

    std::ofstream csv(out_path / "diagnostics.csv");
    csv << "step,time,dt,solver_substeps,frame_dt,adaptive_timestep,target_dt,particle_count,max_speed,mean_speed,rms_speed,total_mass,"
           "fluid_measure_dimension,fluid_cell_count,fluid_area_estimate,fluid_volume_estimate,"
           "kinetic_energy,potential_energy,total_energy,velocity_delta_rms,ringing_index,velocity_reversal_fraction,ringing_particle_count,"
           "strong_reversal_count,active_ringing_candidate_count,strong_reversal_fraction,"
           "oscillating_particle_count,oscillating_particle_fraction,oscillation_velocity_delta_rms,"
           "alternating_acceleration_count,active_acceleration_candidate_count,alternating_acceleration_fraction,"
           "strict_ringing_particle_count,strict_ringing_particle_fraction,strict_ringing_velocity_delta_rms,"
           "stage_before_step_particle_energy,stage_before_step_particle_kinetic_energy,"
           "stage_before_step_particle_angular_momentum_x,stage_before_step_particle_angular_momentum_y,"
           "stage_before_step_particle_angular_momentum_z,"
           "stage_after_p2g_max_grid_speed,stage_after_p2g_grid_kinetic_energy,stage_after_p2g_grid_angular_momentum_x,"
           "stage_after_p2g_grid_angular_momentum_y,stage_after_p2g_grid_angular_momentum_z,"
           "stage_after_forces_max_grid_speed,stage_after_forces_grid_kinetic_energy,"
           "stage_after_first_extrapolate_max_grid_speed,stage_after_first_extrapolate_grid_kinetic_energy,"
           "stage_after_boundary_max_grid_speed,stage_after_boundary_grid_kinetic_energy,"
           "stage_before_projection_max_divergence,stage_before_projection_rms_divergence,"
           "projection_fluid_cell_count,projection_iterations,projection_converged,projection_initial_residual,projection_final_residual,"
           "projection_residual_ratio,pressure_min,pressure_max,pressure_mean,max_pressure_gradient,max_projection_velocity_delta,"
           "stage_after_projection_max_grid_speed,stage_after_projection_grid_kinetic_energy,stage_after_projection_grid_angular_momentum_x,"
           "stage_after_projection_grid_angular_momentum_y,stage_after_projection_grid_angular_momentum_z,"
           "stage_after_projection_max_divergence,stage_after_projection_rms_divergence,"
           "stage_after_second_extrapolate_max_grid_speed,stage_after_second_extrapolate_grid_kinetic_energy,"
           "stage_after_second_extrapolate_grid_angular_momentum_x,stage_after_second_extrapolate_grid_angular_momentum_y,"
           "stage_after_second_extrapolate_grid_angular_momentum_z,"
           "stage_after_g2p_max_particle_speed,stage_after_g2p_particle_energy,stage_after_g2p_particle_kinetic_energy,"
           "stage_after_g2p_particle_angular_momentum_x,"
           "stage_after_g2p_particle_angular_momentum_y,stage_after_g2p_particle_angular_momentum_z,"
           "stage_after_advect_max_particle_speed,stage_after_advect_particle_energy,stage_after_advect_particle_kinetic_energy,"
           "stage_after_advect_particle_angular_momentum_x,"
           "stage_after_advect_particle_angular_momentum_y,stage_after_advect_particle_angular_momentum_z,"
           "stage_after_advect_grid_residual_rms,stage_after_advect_grid_residual_energy_per_mass,stage_after_advect_grid_residual_energy_fraction,"
           "stage_after_advect_grid_residual_delta_rms,stage_after_advect_grid_residual_delta_energy_per_mass,"
           "stage_after_advect_grid_residual_delta_energy_fraction,stage_after_advect_grid_residual_reversal_fraction,"
           "stage_after_advect_grid_residual_reversal_energy_per_mass,stage_after_advect_grid_residual_reversal_energy_fraction,"
           "center_of_mass_x,center_of_mass_y,center_of_mass_z,total_momentum_x,total_momentum_y,total_momentum_z,"
           "orbital_angular_momentum_x,orbital_angular_momentum_y,orbital_angular_momentum_z,"
           "represented_angular_momentum_x,represented_angular_momentum_y,represented_angular_momentum_z,"
           "core_angular_momentum_x,core_angular_momentum_y,core_angular_momentum_z,core_radius,"
           "cfl,cfl_particle_index,cfl_particle_entity,"
           "cfl_particle_position_x,cfl_particle_position_y,cfl_particle_position_z,cfl_particle_velocity_x,cfl_particle_velocity_y,cfl_particle_velocity_z,"
           "cfl_particle_near_boundary,cfl_particle_boundary_distance,"
           "cfl_particle_boundary_collision,cfl_boundary_cell_x,cfl_boundary_cell_y,cfl_boundary_cell_z,"
           "cfl_boundary_normal_x,cfl_boundary_normal_y,cfl_boundary_normal_z,"
           "cfl_boundary_velocity_before_x,cfl_boundary_velocity_before_y,cfl_boundary_velocity_before_z,"
           "cfl_boundary_velocity_after_x,cfl_boundary_velocity_after_y,cfl_boundary_velocity_after_z,"
           "cfl_boundary_speed_before,cfl_boundary_speed_after,cfl_boundary_normal_velocity_before,cfl_boundary_normal_velocity_after,"
           "cfl_boundary_impulse,invalid_count,out_of_bounds_count,boundary_collision_count,max_poly_coeff\n";
    for (auto const& sample : copy)
    {
        csv << sample.step << ','
            << sample.time << ','
            << sample.dt << ','
            << sample.solver_substeps << ','
            << sample.frame_dt << ','
            << sample.adaptive_timestep << ','
            << sample.target_dt << ','
            << sample.particle_count << ','
            << sample.max_speed << ','
            << sample.mean_speed << ','
            << sample.rms_speed << ','
            << sample.total_mass << ','
            << sample.fluid_measure_dimension << ','
            << sample.fluid_cell_count << ','
            << sample.fluid_area_estimate << ','
            << sample.fluid_volume_estimate << ','
            << sample.kinetic_energy << ','
            << sample.potential_energy << ','
            << sample.total_energy << ','
            << sample.velocity_delta_rms << ','
            << sample.ringing_index << ','
            << sample.velocity_reversal_fraction << ','
            << sample.ringing_particle_count << ','
            << sample.strong_reversal_count << ','
            << sample.active_ringing_candidate_count << ','
            << sample.strong_reversal_fraction << ','
            << sample.oscillating_particle_count << ','
            << sample.oscillating_particle_fraction << ','
            << sample.oscillation_velocity_delta_rms << ','
            << sample.alternating_acceleration_count << ','
            << sample.active_acceleration_candidate_count << ','
            << sample.alternating_acceleration_fraction << ','
            << sample.strict_ringing_particle_count << ','
            << sample.strict_ringing_particle_fraction << ','
            << sample.strict_ringing_velocity_delta_rms << ','
            << sample.stages.before_step_particle_energy << ','
            << sample.stages.before_step_particle_kinetic_energy << ','
            << sample.stages.before_step_particle_angular_momentum.x << ','
            << sample.stages.before_step_particle_angular_momentum.y << ','
            << sample.stages.before_step_particle_angular_momentum.z << ','
            << sample.stages.after_particle_to_grid_max_grid_speed << ','
            << sample.stages.after_particle_to_grid_grid_kinetic_energy << ','
            << sample.stages.after_particle_to_grid_grid_angular_momentum.x << ','
            << sample.stages.after_particle_to_grid_grid_angular_momentum.y << ','
            << sample.stages.after_particle_to_grid_grid_angular_momentum.z << ','
            << sample.stages.after_forces_max_grid_speed << ','
            << sample.stages.after_forces_grid_kinetic_energy << ','
            << sample.stages.after_first_extrapolate_max_grid_speed << ','
            << sample.stages.after_first_extrapolate_grid_kinetic_energy << ','
            << sample.stages.after_boundary_max_grid_speed << ','
            << sample.stages.after_boundary_grid_kinetic_energy << ','
            << sample.stages.before_projection_max_divergence << ','
            << sample.stages.before_projection_rms_divergence << ','
            << sample.stages.projection.fluid_cell_count << ','
            << sample.stages.projection.iterations << ','
            << sample.stages.projection.converged << ','
            << sample.stages.projection.initial_residual << ','
            << sample.stages.projection.final_residual << ','
            << sample.stages.projection.residual_ratio << ','
            << sample.stages.projection.pressure_min << ','
            << sample.stages.projection.pressure_max << ','
            << sample.stages.projection.pressure_mean << ','
            << sample.stages.projection.max_pressure_gradient << ','
            << sample.stages.projection.max_velocity_delta << ','
            << sample.stages.after_projection_max_grid_speed << ','
            << sample.stages.after_projection_grid_kinetic_energy << ','
            << sample.stages.after_projection_grid_angular_momentum.x << ','
            << sample.stages.after_projection_grid_angular_momentum.y << ','
            << sample.stages.after_projection_grid_angular_momentum.z << ','
            << sample.stages.after_projection_max_divergence << ','
            << sample.stages.after_projection_rms_divergence << ','
            << sample.stages.after_second_extrapolate_max_grid_speed << ','
            << sample.stages.after_second_extrapolate_grid_kinetic_energy << ','
            << sample.stages.after_second_extrapolate_grid_angular_momentum.x << ','
            << sample.stages.after_second_extrapolate_grid_angular_momentum.y << ','
            << sample.stages.after_second_extrapolate_grid_angular_momentum.z << ','
            << sample.stages.after_grid_to_particle_max_particle_speed << ','
            << sample.stages.after_grid_to_particle_particle_energy << ','
            << sample.stages.after_grid_to_particle_particle_kinetic_energy << ','
            << sample.stages.after_grid_to_particle_particle_angular_momentum.x << ','
            << sample.stages.after_grid_to_particle_particle_angular_momentum.y << ','
            << sample.stages.after_grid_to_particle_particle_angular_momentum.z << ','
            << sample.stages.after_advect_max_particle_speed << ','
            << sample.stages.after_advect_particle_energy << ','
            << sample.stages.after_advect_particle_kinetic_energy << ','
            << sample.stages.after_advect_particle_angular_momentum.x << ','
            << sample.stages.after_advect_particle_angular_momentum.y << ','
            << sample.stages.after_advect_particle_angular_momentum.z << ','
            << sample.stages.after_advect_grid_residual_rms << ','
            << sample.stages.after_advect_grid_residual_energy_per_mass << ','
            << sample.stages.after_advect_grid_residual_energy_fraction << ','
            << sample.stages.after_advect_grid_residual_delta_rms << ','
            << sample.stages.after_advect_grid_residual_delta_energy_per_mass << ','
            << sample.stages.after_advect_grid_residual_delta_energy_fraction << ','
            << sample.stages.after_advect_grid_residual_reversal_fraction << ','
            << sample.stages.after_advect_grid_residual_reversal_energy_per_mass << ','
            << sample.stages.after_advect_grid_residual_reversal_energy_fraction << ','
            << sample.center_of_mass.x << ','
            << sample.center_of_mass.y << ','
            << sample.center_of_mass.z << ','
            << sample.total_momentum.x << ','
            << sample.total_momentum.y << ','
            << sample.total_momentum.z << ','
            << sample.orbital_angular_momentum.x << ','
            << sample.orbital_angular_momentum.y << ','
            << sample.orbital_angular_momentum.z << ','
            << sample.represented_angular_momentum.x << ','
            << sample.represented_angular_momentum.y << ','
            << sample.represented_angular_momentum.z << ','
            << sample.core_angular_momentum.x << ','
            << sample.core_angular_momentum.y << ','
            << sample.core_angular_momentum.z << ','
            << sample.core_radius << ','
            << sample.cfl << ','
            << sample.cfl_particle_index << ','
            << sample.cfl_particle_entity << ','
            << sample.cfl_particle_position.x << ','
            << sample.cfl_particle_position.y << ','
            << sample.cfl_particle_position.z << ','
            << sample.cfl_particle_velocity.x << ','
            << sample.cfl_particle_velocity.y << ','
            << sample.cfl_particle_velocity.z << ','
            << sample.cfl_particle_near_boundary << ','
            << sample.cfl_particle_boundary_distance << ','
            << sample.cfl_particle_boundary_collision << ','
            << sample.cfl_boundary_cell.x << ','
            << sample.cfl_boundary_cell.y << ','
            << sample.cfl_boundary_cell.z << ','
            << sample.cfl_boundary_normal.x << ','
            << sample.cfl_boundary_normal.y << ','
            << sample.cfl_boundary_normal.z << ','
            << sample.cfl_boundary_velocity_before.x << ','
            << sample.cfl_boundary_velocity_before.y << ','
            << sample.cfl_boundary_velocity_before.z << ','
            << sample.cfl_boundary_velocity_after.x << ','
            << sample.cfl_boundary_velocity_after.y << ','
            << sample.cfl_boundary_velocity_after.z << ','
            << sample.cfl_boundary_speed_before << ','
            << sample.cfl_boundary_speed_after << ','
            << sample.cfl_boundary_normal_velocity_before << ','
            << sample.cfl_boundary_normal_velocity_after << ','
            << sample.cfl_boundary_impulse << ','
            << sample.invalid_count << ','
            << sample.out_of_bounds_count << ','
            << sample.boundary_collision_count << ','
            << sample.max_poly_coeff << '\n';
    }

    Diagnostics_Bounds bounds = bounds_from(world, grid);
    std::ofstream notes(out_path / "notes.txt");
    notes << "Diagnostics export\n";
    notes << "sim_time=" << sim_time << "\n";
    notes << "solver=" << (world.settings().MPM ? "MPM" : "APIC") << "\n";
    notes << "dimension_mode=" << world.settings().DIMENSION << "\n";
    notes << "fluid_scene=" << world.settings().FLUID_SCENE << "\n";
    notes << "mpm_scene=" << world.settings().MPM_SCENE << "\n";
    notes << "fluid_solver=" << world.settings().FLUID_SOLVER << "\n";
    notes << "fluid_solver_name="
          << (world.settings().FLUID_SOLVER == POLYPIC ? "POLYPIC"
              : world.settings().FLUID_SOLVER == APIC  ? "APIC"
                                                       : "FLIP")
          << "\n";
    notes << "polypic_modes=" << world.settings().POLYPIC_MODES << "\n";
    notes << "flip_percent=" << world.settings().FLIP_PERCENT << "\n";
    notes << "adaptive_timestep=" << world.settings().ADAPTIVE_TIMESTEP.load(std::memory_order_relaxed) << "\n";
    notes << "target_dt=" << world.settings().SIMULATION_DT.load(std::memory_order_relaxed) << "\n";
    notes << "fluid_dx_setting=" << world.settings().FLUID_DX.load(std::memory_order_relaxed) << "\n";
    notes << "grid_width_setting=" << world.settings().GRID_WIDTH.load(std::memory_order_relaxed) << "\n";
    notes << "grid_height_setting=" << world.settings().GRID_HEIGHT.load(std::memory_order_relaxed) << "\n";
    notes << "fluid_jitter_seed=" << world.settings().FLUID_JITTER_SEED << "\n";
    notes << "ringing_speed_epsilon=" << ringing_speed_epsilon << "\n";
    notes << "ringing_delta_epsilon=" << ringing_delta_epsilon << "\n";
    notes << "ringing_velocity_floor=" << ringing_velocity_floor << "\n";
    notes << "ringing_oscillation_window_samples=" << Particle_Ringing_State::window_size << "\n";
    notes << "ringing_oscillation_min_reversals=" << ringing_oscillation_min_reversals << "\n";
    notes << "ringing_active_speed_floor=" << ringing_active_speed_floor << "\n";
    notes << "ringing_active_speed_max_fraction=" << ringing_active_speed_max_fraction << "\n";
    notes << "ringing_delta_floor=" << ringing_delta_floor << "\n";
    notes << "ringing_delta_max_fraction=" << ringing_delta_max_fraction << "\n";
    notes << "ringing_reversal_cosine_threshold=" << ringing_reversal_cosine_threshold << "\n";
    notes << "ringing_acceleration_reversal_cosine_threshold=" << ringing_acceleration_reversal_cosine_threshold << "\n";
    notes << "paused=" << world.settings().PAUSED.load() << "\n";
    notes << "grid_dx=" << bounds.dx << "\n";
    if (!copy.empty())
    {
        auto const& latest = copy.back();
        notes << "fluid_measure_dimension=" << latest.fluid_measure_dimension << "\n";
        notes << "fluid_cell_count=" << latest.fluid_cell_count << "\n";
        notes << "fluid_area_estimate=" << latest.fluid_area_estimate << "\n";
        notes << "fluid_volume_estimate=" << latest.fluid_volume_estimate << "\n";
        notes << "velocity_delta_rms=" << latest.velocity_delta_rms << "\n";
        notes << "ringing_index=" << latest.ringing_index << "\n";
        notes << "velocity_reversal_fraction=" << latest.velocity_reversal_fraction << "\n";
        notes << "strong_reversal_fraction=" << latest.strong_reversal_fraction << "\n";
        notes << "oscillating_particle_fraction=" << latest.oscillating_particle_fraction << "\n";
        notes << "oscillation_velocity_delta_rms=" << latest.oscillation_velocity_delta_rms << "\n";
        notes << "alternating_acceleration_fraction=" << latest.alternating_acceleration_fraction << "\n";
        notes << "strict_ringing_particle_fraction=" << latest.strict_ringing_particle_fraction << "\n";
        notes << "strict_ringing_velocity_delta_rms=" << latest.strict_ringing_velocity_delta_rms << "\n";
        notes << "stage_after_advect_grid_residual_rms=" << latest.stages.after_advect_grid_residual_rms << "\n";
        notes << "stage_after_advect_grid_residual_energy_per_mass=" << latest.stages.after_advect_grid_residual_energy_per_mass << "\n";
        notes << "stage_after_advect_grid_residual_delta_energy_per_mass=" << latest.stages.after_advect_grid_residual_delta_energy_per_mass << "\n";
        notes << "stage_after_advect_grid_residual_reversal_energy_per_mass="
              << latest.stages.after_advect_grid_residual_reversal_energy_per_mass << "\n";
        notes << "max_poly_coeff=" << latest.max_poly_coeff << "\n";
        notes << "orbital_angular_momentum=" << latest.orbital_angular_momentum.x << "," << latest.orbital_angular_momentum.y << ","
              << latest.orbital_angular_momentum.z << "\n";
        notes << "represented_angular_momentum=" << latest.represented_angular_momentum.x << "," << latest.represented_angular_momentum.y << ","
              << latest.represented_angular_momentum.z << "\n";
        if (latest.fluid_measure_dimension == 2)
            notes << "fluid_volume_note=2D unit-depth volume equals grid-cell area estimate\n";
    }
    notes << "bounds_lo=" << bounds.lo.x << "," << bounds.lo.y << "," << bounds.lo.z << "\n";
    notes << "bounds_hi=" << bounds.hi.x << "," << bounds.hi.y << "," << bounds.hi.z << "\n";
    notes << "sample_count=" << copy.size() << "\n";

    std::lock_guard<std::mutex> lock(mutex_);
    last_export_path_ = std::filesystem::absolute(out_path).string();
    export_status_ = "Exported diagnostics to " + last_export_path_;
    return true;
}

std::string Diagnostics::export_status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return export_status_;
}

std::string Diagnostics::last_export_path() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_export_path_;
}
