#include "renderer.hh"
#include "camera.hh"
#include "ecs/transform_component.hh"
#include "particle_mesher.hh"
#include "resource_handler.hh"
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

template <int Dimension>
static Particle_Render_Instance make_particle_render_instance(Particle_Component const& particle, ecs::Entity entity,
                                                              ecs::Entity selected_entity, ecs::Entity cfl_entity)
{
    Particle_Render_Instance instance;

    instance.position = glm::vec3(particle.position);
    instance.radius = particle.radius;
    instance.velocity = glm::vec3(particle.velocity);
    if (entity == selected_entity && entity == cfl_entity)
        instance.highlight = 3.0f;
    else if (entity == selected_entity)
        instance.highlight = 1.0f;
    else if (entity == cfl_entity)
        instance.highlight = 2.0f;

    return instance;
}

template <int Dimension>
Renderer<Dimension>::Renderer(unsigned int fb_width, unsigned int fb_height)
{
    sprite_shader_   = Resource_Handler::load_shader("../../res/shaders/sprite.vs",   "../../res/shaders/sprite.fs",   nullptr, "sprite");
    particle_shader_ = Resource_Handler::load_shader("../../res/shaders/particle.vs", "../../res/shaders/particle.fs", nullptr, "particle");
    if constexpr (Dimension == 2)
        particle_mesh_shader_ = Resource_Handler::load_shader("../../res/shaders/particle_mesh.vs", "../../res/shaders/particle_mesh.fs", nullptr, "particle_mesh");

    sprite_shader_.use();
    sprite_shader_.set_integer("image", 0);

    particle_shader_.use();
    particle_shader_.set_integer("circleTex", 0);

    if constexpr (Dimension == 3)
    {
        box_shader_ = Resource_Handler::load_shader("../../res/shaders/box.vs", "../../res/shaders/box.fs", nullptr, "box");
        box_shader_.use();
        box_shader_.set_integer("blockTex", 0);

        sky_shader_   = Resource_Handler::load_shader("../../res/shaders/sky.vs",   "../../res/shaders/sky.fs",   nullptr, "sky");
        floor_shader_ = Resource_Handler::load_shader("../../res/shaders/floor.vs", "../../res/shaders/floor.fs", nullptr, "floor");
    }

    initRenderData();
}

template <int Dimension>
Renderer<Dimension>::~Renderer()
{
    glDeleteVertexArrays(1, &sprite_VAO_);
    glDeleteVertexArrays(1, &particle_VAO_);
    glDeleteBuffers(1, &sprite_VBO_);
    glDeleteBuffers(1, &particle_VBO_);
    glDeleteBuffers(1, &instance_VBO_);
    glDeleteVertexArrays(1, &particle_mesh_VAO_);
    glDeleteBuffers(1, &particle_mesh_VBO_);
    if constexpr (Dimension == 3)
    {
        glDeleteVertexArrays(1, &box_VAO_);
        glDeleteBuffers(1, &box_VBO_);
        glDeleteVertexArrays(1, &sky_VAO_);
        glDeleteBuffers(1, &sky_VBO_);
    }
}

