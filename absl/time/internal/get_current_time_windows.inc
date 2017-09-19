#include "absl/time/clock.h"

#include <chrono>
#include <cstdint>

namespace absl {
namespace time_internal {

static int64_t GetCurrentTimeNanosFromSystem() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now() -
             std::chrono::system_clock::from_time_t(0))
      .count();
}

}  // namespace time_internal
}  // namespace absl
