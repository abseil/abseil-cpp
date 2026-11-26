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

#include "absl/log/internal/log_message.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <ios>
#include <ostream>
#include <string>
#include <string_view>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/strerror.h"
#include "absl/base/internal/sysinfo.h"
#include "absl/base/log_severity.h"
#include "absl/base/nullability.h"
#include "absl/container/inlined_vector.h"
#include "absl/debugging/internal/examine_stack.h"
#include "absl/log/globals.h"
#include "absl/log/internal/append_truncated.h"
#include "absl/log/internal/globals.h"
#include "absl/log/internal/log_format.h"
#include "absl/log/internal/log_sink_set.h"
#include "absl/log/internal/nullguard.h"
#include "absl/log/internal/proto.h"
#include "absl/log/internal/structured_proto.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/internal/utf8.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "absl/types/span.h"

#ifndef _WIN32
#include <unistd.h>
#endif

extern "C" ABSL_ATTRIBUTE_WEAK void ABSL_INTERNAL_C_SYMBOL(
    AbslInternalOnFatalLogMessage)(const absl::LogEntry&) {
  // Default - Do nothing
}

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

namespace {
// message `logging.proto.Event`
enum EventTag : uint8_t {
  kFileName = 2,
  kFileLine = 3,
  kTimeNsecs = 4,
  kSeverity = 5,
  kThreadId = 6,
  kValue = 7,
  kSequenceNumber = 9,
  kThreadName = 10,
};

// message `logging.proto.Value`
enum ValueTag : uint8_t {
  kString = 1,
  kStringLiteral = 6,
};

// Decodes a `logging.proto.Value` from `buf` and writes a string representation
// into `dst`.  The string representation will be truncated if `dst` is not
// large enough to hold it.  Returns false if `dst` has size zero or one (i.e.
// sufficient only for a nul-terminator) and no decoded data could be written.
// This function may or may not write a nul-terminator into `dst`, and it may or
// may not truncate the data it writes in order to do make space for that nul
// terminator.  In any case, `dst` will be advanced to point at the byte where
// subsequent writes should begin.
bool PrintValue(absl::Span<char>& dst, absl::Span<const char> buf) {
  if (dst.size() <= 1) return false;
  ProtoField field;
  while (field.DecodeFrom(&buf)) {
    switch (field.tag()) {
      case ValueTag::kString:
      case ValueTag::kStringLiteral:
        if (field.type() == WireType::kLengthDelimited)
          if (log_internal::AppendTruncated(field.string_value(), dst) <
              field.string_value().size())
            return false;
    }
  }
  return true;
}

// See `logging.proto.Severity`
int32_t ProtoSeverity(absl::LogSeverity severity, int verbose_level) {
  switch (severity) {
    case absl::LogSeverity::kInfo:
      if (verbose_level == absl::LogEntry::kNoVerbosityLevel) return 800;
      return 600 - verbose_level;
    case absl::LogSeverity::kWarning:
      return 900;
    case absl::LogSeverity::kError:
      return 950;
    case absl::LogSeverity::kFatal:
      return 1100;
    default:
      return 800;
  }
}

absl::string_view Basename(absl::string_view filepath) {
#ifdef _WIN32
  size_t path = filepath.find_last_of("/\\");
#else
  size_t path = filepath.find_last_of('/');
#endif
  if (path != filepath.npos) filepath.remove_prefix(path + 1);
  return filepath;
}

void WriteToString(const char* data, void* str) {
  reinterpret_cast<std::string*>(str)->append(data);
}
void WriteToStream(const char* data, void* os) {
  auto* cast_os = static_cast<std::ostream*>(os);
  *cast_os << data;
}
}  // namespace

struct LogMessage::LogMessageData final {
  LogMessageData(absl::string_view file, int line, absl::LogSeverity severity,
                 absl::Time timestamp, int saved_errno_in);
  LogMessageData(const LogMessageData&) = delete;
  LogMessageData& operator=(const LogMessageData&) = delete;

  // `LogEntry` sent to `LogSink`s; contains metadata.
  absl::LogEntry entry;

