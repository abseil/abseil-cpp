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

"""Windows RBE C++ toolchains targeting the oss-windows-latest container."""

load("@rules_cc//cc/private/toolchain:windows_cc_toolchain_config.bzl", "cc_toolchain_config")
load("@rules_cc//cc/toolchains:cc_toolchain.bzl", "cc_toolchain")

_MSVC_VERSION = "14.44.35207"
_WINSDK_VERSION = "10.0.26100.0"
_CLANG_MAJOR_VERSION = "23"

_MSVC_ROOT_WIN = "C:\\BuildTools\\VC\\Tools\\MSVC\\" + _MSVC_VERSION
_MSVC_BIN_POSIX = "C:/BuildTools/VC/Tools/MSVC/" + _MSVC_VERSION + "/bin/Hostx64/x64"
_VS_AUX_WIN = "C:\\BuildTools\\VC\\Auxiliary\\VS"
_WINSDK_INCLUDE_WIN = "C:\\Program Files (x86)\\Windows Kits\\10\\Include\\" + _WINSDK_VERSION
_WINSDK_LIB_WIN = "C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\" + _WINSDK_VERSION
_WINSDK_BIN_WIN = "C:\\Program Files (x86)\\Windows Kits\\10\\bin\\" + _WINSDK_VERSION
_CLANG_ROOT_WIN = "C:\\LLVM\\lib\\clang\\" + _CLANG_MAJOR_VERSION

_WINDOWS_ENV_PATH = ";".join([
    _MSVC_ROOT_WIN + "\\bin\\Hostx64\\x64",
    _WINSDK_BIN_WIN + "\\x64",
    "C:\\LLVM\\bin",
    "C:\\CMake\\bin",
    "C:\\Python3",
    "C:\\Windows\\System32",
    "C:\\Windows",
    "C:\\Windows\\System32\\Wbem",
    "C:\\Windows\\System32\\WindowsPowerShell\\v1.0",
    "C:\\tools\\msys64\\usr\\bin",
])

_MSVC_ENV_INCLUDE = ";".join([
    _MSVC_ROOT_WIN + "\\include",
    _VS_AUX_WIN + "\\include",
    _WINSDK_INCLUDE_WIN + "\\ucrt",
    _WINSDK_INCLUDE_WIN + "\\um",
    _WINSDK_INCLUDE_WIN + "\\shared",
    _WINSDK_INCLUDE_WIN + "\\winrt",
    _WINSDK_INCLUDE_WIN + "\\cppwinrt",
])

_MSVC_ENV_LIB = ";".join([
    _MSVC_ROOT_WIN + "\\lib\\x64",
    _WINSDK_LIB_WIN + "\\ucrt\\x64",
    _WINSDK_LIB_WIN + "\\um\\x64",
])

_WINDOWS_BUILTIN_INCLUDE_DIRECTORIES = [
    "C:\\BuildTools\\VC\\Tools\\MSVC",
    "C:\\BuildTools\\VC\\Auxiliary\\VS\\include",
    "C:\\Program Files (x86)\\Windows Kits\\10\\Include",
    "C:\\LLVM\\lib\\clang",
]

