#pragma once

#include "../../ecs/ecs_common.hh"
#include "../../world.hh"
#include "grid.hh"
#include "simulation.hh"

#include <cassert>
#include <vector>

namespace grid_index_3d
{
inline int u(int i, int j, int k, int nx, int ny, int nz) { return i + (nx + 1) * (j + ny * k); }

inline int v(int i, int j, int k, int nx, int ny, int nz) { return i + nx * (j + (ny + 1) * k); }

inline int w(int i, int j, int k, int nx, int ny, int nz) { return i + nx * (j + ny * k); }

inline int center(int i, int j, int k, int nx, int ny, int nz) { return i + nx * (j + ny * k); }
} // namespace grid_index_3d

struct Velocity_Field_3D
{
    int nx, ny, nz;
    std::vector<double> u_component, v_component, w_component;

    Velocity_Field_3D(int nx, int ny, int nz)
      : nx(nx),
        ny(ny),
        nz(nz),
        u_component((nx + 1) * ny * nz),
        v_component(nx * (ny + 1) * nz),
        w_component(nx * ny * (nz + 1))
    {
    }

    double& u(int i, int j, int k)
    {
        assert(i >= 0 && i <= nx && j >= 0 && j < ny && k >= 0 && k < nz);
        return u_component[grid_index_3d::u(i, j, k, nx, ny, nz)];
    }

    double& v(int i, int j, int k)
    {
        assert(i >= 0 && i < nx && j >= 0 && j <= ny && k >= 0 && k < nz);
        return v_component[grid_index_3d::v(i, j, k, nx, ny, nz)];
    }

    double& w(int i, int j, int k)
    {
        assert(i >= 0 && i < nx && j >= 0 && j < ny && k >= 0 && k <= nz);
        return w_component[grid_index_3d::w(i, j, k, nx, ny, nz)];
    }

    double u(int i, int j, int k) const
    {
        assert(i >= 0 && i <= nx && j >= 0 && j < ny && k >= 0 && k < nz);
        return u_component[grid_index_3d::u(i, j, k, nx, ny, nz)];
    }

    double v(int i, int j, int k) const
    {
        assert(i >= 0 && i < nx && j >= 0 && j <= ny && k >= 0 && k < nz);
        return v_component[grid_index_3d::v(i, j, k, nx, ny, nz)];
    }

    double w(int i, int j, int k) const
    {
        assert(i >= 0 && i < nx && j >= 0 && j < ny && k >= 0 && k <= nz);
        return w_component[grid_index_3d::w(i, j, k, nx, ny, nz)];
    }
};

class MAC_Grid_3D : public Grid
{
public:
    MAC_Grid_3D(double dx, int width, int height, int depth, World& world);
    ~MAC_Grid_3D();

    void clear();
    void save_grid_velocity();

    [[nodiscard]] int nx() const { return nx_; };
    [[nodiscard]] int ny() const { return ny_; };
    [[nodiscard]] int nz() const { return nz_; };
    [[nodiscard]] glm::dvec3 origin() const { return origin_; };
    [[nodiscard]] double dx() const { return dx_; };
    [[nodiscard]] glm::dvec3 domain_min() const { return origin_; };
    [[nodiscard]] glm::dvec3 domain_max() const { return origin_ + glm::dvec3(nx_ * dx_, ny_ * dx_, nz_ * dx_); };
    [[nodiscard]] glm::dvec3 fluid_domain_min() const { return origin_ + glm::dvec3(dx_); };
    [[nodiscard]] glm::dvec3 fluid_domain_max() const { return origin_ + glm::dvec3((nx_ - 1) * dx_, (ny_ - 1) * dx_, (nz_ - 1) * dx_); };

    Velocity_Field_3D& velocity() { return velocity_; };
    Velocity_Field_3D& velocity_old() { return velocity_old_; };
    [[nodiscard]] Velocity_Field_3D velocity() const noexcept { return velocity_; };
    [[nodiscard]] Velocity_Field_3D velocity_old() const noexcept { return velocity_old_; };

    [[nodiscard]] std::vector<ecs::Entity>& particles() noexcept { return particles_; }

    double& pressure(int i, int j, int k);
    Cell_Type& cell_type(int i, int j, int k);
    double& distance_field(int i, int j, int k);

    void add_quad(double x, double y, double w, double h) override{};

private:
    double const dx_;
    int const nx_, ny_, nz_;
    glm::dvec3 origin_{0.0};
    World& world_;
    std::vector<ecs::Entity> particles_;
    Velocity_Field_3D velocity_;
    Velocity_Field_3D velocity_old_;
    std::vector<double> pressure_data_;
    std::vector<Cell_Type> cell_labels_;
    std::vector<double> distance_field_;
};
