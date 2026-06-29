#include "enforce_boundary.hh"
#include "../../profile_timer.hh"

void Fluid_Simulation_2D::enforce_boundary(MAC_Grid_2D& grid)
{
    FLUID_PROFILE_SCOPE("Fluid_Simulation_2D::enforce_boundary");

    int nx = grid.nx();
    int ny = grid.ny();

    // u component
    for (int j = 0; j < ny; ++j)
    {
        for (int i = 0; i <= nx; ++i)
        {
            bool leftSolid = (i > 0 && grid.cell_type(i - 1, j) == SOLID);
            bool rightSolid = (i < nx && grid.cell_type(i, j) == SOLID);
            if (leftSolid || rightSolid)
                grid.u().u(i, j) = 0.0;
        }
    }
    // end of u component

    // v component
    for (int j = 0; j <= ny; ++j)
    {
        for (int i = 0; i < nx; ++i)
        {
            bool botSolid = (j > 0 && grid.cell_type(i, j - 1) == SOLID);
            bool topSolid = (j < ny && grid.cell_type(i, j) == SOLID);
            if (botSolid || topSolid)
                grid.u().v(i, j) = 0.0;
        }
    }
    // end of v component
}
