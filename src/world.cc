#include "world.hh"

ecs::Entity World::create_entity(std::string name, ecs::Registry_Type registry)
{
    ecs::Entity id = next_entity_id_++;
    entities_.insert(id);

    if (name != "")
        named_entities_.insert({name, id});

    entity_registry_[id] = registry;
    return id;
}

ecs::Entity World::get_entity(std::string name)
{
    if (named_entities_.find(name) != named_entities_.end())
        return named_entities_.at(name);
    else
        return -1;
}

void World::record_boundary_collision(Boundary_Collision_Event event)
{
    boundary_collisions_.push_back(event);
}

void World::clear_boundary_collisions()
{
    boundary_collisions_.clear();
}

void World::clear()
{
    // save camera
    auto camera = get_entity("Camera");
    Transform_Component camera_transform = *get_component<Transform_Component>(camera);

    entities_.clear();
    named_entities_.clear();
    entity_registry_.clear();

    transform_components_.clear();
    sprite_components_.clear();
    physics_components_.clear();
    particle_components_.clear();
    boundary_collisions_.clear();

    next_entity_id_ = 1;

    // read camera
    create_entity("Camera");
    camera = get_entity("Camera");
    add_component(camera, camera_transform);
}
