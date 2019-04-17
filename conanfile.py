#!/usr/bin/env python
# -*- coding: utf-8 -*-

# Note: Conan is supported on a best-effort basis. Abseil doesn't use Conan
# internally, so we won't know if it stops working. We may ask community
# members to help us debug any problems that arise.

from conans import ConanFile, CMake, tools
from conans.errors import ConanInvalidConfiguration
from conans.model.version import Version


class AbseilConan(ConanFile):
    name = "abseil"
    version = "2019041600"
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
         "revision": "d902eb869bcfacc1bad14933ed9af4bed006d481"
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
        tools.replace_in_file("CMakeLists.txt", "project(absl)", """project(absl)
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
            "absl_hash",
            "absl_city",
            "absl_int128",
            "absl_strings",
            "absl_strings_internal",
            "absl_str_format_internal",
            "absl_graphcycles_internal",
            "absl_synchronization",
            "absl_time",
            "absl_civil_time",
            "absl_time_zone",
            "absl_bad_any_cast_impl",
            "absl_bad_optional_access",
            "absl_bad_variant_access",
            "absl_bad_variant_access",
            "absl_hashtablez_sampler",
            "absl_synchronization",
            "absl_graphcycles_internal",
            "absl_time",
            "absl_civil_time",
            "absl_time_zone",
            "absl_examine_stack",
            "absl_symbolize",
            "absl_malloc_internal",
            "absl_demangle_internal",
            "absl_stacktrace",
            "absl_debugging_internal",
            "absl_leak_check",
            "absl_strings",
            "absl_int128",
            "absl_strings_internal",
            "absl_throw_delegate",
            "absl_base",
            "absl_spinlock_wait",
            "absl_dynamic_annotations"
        ]
        if self.settings.os == "Linux":
            self.cpp_info.libs.append("pthread")