template <int Dimension>
void Renderer<Dimension>::initRenderData()
{
    // --- Sprite VAO/VBO ---
    float quadVertices[] = {// x, y, u, v
                            0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,

                            0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f};

    // --- Particle VAO/VBO ---
    glGenVertexArrays(1, &particle_VAO_);
    glBindVertexArray(particle_VAO_);

    // Particle geometry uses its own quad VBO
    glGenBuffers(1, &particle_VBO_);
    glBindBuffer(GL_ARRAY_BUFFER, particle_VBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // Vertex attributes for quad
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribDivisor(0, 0);

    // Instance buffer
    glGenBuffers(1, &instance_VBO_);
    glBindBuffer(GL_ARRAY_BUFFER, instance_VBO_);
    glBufferData(GL_ARRAY_BUFFER, 10000 * sizeof(Particle_Render_Instance), nullptr, GL_STREAM_DRAW);

    // Instance attributes — always vec3 position for billboard shader
    // Position.xyz
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Particle_Render_Instance), (void*)0);
    glVertexAttribDivisor(1, 1);

    // Radius
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Particle_Render_Instance), (void*)(3 * sizeof(float)));
    glVertexAttribDivisor(2, 1);

    // Velocity.xyz
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Particle_Render_Instance), (void*)(4 * sizeof(float)));
    glVertexAttribDivisor(3, 1);

    // Highlight mode
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Particle_Render_Instance), (void*)(7 * sizeof(float)));
    glVertexAttribDivisor(4, 1);

    glBindVertexArray(0);

    if constexpr (Dimension == 2)
    {
        glGenVertexArrays(1, &particle_mesh_VAO_);
        glBindVertexArray(particle_mesh_VAO_);
        glGenBuffers(1, &particle_mesh_VBO_);
        glBindBuffer(GL_ARRAY_BUFFER, particle_mesh_VBO_);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
        glBindVertexArray(0);
    }

    glGenVertexArrays(1, &sprite_VAO_);
    glBindVertexArray(sprite_VAO_);
    glGenBuffers(1, &sprite_VBO_);

    glBindBuffer(GL_ARRAY_BUFFER, sprite_VBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // Sprite attributes
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribDivisor(0, 0);
    glBindVertexArray(0);

    if constexpr (Dimension == 3)
    {
        // Unit cube (0..1), outward-facing CCW normals — GL_CULL_FACE GL_FRONT renders the inner walls.
        // Adapted from LearnOpenGL standard cube, translated from [-0.5,0.5] to [0,1].
        // Each vertex: x,y,z, u,v, face_id
        static const float box_verts[] = {
            // z=0 face (outward -Z)  face_id=0
            0,0,0, 0,0,0,  1,1,0, 1,1,0,  1,0,0, 1,0,0,  1,1,0, 1,1,0,  0,0,0, 0,0,0,  0,1,0, 0,1,0,
            // z=1 face (outward +Z)  face_id=1
            0,0,1, 0,0,1,  1,0,1, 1,0,1,  1,1,1, 1,1,1,  1,1,1, 1,1,1,  0,1,1, 0,1,1,  0,0,1, 0,0,1,
            // x=0 face (outward -X)  face_id=2
            0,1,1, 1,0,2,  0,1,0, 1,1,2,  0,0,0, 0,1,2,  0,0,0, 0,1,2,  0,0,1, 0,0,2,  0,1,1, 1,0,2,
            // x=1 face (outward +X)  face_id=3
            1,1,1, 1,0,3,  1,0,0, 0,1,3,  1,1,0, 1,1,3,  1,0,0, 0,1,3,  1,1,1, 1,0,3,  1,0,1, 0,0,3,
            // y=0 face (outward -Y)  face_id=4
            0,0,0, 0,1,4,  1,0,0, 1,1,4,  1,0,1, 1,0,4,  1,0,1, 1,0,4,  0,0,1, 0,0,4,  0,0,0, 0,1,4,
            // y=1 face (outward +Y)  face_id=5
            0,1,0, 0,1,5,  1,1,1, 1,0,5,  1,1,0, 1,1,5,  1,1,1, 1,0,5,  0,1,0, 0,1,5,  0,1,1, 0,0,5,
        };

        glGenVertexArrays(1, &box_VAO_);
        glBindVertexArray(box_VAO_);
        glGenBuffers(1, &box_VBO_);
        glBindBuffer(GL_ARRAY_BUFFER, box_VBO_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(box_verts), box_verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(5 * sizeof(float)));
        glBindVertexArray(0);

        // Fullscreen triangle for sky
        static const float sky_verts[] = {-1, -1,  3, -1,  -1, 3};
        glGenVertexArrays(1, &sky_VAO_);
        glBindVertexArray(sky_VAO_);
        glGenBuffers(1, &sky_VBO_);
        glBindBuffer(GL_ARRAY_BUFFER, sky_VBO_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(sky_verts), sky_verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glBindVertexArray(0);
    }
}

template <int Dimension>
void Renderer<Dimension>::apply_camera_uniforms(glm::mat4 const& view, glm::mat4 const& projection)
{
    sprite_shader_.use();
    sprite_shader_.set_matrix_4("view", view);
    sprite_shader_.set_matrix_4("projection", projection);

    particle_shader_.use();
    particle_shader_.set_matrix_4("view", view);
    particle_shader_.set_matrix_4("projection", projection);

    if constexpr (Dimension == 2)
    {
        particle_mesh_shader_.use();
        particle_mesh_shader_.set_matrix_4("view", view);
        particle_mesh_shader_.set_matrix_4("projection", projection);
    }

    if constexpr (Dimension == 3)
    {
        box_shader_.use();
        box_shader_.set_matrix_4("view", view);
        box_shader_.set_matrix_4("projection", projection);

        floor_shader_.use();
        floor_shader_.set_matrix_4("view", view);
        floor_shader_.set_matrix_4("projection", projection);
    }
}

template <int Dimension>
void Renderer<Dimension>::begin_2d_scene_pass()
{
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

template <int Dimension>
void Renderer<Dimension>::begin_3d_scene_pass()
{
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

template <int Dimension>
void Renderer<Dimension>::end_3d_scene_pass()
{
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
}

template <int Dimension>
void Renderer<Dimension>::begin_3d_particle_pass()
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
}

template <int Dimension>
void Renderer<Dimension>::end_3d_particle_pass()
{
    glDepthMask(GL_TRUE);
}

template <int Dimension>
void Renderer<Dimension>::draw_scene_2d(World& world, ecs::Entity selected_entity, ecs::Entity cfl_entity)
{
    begin_2d_scene_pass();
    draw_sprites(world);
    if (world.settings().RENDER_PARTICLE_MESH_2D)
        draw_particle_mesh_2d(world);
    else
        draw_particles(world, selected_entity, cfl_entity);
}

template <int Dimension>
void Renderer<Dimension>::draw_scene_3d(World& world, ecs::Entity selected_entity, ecs::Entity cfl_entity,
                                        glm::vec3 const& cam_pos, glm::mat4 const& view, glm::mat4 const& proj)
{
    begin_3d_scene_pass();
    draw_sky(cam_pos, view, proj);
    draw_floor(world, cam_pos);
    draw_boxes(world, cam_pos);
    begin_3d_particle_pass();
    draw_particles(world, selected_entity, cfl_entity);
    end_3d_particle_pass();
    end_3d_scene_pass();
}

template <int Dimension>
void Renderer<Dimension>::draw_sprite(Texture_2D& texture, glm::vec2 position, glm::vec2 size, float rotate, glm::vec3 color)
{
    sprite_shader_.use();

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(position, 0.0f));
    model = glm::translate(model, glm::vec3(0.5f * size.x, 0.5f * size.y, 0.0f));
    model = glm::rotate(model, glm::radians(rotate), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::translate(model, glm::vec3(-0.5f * size.x, -0.5f * size.y, 0.0f));
    model = glm::scale(model, glm::vec3(size, 1.0f));

    sprite_shader_.set_matrix_4("model", model);
    sprite_shader_.set_vector_3f("spriteColor", color);


    glActiveTexture(GL_TEXTURE0);
    texture.bind();

    glBindVertexArray(sprite_VAO_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

template <int Dimension>
void Renderer<Dimension>::draw_sky(glm::vec3 cam_pos, glm::mat4 const& view, glm::mat4 const& proj)
{
    sky_shader_.use();

    glm::mat4 inv_vp = glm::inverse(proj * view);
    sky_shader_.set_matrix_4("inv_vp", inv_vp);
    sky_shader_.set_vector_3f("cam_pos", cam_pos);
    sky_shader_.set_vector_3f("sun_dir", glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f)));

    glDepthFunc(GL_LEQUAL);
    glBindVertexArray(sky_VAO_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
}

template <int Dimension>
void Renderer<Dimension>::draw_floor(World& world, glm::vec3 cam_pos)
{
    if constexpr (Dimension != 3)
        return;

    static constexpr float half = 2000.f;
    glm::vec2 grid_min(-10.f);
    glm::vec2 grid_max(110.f);
    int show_grid_plate = 0;

    auto& boxes = world.get_array<Box_Component>().data();
    if (!boxes.empty())
    {
        auto const& box = boxes.front();
        static constexpr float margin = 10.f;
        grid_min = glm::vec2(box.offset.x - margin, box.offset.z - margin);
        grid_max = glm::vec2(box.offset.x + box.size.x + margin, box.offset.z + box.size.z + margin);
        show_grid_plate = 1;
    }

    floor_shader_.use();

    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(-half, 0.f, -half));
    model = glm::rotate(model, glm::radians(90.f), glm::vec3(1.f, 0.f, 0.f));
    model = glm::scale(model, glm::vec3(half * 2.f, half * 2.f, 1.f));

    floor_shader_.set_matrix_4("model", model);
    floor_shader_.set_vector_3f("cam_pos", cam_pos);
    floor_shader_.set_vector_2f("grid_min", grid_min);
    floor_shader_.set_vector_2f("grid_max", grid_max);
    floor_shader_.set_integer("show_grid_plate", show_grid_plate);

    glBindVertexArray(sprite_VAO_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

template <int Dimension>
void Renderer<Dimension>::draw_sprites(World& world)
{
    auto& transforms = world.get_array<Transform_Component>();
    auto& sprites = world.get_array<Sprite_Component>();

    auto& s_data = sprites.data();
    auto& s_ents = sprites.entities();

    for (size_t i = 0; i < s_data.size(); ++i)
    {
        Sprite_Component& sprite = s_data[i];
        ecs::Entity e = s_ents[i];

        auto* tf = transforms.get(e);
        if (!tf)
            continue;

        glm::vec2 draw_pos = glm::vec2(tf->position.x, tf->position.y) - (glm::vec2(tf->scale.x, tf->scale.y) * tf->anchor);
        draw_sprite(sprite.texture, draw_pos, tf->scale, tf->rotation, sprite.color);
    }
}

template <int Dimension>
void Renderer<Dimension>::snapshot_particles(World& world, ecs::Entity selected_entity, ecs::Entity cfl_entity)
{
    cached_gpu_data_.clear();
    auto& p_data = world.get_array<Particle_Component>().data();
    auto& p_entities = world.get_array<Particle_Component>().entities();
    cached_gpu_data_.reserve(p_data.size());
    for (size_t i = 0; i < p_data.size(); ++i)
        cached_gpu_data_.push_back(make_particle_render_instance<Dimension>(p_data[i], p_entities[i], selected_entity, cfl_entity));
}

template <int Dimension>
void Renderer<Dimension>::draw_particles(World& world, ecs::Entity selected_entity, ecs::Entity cfl_entity)
{
    snapshot_particles(world, selected_entity, cfl_entity);
    draw_particle_instances(cached_gpu_data_);
}

template <int Dimension>
void Renderer<Dimension>::draw_particle_instances(std::vector<Particle_Render_Instance> const& particles)
{
    if (particles.empty())
        return;

    particle_shader_.use();

    Texture_2D& circleTex = Resource_Handler::get_texture("circle");
    glActiveTexture(GL_TEXTURE0);
    circleTex.bind();
    particle_shader_.set_integer("circleTex", 0);
    particle_shader_.set_float("max_speed", 500.0f);
    if constexpr (Dimension == 3)
    {
        particle_shader_.set_integer("use_solid_color", 1);
        particle_shader_.set_vector_3f("solid_color", glm::vec3(0.2f, 0.5f, 1.0f));
    }
    else
    {
        particle_shader_.set_integer("use_solid_color", 0);
        particle_shader_.set_vector_3f("solid_color", glm::vec3(0.2f, 0.5f, 1.0f));
    }

    // Update instance buffer
    glBindBuffer(GL_ARRAY_BUFFER, instance_VBO_);
    glBufferData(GL_ARRAY_BUFFER, particles.size() * sizeof(Particle_Render_Instance), particles.data(), GL_STREAM_DRAW);

    // Draw instanced quads
    glBindVertexArray(particle_VAO_);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(particles.size()));
    glBindVertexArray(0);
}

template <int Dimension>
void Renderer<Dimension>::draw_particle_mesh_2d(World& world)
{
    if constexpr (Dimension != 2)
        return;

    auto const& particles = world.get_array<Particle_Component>().data();
    Particle_Mesh_2D const mesh = build_particle_mesh_2d(particles);
    if (mesh.vertices.empty())
        return;

    particle_mesh_shader_.use();
    particle_mesh_shader_.set_vector_4f("meshColor", glm::vec4(0.10f, 0.45f, 0.95f, 0.82f));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindBuffer(GL_ARRAY_BUFFER, particle_mesh_VBO_);
    glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(glm::vec2), mesh.vertices.data(), GL_STREAM_DRAW);

    glBindVertexArray(particle_mesh_VAO_);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.vertices.size()));
    glBindVertexArray(0);

    glDisable(GL_BLEND);
}

