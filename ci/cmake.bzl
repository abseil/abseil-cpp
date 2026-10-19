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

"""Reusable hermetic multi-stage CMake build and test rules for Bazel RBE."""

load("@rules_cc//cc:action_names.bzl", "ACTION_NAMES")
load("@rules_cc//cc:find_cc_toolchain.bzl", "find_cc_toolchain", "use_cc_toolchain")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")

DEFAULT_CMAKE_MODES = [
    ("static", "OFF", []),
    ("shared", "ON", []),
    ("monolithic_shared", "ON", ["-DABSL_BUILD_MONOLITHIC_SHARED_LIBS=ON"]),
]

CmakePackageInfo = provider(
    doc = "Installed CMake package prefix archive (include/, lib/, lib/cmake/...).",
    fields = ["package_tar"],
)

_CmakeConfigureInfo = provider(
    doc = "Outputs of CMakeConfigure split into object, library, and test specs.",
    fields = [
        "dep_tars",
        "install_build_tar",
        "libs_spec",
        "objs_spec",
        "tests_spec",
    ],
)

_CmakeTestSpecInfo = provider(
    doc = "Per-test CMake build spec and required library list.",
    fields = [
        "req_libs_spec",
        "test_spec",
    ],
)

_CmakeHeadersInfo = provider(
    doc = "Transitive header and include files required to compile CMake targets.",
    fields = ["headers"],
)

_CmakeGtestSourcesInfo = provider(
    doc = "Source and header files collected from the GoogleTest Bazel target.",
    fields = ["files"],
)

def _cmake_headers_aspect_impl(target, ctx):
    if target.label.workspace_name != "":
        return [_CmakeHeadersInfo(headers = depset())]
    direct = []
    for attr_name in ("srcs", "hdrs", "textual_hdrs"):
        if hasattr(ctx.rule.files, attr_name):
            for f in getattr(ctx.rule.files, attr_name):
                if f.owner.workspace_name == "" and not f.path.endswith((".cc", ".c")):
                    direct.append(f)
    transitive = [
        d[_CmakeHeadersInfo].headers
        for d in getattr(ctx.rule.attr, "deps", [])
        if _CmakeHeadersInfo in d
    ]
    return [_CmakeHeadersInfo(headers = depset(direct, transitive = transitive))]

_cmake_headers_aspect = aspect(
    implementation = _cmake_headers_aspect_impl,
    attr_aspects = ["deps"],
)

def _cmake_gtest_aspect_impl(target, ctx):
    ws = target.label.workspace_name
    direct = []
    for attr_name in ("srcs", "hdrs", "textual_hdrs"):
        if hasattr(ctx.rule.files, attr_name):
            for f in getattr(ctx.rule.files, attr_name):
                if f.owner.workspace_name == ws:
                    direct.append(f)
    transitive = [
        d[_CmakeGtestSourcesInfo].files
        for d in getattr(ctx.rule.attr, "deps", [])
        if _CmakeGtestSourcesInfo in d and d.label.workspace_name == ws
    ]
    return [_CmakeGtestSourcesInfo(files = depset(direct, transitive = transitive))]

_cmake_gtest_aspect = aspect(
    implementation = _cmake_gtest_aspect_impl,
    attr_aspects = ["deps"],
)

_OPT_DEBUG_FLAGS = {
    "-DNDEBUG": True,
    "-O0": True,
    "-O1": True,
    "-O2": True,
    "-O3": True,
    "-Og": True,
    "-Os": True,
    "-Oz": True,
    "-g": True,
}

def _resolve_cc_tools(ctx):
    cc_toolchain = find_cc_toolchain(ctx)
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )
    cc = cc_common.get_tool_for_action(
        feature_configuration = feature_configuration,
        action_name = ACTION_NAMES.c_compile,
    )
    cxx = cc_common.get_tool_for_action(
        feature_configuration = feature_configuration,
        action_name = ACTION_NAMES.cpp_link_executable,
    )
    ar = cc_common.get_tool_for_action(
        feature_configuration = feature_configuration,
        action_name = ACTION_NAMES.cpp_link_static_library,
    )
    c_vars = cc_common.create_compile_variables(
        feature_configuration = feature_configuration,
        cc_toolchain = cc_toolchain,
        user_compile_flags = ctx.fragments.cpp.copts + ctx.fragments.cpp.conlyopts,
    )
    raw_cflags = cc_common.get_memory_inefficient_command_line(
        feature_configuration = feature_configuration,
        action_name = ACTION_NAMES.c_compile,
        variables = c_vars,
    )
    cxx_vars = cc_common.create_compile_variables(
        feature_configuration = feature_configuration,
        cc_toolchain = cc_toolchain,
        user_compile_flags = ctx.fragments.cpp.copts + ctx.fragments.cpp.cxxopts,
    )
    raw_cxxflags = cc_common.get_memory_inefficient_command_line(
        feature_configuration = feature_configuration,
        action_name = ACTION_NAMES.cpp_compile,
        variables = cxx_vars,
    )
    link_vars = cc_common.create_link_variables(
        feature_configuration = feature_configuration,
        cc_toolchain = cc_toolchain,
        is_using_linker = True,
        user_link_flags = ctx.fragments.cpp.linkopts,
    )
    raw_ldflags = cc_common.get_memory_inefficient_command_line(
        feature_configuration = feature_configuration,
        action_name = ACTION_NAMES.cpp_link_executable,
        variables = link_vars,
    )

    cflags = [f for f in raw_cflags if f not in _OPT_DEBUG_FLAGS]
    cxx_std = "17"
    cxxflags = []
    for f in raw_cxxflags:
        if f in _OPT_DEBUG_FLAGS:
            continue
        if f.startswith("-std=c++") or f.startswith("-std=gnu++"):
            cxx_std = f.split("++", 1)[1]
            continue
        cxxflags.append(f)
    cxxflags.append("-std=c++" + cxx_std)

    ldflags = []
    for f in raw_ldflags:
        if f.endswith("/libc++.a"):
            lib_dir = f.rsplit("/", 1)[0]
            ldflags.append("-lc++")
            ldflags.append("-Wl,-rpath=" + lib_dir)
        elif f.endswith("/libc++abi.a"):
            ldflags.append("-lc++abi")
        else:
            ldflags.append(f)

    return struct(
        cc = cc,
        cxx = cxx,
        ar = ar,
        cxx_std = cxx_std,
        cflags = cflags,
        cxxflags = cxxflags,
        ldflags = ldflags,
        files = cc_toolchain.all_files,
    )

