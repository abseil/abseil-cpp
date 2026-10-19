#!/usr/bin/env python3
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

"""Hermetic multi-stage CMake build and test runner for Bazel RBE actions."""

import argparse
import collections
from concurrent import futures
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

# Fixed timestamps (UTC) used to keep CMake/Ninja build trees deterministic
# across isolated Bazel RBE actions:
# - STUB_MTIME (2000-01-01): Applied to stub sources during configure and
#   test/header sources during test compilation.
# - BUILD_MTIME (2020-01-01): Applied to configured build files, compiled
#   object files, and linked libraries.
STUB_MTIME = 946684800
BUILD_MTIME = 1577836800
WORK_ROOT_TOKEN = b"@CMAKE_WORK_ROOT@"

_GTEST_CONFIG_TEMPLATE = """\
include(CMakeFindDependencyMacro)
find_dependency(Threads)

get_filename_component(_GTEST_PREFIX "${{CMAKE_CURRENT_LIST_DIR}}/../../.." ABSOLUTE)
set(GTEST_FOUND TRUE)
set(GTest_FOUND TRUE)
set(GTEST_INCLUDE_DIRS "${{_GTEST_PREFIX}}/include")

if(NOT TARGET GTest::gtest)
  add_library(GTest::gtest {lib_kind} IMPORTED GLOBAL)
  set_target_properties(GTest::gtest PROPERTIES
    IMPORTED_LOCATION "${{_GTEST_PREFIX}}/lib/libgtest.{lib_ext}"
    INTERFACE_INCLUDE_DIRECTORIES "${{_GTEST_PREFIX}}/include"
    INTERFACE_LINK_LIBRARIES "Threads::Threads"
  )
endif()

if(NOT TARGET GTest::gmock)
  add_library(GTest::gmock {lib_kind} IMPORTED GLOBAL)
  set_target_properties(GTest::gmock PROPERTIES
    IMPORTED_LOCATION "${{_GTEST_PREFIX}}/lib/libgmock.{lib_ext}"
    INTERFACE_INCLUDE_DIRECTORIES "${{_GTEST_PREFIX}}/include"
    INTERFACE_LINK_LIBRARIES "GTest::gtest;Threads::Threads"
  )
endif()

if(NOT TARGET GTest::gtest_main)
  add_library(GTest::gtest_main {lib_kind} IMPORTED GLOBAL)
  set_target_properties(GTest::gtest_main PROPERTIES
    IMPORTED_LOCATION "${{_GTEST_PREFIX}}/lib/libgtest_main.{lib_ext}"
    INTERFACE_INCLUDE_DIRECTORIES "${{_GTEST_PREFIX}}/include"
    INTERFACE_LINK_LIBRARIES "GTest::gmock;GTest::gtest;Threads::Threads"
  )
endif()

if(NOT TARGET GTest::gmock_main)
  add_library(GTest::gmock_main {lib_kind} IMPORTED GLOBAL)
  set_target_properties(GTest::gmock_main PROPERTIES
    IMPORTED_LOCATION "${{_GTEST_PREFIX}}/lib/libgmock_main.{lib_ext}"
    INTERFACE_INCLUDE_DIRECTORIES "${{_GTEST_PREFIX}}/include"
    INTERFACE_LINK_LIBRARIES "GTest::gmock;GTest::gtest;Threads::Threads"
  )
endif()

foreach(_t gtest gmock gtest_main gmock_main)
  if(NOT TARGET ${{_t}})
    add_library(${{_t}} ALIAS GTest::${{_t}})
  endif()
endforeach()
"""


def _resolve_tool(path):
  """Returns the absolute path to `path` if it exists locally."""
  if path and os.path.exists(path):
    return os.path.abspath(path)
  return path


def _resolve_cxx(cc, cxx):
  """Resolves the C++ compiler executable from `cc` and `cxx`."""
  cxx = _resolve_tool(cxx)
  if cxx.endswith("/gcc") and os.path.exists(cxx[:-3] + "g++"):
    return cxx[:-3] + "g++"
  if cxx.endswith("/clang") and os.path.exists(cxx + "++"):
    return cxx + "++"
  if not cxx and cc:
    return _resolve_cxx(cc, cc)
  return cxx


def _make_env(args, work_root=None, extra_env=None):
  """Constructs the environment dictionary for CMake, Ninja, and CTest."""
  env = dict(os.environ)
  cc = _resolve_tool(args.cc)
  cxx = _resolve_cxx(cc, args.cxx)
  path_dirs = []
  for tool in (cc, cxx):
    if tool and os.path.dirname(tool):
      path_dirs.append(os.path.dirname(tool))
  path_dirs.extend([
      "/usr/local/sbin",
      "/usr/local/bin",
      "/usr/sbin",
      "/usr/bin",
      "/sbin",
      "/bin",
  ])
  if env.get("PATH"):
    path_dirs.append(env["PATH"])
  env["PATH"] = ":".join(path_dirs)
  env["CC"] = cc
  env["CXX"] = cxx
  base_flags = ["-Werror", "-ffile-prefix-map=../src=."]
  cflags = list(args.cflag) + base_flags
  cxxflags = list(args.cxxflag) + base_flags
  ldflags = list(args.ldflag)
  env["CFLAGS"] = " ".join(cflags)
  env["CXXFLAGS"] = " ".join(cxxflags)
  if ldflags:
    env["LDFLAGS"] = " ".join(ldflags)
  env["CMAKE_GENERATOR"] = "Ninja"
  jobs = str(os.cpu_count() or 2)
  env["CMAKE_BUILD_PARALLEL_LEVEL"] = jobs
  env["CTEST_PARALLEL_LEVEL"] = jobs
  symbolizer = (
      os.path.join(os.path.dirname(cc), "llvm-symbolizer") if cc else ""
  )
  if symbolizer and os.path.exists(symbolizer):
    for var in (
        "ASAN_SYMBOLIZER_PATH",
        "UBSAN_SYMBOLIZER_PATH",
        "MSAN_SYMBOLIZER_PATH",
        "TSAN_SYMBOLIZER_PATH",
    ):
      env.setdefault(var, symbolizer)
  env.setdefault("UBSAN_OPTIONS", "print_stacktrace=1")
  env.setdefault("GTEST_INSTALL_FAILURE_SIGNAL_HANDLER", "1")
  ld_paths = []
  if work_root:
    ld_paths.extend([
        os.path.join(work_root, "deps", "lib"),
        os.path.join(work_root, "build", "lib"),
        os.path.join(work_root, "build", "bin"),
    ])
    ninja_bin = shutil.which("ninja", path=env.get("PATH"))
    if ninja_bin:
      p = subprocess.run(
          [ninja_bin, "--version"], env=env, capture_output=True, check=False
      )
      if p.returncode != 0:
        for ld_so, sys_lib in (
            (
                "/lib64/ld-linux-x86-64.so.2",
                "/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu",
            ),
            (
                "/lib/ld-linux-aarch64.so.1",
                "/usr/lib/aarch64-linux-gnu:/lib/aarch64-linux-gnu",
            ),
        ):
          if os.path.exists(ld_so):
            bin_dir = os.path.join(work_root, "_bin")
            os.makedirs(bin_dir, exist_ok=True)
            wrapper = os.path.join(bin_dir, "ninja")
            with open(wrapper, "w") as wf:
              wf.write(
                  f"#!/bin/sh\nexec {ld_so} --library-path {sys_lib}"
                  f' {ninja_bin} "$@"\n'
              )
            os.chmod(wrapper, 0o755)
            env["PATH"] = bin_dir + ":" + env["PATH"]
            break
  if env.get("LD_LIBRARY_PATH"):
    ld_paths.append(env["LD_LIBRARY_PATH"])
  if ld_paths:
    env["LD_LIBRARY_PATH"] = ":".join(ld_paths)
  if extra_env:
    env.update(extra_env)
  return env


