#!/bin/bash
#
# Copyright 2019 The Abseil Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# "Unit" and integration tests for Absl CMake installation

# TODO(absl-team): This script isn't fully hermetic because
# -DABSL_USE_GOOGLETEST_HEAD=ON means that this script isn't pinned to a fixed
# version of GoogleTest. This means that an upstream change to GoogleTest could
# break this test. Fix this by allowing this script to pin to a known-good
# version of GoogleTest.

# Fail on any error. Treat unset variables an error. Print commands as executed.
set -euox pipefail
absl_dir=/abseil-cpp
absl_build_dir=/buildfs/absl-build
project_dir="${absl_dir}"/CMake/install_test_project
project_build_dir=/buildfs/project-build
install_dir="${project_build_dir}"/install

mkdir -p "${absl_build_dir}"
mkdir -p "${project_build_dir}"
mkdir -p "${install_dir}"

install_absl() {
  pushd "${absl_build_dir}"
  if [[ "${#}" -eq 1 ]]; then
    cmake -DCMAKE_INSTALL_PREFIX="${1}" "${absl_dir}"
  else
    cmake "${absl_dir}"
  fi
  cmake --build . --target install -- -j
  popd
}

uninstall_absl() {
  xargs rm < "${absl_build_dir}"/install_manifest.txt
  rm -rf "${absl_build_dir}"
  mkdir -p "${absl_build_dir}"
}

# Test build, install, and link against installed abseil
install_absl "${install_dir}"
pushd "${project_build_dir}"
cmake "${project_dir}" -DCMAKE_PREFIX_PATH="${install_dir}"
cmake --build . --target simple

output="$(${project_build_dir}/simple "printme" 2>&1)"
if [[ "${output}" != *"Arg 1: printme"* ]]; then
  echo "Faulty output on simple project:"
  echo "${output}"
  exit 1
fi

# Test that we haven't accidentally made absl::abslblah
pushd "${install_dir}"

# Starting in CMake 3.12 the default install dir is lib$bit_width
if [[ -d lib ]]; then
  libdir="lib"
elif [[ -d lib64 ]]; then
  libdir="lib64"
else
  echo "ls *, */*, */*/*:"
  ls *
  ls */*
  ls */*/*
  echo "unknown lib dir"
fi

if ! grep absl::strings "${libdir}"/cmake/absl/abslTargets.cmake;  then
  cat "${libdir}"/cmake/absl/abslTargets.cmake
  echo "CMake targets named incorrectly"
  exit 1
fi

uninstall_absl
popd

# Test that we warn if installed without a prefix or a system prefix
output="$(install_absl 2>&1)"
if [[ "${output}" != *"Please set CMAKE_INSTALL_PREFIX"* ]]; then
  echo "Install without prefix didn't warn as expected. Output:"
  echo "${output}"
  exit 1
fi
uninstall_absl

output="$(install_absl /usr 2>&1)"
if [[ "${output}" != *"Please set CMAKE_INSTALL_PREFIX"* ]]; then
  echo "Install with /usr didn't warn as expected. Output:"
  echo "${output}"
  exit 1
fi
uninstall_absl

echo "Install test complete!"
exit 0