def _format_cflag(f):
    return "--cflag=" + f

def _format_cxxflag(f):
    return "--cxxflag=" + f

def _format_ldflag(f):
    return "--ldflag=" + f

def _add_tool_args(args, tools):
    args.add("--cc=" + tools.cc)
    args.add("--cxx=" + tools.cxx)
    args.add("--ar=" + tools.ar)
    args.add("--cxx-std=" + tools.cxx_std)
    args.add_all(tools.cflags, map_each = _format_cflag)
    args.add_all(tools.cxxflags, map_each = _format_cxxflag)
    args.add_all(tools.ldflags, map_each = _format_ldflag)

def _format_src_map(f):
    return "--src-map=%s=%s" % (f.path, f.short_path)

def _format_hdr_map(f):
    return "--hdr-map=%s=%s" % (f.path, f.short_path)

def _format_repo_src_map(f):
    rel = f.short_path.split("/", 2)[2] if f.short_path.startswith("../") else f.short_path
    return "--src-map=%s=%s" % (f.path, rel)

def _format_dep_tar(f):
    return "--dep-tar=" + f.path

def _format_obj_tar(f):
    return "--obj-tar=" + f.path

def _format_cc_rel(f):
    return "--cc-rel=" + f.short_path

def _format_cmake_arg(arg):
    return "--cmake-arg=" + arg

def _format_log_short_path(f):
    return "--log=" + f.short_path

_COMMON_RUNNER_ATTRS = {
    "_cc_toolchain": attr.label(
        default = Label("@rules_cc//cc:current_cc_toolchain"),
    ),
    "_runner": attr.label(
        default = Label("//ci:cmake_runner.py"),
        allow_single_file = True,
        executable = True,
        cfg = "exec",
    ),
}

_RUNNER_ONLY_ATTRS = {
    "_runner": attr.label(
        default = Label("//ci:cmake_runner.py"),
        allow_single_file = True,
        executable = True,
        cfg = "exec",
    ),
}

def _cmake_googletest_impl(ctx):
    out_tar = ctx.actions.declare_file(ctx.label.name + ".tar")
    tools = _resolve_cc_tools(ctx)
    gtest_files = ctx.attr.gtest[_CmakeGtestSourcesInfo].files

    args = ctx.actions.args()
    args.use_param_file("@%s", use_always = True)
    args.set_param_file_format("multiline")
    args.add("build_gtest")
    _add_tool_args(args, tools)
    args.add("--build-type=" + ctx.attr.build_type)
    args.add("--shared=" + ctx.attr.shared)
    args.add("--out-tar=" + out_tar.path)
    args.add_all(gtest_files, map_each = _format_repo_src_map)

    ctx.actions.run(
        executable = ctx.executable._runner,
        arguments = [args],
        inputs = depset(transitive = [gtest_files, tools.files]),
        outputs = [out_tar],
        mnemonic = "CMakeGoogleTest",
        progress_message = "Building GoogleTest CMake package for %s (%s)" % (ctx.label.name, ctx.attr.build_type),
    )
    return [
        DefaultInfo(files = depset([out_tar])),
        CmakePackageInfo(package_tar = out_tar),
    ]

_cmake_googletest = rule(
    implementation = _cmake_googletest_impl,
    attrs = dict(_COMMON_RUNNER_ATTRS, **{
        "build_type": attr.string(mandatory = True),
        "gtest": attr.label(
            default = Label("@googletest//:gtest_main"),
            aspects = [_cmake_gtest_aspect],
        ),
        "shared": attr.string(mandatory = True),
    }),
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)