  int saved_errno;
  // true => this was first fatal msg
  bool first_fatal;
  // true => all failures should be quiet
  bool fail_quietly;
  // true => PLOG was requested
  bool is_perror;

  // Extra `LogSink`s to log to, in addition to `global_sinks`.
  absl::InlinedVector<absl::LogSink* absl_nonnull, 16> extra_sinks;
  // If true, log to `extra_sinks` but not to `global_sinks` or hardcoded
  // non-sink targets (e.g. stderr, log files).
  bool extra_sinks_only;

  std::ostream manipulated;  // ostream with IO manipulators applied

  // A `logging.proto.Event` proto message is built into `encoded_buf`.
  std::array<char, kLogMessageBufferSize> encoded_buf;
  // `encoded_remaining()` is the suffix of `encoded_buf` that has not been
  // filled yet.  If a datum to be encoded does not fit into
  // `encoded_remaining()` and cannot be truncated to fit, the size of
  // `encoded_remaining()` will be zeroed to prevent encoding of any further
  // data.  Note that in this case its `data()` pointer will not point past the
  // end of `encoded_buf`.
  // The first use of `encoded_remaining()` is our chance to record metadata
  // after any modifications (e.g. by `AtLocation()`) but before any data have
  // been recorded.  We want to record metadata before data so that data are
  // preferentially truncated if we run out of buffer.
  absl::Span<char>& encoded_remaining() {
    if (encoded_remaining_actual_do_not_use_directly.data() == nullptr) {
      encoded_remaining_actual_do_not_use_directly =
          absl::MakeSpan(encoded_buf);
      InitializeEncodingAndFormat();
    }
    return encoded_remaining_actual_do_not_use_directly;
  }
  absl::Span<char> encoded_remaining_actual_do_not_use_directly;

  // A formatted string message is built in `string_buf`.
  std::array<char, kLogMessageBufferSize> string_buf;

  void InitializeEncodingAndFormat();
  void FinalizeEncodingAndFormat();
  bool IsFatal() const;
  void PrepareToDie();
  void Die();
  void SendToLog();
  void LogBacktraceIfNeeded();
};

LogMessage::LogMessageData::LogMessageData(absl::string_view file, int line,
                                           absl::LogSeverity severity,
                                           absl::Time timestamp,
                                           int saved_errno_in)
    : saved_errno(saved_errno_in),
      first_fatal(false),
      fail_quietly(false),
      is_perror(false),
      extra_sinks_only(false),
      manipulated(nullptr) {
  // Legacy defaults for LOG's ostream:
  manipulated.setf(std::ios_base::showbase | std::ios_base::boolalpha);
  entry.full_filename_ = file;
  entry.base_filename_ = Basename(file);
  entry.line_ = line;
  entry.prefix_ = absl::ShouldPrependLogPrefix();
  entry.severity_ = absl::NormalizeLogSeverity(severity);
  entry.verbose_level_ = absl::LogEntry::kNoVerbosityLevel;
  entry.timestamp_ = timestamp;
  entry.tid_ = absl::base_internal::GetCachedTID();
}

void LogMessage::LogMessageData::InitializeEncodingAndFormat() {
  EncodeStringTruncate(EventTag::kFileName, entry.source_filename(),
                       &encoded_remaining());
  EncodeVarint(EventTag::kFileLine, static_cast<int32_t>(entry.source_line()),
               &encoded_remaining());
  EncodeVarint(EventTag::kTimeNsecs, absl::ToUnixNanos(entry.timestamp()),
               &encoded_remaining());
  EncodeVarint(EventTag::kSeverity,
               ProtoSeverity(entry.log_severity(), entry.verbosity()),
               &encoded_remaining());
  EncodeVarint(EventTag::kThreadId, static_cast<uint64_t>(entry.tid()),
               &encoded_remaining());
}

