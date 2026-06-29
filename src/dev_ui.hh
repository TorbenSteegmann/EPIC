#pragma once

#include "diagnostics.hh"
#include "ecs/transform_component.hh"
#include "simulation/core/grid.hh"
#include "world.hh"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>

#include "GLFW/glfw3.h"

#include <cstdint>
#include <memory>

class Dev_UI
{
public:
    float last_time;
    double last_sps_time = 0.0;
    double current_sps = 0.0;
    std::uint64_t last_sps_completed_steps = 0;

    GLFWwindow* window;

    void Init(GLFWwindow* window, char const* glsl_version);
    void NewFrame();
    virtual void Update(World& world, Grid& grid, Diagnostics& diagnostics, double sim_time, std::uint64_t completed_sim_steps);
    void Render();
    void Shutdown();
};
