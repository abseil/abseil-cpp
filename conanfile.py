#!/usr/bin/env python
# -*- coding: utf-8 -*-
from conans import ConanFile, CMake, tools
from conans.errors import ConanInvalidConfiguration
from conans.model.version import Version


class AbseilConan(ConanFile):
    name = "abseil"
    url = "https://github.com/abseil/abseil-cpp"
    homepage = url
    author = "Ashley Hedberg <ahedberg@google.com>"
    description = "Abseil Common Libraries (C++) from Google"
    license = "Apache-2.0"
    exports = ["LICENSE"]
    exports_sources = ["CMakeLists.txt", "CMake/*", "absl/*"]
    generators = "cmake"
    settings = "os", "arch", "compiler", "build_type"

    def configure(self):
        if self.settings.os == "Windows" and \
           self.settings.compiler == "Visual Studio" and \
           Version(self.settings.compiler.version.value) < "14":
            raise ConanInvalidConfiguration("Abseil does not support MSVC < 14")

    def build(self):
        tools.replace_in_file("CMakeLists.txt", "project(absl)", "project(absl)\ninclude(conanbuildinfo.cmake)\nconan_basic_setup()")
        cmake = CMake(self)
        cmake.definitions["BUILD_TESTING"] = False
        cmake.configure()
        cmake.build()

    def package(self):
        self.copy("LICENSE", dst="licenses")
        self.copy("*.h", dst="include", src="absl")
        self.copy("*.inc", dst="include", src="absl")
        self.copy("*.a", dst="lib", src=".", keep_path=False)
        self.copy("*.lib", dst="lib", src=".", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["absl_base",
                              "absl_synchronization",
                              "absl_strings",
                              "absl_symbolize",
                              "absl_malloc_internal",
                              "absl_time",
                              "absl_strings",
                              "absl_base",
                              "absl_dynamic_annotations",
                              "absl_spinlock_wait",
                              "absl_throw_delegate",
                              "absl_stacktrace",
                              "absl_int128",
                              "absl_span",
                              "test_instance_tracker_lib",
                              "absl_stack_consumption",
                              "absl_bad_any_cast",
                              "absl_hash",
                              "str_format_extension_internal",
                              "absl_failure_signal_handler",
                              "absl_str_format",
                              "absl_numeric",
                              "absl_any",
                              "absl_optional",
                              "absl_container",
                              "absl_debugging",
                              "absl_memory",
                              "absl_leak_check",
                              "absl_meta",
                              "absl_utility",
                              "str_format_internal",
                              "absl_variant",
                              "absl_examine_stack",
                              "absl_bad_optional_access",
                              "absl_algorithm"]
        if self.settings.os == "Linux":
            self.cpp_info.libs.append("pthread")
