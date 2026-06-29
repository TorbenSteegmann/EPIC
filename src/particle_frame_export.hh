#pragma once

#include "world.hh"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <glm/glm.hpp>

struct Particle_Frame_Export_Config
{
    int dimension = 2;
    int scene = 0;
    int solver = 0;
    int polypic_modes = 0;
    float flip_percent = 0.0f;
    double fps = 27.0;
    double domain_width = 0.0;
    double domain_height = 0.0;
    double domain_depth = 0.0;
    double dx = 1.0;
};

struct Particle_Frame_Record
{
    std::uint32_t entity = 0;
    float radius = 0.0f;
    glm::dvec3 position{0.0};
    glm::dvec3 velocity{0.0};
};

using Particle_Frame_Snapshot = std::vector<Particle_Frame_Record>;

Particle_Frame_Snapshot capture_particle_frame(World& world);

class Particle_Frame_Writer
{
public:
    Particle_Frame_Writer(std::filesystem::path const& path, Particle_Frame_Export_Config const& config);
    ~Particle_Frame_Writer();

    Particle_Frame_Writer(Particle_Frame_Writer const&) = delete;
    Particle_Frame_Writer& operator=(Particle_Frame_Writer const&) = delete;

    void write_frame(Particle_Frame_Snapshot const& particles, double frame_time, double simulation_time);
    void write_interpolated_frame(Particle_Frame_Snapshot const& previous, Particle_Frame_Snapshot const& current,
                                  double alpha, double frame_time, double simulation_time);
    void close();

    std::uint32_t frame_count() const { return frame_count_; }
    std::filesystem::path const& path() const { return path_; }

private:
    std::filesystem::path path_;
    std::ofstream stream_;
    std::streampos frame_count_position_{};
    std::uint32_t frame_count_ = 0;
    bool closed_ = false;

    template <typename T>
    void write_scalar(T const& value)
    {
        stream_.write(reinterpret_cast<char const*>(&value), sizeof(T));
    }
};
