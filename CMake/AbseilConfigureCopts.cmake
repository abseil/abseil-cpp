# Abseil-specific compiler flags.  See absl/copts.bzl for description.
# DO NOT CHANGE THIS FILE WITHOUT THE CORRESPONDING CHANGE TO absl/copts.bzl

list(APPEND GCC_FLAGS
  -Wall
  -Wextra
  -Wcast-qual
  -Wconversion-null
  -Wmissing-declarations
  -Woverlength-strings
  -Wpointer-arith
  -Wunused-local-typedefs
  -Wunused-result
  -Wvarargs
  -Wwrite-strings
  -Wno-sign-compare
)

list(APPEND GCC_TEST_FLAGS
  -Wno-conversion-null
  -Wno-missing-declarations
  -Wno-sign-compare
  -Wno-unused-function
  -Wno-unused-parameter
  -Wno-unused-private-field
)

list(APPEND LLVM_FLAGS
  -Wall
  -Wextra
  -Weverything
  -Wno-c++98-compat-pedantic
  -Wno-conversion
  -Wno-covered-switch-default
  -Wno-deprecated
  -Wno-disabled-macro-expansion
  -Wno-double-promotion
  -Wno-comma
  -Wno-extra-semi
  -Wno-packed
  -Wno-padded
  -Wno-sign-compare
  -Wno-float-conversion
  -Wno-float-equal
  -Wno-format-nonliteral
  -Wno-gcc-compat
  -Wno-global-constructors
  -Wno-exit-time-destructors
  -Wno-nested-anon-types
  -Wno-non-modular-include-in-module
  -Wno-old-style-cast
  -Wno-range-loop-analysis
  -Wno-reserved-id-macro
  -Wno-shorten-64-to-32
  -Wno-switch-enum
  -Wno-thread-safety-negative
  -Wno-undef
  -Wno-unknown-warning-option
  -Wno-unreachable-code
  -Wno-unused-macros
  -Wno-weak-vtables
  -Wbitfield-enum-conversion
  -Wbool-conversion
  -Wconstant-conversion
  -Wenum-conversion
  -Wint-conversion
  -Wliteral-conversion
  -Wnon-literal-null-conversion
  -Wnull-conversion
  -Wobjc-literal-conversion
  -Wno-sign-conversion
  -Wstring-conversion
)

list(APPEND LLVM_TEST_FLAGS
  -Wno-c99-extensions
  -Wno-missing-noreturn
  -Wno-missing-prototypes
  -Wno-missing-variable-declarations
  -Wno-null-conversion
  -Wno-shadow
  -Wno-shift-sign-overflow
  -Wno-sign-compare
  -Wno-unused-function
  -Wno-unused-member-function
  -Wno-unused-parameter
  -Wno-unused-private-field
  -Wno-unused-template
  -Wno-used-but-marked-unused
  -Wno-zero-as-null-pointer-constant
  -Wno-gnu-zero-variadic-macro-arguments
)

list(APPEND MSVC_FLAGS
  /W3
  /wd4005
  /wd4018
  /wd4068
  /wd4180
  /wd4244
  /wd4267
  /wd4800
  /DNOMINMAX
  /DWIN32_LEAN_AND_MEAN
  /D_CRT_SECURE_NO_WARNINGS
  /D_SCL_SECURE_NO_WARNINGS
  /D_ENABLE_EXTENDED_ALIGNED_STORAGE
)

list(APPEND MSVC_TEST_FLAGS
  /wd4101
  /wd4503
)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set(ABSL_DEFAULT_COPTS "${GCC_FLAGS}")
  set(ABSL_TEST_COPTS "${GCC_FLAGS};${GCC_TEST_FLAGS}")
  set(ABSL_EXCEPTIONS_FLAG "-fexceptions")
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  # MATCHES so we get both Clang and AppleClang
  set(ABSL_DEFAULT_COPTS "${LLVM_FLAGS}")
  set(ABSL_TEST_COPTS "${LLVM_FLAGS};${LLVM_TEST_FLAGS}")
  set(ABSL_EXCEPTIONS_FLAG "-fexceptions")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  set(ABSL_DEFAULT_COPTS "${MSVC_FLAGS}")
  set(ABSL_TEST_COPTS "${MSVC_FLAGS};${MSVC_TEST_FLAGS}")
  set(ABSL_EXCEPTIONS_FLAG "/U_HAS_EXCEPTIONS;/D_HAS_EXCEPTIONS=1;/EHsc")
else()
  message(WARNING "Unknown compiler: ${CMAKE_CXX_COMPILER}.  Building with no default flags")
  set(ABSL_DEFAULT_COPTS "")
  set(ABSL_TEST_COPTS "")
  set(ABSL_EXCEPTIONS_FLAG "")
endif()

# This flag is used internally for Bazel builds and is kept here for consistency
set(ABSL_EXCEPTIONS_FLAG_LINKOPTS "")

if("${CMAKE_CXX_STANDARD}" EQUAL 98)
  message(FATAL_ERROR "Abseil requires at least C++11")
elseif(NOT "${CMAKE_CXX_STANDARD}")
  message(STATUS "No CMAKE_CXX_STANDARD set, assuming 11")
  set(ABSL_CXX_STANDARD 11)
else()
  set(ABSL_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
endif()
