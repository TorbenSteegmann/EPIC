#include "mac_grid_3d.hh"

#include <algorithm>
#include <limits>

MAC_Grid_3D::MAC_Grid_3D(double dx, int width, int height, int depth, World& world)
  : dx_(dx),
    nx_(static_cast<int>(width / dx_)),
    ny_(static_cast<int>(height / dx_)),
    nz_(static_cast<int>(depth / dx_)),
    world_(world),
    velocity_(nx_, ny_, nz_),
    velocity_old_(nx_, ny_, nz_),
    pressure_data_(nx_ * ny_ * nz_),
    cell_labels_(nx_ * ny_ * nz_, AIR),
    distance_field_(nx_ * ny_ * nz_, std::numeric_limits<double>::infinity())
{
    for (int k = 0; k < nz_; ++k)
    {
        for (int j = 0; j < ny_; ++j)
        {
            for (int i = 0; i < nx_; ++i)
            {
                bool x_boundary = i == 0 || i == nx_ - 1;
                bool y_boundary = j == 0 || j == ny_ - 1;
                bool z_boundary = k == 0 || k == nz_ - 1;

                if (x_boundary || y_boundary || z_boundary)
                    cell_type(i, j, k) = SOLID;
            }
        }
    }
}

MAC_Grid_3D::~MAC_Grid_3D() { world_.clear(); }

void MAC_Grid_3D::clear()
{
    std::fill(velocity_.u_component.begin(), velocity_.u_component.end(), 0.0);
    std::fill(velocity_.v_component.begin(), velocity_.v_component.end(), 0.0);
    std::fill(velocity_.w_component.begin(), velocity_.w_component.end(), 0.0);
    std::fill(pressure_data_.begin(), pressure_data_.end(), 0.0);
    std::fill(distance_field_.begin(), distance_field_.end(), std::numeric_limits<double>::infinity());

    for (int k = 0; k < nz_; ++k)
    {
        for (int j = 0; j < ny_; ++j)
        {
            for (int i = 0; i < nx_; ++i)
            {
                if (cell_type(i, j, k) != SOLID)
                    cell_type(i, j, k) = AIR;
            }
        }
    }
}

void MAC_Grid_3D::save_grid_velocity()
{
    std::copy(velocity_.u_component.begin(), velocity_.u_component.end(), velocity_old_.u_component.begin());
    std::copy(velocity_.v_component.begin(), velocity_.v_component.end(), velocity_old_.v_component.begin());
    std::copy(velocity_.w_component.begin(), velocity_.w_component.end(), velocity_old_.w_component.begin());
}

double& MAC_Grid_3D::pressure(int i, int j, int k)
{
    assert(i >= 0 && i < nx_ && j >= 0 && j < ny_ && k >= 0 && k < nz_);
    return pressure_data_[grid_index_3d::center(i, j, k, nx_, ny_, nz_)];
}

Cell_Type& MAC_Grid_3D::cell_type(int i, int j, int k)
{
    assert(i >= 0 && i < nx_ && j >= 0 && j < ny_ && k >= 0 && k < nz_);
    return cell_labels_[grid_index_3d::center(i, j, k, nx_, ny_, nz_)];
}

double& MAC_Grid_3D::distance_field(int i, int j, int k)
{
    assert(i >= 0 && i < nx_ && j >= 0 && j < ny_ && k >= 0 && k < nz_);
    return distance_field_[grid_index_3d::center(i, j, k, nx_, ny_, nz_)];
}
