#include "coroutine.hpp"

namespace asyncio {

uint64_t generate_id() {
  static uint64_t next_id = 0;
  return ++ next_id;
}

} // namespace asyncio
