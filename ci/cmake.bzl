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

"""Reusable multi-stage CMake build and per-test execution rules for Bazel RBE."""

DEFAULT_CMAKE_MODES = [
    ("static", "OFF", "OFF"),
    ("shared", "ON", "OFF"),
    ("monolithic_shared", "ON", "ON"),
]

_CmakeHeadersInfo = provider(
    doc = "Transitive header and include files required to compile CMake targets.",
    fields = ["headers"],
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

def _cmake_sh_test_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name + ".sh")
    log_paths = " ".join(['"%s"' % f.short_path for f in ctx.files.data if f.path.endswith(".log")])
    ctx.actions.write(
        output = out,
        content = ctx.attr.content.replace("__CMAKE_LOG_FILES__", log_paths),
        is_executable = True,
    )
    runfiles = ctx.runfiles(files = ctx.files.data)
    for d in ctx.attr.data:
        runfiles = runfiles.merge(d[DefaultInfo].default_runfiles)
    return [DefaultInfo(
        files = depset([out]),
        executable = out,
        runfiles = runfiles,
    )]

_cmake_sh_test = rule(
    implementation = _cmake_sh_test_impl,
    attrs = {
        "content": attr.string(mandatory = True),
        "data": attr.label_list(allow_files = True),
    },
    exec_groups = {
        "test": exec_group(),
    },
    test = True,
)

