workspace(name = "com_google_absl")
# Bazel toolchains
http_archive(
    name = "bazel_toolchains",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/archive/r324073.tar.gz",
        "https://github.com/bazelbuild/bazel-toolchains/archive/r324073.tar.gz",
    ],
    strip_prefix = "bazel-toolchains-r324073",
    sha256 = "71548c0d6cd53eddebbde4fa9962f5395e82645fb9992719e0890505b177f245",
)

# GoogleTest/GoogleMock framework. Used by most unit-tests.
http_archive(
     name = "com_google_googletest",
     urls = ["https://github.com/google/googletest/archive/master.zip"],
     strip_prefix = "googletest-master",
)

# RE2 regular-expression framework. Used by some unit-tests.
http_archive(
    name = "com_googlesource_code_re2",
    urls = ["https://github.com/google/re2/archive/master.zip"],
    strip_prefix = "re2-master",
)
