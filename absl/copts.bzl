"""absl specific copts.

Flags specified here must not impact ABI. Code compiled with and without these
opts will be linked together, and in some cases headers compiled with and
without these options will be part of the same program.
"""
GCC_FLAGS = [
    "-Wall",
    "-Wextra",
    "-Wcast-qual",
    "-Wconversion-null",
    "-Wmissing-declarations",
    "-Woverlength-strings",
    "-Wpointer-arith",
    "-Wunused-local-typedefs",
    "-Wunused-result",
    "-Wvarargs",
    "-Wvla",  # variable-length array
    "-Wwrite-strings",
]

GCC_TEST_FLAGS = [
    "-Wno-conversion-null",
    "-Wno-missing-declarations",
    "-Wno-sign-compare",
    "-Wno-unused-function",
    "-Wno-unused-parameter",
    "-Wno-unused-private-field",
]

LLVM_FLAGS = [
    "-Wall",
    "-Wextra",
    "-Weverything",
    "-Wno-c++98-compat-pedantic",
    "-Wno-comma",
    "-Wno-conversion",
    "-Wno-disabled-macro-expansion",
    "-Wno-documentation",
    "-Wno-documentation-unknown-command",
    "-Wno-double-promotion",
    "-Wno-exit-time-destructors",
    "-Wno-extra-semi",
    "-Wno-float-conversion",
    "-Wno-float-equal",
    "-Wno-format-nonliteral",
    "-Wno-gcc-compat",
    "-Wno-global-constructors",
    "-Wno-google3-inheriting-constructor",
    "-Wno-google3-lambda-expression",
    "-Wno-google3-rvalue-reference",
    "-Wno-google3-trailing-return-type",
    "-Wno-nested-anon-types",
    "-Wno-non-modular-include-in-module",
    "-Wno-old-style-cast",
    "-Wno-packed",
    "-Wno-padded",
    "-Wno-range-loop-analysis",
    "-Wno-reserved-id-macro",
    "-Wno-shorten-64-to-32",
    "-Wno-sign-conversion",
    "-Wno-switch-enum",
    "-Wno-thread-safety-negative",
    "-Wno-undef",
    "-Wno-unused-macros",
    "-Wno-weak-vtables",
    # flags below are also controled by -Wconversion which is disabled
    "-Wbitfield-enum-conversion",
    "-Wbool-conversion",
    "-Wconstant-conversion",
    "-Wenum-conversion",
    "-Wint-conversion",
    "-Wliteral-conversion",
    "-Wnon-literal-null-conversion",
    "-Wnull-conversion",
    "-Wobjc-literal-conversion",
    "-Wstring-conversion",
]

LLVM_TEST_FLAGS = [
    "-Wno-c99-extensions",
    "-Wno-missing-noreturn",
    "-Wno-missing-prototypes",
    "-Wno-null-conversion",
    "-Wno-shadow",
    "-Wno-shift-sign-overflow",
    "-Wno-sign-compare",
    "-Wno-unused-function",
    "-Wno-unused-member-function",
    "-Wno-unused-parameter",
    "-Wno-unused-private-field",
    "-Wno-unused-template",
    "-Wno-used-but-marked-unused",
    "-Wno-zero-as-null-pointer-constant",
]

MSVC_FLAGS = [
    "/W3",
    "/WX",
    "/wd4005",  # macro-redifinition
    "/wd4068",  # unknown pragma
    "/wd4244",  # conversion from 'type1' to 'type2', possible loss of data
    "/wd4267",  # conversion from 'size_t' to 'type', possible loss of data
    "/wd4800",  # forcing value to bool 'true' or 'false' (performance warning)
    "/DWIN32_LEAN_AND_MEAN",  # Don't bloat namespace with incompatible winsock versions.
]

MSVC_TEST_FLAGS = [
    "/wd4018",  # signed/unsigned mismatch
    "/wd4101",  # unreferenced local variable
    "/wd4503",  # decorated name length exceeded, name was truncated
]

def _qualify_flags(scope, flags):
  return [scope + x for x in flags]

HYBRID_FLAGS = _qualify_flags("-Xgcc-only=", GCC_FLAGS) + _qualify_flags("-Xclang-only=", LLVM_FLAGS)
HYBRID_TEST_FLAGS = _qualify_flags("-Xgcc-only=", GCC_TEST_FLAGS) + _qualify_flags("-Xclang-only=", LLVM_TEST_FLAGS)

# /Wall with msvc includes unhelpful warnings such as C4711, C4710, ...
ABSL_DEFAULT_COPTS = select({
    "//absl:windows": MSVC_FLAGS,
    "//absl:llvm_warnings": LLVM_FLAGS,
    "//conditions:default": GCC_FLAGS,
})

# in absence of modules (--compiler=gcc or -c opt), cc_tests leak their copts
# to their (included header) dependencies and fail to build outside absl
ABSL_TEST_COPTS = ABSL_DEFAULT_COPTS + select({
    "//absl:windows": MSVC_TEST_FLAGS,
    "//absl:llvm_warnings": LLVM_TEST_FLAGS,
    "//conditions:default": GCC_TEST_FLAGS,
})

ABSL_EXCEPTIONS_FLAG = select({
    "//absl:windows": ["/U_HAS_EXCEPTIONS", "/D_HAS_EXCEPTIONS=1", "/EHsc"],
    "//conditions:default": ["-fexceptions"],
})
