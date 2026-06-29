#pragma once

#include "../texture_2d.hh"

#include <glm/glm.hpp>

struct Sprite_Component
{
    Texture_2D texture;
    glm::vec3 color = glm::vec3(1.0f);
};