template <int Dimension>
void Renderer<Dimension>::draw_boxes(World& world, glm::vec3 cam_pos)
{
    auto& boxes = world.get_array<Box_Component>();
    if (boxes.data().empty())
        return;

    box_shader_.use();
    glActiveTexture(GL_TEXTURE0);
    Resource_Handler::get_texture("block").bind();

    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    for (auto& box : boxes.data())
    {
        glm::vec3 lo = box.offset;
        glm::vec3 hi = box.offset + box.size;
        bool inside = cam_pos.x > lo.x && cam_pos.x < hi.x &&
                      cam_pos.y > lo.y && cam_pos.y < hi.y &&
                      cam_pos.z > lo.z && cam_pos.z < hi.z;
        glCullFace(inside ? GL_FRONT : GL_BACK);

        box_shader_.set_vector_3f("box_size", box.size);
        box_shader_.set_vector_3f("box_offset", box.offset);

        glBindVertexArray(box_VAO_);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
}

#ifndef FLUID_REPLAY_ONLY
template <int Dimension>
void Renderer<Dimension>::tick(World& world, Camera<Dimension> const& camera, Diagnostics const& diagnostics)
{
    glm::mat4 const& view = camera.view_matrix();
    glm::mat4 const& proj = camera.projection_matrix();
    glm::vec3 const& cam_pos = camera.position();

    apply_camera_uniforms(view, proj);

    if constexpr (Dimension == 3)
    {
        draw_scene_3d(world, diagnostics.selected_particle_entity(), diagnostics.cfl_particle_entity(), cam_pos, view, proj);
    }
    else
    {
        draw_scene_2d(world, diagnostics.selected_particle_entity(), diagnostics.cfl_particle_entity());
    }
}
#endif

template <int Dimension>
void Renderer<Dimension>::tick_replay(World& world, std::vector<Particle_Render_Instance> const& particles,
                                      glm::vec3 const& camera_position, glm::mat4 const& view, glm::mat4 const& projection)
{
    apply_camera_uniforms(view, projection);
    if constexpr (Dimension == 3)
    {
        begin_3d_scene_pass();
        draw_sky(camera_position, view, projection);
        draw_floor(world, camera_position);
        draw_boxes(world, camera_position);
        begin_3d_particle_pass();
        draw_particle_instances(particles);
        end_3d_particle_pass();
        end_3d_scene_pass();
    }
    else
    {
        begin_2d_scene_pass();
        draw_particle_instances(particles);
    }
}


template class Renderer<2>;
template class Renderer<3>;