def _copy_mapped_files(exec_root, dst_root, mappings, mtime=None):
  """Copies `<src>=<dst>` file mappings from `exec_root` into `dst_root`."""
  for item in mappings:
    src_rel, dst_rel = item.split("=", 1)
    full_src = os.path.join(exec_root, src_rel)
    full_dst = os.path.join(dst_root, dst_rel)
    if os.path.exists(full_src):
      os.makedirs(os.path.dirname(full_dst), exist_ok=True)
      shutil.copyfile(full_src, full_dst)
      if mtime is not None:
        os.utime(full_dst, (mtime, mtime))
      else:
        os.utime(full_dst, None)


def _replace_in_tree(root_dir, old_bytes, new_bytes, mtime=BUILD_MTIME):
  """Replaces `old_bytes` with `new_bytes` in non-binary files under `root_dir`."""
  if not os.path.exists(root_dir):
    return
  for root, _, files in os.walk(root_dir):
    for f in files:
      if f in ("build.ninja", "rules.ninja") or f.endswith((".o", ".a", ".so")):
        continue
      fpath = os.path.join(root, f)
      if os.path.islink(fpath):
        continue
      try:
        with open(fpath, "rb") as rf:
          data = rf.read()
      except OSError:
        continue
      if b"\x00" in data[:4096] or old_bytes not in data:
        continue
      with open(fpath, "wb") as wf:
        wf.write(data.replace(old_bytes, new_bytes))
      if mtime is not None:
        os.utime(fpath, (mtime, mtime))


def _set_tree_mtime(root_dir, mtime=BUILD_MTIME):
  """Sets the modification time of all regular files under `root_dir`."""
  if not os.path.exists(root_dir):
    return
  for root, _, files in os.walk(root_dir):
    for f in files:
      fpath = os.path.join(root, f)
      if not os.path.islink(fpath):
        os.utime(fpath, (mtime, mtime))


def _tar_create(out_tar, base_dir, items, mtime=BUILD_MTIME):
  """Creates a deterministic tar archive with normalized metadata and ordering."""
  subprocess.check_call([
      "tar",
      "--sort=name",
      f"--mtime=@{mtime}",
      "--owner=0",
      "--group=0",
      "--numeric-owner",
      "-cf",
      out_tar,
      "-C",
      base_dir,
      *items,
  ])


