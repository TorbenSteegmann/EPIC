#pragma once

#include <cmath>

#include "ecs/ecs_common.hh"
#include "window.hh"
#include "world.hh"

#include <glm/gtc/matrix_transform.hpp>

struct Framed_Camera_3D
{
    glm::vec3 position{0.0f};
    glm::vec3 target{0.0f};
    float fov = 45.0f;
};

inline Framed_Camera_3D frame_box_for_dam_break(Box_Component const& box)
{
    Framed_Camera_3D camera;
    camera.target = box.offset + box.size * 0.5f;
    camera.target.y = box.offset.y + box.size.y * 0.3f;
    camera.position = glm::vec3(190.0f, 93.0f, 175.0f);
    return camera;
}

template <int Dimension>
class Camera
{
public:
    float zoom = 1.f;
    float fov = 90.f;

    Camera(World& world, ecs::Entity const& player_entity, Window& window) : world_(world), player_entity_(player_entity), window_(window)
    {
        camera_entity_ = world.get_entity("Camera");
        auto* transform = world_.get_component<Transform_Component>(camera_entity_);
        if (transform)
        {
            if constexpr (Dimension == 3)
            {
                auto& boxes = world.get_array<Box_Component>();
                if (boxes.data().empty())
                {
                    return;
                }

                // Frame the largest box (normally the fluid domain) from its
                // positive-X side. Dam-break motion starts toward +X, so this
                // makes the initial surge move toward the camera.
                Box_Component const* framing_box = &boxes.data().front();
                float largest_diagonal = 0.0f;
                for (auto& b : boxes.data())
                {
                    float const diagonal = glm::length(b.size);
                    if (diagonal > largest_diagonal)
                    {
                        largest_diagonal = diagonal;
                        framing_box = &b;
                    }
                }

                Framed_Camera_3D const framed = frame_box_for_dam_break(*framing_box);
                fov = framed.fov;
                glm::vec3 const target = framed.target;
                transform->position = framed.position;

                glm::vec3 dir = glm::normalize(target - transform->position);

                // yaw: angle around Y from -Z axis. atan2(-x, -z) gives yaw=0 at -Z.
                transform->yaw = glm::degrees(std::atan2(-dir.x, -dir.z));
                transform->pitch = glm::degrees(std::asin(dir.y));
            }
            else
            {
                transform->position = glm::vec3(50.f, 50.f, 30.f);
                transform->yaw = 0.f;
                transform->pitch = 0.f;
            }
            transform->update_quaternion();

            position_ = transform->position;
            forward_ = transform->forward();
            up_ = transform->up();
            zoom = window_.scroll_factor;
            update_view_matrix();
            update_projection_matrix();
        }
    }

    glm::mat4 const& view_matrix() const { return view_; }
    glm::mat4 const& projection_matrix() const { return projection_; }
    glm::vec3 const& position() const { return position_; }
    glm::vec3 const& forward() const { return forward_; }
    glm::vec3 const& up() const { return up_; }

    void tick()
    {
        auto* cam_transform = world_.get_component<Transform_Component>(camera_entity_);

        position_ = cam_transform->position;
        forward_ = cam_transform->forward();
        up_ = cam_transform->up();

        zoom = window_.scroll_factor;

        update_view_matrix();
        update_projection_matrix();
    }

private:
    void update_view_matrix() { view_ = glm::lookAt(position_, position_ + forward_, up_); }

    void update_projection_matrix()
    {
        if constexpr (Dimension == 3)
        {
            float aspect = static_cast<float>(window_.BASE_WIDTH) / static_cast<float>(window_.BASE_HEIGHT);
            projection_ = glm::perspective(glm::radians(fov), aspect, 0.1f, 2000.0f);
        }
        else
        {
            float hw = static_cast<float>(window_.BASE_WIDTH) * 0.5f;
            float hh = static_cast<float>(window_.BASE_HEIGHT) * 0.5f;
            projection_ = glm::ortho(-hw / zoom, hw / zoom, -hh / zoom, hh / zoom, -100.0f, 100.0f);
        }
    }

    World& world_;
    ecs::Entity camera_entity_;
    ecs::Entity const& player_entity_;
    Window& window_;
    glm::vec3 position_;
    glm::mat4 view_{1.0f};
    glm::mat4 projection_{1.0f};

    glm::vec3 forward_ = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up_ = glm::vec3(0.0f, 1.0f, 0.0f);
};
