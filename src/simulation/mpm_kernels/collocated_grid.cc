#include "collocated_grid.hh"

#include "../../resource_handler.hh"

#include <iostream>

Collocated_Grid::Collocated_Grid(int nx, int ny, double dx, World& world)
  : nx_(nx), ny_(ny), dx_(dx), world_(world), forces_(nx * ny), velocities_(nx * ny), masses_(nx * ny), cell_types_(nx * ny)
{
    for (int j = 0; j < ny_; ++j)
    {
        for (int i = 0; i < nx_; ++i)
        {
            bool x_boundary = i == 0 || i == nx_ - 1;
            bool y_boundary = j == 0 || j == ny_ - 1;

            if (y_boundary || x_boundary)
            {
                cell_type(i, j) = SOLID;

                // Boundary tiles are purely visual. Skip when visuals are off (e.g.
                // headless runs with no GL context): Sprite_Component's Texture_2D
                // default ctor calls glGenTextures and would segfault.
                if (world_.settings().CREATE_BOUNDARY_VISUALS)
                {
                    // create floor entity
                    ecs::Entity tile_ent = world_.create_entity("", ecs::Registry_Type::Local);

                    // determine position
                    Transform_Component tf{};
                    tf.position = glm::vec3(i * dx_, j * dx_, 0);
                    world.add_component<Transform_Component>(tile_ent, tf);

                    // assign a sprite
                    Sprite_Component sprite{};
                    sprite.texture = Resource_Handler::get_texture("block");
                    sprite.color = glm::vec3(0.0f);
                    world.add_component<Sprite_Component>(tile_ent, sprite);
                }
            }
        }
    }

    // bottom floor
    ecs::Entity floor_ent = world_.create_entity("", ecs::Registry_Type::Local);
    Collision_Component floor = Collision_Component::Plane(glm::dvec2(0.0, 1.0), 1.0 * dx_);
    floor.is_rigid = true;
    floor.friction = 0.5;
    world.add_component<Collision_Component>(floor_ent, floor);
    collision_bodies_.emplace_back(floor_ent);

    // top
    ecs::Entity ceiling_ent = world_.create_entity("", ecs::Registry_Type::Local);
    Collision_Component ceiling = Collision_Component::Plane(glm::dvec2(0.0, -1.0), -(ny_ - 3.0) * dx_);
    ceiling.is_rigid = true;
    world.add_component<Collision_Component>(ceiling_ent, ceiling);
    collision_bodies_.emplace_back(ceiling_ent);

    // left wall
    ecs::Entity left_ent = world_.create_entity("", ecs::Registry_Type::Local);
    Collision_Component left = Collision_Component::Plane(glm::dvec2(1.0, 0.0), 1.0 * dx_);
    left.is_rigid = true;
    world.add_component<Collision_Component>(left_ent, left);
    collision_bodies_.emplace_back(left_ent);

    // right wall
    ecs::Entity right_ent = world_.create_entity("", ecs::Registry_Type::Local);
    Collision_Component right = Collision_Component::Plane(glm::dvec2(-1.0, 0.0), -(nx_ - 3.0) * dx_);
    right.is_rigid = true;
    world.add_component<Collision_Component>(right_ent, right);
    collision_bodies_.emplace_back(right_ent);
}

Collocated_Grid::~Collocated_Grid() { world_.clear(); };

void Collocated_Grid::add_quad(double x, double y, double width, double height)
{
    double half_width = 0.5 * width;
    double half_height = 0.5 * height;

    bool oob_left = x - half_width < origin_.x;
    bool oob_right = x + half_width > origin_.x + nx_ / dx_;
    bool oob_bottom = y - half_height < origin_.y;
    bool oob_top = y + half_height > origin_.y + nx_ / dx_;

    if (oob_left || oob_right || oob_bottom || oob_top)
    {
        std::cerr << "Error: tried to add_quad out of grid-bounds." << std::endl;
        return;
    }

    glm::dvec2 min_corner{x - half_width, y - half_height};
    glm::dvec2 max_corner{x + half_width, y + half_height};

    auto [i0, j0] = cell_index_from_pos(min_corner);
    auto [in, jn] = cell_index_from_pos(max_corner);

    for (int j = j0; j < jn; ++j)
    {
        for (int i = i0; i < in; ++i)
        {
            cell_type(i, j) = FLUID;
            glm::dvec2 particle_pos_world = origin_ + glm::dvec2{(i + 0.5), (j + 0.5)};
            ecs::Entity particle = world_.create_entity();
            Particle_Component pc;
            pc.position = glm::dvec3(particle_pos_world, 0.0);
            pc.radius = dx_;
            world_.add_component(particle, pc);
            Mass_Component mc;
            mc.mass = 1.0;
            world_.add_component(particle, mc);
            Deformable_Component dc;
            world_.add_component(particle, dc);
            particles_.emplace_back(particle);
        }
    }
}

