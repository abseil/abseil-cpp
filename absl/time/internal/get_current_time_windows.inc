#include "absl/time/clock.h"

#include <chrono>
#include <cstdint>

namespace absl {
inline namespace lts_2018_06_20 {
namespace time_internal {

static int64_t GetCurrentTimeNanosFromSystem() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now() -
             std::chrono::system_clock::from_time_t(0))
      .count();
}

}  // namespace time_internal
}  // inline namespace lts_2018_06_20
}  // namespace absl
