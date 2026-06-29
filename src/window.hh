#pragma once

#include "dev_ui.hh"

#include "simulation/core/grid.hh"

#include <memory>

#include <glad/glad.h>
// glad needs to be included before GLFW
#include <GLFW/glfw3.h>

class Window
{
public:
    unsigned int BASE_WIDTH;
    unsigned int BASE_HEIGHT;
    unsigned int SCREEN_WIDTH;
    unsigned int SCREEN_HEIGHT;
    unsigned int FB_WIDTH;
    unsigned int FB_HEIGHT;
    float scale_width;
    float scale_height;
    float aspect_ratio;
    float scroll_factor;
    GLFWwindow* glfw_window;
    std::shared_ptr<Dev_UI> dev_ui;

    Window(unsigned int width = 800, unsigned int height = 600);
    void clear();
};
