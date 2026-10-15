set(ABSL_FUZZING_ENGINE "$ENV{LIB_FUZZING_ENGINE}" CACHE STRING
  "Link flags or library providing LLVMFuzzerTestOneInput's driver")
set(ABSL_FUZZED_DATA_PROVIDER_INCLUDE_DIR "" CACHE PATH
  "Optional directory containing fuzzer/FuzzedDataProvider.h")

if(ABSL_FUZZING_ENGINE)
  separate_arguments(absl_fuzz_engine UNIX_COMMAND "${ABSL_FUZZING_ENGINE}")
else()
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "ABSL_BUILD_FUZZERS requires Clang or ABSL_FUZZING_ENGINE")
  endif()
  set(absl_fuzz_engine -fsanitize=fuzzer)
endif()

add_executable(string_escape_fuzzer string_escape_fuzzer.cc)
target_link_libraries(string_escape_fuzzer PRIVATE absl::strings ${absl_fuzz_engine})

add_executable(string_utilities_fuzzer string_utilities_fuzzer.cc)
target_link_libraries(string_utilities_fuzzer PRIVATE
  absl::strings absl::str_format ${absl_fuzz_engine})
if(ABSL_FUZZED_DATA_PROVIDER_INCLUDE_DIR)
  target_include_directories(string_utilities_fuzzer PRIVATE
    "${ABSL_FUZZED_DATA_PROVIDER_INCLUDE_DIR}")
endif()

if(NOT ABSL_FUZZING_ENGINE)
  target_compile_options(string_escape_fuzzer PRIVATE -fsanitize=fuzzer-no-link)
  target_compile_options(string_utilities_fuzzer PRIVATE -fsanitize=fuzzer-no-link)
endif()

add_custom_target(fuzz_targets DEPENDS string_escape_fuzzer string_utilities_fuzzer)
