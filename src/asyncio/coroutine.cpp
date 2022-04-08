#include "coroutine.hpp"

namespace asyncio {

thread_local abstract_engine* abstract_engine::current_loop = nullptr;

uint64_t generate_id() {
  static uint64_t next_id = 0;
  return ++ next_id;
}

} // namespace asyncio
