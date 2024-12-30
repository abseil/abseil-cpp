"""absl specific copts.

This file simply selects the correct options from the generated files.  To
change Abseil copts, edit absl/copts/copts.py
"""

load(
    "//absl:copts/GENERATED_copts.bzl",
    "ABSL_CLANG_CL_FLAGS",
    "ABSL_CLANG_CL_TEST_FLAGS",
    "ABSL_GCC_FLAGS",
    "ABSL_GCC_TEST_FLAGS",
    "ABSL_LLVM_FLAGS",
    "ABSL_LLVM_TEST_FLAGS",
    "ABSL_MSVC_FLAGS",
    "ABSL_MSVC_LINKOPTS",
    "ABSL_MSVC_TEST_FLAGS",
    "ABSL_RANDOM_HWAES_ARM64_FLAGS",
    "ABSL_RANDOM_HWAES_MSVC_X64_FLAGS",
    "ABSL_RANDOM_HWAES_X64_FLAGS",
)

ABSL_DEFAULT_COPTS = select({
    "//absl:msvc_compiler": ABSL_MSVC_FLAGS,
    "//absl:clang-cl_compiler": ABSL_CLANG_CL_FLAGS,
    "//absl:clang_compiler": ABSL_LLVM_FLAGS,
    "//absl:gcc_compiler": ABSL_GCC_FLAGS,
    "//conditions:default": ABSL_GCC_FLAGS,
})

ABSL_TEST_COPTS = select({
    "//absl:msvc_compiler": ABSL_MSVC_TEST_FLAGS,
    "//absl:clang-cl_compiler": ABSL_CLANG_CL_TEST_FLAGS,
    "//absl:clang_compiler": ABSL_LLVM_TEST_FLAGS,
    "//absl:gcc_compiler": ABSL_GCC_TEST_FLAGS,
    "//conditions:default": ABSL_GCC_TEST_FLAGS,
})

ABSL_DEFAULT_LINKOPTS = select({
    "//absl:msvc_compiler": ABSL_MSVC_LINKOPTS,
    "//conditions:default": [],
})

# ABSL_RANDOM_RANDEN_COPTS blaze copts flags which are required by each
# environment to build an accelerated RandenHwAes library.
ABSL_RANDOM_RANDEN_COPTS = select({
    "//absl:windows_x86_64": ABSL_RANDOM_HWAES_MSVC_X64_FLAGS,
    "@platforms//cpu:x86_64": ABSL_RANDOM_HWAES_X64_FLAGS,
    "@platforms//cpu:ppc": ["-mcrypto"],
    "@platforms//cpu:aarch64": ABSL_RANDOM_HWAES_ARM64_FLAGS,

    # Supported by default or unsupported.
    "//conditions:default": [],
})