def _cmake_configure_impl(ctx):
    objs_spec = ctx.actions.declare_file(ctx.label.name + "_objs_spec.json")
    libs_spec = ctx.actions.declare_file(ctx.label.name + "_libs_spec.json")
    tests_spec = ctx.actions.declare_file(ctx.label.name + "_tests_spec.json")
    install_build_tar = ctx.actions.declare_file(ctx.label.name + "_install_build.tar")
    tools = _resolve_cc_tools(ctx)
    dep_tars = [d[CmakePackageInfo].package_tar for d in ctx.attr.cmake_deps]
    all_outputs = [objs_spec, libs_spec, tests_spec, install_build_tar]

    args = ctx.actions.args()
    args.use_param_file("@%s", use_always = True)
    args.set_param_file_format("multiline")
    args.add("configure")
    _add_tool_args(args, tools)
    args.add("--build-type=" + ctx.attr.build_type)
    args.add("--shared=" + ctx.attr.shared)
    args.add("--objs-spec-out=" + objs_spec.path)
    args.add("--libs-spec-out=" + libs_spec.path)
    args.add("--tests-spec-out=" + tests_spec.path)
    args.add("--install-build-out=" + install_build_tar.path)
    args.add_all(dep_tars, map_each = _format_dep_tar)
    args.add_all(ctx.files.srcs, map_each = _format_src_map)
    args.add_all(ctx.attr.cmake_args, map_each = _format_cmake_arg)

    ctx.actions.run(
        executable = ctx.executable._runner,
        arguments = [args],
        inputs = depset(ctx.files.srcs + dep_tars, transitive = [tools.files]),
        outputs = all_outputs,
        mnemonic = "CMakeConfigure",
        progress_message = "Configuring CMake for %s (%s)" % (ctx.label.name, ctx.attr.build_type),
    )
    return [
        DefaultInfo(files = depset(all_outputs)),
        _CmakeConfigureInfo(
            dep_tars = dep_tars,
            install_build_tar = install_build_tar,
            libs_spec = libs_spec,
            objs_spec = objs_spec,
            tests_spec = tests_spec,
        ),
    ]

_cmake_configure = rule(
    implementation = _cmake_configure_impl,
    attrs = dict(_COMMON_RUNNER_ATTRS, **{
        "build_type": attr.string(mandatory = True),
        "cmake_args": attr.string_list(),
        "cmake_deps": attr.label_list(providers = [CmakePackageInfo]),
        "shared": attr.string(mandatory = True),
        "srcs": attr.label_list(
            allow_files = True,
            mandatory = True,
        ),
    }),
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)

def _cmake_obj_spec_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name + ".json")
    objs_spec = ctx.attr.configure[_CmakeConfigureInfo].objs_spec
    cc_files = [f for f in ctx.files.srcs if f.path.endswith((".cc", ".c"))]

    args = ctx.actions.args()
    args.use_param_file("@%s", use_always = True)
    args.set_param_file_format("multiline")
    args.add("slice_obj_spec")
    args.add("--objs-spec=" + objs_spec.path)
    args.add("--out=" + out.path)
    args.add_all(cc_files, map_each = _format_cc_rel)

    ctx.actions.run(
        executable = ctx.executable._runner,
        arguments = [args],
        inputs = [objs_spec],
        outputs = [out],
        mnemonic = "CMakeObjSpec",
        progress_message = "Extracting CMake object spec for %s" % ctx.label,
    )
    return [DefaultInfo(files = depset([out]))]

_cmake_obj_spec = rule(
    implementation = _cmake_obj_spec_impl,
    attrs = dict(_RUNNER_ONLY_ATTRS, **{
        "configure": attr.label(
            mandatory = True,
            providers = [_CmakeConfigureInfo],
        ),
        "srcs": attr.label_list(allow_files = True),
    }),
)

def _cmake_lib_objs_impl(ctx):
    out_tar = ctx.actions.declare_file(ctx.label.name + ".tar")
    tools = _resolve_cc_tools(ctx)
    obj_spec = ctx.file.obj_spec
    dep_tars = ctx.attr.configure[_CmakeConfigureInfo].dep_tars
    dep_hdrs = [
        d[_CmakeHeadersInfo].headers
        for d in ctx.attr.deps
        if _CmakeHeadersInfo in d
    ]
    all_src_files = depset(
        ctx.files.srcs + ctx.files.hdrs + ctx.files.textual_hdrs + ctx.files.pkg_hdrs,
        transitive = dep_hdrs,
    )

    args = ctx.actions.args()
    args.use_param_file("@%s", use_always = True)
    args.set_param_file_format("multiline")
    args.add("compile_objs")
    _add_tool_args(args, tools)
    args.add("--obj-spec=" + obj_spec.path)
    args.add("--out-tar=" + out_tar.path)
    args.add_all(dep_tars, map_each = _format_dep_tar)
    args.add_all(all_src_files, map_each = _format_src_map)

    ctx.actions.run(
        executable = ctx.executable._runner,
        arguments = [args],
        inputs = depset([obj_spec] + dep_tars, transitive = [all_src_files, tools.files]),
        outputs = [out_tar],
        mnemonic = "CMakeLibObjs",
        progress_message = "Compiling CMake library objects for %s" % ctx.label,
    )
    return [DefaultInfo(files = depset([out_tar]))]

_cmake_lib_objs = rule(
    implementation = _cmake_lib_objs_impl,
    attrs = dict(_COMMON_RUNNER_ATTRS, **{
        "configure": attr.label(
            mandatory = True,
            providers = [_CmakeConfigureInfo],
        ),
        "deps": attr.label_list(
            aspects = [_cmake_headers_aspect],
        ),
        "hdrs": attr.label_list(allow_files = True),
        "obj_spec": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "pkg_hdrs": attr.label_list(allow_files = True),
        "srcs": attr.label_list(allow_files = True),
        "textual_hdrs": attr.label_list(allow_files = True),
    }),
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)