def _cmd_build_gtest(args):
  """Compiles and packages GoogleTest and GoogleMock with a GTestConfig.cmake."""
  exec_root = os.getcwd()
  out_tar = os.path.abspath(args.out_tar)
  cc = _resolve_tool(args.cc)
  cxx = _resolve_cxx(cc, args.cxx)
  ar = _resolve_tool(args.ar) or "ar"
  opt_flag = "-O2" if args.build_type == "Release" else "-g"

  with tempfile.TemporaryDirectory(prefix="cmake_gtest_") as work_root:
    src_root = os.path.join(work_root, "src")
    obj_root = os.path.join(work_root, "objs")
    prefix_root = os.path.join(work_root, "prefix")
    inc_dir = os.path.join(prefix_root, "include")
    lib_dir = os.path.join(prefix_root, "lib")
    cmake_dir = os.path.join(lib_dir, "cmake", "GTest")
    os.makedirs(obj_root, exist_ok=True)
    os.makedirs(inc_dir, exist_ok=True)
    os.makedirs(cmake_dir, exist_ok=True)

    _copy_mapped_files(exec_root, src_root, args.src_map, mtime=STUB_MTIME)

    for sub in ("googletest", "googlemock"):
      src_inc = os.path.join(src_root, sub, "include")
      if os.path.isdir(src_inc):
        shutil.copytree(src_inc, inc_dir, dirs_exist_ok=True)

    gtest_src_dir = os.path.join(src_root, "googletest", "src")
    gmock_src_dir = os.path.join(src_root, "googlemock", "src")
    gtest_ccs = sorted([
        os.path.join(gtest_src_dir, f)
        for f in os.listdir(gtest_src_dir)
        if f.endswith(".cc") and f not in ("gtest-all.cc", "gtest_main.cc")
    ])
    gmock_ccs = sorted([
        os.path.join(gmock_src_dir, f)
        for f in os.listdir(gmock_src_dir)
        if f.endswith(".cc") and f not in ("gmock-all.cc", "gmock_main.cc")
    ])
    main_cc = os.path.join(gmock_src_dir, "gmock_main.cc")
    if not os.path.exists(main_cc):
      main_cc = os.path.join(gtest_src_dir, "gtest_main.cc")

    include_flags = [
        "-I",
        os.path.join(src_root, "googletest", "include"),
        "-I",
        os.path.join(src_root, "googletest"),
        "-I",
        os.path.join(src_root, "googlemock", "include"),
        "-I",
        os.path.join(src_root, "googlemock"),
    ]

    env = _make_env(args)

    def compile_one(cc_file):
      obj_file = os.path.join(obj_root, os.path.basename(cc_file) + ".o")
      cmd = [
          cxx,
          *args.cxxflag,
          "-w",
          "-fPIC",
          opt_flag,
          "-pthread",
          f"-ffile-prefix-map={work_root}=.",
          *include_flags,
          "-c",
          cc_file,
          "-o",
          obj_file,
      ]
      subprocess.check_call(cmd, env=env)
      return obj_file

    all_ccs = gtest_ccs + gmock_ccs + [main_cc]
    with futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
      compiled = dict(zip(all_ccs, pool.map(compile_one, all_ccs)))

    gtest_objs = [compiled[c] for c in gtest_ccs]
    gmock_objs = [compiled[c] for c in gmock_ccs]
    main_obj = compiled[main_cc]

    if args.shared == "ON":
      lib_kind = "SHARED"
      lib_ext = "so"
      gtest_so = os.path.join(lib_dir, "libgtest.so")
      gmock_so = os.path.join(lib_dir, "libgmock.so")
      gtest_main_so = os.path.join(lib_dir, "libgtest_main.so")
      gmock_main_so = os.path.join(lib_dir, "libgmock_main.so")
      subprocess.check_call(
          [
              cxx,
              "-shared",
              "-Wl,-soname,libgtest.so",
              "-Wl,-rpath,$ORIGIN",
              "-pthread",
              *gtest_objs,
              *args.ldflag,
              "-o",
              gtest_so,
          ],
          env=env,
      )
      subprocess.check_call(
          [
              cxx,
              "-shared",
              "-Wl,-soname,libgmock.so",
              "-Wl,-rpath,$ORIGIN",
              "-pthread",
              *gmock_objs,
              gtest_so,
              *args.ldflag,
              "-o",
              gmock_so,
          ],
          env=env,
      )
      subprocess.check_call(
          [
              cxx,
              "-shared",
              "-Wl,-soname,libgtest_main.so",
              "-Wl,-rpath,$ORIGIN",
              "-pthread",
              main_obj,
              gmock_so,
              gtest_so,
              *args.ldflag,
              "-o",
              gtest_main_so,
          ],
          env=env,
      )
      subprocess.check_call(
          [
              cxx,
              "-shared",
              "-Wl,-soname,libgmock_main.so",
              "-Wl,-rpath,$ORIGIN",
              "-pthread",
              main_obj,
              gmock_so,
              gtest_so,
              *args.ldflag,
              "-o",
              gmock_main_so,
          ],
          env=env,
      )
    else:
      lib_kind = "STATIC"
      lib_ext = "a"
      subprocess.check_call(
          [ar, "rcs", os.path.join(lib_dir, "libgtest.a"), *gtest_objs],
          env=env,
      )
      subprocess.check_call(
          [ar, "rcs", os.path.join(lib_dir, "libgmock.a"), *gmock_objs],
          env=env,
      )
      subprocess.check_call(
          [ar, "rcs", os.path.join(lib_dir, "libgtest_main.a"), main_obj],
          env=env,
      )
      subprocess.check_call(
          [ar, "rcs", os.path.join(lib_dir, "libgmock_main.a"), main_obj],
          env=env,
      )

    with open(os.path.join(cmake_dir, "GTestConfig.cmake"), "w") as cf:
      cf.write(
          _GTEST_CONFIG_TEMPLATE.format(lib_kind=lib_kind, lib_ext=lib_ext)
      )

    _set_tree_mtime(prefix_root, BUILD_MTIME)
    _tar_create(out_tar, prefix_root, ["include", "lib"])