void LogMessage::LogMessageData::FinalizeEncodingAndFormat() {
  // Note that `encoded_remaining()` may have zero size without pointing past
  // the end of `encoded_buf`, so the difference between `data()` pointers is
  // used to compute the size of `encoded_data`.
  absl::Span<const char> encoded_data(
      encoded_buf.data(),
      static_cast<size_t>(encoded_remaining().data() - encoded_buf.data()));
  // `string_remaining` is the suffix of `string_buf` that has not been filled
  // yet.
  absl::Span<char> string_remaining(string_buf);
  // We may need to write a newline and nul-terminator at the end of the decoded
  // string data.  Rather than worry about whether those should overwrite the
  // end of the string (if the buffer is full) or be appended, we avoid writing
  // into the last two bytes so we always have space to append.
  string_remaining.remove_suffix(2);
  entry.prefix_len_ =
      entry.prefix() ? log_internal::FormatLogPrefix(
                           entry.log_severity(), entry.timestamp(), entry.tid(),
                           entry.source_basename(), entry.source_line(),
                           log_internal::ThreadIsLoggingToLogSink()
                               ? PrefixFormat::kRaw
                               : PrefixFormat::kNotRaw,
                           string_remaining)
                     : 0;
  // Decode data from `encoded_buf` until we run out of data or we run out of
  // `string_remaining`.
  ProtoField field;
  while (field.DecodeFrom(&encoded_data)) {
    switch (field.tag()) {
      case EventTag::kValue:
        if (field.type() != WireType::kLengthDelimited) continue;
        if (PrintValue(string_remaining, field.bytes_value())) continue;
        break;
    }
  }
  auto chars_written =
      static_cast<size_t>(string_remaining.data() - string_buf.data());
    string_buf[chars_written++] = '\n';
  string_buf[chars_written++] = '\0';
  entry.text_message_with_prefix_and_newline_and_nul_ =
      absl::MakeSpan(string_buf).subspan(0, chars_written);
}

LogMessage::LogMessageData* absl_nonnull LogMessage::CreateData(
    const char* absl_nonnull file, int line, absl::LogSeverity severity) {
  return CreateData(absl::string_view(file), line, severity);
}
LogMessage::LogMessageData* absl_nonnull LogMessage::CreateData(
    absl::string_view file, int line, absl::LogSeverity severity) {
  int saved_errno = errno;
  auto* data =
      new LogMessageData(file, line, severity, absl::Now(), saved_errno);

  // This logs a backtrace even if the location is subsequently changed using
  // AtLocation.  This quirk, and the behavior when AtLocation is called twice,
  // are fixable but probably not worth fixing.
  data->LogBacktraceIfNeeded();
  return data;
}

LogMessage::LogMessageData* absl_nonnull LogMessage::CreateInfoData(
    const char* absl_nonnull file, int line) {
  return CreateData(file, line, absl::LogSeverity::kInfo);
}
LogMessage::LogMessageData* absl_nonnull LogMessage::CreateWarningData(
    const char* absl_nonnull file, int line) {
  return CreateData(file, line, absl::LogSeverity::kWarning);
}
LogMessage::LogMessageData* absl_nonnull LogMessage::CreateErrorData(
    const char* absl_nonnull file, int line) {
  return CreateData(file, line, absl::LogSeverity::kError);
}

void LogMessage::DestroyData(LogMessageData* absl_nonnull data) {
  int saved_errno = data->saved_errno;
  delete data;
  errno = saved_errno;
}

void LogMessage::AtLocationImpl(LogMessageData* absl_nonnull data,
                                absl::string_view file, int line) {
  data->entry.full_filename_ = file;
  data->entry.base_filename_ = Basename(file);
  data->entry.line_ = line;
  data->LogBacktraceIfNeeded();
}

void LogMessage::NoPrefixImpl(LogMessageData* absl_nonnull data) {
  data->entry.prefix_ = false;
}

void LogMessage::WithVerbosityImpl(LogMessageData* absl_nonnull data,
                                   int verbose_level) {
  if (verbose_level == absl::LogEntry::kNoVerbosityLevel) {
    data->entry.verbose_level_ = absl::LogEntry::kNoVerbosityLevel;
  } else {
    data->entry.verbose_level_ = std::max(0, verbose_level);
  }
}

void LogMessage::WithTimestampImpl(LogMessageData* absl_nonnull data,
                                   absl::Time timestamp) {
  data->entry.timestamp_ = timestamp;
}

