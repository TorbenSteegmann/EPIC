#pragma once

#include "diagnostics.hh"
#include "shader_program.hh"
#include "texture_2d.hh"
#include "world.hh"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

template <int Dimension>
class Camera;

struct Particle_Render_Instance
{
    glm::vec3 position{0.0f};
    float radius = 1.0f;
    glm::vec3 velocity{0.0f};
    float highlight = 0.0f;
};

template <int Dimension>
class Renderer
{
public:
    Renderer(unsigned int fb_width, unsigned int fb_height);
    ~Renderer();

#ifndef FLUID_REPLAY_ONLY
    void tick(World& world, Camera<Dimension> const& camera, Diagnostics const& diagnostics);
#endif
    void tick_replay(World& world, std::vector<Particle_Render_Instance> const& particles,
                     glm::vec3 const& camera_position, glm::mat4 const& view, glm::mat4 const& projection);

private:
    // Shaders
    Shader_Program sprite_shader_;
    Shader_Program particle_shader_;
    Shader_Program particle_mesh_shader_;
    Shader_Program box_shader_;
    Shader_Program sky_shader_;
    Shader_Program floor_shader_;

    // VAOs & VBOs
    unsigned int sprite_VAO_        = 0;
    unsigned int particle_VAO_      = 0;
    unsigned int sprite_VBO_        = 0;
    unsigned int particle_VBO_      = 0;
    unsigned int instance_VBO_      = 0;
    unsigned int particle_mesh_VAO_ = 0;
    unsigned int particle_mesh_VBO_ = 0;
    unsigned int box_VAO_           = 0;
    unsigned int box_VBO_           = 0;
    unsigned int sky_VAO_           = 0;
    unsigned int sky_VBO_           = 0;

    void initRenderData();

    std::vector<Particle_Render_Instance> cached_gpu_data_;

    void apply_camera_uniforms(glm::mat4 const& view, glm::mat4 const& projection);
    void begin_2d_scene_pass();
    void begin_3d_scene_pass();
    void end_3d_scene_pass();
    void begin_3d_particle_pass();
    void end_3d_particle_pass();
    void draw_scene_2d(World& world, ecs::Entity selected_entity, ecs::Entity cfl_entity);
    void draw_scene_3d(World& world, ecs::Entity selected_entity, ecs::Entity cfl_entity,
                       glm::vec3 const& cam_pos, glm::mat4 const& view, glm::mat4 const& proj);
    void draw_sprite(Texture_2D& texture, glm::vec2 position, glm::vec2 size, float rotate, glm::vec3 color);
    void snapshot_particles(World& world, ecs::Entity selected_entity, ecs::Entity cfl_entity);
    void draw_sprites(World& world);
    void draw_particles(World& world, ecs::Entity selected_entity, ecs::Entity cfl_entity);
    void draw_particle_instances(std::vector<Particle_Render_Instance> const& particles);
    void draw_particle_mesh_2d(World& world);
    void draw_floor(World& world, glm::vec3 cam_pos);
    void draw_sky(glm::vec3 cam_pos, glm::mat4 const& view, glm::mat4 const& proj);
    void draw_boxes(World& world, glm::vec3 cam_pos);
};
