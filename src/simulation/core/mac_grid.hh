#pragma once

#include "../../ecs/ecs_common.hh"
#include "../../world.hh"
#include "grid.hh"
#include "simulation.hh"

#include <span>
#include <vector>


// Index helpers
namespace grid_index
{
inline int u(int i, int j, int nx, int ny) { return i + (nx + 1) * j; }

inline int v(int i, int j, int nx, int ny) { return i + nx * j; }

inline int center(int i, int j, int nx, int ny) { return i + nx * j; }
} // namespace grid_index


struct Velocity_Field
{
    int nx, ny;
    std::vector<double> u_component, v_component;

    Velocity_Field(int nx, int ny) : nx(nx), ny(ny), u_component((nx + 1) * ny), v_component(nx * (ny + 1)) {}

    double& u(int i, int j)
    {
        assert(i >= 0 && i <= nx && j >= 0 && j < ny);
        return u_component[grid_index::u(i, j, nx, ny)];
    }

    double& v(int i, int j)
    {
        assert(i >= 0 && i < nx && j >= 0 && j <= ny);
        return v_component[grid_index::v(i, j, nx, ny)];
    }

    double u(int i, int j) const
    {
        assert(i >= 0 && i <= nx && j >= 0 && j < ny);
        return u_component[grid_index::u(i, j, nx, ny)];
    }

    double v(int i, int j) const
    {
        assert(i >= 0 && i < nx && j >= 0 && j <= ny);
        return v_component[grid_index::v(i, j, nx, ny)];
    }
};

class MAC_Grid_2D : public Grid
{
public:
    MAC_Grid_2D(double dx, int nx, int ny, World& world);
    ~MAC_Grid_2D();

    void clear();
    void save_grid_velocity();

    // Access functions
    [[nodiscard]] int nx() const { return nx_; };
    [[nodiscard]] int ny() const { return ny_; };
    [[nodiscard]] glm::dvec2 origin() { return origin_; };
    [[nodiscard]] double dx() { return dx_; };

    Velocity_Field& u() { return u_; };
    Velocity_Field& u_old() { return u_old_; };
    // Velocity_Field u() const { return u_; };
    // Velocity_Field u_old() const { return u_old_; };

    [[nodiscard]] std::vector<ecs::Entity>& particles() noexcept { return particles_; }

    [[nodiscard]] Velocity_Field u() const noexcept { return u_; };
    [[nodiscard]] Velocity_Field u_old() const noexcept { return u_old_; };

    double& pressure(int i, int j);
    Cell_Type& cell_type(int i, int j);
    double& distance_field(int i, int j);

    void add_quad(double x, double y, double w, double h) override{};

private:
    double const dx_;
    int const nx_, ny_;
    glm::dvec2 origin_{0.0};
    World& world_;
    std::vector<ecs::Entity> particles_;
    std::vector<ecs::Entity> created_entities_;
    Velocity_Field u_;
    Velocity_Field u_old_;
    std::vector<double> pressure_data_;
    std::vector<Cell_Type> cell_labels_;
    std::vector<double> distance_field_;
    std::vector<glm::dvec2> force;
};
