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
#
# Unit and integration tests for Abseil LTS CMake installation

# Fail on any error. Treat unset variables an error. Print commands as executed.
set -euox pipefail

absl_dir="${PWD}"
absl_build_dir="${ABSL_INSTALL_TEST_BUILD_DIR:-/buildfs}"
install_prefix="${ABSL_INSTALL_TEST_PREFIX:-/usr/local}"
mkdir -p "${absl_build_dir}" "${install_prefix}"
googletest_builddir="${ABSL_INSTALL_TEST_GTEST_BUILD_DIR:-/googletest_builddir}"
googletest_archive="googletest-${ABSL_GOOGLETEST_VERSION}.tar.gz"
project_dir="${absl_dir}/CMake/install_test_project"
project_build_dir="${absl_build_dir}/project-build"
export LD_LIBRARY_PATH="${install_prefix}/lib:${LD_LIBRARY_PATH:-}"
export PKG_CONFIG_PATH="${install_prefix}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

build_shared_libs="OFF"
if [ "${LINK_TYPE:-}" = "DYNAMIC" ]; then
  build_shared_libs="ON"
fi

build_testing="${ABSL_INSTALL_TEST_BUILD_TESTING:-ON}"

if [ "${build_testing}" = "ON" ]; then
  # Build and install GoogleTest
  mkdir "${googletest_builddir}"
  pushd "${googletest_builddir}"
  curl -L "${ABSL_GOOGLETEST_DOWNLOAD_URL}" --output "${googletest_archive}"
  tar -xz -f "${googletest_archive}"
  pushd "googletest-${ABSL_GOOGLETEST_VERSION}"
  mkdir build
  pushd build
  cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="${install_prefix}" -DBUILD_SHARED_LIBS="${build_shared_libs}" ..
  cmake --build .
  cmake --install .
  if [ "${install_prefix}" = "/usr/local" ]; then
    ldconfig
  fi
  popd
  popd
  popd
fi

# Run the LTS transformations
./create_lts.py 99998877

# Build and install Abseil
pushd "${absl_build_dir}"
cmake "${absl_dir}" \
  -DCMAKE_INSTALL_PREFIX="${install_prefix}" \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_PREFIX_PATH="${install_prefix}" \
  -DABSL_USE_EXTERNAL_GOOGLETEST="${build_testing}" \
  -DABSL_FIND_GOOGLETEST="${build_testing}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DABSL_BUILD_TESTING="${build_testing}" \
  -DBUILD_SHARED_LIBS="${build_shared_libs}" \
  -DCMAKE_JOB_POOLS="link=$(nproc)" \
  -DCMAKE_JOB_POOL_LINK=link
cmake --build .
if [ "${build_testing}" = "ON" ]; then
  ctest --output-on-failure --repeat until-pass:3
fi
cmake --install .
if [ "${install_prefix}" = "/usr/local" ]; then
  ldconfig
fi
popd

# Test the project against the installed Abseil
mkdir -p "${project_build_dir}"
pushd "${project_build_dir}"
cmake "${project_dir}" -DCMAKE_PREFIX_PATH="${install_prefix}"
cmake --build . --target simple

output="$(${project_build_dir}/simple "printme" 2>&1)"
if [[ "${output}" != *"Arg 1: printme"* ]]; then
  echo "Faulty output on simple project:"
  echo "${output}"
  exit 1
fi

popd

if ! grep absl::strings "${install_prefix}/lib/cmake/absl/abslTargets.cmake"; then
  cat "${install_prefix}/lib/cmake/absl/abslTargets.cmake"
  echo "CMake targets named incorrectly"
  exit 1
fi

pushd "${HOME}"
cat > hello-abseil.cc << EOF
#include <cstdlib>

#include "absl/strings/str_format.h"

int main(int argc, char **argv) {
  absl::PrintF("Hello Abseil!\n");
  return EXIT_SUCCESS;
}
EOF

if [ "${LINK_TYPE:-}" != "DYNAMIC" ]; then
  pc_args=($(pkg-config --cflags --libs --static absl_str_format))
  ${CXX:-g++} ${CXXFLAGS:-} -static -o hello-abseil hello-abseil.cc "${pc_args[@]}" ${LDFLAGS:-}
else
  pc_args=($(pkg-config --cflags --libs absl_str_format))
  ${CXX:-g++} ${CXXFLAGS:-} -o hello-abseil hello-abseil.cc "${pc_args[@]}" ${LDFLAGS:-}
fi
hello="$(./hello-abseil)"
[[ "${hello}" == "Hello Abseil!" ]]

popd

echo "Install test complete!"
exit 0