def _cmd_configure(args):
  """Runs CMake configure and emits object, library, and test build specs."""
  exec_root = os.getcwd()
  objs_spec_out = os.path.abspath(args.objs_spec_out)
  libs_spec_out = os.path.abspath(args.libs_spec_out)
  tests_spec_out = os.path.abspath(args.tests_spec_out)
  install_build_out = os.path.abspath(args.install_build_out)

  with tempfile.TemporaryDirectory(prefix="cmake_cfg_") as work_root:
    src_root = os.path.join(work_root, "src")
    build_dir = os.path.join(work_root, "build")
    deps_dir = os.path.join(work_root, "deps")
    install_dir = os.path.join(work_root, "install")
    os.makedirs(src_root, exist_ok=True)
    os.makedirs(build_dir, exist_ok=True)
    os.makedirs(deps_dir, exist_ok=True)

    for dep_tar in args.dep_tar:
      subprocess.check_call(
          ["tar", "-xf", os.path.join(exec_root, dep_tar), "-C", deps_dir]
      )
    _replace_in_tree(deps_dir, WORK_ROOT_TOKEN, work_root.encode(), BUILD_MTIME)

    _copy_mapped_files(exec_root, src_root, args.src_map, mtime=STUB_MTIME)

    # Create 0-byte stubs for source and header files referenced in
    # CMakeLists.txt and *.cmake so CMake generates the full Ninja/CTest graph
    # without depending on any project .cc or .h file contents.
    absl_root = os.path.join(src_root, "absl")
    for root, _, files in os.walk(src_root):
      for f in files:
        fpath = os.path.join(root, f)
        os.utime(fpath, (STUB_MTIME, STUB_MTIME))
        if f == "CMakeLists.txt" or f.endswith(".cmake"):
          with open(fpath, errors="replace") as cf:
            text = cf.read()
          base_dir = (
              absl_root
              if (f == "AbseilDll.cmake" and os.path.isdir(absl_root))
              else root
          )
          for token in re.findall(
              r'["\s(]([A-Za-z0-9_./-]+\.(?:cc|c|h|inc|hpp))["\s)]', text
          ):
            candidate = os.path.join(base_dir, token)
            if not os.path.exists(candidate):
              os.makedirs(os.path.dirname(candidate), exist_ok=True)
              open(candidate, "w").close()
              os.utime(candidate, (STUB_MTIME, STUB_MTIME))

    env = _make_env(args, work_root=work_root)
    jobs = env["CMAKE_BUILD_PARALLEL_LEVEL"]
    cmake_cmd = [
        "cmake",
        "-Wno-unused-cli",
        "-S",
        src_root,
        "-B",
        build_dir,
        "-GNinja",
        "-DCMAKE_SUPPRESS_REGENERATION=ON",
        "-DCMAKE_BUILD_RPATH_USE_ORIGIN=ON",
        "-DCMAKE_INSTALL_LIBDIR=lib",
        f"-DCMAKE_INSTALL_PREFIX={install_dir}",
        f"-DCMAKE_PREFIX_PATH={deps_dir}",
        f"-DCMAKE_BUILD_TYPE={args.build_type}",
        "-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG",
        "-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG",
        "-DCMAKE_EXE_LINKER_FLAGS_DEBUG=",
        "-DCMAKE_SHARED_LINKER_FLAGS_DEBUG=",
        f"-DCMAKE_CXX_STANDARD={args.cxx_std}",
        "-DCMAKE_CXX_STANDARD_REQUIRED=ON",
        f"-DBUILD_SHARED_LIBS={args.shared}",
        "-DBUILD_TESTING=ON",
        f"-DCMAKE_JOB_POOLS=link={jobs}",
        "-DCMAKE_JOB_POOL_LINK=link",
        *args.cmake_arg,
    ]
    subprocess.check_call(cmake_cmd, env=env)

    def _relativize_cmd(text):
      text = re.sub(r"\s+-MD\s+-MT\s+\S+\s+-MF\s+\S+", "", text)
      text = re.sub(r"\s*(?:-Xlinker\s+|-Wl,)--dependency-file=\S+", "", text)
      text = text.replace(work_root + "/src", "../src")
      text = text.replace(work_root + "/deps", "../deps")
      text = text.replace(work_root, WORK_ROOT_TOKEN.decode())
      return text

    targets_out = subprocess.check_output(
        ["ninja", "-C", build_dir, "-t", "targets", "all"],
        text=True,
        env=env,
    )
    lib_cmake_names = set()
    lib_outs = set()
    exe_cmake_to_out = {}
    for line in targets_out.splitlines():
      if ": " not in line:
        continue
      out, rule = [x.strip() for x in line.split(": ", 1)]
      m_lib = re.search(
          r"_(?:STATIC|SHARED)_LIBRARY_LINKER__(.+?)_(?:Release|Debug|RelWithDebInfo|MinSizeRel)\b",
          rule,
      )
      if m_lib:
        lib_cmake_names.add(m_lib.group(1))
        lib_outs.add(out)
      elif "CMAKE_SYMLINK_LIBRARY" in rule:
        lib_outs.add(out)
      m_exe = re.search(
          r"_EXECUTABLE_LINKER__(.+?)_(?:Release|Debug|RelWithDebInfo|MinSizeRel)\b",
          rule,
      )
      if m_exe:
        exe_cmake_to_out[m_exe.group(1)] = out

    compdb = json.loads(
        subprocess.check_output(
            ["ninja", "-C", build_dir, "-t", "compdb"], text=True, env=env
        )
    )
    cmd_by_out = {}
    cmd_to_lib_out = {}
    lib_objs_by_src = collections.defaultdict(set)
    exe_objs = collections.defaultdict(list)

    for entry in compdb:
      out = entry["output"]
      raw_cmd = entry["command"]
      if not raw_cmd:
        continue
      cmd = _relativize_cmd(raw_cmd)
      cmd_by_out[out] = cmd
      if out in lib_outs:
        cmd_to_lib_out[cmd] = out
      if out.endswith(".o"):
        in_file = _relativize_cmd(entry["file"])
        if in_file.startswith("../src/"):
          rel_cc = in_file[len("../src/") :]
          m = re.search(r"CMakeFiles/([^/]+)\.dir/", out)
          if m:
            tname = m.group(1)
            if tname in lib_cmake_names:
              lib_objs_by_src[rel_cc].add((out, cmd))
            if tname in exe_cmake_to_out:
              exe_objs[tname].append((rel_cc, out, cmd))

    ordered_lib_cmds = []
    if lib_outs:
      raw_lib_cmds = subprocess.check_output(
          ["ninja", "-C", build_dir, "-t", "commands", *sorted(lib_outs)],
          text=True,
          env=env,
      ).splitlines()
      seen_cmds = set()
      for raw_cmd in raw_lib_cmds:
        rel_cmd = _relativize_cmd(raw_cmd)
        if rel_cmd in cmd_to_lib_out and rel_cmd not in seen_cmds:
          seen_cmds.add(rel_cmd)
          ordered_lib_cmds.append([cmd_to_lib_out[rel_cmd], rel_cmd])

    def _direct_libs(out):
      res = set()
      for tok in cmd_by_out.get(out, "").split():
        if tok in lib_outs:
          res.add(tok)
          if tok.endswith(".so"):
            for l in lib_outs:
              if l.startswith(tok + "."):
                res.add(l)
          elif ".so." in tok:
            base = tok.split(".so.")[0] + ".so"
            if base in lib_outs:
              res.add(base)
      return res

    trans_cache = {}

    def _trans_libs(out):
      if out in trans_cache:
        return trans_cache[out]
      res = set(_direct_libs(out))
      trans_cache[out] = res
      for dlib in list(res):
        res |= _trans_libs(dlib)
      return res

    ctest_by_id = collections.defaultdict(list)
    for root, _, files in os.walk(build_dir):
      if "CTestTestfile.cmake" in files:
        cpath = os.path.join(root, "CTestTestfile.cmake")
        with open(cpath) as cf:
          ctext = cf.read()
        ctext = re.sub(r'\s*_BACKTRACE_TRIPLES\s+"[^"]*"', "", ctext)
        ctext = ctext.replace(work_root, WORK_ROOT_TOKEN.decode())
        for line in ctext.splitlines():
          sline = line.strip()
          m = re.match(r"^(?:add_test|set_tests_properties)\((\S+)\b", sline)
          if m:
            ctest_by_id[m.group(1)].append(sline)

    exes_by_name = {}
    src_to_exes = collections.defaultdict(set)

    for tname, exe_out in sorted(exe_cmake_to_out.items()):
      exe_cmd = cmd_by_out.get(exe_out, "")
      req_libs = sorted(_trans_libs(exe_out))
      obj_cmds = [
          [o, c] for o, c in sorted(set((o, c) for _, o, c in exe_objs[tname]))
      ]
      ctest_lines = []
      for cid in sorted(ctest_by_id.keys()):
        if cid == tname or cid.startswith(tname + "_shard_"):
          ctest_lines.extend(ctest_by_id[cid])
      exes_by_name[tname] = {
          "cmake_name": tname,
          "ctest_lines": ctest_lines,
          "exe_cmd": exe_cmd,
          "exe_out": exe_out,
          "obj_cmds": obj_cmds,
          "req_libs": req_libs,
      }
      for rel_cc, _, _ in exe_objs[tname]:
        src_to_exes[rel_cc].add(tname)

    objs_json = {
        rel_cc: [[o, c] for o, c in sorted(pairs)]
        for rel_cc, pairs in sorted(lib_objs_by_src.items())
    }
    with open(objs_spec_out, "w") as of:
      json.dump(objs_json, of, indent=2, sort_keys=True)

    tests_json = {
        "exes": exes_by_name,
        "src_to_exes": {
            rel_cc: sorted(tnames)
            for rel_cc, tnames in sorted(src_to_exes.items())
        },
    }
    with open(tests_spec_out, "w") as tf:
      json.dump(tests_json, tf, indent=2, sort_keys=True)

    with open(libs_spec_out, "w") as lf:
      json.dump(
          {"ordered_lib_cmds": ordered_lib_cmds}, lf, indent=2, sort_keys=True
      )

    for nfile in (
        os.path.join(build_dir, "build.ninja"),
        os.path.join(build_dir, "CMakeFiles", "rules.ninja"),
        os.path.join(build_dir, ".ninja_deps"),
        os.path.join(build_dir, ".ninja_log"),
    ):
      if os.path.exists(nfile):
        os.remove(nfile)

    for root, _, files in os.walk(build_dir):
      for f in files:
        if f == "cmake_install.cmake":
          p = os.path.join(root, f)
          with open(p) as cf:
            ctext = cf.read()
          ctext = ctext.replace(f"$ORIGIN:{work_root}/deps/lib", "$ORIGIN")
          ctext = ctext.replace(work_root + "/deps", "../deps")
          with open(p, "w") as cf:
            cf.write(ctext)

    _replace_in_tree(
        build_dir, work_root.encode(), WORK_ROOT_TOKEN, BUILD_MTIME
    )
    _set_tree_mtime(build_dir, BUILD_MTIME)

    _tar_create(install_build_out, work_root, ["src", "build"])


