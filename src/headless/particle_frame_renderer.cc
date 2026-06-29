#include "camera.hh"
#include "ecs/box_component.hh"
#include "renderer.hh"
#include "resource_handler.hh"
#include "stb_image/stb_image_write.h"
#include "world.hh"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr std::array<char, 8> MAGIC{'F', 'L', 'D', 'P', 'A', 'R', 'T', '1'};

struct Export_Header
{
    std::uint32_t version = 0;
    std::uint32_t dimension = 0;
    std::uint32_t frame_count = 0;
    std::uint32_t record_size = 0;
    double fps = 0.0;
    double domain_width = 0.0;
    double domain_height = 0.0;
    double domain_depth = 0.0;
    double dx = 0.0;
    std::int32_t scene = 0;
    std::int32_t solver = 0;
    std::int32_t polypic_modes = 0;
    float flip_percent = 0.0f;
};

struct Frame_Header
{
    double frame_time = 0.0;
    double simulation_time = 0.0;
    std::uint32_t particle_count = 0;
    std::uint32_t reserved = 0;
};

struct Particle_Record
{
    std::uint32_t entity = 0;
    float radius = 0.0f;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
};

struct Options
{
    std::filesystem::path input;
    std::filesystem::path frames_dir;
    int width = 1600;
    int height = 1000;
};

template <typename T>
T read_scalar(std::ifstream& stream, char const* label)
{
    T value{};
    stream.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!stream)
        throw std::runtime_error(std::string("Particle export ended while reading ") + label);
    return value;
}

Export_Header read_header(std::ifstream& stream)
{
    std::array<char, 8> magic{};
    stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!stream || magic != MAGIC)
        throw std::runtime_error("Input is not a supported FLDPART1 particle export");

    Export_Header header;
    header.version = read_scalar<std::uint32_t>(stream, "version");
    header.dimension = read_scalar<std::uint32_t>(stream, "dimension");
    header.frame_count = read_scalar<std::uint32_t>(stream, "frame count");
    header.record_size = read_scalar<std::uint32_t>(stream, "record size");
    header.fps = read_scalar<double>(stream, "FPS");
    header.domain_width = read_scalar<double>(stream, "domain width");
    header.domain_height = read_scalar<double>(stream, "domain height");
    header.domain_depth = read_scalar<double>(stream, "domain depth");
    header.dx = read_scalar<double>(stream, "grid spacing");
    header.scene = read_scalar<std::int32_t>(stream, "scene");
    header.solver = read_scalar<std::int32_t>(stream, "solver");
    header.polypic_modes = read_scalar<std::int32_t>(stream, "PolyPIC modes");
    header.flip_percent = read_scalar<float>(stream, "FLIP percentage");

    if (header.version != 1 || header.record_size != 32)
        throw std::runtime_error("Unsupported particle export version or record size");
    if (header.dimension != 3)
        throw std::runtime_error("The C++ replay renderer currently requires a 3D particle export");
    if (header.fps <= 0.0 || header.domain_width <= 0.0 || header.domain_height <= 0.0 || header.domain_depth <= 0.0)
        throw std::runtime_error("Particle export contains invalid FPS or domain extents");
    return header;
}

Frame_Header read_frame_header(std::ifstream& stream)
{
    return {
        .frame_time = read_scalar<double>(stream, "frame time"),
        .simulation_time = read_scalar<double>(stream, "simulation time"),
        .particle_count = read_scalar<std::uint32_t>(stream, "particle count"),
        .reserved = read_scalar<std::uint32_t>(stream, "frame reserved field"),
    };
}

Particle_Record read_particle(std::ifstream& stream)
{
    Particle_Record particle;
    particle.entity = read_scalar<std::uint32_t>(stream, "particle entity");
    particle.radius = read_scalar<float>(stream, "particle radius");
    particle.position.x = read_scalar<float>(stream, "particle position x");
    particle.position.y = read_scalar<float>(stream, "particle position y");
    particle.position.z = read_scalar<float>(stream, "particle position z");
    particle.velocity.x = read_scalar<float>(stream, "particle velocity x");
    particle.velocity.y = read_scalar<float>(stream, "particle velocity y");
    particle.velocity.z = read_scalar<float>(stream, "particle velocity z");
    return particle;
}

int parse_positive_int(std::string const& value, char const* option)
{
    try
    {
        int parsed = std::stoi(value);
        if (parsed > 0)
            return parsed;
    }
    catch (...)
    {
    }
    throw std::runtime_error(std::string(option) + " must be a positive integer");
}

