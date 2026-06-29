#include "enforce_boundary_3d.hh"
#include "../../profile_timer.hh"

namespace Fluid_Simulation_3D
{
void enforce_boundary_3d(MAC_Grid_3D& grid)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_3D::enforce_boundary_3d");

    int nx = grid.nx();
    int ny = grid.ny();
    int nz = grid.nz();

    // u component
    for (int k = 0; k < nz; ++k)
    {
        for (int j = 0; j < ny; ++j)
        {
            for (int i = 0; i <= nx; ++i)
            {
                bool left_solid = (i > 0 && grid.cell_type(i - 1, j, k) == SOLID);
                bool right_solid = (i < nx && grid.cell_type(i, j, k) == SOLID);
                if (left_solid || right_solid)
                    grid.velocity().u(i, j, k) = 0.0;
            }
        }
    }

    // v component
    for (int k = 0; k < nz; ++k)
    {
        for (int j = 0; j <= ny; ++j)
        {
            for (int i = 0; i < nx; ++i)
            {
                bool bot_solid = (j > 0 && grid.cell_type(i, j - 1, k) == SOLID);
                bool top_solid = (j < ny && grid.cell_type(i, j, k) == SOLID);
                if (bot_solid || top_solid)
                    grid.velocity().v(i, j, k) = 0.0;
            }
        }
    }

    // w component
    for (int k = 0; k <= nz; ++k)
    {
        for (int j = 0; j < ny; ++j)
        {
            for (int i = 0; i < nx; ++i)
            {
                bool back_solid = (k > 0 && grid.cell_type(i, j, k - 1) == SOLID);
                bool front_solid = (k < nz && grid.cell_type(i, j, k) == SOLID);
                if (back_solid || front_solid)
                    grid.velocity().w(i, j, k) = 0.0;
            }
        }
    }
}
} // namespace Fluid_Simulation_3D