void Collocated_Grid::add_compressed_square(int square_cells, int particles_per_cell_axis, double compression)
{
    if (square_cells <= 0 || particles_per_cell_axis <= 0 || square_cells >= nx_ - 4 || square_cells >= ny_ - 4)
    {
        std::cerr << "Error: compressed square does not fit in the MPM grid." << std::endl;
        return;
    }

    int const first_i = (nx_ - square_cells) / 2;
    int const first_j = (ny_ - square_cells) / 2;
    double const spacing = dx_ / static_cast<double>(particles_per_cell_axis);
    double const mass = 1.0 / static_cast<double>(particles_per_cell_axis * particles_per_cell_axis);

    for (int j = 0; j < square_cells; ++j)
    {
        for (int i = 0; i < square_cells; ++i)
        {
            for (int py = 0; py < particles_per_cell_axis; ++py)
            {
                for (int px = 0; px < particles_per_cell_axis; ++px)
                {
                    glm::dvec2 const position{
                        (first_i + i) * dx_ + (px + 0.5) * spacing,
                        (first_j + j) * dx_ + (py + 0.5) * spacing,
                    };

                    ecs::Entity particle = world_.create_entity();

                    Particle_Component pc;
                    pc.position = glm::dvec3(position, 0.0);
                    pc.radius = static_cast<float>(0.42 * spacing);
                    world_.add_component(particle, pc);

                    Mass_Component mc;
                    mc.mass = mass;
                    world_.add_component(particle, mc);

                    Deformable_Component dc;
                    dc.F_E = compression * glm::dmat2(1.0);
                    dc.F = dc.F_E;
                    world_.add_component(particle, dc);

                    particles_.push_back(particle);
                }
            }
        }
    }
}

double Collocated_Grid::b_spline(double component_distance)
{
    component_distance = glm::abs(component_distance);

    if (1.5 <= component_distance)
        return 0.0;

    if (component_distance < 0.5)
        return 0.75 - component_distance * component_distance;

    double const d = 1.5 - component_distance;
    return 0.5 * d * d;
}

double Collocated_Grid::cell_weight_from_pos(int i, int j, glm::dvec2 pos)
{
    return b_spline((1.0 / dx_) * (pos.x - i * dx_)) * b_spline((1.0 / dx_) * (pos.y - j * dx_));
};

double Collocated_Grid::b_spline_slope(double component_distance)
{
    double abs_dist = glm::abs(component_distance);

    if (abs_dist >= 1.5)
        return 0.0;

    double sign = (component_distance < 0.0) ? -1.0 : 1.0;

    if (abs_dist < 0.5)
        return -2.0 * component_distance;

    return -(1.5 - abs_dist) * sign;
}

glm::dvec2 Collocated_Grid::cell_weight_gradient_from_pos(int i, int j, glm::dvec2 pos)
{
    double dist_x = (1.0 / dx_) * (pos.x - i * dx_);
    double dist_y = (1.0 / dx_) * (pos.y - j * dx_);

    double w_x = b_spline(dist_x);
    double w_y = b_spline(dist_y);

    double dw_x = b_spline_slope(dist_x);
    double dw_y = b_spline_slope(dist_y);

    double inv_dx = 1.0 / dx_;

    return glm::dvec2(dw_x * w_y * inv_dx, w_x * dw_y * inv_dx);
}

void Collocated_Grid::clear()
{
    std::fill(masses_.begin(), masses_.end(), 0.0);
    std::fill(velocities_.begin(), velocities_.end(), glm::dvec2(0.0));
    std::fill(forces_.begin(), forces_.end(), glm::dvec2(0.0));
    mpm_cells_.clear();

    for (int j = 0; j < ny_; ++j)
    {
        for (int i = 0; i < nx_; ++i)
        {
            if (cell_type(i, j) != SOLID)
                cell_type(i, j) = AIR;
        }
    }
}

void Collocated_Grid::save() { old_velocities_ = velocities_; }

bool Collocated_Grid::in_bounds(int i, int j)
{
    bool in_x = i >= 0 && i < nx_;
    bool in_y = j >= 0 && j < ny_;
    return in_x && in_y;
}