Options parse_args(int argc, char** argv)
{
    if (argc < 2)
        throw std::runtime_error("usage: Fluid_Particle_Renderer INPUT --frames-dir DIR [--width N] [--height N]");

    Options options;
    options.input = argv[1];
    for (int index = 2; index < argc; ++index)
    {
        std::string_view argument = argv[index];
        auto value = [&](char const* name) -> std::string
        {
            if (++index >= argc)
                throw std::runtime_error(std::string("Missing value for ") + name);
            return argv[index];
        };
        if (argument == "--frames-dir")
            options.frames_dir = value("--frames-dir");
        else if (argument == "--width")
            options.width = parse_positive_int(value("--width"), "--width");
        else if (argument == "--height")
            options.height = parse_positive_int(value("--height"), "--height");
        else
            throw std::runtime_error("Unknown option: " + std::string(argument));
    }
    if (options.frames_dir.empty())
        throw std::runtime_error("--frames-dir is required");
    return options;
}

std::filesystem::path frame_path(std::filesystem::path const& directory, std::uint32_t index)
{
    std::ostringstream name;
    name << "frame_" << std::setw(6) << std::setfill('0') << index << ".png";
    return directory / name.str();
}

void write_framebuffer_png(std::filesystem::path const& path, int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);
    if (!stbi_write_png(path.string().c_str(), width, height, 3, pixels.data(), width * 3))
        throw std::runtime_error("Failed to write PNG frame: " + path.string());
}

std::vector<Particle_Render_Instance> read_frame_particles(std::ifstream& stream, Frame_Header const& frame)
{
    std::vector<Particle_Render_Instance> particles(frame.particle_count);
    for (std::uint32_t index = 0; index < frame.particle_count; ++index)
    {
        Particle_Record const record = read_particle(stream);
        Particle_Render_Instance& particle = particles[index];
        particle.position = record.position;
        particle.radius = record.radius;
        particle.velocity = record.velocity;
    }
    return particles;
}

int render(Options const& options)
{
    std::ifstream stream(options.input, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Could not open particle export: " + options.input.string());
    Export_Header const header = read_header(stream);
    std::filesystem::create_directories(options.frames_dir);
    for (auto const& old_frame : std::filesystem::directory_iterator(options.frames_dir))
        if (old_frame.is_regular_file() && old_frame.path().filename().string().starts_with("frame_") && old_frame.path().extension() == ".png")
            std::filesystem::remove(old_frame.path());

    if (!glfwInit())
        throw std::runtime_error("Failed to initialize GLFW");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    GLFWwindow* window = glfwCreateWindow(options.width, options.height, "Fluid particle replay", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        throw std::runtime_error("Failed to create hidden OpenGL render window");
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
    {
        glfwDestroyWindow(window);
        glfwTerminate();
        throw std::runtime_error("Failed to initialize GLAD");
    }

    glViewport(0, 0, options.width, options.height);
    glEnable(GL_BLEND);
    glEnable(GL_MULTISAMPLE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    int result = 0;
    try
    {
        Resource_Handler::load_texture("../../res/sprites/block.png", false, "block");
        Resource_Handler::load_texture("../../res/sprites/circle.png", true, "circle");

        World world;
        Box_Component const domain_box{
            .size = glm::vec3(static_cast<float>(header.domain_width), static_cast<float>(header.domain_height), static_cast<float>(header.domain_depth)),
            .offset = glm::vec3(0.0f),
        };
        ecs::Entity box_entity = world.create_entity("Domain");
        world.add_component(box_entity, domain_box);

        Framed_Camera_3D const camera = frame_box_for_dam_break(domain_box);
        glm::mat4 const view = glm::lookAt(camera.position, camera.target, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 const projection = glm::perspective(
            glm::radians(camera.fov), static_cast<float>(options.width) / static_cast<float>(options.height), 0.1f,
            std::max(2000.0f, glm::length(domain_box.size) * 20.0f));

        Renderer<3> renderer(static_cast<unsigned int>(options.width), static_cast<unsigned int>(options.height));
        for (std::uint32_t frame_index = 0; frame_index < header.frame_count; ++frame_index)
        {
            Frame_Header const frame = read_frame_header(stream);
            std::vector<Particle_Render_Instance> const particles = read_frame_particles(stream, frame);
            glClearColor(0.79f, 0.79f, 0.79f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderer.tick_replay(world, particles, camera.position, view, projection);
            glFinish();
            write_framebuffer_png(frame_path(options.frames_dir, frame_index), options.width, options.height);
        }
        std::cout << "Rendered " << header.frame_count << " frame(s) at " << header.fps << " FPS to " << options.frames_dir << '\n';
    }
    catch (...)
    {
        result = 1;
        Resource_Handler::clear();
        glfwDestroyWindow(window);
        glfwTerminate();
        throw;
    }

    Resource_Handler::clear();
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        return render(parse_args(argc, argv));
    }
    catch (std::exception const& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