def setup_windows_toolchains():
    """Defines MSVC and Clang-CL toolchains for the Windows RBE container."""
    cc_toolchain_config(
        name = "windows_msvc_config",
        abi_libc_version = "local",
        abi_version = "local",
        archiver_flags = ["/MACHINE:X64"],
        compiler = "msvc-cl",
        cpu = "x64_windows",
        cxx_builtin_include_directories = _WINDOWS_BUILTIN_INCLUDE_DIRECTORIES,
        dbg_mode_debug_flag = "/DEBUG:FULL",
        default_link_flags = ["/MACHINE:X64"],
        fastbuild_mode_debug_flag = "/DEBUG:FULL",
        host_system_name = "local",
        msvc_cl_path = _MSVC_BIN_POSIX + "/cl.exe",
        msvc_env_include = _MSVC_ENV_INCLUDE,
        msvc_env_lib = _MSVC_ENV_LIB,
        msvc_env_path = _WINDOWS_ENV_PATH,
        msvc_env_tmp = "C:\\TEMP",
        msvc_lib_path = _MSVC_BIN_POSIX + "/lib.exe",
        msvc_link_path = _MSVC_BIN_POSIX + "/link.exe",
        msvc_ml_path = _MSVC_BIN_POSIX + "/ml64.exe",
        shorten_virtual_includes = True,
        supports_parse_showincludes = True,
        target_libc = "ucrt",
        target_system_name = "local",
        tool_paths = {
            "ar": _MSVC_BIN_POSIX + "/lib.exe",
            "cpp": _MSVC_BIN_POSIX + "/cl.exe",
            "dumpbin": _MSVC_BIN_POSIX + "/dumpbin.exe",
            "gcc": _MSVC_BIN_POSIX + "/cl.exe",
            "gcov": "wrapper/bin/msvc_nop.bat",
            "ld": _MSVC_BIN_POSIX + "/link.exe",
            "ml": _MSVC_BIN_POSIX + "/ml64.exe",
            "nm": "wrapper/bin/msvc_nop.bat",
            "objcopy": "wrapper/bin/msvc_nop.bat",
            "objdump": "wrapper/bin/msvc_nop.bat",
            "strip": "wrapper/bin/msvc_nop.bat",
        },
        toolchain_identifier = "msvc_x64",
    )

    cc_toolchain(
        name = "windows_msvc_cc_toolchain",
        all_files = ":empty",
        ar_files = ":empty",
        as_files = ":empty",
        compiler_files = ":empty",
        dwp_files = ":empty",
        linker_files = ":empty",
        objcopy_files = ":empty",
        strip_files = ":empty",
        supports_param_files = 1,
        toolchain_config = ":windows_msvc_config",
        toolchain_identifier = "msvc_x64",
    )

    cc_toolchain_config(
        name = "windows_clang_cl_config",
        abi_libc_version = "local",
        abi_version = "local",
        archiver_flags = ["/MACHINE:X64"],
        compiler = "clang-cl",
        cpu = "x64_windows",
        cxx_builtin_include_directories = _WINDOWS_BUILTIN_INCLUDE_DIRECTORIES,
        dbg_mode_debug_flag = "/DEBUG",
        default_link_flags = ["/MACHINE:X64"],
        fastbuild_mode_debug_flag = "/DEBUG",
        host_system_name = "local",
        msvc_cl_path = "C:/LLVM/bin/clang-cl.exe",
        msvc_env_include = _MSVC_ENV_INCLUDE + ";" + _CLANG_ROOT_WIN + "\\include",
        msvc_env_lib = _MSVC_ENV_LIB + ";" + _CLANG_ROOT_WIN + "\\lib\\windows",
        msvc_env_path = _WINDOWS_ENV_PATH,
        msvc_env_tmp = "C:\\TEMP",
        msvc_lib_path = "C:/LLVM/bin/llvm-lib.exe",
        msvc_link_path = "C:/LLVM/bin/lld-link.exe",
        msvc_ml_path = "C:/LLVM/bin/clang-cl.exe",
        supports_parse_showincludes = True,
        target_libc = "ucrt",
        target_system_name = "local",
        tool_paths = {
            "ar": "C:/LLVM/bin/llvm-lib.exe",
            "cpp": "C:/LLVM/bin/clang-cl.exe",
            "gcc": "C:/LLVM/bin/clang-cl.exe",
            "gcov": "wrapper/bin/msvc_nop.bat",
            "ld": "C:/LLVM/bin/lld-link.exe",
            "ml": "C:/LLVM/bin/clang-cl.exe",
            "nm": "wrapper/bin/msvc_nop.bat",
            "objcopy": "wrapper/bin/msvc_nop.bat",
            "objdump": "wrapper/bin/msvc_nop.bat",
            "strip": "wrapper/bin/msvc_nop.bat",
        },
        toolchain_identifier = "clang_cl_x64",
    )

    cc_toolchain(
        name = "windows_clang_cl_cc_toolchain",
        all_files = ":empty",
        ar_files = ":empty",
        as_files = ":empty",
        compiler_files = ":empty",
        dwp_files = ":empty",
        linker_files = ":empty",
        objcopy_files = ":empty",
        strip_files = ":empty",
        supports_param_files = 1,
        toolchain_config = ":windows_clang_cl_config",
        toolchain_identifier = "clang_cl_x64",
    )

    native.toolchain(
        name = "windows_clang_cl_toolchain",
        exec_compatible_with = [
            "@platforms//os:windows",
            "@platforms//cpu:x86_64",
        ],
        target_compatible_with = [
            "@platforms//os:windows",
            "@platforms//cpu:x86_64",
            "@bazel_tools//tools/cpp:clang-cl",
        ],
        toolchain = ":windows_clang_cl_cc_toolchain",
        toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
    )

    native.toolchain(
        name = "windows_msvc_toolchain",
        exec_compatible_with = [
            "@platforms//os:windows",
            "@platforms//cpu:x86_64",
        ],
        target_compatible_with = [
            "@platforms//os:windows",
            "@platforms//cpu:x86_64",
        ],
        toolchain = ":windows_msvc_cc_toolchain",
        toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
    )
