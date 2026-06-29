#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>

struct Transform_Component
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    float rotation = 0.0f;
    glm::vec2 anchor = glm::vec2(0.f);

    glm::quat orientation = glm::quat(1.0, 0.0, 0.0, 0.0);

    float yaw = -90.0f;
    float pitch = 0.0f;
    float roll = 0.0f;

    glm::mat4 get_model_matrix() const
    {
        glm::mat4 model = glm::mat4(1.0f);

        model = glm::translate(model, position);

        model *= glm::toMat4(orientation);

        model = glm::scale(model, scale);

        return model;
    }

    glm::mat4 get_view_matrix() const
    {
        glm::mat4 rotate = glm::toMat4(glm::conjugate(orientation));
        glm::mat4 translate = glm::translate(glm::mat4(1.0f), -position);
        return rotate * translate;
    }

    void add_input_rotation(float delta_yaw, float delta_pitch)
    {
        yaw += delta_yaw;
        pitch += delta_pitch;

        pitch = std::clamp(pitch, -89.0f, 89.0f);

        update_quaternion();
    }

    void update_quaternion()
    {
        glm::quat q_yaw = glm::angleAxis(glm::radians(yaw), glm::vec3(0, 1, 0));
        glm::quat q_pitch = glm::angleAxis(glm::radians(pitch), glm::vec3(1, 0, 0));
        glm::quat q_roll = glm::angleAxis(glm::radians(roll), glm::vec3(0, 0, 1));

        orientation = q_yaw * q_pitch * q_roll;
    }

    void set_rotation(glm::vec3 euler_angles_degrees)
    {
        glm::vec3 radians = glm::radians(euler_angles_degrees);
        orientation = glm::quat(radians);
    }

    void rotate(float angle_degrees, glm::vec3 axis)
    {
        orientation = glm::angleAxis(glm::radians(angle_degrees), glm::normalize(axis)) * orientation;
    }

    glm::vec3 forward() const { return glm::rotate(orientation, glm::vec3(0.0f, 0.0f, -1.0f)); }

    glm::vec3 right() const { return glm::rotate(orientation, glm::vec3(1.0f, 0.0f, 0.0f)); }

    glm::vec3 up() const { return glm::rotate(orientation, glm::vec3(0.0f, 1.0f, 0.0f)); }
};