def _cmd_slice_obj_spec(args):
  """Extracts the CMake object compile spec for a single cc_library."""
  with open(args.objs_spec) as pf:
    all_objs = json.load(pf)
  cmds = []
  seen = set()
  for rel_cc in sorted(args.cc_rel):
    for obj_out, cmd in all_objs.get(rel_cc, []):
      if obj_out not in seen:
        seen.add(obj_out)
        cmds.append([obj_out, cmd])
  with open(args.out, "w") as of:
    json.dump({"obj_cmds": cmds}, of, indent=2, sort_keys=True)


def _cmd_compile_objs(args):
  """Compiles CMake library object files for a single cc_library."""
  exec_root = os.getcwd()
  out_tar = os.path.abspath(args.out_tar)
  with open(args.obj_spec) as sf:
    obj_cmds = json.load(sf).get("obj_cmds", [])

  with tempfile.TemporaryDirectory(prefix="cmake_objs_") as work_root:
    src_root = os.path.join(work_root, "src")
    build_dir = os.path.join(work_root, "build")
    deps_dir = os.path.join(work_root, "deps")
    os.makedirs(src_root, exist_ok=True)
    os.makedirs(build_dir, exist_ok=True)

    if any("../deps" in cmd for _, cmd in obj_cmds):
      os.makedirs(deps_dir, exist_ok=True)
      for dep_tar in args.dep_tar:
        subprocess.check_call(
            ["tar", "-xf", os.path.join(exec_root, dep_tar), "-C", deps_dir]
        )
      _replace_in_tree(
          deps_dir, WORK_ROOT_TOKEN, work_root.encode(), BUILD_MTIME
      )

    _copy_mapped_files(exec_root, src_root, args.src_map, mtime=None)
    env = _make_env(args, work_root=work_root)

    def _compile_one(item):
      obj_out, cmd = item
      full_out = os.path.join(build_dir, obj_out)
      os.makedirs(os.path.dirname(full_out), exist_ok=True)
      subprocess.check_call(cmd, shell=True, cwd=build_dir, env=env)
      os.utime(full_out, (BUILD_MTIME, BUILD_MTIME))
      return obj_out

    if obj_cmds:
      with futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
        sorted_objs = sorted(pool.map(_compile_one, obj_cmds))
      _tar_create(out_tar, build_dir, sorted_objs)
    else:
      subprocess.check_call(["tar", "-cf", out_tar, "-T", "/dev/null"])


