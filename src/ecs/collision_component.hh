#pragma once
#include <algorithm>
#include <functional>
#include <glm/glm.hpp>

struct Collision_Component
{
    std::function<double(glm::dvec2 const&)> sdf;

    std::function<glm::dvec2(glm::dvec2 const&)> sdf_gradient;

    double friction = 0.5;
    double restitution = 0.0;
    glm::dvec2 velocity = {0.0, 0.0};

    bool is_rigid = true;

    // Plane
    static Collision_Component Plane(glm::dvec2 const& normal, double offset, double friction = 0.5, double restitution = 0.0)
    {
        Collision_Component c;

        c.sdf = [normal, offset](glm::dvec2 const& x) { return glm::dot(normal, x) - offset; };

        c.sdf_gradient = [normal](glm::dvec2 const&) { return glm::normalize(normal); };

        c.friction = friction;
        c.restitution = restitution;

        return c;
    }

    // Circle
    static Collision_Component Circle(glm::dvec2 const& center, double radius, double friction = 0.5, double restitution = 0.0)
    {
        Collision_Component c;

        c.sdf = [center, radius](glm::dvec2 const& x) { return glm::length(x - center) - radius; };

        c.sdf_gradient = [center](glm::dvec2 const& x) { return glm::normalize(x - center); };

        c.friction = friction;
        c.restitution = restitution;

        return c;
    }

    // Axis-aligned box
    static Collision_Component Box(glm::dvec2 const& min, glm::dvec2 const& max, double friction = 0.5, double restitution = 0.0)
    {
        Collision_Component c;

        c.sdf = [min, max](glm::dvec2 const& x)
        {
            glm::dvec2 d = glm::max(glm::max(min - x, glm::dvec2(0.0)), x - max);
            double outside = glm::length(d);
            double inside = std::min(std::max(min.x - x.x, std::max(min.y - x.y, std::max(x.x - max.x, x.y - max.y))), 0.0);
            return outside + inside;
        };

        c.sdf_gradient = [min, max](glm::dvec2 const& x)
        {
            double eps = 1e-6;
            auto sdf = [min, max](glm::dvec2 const& p)
            {
                glm::dvec2 d = glm::max(glm::max(min - p, glm::dvec2(0.0)), p - max);
                double outside = glm::length(d);
                double inside = std::min(std::max(min.x - p.x, std::max(min.y - p.y, std::max(p.x - max.x, p.y - max.y))), 0.0);
                return outside + inside;
            };
            double dx = sdf(x + glm::dvec2(eps, 0.0)) - sdf(x - glm::dvec2(eps, 0.0));
            double dy = sdf(x + glm::dvec2(0.0, eps)) - sdf(x - glm::dvec2(0.0, eps));

            return glm::normalize(glm::dvec2(dx, dy) / (2.0 * eps));
        };

        c.friction = friction;
        c.restitution = restitution;

        return c;
    }
};
