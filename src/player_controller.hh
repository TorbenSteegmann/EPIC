#pragma once

#include "ecs/ecs_common.hh"
#include "game_state.hh"
#include "glm/glm.hpp"
#include "window.hh"
#include "world.hh"

#include <glad/glad.h>
// glad needs to be included before GLFW
#include <GLFW/glfw3.h>

template <int Dimension>
class Player_Controller
{
public:
    bool first_mouse = true;
    bool mouse_captured_ = false;
    float last_mouse_x = 0.0f;
    float last_mouse_y = 0.0f;

    ecs::Entity const& player_entity_;
    Game_State& game_state;
    Window& window;

    float last_time;
    glm::vec2 curser_position;

    Player_Controller(ecs::Entity const& player_entity, Game_State& game_state, Window& window)
      : player_entity_(player_entity), game_state(game_state), window(window), curser_position(0.f)
    {
        last_time = static_cast<float>(glfwGetTime());
        double mouse_x = 0., mouse_y = 0.;
        glfwGetCursorPos(window.glfw_window, &mouse_x, &mouse_y);
        last_mouse_x = static_cast<float>(mouse_x);
        last_mouse_y = static_cast<float>(mouse_y);
    }

    void tick(World& world);
    void handle_mouse_look(World& world);

    bool get_key(unsigned int key) { return glfwGetKey(window.glfw_window, key) == GLFW_PRESS; }
    bool get_mouse_button(int button) { return glfwGetMouseButton(window.glfw_window, button) == GLFW_PRESS; }
};

template <int Dimension>
void Player_Controller<Dimension>::tick(World& world)
{
    if (get_key(GLFW_KEY_ESCAPE))
    {
        glfwSetWindowShouldClose(window.glfw_window, true);
        return;
    }

    // Mouse polls
    double mouse_x = 0.;
    double mouse_y = 0.;
    glfwGetCursorPos(window.glfw_window, &mouse_x, &mouse_y);
    this->curser_position = {static_cast<float>(mouse_x), static_cast<float>(mouse_y)};

    // Delta time
    auto current_time = static_cast<float>(glfwGetTime());
    auto dt = current_time - last_time;
    last_time = current_time;

    auto camera_entity = world.get_entity("Camera");
    auto* cam_transform = world.get_component<Transform_Component>(camera_entity);

    // Toggle pause (P in both modes)
    static bool p_pressed = false;
    if (get_key(GLFW_KEY_P))
    {
        if (!p_pressed)
        {
            p_pressed = true;
            world.settings().PAUSED = !world.settings().PAUSED;
        }
    }
    else
    {
        p_pressed = false;
    }

    // Toggle reset
    static bool r_pressed = false;
    if (get_key(GLFW_KEY_R))
    {
        if (!r_pressed)
        {
            r_pressed = true;
            world.settings().RESET = true;
        }
    }
    else
    {
        r_pressed = false;
    }

    // Movement
    float speed = 30.0f * dt;

    if constexpr (Dimension == 2)
    {
        if (get_key(GLFW_KEY_A)) cam_transform->position.x -= speed;
        if (get_key(GLFW_KEY_D)) cam_transform->position.x += speed;
        if (get_key(GLFW_KEY_W)) cam_transform->position.y += speed;
        if (get_key(GLFW_KEY_S)) cam_transform->position.y -= speed;
    }
    else
    {
        handle_mouse_look(world);

        if (get_key(GLFW_KEY_LEFT_SHIFT) || get_key(GLFW_KEY_RIGHT_SHIFT))
            speed *= 3.0f;

        glm::vec3 fwd = cam_transform->forward();
        glm::vec3 right = cam_transform->right();

        if (get_key(GLFW_KEY_W)) cam_transform->position += fwd * speed;
        if (get_key(GLFW_KEY_S)) cam_transform->position -= fwd * speed;
        if (get_key(GLFW_KEY_A)) cam_transform->position -= right * speed;
        if (get_key(GLFW_KEY_D)) cam_transform->position += right * speed;
        if (get_key(GLFW_KEY_SPACE))                                                      cam_transform->position.y += speed;
        if (get_key(GLFW_KEY_C) || get_key(GLFW_KEY_LEFT_CONTROL) || get_key(GLFW_KEY_RIGHT_CONTROL)) cam_transform->position.y -= speed;
    }
};

template <int Dimension>
void Player_Controller<Dimension>::handle_mouse_look(World& world)
{
    double xpos, ypos;
    glfwGetCursorPos(window.glfw_window, &xpos, &ypos);

    float xoffset = static_cast<float>(xpos - last_mouse_x);
    float yoffset = static_cast<float>(last_mouse_y - ypos);

    last_mouse_x = static_cast<float>(xpos);
    last_mouse_y = static_cast<float>(ypos);

    if (!get_mouse_button(GLFW_MOUSE_BUTTON_RIGHT))
    {
        if (mouse_captured_)
        {
            glfwSetInputMode(window.glfw_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            mouse_captured_ = false;
        }
        return;
    }
    if (!mouse_captured_)
    {
        glfwSetInputMode(window.glfw_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        mouse_captured_ = true;
    }

    auto camera = world.get_entity("Camera");
    auto* transform = world.get_component<Transform_Component>(camera);

    float sensitivity = 0.1f;
    transform->add_input_rotation(-xoffset * sensitivity, yoffset * sensitivity);
}