def _cmake_libs_link_impl(ctx):
    out_tar = ctx.actions.declare_file(ctx.label.name + ".tar")
    tools = _resolve_cc_tools(ctx)
    cfg_info = ctx.attr.configure[_CmakeConfigureInfo]
    libs_spec = cfg_info.libs_spec
    dep_tars = cfg_info.dep_tars
    obj_tars = ctx.files.obj_tars

    args = ctx.actions.args()
    args.use_param_file("@%s", use_always = True)
    args.set_param_file_format("multiline")
    args.add("link_libs")
    _add_tool_args(args, tools)
    args.add("--libs-spec=" + libs_spec.path)
    args.add("--out-tar=" + out_tar.path)
    args.add_all(dep_tars, map_each = _format_dep_tar)
    args.add_all(obj_tars, map_each = _format_obj_tar)

    ctx.actions.run(
        executable = ctx.executable._runner,
        arguments = [args],
        inputs = depset([libs_spec] + dep_tars + obj_tars, transitive = [tools.files]),
        outputs = [out_tar],
        mnemonic = "CMakeLibsLink",
        progress_message = "Linking CMake libraries for %s" % ctx.label.name,
    )
    return [DefaultInfo(files = depset([out_tar]))]

_cmake_libs_link = rule(
    implementation = _cmake_libs_link_impl,
    attrs = dict(_COMMON_RUNNER_ATTRS, **{
        "configure": attr.label(
            mandatory = True,
            providers = [_CmakeConfigureInfo],
        ),
        "obj_tars": attr.label_list(
            allow_files = True,
            mandatory = True,
        ),
    }),
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)

def _cmake_test_spec_impl(ctx):
    spec_out = ctx.actions.declare_file(ctx.label.name + "_spec.json")
    req_libs_out = ctx.actions.declare_file(ctx.label.name + "_req_libs.json")
    tests_spec = ctx.attr.configure[_CmakeConfigureInfo].tests_spec
    cc_files = [f for f in ctx.files.srcs if f.path.endswith((".cc", ".c"))]

    args = ctx.actions.args()
    args.use_param_file("@%s", use_always = True)
    args.set_param_file_format("multiline")
    args.add("slice_test_spec")
    args.add("--tests-spec=" + tests_spec.path)
    args.add("--spec-out=" + spec_out.path)
    args.add("--req-libs-out=" + req_libs_out.path)
    args.add_all(cc_files, map_each = _format_cc_rel)

    ctx.actions.run(
        executable = ctx.executable._runner,
        arguments = [args],
        inputs = [tests_spec],
        outputs = [spec_out, req_libs_out],
        mnemonic = "CMakeTestSpec",
        progress_message = "Extracting CMake test spec for %s" % ctx.label,
    )
    return [
        DefaultInfo(files = depset([spec_out, req_libs_out])),
        _CmakeTestSpecInfo(
            req_libs_spec = req_libs_out,
            test_spec = spec_out,
        ),
    ]

_cmake_test_spec = rule(
    implementation = _cmake_test_spec_impl,
    attrs = dict(_RUNNER_ONLY_ATTRS, **{
        "configure": attr.label(
            mandatory = True,
            providers = [_CmakeConfigureInfo],
        ),
        "srcs": attr.label_list(allow_files = True),
    }),
)

def _cmake_test_libs_impl(ctx):
    out_tar = ctx.actions.declare_file(ctx.label.name + ".tar")
    req_libs_spec = ctx.attr.test_spec[_CmakeTestSpecInfo].req_libs_spec
    libs_tar = ctx.file.libs

    args = ctx.actions.args()
    args.use_param_file("@%s", use_always = True)
    args.set_param_file_format("multiline")
    args.add("slice_test_libs")
    args.add("--req-libs-spec=" + req_libs_spec.path)
    args.add("--libs-tar=" + libs_tar.path)
    args.add("--out-tar=" + out_tar.path)

    ctx.actions.run(
        executable = ctx.executable._runner,
        arguments = [args],
        inputs = [req_libs_spec, libs_tar],
        outputs = [out_tar],
        mnemonic = "CMakeTestLibs",
        progress_message = "Packaging CMake libraries for %s" % ctx.label,
    )
    return [DefaultInfo(files = depset([out_tar]))]

_cmake_test_libs = rule(
    implementation = _cmake_test_libs_impl,
    attrs = dict(_RUNNER_ONLY_ATTRS, **{
        "libs": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "test_spec": attr.label(
            mandatory = True,
            providers = [_CmakeTestSpecInfo],
        ),
    }),
)

def _cmake_package_impl(ctx):
    out_tar = ctx.actions.declare_file(ctx.label.name + ".tar")
    tools = _resolve_cc_tools(ctx)
    install_build_tar = ctx.attr.configure[_CmakeConfigureInfo].install_build_tar
    libs_tar = ctx.file.libs_tar
    hdr_files = ctx.files.hdrs

    args = ctx.actions.args()
    args.use_param_file("@%s", use_always = True)
    args.set_param_file_format("multiline")
    args.add("package")
    _add_tool_args(args, tools)
    args.add("--install-build-tar=" + install_build_tar.path)
    args.add("--libs-tar=" + libs_tar.path)
    args.add("--out-tar=" + out_tar.path)
    args.add_all(hdr_files, map_each = _format_hdr_map)

    ctx.actions.run(
        executable = ctx.executable._runner,
        arguments = [args],
        inputs = depset([install_build_tar, libs_tar] + hdr_files, transitive = [tools.files]),
        outputs = [out_tar],
        mnemonic = "CMakePackage",
        progress_message = "Installing CMake package for %s" % ctx.label.name,
    )
    return [
        DefaultInfo(files = depset([out_tar])),
        CmakePackageInfo(package_tar = out_tar),
    ]

