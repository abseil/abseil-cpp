# Standalone Fuzz Targets

This directory owns the existing OSS-Fuzz `string_escape_fuzzer` and
`string_utilities_fuzzer` sources. Their contents, target names, input formats,
and checks are unchanged by the migration. The escape target retains its
existing escape/unescape comparisons; this migration does not introduce new
round-trip targets or revise existing assertions. No new corpus, dictionary,
or runtime options are added.

The Bazel filegroups `//fuzz:string_escape_fuzzer_src` and
`//fuzz:string_utilities_fuzzer_src` expose the sources to an external
`rules_fuzzing` wrapper such as OSS-Fuzz. Abseil's normal Bazel builds do not
require `rules_fuzzing`, and the existing OSS-Fuzz build retains responsibility
for fuzzing engine selection and artifact export.

## CMake

Fuzz targets are disabled by default. Enable `ABSL_BUILD_FUZZERS` explicitly:

```sh
CC=clang CXX=clang++ cmake -S . -B build-fuzz \
  -DABSL_BUILD_FUZZERS=ON \
  -DABSL_BUILD_TESTING=OFF \
  -DABSL_ENABLE_INSTALL=OFF \
  -DCMAKE_CXX_STANDARD=17 \
  -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build-fuzz --target fuzz_targets
```

Both binaries are written to `build-fuzz/bin/`. The explicit compiler flags
instrument the Abseil libraries as well as the harnesses. With no external
engine configured, Clang's `-fsanitize=fuzzer` supplies the driver. Otherwise
`ABSL_FUZZING_ENGINE` takes driver link flags or a library, defaulting to
`LIB_FUZZING_ENGINE` from the environment; instrumentation flags remain the
caller's responsibility.

The utilities target requires `fuzzer/FuzzedDataProvider.h` from the compiler
toolchain or OSS-Fuzz environment. If it is not on the compiler's include
search path, set `ABSL_FUZZED_DATA_PROVIDER_INCLUDE_DIR` to the directory that
contains `fuzzer/`. No dependency is downloaded during configuration.

The paired OSS-Fuzz migration requires this upstream source tree until the
changes merge. Build, execution, and coverage validation remain pending.
