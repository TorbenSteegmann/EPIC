#include "window.hh"

#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

Window::Window(unsigned int width, unsigned int height) : SCREEN_WIDTH(width), SCREEN_HEIGHT(height)
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_RESIZABLE, true);
    glfwWindowHint(GLFW_SAMPLES, 4);
    // glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_FALSE); // uncap fps
    glfw_window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "2D_Fluids", nullptr, nullptr);
    glfwSetWindowUserPointer(glfw_window, this);
    glfwMakeContextCurrent(glfw_window);

    // glad: load all OpenGL function pointers
    // ---------------------------------------
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        throw -1;
    }

    glfwSetFramebufferSizeCallback(glfw_window, framebuffer_size_callback);
    glfwSetScrollCallback(glfw_window, scroll_callback);
    glfwSwapInterval(0); // turn of v-sync (1 turns on and 2 syncs every 2 frames)

    // OpenGL configuration
    // --------------------
    int fb_width, fb_height;
    glfwGetFramebufferSize(glfw_window, &fb_width, &fb_height);
    FB_WIDTH = fb_width;
    FB_HEIGHT = fb_height;
    BASE_WIDTH = width;
    scale_width = fb_width / BASE_WIDTH;
    BASE_HEIGHT = height;
    scale_height = fb_height / BASE_HEIGHT;

    scroll_factor = 5.5;

    glViewport(0, 0, FB_WIDTH, FB_HEIGHT);
    glEnable(GL_BLEND);
    glEnable(GL_MULTISAMPLE);
    // glEnable(GL_DEPTH_TEST); // render doesnt blend no more
    // glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ImGui Config
    char const* glsl_version = "#version 330";
    dev_ui = std::make_shared<Dev_UI>();
    dev_ui->Init(glfw_window, glsl_version);
}

void Window::clear()
{
    glClearColor(0.79f, 0.79f, 0.79f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void framebuffer_size_callback(GLFWwindow* glfw_window, int width, int height)
{
    glViewport(0, 0, width, height);

    int win_width, win_height;
    glfwGetWindowSize(glfw_window, &win_width, &win_height);

    // Get Window instance
    Window* win = static_cast<Window*>(glfwGetWindowUserPointer(glfw_window));
    if (!win)
        return;

    win->FB_WIDTH = width;
    win->FB_HEIGHT = height;
    win->scale_width = static_cast<float>(width) / static_cast<float>(win->BASE_WIDTH);
    win->scale_height = static_cast<float>(height) / static_cast<float>(win->BASE_HEIGHT);
    win->aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
}

void scroll_callback(GLFWwindow* glfw_window, double xoffset, double yoffset)
{
    Window* win = static_cast<Window*>(glfwGetWindowUserPointer(glfw_window));
    if (!win)
        return;

    win->scroll_factor += yoffset * 0.1;
}