void LogMessage::WithThreadIDImpl(LogMessageData* absl_nonnull data,
                                  absl::LogEntry::tid_t tid) {
  data->entry.tid_ = tid;
}

void LogMessage::WithMetadataFromImpl(LogMessageData* absl_nonnull data,
                                      const absl::LogEntry& entry) {
  data->entry.full_filename_ = entry.full_filename_;
  data->entry.base_filename_ = entry.base_filename_;
  data->entry.line_ = entry.line_;
  data->entry.prefix_ = entry.prefix_;
  data->entry.severity_ = entry.severity_;
  data->entry.verbose_level_ = entry.verbose_level_;
  data->entry.timestamp_ = entry.timestamp_;
  data->entry.tid_ = entry.tid_;
}

void LogMessage::WithPerrorImpl(LogMessageData* absl_nonnull data) {
  data->is_perror = true;
}

void LogMessage::ToSinkAlsoImpl(LogMessageData* absl_nonnull data,
                                absl::LogSink* absl_nonnull sink) {
  ABSL_INTERNAL_CHECK(sink, "null LogSink*");
  data->extra_sinks.push_back(sink);
}

void LogMessage::ToSinkOnlyImpl(LogMessageData* absl_nonnull data,
                                absl::LogSink* absl_nonnull sink) {
  ABSL_INTERNAL_CHECK(sink, "null LogSink*");
  data->extra_sinks.clear();
  data->extra_sinks.push_back(sink);
  data->extra_sinks_only = true;
}

#ifdef __ELF__
extern "C" void __gcov_dump() ABSL_ATTRIBUTE_WEAK;
extern "C" void __gcov_flush() ABSL_ATTRIBUTE_WEAK;
#endif

void LogMessage::FailWithoutStackTrace() {
  // Now suppress repeated trace logging:
  log_internal::SetSuppressSigabortTrace(true);
#if defined _DEBUG && defined COMPILER_MSVC
  // When debugging on windows, avoid the obnoxious dialog.
  __debugbreak();
#endif

#ifdef __ELF__
  // For b/8737634, flush coverage if we are in coverage mode.
  if (&__gcov_dump != nullptr) {
    __gcov_dump();
  } else if (&__gcov_flush != nullptr) {
    __gcov_flush();
  }
#endif

  abort();
}

void LogMessage::FailQuietly() {
  // _exit. Calling abort() would trigger all sorts of death signal handlers
  // and a detailed stack trace. Calling exit() would trigger the onexit
  // handlers, including the heap-leak checker, which is guaranteed to fail in
  // this case: we probably just new'ed the std::string that we logged.
  // Anyway, if you're calling Fail or FailQuietly, you're trying to bail out
  // of the program quickly, and it doesn't make much sense for FailQuietly to
  // offer different guarantees about exit behavior than Fail does. (And as a
  // consequence for QCHECK and CHECK to offer different exit behaviors)
  _exit(1);
}

void LogMessage::StreamNullWCharPtr(LogMessageData* absl_nonnull data,
                                    const wchar_t* absl_nullable v) {
  if (v == nullptr) {
    CopyToEncodedBufferImpl<StringType::kNotLiteral>(
        data, absl::string_view(kCharNull.data(), kCharNull.size() - 1));
  } else {
    CopyToEncodedBufferImpl<StringType::kNotLiteral>(data, v);
  }
}

void LogMessage::StreamSourceLocation(LogMessageData* absl_nonnull data,
                                      const absl::SourceLocation& loc) {
  OstreamView view(*data);
  view.stream() << loc.file_name() << ':' << loc.line();
}