def _cmd_link_libs(args):
  """Links all CMake libraries from per-library object archives."""
  exec_root = os.getcwd()
  out_tar = os.path.abspath(args.out_tar)
  with open(args.libs_spec) as lf:
    ordered_lib_cmds = json.load(lf).get("ordered_lib_cmds", [])

  with tempfile.TemporaryDirectory(prefix="cmake_libs_") as work_root:
    build_dir = os.path.join(work_root, "build")
    deps_dir = os.path.join(work_root, "deps")
    os.makedirs(build_dir, exist_ok=True)
    os.makedirs(deps_dir, exist_ok=True)

    for dep_tar in args.dep_tar:
      subprocess.check_call(
          ["tar", "-xf", os.path.join(exec_root, dep_tar), "-C", deps_dir]
      )
    _replace_in_tree(deps_dir, WORK_ROOT_TOKEN, work_root.encode(), BUILD_MTIME)

    for rel_tar in args.obj_tar:
      full_tar = os.path.join(exec_root, rel_tar)
      subprocess.check_call(["tar", "-xf", full_tar, "-C", build_dir])

    env = _make_env(args, work_root=work_root)
    for lib_out, cmd in ordered_lib_cmds:
      full_out = os.path.join(build_dir, lib_out)
      os.makedirs(os.path.dirname(full_out), exist_ok=True)
      subprocess.check_call(cmd, shell=True, cwd=build_dir, env=env)
      if os.path.exists(full_out) and not os.path.islink(full_out):
        os.utime(full_out, (BUILD_MTIME, BUILD_MTIME))

    # Remove intermediate .o files so the library archive only contains linked
    # libraries and dependencies.
    for root, _, files in os.walk(build_dir):
      for f in files:
        if f.endswith(".o"):
          os.remove(os.path.join(root, f))

    _replace_in_tree(
        build_dir, work_root.encode(), WORK_ROOT_TOKEN, BUILD_MTIME
    )
    _replace_in_tree(deps_dir, work_root.encode(), WORK_ROOT_TOKEN, BUILD_MTIME)
    _set_tree_mtime(build_dir, BUILD_MTIME)
    _set_tree_mtime(deps_dir, BUILD_MTIME)

    _tar_create(out_tar, work_root, ["build", "deps"])


def _cmd_slice_test_spec(args):
  """Extracts the CMake test build spec and required libraries for a cc_test."""
  with open(args.tests_spec) as tf:
    tests_spec = json.load(tf)
  exes_by_name = tests_spec.get("exes", {})
  src_to_exes = tests_spec.get("src_to_exes", {})

  matched_names = set()
  for rel_cc in args.cc_rel:
    matched_names.update(src_to_exes.get(rel_cc, []))

  exes = []
  req_lib_set = set()
  for name in sorted(matched_names):
    exe = dict(exes_by_name[name])
    req_lib_set.update(exe.pop("req_libs", []))
    exes.append(exe)

  with open(args.spec_out, "w") as sf:
    json.dump({"exes": exes}, sf, indent=2, sort_keys=True)
  with open(args.req_libs_out, "w") as rf:
    json.dump(
        {"has_exes": bool(exes), "req_libs": sorted(req_lib_set)},
        rf,
        indent=2,
        sort_keys=True,
    )


def _cmd_slice_test_libs(args):
  """Packages only the linked CMake libraries required by a single cc_test."""
  out_tar = os.path.abspath(args.out_tar)
  with open(args.req_libs_spec) as rf:
    req_info = json.load(rf)
  has_exes = req_info.get("has_exes", False)
  req_libs = req_info.get("req_libs", [])

  with tarfile.open(out_tar, "w") as out_tf:
    if has_exes:
      wanted = set("build/" + l for l in req_libs)
      with tarfile.open(args.libs_tar, "r") as in_tf:
        members = [
            m
            for m in in_tf.getmembers()
            if m.name == "deps"
            or m.name.startswith("deps/")
            or m.name in wanted
        ]
        members.sort(key=lambda m: m.name)
        for member in members:
          fobj = in_tf.extractfile(member) if member.isfile() else None
          member.mtime = BUILD_MTIME
          member.uid = 0
          member.gid = 0
          member.uname = ""
          member.gname = ""
          out_tf.addfile(member, fobj)


def _cmd_package(args):
  """Runs `cmake --install` and packages the installed prefix archive."""
  exec_root = os.getcwd()
  out_tar = os.path.abspath(args.out_tar)
  install_build_tar = os.path.join(exec_root, args.install_build_tar)
  libs_tar = os.path.join(exec_root, args.libs_tar)

  with tempfile.TemporaryDirectory(prefix="cmake_pkg_") as work_root:
    subprocess.check_call(["tar", "-xf", install_build_tar, "-C", work_root])
    subprocess.check_call(["tar", "-xf", libs_tar, "-C", work_root])
    src_root = os.path.join(work_root, "src")
    build_dir = os.path.join(work_root, "build")
    deps_dir = os.path.join(work_root, "deps")
    install_dir = os.path.join(work_root, "install")

    _replace_in_tree(
        build_dir, WORK_ROOT_TOKEN, work_root.encode(), BUILD_MTIME
    )
    _replace_in_tree(deps_dir, WORK_ROOT_TOKEN, work_root.encode(), BUILD_MTIME)
    _copy_mapped_files(exec_root, src_root, args.hdr_map, mtime=STUB_MTIME)

    env = _make_env(args, work_root=work_root)
    subprocess.check_call(
        ["cmake", "--install", build_dir, "--prefix", install_dir], env=env
    )

    _replace_in_tree(
        install_dir, work_root.encode(), WORK_ROOT_TOKEN, BUILD_MTIME
    )
    _set_tree_mtime(install_dir, BUILD_MTIME)
    entries = sorted(os.listdir(install_dir))
    _tar_create(out_tar, install_dir, entries)