_cmake_package = rule(
    implementation = _cmake_package_impl,
    attrs = dict(_COMMON_RUNNER_ATTRS, **{
        "configure": attr.label(
            mandatory = True,
            providers = [_CmakeConfigureInfo],
        ),
        "hdrs": attr.label_list(allow_files = True),
        "libs_tar": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
    }),
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)

def _cmake_test_action_impl(ctx):
    out_log = ctx.actions.declare_file(ctx.label.name + ".log")
    for feature_name, disable_tag in [
        ("asan", "noasan"),
        ("tsan", "notsan"),
        ("ubsan", "noubsan"),
        ("msan", "nomsan"),
    ]:
        if feature_name in ctx.features and disable_tag in ctx.attr.test_tags:
            ctx.actions.write(
                output = out_log,
                content = "SKIPPED %s (%s)\n" % (ctx.label, disable_tag),
            )
            return [DefaultInfo(files = depset([out_log]))]

    tools = _resolve_cc_tools(ctx)
    test_spec_file = ctx.attr.test_spec[_CmakeTestSpecInfo].test_spec
    test_libs_file = ctx.file.test_libs
    dep_hdrs = [
        d[_CmakeHeadersInfo].headers
        for d in ctx.attr.deps
        if _CmakeHeadersInfo in d
    ]
    hdr_files = depset(ctx.files.pkg_hdrs, transitive = dep_hdrs)
    src_files = ctx.files.srcs
    data_files = ctx.files.data

    args = ctx.actions.args()
    args.use_param_file("@%s", use_always = True)
    args.set_param_file_format("multiline")
    args.add("run_test")
    _add_tool_args(args, tools)
    args.add("--test-spec=" + test_spec_file.path)
    args.add("--test-libs-tar=" + test_libs_file.path)
    args.add("--test-label=" + str(ctx.label))
    args.add("--out-log=" + out_log.path)
    args.add_all(hdr_files, map_each = _format_hdr_map)
    args.add_all(src_files, map_each = _format_src_map)

    ctx.actions.run(
        executable = ctx.executable._runner,
        arguments = [args],
        inputs = depset([test_spec_file, test_libs_file] + src_files + data_files, transitive = [hdr_files, tools.files]),
        outputs = [out_log],
        mnemonic = "CMakeTest",
        progress_message = "Building and running CMake test %s" % ctx.label,
    )
    return [DefaultInfo(files = depset([out_log]))]

_cmake_test_action = rule(
    implementation = _cmake_test_action_impl,
    attrs = dict(_COMMON_RUNNER_ATTRS, **{
        "data": attr.label_list(allow_files = True),
        "deps": attr.label_list(
            aspects = [_cmake_headers_aspect],
        ),
        "pkg_hdrs": attr.label_list(allow_files = True),
        "srcs": attr.label_list(
            allow_files = True,
            mandatory = True,
        ),
        "test_libs": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "test_spec": attr.label(
            mandatory = True,
            providers = [_CmakeTestSpecInfo],
        ),
        "test_tags": attr.string_list(),
    }),
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)

def _cmake_install_action_impl(ctx):
    out_log = ctx.actions.declare_file(ctx.label.name + ".log")
    for s in ("asan", "tsan", "ubsan", "msan"):
        if s in ctx.features:
            ctx.actions.write(
                output = out_log,
                content = "SKIPPED %s (%s active)\n" % (ctx.label, s),
            )
            return [DefaultInfo(files = depset([out_log]))]

    tools = _resolve_cc_tools(ctx)

    args = ctx.actions.args()
    args.use_param_file("@%s", use_always = True)
    args.set_param_file_format("multiline")
    args.add("install_test")
    _add_tool_args(args, tools)
    args.add("--link-type=" + ctx.attr.link_type)
    args.add("--script=" + ctx.attr.script)
    args.add("--out-log=" + out_log.path)
    args.add_all(ctx.files.srcs, map_each = _format_src_map)

    ctx.actions.run(
        executable = ctx.executable._runner,
        arguments = [args],
        inputs = depset(ctx.files.srcs, transitive = [tools.files]),
        outputs = [out_log],
        mnemonic = "CMakeInstallTest",
        progress_message = "Running CMake install test %s" % ctx.label.name,
    )
    return [DefaultInfo(files = depset([out_log]))]

_cmake_install_action = rule(
    implementation = _cmake_install_action_impl,
    attrs = dict(_COMMON_RUNNER_ATTRS, **{
        "link_type": attr.string(mandatory = True),
        "script": attr.string(mandatory = True),
        "srcs": attr.label_list(
            allow_files = True,
            mandatory = True,
        ),
    }),
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)

