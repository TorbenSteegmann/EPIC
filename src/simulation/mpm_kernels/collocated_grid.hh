#pragma once

#include "../../world.hh"
#include "../core/grid.hh"
#include "../core/simulation.hh"

#include "glm/glm.hpp"

#include <vector>

class Collocated_Grid : public Grid
{
public:
    Collocated_Grid(int nx, int ny, double dx, World& world);
    ~Collocated_Grid();

    void add_quad(double x, double y, double width, double height) override;
    void add_compressed_square(int square_cells = 12, int particles_per_cell_axis = 2, double compression = 0.9);

    bool in_bounds(int i, int j);
    int cell_index(int i, int j) { return i + j * nx_; };
    std::pair<int, int> cell_index(int idx) { return {idx % nx_, idx / nx_}; }
    glm::dvec2 cell_position(int i, int j) { return glm::dvec2(i * dx_, j * dx_) + origin_; };
    std::pair<int, int> cell_index_from_pos(glm::dvec2 pos)
    {
        return {static_cast<int>(glm::floor(pos.x / dx_)), static_cast<int>(std::floor(pos.y / dx_))};
    };
    double cell_weight_from_pos(int i, int j, glm::dvec2 pos);
    glm::dvec2 cell_weight_gradient_from_pos(int i, int j, glm::dvec2 pos);

    glm::dvec2& cell_force(int i, int j) { return forces_[cell_index(i, j)]; };
    glm::dvec2& cell_velocity(int i, int j) { return velocities_[cell_index(i, j)]; };
    glm::dvec2 old_cell_velocity(int i, int j) { return old_velocities_[cell_index(i, j)]; };
    double& cell_mass(int i, int j) { return masses_[cell_index(i, j)]; };
    double cell_density(int i, int j) { return cell_mass(i, j) / (dx_ * dx_); };
    Cell_Type& cell_type(int i, int j) { return cell_types_[cell_index(i, j)]; };
    std::vector<ecs::Entity>& particles() { return particles_; };
    std::vector<ecs::Entity>& collision_bodies() { return collision_bodies_; };
    std::vector<int>& mpm_cells() { return mpm_cells_; };

    int nx() const { return nx_; }
    int ny() const { return ny_; }
    double dx() { return dx_; };

    void clear();
    void save();

private:
    glm::dvec2 origin_{0.0, 0.0};
    int nx_, ny_;
    double dx_;
    World& world_;
    std::vector<ecs::Entity> particles_;
    std::vector<ecs::Entity> collision_bodies_;

    std::vector<glm::dvec2> forces_;
    std::vector<glm::dvec2> velocities_;
    std::vector<glm::dvec2> old_velocities_;
    std::vector<double> masses_;
    std::vector<Cell_Type> cell_types_;
    std::vector<int> mpm_cells_;

    double b_spline(double component_distance);
    double b_spline_slope(double component_distance);
};
