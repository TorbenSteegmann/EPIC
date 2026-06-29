#include "particle_frame_export.hh"

#include "ecs/particle_component.hh"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>

namespace
{
constexpr std::array<char, 8> PARTICLE_FRAME_MAGIC{'F', 'L', 'D', 'P', 'A', 'R', 'T', '1'};
constexpr std::uint32_t PARTICLE_FRAME_VERSION = 1;
constexpr std::uint32_t PARTICLE_RECORD_SIZE = 32;

void write_particle(std::ofstream& stream, Particle_Frame_Record const& particle)
{
    float const position_x = static_cast<float>(particle.position.x);
    float const position_y = static_cast<float>(particle.position.y);
    float const position_z = static_cast<float>(particle.position.z);
    float const velocity_x = static_cast<float>(particle.velocity.x);
    float const velocity_y = static_cast<float>(particle.velocity.y);
    float const velocity_z = static_cast<float>(particle.velocity.z);

    stream.write(reinterpret_cast<char const*>(&particle.entity), sizeof(particle.entity));
    stream.write(reinterpret_cast<char const*>(&particle.radius), sizeof(particle.radius));
    stream.write(reinterpret_cast<char const*>(&position_x), sizeof(position_x));
    stream.write(reinterpret_cast<char const*>(&position_y), sizeof(position_y));
    stream.write(reinterpret_cast<char const*>(&position_z), sizeof(position_z));
    stream.write(reinterpret_cast<char const*>(&velocity_x), sizeof(velocity_x));
    stream.write(reinterpret_cast<char const*>(&velocity_y), sizeof(velocity_y));
    stream.write(reinterpret_cast<char const*>(&velocity_z), sizeof(velocity_z));
}
}

Particle_Frame_Snapshot capture_particle_frame(World& world)
{
    auto& particles = world.get_array<Particle_Component>().data();
    auto& entities = world.get_array<Particle_Component>().entities();
    if (particles.size() != entities.size())
        throw std::runtime_error("Particle frame capture found mismatched particle and entity arrays");

    Particle_Frame_Snapshot snapshot;
    snapshot.reserve(particles.size());
    for (std::size_t i = 0; i < particles.size(); ++i)
    {
        Particle_Component const& particle = particles[i];
        snapshot.push_back({
          .entity = static_cast<std::uint32_t>(entities[i]),
          .radius = particle.radius,
          .position = particle.position,
          .velocity = particle.velocity,
        });
    }
    return snapshot;
}

Particle_Frame_Writer::Particle_Frame_Writer(std::filesystem::path const& path, Particle_Frame_Export_Config const& config) : path_(path)
{
    static_assert(std::endian::native == std::endian::little, "Particle frame export currently requires a little-endian platform");

    if (config.fps <= 0.0)
        throw std::invalid_argument("Particle frame FPS must be positive");

    if (!path_.parent_path().empty())
        std::filesystem::create_directories(path_.parent_path());

    stream_.open(path_, std::ios::binary | std::ios::trunc);
    if (!stream_)
        throw std::runtime_error("Failed to open particle frame export: " + path_.string());

    stream_.write(PARTICLE_FRAME_MAGIC.data(), static_cast<std::streamsize>(PARTICLE_FRAME_MAGIC.size()));
    write_scalar(PARTICLE_FRAME_VERSION);
    write_scalar(static_cast<std::uint32_t>(config.dimension));
    frame_count_position_ = stream_.tellp();
    write_scalar(frame_count_);
    write_scalar(PARTICLE_RECORD_SIZE);
    write_scalar(config.fps);
    write_scalar(config.domain_width);
    write_scalar(config.domain_height);
    write_scalar(config.domain_depth);
    write_scalar(config.dx);
    write_scalar(static_cast<std::int32_t>(config.scene));
    write_scalar(static_cast<std::int32_t>(config.solver));
    write_scalar(static_cast<std::int32_t>(config.polypic_modes));
    write_scalar(config.flip_percent);
}

Particle_Frame_Writer::~Particle_Frame_Writer()
{
    close();
}

void Particle_Frame_Writer::write_frame(Particle_Frame_Snapshot const& particles, double frame_time, double simulation_time)
{
    if (closed_ || !stream_)
        throw std::runtime_error("Particle frame export is not writable");

    if (particles.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("Particle frame export particle count exceeds the format limit");
    if (frame_count_ == std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("Particle frame export frame count exceeds the format limit");

    write_scalar(frame_time);
    write_scalar(simulation_time);
    write_scalar(static_cast<std::uint32_t>(particles.size()));
    write_scalar(std::uint32_t{0});

    for (Particle_Frame_Record const& particle : particles)
        write_particle(stream_, particle);

    ++frame_count_;
}

void Particle_Frame_Writer::write_interpolated_frame(Particle_Frame_Snapshot const& previous, Particle_Frame_Snapshot const& current,
                                                     double alpha, double frame_time, double simulation_time)
{
    if (previous.size() != current.size())
    {
        write_frame(alpha < 0.5 ? previous : current, frame_time, simulation_time);
        return;
    }

    alpha = std::clamp(alpha, 0.0, 1.0);
    Particle_Frame_Snapshot interpolated;
    interpolated.reserve(current.size());
    for (std::size_t i = 0; i < current.size(); ++i)
    {
        if (previous[i].entity != current[i].entity)
        {
            write_frame(alpha < 0.5 ? previous : current, frame_time, simulation_time);
            return;
        }

        interpolated.push_back({
          .entity = current[i].entity,
          .radius = static_cast<float>((1.0 - alpha) * previous[i].radius + alpha * current[i].radius),
          .position = (1.0 - alpha) * previous[i].position + alpha * current[i].position,
          .velocity = (1.0 - alpha) * previous[i].velocity + alpha * current[i].velocity,
        });
    }

    write_frame(interpolated, frame_time, simulation_time);
}

void Particle_Frame_Writer::close()
{
    if (closed_)
        return;

    if (stream_)
    {
        std::streampos const end_position = stream_.tellp();
        stream_.seekp(frame_count_position_);
        write_scalar(frame_count_);
        stream_.seekp(end_position);
        stream_.close();
    }

    closed_ = true;
}
