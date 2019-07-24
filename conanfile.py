#!/usr/bin/env python
# -*- coding: utf-8 -*-

# Note: Conan is supported on a best-effort basis. Abseil doesn't use Conan
# internally, so we won't know if it stops working. We may ask community
# members to help us debug any problems that arise.

from conans import ConanFile, CMake, tools
from conans.errors import ConanInvalidConfiguration
from conans.tools import Version

class AbseilConan(ConanFile):
    name = "abseil"
    version = "2019071800"
    url = "https://github.com/abseil/abseil-cpp"
    homepage = "https://abseil.io"
    author = "Abseil <abseil-io@googlegroups.com>"
    description = "Abseil Common Libraries (C++) from Google"
    license = "Apache-2.0"
    topics = ("conan", "abseil", "abseil-cpp", "google", "common-libraries")
    generators = "cmake"
    settings = "os", "arch", "compiler", "build_type"
    scm = {
         "type": "git",
         "url": url,
         "revision": "f3840bc5e33ce4932e35986cf3718450c6f02af2"
    }

    def configure(self):
        if self.settings.os == "Windows" and \
           self.settings.compiler == "Visual Studio" and \
           Version(self.settings.compiler.version.value) < "14":
            raise ConanInvalidConfiguration("Abseil does not support MSVC < 14")

    def _configure_cmake(self):
        cmake = CMake(self)
        cmake.definitions["BUILD_TESTING"] = False
        cmake.configure()
        return cmake

    def build(self):
        tools.replace_in_file("CMakeLists.txt", "project(absl CXX)", """project(absl CXX)
                                                                    include(conanbuildinfo.cmake)
                                                                    conan_basic_setup()""")
        cmake = self._configure_cmake()
        cmake.build()

    def package(self):
        self.copy("LICENSE", dst="licenses")
        cmake = self._configure_cmake()
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = [
            "absl_spinlock_wait",
            "absl_dynamic_annotations",
            "absl_malloc_internal",
            "absl_base",
            "absl_throw_delegate",
            "absl_scoped_set_env",
            "absl_hashtablez_sampler",
            "absl_raw_hash_set",
            "absl_stacktrace",
            "absl_symbolize",
            "absl_examine_stack",
            "absl_failure_signal_handler",
            "absl_debugging_internal",
            "absl_demangle_internal",
            "absl_leak_check",
            "absl_leak_check_disable",
            "absl_flags_internal",
            "absl_flags_config",
            "absl_flags_marshalling",
            "absl_flags_handle",
            "absl_flags_registry",
            "absl_flags",
            "absl_flags_usage_internal",
            "absl_flags_usage",
            "absl_flags_parse",
            "absl_hash",
            "absl_city ",
            "absl_int128",
            "absl_strings",
            "absl_strings_internal",
            "absl_str_format_internal",
            "absl_graphcycles_internal",
            "absl_synchronization",
            "absl_time absl_civil_time",
            "absl_time_zone",
            "absl_bad_any_cast_impl",
            "absl_bad_optional_access",
            "absl_bad_variant_access",
            "absl_bad_variant_access",
            "absl_hashtablez_sampler",
            "absl_examine_stack",
            "absl_leak_check",
            "absl_flags_usage",
            "absl_flags_usage_internal",
            "absl_flags",
            "absl_flags_registry",
            "absl_flags_handle",
            "absl_flags_config",
            "absl_flags_internal",
            "absl_flags_marshalling",
            "absl_str_format_internal",
            "absl_bad_optional_access",
            "absl_synchronization",
            "absl_stacktrace",
            "absl_symbolize",
            "absl_debugging_internal",
            "absl_demangle_internal",
            "absl_graphcycles_internal",
            "absl_malloc_internal",
            "absl_time absl_strings",
            "absl_throw_delegate",
            "absl_strings_internal",
            "absl_civil_time",
            "absl_time_zone",
            "absl_int128",
            "absl_base ",
            "absl_spinlock_wait",
            "absl_dynamic_annotations"
        ]
        if self.settings.os == "Linux":
            self.cpp_info.libs.append("pthread")