def _cmake_log_test_impl(ctx):
    exe = ctx.actions.declare_file(ctx.label.name)
    args_file = ctx.actions.declare_file(ctx.label.name + ".args")
    log_files = [f for f in ctx.files.data if f.path.endswith(".log")]

    args = ctx.actions.args()
    args.set_param_file_format("multiline")
    args.add("verify_logs")
    args.add("--mode=" + ctx.attr.mode)
    args.add_all(log_files, map_each = _format_log_short_path)
    ctx.actions.write(output = args_file, content = args)

    ctx.actions.symlink(
        output = exe,
        target_file = ctx.executable._runner,
        is_executable = True,
    )
    runfiles = ctx.runfiles(files = [args_file] + ctx.files.data)
    for d in ctx.attr.data:
        runfiles = runfiles.merge(d[DefaultInfo].default_runfiles)
    return [DefaultInfo(
        files = depset([exe]),
        executable = exe,
        runfiles = runfiles,
    )]

_cmake_log_test = rule(
    implementation = _cmake_log_test_impl,
    attrs = {
        "data": attr.label_list(allow_files = True),
        "mode": attr.string(mandatory = True),
        "_runner": attr.label(
            default = Label("//ci:cmake_runner.py"),
            allow_single_file = True,
            executable = True,
            cfg = "exec",
        ),
    },
    exec_groups = {
        "test": exec_group(),
    },
    test = True,
)

