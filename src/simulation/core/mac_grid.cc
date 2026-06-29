#include "mac_grid.hh"

#include "../../resource_handler.hh"

#include <algorithm>
#include <cassert>

MAC_Grid_2D::MAC_Grid_2D(double dx, int width, int height, World& world)
  : dx_(dx),
    nx_(static_cast<int>(width / dx_)),
    ny_(static_cast<int>(height / dx_)),
    world_(world),
    u_(nx_, ny_),
    u_old_(nx_, ny_),
    pressure_data_(nx_ * ny_),
    cell_labels_(nx_ * ny_, AIR),
    distance_field_(nx_ * ny_, std::numeric_limits<double>::infinity())
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

                if (!world.settings().CREATE_BOUNDARY_VISUALS)
                    continue;

                // create floor entity
                ecs::Entity floor_ent = world_.create_entity("", ecs::Registry_Type::Local);

                // determine position
                Transform_Component tf{};
                tf.position = glm::vec3(i * dx_, j * dx_, 0.f);
                tf.scale = glm::vec3(dx_, dx_, 1.f);
                world.add_component<Transform_Component>(floor_ent, tf);

                // assign a sprite
                Sprite_Component sprite{};
                sprite.texture = Resource_Handler::get_texture("block");
                sprite.color = glm::vec3(0.0f);
                world.add_component<Sprite_Component>(floor_ent, sprite);
                created_entities_.emplace_back(floor_ent);
            }
        }
    }
}

MAC_Grid_2D::~MAC_Grid_2D() { world_.clear(); }

void MAC_Grid_2D::clear()
{
    std::fill(u_.u_component.begin(), u_.u_component.end(), 0.0);
    std::fill(u_.v_component.begin(), u_.v_component.end(), 0.0);
    std::fill(pressure_data_.begin(), pressure_data_.end(), 0.0);
    std::fill(distance_field_.begin(), distance_field_.end(), std::numeric_limits<double>::infinity());

    if (world_.settings().FLUID_SCENE == FULL_GRID || world_.settings().FLUID_SCENE == TAYLOR_GREEN_VORTEX)
        return;

    for (int j = 0; j < ny_; ++j)
        for (int i = 0; i < nx_; ++i)
        {
            if (cell_type(i, j) != SOLID)
                cell_type(i, j) = world_.settings().FLUID_SCENE == CONFINED_VORTEX ? FLUID : AIR;
        }
}

void MAC_Grid_2D::save_grid_velocity()
{
    std::copy(u_.u_component.begin(), u_.u_component.end(), u_old_.u_component.begin());
    std::copy(u_.v_component.begin(), u_.v_component.end(), u_old_.v_component.begin());
}

// Accessors
double& MAC_Grid_2D::pressure(int i, int j)
{
    assert(i >= 0 && i < nx_ && j >= 0 && j < ny_);
    return pressure_data_[grid_index::center(i, j, nx_, ny_)];
}

Cell_Type& MAC_Grid_2D::cell_type(int i, int j)
{
    assert(i >= 0 && i < nx_ && j >= 0 && j < ny_);
    return cell_labels_[grid_index::center(i, j, nx_, ny_)];
}

double& MAC_Grid_2D::distance_field(int i, int j)
{
    assert(i >= 0 && i < nx_ && j >= 0 && j < ny_);
    return distance_field_[grid_index::center(i, j, nx_, ny_)];
}
