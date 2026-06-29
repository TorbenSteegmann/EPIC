#pragma once

#include "ecs/box_component.hh"
#include "ecs/collision_component.hh"
#include "ecs/packed_array.hh"
#include "ecs/particle_component.hh"
#include "ecs/physics_component.hh"
#include "ecs/sprite_component.hh"
#include "ecs/transform_component.hh"

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

struct Boundary_Collision_Event
{
    ecs::Entity entity = ecs::invalid_entity;
    glm::dvec3 position_before{0.0};
    glm::dvec3 position_after{0.0};
    glm::dvec3 velocity_before{0.0};
    glm::dvec3 velocity_after{0.0};
    glm::dvec3 normal{0.0};
    glm::ivec3 solid_cell{0};
    int substep = 0;
};

enum Fluid_Scenes
{
    DAM_BREAK = 0,
    SOLID_BLOCK = 1,
    FULL_GRID = 2,
    CONSTANT_STREAM = 3,
    TAYLOR_GREEN_VORTEX = 4,
    CONFINED_VORTEX = 5,
    SETTLING_POOL = 6,
    SETTLING_POOL_OBSTACLE = 7
};

enum Fluid_Solvers
{
    FLIP = 0,
    APIC = 1,
    POLYPIC = 2
};

enum MPM_Scenes
{
    MPM_EMPTY = 0,
    MPM_COMPRESSED_SQUARE = 1
};

struct Settings
{
    float FLIP_PERCENT = 0.95f;
    std::atomic<bool> ADAPTIVE_TIMESTEP{false};
    std::atomic<double> SIMULATION_DT{0.1};
    std::atomic<double> FLUID_DX{1.0};
    std::atomic<bool> PAUSED{true};
    std::atomic<bool> RESET{false};
    bool MPM = false;
    int DIMENSION = 2;
    int FLUID_SOLVER = FLIP;
    int POLYPIC_MODES = 4; // active PolyPIC mode count Nr; fluid 2D max 4, fluid 3D max 8, MPM 2D max 9
    double MPM_POLYPIC_QUAD_REG = 0.02; // == MPM_POLYPIC_DEFAULT_QUAD_REG; conditioning guard for MPM quadratic modes; 0 = off (lossless/unstable)
    int FLUID_SCENE = DAM_BREAK;
    int MPM_SCENE = MPM_EMPTY;
    bool RENDER_PARTICLE_MESH_2D = false;
    bool CREATE_BOUNDARY_VISUALS = true;
    std::pair<int, int> WINDOW_DIMENSIONS = {1280, 720};
    std::atomic<bool> APPLY_GRAVITY{true};
    std::atomic<bool> APPLY_TOP_FORCE{false};
    std::atomic<double> TOP_FORCE_MIN_Y{-1.0};
    std::atomic<double> TOP_FORCE_MAX_Y{-1.0};
    std::atomic<double> TOP_FORCE_ACCELERATION{20.0};
    std::atomic<double> TOP_FORCE_MAX_SPEED{35.0};
    std::atomic<double> GRID_WIDTH{100.0};
    std::atomic<double> GRID_HEIGHT{100.0};
    std::uint32_t FLUID_JITTER_SEED = 0;
    bool CANONICAL_APIC{false};
};

class World
{
public:
    World(){};

    Settings& settings() { return settings_; }

    ecs::Entity create_entity(std::string name = "", ecs::Registry_Type registry = ecs::Registry_Type::Local);
    ecs::Entity get_entity(std::string name);

    template <typename T>
    void add_component(ecs::Entity entity, T component);

    template <typename T>
    T const* get_component(ecs::Entity entity) const;
    template <typename T>
    T* get_component(ecs::Entity entity);

    template <typename T>
    Packed_Array<T> const& get_array() const;
    template <typename T>
    Packed_Array<T>& get_array();

    void record_boundary_collision(Boundary_Collision_Event event);
    std::vector<Boundary_Collision_Event> const& boundary_collisions() const { return boundary_collisions_; }
    void clear_boundary_collisions();

    void clear();

private:
    Settings settings_;
    ecs::Entity next_entity_id_ = 1;
    std::unordered_set<ecs::Entity> entities_;
    std::unordered_map<std::string, ecs::Entity> named_entities_;
    std::unordered_map<ecs::Entity, ecs::Registry_Type> entity_registry_;

    Packed_Array<Transform_Component> transform_components_;
    Packed_Array<Sprite_Component> sprite_components_;
    Packed_Array<Physics_Component> physics_components_;
    Packed_Array<Particle_Component> particle_components_;
    Packed_Array<Mass_Component> mass_components_;
    Packed_Array<Deformable_Component> deformable_components_;
    Packed_Array<Collision_Component> collision_components_;
    Packed_Array<Box_Component> box_components_;
    std::vector<Boundary_Collision_Event> boundary_collisions_;
};

template <typename T>
void World::add_component(ecs::Entity entity, T component)
{
    get_array<T>().insert(entity, component);
}

template <typename T>
T const* World::get_component(ecs::Entity entity) const
{
    return get_array<T>().get(entity);
}
template <typename T>
T* World::get_component(ecs::Entity entity)
{
    return get_array<T>().get(entity);
}

template <>
inline Packed_Array<Transform_Component> const& World::get_array() const
{
    return transform_components_;
}

template <>
inline Packed_Array<Sprite_Component> const& World::get_array() const
{
    return sprite_components_;
}

template <>
inline Packed_Array<Physics_Component> const& World::get_array() const
{
    return physics_components_;
}

template <>
inline Packed_Array<Particle_Component> const& World::get_array() const
{
    return particle_components_;
}

template <>
inline Packed_Array<Transform_Component>& World::get_array()
{
    return transform_components_;
}

template <>
inline Packed_Array<Sprite_Component>& World::get_array()
{
    return sprite_components_;
}

template <>
inline Packed_Array<Physics_Component>& World::get_array()
{
    return physics_components_;
}

template <>
inline Packed_Array<Particle_Component>& World::get_array()
{
    return particle_components_;
}

template <>
inline Packed_Array<Mass_Component>& World::get_array()
{
    return mass_components_;
}

template <>
inline Packed_Array<Deformable_Component>& World::get_array()
{
    return deformable_components_;
}

template <>
inline Packed_Array<Collision_Component>& World::get_array()
{
    return collision_components_;
}

template <>
inline Packed_Array<Box_Component> const& World::get_array() const
{
    return box_components_;
}

template <>
inline Packed_Array<Box_Component>& World::get_array()
{
    return box_components_;
}