// NOLINTBEGIN(runtime/int)
// NOLINTBEGIN(google-runtime-int)
template void LogMessage::StreamData(LogMessageData* absl_nonnull data, char v);
template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                     signed char v);
template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                     unsigned char v);
template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                     short v);
template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                     unsigned short v);
template void LogMessage::StreamData(LogMessageData* absl_nonnull data, int v);
template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                     unsigned int v);
template void LogMessage::StreamData(LogMessageData* absl_nonnull data, long v);
template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                     unsigned long v);
template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                     long long v);
template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                     unsigned long long v);
template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                     void* absl_nullable v);
template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                     const void* absl_nullable v);
template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                     float v);
template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                     double v);
template void LogMessage::StreamData(LogMessageData* absl_nonnull data, bool v);
// NOLINTEND(google-runtime-int)
// NOLINTEND(runtime/int)
template void LogMessage::StreamData(
    LogMessageData* absl_nonnull data,
    std::ostream& (*absl_nonnull m)(std::ostream& os));
template void LogMessage::StreamData(
    LogMessageData* absl_nonnull data,
    std::ios_base& (*absl_nonnull m)(std::ios_base& os));

void LogMessage::FlushData(LogMessageData* absl_nonnull data) {
  if (data->entry.log_severity() >= absl::MinLogLevel()) {
    if (data->is_perror) {
      CopyToEncodedBufferImpl<StringType::kLiteral>(data,
                                                    absl::string_view(": "));
      CopyToEncodedBufferImpl<StringType::kNotLiteral>(
          data, absl::base_internal::StrError(data->saved_errno));
      CopyToEncodedBufferImpl<StringType::kLiteral>(data,
                                                    absl::string_view(" ["));
      StreamData<int>(data, data->saved_errno);
      CopyToEncodedBufferImpl<StringType::kLiteral>(data,
                                                    absl::string_view("]"));
    }

    // Have we already seen a fatal message?
    ABSL_CONST_INIT static std::atomic<bool> seen_fatal(false);
    if (data->entry.log_severity() == absl::LogSeverity::kFatal &&
        absl::log_internal::ExitOnDFatal()) {
      // Exactly one LOG(FATAL) message is responsible for aborting the process,
      // even if multiple threads LOG(FATAL) concurrently.
      bool expected_seen_fatal = false;
      if (seen_fatal.compare_exchange_strong(expected_seen_fatal, true,
                                             std::memory_order_relaxed)) {
        data->first_fatal = true;
      }
    }

    data->FinalizeEncodingAndFormat();
    data->entry.encoding_ =
        absl::string_view(data->encoded_buf.data(),
                          static_cast<size_t>(data->encoded_remaining().data() -
                                              data->encoded_buf.data()));
    data->SendToLog();
  }
}

LogMessage::OstreamView::OstreamView(LogMessageData& message_data)
    : data_(message_data), encoded_remaining_copy_(data_.encoded_remaining()) {
  // This constructor sets the `streambuf` up so that streaming into an attached
  // ostream encodes string data in-place.  To do that, we write appropriate
  // headers into the buffer using a copy of the buffer view so that we can
  // decide not to keep them later if nothing is ever streamed in.  We don't
  // know how much data we'll get, but we can use the size of the remaining
  // buffer as an upper bound and fill in the right size once we know it.
  message_start_ =
      EncodeMessageStart(EventTag::kValue, encoded_remaining_copy_.size(),
                         &encoded_remaining_copy_);
  string_start_ =
      EncodeMessageStart(ValueTag::kString, encoded_remaining_copy_.size(),
                         &encoded_remaining_copy_);
  setp(encoded_remaining_copy_.data(),
       encoded_remaining_copy_.data() + encoded_remaining_copy_.size());
  data_.manipulated.rdbuf(this);
}

LogMessage::OstreamView::~OstreamView() {
  data_.manipulated.rdbuf(nullptr);
  if (!string_start_.data()) {
    // The second field header didn't fit.  Whether the first one did or not, we
    // shouldn't commit `encoded_remaining_copy_`, and we also need to zero the
    // size of `data_->encoded_remaining()` so that no more data are encoded.
    data_.encoded_remaining().remove_suffix(data_.encoded_remaining().size());
    return;
  }
  const absl::Span<const char> contents(pbase(),
                                        static_cast<size_t>(pptr() - pbase()));
  if (contents.empty()) return;
  encoded_remaining_copy_.remove_prefix(contents.size());
  EncodeMessageLength(string_start_, &encoded_remaining_copy_);
  EncodeMessageLength(message_start_, &encoded_remaining_copy_);
  data_.encoded_remaining() = encoded_remaining_copy_;
}

