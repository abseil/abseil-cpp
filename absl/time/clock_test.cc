// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/time/clock.h"

#include "absl/base/config.h"
#if defined(ABSL_HAVE_ALARM)
#include <signal.h>
#include <unistd.h>
#elif defined(__linux__) || defined(__APPLE__)
#error all known Linux and Apple targets have alarm
#endif

#include "gtest/gtest.h"
#include "absl/time/time.h"

namespace {

TEST(Time, Now) {
  const absl::Time before = absl::FromUnixNanos(absl::GetCurrentTimeNanos());
  const absl::Time now = absl::Now();
  const absl::Time after = absl::FromUnixNanos(absl::GetCurrentTimeNanos());
  EXPECT_GE(now, before);
  EXPECT_GE(after, now);
}

TEST(SleepForTest, BasicSanity) {
  absl::Duration sleep_time = absl::Milliseconds(2500);
  absl::Time start = absl::Now();
  absl::SleepFor(sleep_time);
  absl::Time end = absl::Now();
  EXPECT_LE(sleep_time - absl::Milliseconds(100), end - start);
  EXPECT_GE(sleep_time + absl::Milliseconds(100), end - start);
}

#ifdef ABSL_HAVE_ALARM
// Helper for test SleepFor.
bool alarm_handler_invoked = false;
void AlarmHandler(int signo) {
  ASSERT_EQ(signo, SIGALRM);
  alarm_handler_invoked = true;
}

TEST(SleepForTest, AlarmSupport) {
  alarm_handler_invoked = false;
  sig_t old_alarm = signal(SIGALRM, AlarmHandler);
  alarm(2);
  absl::Duration sleep_time = absl::Milliseconds(3500);
  absl::Time start = absl::Now();
  absl::SleepFor(sleep_time);
  absl::Time end = absl::Now();
  EXPECT_TRUE(alarm_handler_invoked);
  EXPECT_LE(sleep_time - absl::Milliseconds(100), end - start);
  EXPECT_GE(sleep_time + absl::Milliseconds(100), end - start);
  signal(SIGALRM, old_alarm);
}
#endif  // ABSL_HAVE_ALARM

}  // namespace
