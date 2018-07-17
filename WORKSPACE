workspace(name = "com_google_absl")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# Bazel toolchains
http_archive(
  name = "bazel_toolchains",
  urls = [
    "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/archive/287b64e0a211fb7c23b74695f8d5f5205b61f4eb.tar.gz",
    "https://github.com/bazelbuild/bazel-toolchains/archive/287b64e0a211fb7c23b74695f8d5f5205b61f4eb.tar.gz",
  ],
  strip_prefix = "bazel-toolchains-287b64e0a211fb7c23b74695f8d5f5205b61f4eb",
  sha256 = "aca8ac6afd7745027ee4a43032b51a725a61a75a30f02cc58681ee87e4dcdf4b",
)

# GoogleTest/GoogleMock framework. Used by most unit-tests.
http_archive(
     name = "com_google_googletest",
     urls = ["https://github.com/google/googletest/archive/b4d4438df9479675a632b2f11125e57133822ece.zip"],  # 2018-07-16
     strip_prefix = "googletest-b4d4438df9479675a632b2f11125e57133822ece",
     sha256 = "5aaa5d566517cae711e2a3505ea9a6438be1b37fcaae0ebcb96ccba9aa56f23a",
)

# Google benchmark.
http_archive(
    name = "com_github_google_benchmark",
    urls = ["https://github.com/google/benchmark/archive/16703ff83c1ae6d53e5155df3bb3ab0bc96083be.zip"],
    strip_prefix = "benchmark-16703ff83c1ae6d53e5155df3bb3ab0bc96083be",
    sha256 = "59f918c8ccd4d74b6ac43484467b500f1d64b40cc1010daa055375b322a43ba3",
)