std::ostream& LogMessage::OstreamView::stream() { return data_.manipulated; }

bool LogMessage::LogMessageData::IsFatal() const {
  return entry.log_severity() == absl::LogSeverity::kFatal &&
         absl::log_internal::ExitOnDFatal();
}

void LogMessage::LogMessageData::PrepareToDie() {
  // If we log a FATAL message, flush all the log destinations, then toss
  // a signal for others to catch. We leave the logs in a state that
  // someone else can use them (as long as they flush afterwards)
  if (first_fatal) {
    // Notify observers about the upcoming fatal error.
    ABSL_INTERNAL_C_SYMBOL(AbslInternalOnFatalLogMessage)(entry);
  }

  if (!fail_quietly) {
    // Log the message first before we start collecting stack trace.
    log_internal::LogToSinks(entry, absl::MakeSpan(extra_sinks),
                             extra_sinks_only);

    // `DumpStackTrace` generates an empty string under MSVC.
    // Adding the constant prefix here simplifies testing.
    entry.stacktrace_ = "*** Check failure stack trace: ***\n";
    debugging_internal::DumpStackTrace(
        0, log_internal::MaxFramesInLogStackTrace(),
        log_internal::ShouldSymbolizeLogStackTrace(), WriteToString,
        &entry.stacktrace_);
  }
}

void LogMessage::LogMessageData::Die() {
  absl::FlushLogSinks();

  if (fail_quietly) {
    FailQuietly();
  } else {
    FailWithoutStackTrace();
  }
}

void LogMessage::LogMessageData::SendToLog() {
  if (IsFatal()) PrepareToDie();
  // Also log to all registered sinks, even if OnlyLogToStderr() is set.
  log_internal::LogToSinks(entry, absl::MakeSpan(extra_sinks),
                           extra_sinks_only);
  if (IsFatal()) Die();
}

void LogMessage::LogMessageData::LogBacktraceIfNeeded() {
  if (!absl::log_internal::IsInitialized()) return;

  if (!absl::log_internal::ShouldLogBacktraceAt(entry.source_basename(),
                                                entry.source_line()))
    return;
  OstreamView view(*this);
  view.stream() << " (stacktrace:\n";
  debugging_internal::DumpStackTrace(
      1, log_internal::MaxFramesInLogStackTrace(),
      log_internal::ShouldSymbolizeLogStackTrace(), WriteToStream,
      &view.stream());
  view.stream() << ") ";
}

// Encodes into `data->encoded_remaining()` a partial `logging.proto.Event`
// containing the specified string data using a `Value` field appropriate to
// `str_type`.  Truncates `str` if necessary, but emits nothing and marks the
// buffer full if  even the field headers do not fit.
template <LogMessage::StringType str_type>
ABSL_ATTRIBUTE_NOINLINE void LogMessage::CopyToEncodedBufferImpl(
    LogMessageData* absl_nonnull data, absl::string_view str) {
  auto encoded_remaining_copy = data->encoded_remaining();
  constexpr uint8_t tag_value = str_type == StringType::kLiteral
                                    ? ValueTag::kStringLiteral
                                    : ValueTag::kString;
  auto start = EncodeMessageStart(
      EventTag::kValue,
      BufferSizeFor(tag_value, WireType::kLengthDelimited) + str.size(),
      &encoded_remaining_copy);
  // If the `logging.proto.Event.value` field header did not fit,
  // `EncodeMessageStart` will have zeroed `encoded_remaining_copy`'s size and
  // `EncodeStringTruncate` will fail too.
  if (EncodeStringTruncate(tag_value, str, &encoded_remaining_copy)) {
    // The string may have been truncated, but the field header fit.
    EncodeMessageLength(start, &encoded_remaining_copy);
    data->encoded_remaining() = encoded_remaining_copy;
  } else {
    // The field header(s) did not fit; zero `encoded_remaining()` so we don't
    // write anything else later.
    data->encoded_remaining().remove_suffix(data->encoded_remaining().size());
  }
}
template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kLiteral>(
    LogMessageData* absl_nonnull data, absl::string_view str);
