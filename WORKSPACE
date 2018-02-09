workspace(name = "com_google_absl")
# Bazel toolchains
http_archive(
    name = "bazel_toolchains",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/archive/f3b09700fae5d7b6e659d7cefe0dcc6e8498504c.tar.gz",
        "https://github.com/bazelbuild/bazel-toolchains/archive/f3b09700fae5d7b6e659d7cefe0dcc6e8498504c.tar.gz",
    ],
    strip_prefix = "bazel-toolchains-f3b09700fae5d7b6e659d7cefe0dcc6e8498504c",
    sha256 = "ed829b5eea8af1f405f4cc3d6ecfc3b1365bb7843171036030a31b5127002311",
)

# GoogleTest/GoogleMock framework. Used by most unit-tests.
http_archive(
     name = "com_google_googletest",
     urls = ["https://github.com/google/googletest/archive/master.zip"],
     strip_prefix = "googletest-master",
)

# CCTZ (Time-zone framework).
http_archive(
    name = "com_googlesource_code_cctz",
    urls = ["https://github.com/google/cctz/archive/master.zip"],
    strip_prefix = "cctz-master",
)

# RE2 regular-expression framework. Used by some unit-tests.
http_archive(
    name = "com_googlesource_code_re2",
    urls = ["https://github.com/google/re2/archive/master.zip"],
    strip_prefix = "re2-master",
)
