#pragma once

#include "ecs_common.hh"

#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

template <typename T>
class Packed_Array
{
public:
    void insert(ecs::Entity e, T component)
    {
        size_t idx = data_.size();
        data_.push_back(std::move(component));
        entities_.push_back(e);
        index_map_[e] = idx;
    }

    T* get(ecs::Entity e)
    {
        auto it = index_map_.find(e);
        return (it != index_map_.end()) ? &data_[it->second] : nullptr;
    }
    T const* get(ecs::Entity e) const
    {
        auto it = index_map_.find(e);
        return (it != index_map_.end()) ? &data_[it->second] : nullptr;
    }

    void remove(ecs::Entity e)
    {
        auto it = index_map_.find(e);
        if (it == index_map_.end())
            return;

        size_t idx = it->second;
        size_t last = data_.size() - 1;

        if (idx != last)
        {
            data_[idx] = std::move(data_[last]);
            entities_[idx] = entities_[last];
            index_map_[entities_[idx]] = idx;
        }

        data_.pop_back();
        entities_.pop_back();
        index_map_.erase(e);
    }

    void clear()
    {
        data_.clear();
        entities_.clear();
        index_map_.clear();
    }

    std::vector<T>& data() { return data_; }
    std::vector<ecs::Entity>& entities() { return entities_; }

private:
    std::vector<T> data_;
    std::vector<ecs::Entity> entities_;
    std::unordered_map<ecs::Entity, size_t> index_map_;
};
