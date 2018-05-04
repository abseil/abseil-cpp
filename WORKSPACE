workspace(name = "com_google_absl")
# Bazel toolchains
http_archive(
    name = "bazel_toolchains",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/archive/f8847f64e6950e8ab9fde1c0aba768550b0d9ab2.tar.gz",
        "https://github.com/bazelbuild/bazel-toolchains/archive/f8847f64e6950e8ab9fde1c0aba768550b0d9ab2.tar.gz",
    ],
    strip_prefix = "bazel-toolchains-f8847f64e6950e8ab9fde1c0aba768550b0d9ab2",
    sha256 = "794366f51fea224b3656a0b0f8f1518e739748646523a572fcd3d68614a0e670",
)

# GoogleTest/GoogleMock framework. Used by most unit-tests.
http_archive(
     name = "com_google_googletest",
     urls = ["https://github.com/google/googletest/archive/master.zip"],
     strip_prefix = "googletest-master",
)

# Google benchmark.
http_archive(
    name = "com_github_google_benchmark",
    urls = ["https://github.com/google/benchmark/archive/master.zip"],
    strip_prefix = "benchmark-master",
)

# RE2 regular-expression framework. Used by some unit-tests.
http_archive(
    name = "com_googlesource_code_re2",
    urls = ["https://github.com/google/re2/archive/master.zip"],
    strip_prefix = "re2-master",
)