def _cmake_configure_impl(ctx):
    out_tar = ctx.actions.declare_file(ctx.label.name + ".tar")
    gtest_file = ctx.file.googletest_archive
    inputs = depset(ctx.files.srcs + [gtest_file])

    cmd = """
set -euo pipefail
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:${{PATH}}"
GTEST_ARCHIVE="$(realpath "{gtest_path}")"
OUT_TAR="$(realpath -m "{out_tar_path}")"
WORK_ROOT="/tmp/cmake-work"
exec 9>/tmp/cmake-work.lock
flock 9
rm -rf "${{WORK_ROOT}}"
mkdir -p "${{WORK_ROOT}}/src"

cp -RL {top_level_entries} "${{WORK_ROOT}}/src/"
chmod -R u+w "${{WORK_ROOT}}/src"

# Create 0-byte stubs for all source/header files referenced in CMakeLists.txt
# and CMake/*.cmake so CMake generates the full Ninja/CTest graph without
# depending on any project .cc or .h file contents.
python3 -c '
import os, re
src_root = "/tmp/cmake-work/src"
for root, dirs, files in os.walk(src_root):
    for f in files:
        os.utime(os.path.join(root, f), (946684800, 946684800))
        if f == "CMakeLists.txt" or f.endswith(".cmake"):
            with open(os.path.join(root, f)) as cf:
                text = cf.read()
            base_dir = os.path.join(src_root, "absl") if f == "AbseilDll.cmake" else root
            for token in re.findall(r"[\\"\\s]([A-Za-z0-9_./-]+\\.(?:cc|c|h|inc))[\\"\\s)]", text):
                candidate = os.path.join(base_dir, token)
                if not os.path.exists(candidate):
                    os.makedirs(os.path.dirname(candidate), exist_ok=True)
                    open(candidate, "w").close()
                    os.utime(candidate, (946684800, 946684800))
'

export CC=/usr/local/bin/gcc
export CXX=/usr/local/bin/g++
export CFLAGS="-Werror"
export CXXFLAGS="-Werror"
export CMAKE_GENERATOR=Ninja
export CMAKE_BUILD_PARALLEL_LEVEL="$(nproc)"

cmake -S "${{WORK_ROOT}}/src" -B "${{WORK_ROOT}}/build" \\
  -DCMAKE_SUPPRESS_REGENERATION=ON \\
  -DABSL_BUILD_TESTING=ON \\
  -DABSL_GOOGLETEST_DOWNLOAD_URL="file://${{GTEST_ARCHIVE}}" \\
  -DCMAKE_BUILD_TYPE="{build_type}" \\
  -DCMAKE_C_FLAGS_DEBUG="-Og" \\
  -DCMAKE_CXX_FLAGS_DEBUG="-Og" \\
  -DCMAKE_EXE_LINKER_FLAGS_DEBUG="" \\
  -DCMAKE_SHARED_LINKER_FLAGS_DEBUG="" \\
  -DCMAKE_CXX_STANDARD=17 \\
  -DBUILD_SHARED_LIBS={shared} \\
  -DABSL_BUILD_MONOLITHIC_SHARED_LIBS={monolithic} \\
  -DCMAKE_JOB_POOLS="link=$(nproc)" \\
  -DCMAKE_JOB_POOL_LINK=link

# Build GoogleTest/GMock once during configure and prepare rules.ninja/build.ninja
# for isolated per-cc_library object compilation on RBE.
python3 -c '
import os, re, subprocess
build_dir = "/tmp/cmake-work/build"
out = subprocess.check_output(["ninja", "-C", build_dir, "-t", "targets", "all"], text=True)
gtest_targets = [
    line.split(":", 1)[0]
    for line in out.splitlines()
    if ("_STATIC_LIBRARY_LINKER__" in line or "_SHARED_LIBRARY_LINKER__" in line)
    and line.startswith("lib/")
]
if gtest_targets:
    subprocess.check_call(["ninja", "-C", build_dir] + gtest_targets)

import shutil
for d in ("googletest-build", "googletest-external"):
    p = os.path.join(build_dir, d)
    if os.path.exists(p):
        shutil.rmtree(p)

rules_file = os.path.join(build_dir, "CMakeFiles", "rules.ninja")
if os.path.exists(rules_file):
    with open(rules_file) as rf:
        rtext = rf.read()
    rtext = re.sub(r"^\\s*deps\\s*=\\s*gcc\\s*\\n", "", rtext, flags=re.M)
    rtext = re.sub(r"^\\s*depfile\\s*=\\s*\\$DEP_FILE\\s*\\n", "", rtext, flags=re.M)
    with open(rules_file, "w") as rf:
        rf.write(rtext)

bninja_file = os.path.join(build_dir, "build.ninja")
if os.path.exists(bninja_file):
    with open(bninja_file) as bf:
        btext = bf.read()
    btext = re.sub(
        r"(build\\s+\\S+\\.o:\\s+C(?:XX)?_COMPILER__\\S+\\s+[^\\n|]+?)\\s*\\|\\|\\s*cmake_object_order_depends_target_\\S+",
        r"\\1",
        btext,
    )
    btext = re.sub(
        r"^build build\\.ninja: RERUN_CMAKE\\b",
        "build build.ninja: phony",
        btext,
        flags=re.M,
    )
    with open(bninja_file, "w") as bf:
        bf.write(btext)

for root, dirs, files in os.walk(build_dir):
    for f in files:
        os.utime(os.path.join(root, f), (1577836800, 1577836800))
'

tar -cf "${{OUT_TAR}}" -C "${{WORK_ROOT}}" src build
rm -rf "${{WORK_ROOT}}"
""".format(
        gtest_path = gtest_file.path,
        out_tar_path = out_tar.path,
        top_level_entries = " ".join(ctx.attr.top_level_entries),
        build_type = ctx.attr.build_type,
        shared = ctx.attr.shared,
        monolithic = ctx.attr.monolithic,
    )

    ctx.actions.run_shell(
        inputs = inputs,
        outputs = [out_tar],
        command = cmd,
        mnemonic = "CMakeConfigure",
        progress_message = "Configuring CMake and building GoogleTest for %s (%s)" % (ctx.label.name, ctx.attr.build_type),
    )
    return [DefaultInfo(files = depset([out_tar]))]

_cmake_configure = rule(
    implementation = _cmake_configure_impl,
    attrs = {
        "build_type": attr.string(mandatory = True),
        "googletest_archive": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "monolithic": attr.string(mandatory = True),
        "shared": attr.string(mandatory = True),
        "srcs": attr.label_list(
            allow_files = True,
            mandatory = True,
        ),
        "top_level_entries": attr.string_list(mandatory = True),
    },
)

