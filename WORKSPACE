workspace(name = "com_google_absl")
# Bazel toolchains
http_archive(
  name = "bazel_toolchains",
  urls = [
    "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/archive/2cec6c9f6d12224e93d9b3f337b24e41602de3ba.tar.gz",
    "https://github.com/bazelbuild/bazel-toolchains/archive/2cec6c9f6d12224e93d9b3f337b24e41602de3ba.tar.gz",
  ],
  strip_prefix = "bazel-toolchains-2cec6c9f6d12224e93d9b3f337b24e41602de3ba",
  sha256 = "9b8d85b61d8945422e86ac31e4d4d2d967542c080d1da1b45364da7fd6bdd638",
)

# GoogleTest/GoogleMock framework. Used by most unit-tests.
http_archive(
     name = "com_google_googletest",
     urls = ["https://github.com/google/googletest/archive/4e4df226fc197c0dda6e37f5c8c3845ca1e73a49.zip"],
     strip_prefix = "googletest-4e4df226fc197c0dda6e37f5c8c3845ca1e73a49",
     sha256 = "d4179caf54410968d1fff0b869e7d74803dd30209ee6645ccf1ca65ab6cf5e5a",
)

# Google benchmark.
http_archive(
    name = "com_github_google_benchmark",
    urls = ["https://github.com/google/benchmark/archive/16703ff83c1ae6d53e5155df3bb3ab0bc96083be.zip"],
    strip_prefix = "benchmark-16703ff83c1ae6d53e5155df3bb3ab0bc96083be",
    sha256 = "59f918c8ccd4d74b6ac43484467b500f1d64b40cc1010daa055375b322a43ba3",
)

# RE2 regular-expression framework. Used by some unit-tests.
http_archive(
    name = "com_googlesource_code_re2",
    urls = ["https://github.com/google/re2/archive/6cf8ccd82dbaab2668e9b13596c68183c9ecd13f.zip"],
    strip_prefix = "re2-6cf8ccd82dbaab2668e9b13596c68183c9ecd13f",
    sha256 = "279a852219dbfc504501775596089d30e9c0b29664ce4128b0ac4c841471a16a",
)
