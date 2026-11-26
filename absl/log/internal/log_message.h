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
//
// -----------------------------------------------------------------------------
// File: log/internal/log_message.h
// -----------------------------------------------------------------------------
//
// This file declares `class absl::log_internal::LogMessage`. This class more or
// less represents a particular log message. LOG/CHECK macros create a temporary
// instance of `LogMessage` and then stream values to it.  At the end of the
// LOG/CHECK statement, the LogMessage is voidified by operator&&, and `Flush()`
// directs the message to the registered log sinks.  Heap-allocation of
// `LogMessage` is unsupported.  Construction outside of a `LOG` macro is
// unsupported.

#ifndef ABSL_LOG_INTERNAL_LOG_MESSAGE_H_
#define ABSL_LOG_INTERNAL_LOG_MESSAGE_H_

#include <wchar.h>

#include <cstddef>
#include <ios>
#include <ostream>
#include <streambuf>
#include <string>
#include <string_view>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/log/internal/nullguard.h"
#include "absl/log/internal/structured_proto.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "absl/strings/has_absl_stringify.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {
constexpr int kLogMessageBufferSize = 15000;

enum class StructuredStringType;

class LogMessage {
 public:
  struct InfoTag {};
  struct WarningTag {};
  struct ErrorTag {};

  // Used for `LOG`.  Taking `const char *` instead of `string_view` keeps
  // callsites a little bit smaller at the cost of doing `strlen` at runtime.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage(const char* absl_nonnull file,
                                          int line, absl::LogSeverity severity)
      : data_(CreateData(file, line, severity)) {}
  // Used for FFI integrations that don't have a NUL-terminated string.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage(absl::string_view file, int line,
                                          absl::LogSeverity severity)
      : data_(CreateData(file, line, severity)) {}
  // These constructors are slightly smaller/faster to call; the severity is
  // curried into the function pointer.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage(const char* absl_nonnull file,
                                          int line, InfoTag)
      : data_(CreateInfoData(file, line)) {}
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage(const char* absl_nonnull file,
                                          int line, WarningTag)
      : data_(CreateWarningData(file, line)) {}
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage(const char* absl_nonnull file,
                                          int line, ErrorTag)
      : data_(CreateErrorData(file, line)) {}
  LogMessage(const LogMessage&) = delete;
  LogMessage& operator=(const LogMessage&) = delete;
  // The message object never escapes and flushing clears the pointer, so the
  // compiler can eliminate this destructor on normal paths.
  ABSL_ATTRIBUTE_ALWAYS_INLINE ~LogMessage() {
    if (ABSL_PREDICT_FALSE(data_ != nullptr)) DestroyData(data_);
  }

  // Methods that return `*this` are inline wrappers around static out-of-line
  // helpers so the compiler knows the returned reference aliases `this` and
  // `this` never escapes.

  // Overrides the location inferred from the callsite.  The string pointed to
  // by `file` must be valid until the end of the statement.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& AtLocation(absl::string_view file,
                                                      int line) {
    AtLocationImpl(data_, file, line);
    return *this;
  }
  // `loc` doesn't default to `absl::SourceLocation::current()` here since the
  // callsite is already the default location for `LOG` statements.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& AtLocation(
      absl::SourceLocation loc) {
    return AtLocation(loc.file_name(), static_cast<int>(loc.line()));
  }
  // Omits the prefix from this line.  The prefix includes metadata about the
  // logged data such as source code location and timestamp.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& NoPrefix() {
    NoPrefixImpl(data_);
    return *this;
  }
  // Sets the verbosity field of the logged message as if it was logged by
  // `VLOG(verbose_level)`.  Unlike `VLOG`, this method does not affect
  // evaluation of the statement when the specified `verbose_level` has been
  // disabled.  The only effect is on `absl::LogSink` implementations which
  // make use of the `absl::LogSink::verbosity()` value.  The value
  // `absl::LogEntry::kNoVerbosityLevel` can be specified to mark the message
  // not verbose.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& WithVerbosity(int verbose_level) {
    WithVerbosityImpl(data_, verbose_level);
    return *this;
  }
  // Uses the specified timestamp instead of one collected in the constructor.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& WithTimestamp(absl::Time timestamp) {
    WithTimestampImpl(data_, timestamp);
    return *this;
  }
  // Uses the specified thread ID instead of one collected in the constructor.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& WithThreadID(
      absl::LogEntry::tid_t tid) {
    WithThreadIDImpl(data_, tid);
    return *this;
  }
  // Copies all metadata (but no data) from the specified `absl::LogEntry`.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& WithMetadataFrom(
      const absl::LogEntry& entry) {
    WithMetadataFromImpl(data_, entry);
    return *this;
  }
  // Appends to the logged message a colon, a space, a textual description of
  // the current value of `errno` (as by strerror(3)), and the numerical value
  // of `errno`.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& WithPerror() {
    WithPerrorImpl(data_);
    return *this;
  }
  // Sends this message to `*sink` in addition to whatever other sinks it would
  // otherwise have been sent to.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& ToSinkAlso(
      absl::LogSink* absl_nonnull sink) {
    ToSinkAlsoImpl(data_, sink);
    return *this;
  }
  // Sends this message to `*sink` and no others.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& ToSinkOnly(
      absl::LogSink* absl_nonnull sink) {
    ToSinkOnlyImpl(data_, sink);
    return *this;
  }

  // Don't call this method from outside this library.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& InternalStream() { return *this; }

  // By-value overloads for small, common types let us overlook common failures
  // to define globals and static data members (i.e. in a .cc file).
  // Values are passed by value to `StreamData` so they stay in registers.
  // NOLINTBEGIN(runtime/int)
  // NOLINTBEGIN(google-runtime-int)
  // clang-format off:  The CUDA toolchain cannot handle these <<<'s
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(char v) {
    StreamData(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(signed char v) {
    StreamData(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(unsigned char v) {
    StreamData(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(signed short v) {
    StreamData(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(signed int v) {
    StreamData(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(signed long v) {
    StreamData(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(signed long long v) {
    StreamData(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(unsigned short v) {
    StreamData(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(unsigned int v) {
    StreamData(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(unsigned long v) {
    StreamData(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(unsigned long long v) {
    StreamData(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(void* absl_nullable v) {
    StreamData(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(
      const void* absl_nullable v) {
    StreamData(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(float v) {
    StreamData(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(double v) {
    StreamData(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(bool v) {
    StreamData(data_, v);
    return *this;
  }
  // clang-format on
  // NOLINTEND(google-runtime-int)
  // NOLINTEND(runtime/int)

  // These overloads are more efficient since no `ostream` is involved.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(const std::string& v) {
    CopyToEncodedBufferImpl<StringType::kNotLiteral>(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(absl::string_view v) {
    CopyToEncodedBufferImpl<StringType::kNotLiteral>(data_, v);
    return *this;
  }

  // Wide string overloads (since std::ostream does not provide them).
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(const std::wstring& v) {
    CopyToEncodedBufferImpl<StringType::kNotLiteral>(data_,
                                                     std::wstring_view(v));
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(std::wstring_view v) {
    CopyToEncodedBufferImpl<StringType::kNotLiteral>(data_, v);
    return *this;
  }
  // `const wchar_t*` is handled by `operator<< <const wchar_t*>`.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(
      wchar_t* absl_nullable v) {
    StreamNullWCharPtr(data_, v);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(wchar_t v) {
    CopyToEncodedBufferImpl<StringType::kNotLiteral>(data_,
                                                     std::wstring_view(&v, 1));
    return *this;
  }

  // Overload for absl::SourceLocation or the std::source_location alias.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(
      const absl::SourceLocation& loc) {
    StreamSourceLocation(data_, loc);
    return *this;
  }

  // Handle stream manipulators e.g. std::endl.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(
      std::ostream& (*absl_nonnull m)(std::ostream& os)) {
    StreamData(data_, m);
    return *this;
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(
      std::ios_base& (*absl_nonnull m)(std::ios_base& os)) {
    StreamData(data_, m);
    return *this;
  }

  // Literal strings.  This allows us to record C string literals as literals in
  // the logging.proto.Value.
  //
  // Force-inlined to avoid per-`SIZE` template instantiations in cold blocks;
  // passing `const char*` keeps callsites smaller at the cost of doing `strlen`
  // at runtime.
  template <int SIZE>
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(const char (&buf)[SIZE]) {
    CopyToEncodedBufferImpl<StringType::kLiteral>(data_, buf);
    return *this;
  }
  template <int SIZE>
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(
      const wchar_t (&buf)[SIZE]) {
    CopyToEncodedBufferImpl<StringType::kLiteral>(data_, buf);
    return *this;
  }

  // This prevents non-const `char[]` arrays from looking like literals.
  template <int SIZE>
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(char (&buf)[SIZE]) {
    CopyToEncodedBufferImpl<StringType::kNotLiteral>(data_, buf);
    return *this;
  }
  // `wchar_t[SIZE]` is handled by `operator<< <const wchar_t*>`.

  // Types that support `AbslStringify()` are serialized that way.
  // Types that don't support `AbslStringify()` but do support streaming into a
  // `std::ostream&` are serialized that way.
  template <typename T>
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessage& operator<<(const T& v) {
    StreamData<const T&>(data_, v);
    return *this;
  }

  // Dispatches the completed `absl::LogEntry` to applicable `absl::LogSink`s.
  ABSL_ATTRIBUTE_ALWAYS_INLINE void Flush() {
    FlushData(data_);
    data_ = nullptr;
  }

  // Dispatches a fatal `absl::LogEntry` and terminates the process.
  [[noreturn]] ABSL_ATTRIBUTE_ALWAYS_INLINE void FlushFatal() {
    FlushFatalData(data_);
  }

  // Note: We explicitly do not support `operator<<` for non-const references
  // because it breaks logging of non-integer bitfield types (i.e., enums).

 protected:
  struct LogMessageData;  // Opaque type containing message state
  ABSL_ATTRIBUTE_ALWAYS_INLINE explicit LogMessage(
      LogMessageData* absl_nonnull data)
      : data_(data) {}

  ABSL_ATTRIBUTE_COLD static LogMessageData* absl_nonnull CreateFatalData(
      const char* absl_nonnull file, int line,
      const char* absl_nonnull failure_msg);
  ABSL_ATTRIBUTE_COLD static LogMessageData* absl_nonnull
  CreateQuietlyFatalData(const char* absl_nonnull file, int line);
  ABSL_ATTRIBUTE_COLD static LogMessageData* absl_nonnull
  CreateQuietlyFatalData(const char* absl_nonnull file, int line,
                         const char* absl_nonnull failure_msg);

  // Call `abort()` or similar to perform `LOG(FATAL)` crash.  It is assumed
  // that the caller has already generated and written the trace as appropriate.
  [[noreturn]] static void FailWithoutStackTrace();

  // Similar to `FailWithoutStackTrace()`, but without `abort()`.  Terminates
  // the process with an error exit code.
  [[noreturn]] static void FailQuietly();

 private:
  friend class AsLiteralImpl;
  friend class StringifySink;
  template <StructuredStringType str_type>
  friend class AsStructuredStringTypeImpl;
  template <typename T>
  friend class AsStructuredValueImpl;

  // This streambuf writes directly into the structured logging buffer so that
  // arbitrary types can be encoded as string data (using
  // `operator<<(std::ostream &, ...)` without any extra allocation or copying.
  // Space is reserved before the data to store the length field, which is
  // filled in by `~OstreamView`.
  class OstreamView final : public std::streambuf {
   public:
    explicit OstreamView(LogMessageData& message_data);
    ~OstreamView() override;
    OstreamView(const OstreamView&) = delete;
    OstreamView& operator=(const OstreamView&) = delete;
    std::ostream& stream();

   private:
    LogMessageData& data_;
    absl::Span<char> encoded_remaining_copy_;
    absl::Span<char> message_start_;
    absl::Span<char> string_start_;
  };

  ABSL_ATTRIBUTE_COLD static LogMessageData* absl_nonnull CreateData(
      const char* absl_nonnull file, int line, absl::LogSeverity severity);
  ABSL_ATTRIBUTE_COLD static LogMessageData* absl_nonnull CreateData(
      absl::string_view file, int line, absl::LogSeverity severity);
  ABSL_ATTRIBUTE_COLD
  ABSL_ATTRIBUTE_NOINLINE static LogMessageData* absl_nonnull CreateInfoData(
      const char* absl_nonnull file, int line);
  ABSL_ATTRIBUTE_COLD
  ABSL_ATTRIBUTE_NOINLINE static LogMessageData* absl_nonnull CreateWarningData(
      const char* absl_nonnull file, int line);
  ABSL_ATTRIBUTE_COLD
  ABSL_ATTRIBUTE_NOINLINE static LogMessageData* absl_nonnull CreateErrorData(
      const char* absl_nonnull file, int line);

  static void AtLocationImpl(LogMessageData* absl_nonnull data,
                             absl::string_view file, int line);
  static void NoPrefixImpl(LogMessageData* absl_nonnull data);
  static void WithVerbosityImpl(LogMessageData* absl_nonnull data,
                                int verbose_level);
  static void WithTimestampImpl(LogMessageData* absl_nonnull data,
                                absl::Time timestamp);
  static void WithThreadIDImpl(LogMessageData* absl_nonnull data,
                               absl::LogEntry::tid_t tid);
  static void WithMetadataFromImpl(LogMessageData* absl_nonnull data,
                                   const absl::LogEntry& entry);
  static void WithPerrorImpl(LogMessageData* absl_nonnull data);
  static void ToSinkAlsoImpl(LogMessageData* absl_nonnull data,
                             absl::LogSink* absl_nonnull sink);
  static void ToSinkOnlyImpl(LogMessageData* absl_nonnull data,
                             absl::LogSink* absl_nonnull sink);

  template <typename T>
  ABSL_ATTRIBUTE_NOINLINE static void StreamData(
      LogMessageData* absl_nonnull data, T v);
  static void StreamNullWCharPtr(LogMessageData* absl_nonnull data,
                                 const wchar_t* absl_nullable v);
  static void StreamSourceLocation(LogMessageData* absl_nonnull data,
                                   const absl::SourceLocation& loc);

  enum class StringType {
    kLiteral,
    kNotLiteral,
  };
  template <StringType str_type>
  ABSL_ATTRIBUTE_ALWAYS_INLINE void CopyToEncodedBuffer(absl::string_view str) {
    CopyToEncodedBufferImpl<str_type>(data_, str);
  }
  template <StringType str_type>
  ABSL_ATTRIBUTE_ALWAYS_INLINE void CopyToEncodedBufferWithStructuredProtoField(
      StructuredProtoField field, absl::string_view str) {
    CopyToEncodedBufferWithStructuredProtoFieldImpl<str_type>(data_, field,
                                                              str);
  }

  template <StringType str_type>
  static void CopyToEncodedBufferImpl(LogMessageData* absl_nonnull data,
                                      absl::string_view str);
  template <StringType str_type>
  static void CopyToEncodedBufferImpl(LogMessageData* absl_nonnull data,
                                      const char* absl_nonnull str);
  // Keeps std::string data and size access out of callsites.
  template <StringType str_type>
  static void CopyToEncodedBufferImpl(LogMessageData* absl_nonnull data,
                                      const std::string& str);
  template <StringType str_type>
  static void CopyToEncodedBufferImpl(LogMessageData* absl_nonnull data,
                                      char ch, size_t num);
  template <StringType str_type>
  static void CopyToEncodedBufferImpl(LogMessageData* absl_nonnull data,
                                      std::wstring_view str);

  // Copies `field` to the encoded buffer, then appends `str` after it
  // (truncating `str` if necessary to fit).
  template <StringType str_type>
  static void CopyToEncodedBufferWithStructuredProtoFieldImpl(
      LogMessageData* absl_nonnull data, StructuredProtoField field,
      absl::string_view str);

  ABSL_ATTRIBUTE_COLD static void FlushData(LogMessageData* absl_nonnull data);
  [[noreturn]] ABSL_ATTRIBUTE_COLD static void FlushFatalData(
      LogMessageData* absl_nonnull data);
  ABSL_ATTRIBUTE_COLD static void DestroyData(
      LogMessageData* absl_nonnull data);

  // Non-null until flushed; nullability is left unspecified to reflect that.
  LogMessageData* absl_nullability_unknown data_;
};

// Explicitly specializes the generic operator<< for `const wchar_t*`
// arguments.
//
// This method is used instead of a non-template `const wchar_t*` overload,
// as the latter was found to take precedence over the array template
// (`operator<<(const wchar_t(&)[SIZE])`) when handling string literals.
// This specialization ensures the array template now correctly processes
// literals.
template <>
ABSL_ATTRIBUTE_ALWAYS_INLINE inline LogMessage& LogMessage::operator<< <
    const wchar_t*>(
    const wchar_t* absl_nullable const& v) {
  StreamNullWCharPtr(data_, v);
  return *this;
}

// Helper class so that `AbslStringify()` can modify the LogMessage.
class StringifySink final {
 public:
  explicit StringifySink(LogMessage::LogMessageData* absl_nonnull data)
      : data_(data) {}

  void Append(size_t count, char ch) {
    LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kNotLiteral>(
        data_, ch, count);
  }

  void Append(absl::string_view v) {
    LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kNotLiteral>(
        data_, v);
  }

  // For types that implement `AbslStringify` using `absl::Format()`.
  friend void AbslFormatFlush(StringifySink* absl_nonnull sink,
                              absl::string_view v) {
    sink->Append(v);
  }

 private:
  LogMessage::LogMessageData* absl_nonnull data_;
};

// Note: the following is declared `ABSL_ATTRIBUTE_NOINLINE`
template <typename T>
void LogMessage::StreamData(LogMessageData* absl_nonnull data, T v) {
  // `T` may be `const U&` (from the generic `operator<<`); strip `&` and
  // `const` (preserving `volatile`) so trait and `NullGuard` lookups see `U`.
  using RawT = std::remove_const_t<std::remove_reference_t<T>>;
  if constexpr (absl::HasAbslStringify<RawT>::value) {
    StringifySink sink(data);
    // Replace with public API.
    AbslStringify(sink, v);
  } else {
    OstreamView view(*data);
    view.stream() << log_internal::NullGuard<RawT>().Guard(v);
  }
}

// We instantiate these specializations in the library's TU to save space in
// other TUs.  Since the template is marked `ABSL_ATTRIBUTE_NOINLINE` we will be
// emitting a function call either way.
// NOLINTBEGIN(runtime/int)
// NOLINTBEGIN(google-runtime-int)
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            char v);
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            signed char v);
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            unsigned char v);
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            short v);
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            unsigned short v);
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            int v);
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            unsigned int v);
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            long v);
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            unsigned long v);
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            long long v);
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            unsigned long long v);
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            void* absl_nullable v);
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            const void* absl_nullable v);
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            float v);
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            double v);
extern template void LogMessage::StreamData(LogMessageData* absl_nonnull data,
                                            bool v);
// NOLINTEND(google-runtime-int)
// NOLINTEND(runtime/int)
extern template void LogMessage::StreamData(
    LogMessageData* absl_nonnull data,
    std::ostream& (*absl_nonnull m)(std::ostream& os));
extern template void LogMessage::StreamData(
    LogMessageData* absl_nonnull data,
    std::ios_base& (*absl_nonnull m)(std::ios_base& os));

extern template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kLiteral>(
    LogMessageData* absl_nonnull data, absl::string_view str);
extern template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kNotLiteral>(
    LogMessageData* absl_nonnull data, absl::string_view str);
extern template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kLiteral>(
    LogMessageData* absl_nonnull data, const char* absl_nonnull str);
extern template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kNotLiteral>(
    LogMessageData* absl_nonnull data, const char* absl_nonnull str);
extern template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kLiteral>(
    LogMessageData* absl_nonnull data, const std::string& str);
extern template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kNotLiteral>(
    LogMessageData* absl_nonnull data, const std::string& str);
extern template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kLiteral>(
    LogMessageData* absl_nonnull data, char ch, size_t num);
extern template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kNotLiteral>(
    LogMessageData* absl_nonnull data, char ch, size_t num);
extern template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kLiteral>(
    LogMessageData* absl_nonnull data, std::wstring_view str);
extern template void
LogMessage::CopyToEncodedBufferImpl<LogMessage::StringType::kNotLiteral>(
    LogMessageData* absl_nonnull data, std::wstring_view str);

// `LogMessageFatal` ensures the process will exit in failure after logging this
// message.
class LogMessageFatal final : public LogMessage {
 public:
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessageFatal(const char* absl_nonnull file,
                                               int line)
      : LogMessage(file, line, absl::LogSeverity::kFatal) {}
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessageFatal(
      const char* absl_nonnull file, int line,
      const char* absl_nonnull failure_msg)
      : LogMessage(CreateFatalData(file, line, failure_msg)) {}
  [[noreturn]] ~LogMessageFatal();
};

// `LogMessageDebugFatal` ensures the process will exit in failure after logging
// this message. It matches LogMessageFatal but is not [[noreturn]] as it's used
// for DLOG(FATAL) variants.
class LogMessageDebugFatal final : public LogMessage {
 public:
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessageDebugFatal(
      const char* absl_nonnull file, int line)
      : LogMessage(file, line, absl::LogSeverity::kFatal) {}
  ~LogMessageDebugFatal();
};

class LogMessageQuietlyDebugFatal final : public LogMessage {
 public:
  // DLOG(QFATAL) calls this instead of LogMessageQuietlyFatal to make sure the
  // destructor is not [[noreturn]] even if this is always FATAL as this is only
  // invoked when DLOG() is enabled.
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessageQuietlyDebugFatal(
      const char* absl_nonnull file, int line)
      : LogMessage(CreateQuietlyFatalData(file, line)) {}
  ~LogMessageQuietlyDebugFatal();
};

// Used for LOG(QFATAL) to make sure it's properly understood as [[noreturn]].
class LogMessageQuietlyFatal final : public LogMessage {
 public:
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessageQuietlyFatal(
      const char* absl_nonnull file, int line)
      : LogMessage(CreateQuietlyFatalData(file, line)) {}
  ABSL_ATTRIBUTE_ALWAYS_INLINE LogMessageQuietlyFatal(
      const char* absl_nonnull file, int line,
      const char* absl_nonnull failure_msg)
      : LogMessage(CreateQuietlyFatalData(file, line, failure_msg)) {}
  [[noreturn]] ~LogMessageQuietlyFatal();
};

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

extern "C" ABSL_ATTRIBUTE_WEAK void ABSL_INTERNAL_C_SYMBOL(
    AbslInternalOnFatalLogMessage)(const absl::LogEntry&);

#endif  // ABSL_LOG_INTERNAL_LOG_MESSAGE_H_