def _cmake_lib_objs_impl(ctx):
    out_tar = ctx.actions.declare_file(ctx.label.name + ".tar")
    configure_tar = ctx.file.configure_tar
    dep_hdrs = [
        d[_CmakeHeadersInfo].headers
        for d in ctx.attr.deps
        if _CmakeHeadersInfo in d
    ]
    all_src_files = depset(
        ctx.files.srcs + ctx.files.hdrs + ctx.files.textual_hdrs + ctx.files.pkg_hdrs,
        transitive = dep_hdrs,
    )
    cc_files = [f for f in ctx.files.srcs if f.path.endswith((".cc", ".c"))]
    inputs = depset([configure_tar], transitive = [all_src_files])

    copy_entries = [(f.path, f.short_path) for f in all_src_files.to_list()]
    cc_rel_paths = [f.short_path for f in cc_files]

    cmd = """
set -euo pipefail
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:${{PATH}}"
CONFIGURE_TAR="$(realpath "{configure_tar_path}")"
export OUT_TAR="$(realpath -m "{out_tar_path}")"
export EXEC_ROOT="${{PWD}}"
WORK_ROOT="/tmp/cmake-work"
exec 9>/tmp/cmake-work.lock
flock 9
rm -rf "${{WORK_ROOT}}"
mkdir -p "${{WORK_ROOT}}"

tar -xf "${{CONFIGURE_TAR}}" -C "${{WORK_ROOT}}"

export CC=/usr/local/bin/gcc
export CXX=/usr/local/bin/g++
export CFLAGS="-Werror"
export CXXFLAGS="-Werror"
export CMAKE_GENERATOR=Ninja
export CMAKE_BUILD_PARALLEL_LEVEL="$(nproc)"

python3 -c '
import os, re, shutil, subprocess

exec_root = os.environ["EXEC_ROOT"]
work_root = "/tmp/cmake-work"
src_root = os.path.join(work_root, "src")
build_dir = os.path.join(work_root, "build")
out_tar = os.environ["OUT_TAR"]

copy_entries = {copy_entries_repr}
cc_rel_paths = {cc_rel_paths_repr}

for src_p, rel_p in copy_entries:
    full_src = os.path.join(exec_root, src_p)
    full_dst = os.path.join(src_root, rel_p)
    if os.path.exists(full_src):
        os.makedirs(os.path.dirname(full_dst), exist_ok=True)
        shutil.copyfile(full_src, full_dst)
        os.utime(full_dst, None)

all_targets_out = subprocess.check_output(
    ["ninja", "-C", build_dir, "-t", "targets", "all"], text=True
)
lib_cmake_names = set()
for line in all_targets_out.splitlines():
    if "_STATIC_LIBRARY_LINKER__" in line or "_SHARED_LIBRARY_LINKER__" in line:
        m = re.search(r"_(?:STATIC|SHARED)_LIBRARY_LINKER__(.+?)_(?:Release|Debug|RelWithDebInfo|MinSizeRel)\\b", line)
        if m:
            lib_cmake_names.add(m.group(1))

obj_targets = set()
for rel_cc in cc_rel_paths:
    dst_path = os.path.join(src_root, rel_cc)
    p = subprocess.run(
        ["ninja", "-C", build_dir, "-t", "query", dst_path],
        capture_output=True, text=True
    )
    if p.returncode == 0:
        for m in re.finditer(r"^\\s*(\\S*CMakeFiles/([^/]+)\\.dir/\\S+\\.o)\\s*$", p.stdout, re.M):
            if m.group(2) in lib_cmake_names:
                obj_targets.add(m.group(1))

sorted_objs = sorted(obj_targets)
if sorted_objs:
    subprocess.check_call(["ninja", "-C", build_dir] + sorted_objs)
    for o in sorted_objs:
        os.utime(os.path.join(build_dir, o), (1577836800, 1577836800))

log_part_path = os.path.join(build_dir, ".ninja_log.part")
nlog_path = os.path.join(build_dir, ".ninja_log")
part_lines = []
if sorted_objs and os.path.exists(nlog_path):
    obj_set = set(sorted_objs)
    with open(nlog_path) as nf:
        for line in nf:
            if line.startswith("#"):
                continue
            parts = line.rstrip("\\n").split("\\t")
            if len(parts) >= 5 and parts[3] in obj_set:
                parts[2] = "1577836800000000000"
                part_lines.append("\\t".join(parts) + "\\n")
with open(log_part_path, "w") as pf:
    pf.writelines(part_lines)

subprocess.check_call(["tar", "-cf", out_tar, "-C", build_dir, ".ninja_log.part"] + sorted_objs)
'
rm -rf "${{WORK_ROOT}}"
""".format(
        configure_tar_path = configure_tar.path,
        out_tar_path = out_tar.path,
        copy_entries_repr = repr(copy_entries),
        cc_rel_paths_repr = repr(cc_rel_paths),
    )

    ctx.actions.run_shell(
        inputs = inputs,
        outputs = [out_tar],
        command = cmd,
        mnemonic = "CMakeLibObjs",
        progress_message = "Compiling CMake library objects for %s" % ctx.label,
    )
    return [DefaultInfo(files = depset([out_tar]))]

