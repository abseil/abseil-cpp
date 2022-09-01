//
// Copyright 2022 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/log/internal/flags.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/log/globals.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/log/internal/test_matchers.h"
#include "absl/log/log.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/strings/str_cat.h"

namespace {
using ::absl::log_internal::TextMessage;

using ::testing::HasSubstr;
using ::testing::Not;

auto* test_env ABSL_ATTRIBUTE_UNUSED = ::testing::AddGlobalTestEnvironment(
    new absl::log_internal::LogTestEnvironment);

constexpr static absl::LogSeverityAtLeast DefaultStderrThreshold() {
  return absl::LogSeverityAtLeast::kError;
}

class LogFlagsTest : public ::testing::Test {
 protected:
  absl::FlagSaver flag_saver_;
};

TEST_F(LogFlagsTest, StderrKnobsDefault) {
  EXPECT_EQ(absl::StderrThreshold(), DefaultStderrThreshold());
}

TEST_F(LogFlagsTest, SetStderrThreshold) {
  // Verify that the API and the flag agree.
  EXPECT_EQ(absl::GetFlag(FLAGS_stderrthreshold),
            static_cast<int>(absl::StderrThreshold()));

  // Verify that setting the flag changes the value at the API level.
  for (const absl::LogSeverityAtLeast level :
       {absl::LogSeverityAtLeast::kInfo, absl::LogSeverityAtLeast::kError,
        absl::LogSeverityAtLeast::kInfinity}) {
    absl::SetFlag(&FLAGS_stderrthreshold, static_cast<int>(level));

    EXPECT_EQ(absl::StderrThreshold(), level);
  }

  // Verify that setting the value through the API changes the flag value
  // as well.
  for (const absl::LogSeverityAtLeast level :
       {absl::LogSeverityAtLeast::kInfo, absl::LogSeverityAtLeast::kError,
        absl::LogSeverityAtLeast::kInfinity}) {
    absl::SetStderrThreshold(level);

    EXPECT_EQ(absl::GetFlag(FLAGS_stderrthreshold), static_cast<int>(level));
  }

  // Verify that the scoped API changes both the API and the flag.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kWarning);

  for (const absl::LogSeverityAtLeast level :
       {absl::LogSeverityAtLeast::kInfo, absl::LogSeverityAtLeast::kError,
        absl::LogSeverityAtLeast::kInfinity}) {
    absl::ScopedStderrThreshold scoped_threshold(level);

    EXPECT_EQ(absl::StderrThreshold(), level);
    EXPECT_EQ(absl::GetFlag(FLAGS_stderrthreshold), static_cast<int>(level));
  }

  // ...and that going out of scope restores both.
  EXPECT_EQ(absl::StderrThreshold(), absl::LogSeverityAtLeast::kWarning);
  EXPECT_EQ(absl::GetFlag(FLAGS_stderrthreshold),
            static_cast<int>(absl::LogSeverityAtLeast::kWarning));
}

TEST_F(LogFlagsTest, SetMinLogLevel) {
  // Verify that the API and the flag agree.
  EXPECT_EQ(absl::GetFlag(FLAGS_minloglevel),
            static_cast<int>(absl::MinLogLevel()));

  // Verify that setting the flag changes the value at the API level.
  for (const absl::LogSeverityAtLeast level :
       {absl::LogSeverityAtLeast::kInfo, absl::LogSeverityAtLeast::kError,
        absl::LogSeverityAtLeast::kInfinity}) {
    absl::SetFlag(&FLAGS_minloglevel, static_cast<int>(level));

    EXPECT_EQ(absl::MinLogLevel(), level);
  }

  // Verify that setting the value through the API changes the flag value
  // as well.
  for (const absl::LogSeverityAtLeast level :
       {absl::LogSeverityAtLeast::kInfo, absl::LogSeverityAtLeast::kError,
        absl::LogSeverityAtLeast::kInfinity}) {
    absl::SetMinLogLevel(level);

    EXPECT_EQ(absl::GetFlag(FLAGS_minloglevel), static_cast<int>(level));
  }

  // Verify that the scoped API changes both the API and the flag.
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kWarning);

