"""Common definitions of gunit and gmock dependencies for Abseil."""

# pylint: disable=pointless-std::string-statement

# TODO(catlyons): Clean up below selectors when possible. Hold on to them for
# now as we may still need our own gunit_main selectors that do not bring in any
# heapchecker-related deps, and possibly to deal with benchmark dependencies.

"""Use GUNIT_DEPS_SELECTOR when you don't need gunit_main."""
GUNIT_DEPS_SELECTOR = {
    "//conditions:default": [
        "@com_google_googletest//:gtest",
    ],
}

"""Use GUNIT_MAIN_DEPS_SELECTOR to get gunit_main with leak checking."""
GUNIT_MAIN_DEPS_SELECTOR = {
    "//conditions:default": [
        "@com_google_googletest//:gtest_main",
    ],
}

# TODO(b/30141238): In order to set up absl deps on leak checking
# without base, we'll need gunit_main without either
# base:heapcheck or base:noheapcheck.
GUNIT_MAIN_NO_LEAK_CHECK_DEPS = [
    "@com_google_googletest//:gtest_main",
]

# TODO(alanjones): Merge this into @com_google_googletest//:gunit_main_no_heapcheck
GUNIT_MAIN_NO_LEAK_CHECK_PORTABLE_DEPS = [
    "@com_google_googletest//:gtest_main",
]

"""Use GUNIT_MAIN_NO_LEAK_CHECK_DEPS_SELECTOR to turn off leak checking."""
GUNIT_MAIN_NO_LEAK_CHECK_DEPS_SELECTOR = {
    "//absl:ios": GUNIT_MAIN_NO_LEAK_CHECK_PORTABLE_DEPS,
    "//absl:windows": GUNIT_MAIN_NO_LEAK_CHECK_PORTABLE_DEPS,
    "//conditions:default": GUNIT_MAIN_NO_LEAK_CHECK_DEPS,
}