_cmake_lib_objs = rule(
    implementation = _cmake_lib_objs_impl,
    attrs = {
        "configure_tar": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "deps": attr.label_list(
            aspects = [_cmake_headers_aspect],
        ),
        "hdrs": attr.label_list(allow_files = True),
        "pkg_hdrs": attr.label_list(allow_files = True),
        "srcs": attr.label_list(allow_files = True),
        "textual_hdrs": attr.label_list(allow_files = True),
    },
)

def _cmake_libs_link_impl(ctx):
    out_tar = ctx.actions.declare_file(ctx.label.name + ".tar")
    configure_tar = ctx.file.configure_tar
    obj_tars = ctx.files.obj_tars
    inputs = depset([configure_tar] + obj_tars)

    cmd = """
set -euo pipefail
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:${{PATH}}"
CONFIGURE_TAR="$(realpath "{configure_tar_path}")"
OUT_TAR="$(realpath -m "{out_tar_path}")"
export EXEC_ROOT="${{PWD}}"
WORK_ROOT="/tmp/cmake-work"
exec 9>/tmp/cmake-work.lock
flock 9
rm -rf "${{WORK_ROOT}}"
mkdir -p "${{WORK_ROOT}}"

tar -xf "${{CONFIGURE_TAR}}" -C "${{WORK_ROOT}}"

export CC=/usr/local/bin/gcc
export CXX=/usr/local/bin/g++
export CFLAGS="-Werror"
export CXXFLAGS="-Werror"
export CMAKE_GENERATOR=Ninja
export CMAKE_BUILD_PARALLEL_LEVEL="$(nproc)"

python3 -c '
import os, re, subprocess

exec_root = os.environ["EXEC_ROOT"]
work_root = "/tmp/cmake-work"
build_dir = os.path.join(work_root, "build")
nlog_path = os.path.join(build_dir, ".ninja_log")
part_path = os.path.join(build_dir, ".ninja_log.part")
obj_tars = {obj_tars_repr}

for rel_tar in obj_tars:
    full_tar = os.path.join(exec_root, rel_tar)
    subprocess.check_call(["tar", "-xf", full_tar, "-C", build_dir])
    if os.path.exists(part_path):
        with open(part_path) as pf:
            pdata = pf.read()
        if pdata:
            with open(nlog_path, "a") as nf:
                nf.write(pdata)
        os.remove(part_path)

for root, dirs, files in os.walk(build_dir):
    for f in files:
        if f.endswith(".o"):
            os.utime(os.path.join(root, f), (1577836800, 1577836800))

bninja_file = os.path.join(build_dir, "build.ninja")
if os.path.exists(bninja_file):
    with open(bninja_file) as bf:
        btext = bf.read()
    btext = re.sub(
        r"^build lib/lib\\S+:\\s+(?:CXX_STATIC|CXX_SHARED)_LIBRARY_LINKER__\\S+\\s+[^\\n]+(?:\\n\\s+[^\\n]+)*\\n",
        "",
        btext,
        flags=re.M,
    )
    with open(bninja_file, "w") as bf:
        bf.write(btext)

out = subprocess.check_output(["ninja", "-C", build_dir, "-t", "targets", "all"], text=True)
lib_targets = [
    line.split(":", 1)[0]
    for line in out.splitlines()
    if "_STATIC_LIBRARY_LINKER__" in line or "_SHARED_LIBRARY_LINKER__" in line
]
if lib_targets:
    subprocess.check_call(["ninja", "-C", build_dir] + lib_targets)

if os.path.exists(nlog_path):
    lines = []
    with open(nlog_path) as nf:
        for line in nf:
            if line.startswith("#"):
                lines.append(line)
                continue
            parts = line.rstrip("\\n").split("\\t")
            if len(parts) >= 5:
                parts[2] = "1577836800000000000"
                lines.append("\\t".join(parts) + "\\n")
    with open(nlog_path, "w") as nf:
        nf.writelines(lines)

for root, dirs, files in os.walk(build_dir):
    for f in files:
        os.utime(os.path.join(root, f), (1577836800, 1577836800))
'

tar -cf "${{OUT_TAR}}" -C "${{WORK_ROOT}}" src build
rm -rf "${{WORK_ROOT}}"
""".format(
        configure_tar_path = configure_tar.path,
        out_tar_path = out_tar.path,
        obj_tars_repr = repr([f.path for f in obj_tars]),
    )

    ctx.actions.run_shell(
        inputs = inputs,
        outputs = [out_tar],
        command = cmd,
        mnemonic = "CMakeLibsLink",
        progress_message = "Linking CMake libraries for %s" % ctx.label.name,
    )
    return [DefaultInfo(files = depset([out_tar]))]

