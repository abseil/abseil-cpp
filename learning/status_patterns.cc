#include <cctype>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace learning {

absl::StatusOr<int> ParsePositiveInt(const std::string& s) {
  if (s.empty()) return absl::InvalidArgumentError("empty string");

  int v = 0;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return absl::InvalidArgumentError("not a number");
    }
    v = v * 10 + (c - '0');
    if (v > 1000000) return absl::OutOfRangeError("value too large");
  }

  if (v <= 0) return absl::OutOfRangeError("value must be positive");
  return v;
}

absl::Status ValidateRange(int v, int lo, int hi) {
  if (v < lo || v > hi) {
    return absl::OutOfRangeError("value out of allowed range");
  }
  return absl::OkStatus();
}

absl::Status ConfigureRetries(const std::string& retries_text) {
  absl::StatusOr<int> retries = ParsePositiveInt(retries_text);
  if (!retries.ok()) return retries.status();

  absl::Status vr = ValidateRange(*retries, 1, 10);
  if (!vr.ok()) return vr;

  return absl::OkStatus();
}

absl::StatusOr<std::pair<std::string, int>> ParseEndpoint(const std::string& input) {
  auto pos = input.find(':');
  if (pos == std::string::npos) return absl::InvalidArgumentError("missing ':'");

  std::string host = input.substr(0, pos);
  std::string port_text = input.substr(pos + 1);

  if (host.empty()) return absl::InvalidArgumentError("empty host");

  absl::StatusOr<int> port = ParsePositiveInt(port_text);
  if (!port.ok()) return port.status();

  absl::Status in_range = ValidateRange(*port, 1, 65535);
  if (!in_range.ok()) return in_range;

  return std::make_pair(host, *port);
}

absl::StatusOr<std::pair<std::string, int>> ParseEndpointWithContext(const std::string& input) {
  absl::StatusOr<std::pair<std::string, int>> ep = ParseEndpoint(input);
  if (!ep.ok()) {
    std::string msg = "failed to parse endpoint '" + input + "': " + std::string(ep.status().message());
    return absl::InvalidArgumentError(msg);
  }
  return *ep;
}

}  // namespace learning

int main() {
  absl::Status s1 = learning::ConfigureRetries("3");
  absl::Status s2 = learning::ConfigureRetries("0");
  absl::Status s3 = learning::ConfigureRetries("abc");

  auto e1 = learning::ParseEndpoint("localhost:8080");
  auto e2 = learning::ParseEndpoint("localhost:99999");
  auto e3 = learning::ParseEndpointWithContext("bad_endpoint");

  (void)s1;
  (void)s2;
  (void)s3;
  (void)e1;
  (void)e2;
  (void)e3;
  return 0;
}
