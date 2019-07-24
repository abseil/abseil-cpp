# -*- coding: utf-8 -*-
from conans import ConanFile, CMake
import os

class TestPackageConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        test_binary = os.path.join("bin","test_package")
        self.run(test_binary, run_environment=True)