# buildifier: disable=unnamed-macro
def cmake_package(modes = [m[0] for m in DEFAULT_CMAKE_MODES]):
    """Inspects existing rules in the package to create per-library object actions and per-test actions.

    Args:
      modes: List of CMake mode suffixes to generate targets for.
    """
    lib_srcs = {}
    lib_rules_by_name = {}
    all_cc_lib_labels = []
    seen_lib_cc_sets = {}
    objs_by_mode = {mode: [] for mode in modes}
    tests_by_mode = {mode: [] for mode in modes}

    test_groups = []
    seen_test_src_sets = {}
    claimed_cc_srcs = {}
    non_lib_cc_srcs = {}
    used_test_names = {}

    pkg_name = native.package_name()
    pkg_prefix = "//" + pkg_name + "/" if pkg_name else "//"
    subpkgs = [
        pkg_prefix + sub
        for sub in native.subpackages(
            include = ["**"],
            exclude = ["bazel-*", "bazel-*/**", "ci", "ci/**"],
            allow_empty = True,
        )
    ]

    native.filegroup(
        name = "cmake_configure_srcs",
        srcs = native.glob(["**/CMakeLists.txt", "**/*.cmake", "options.h"], allow_empty = True) + [
            sub + ":cmake_configure_srcs"
            for sub in subpkgs
        ],
        tags = ["cmake", "manual"],
        visibility = ["//:__subpackages__"],
    )

    native.filegroup(
        name = "cmake_pkg_hdrs",
        srcs = native.glob(["**/*.h", "**/*.inc", "**/*.hpp", "**/*.def"], allow_empty = True),
        tags = ["cmake", "manual"],
        visibility = ["//:__subpackages__"],
    )

    native.filegroup(
        name = "cmake_all_hdrs",
        srcs = [":cmake_pkg_hdrs"] + [
            sub + ":cmake_all_hdrs"
            for sub in subpkgs
        ],
        tags = ["cmake", "manual"],
        visibility = ["//:__subpackages__"],
    )

    for r in native.existing_rules().values():
        if r.get("kind") == "cc_library":
            lib_rules_by_name[":" + r["name"]] = r
            all_cc_lib_labels.append(":" + r["name"])
            srcs = r.get("srcs") or ()
            hdrs = r.get("hdrs") or ()
            for f in (srcs if type(srcs) == "tuple" else ()) + (hdrs if type(hdrs) == "tuple" else ()):
                if type(f) == "string" and f.startswith(":"):
                    lib_srcs[f[1:]] = True

            should_add_objs = False
            if type(srcs) == "tuple":
                cc_key = tuple(sorted([f for f in srcs if type(f) == "string" and (f.endswith(".cc") or f.endswith(".c"))]))
                if cc_key and cc_key not in seen_lib_cc_sets:
                    seen_lib_cc_sets[cc_key] = True
                    should_add_objs = True
            elif srcs:
                should_add_objs = True

            if should_add_objs:
                src_list = list(srcs) if type(srcs) == "tuple" else srcs
                for mode in modes:
                    spec_name = "cmake_obj_spec_" + mode + "_" + r["name"]
                    _cmake_obj_spec(
                        name = spec_name,
                        testonly = True,
                        configure = "//:cmake_configure_" + mode,
                        srcs = src_list,
                        tags = ["cmake", "manual"],
                    )
                    obj_name = "cmake_objs_" + mode + "_" + r["name"]
                    _cmake_lib_objs(
                        name = obj_name,
                        testonly = True,
                        configure = "//:cmake_configure_" + mode,
                        obj_spec = ":" + spec_name,
                        srcs = src_list,
                        hdrs = list(hdrs) if type(hdrs) == "tuple" else hdrs,
                        textual_hdrs = list(r.get("textual_hdrs")) if type(r.get("textual_hdrs")) == "tuple" else r.get("textual_hdrs"),
                        pkg_hdrs = [":cmake_pkg_hdrs"],
                        deps = list(r.get("deps")) if type(r.get("deps")) == "tuple" else r.get("deps"),
                        tags = ["cmake", "manual"],
                    )
                    objs_by_mode[mode].append(":" + obj_name)

    for mode in modes:
        native.filegroup(
            name = "cmake_objs_" + mode,
            testonly = True,
            srcs = objs_by_mode[mode] + [
                sub + ":cmake_objs_" + mode
                for sub in subpkgs
            ],
            tags = ["cmake", "manual"],
            visibility = ["//:__subpackages__"],
        )

    for r in native.existing_rules().values():
        kind = r.get("kind")
        if kind in ("cc_test", "cc_binary"):
            srcs = r.get("srcs") or ()
            if type(srcs) == "tuple":
                for f in srcs:
                    if type(f) == "string" and f.startswith(":") and (f.endswith(".cc") or f.endswith(".c")):
                        rel = f[1:]
                        if rel not in lib_srcs:
                            non_lib_cc_srcs[rel] = True
        if kind == "cc_test":
            tags = r.get("tags") or ()
            if "benchmark" in tags or r["name"].endswith("_benchmark"):
                continue
            srcs = r.get("srcs") or ()
            deps = r.get("deps")
            if type(srcs) == "tuple" and not srcs and type(deps) == "tuple":
                # Handle cc_test targets whose .cc files live in a same-package testonly cc_library.
                inferred = []
                for d in deps:
                    if d in lib_rules_by_name and lib_rules_by_name[d].get("testonly"):
                        d_srcs = lib_rules_by_name[d].get("srcs") or ()
                        if type(d_srcs) == "tuple":
                            inferred.extend(list(d_srcs))
                srcs = tuple(inferred)
            if type(srcs) == "tuple" and srcs:
                cc_key = tuple(sorted([f for f in srcs if type(f) == "string" and (f.endswith(".cc") or f.endswith(".c"))]))
                if cc_key and cc_key not in seen_test_src_sets:
                    seen_test_src_sets[cc_key] = True
                    for f in cc_key:
                        if f.startswith(":"):
                            claimed_cc_srcs[f[1:]] = True
                    data = r.get("data") or ()
                    test_tags = [
                        t
                        for t in (tags if type(tags) == "tuple" else ())
                        if type(t) == "string" and t in ("noasan", "notsan", "noubsan", "nomsan")
                    ]
                    used_test_names[r["name"]] = True
                    test_groups.append(struct(
                        name = r["name"],
                        srcs = list(srcs),
                        deps = list(deps) if type(deps) == "tuple" else deps,
                        data = list(data) if type(data) == "tuple" else data,
                        test_tags = test_tags,
                    ))

    for f in native.glob(["**/*_test.cc", "**/*_test.c"], allow_empty = True):
        if f not in claimed_cc_srcs:
            if f not in lib_srcs:
                non_lib_cc_srcs[f] = True
            base = f[:-3] if f.endswith(".cc") else f[:-2]
            tname = base.replace("/", "_")
            if tname in used_test_names:
                tname = tname + "_file"
            used_test_names[tname] = True
            test_groups.append(struct(
                name = tname,
                srcs = [":" + f],
                deps = all_cc_lib_labels,
                data = [],
                test_tags = [],
            ))

    native.filegroup(
        name = "cmake_lib_srcs",
        srcs = native.glob(
            ["**"],
            exclude = [
                "BUILD",
                "BUILD.bazel",
                "**/*_benchmark.cc",
                "**/benchmarks.cc",
            ] + non_lib_cc_srcs.keys(),
            allow_empty = True,
        ) + [
            sub + ":cmake_lib_srcs"
            for sub in subpkgs
        ],
        tags = ["cmake", "manual"],
        visibility = ["//:__subpackages__"],
    )

    for g in test_groups:
        for mode in modes:
            spec_name = "cmake_test_spec_" + mode + "_" + g.name
            _cmake_test_spec(
                name = spec_name,
                testonly = True,
                configure = "//:cmake_configure_" + mode,
                srcs = g.srcs,
                tags = ["cmake", "manual"],
            )
            libs_name = "cmake_test_libs_" + mode + "_" + g.name
            _cmake_test_libs(
                name = libs_name,
                testonly = True,
                libs = "//:cmake_libs_" + mode,
                test_spec = ":" + spec_name,
                tags = ["cmake", "manual"],
            )
            target_name = "cmake_" + mode + "_" + g.name
            _cmake_test_action(
                name = target_name,
                testonly = True,
                test_libs = ":" + libs_name,
                test_spec = ":" + spec_name,
                srcs = g.srcs,
                pkg_hdrs = [":cmake_pkg_hdrs"],
                deps = g.deps,
                data = g.data,
                test_tags = g.test_tags,
                tags = ["cmake", "manual"],
            )
            tests_by_mode[mode].append(":" + target_name)

    for mode in modes:
        native.filegroup(
            name = "cmake_tests_" + mode,
            testonly = True,
            srcs = tests_by_mode[mode] + [
                sub + ":cmake_tests_" + mode
                for sub in subpkgs
            ],
            tags = ["cmake", "manual"],
            visibility = ["//:__subpackages__"],
        )