template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kNotLiteral>(
    LogMessageData* absl_nonnull data, absl::string_view str);

template <LogMessage::StringType str_type>
ABSL_ATTRIBUTE_NOINLINE void LogMessage::CopyToEncodedBufferImpl(
    LogMessageData* absl_nonnull data, const char* absl_nonnull str) {
  CopyToEncodedBufferImpl<str_type>(data, absl::string_view(str));
}
template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kLiteral>(
    LogMessageData* absl_nonnull data, const char* absl_nonnull str);
template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kNotLiteral>(
    LogMessageData* absl_nonnull data, const char* absl_nonnull str);

template <LogMessage::StringType str_type>
ABSL_ATTRIBUTE_NOINLINE void LogMessage::CopyToEncodedBufferImpl(
    LogMessageData* absl_nonnull data, const std::string& str) {
  CopyToEncodedBufferImpl<str_type>(data, absl::string_view(str));
}
template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kLiteral>(
    LogMessageData* absl_nonnull data, const std::string& str);
template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kNotLiteral>(
    LogMessageData* absl_nonnull data, const std::string& str);

template <LogMessage::StringType str_type>
ABSL_ATTRIBUTE_NOINLINE void LogMessage::CopyToEncodedBufferImpl(
    LogMessageData* absl_nonnull data, char ch, size_t num) {
  auto encoded_remaining_copy = data->encoded_remaining();
  constexpr uint8_t tag_value = str_type == StringType::kLiteral
                                    ? ValueTag::kStringLiteral
                                    : ValueTag::kString;
  auto value_start = EncodeMessageStart(
      EventTag::kValue,
      BufferSizeFor(tag_value, WireType::kLengthDelimited) + num,
      &encoded_remaining_copy);
  auto str_start = EncodeMessageStart(tag_value, num, &encoded_remaining_copy);
  if (str_start.data()) {
    // The field headers fit.
    log_internal::AppendTruncated(ch, num, encoded_remaining_copy);
    EncodeMessageLength(str_start, &encoded_remaining_copy);
    EncodeMessageLength(value_start, &encoded_remaining_copy);
    data->encoded_remaining() = encoded_remaining_copy;
  } else {
    // The field header(s) did not fit; zero `encoded_remaining()` so we don't
    // write anything else later.
    data->encoded_remaining().remove_suffix(data->encoded_remaining().size());
  }
}
template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kLiteral>(
    LogMessageData* absl_nonnull data, char ch, size_t num);
template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kNotLiteral>(
    LogMessageData* absl_nonnull data, char ch, size_t num);

template <LogMessage::StringType str_type>
ABSL_ATTRIBUTE_NOINLINE void LogMessage::CopyToEncodedBufferImpl(
    LogMessageData* absl_nonnull data, std::wstring_view str) {
  auto encoded_remaining_copy = data->encoded_remaining();
  constexpr uint8_t tag_value = str_type == StringType::kLiteral
                                    ? ValueTag::kStringLiteral
                                    : ValueTag::kString;
  size_t max_str_byte_length =
      absl::strings_internal::kMaxEncodedUTF8Size * str.length();
  auto value_start =
      EncodeMessageStart(EventTag::kValue,
                         BufferSizeFor(tag_value, WireType::kLengthDelimited) +
                             max_str_byte_length,
                         &encoded_remaining_copy);
  auto str_start = EncodeMessageStart(tag_value, max_str_byte_length,
                                      &encoded_remaining_copy);
  if (str_start.data()) {
    log_internal::AppendTruncated(str, encoded_remaining_copy);
    EncodeMessageLength(str_start, &encoded_remaining_copy);
    EncodeMessageLength(value_start, &encoded_remaining_copy);
    data->encoded_remaining() = encoded_remaining_copy;
  } else {
    // The field header(s) did not fit; zero `encoded_remaining()` so we don't
    // write anything else later.
    data->encoded_remaining().remove_suffix(data->encoded_remaining().size());
  }
}
template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kLiteral>(
    LogMessageData* absl_nonnull data, std::wstring_view str);