_cmake_libs_link = rule(
    implementation = _cmake_libs_link_impl,
    attrs = {
        "configure_tar": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "obj_tars": attr.label_list(
            allow_files = True,
            mandatory = True,
        ),
    },
)

def _cmake_test_action_impl(ctx):
    out_log = ctx.actions.declare_file(ctx.label.name + ".log")
    libs_file = ctx.file.libs
    dep_hdrs = [
        d[_CmakeHeadersInfo].headers
        for d in ctx.attr.deps
        if _CmakeHeadersInfo in d
    ]
    hdr_files = depset(ctx.files.pkg_hdrs, transitive = dep_hdrs)
    src_files = ctx.files.srcs
    data_files = ctx.files.data
    inputs = depset([libs_file] + src_files + data_files, transitive = [hdr_files])

    hdr_entries = [(f.path, f.short_path) for f in hdr_files.to_list()]
    src_entries = [(f.path, f.short_path) for f in src_files]

    cmd = """
set -euo pipefail
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:${{PATH}}"
export EXEC_ROOT="${{PWD}}"
LIBS_TAR="$(realpath "{libs_path}")"
OUT_LOG="$(realpath -m "{out_log_path}")"
WORK_ROOT="/tmp/cmake-work"
exec 9>/tmp/cmake-work.lock
flock 9
rm -rf "${{WORK_ROOT}}"
mkdir -p "${{WORK_ROOT}}"

tar -xf "${{LIBS_TAR}}" -C "${{WORK_ROOT}}"

export CC=/usr/local/bin/gcc
export CXX=/usr/local/bin/g++
export CFLAGS="-Werror"
export CXXFLAGS="-Werror"
export CMAKE_GENERATOR=Ninja
export CMAKE_BUILD_PARALLEL_LEVEL="$(nproc)"
export CTEST_PARALLEL_LEVEL="$(nproc)"
if [ -d "${{EXEC_ROOT}}/absl/time/internal/cctz/testdata/zoneinfo" ]; then
  export TZDIR="${{EXEC_ROOT}}/absl/time/internal/cctz/testdata/zoneinfo"
else
  export TZDIR="${{WORK_ROOT}}/src/absl/time/internal/cctz/testdata/zoneinfo"
fi

TARGETS="$(python3 -c '
import os, re, shutil, subprocess

work_root = "/tmp/cmake-work"
exec_root = os.environ["EXEC_ROOT"]
src_root = os.path.join(work_root, "src")
build_dir = os.path.join(work_root, "build")

hdr_entries = {hdr_entries_repr}
src_entries = {src_entries_repr}

for src_p, rel_p in hdr_entries:
    full_src = os.path.join(exec_root, src_p)
    full_dst = os.path.join(src_root, rel_p)
    if os.path.exists(full_src):
        os.makedirs(os.path.dirname(full_dst), exist_ok=True)
        shutil.copyfile(full_src, full_dst)
        os.utime(full_dst, (946684800, 946684800))

all_targets_out = subprocess.check_output(
    ["ninja", "-C", build_dir, "-t", "targets", "all"], text=True
)
exe_link_targets = set()
for line in all_targets_out.splitlines():
    if "_EXECUTABLE_LINKER__" in line:
        m = re.search(r"_EXECUTABLE_LINKER__(.+?)_(?:Release|Debug|RelWithDebInfo|MinSizeRel)\\b", line)
        if m:
            exe_link_targets.add(m.group(1))
        exe_link_targets.add(os.path.basename(line.split(":", 1)[0]))

cmake_targets = set()
for src_p, rel_src in src_entries:
    full_src = os.path.join(exec_root, src_p)
    dst_path = os.path.join(src_root, rel_src)
    if os.path.exists(full_src):
        os.makedirs(os.path.dirname(dst_path), exist_ok=True)
        shutil.copyfile(full_src, dst_path)
        os.utime(dst_path, (946684800, 946684800))
    if rel_src.endswith(".cc") or rel_src.endswith(".c"):
        p = subprocess.run(
            ["ninja", "-C", build_dir, "-t", "query", dst_path],
            capture_output=True, text=True
        )
        if p.returncode == 0:
            for m in re.finditer(r"CMakeFiles/([^/]+)\\.dir/", p.stdout):
                tname = m.group(1)
                if tname in exe_link_targets:
                    cmake_targets.add(tname)

print(" ".join(sorted(cmake_targets)))
')"

if [ -z "${{TARGETS}}" ]; then
  echo "SKIPPED {test_label} (not part of any CMake test executable)" > "${{OUT_LOG}}"
  rm -rf "${{WORK_ROOT}}"
  exit 0
fi

{{
  echo "=== {test_label} (${{TARGETS}}) ==="
  cmake --build "${{WORK_ROOT}}/build" --target ${{TARGETS}}
  CTEST_REGEX="^($(echo "${{TARGETS}}" | tr " " "|"))(_shard_[0-9]+)?$"
  ctest --test-dir "${{WORK_ROOT}}/build" -R "${{CTEST_REGEX}}" --output-on-failure --repeat until-pass:3
}} > "${{OUT_LOG}}" 2>&1 || {{
  cat "${{OUT_LOG}}" >&2
  rm -rf "${{WORK_ROOT}}"
  exit 1
}}

rm -rf "${{WORK_ROOT}}"
""".format(
        libs_path = libs_file.path,
        out_log_path = out_log.path,
        hdr_entries_repr = repr(hdr_entries),
        src_entries_repr = repr(src_entries),
        test_label = str(ctx.label),
    )

    ctx.actions.run_shell(
        inputs = inputs,
        outputs = [out_log],
        command = cmd,
        mnemonic = "CMakeTest",
        progress_message = "Building and running CMake test %s" % ctx.label,
    )
    return [DefaultInfo(files = depset([out_log]))]

