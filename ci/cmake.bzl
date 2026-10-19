# Copyright 2026 The Abseil Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""CMake build and test targets using rules_foreign_cc."""

load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

def _write_executable_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name + ".sh")
    ctx.actions.write(
        output = out,
        content = ctx.attr.content,
        is_executable = True,
    )
    return [DefaultInfo(files = depset([out]), executable = out)]

_write_executable = rule(
    implementation = _write_executable_impl,
    attrs = {
        "content": attr.string(mandatory = True),
    },
)

def define_cmake_tests():
    """Defines CMake build and test targets for static, shared, and monolithic-shared modes."""
    native.config_setting(
        name = "cmake_opt_mode",
        values = {"compilation_mode": "opt"},
    )

    for mode, shared, monolithic in [
        ("static", "OFF", "OFF"),
        ("shared", "ON", "OFF"),
        ("monolithic_shared", "ON", "ON"),
    ]:
        build_target = "cmake_build_" + mode
        script_name = "cmake_test_" + mode + "_ok.sh"
        base_cache_entries = {
            "ABSL_BUILD_MONOLITHIC_SHARED_LIBS": monolithic,
            "ABSL_BUILD_TESTING": "ON",
            "ABSL_GOOGLETEST_DOWNLOAD_URL": "file://$$EXT_BUILD_ROOT$$/$(execpath @googletest_archive//" + "file)",
            "BUILD_SHARED_LIBS": shared,
            "CMAKE_CXX_COMPILER": "g++",
            "CMAKE_CXX_FLAGS": "-Werror",
            "CMAKE_CXX_FLAGS_DEBUG": "-Og",
            "CMAKE_CXX_STANDARD": "17",
            "CMAKE_C_COMPILER": "gcc",
            "CMAKE_C_FLAGS": "-Werror",
            "CMAKE_C_FLAGS_DEBUG": "-Og",
            "CMAKE_EXE_LINKER_FLAGS_DEBUG": "",
            "CMAKE_SHARED_LINKER_FLAGS_DEBUG": "",
        }
        cmake(
            name = build_target,
            testonly = True,
            build_data = ["@googletest_archive//" + "file"],
            cache_entries = select({
                ":cmake_opt_mode": base_cache_entries | {"CMAKE_BUILD_TYPE": "Release"},
                "//conditions:default": base_cache_entries | {"CMAKE_BUILD_TYPE": "Debug"},
            }),
            generate_args = ["-GNinja"],
            install = False,
            lib_source = "//:cmake_srcs",
            out_binaries = [script_name],
            postfix_script = """
                TZDIR="$$EXT_BUILD_ROOT$$/absl/time/internal/cctz/testdata/zoneinfo" \
                  "$$(dirname $$CMAKE$$)/ctest" --output-on-failure --repeat until-pass:3 -j$$(nproc)
                mkdir -p "$$INSTALLDIR$$/bin"
                printf '#!/bin/sh\\nexit 0\\n' > "$$INSTALLDIR$$/bin/{script_name}"
                chmod +x "$$INSTALLDIR$$/bin/{script_name}"
            """.format(script_name = script_name),
            tags = [
                "cmake",
                "manual",
            ],
            targets = ["all"],
        )

        ok_runner = "cmake_" + mode + "_ok"
        _write_executable(
            name = ok_runner,
            testonly = True,
            content = "#!/bin/sh\nexit 0\n",
            tags = [
                "cmake",
                "manual",
            ],
        )

        sh_test(
            name = "cmake_" + mode + "_test",
            size = "enormous",
            srcs = [":" + ok_runner],
            data = [":" + build_target],
            tags = [
                "cmake",
                "manual",
            ],
        )

    for mode, link_type in [("static", "STATIC"), ("shared", "DYNAMIC")]:
        runner_name = "cmake_install_" + mode + "_runner"
        _write_executable(
            name = runner_name,
            testonly = True,
            content = """#!/bin/bash
set -euo pipefail
export LINK_TYPE="{link_type}"
export ABSL_GOOGLETEST_VERSION="1.18.0"
export ABSL_GOOGLETEST_DOWNLOAD_URL="file://$(realpath "$1")"
export CC=gcc
export CXX=g++
export CMAKE_GENERATOR=Ninja
export CMAKE_BUILD_PARALLEL_LEVEL="$(nproc)"
export CTEST_PARALLEL_LEVEL="$(nproc)"
WORKDIR="$(mktemp -d "${{TEST_TMPDIR}}/absl_install_test.XXXXXX")"
cp -RL CMakeLists.txt CMake MODULE.bazel absl create_lts.py "${{WORKDIR}}/"
chmod -R u+w "${{WORKDIR}}"
export TZDIR="${{WORKDIR}}/absl/time/internal/cctz/testdata/zoneinfo"
export HOME="${{TEST_TMPDIR}}"
rm -rf /buildfs /googletest_builddir
cd "${{WORKDIR}}"
exec ./CMake/install_test_project/test.sh
""".format(link_type = link_type),
            tags = [
                "cmake",
                "manual",
            ],
        )

        sh_test(
            name = "cmake_install_" + mode + "_test",
            size = "enormous",
            srcs = [":" + runner_name],
            args = ["$(rootpath @googletest_archive//" + "file)"],
            data = [
                "//:cmake_srcs",
                "@googletest_archive//" + "file",
            ],
            tags = [
                "cmake",
                "manual",
            ],
            target_compatible_with = select({
                ":cmake_opt_mode": [],
                "//conditions:default": ["@platforms//:incompatible"],
            }),
        )
