#pragma once

struct Grid
{
    virtual ~Grid() = default;
    virtual void add_quad(double x, double y, double w, double h) = 0;
};