_cmake_test_action = rule(
    implementation = _cmake_test_action_impl,
    attrs = {
        "data": attr.label_list(allow_files = True),
        "deps": attr.label_list(
            aspects = [_cmake_headers_aspect],
        ),
        "libs": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "pkg_hdrs": attr.label_list(allow_files = True),
        "srcs": attr.label_list(
            allow_files = True,
            mandatory = True,
        ),
    },
)

def cmake_package(modes = [m[0] for m in DEFAULT_CMAKE_MODES]):
    """Inspects existing rules in the package to create per-library object actions and per-test actions."""
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

    native.filegroup(
        name = "cmake_configure_srcs",
        srcs = native.glob(["**/CMakeLists.txt", "**/*.cmake"], allow_empty = True),
        visibility = ["//:__pkg__"],
    )

    native.filegroup(
        name = "cmake_pkg_hdrs",
        srcs = native.glob(["**/*.h", "**/*.inc", "**/*.hpp", "**/*.def"], allow_empty = True),
        visibility = ["//visibility:private"],
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
                for mode in modes:
                    obj_name = "cmake_objs_" + mode + "_" + r["name"]
                    _cmake_lib_objs(
                        name = obj_name,
                        testonly = True,
                        configure_tar = "//:cmake_configure_" + mode,
                        srcs = list(srcs) if type(srcs) == "tuple" else srcs,
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
            srcs = objs_by_mode[mode],
            visibility = ["//:__pkg__"],
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
                    used_test_names[r["name"]] = True
                    test_groups.append(struct(
                        name = r["name"],
                        srcs = list(srcs),
                        deps = list(deps) if type(deps) == "tuple" else deps,
                        data = list(data) if type(data) == "tuple" else data,
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
        ),
        visibility = ["//:__pkg__"],
    )

    for g in test_groups:
        for mode in modes:
            target_name = "cmake_" + mode + "_" + g.name
            _cmake_test_action(
                name = target_name,
                testonly = True,
                libs = "//:cmake_libs_" + mode,
                srcs = g.srcs,
                pkg_hdrs = [":cmake_pkg_hdrs"],
                deps = g.deps,
                data = g.data,
                tags = ["cmake", "manual"],
            )
            tests_by_mode[mode].append(":" + target_name)

    for mode in modes:
        native.filegroup(
            name = "cmake_tests_" + mode,
            testonly = True,
            srcs = tests_by_mode[mode],
            visibility = ["//:__pkg__"],
        )

def define_cmake_tests(
        packages,
        root_srcs,
        top_level_entries = ["CMakeLists.txt", "CMake", "MODULE.bazel", "absl", "create_lts.py"],
        modes = DEFAULT_CMAKE_MODES):
    """Defines Stage 0 CMake configure, Stage 1 library link, and bundled top-level CMake test targets."""
    native.config_setting(
        name = "cmake_opt_mode",
        values = {"compilation_mode": "opt"},
    )

    native.filegroup(
        name = "cmake_configure_srcs",
        srcs = root_srcs + [pkg + ":cmake_configure_srcs" for pkg in packages],
    )

    native.filegroup(
        name = "cmake_lib_srcs",
        srcs = root_srcs + [pkg + ":cmake_lib_srcs" for pkg in packages],
    )

    top_level_tests = []
    for mode, shared, monolithic in modes:
        _cmake_configure(
            name = "cmake_configure_" + mode,
            testonly = True,
            build_type = select({
                ":cmake_opt_mode": "Release",
                "//conditions:default": "Debug",
            }),
            googletest_archive = "@googletest_archive//" + "file",
            monolithic = monolithic,
            shared = shared,
            srcs = [":cmake_configure_srcs"],
            top_level_entries = top_level_entries,
            tags = [
                "cmake",
                "manual",
            ],
        )

        _cmake_libs_link(
            name = "cmake_libs_" + mode,
            testonly = True,
            configure_tar = ":cmake_configure_" + mode,
            obj_tars = [pkg + ":cmake_objs_" + mode for pkg in packages],
            tags = [
                "cmake",
                "manual",
            ],
        )

        test_name = "cmake_" + mode + "_test"
        top_level_tests.append(":" + test_name)
        _cmake_sh_test(
            name = test_name,
            size = "small",
            content = """#!/bin/bash
set -euo pipefail
LOGS=(__CMAKE_LOG_FILES__)
echo "Verified ${{#LOGS[@]}} CMake ({mode}) test action logs."
""".format(mode = mode),
            data = [pkg + ":cmake_tests_" + mode for pkg in packages],
            tags = [
                "cmake",
                "manual",
            ],
        )

    for mode, link_type in [("static", "STATIC"), ("shared", "DYNAMIC")]:
        install_name = "cmake_install_" + mode + "_test"
        top_level_tests.append(":" + install_name)
        _cmake_sh_test(
            name = install_name,
            size = "large",
            args = ["$(rootpath @googletest_archive//" + "file)"],
            content = """#!/bin/bash
set -euo pipefail
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:${{PATH}}"
export LINK_TYPE="{link_type}"
export ABSL_INSTALL_TEST_BUILD_TESTING="OFF"
export ABSL_GOOGLETEST_VERSION="1.18.0"
export ABSL_GOOGLETEST_DOWNLOAD_URL="file://$(realpath "$1")"
export CC=/usr/local/bin/gcc
export CXX=/usr/local/bin/g++
export CMAKE_GENERATOR=Ninja
export CMAKE_BUILD_PARALLEL_LEVEL="$(nproc)"
export CTEST_PARALLEL_LEVEL="$(nproc)"
WORKDIR="$(mktemp -d "${{TEST_TMPDIR}}/absl_install_test.XXXXXX")"
cp -RL {top_level_entries} "${{WORKDIR}}/"
chmod -R u+w "${{WORKDIR}}"
export TZDIR="${{WORKDIR}}/absl/time/internal/cctz/testdata/zoneinfo"
export HOME="${{TEST_TMPDIR}}"
rm -rf /buildfs /googletest_builddir
cd "${{WORKDIR}}"
exec ./CMake/install_test_project/test.sh
""".format(
                link_type = link_type,
                top_level_entries = " ".join(top_level_entries),
            ),
            data = [
                ":cmake_lib_srcs",
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

    native.test_suite(
        name = "cmake_tests",
        tags = [
            "cmake",
            "manual",
        ],
        tests = top_level_tests,
    )