def _cmd_run_test(args):
  """Compiles, links, and executes CMake test targets for a single test rule."""
  exec_root = os.getcwd()
  out_log = os.path.abspath(args.out_log)
  test_libs_tar = os.path.join(exec_root, args.test_libs_tar)
  with open(args.test_spec) as sf:
    exes = json.load(sf).get("exes", [])

  if not exes:
    with open(out_log, "w") as lf:
      lf.write(
          f"SKIPPED {args.test_label} (not part of any CMake test"
          " executable)\n"
      )
    return

  with tempfile.TemporaryDirectory(prefix="cmake_test_") as work_root:
    subprocess.check_call(["tar", "-xf", test_libs_tar, "-C", work_root])
    src_root = os.path.join(work_root, "src")
    build_dir = os.path.join(work_root, "build")
    deps_dir = os.path.join(work_root, "deps")
    os.makedirs(src_root, exist_ok=True)
    os.makedirs(build_dir, exist_ok=True)

    _replace_in_tree(
        build_dir, WORK_ROOT_TOKEN, work_root.encode(), BUILD_MTIME
    )
    _replace_in_tree(deps_dir, WORK_ROOT_TOKEN, work_root.encode(), BUILD_MTIME)
    _copy_mapped_files(exec_root, src_root, args.hdr_map, mtime=STUB_MTIME)
    _copy_mapped_files(exec_root, src_root, args.src_map, mtime=STUB_MTIME)

    extra_env = {}
    tz_exec = os.path.join(
        exec_root, "absl/time/internal/cctz/testdata/zoneinfo"
    )
    tz_src = os.path.join(src_root, "absl/time/internal/cctz/testdata/zoneinfo")
    if os.path.isdir(tz_exec):
      extra_env["TZDIR"] = tz_exec
    elif os.path.isdir(tz_src):
      extra_env["TZDIR"] = tz_src

    env = _make_env(args, work_root=work_root, extra_env=extra_env)

    all_obj_cmds = []
    seen_objs = set()
    for exe in exes:
      for obj_out, obj_cmd in exe.get("obj_cmds", []):
        if obj_out not in seen_objs:
          seen_objs.add(obj_out)
          all_obj_cmds.append((obj_out, obj_cmd))

    sorted_targets = [exe["cmake_name"] for exe in exes]
    ctest_lines = []
    for exe in exes:
      for line in exe.get("ctest_lines", []):
        ctest_lines.append(
            line.replace(WORK_ROOT_TOKEN.decode(), work_root) + "\n"
        )
    with open(os.path.join(build_dir, "CTestTestfile.cmake"), "w") as cf:
      cf.writelines(ctest_lines)

    ctest_regex = "^(" + "|".join(sorted_targets) + ")(_shard_[0-9]+)?$"
    with open(out_log, "w") as lf:
      lf.write(f"=== {args.test_label} ({' '.join(sorted_targets)}) ===\n")
      lf.flush()

      def _run_step(cmd_str, out_rel=None):
        if out_rel:
          os.makedirs(
              os.path.dirname(os.path.join(build_dir, out_rel)), exist_ok=True
          )
        p = subprocess.run(
            cmd_str,
            shell=True,
            cwd=build_dir,
            stdout=lf,
            stderr=subprocess.STDOUT,
            env=env,
            check=False,
        )
        if p.returncode != 0:
          with open(out_log) as rf:
            sys.stderr.write(rf.read())
          sys.exit(p.returncode)

      for obj_out, obj_cmd in all_obj_cmds:
        _run_step(obj_cmd.replace(WORK_ROOT_TOKEN.decode(), work_root), obj_out)
      for exe in exes:
        _run_step(
            exe["exe_cmd"].replace(WORK_ROOT_TOKEN.decode(), work_root),
            exe["exe_out"],
        )

      p = subprocess.run(
          [
              "ctest",
              "--test-dir",
              build_dir,
              "-R",
              ctest_regex,
              "--output-on-failure",
              "--repeat",
              "until-pass:3",
          ],
          stdout=lf,
          stderr=subprocess.STDOUT,
          env=env,
          check=False,
      )
      if p.returncode != 0:
        with open(out_log) as rf:
          sys.stderr.write(rf.read())
        sys.exit(p.returncode)


def _cmd_install_test(args):
  """Runs the CMake installation integration test script in a temp directory."""
  exec_root = os.getcwd()
  out_log = os.path.abspath(args.out_log)

  with tempfile.TemporaryDirectory(prefix="cmake_install_test_") as work_dir:
    _copy_mapped_files(exec_root, work_dir, args.src_map, mtime=None)
    options_h = os.path.join(work_dir, "absl", "base", "options.h")
    if os.path.isfile(options_h):
      with open(options_h) as rf:
        content = rf.read()
      content = content.replace(
          "#define ABSL_OPTION_USE_INLINE_NAMESPACE 1",
          "#define ABSL_OPTION_USE_INLINE_NAMESPACE 0",
      ).replace(
          "#define ABSL_OPTION_INLINE_NAMESPACE_NAME ns",
          "#define ABSL_OPTION_INLINE_NAMESPACE_NAME head",
      )
      with open(options_h, "w") as wf:
        wf.write(content)
    for root, _, files in os.walk(work_dir):
      for f in files:
        if f.endswith((".sh", ".py")):
          os.chmod(os.path.join(root, f), 0o755)

    home_dir = os.path.join(work_dir, "_home")
    os.makedirs(home_dir, exist_ok=True)
    extra_env = {
        "LINK_TYPE": args.link_type,
        "ABSL_INSTALL_TEST_BUILD_DIR": os.path.join(work_dir, "_buildfs"),
        "ABSL_INSTALL_TEST_PREFIX": os.path.join(work_dir, "_install"),
        "ABSL_INSTALL_TEST_GTEST_BUILD_DIR": os.path.join(work_dir, "_gtest"),
        "ABSL_INSTALL_TEST_BUILD_TESTING": "OFF",
        "ABSL_GOOGLETEST_VERSION": "none",
        "ABSL_GOOGLETEST_DOWNLOAD_URL": "",
        "TZDIR": os.path.join(
            work_dir, "absl/time/internal/cctz/testdata/zoneinfo"
        ),
        "HOME": home_dir,
    }
    env = _make_env(args, work_root=work_dir, extra_env=extra_env)
    script_path = os.path.join(work_dir, args.script)
    with open(out_log, "w") as lf:
      p = subprocess.run(
          [script_path],
          cwd=work_dir,
          stdout=lf,
          stderr=subprocess.STDOUT,
          env=env,
          check=False,
      )
      if p.returncode != 0:
        with open(out_log) as rf:
          sys.stderr.write(rf.read())
        sys.exit(p.returncode)


