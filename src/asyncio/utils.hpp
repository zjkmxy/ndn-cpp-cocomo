#pragma once

#include <functional>
#include <cstdint>
#include <chrono>
#include <thread>

namespace asyncio {

using msec = uint64_t;

struct timer {
  virtual ~timer(){}

  virtual msec now() {
    return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
  }

  virtual void sleep(msec tim) {
    std::this_thread::sleep_for(std::chrono::milliseconds(tim));
  }
};

uint64_t generate_id();

} // namespace asyncio
