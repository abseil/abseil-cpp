#include "absl/time/clock.h"

#include <sys/time.h>
#include <ctime>
#include <cstdint>

#include "absl/base/internal/raw_logging.h"

// These are not defined in the Xcode 7.3.1 SDK Headers.
// Once we are no longer supporting Xcode 7.3.1 we can
// remove these.
#ifndef __WATCHOS_3_0
#define __WATCHOS_3_0 30000
#endif

#ifndef __TVOS_10_0
#define __TVOS_10_0 100000
#endif

#ifndef __IPHONE_10_0
#define __IPHONE_10_0 100000
#endif

#ifndef __MAC_10_12
#define __MAC_10_12 101200
#endif

namespace absl {
namespace time_internal {

static int64_t GetCurrentTimeNanosFromSystem() {
#if (__MAC_OS_X_VERSION_MAX_ALLOWED >= __MAC_10_12) ||    \
    (__IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_10_0) || \
    (__WATCH_OS_VERSION_MAX_ALLOWED >= __WATCHOS_3_0) ||  \
    (__TV_OS_VERSION_MAX_ALLOWED >= __TVOS_10_0)
  // clock_gettime_nsec_np is not defined on SDKs before Xcode 8.0.
  // This preprocessor logic is based upon __CLOCK_AVAILABILITY in
  // usr/include/time.h. Once we are no longer supporting Xcode 7.3.1 we can
  // remove this #if.
  // We must continue to check if it is defined until we are sure that ALL the
  // platforms we are shipping on support it.
  // clock_gettime_nsec_np is preferred because it may give higher accuracy than
  // gettimeofday in future Apple operating systems.
  // Currently (macOS 10.12/iOS 10.2) clock_gettime_nsec_np accuracy is
  // microsecond accuracy (i.e. equivalent to gettimeofday).
  if (&clock_gettime_nsec_np != nullptr) {
    return clock_gettime_nsec_np(CLOCK_REALTIME);
  }
#endif
#if (defined(__MAC_OS_X_VERSION_MIN_REQUIRED) &&            \
     (__MAC_OS_X_VERSION_MIN_REQUIRED < __MAC_10_12)) ||    \
    (defined(__IPHONE_OS_VERSION_MIN_REQUIRED) &&           \
     (__IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_10_0)) || \
    (defined(__WATCH_OS_VERSION_MIN_REQUIRED) &&            \
     (__WATCH_OS_VERSION_MIN_REQUIRED < __WATCHOS_3_0)) ||  \
    (defined(__TV_OS_VERSION_MIN_REQUIRED) &&               \
     (__TV_OS_VERSION_MIN_REQUIRED < __TVOS_10_0))
  // We need this block in 2 different cases:
  // a) where we are compiling with Xcode 7 in which case the block above
  //    will not be compiled in, and this is the only block executed.
  // b) where we are compiling with Xcode 8+ but supporting operating systems
  //    that do not define clock_gettime_nsec_np, so this is in effect
  //    an else block to the block above.
  // This block will not be compiled if the min supported version is
  // guaranteed to supply clock_gettime_nsec_np.
  //
  // Once we know that clock_gettime_nsec_np is in the SDK *AND* exists on
  // all the platforms we support, we can remove both this block and alter the
  // block above to just call clock_gettime_nsec_np directly.
  const int64_t kNanosPerSecond = 1000 * 1000 * 1000;
  const int64_t kNanosPerMicrosecond = 1000;
  struct timeval tp;
  ABSL_RAW_CHECK(gettimeofday(&tp, nullptr) == 0, "Failed gettimeofday");
  return (int64_t{tp.tv_sec} * kNanosPerSecond +
          int64_t{tp.tv_usec} * kNanosPerMicrosecond);
#endif
}

}  // namespace time_internal
}  // namespace absl