def _cmd_verify_logs(args):
  """Verifies that all expected CMake test action log files exist."""
  for log_path in args.log:
    if not os.path.exists(log_path):
      sys.stderr.write(f"Missing CMake test log: {log_path}\n")
      sys.exit(1)
  print(f"Verified {len(args.log)} CMake ({args.mode}) test action logs.")


def _add_common_args(p):
  """Registers common toolchain and compiler flag CLI arguments on `p`."""
  p.add_argument("--cc", required=True)
  p.add_argument("--cxx", required=True)
  p.add_argument("--ar", default="ar")
  p.add_argument("--cxx-std", default="17")
  p.add_argument("--cflag", action="append", default=[])
  p.add_argument("--cxxflag", action="append", default=[])
  p.add_argument("--ldflag", action="append", default=[])


def main():
  """Parses CLI arguments and dispatches to the requested subcommand."""
  if len(sys.argv) == 1:
    candidate = sys.argv[0] + ".args"
    if os.path.exists(candidate):
      sys.argv.append("@" + candidate)
    elif os.environ.get("TEST_TARGET"):
      target_rel = os.environ["TEST_TARGET"].lstrip("/").replace(":", "/")
      if os.path.exists(target_rel + ".args"):
        sys.argv.append("@" + target_rel + ".args")

  parser = argparse.ArgumentParser(fromfile_prefix_chars="@")
  subparsers = parser.add_subparsers(dest="subcommand", required=True)

  p_gtest = subparsers.add_parser("build_gtest")
  _add_common_args(p_gtest)
  p_gtest.add_argument("--build-type", required=True)
  p_gtest.add_argument("--shared", required=True)
  p_gtest.add_argument("--src-map", action="append", default=[])
  p_gtest.add_argument("--out-tar", required=True)
  p_gtest.set_defaults(func=_cmd_build_gtest)

  p_cfg = subparsers.add_parser("configure")
  _add_common_args(p_cfg)
  p_cfg.add_argument("--build-type", required=True)
  p_cfg.add_argument("--shared", required=True)
  p_cfg.add_argument("--dep-tar", action="append", default=[])
  p_cfg.add_argument("--src-map", action="append", default=[])
  p_cfg.add_argument("--cmake-arg", action="append", default=[])
  p_cfg.add_argument("--objs-spec-out", required=True)
  p_cfg.add_argument("--libs-spec-out", required=True)
  p_cfg.add_argument("--tests-spec-out", required=True)
  p_cfg.add_argument("--install-build-out", required=True)
  p_cfg.set_defaults(func=_cmd_configure)

  p_slice_obj = subparsers.add_parser("slice_obj_spec")
  p_slice_obj.add_argument("--objs-spec", required=True)
  p_slice_obj.add_argument("--cc-rel", action="append", default=[])
  p_slice_obj.add_argument("--out", required=True)
  p_slice_obj.set_defaults(func=_cmd_slice_obj_spec)

  p_objs = subparsers.add_parser("compile_objs")
  _add_common_args(p_objs)
  p_objs.add_argument("--obj-spec", required=True)
  p_objs.add_argument("--dep-tar", action="append", default=[])
  p_objs.add_argument("--src-map", action="append", default=[])
  p_objs.add_argument("--out-tar", required=True)
  p_objs.set_defaults(func=_cmd_compile_objs)

  p_libs = subparsers.add_parser("link_libs")
  _add_common_args(p_libs)
  p_libs.add_argument("--libs-spec", required=True)
  p_libs.add_argument("--dep-tar", action="append", default=[])
  p_libs.add_argument("--obj-tar", action="append", default=[])
  p_libs.add_argument("--out-tar", required=True)
  p_libs.set_defaults(func=_cmd_link_libs)

  p_slice_test_spec = subparsers.add_parser("slice_test_spec")
  p_slice_test_spec.add_argument("--tests-spec", required=True)
  p_slice_test_spec.add_argument("--cc-rel", action="append", default=[])
  p_slice_test_spec.add_argument("--spec-out", required=True)
  p_slice_test_spec.add_argument("--req-libs-out", required=True)
  p_slice_test_spec.set_defaults(func=_cmd_slice_test_spec)

  p_slice_test_libs = subparsers.add_parser("slice_test_libs")
  p_slice_test_libs.add_argument("--req-libs-spec", required=True)
  p_slice_test_libs.add_argument("--libs-tar", required=True)
  p_slice_test_libs.add_argument("--out-tar", required=True)
  p_slice_test_libs.set_defaults(func=_cmd_slice_test_libs)

  p_pkg = subparsers.add_parser("package")
  _add_common_args(p_pkg)
  p_pkg.add_argument("--install-build-tar", required=True)
  p_pkg.add_argument("--libs-tar", required=True)
  p_pkg.add_argument("--hdr-map", action="append", default=[])
  p_pkg.add_argument("--out-tar", required=True)
  p_pkg.set_defaults(func=_cmd_package)

  p_test = subparsers.add_parser("run_test")
  _add_common_args(p_test)
  p_test.add_argument("--test-spec", required=True)
  p_test.add_argument("--test-libs-tar", required=True)
  p_test.add_argument("--hdr-map", action="append", default=[])
  p_test.add_argument("--src-map", action="append", default=[])
  p_test.add_argument("--test-label", required=True)
  p_test.add_argument("--out-log", required=True)
  p_test.set_defaults(func=_cmd_run_test)

  p_inst = subparsers.add_parser("install_test")
  _add_common_args(p_inst)
  p_inst.add_argument("--link-type", required=True)
  p_inst.add_argument("--script", required=True)
  p_inst.add_argument("--src-map", action="append", default=[])
  p_inst.add_argument("--out-log", required=True)
  p_inst.set_defaults(func=_cmd_install_test)

  p_verify = subparsers.add_parser("verify_logs")
  p_verify.add_argument("--mode", required=True)
  p_verify.add_argument("--log", action="append", default=[])
  p_verify.set_defaults(func=_cmd_verify_logs)

  args = parser.parse_args()
  args.func(args)


if __name__ == "__main__":
  main()
