#pragma once

#include <functional>
#include <cstdint>

namespace asyncio {

using msec = uint64_t;

using timer = std::function<msec()>;

uint64_t generate_id();

} // namespace asyncio