# buildifier: disable=unnamed-macro
def define_cmake_tests(
        root_srcs,
        packages = None,
        exclude_packages = ["ci"],
        modes = DEFAULT_CMAKE_MODES,
        cmake_args = [
            "-DABSL_BUILD_TESTING=ON",
            "-DABSL_BUILD_TEST_HELPERS=ON",
            "-DABSL_USE_EXTERNAL_GOOGLETEST=ON",
            "-DABSL_FIND_GOOGLETEST=ON",
        ],
        cmake_deps = {},
        build_googletest = True,
        install_test_script = None):
    """Defines CMake configure, library link, install package, and bundled top-level CMake test targets.

    Args:
      root_srcs: Top-level CMake configuration files.
      packages: Optional list of Bazel package paths; auto-discovered when None.
      exclude_packages: Subpackages to exclude from auto-discovery.
      modes: List of (mode_suffix, shared, extra_cmake_args) tuples.
      cmake_args: Extra CMake configure flags passed to all modes.
      cmake_deps: Dict mapping mode_suffix to pre-packaged CMake dependency tar labels.
      build_googletest: Whether to build and provide GoogleTest from @googletest.
      install_test_script: Optional script path for CMake installation integration tests.
    """
    if packages == None:
        if "cmake_configure_srcs" in native.existing_rules():
            packages = [""]
        else:
            exclude_globs = ["bazel-*", "bazel-*/**"]
            for p in exclude_packages:
                exclude_globs.append(p)
                exclude_globs.append(p + "/**")
            packages = [
                "//" + p
                for p in native.subpackages(
                    include = ["**"],
                    exclude = exclude_globs,
                    allow_empty = True,
                )
            ]

    native.config_setting(
        name = "cmake_opt_mode",
        values = {"compilation_mode": "opt"},
    )

    build_type_select = select({
        ":cmake_opt_mode": "Release",
        "//conditions:default": "Debug",
    })

    native.filegroup(
        name = "cmake_root_configure_srcs",
        srcs = root_srcs + [pkg + ":cmake_configure_srcs" for pkg in packages],
        tags = ["cmake", "manual"],
    )

    native.filegroup(
        name = "cmake_root_lib_srcs",
        srcs = root_srcs + [pkg + ":cmake_lib_srcs" for pkg in packages],
        tags = ["cmake", "manual"],
    )

    native.filegroup(
        name = "cmake_root_all_hdrs",
        srcs = [pkg + ":cmake_all_hdrs" for pkg in packages],
        tags = ["cmake", "manual"],
    )

    top_level_tests = []
    for mode, shared, mode_extra in modes:
        if type(mode_extra) == "string":
            extra_args = ["-DABSL_BUILD_MONOLITHIC_SHARED_LIBS=" + mode_extra]
        else:
            extra_args = list(mode_extra)

        mode_deps = []
        if build_googletest:
            gtest_name = "cmake_gtest_" + mode
            _cmake_googletest(
                name = gtest_name,
                testonly = True,
                build_type = build_type_select,
                shared = shared,
                tags = ["cmake", "manual"],
                visibility = ["//visibility:public"],
            )
            mode_deps.append(":" + gtest_name)

        if type(cmake_deps) == "dict":
            mode_deps.extend(cmake_deps.get(mode, []))
        else:
            mode_deps.extend(cmake_deps)

        _cmake_configure(
            name = "cmake_configure_" + mode,
            testonly = True,
            build_type = build_type_select,
            cmake_args = cmake_args + extra_args,
            cmake_deps = mode_deps,
            shared = shared,
            srcs = [":cmake_root_configure_srcs"],
            tags = ["cmake", "manual"],
            visibility = ["//:__subpackages__"],
        )

        _cmake_libs_link(
            name = "cmake_libs_" + mode,
            testonly = True,
            configure = ":cmake_configure_" + mode,
            obj_tars = [pkg + ":cmake_objs_" + mode for pkg in packages],
            tags = ["cmake", "manual"],
            visibility = ["//:__subpackages__"],
        )

        _cmake_package(
            name = "cmake_package_" + mode,
            testonly = True,
            configure = ":cmake_configure_" + mode,
            hdrs = [":cmake_root_all_hdrs"],
            libs_tar = ":cmake_libs_" + mode,
            tags = ["cmake", "manual"],
            visibility = ["//visibility:public"],
        )

        test_name = "cmake_" + mode + "_test"
        top_level_tests.append(":" + test_name)
        _cmake_log_test(
            name = test_name,
            size = "small",
            data = [pkg + ":cmake_tests_" + mode for pkg in packages],
            mode = mode,
            tags = ["cmake"],
            target_compatible_with = ["@platforms//os:linux"],
        )

    if install_test_script:
        for mode, link_type in [("static", "STATIC"), ("shared", "DYNAMIC")]:
            action_name = "cmake_install_" + mode + "_action"
            _cmake_install_action(
                name = action_name,
                testonly = True,
                link_type = link_type,
                script = install_test_script,
                srcs = [":cmake_root_lib_srcs"],
                tags = ["cmake", "manual"],
                target_compatible_with = select({
                    ":cmake_opt_mode": ["@platforms//os:linux"],
                    "//conditions:default": ["@platforms//:incompatible"],
                }),
            )

            install_test_name = "cmake_install_" + mode + "_test"
            top_level_tests.append(":" + install_test_name)
            _cmake_log_test(
                name = install_test_name,
                size = "small",
                data = [":" + action_name],
                mode = "install_" + mode,
                tags = ["cmake"],
                target_compatible_with = select({
                    ":cmake_opt_mode": ["@platforms//os:linux"],
                    "//conditions:default": ["@platforms//:incompatible"],
                }),
            )

    native.test_suite(
        name = "cmake_tests",
        tags = ["cmake", "manual"],
        tests = top_level_tests,
    )