  for (const absl::LogSeverityAtLeast level :
       {absl::LogSeverityAtLeast::kInfo, absl::LogSeverityAtLeast::kError,
        absl::LogSeverityAtLeast::kInfinity}) {
    absl::log_internal::ScopedMinLogLevel scoped_threshold(level);

    EXPECT_EQ(absl::MinLogLevel(), level);
    EXPECT_EQ(absl::GetFlag(FLAGS_minloglevel), static_cast<int>(level));
  }

  // ...and that going out of scope restores both.
  EXPECT_EQ(absl::MinLogLevel(), absl::LogSeverityAtLeast::kWarning);
  EXPECT_EQ(absl::GetFlag(FLAGS_minloglevel),
            static_cast<int>(absl::LogSeverityAtLeast::kWarning));
}

TEST_F(LogFlagsTest, PrependLogPrefix) {
  // Verify that the API and the flag agree.
  EXPECT_EQ(absl::GetFlag(FLAGS_log_prefix), absl::ShouldPrependLogPrefix());

  // Verify that setting the flag changes the value at the API level.
  for (const bool value : {false, true}) {
    absl::SetFlag(&FLAGS_log_prefix, value);
    EXPECT_EQ(absl::ShouldPrependLogPrefix(), value);
  }

  // Verify that setting the value through the API changes the flag.
  for (const bool value : {false, true}) {
    absl::EnableLogPrefix(value);
    EXPECT_EQ(absl::GetFlag(FLAGS_log_prefix), value);
  }
}

TEST_F(LogFlagsTest, EmptyBacktraceAtFlag) {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  absl::SetFlag(&FLAGS_log_backtrace_at, "");
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(TextMessage(Not(HasSubstr("(stacktrace:")))));

  test_sink.StartCapturingLogs();
  LOG(INFO) << "hello world";
}

TEST_F(LogFlagsTest, BacktraceAtNonsense) {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  absl::SetFlag(&FLAGS_log_backtrace_at, "gibberish");
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(TextMessage(Not(HasSubstr("(stacktrace:")))));

  test_sink.StartCapturingLogs();
  LOG(INFO) << "hello world";
}

TEST_F(LogFlagsTest, BacktraceAtWrongFile) {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  const int log_line = __LINE__ + 1;
  auto do_log = [] { LOG(INFO) << "hello world"; };
  absl::SetFlag(&FLAGS_log_backtrace_at,
                absl::StrCat("some_other_file.cc:", log_line));
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(TextMessage(Not(HasSubstr("(stacktrace:")))));

  test_sink.StartCapturingLogs();
  do_log();
}

TEST_F(LogFlagsTest, BacktraceAtWrongLine) {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  const int log_line = __LINE__ + 1;
  auto do_log = [] { LOG(INFO) << "hello world"; };
  absl::SetFlag(&FLAGS_log_backtrace_at,
                absl::StrCat("flags_test.cc:", log_line + 1));
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(TextMessage(Not(HasSubstr("(stacktrace:")))));

  test_sink.StartCapturingLogs();
  do_log();
}

TEST_F(LogFlagsTest, BacktraceAtWholeFilename) {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  const int log_line = __LINE__ + 1;
  auto do_log = [] { LOG(INFO) << "hello world"; };
  absl::SetFlag(&FLAGS_log_backtrace_at, absl::StrCat(__FILE__, ":", log_line));
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(TextMessage(Not(HasSubstr("(stacktrace:")))));

  test_sink.StartCapturingLogs();
  do_log();
}

TEST_F(LogFlagsTest, BacktraceAtNonmatchingSuffix) {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  const int log_line = __LINE__ + 1;
  auto do_log = [] { LOG(INFO) << "hello world"; };
  absl::SetFlag(&FLAGS_log_backtrace_at,
                absl::StrCat("flags_test.cc:", log_line, "gibberish"));
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(TextMessage(Not(HasSubstr("(stacktrace:")))));

  test_sink.StartCapturingLogs();
  do_log();
}

TEST_F(LogFlagsTest, LogsBacktrace) {
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  const int log_line = __LINE__ + 1;
  auto do_log = [] { LOG(INFO) << "hello world"; };
  absl::SetFlag(&FLAGS_log_backtrace_at,
                absl::StrCat("flags_test.cc:", log_line));
  absl::ScopedMockLog test_sink(absl::MockLogDefault::kDisallowUnexpected);

  EXPECT_CALL(test_sink, Send(TextMessage(HasSubstr("(stacktrace:"))));

  test_sink.StartCapturingLogs();
  do_log();
}

}  // namespace
