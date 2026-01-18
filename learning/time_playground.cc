#include <string>

#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace learning {

std::string FormatLocalNow() {
  absl::Time now = absl::Now();
  absl::TimeZone tz = absl::LocalTimeZone();
  return absl::FormatTime("%Y-%m-%d %H:%M:%S", now, tz);
}

absl::Duration BackoffDelay(int attempt) {
  if (attempt <= 0) return absl::ZeroDuration();
  absl::Duration base = absl::Milliseconds(200);
  absl::Duration cap = absl::Seconds(5);
  absl::Duration d = base * attempt;
  if (d > cap) d = cap;
  return d;
}

}  // namespace learning

int main() {
  std::string t = learning::FormatLocalNow();
  absl::Duration d1 = learning::BackoffDelay(1);
  absl::Duration d2 = learning::BackoffDelay(10);
  (void)t;
  (void)d1;
  (void)d2;
  return 0;
}
