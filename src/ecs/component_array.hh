#pragma once

#include "ecs_common.hh"

#include <unordered_map>

template <typename T>
class Component_Array
{
public:
    void insert(ecs::Entity entity, T component) { components_[entity] = component; }

    T const* get(ecs::Entity entity) const
    {
        auto it = components_.find(entity);
        return it != components_.end() ? &it->second : nullptr;
    }
    T* get(ecs::Entity entity)
    {
        auto it = components_.find(entity);
        return it != components_.end() ? &it->second : nullptr;
    }

    void remove(ecs::Entity entity) { components_.erase(entity); }

    std::unordered_map<ecs::Entity, T>& all() { return components_; }

private:
    std::unordered_map<ecs::Entity, T> components_;
};
