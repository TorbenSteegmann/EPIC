#pragma once
#include <cstdint>

namespace ecs
{
using Entity = uint32_t;

static constexpr Entity invalid_entity = 0;

enum class Registry_Type : uint8_t
{
    Local = 0,
    Remote,
    Server
};
} // namespace ecs
