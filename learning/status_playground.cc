#include <cctype>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace learning {

absl::StatusOr<int> ParsePort(const std::string& input) {
  if (input.empty()) {
    return absl::InvalidArgumentError("port is empty");
  }

  int value = 0;
  for (char c : input) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return absl::InvalidArgumentError("port must be numeric");
    }
    value = value * 10 + (c - '0');
    if (value > 65535) {
      return absl::OutOfRangeError("port is out of range");
    }
  }

  if (value == 0) {
    return absl::OutOfRangeError("port must be between 1 and 65535");
  }

  return value;
}

absl::Status ValidateHostPort(const std::string& host, const std::string& port) {
  if (host.empty()) {
    return absl::InvalidArgumentError("host is empty");
  }

  absl::StatusOr<int> parsed = ParsePort(port);
  if (!parsed.ok()) {
    return parsed.status();
  }

  return absl::OkStatus();
}

}  // namespace learning

int main() {
  absl::Status s1 = learning::ValidateHostPort("localhost", "8080");
  absl::Status s2 = learning::ValidateHostPort("", "8080");
  absl::Status s3 = learning::ValidateHostPort("localhost", "99999");
  (void)s1;
  (void)s2;
  (void)s3;
  return 0;
}
