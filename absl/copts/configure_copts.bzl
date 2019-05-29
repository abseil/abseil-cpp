"""absl specific copts.

This file simply selects the correct options from the generated files.  To
change Abseil copts, edit absl/copts/copts.py
"""

load(
    "//absl:copts/GENERATED_copts.bzl",
    "ABSL_GCC_EXCEPTIONS_FLAGS",
    "ABSL_GCC_FLAGS",
    "ABSL_GCC_TEST_FLAGS",
    "ABSL_LLVM_EXCEPTIONS_FLAGS",
    "ABSL_LLVM_FLAGS",
    "ABSL_LLVM_TEST_FLAGS",
    "ABSL_MSVC_EXCEPTIONS_FLAGS",
    "ABSL_MSVC_FLAGS",
    "ABSL_MSVC_LINKOPTS",
    "ABSL_MSVC_TEST_FLAGS",
)

ABSL_DEFAULT_COPTS = select({
    "//absl:windows": ABSL_MSVC_FLAGS,
    "//absl:llvm_compiler": ABSL_LLVM_FLAGS,
    "//conditions:default": ABSL_GCC_FLAGS,
})

# in absence of modules (--compiler=gcc or -c opt), cc_tests leak their copts
# to their (included header) dependencies and fail to build outside absl
ABSL_TEST_COPTS = ABSL_DEFAULT_COPTS + select({
    "//absl:windows": ABSL_MSVC_TEST_FLAGS,
    "//absl:llvm_compiler": ABSL_LLVM_TEST_FLAGS,
    "//conditions:default": ABSL_GCC_TEST_FLAGS,
})

ABSL_EXCEPTIONS_FLAG = select({
    "//absl:windows": ABSL_MSVC_EXCEPTIONS_FLAGS,
    "//absl:llvm_compiler": ABSL_LLVM_EXCEPTIONS_FLAGS,
    "//conditions:default": ABSL_GCC_EXCEPTIONS_FLAGS,
})

ABSL_EXCEPTIONS_FLAG_LINKOPTS = select({
    "//conditions:default": [],
})

ABSL_DEFAULT_LINKOPTS = select({
    "//absl:windows": ABSL_MSVC_LINKOPTS,
    "//conditions:default": [],
})
