#pragma once

#include <glm/glm.hpp>

struct Physics_Component
{
    glm::vec2 velocity = glm::vec2(0.0f);
    glm::vec2 forces = glm::vec2(0.0f);
    bool is_dynamic = true;
    bool is_jumping = false;
};