template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kNotLiteral>(
    LogMessageData* absl_nonnull data, std::wstring_view str);

template void LogMessage::CopyToEncodedBufferWithStructuredProtoFieldImpl<
    LogMessage::StringType::kLiteral>(LogMessageData* absl_nonnull data,
                                      StructuredProtoField field,
                                      absl::string_view str);
template void LogMessage::CopyToEncodedBufferWithStructuredProtoFieldImpl<
    LogMessage::StringType::kNotLiteral>(LogMessageData* absl_nonnull data,
                                         StructuredProtoField field,
                                         absl::string_view str);

template <LogMessage::StringType str_type>
ABSL_ATTRIBUTE_NOINLINE void
LogMessage::CopyToEncodedBufferWithStructuredProtoFieldImpl(
    LogMessageData* absl_nonnull data, StructuredProtoField field,
    absl::string_view str) {
  auto encoded_remaining_copy = data->encoded_remaining();
  size_t encoded_field_size = BufferSizeForStructuredProtoField(field);
  constexpr uint8_t tag_value = str_type == StringType::kLiteral
                                    ? ValueTag::kStringLiteral
                                    : ValueTag::kString;
  auto start = EncodeMessageStart(
      EventTag::kValue,
      encoded_field_size +
          BufferSizeFor(tag_value, WireType::kLengthDelimited) + str.size(),
      &encoded_remaining_copy);

  // Write the encoded proto field.
  if (!EncodeStructuredProtoField(field, encoded_remaining_copy)) {
    // The header / field will not fit; zero `encoded_remaining()` so we
    // don't write anything else later.
    data->encoded_remaining().remove_suffix(data->encoded_remaining().size());
    return;
  }

  // Write the string, truncating if necessary.
  if (!EncodeStringTruncate(tag_value, str, &encoded_remaining_copy)) {
    // The length of the string itself did not fit; zero `encoded_remaining()`
    // so the value is not encoded at all.
    data->encoded_remaining().remove_suffix(data->encoded_remaining().size());
    return;
  }

  EncodeMessageLength(start, &encoded_remaining_copy);
  data->encoded_remaining() = encoded_remaining_copy;
}

// We intentionally don't return from these destructors. Disable MSVC's warning
// about the destructor never returning as we do so intentionally here.
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push)
#pragma warning(disable : 4722)
#endif

LogMessage::~LogMessage() { DestroyData(data_); }

LogMessage::LogMessageData* absl_nonnull LogMessage::CreateFatalData(
    const char* absl_nonnull file, int line,
    const char* absl_nonnull failure_msg) {
  LogMessageData* data = CreateData(file, line, absl::LogSeverity::kFatal);
  CopyToEncodedBufferImpl<StringType::kLiteral>(
      data, absl::string_view("Check failed: "));
  CopyToEncodedBufferImpl<StringType::kLiteral>(data, failure_msg);
  CopyToEncodedBufferImpl<StringType::kLiteral>(data, absl::string_view(" "));
  return data;
}

LogMessageFatal::~LogMessageFatal() { FailWithoutStackTrace(); }

LogMessageDebugFatal::~LogMessageDebugFatal() { FailWithoutStackTrace(); }

LogMessage::LogMessageData* absl_nonnull LogMessage::CreateQuietlyFatalData(
    const char* absl_nonnull file, int line) {
  LogMessageData* data = CreateData(file, line, absl::LogSeverity::kFatal);
  data->fail_quietly = true;
  return data;
}

LogMessage::LogMessageData* absl_nonnull LogMessage::CreateQuietlyFatalData(
    const char* absl_nonnull file, int line,
    const char* absl_nonnull failure_msg) {
  LogMessageData* data = CreateFatalData(file, line, failure_msg);
  data->fail_quietly = true;
  return data;
}

LogMessageQuietlyDebugFatal::~LogMessageQuietlyDebugFatal() { FailQuietly(); }

LogMessageQuietlyFatal::~LogMessageQuietlyFatal() { FailQuietly(); }
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#endif

}  // namespace log_internal

ABSL_NAMESPACE_END
}  // namespace absl
