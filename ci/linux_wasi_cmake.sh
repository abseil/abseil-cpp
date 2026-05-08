#!/bin/bash
#
# Copyright 2025 The Abseil Authors.
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

# Verify that abseil-cpp builds for WASI using wasi-sdk.

set -euox pipefail

if [[ -z ${ABSEIL_ROOT:-} ]]; then
  ABSEIL_ROOT="$(realpath $(dirname ${0})/..)"
fi

WASI_SDK_VERSION="25.0"
WASI_SDK_DIR="/opt/wasi-sdk-${WASI_SDK_VERSION}-x86_64-linux"

if [[ ! -d "${WASI_SDK_DIR}" ]]; then
  curl -fL "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-25/wasi-sdk-${WASI_SDK_VERSION}-x86_64-linux.tar.gz" \
    | tar xz -C /opt
fi

cmake -B /tmp/abseil-wasi-build -S "${ABSEIL_ROOT}" \
  -DCMAKE_TOOLCHAIN_FILE="${WASI_SDK_DIR}/share/cmake/wasi-sdk-pthread.cmake" \
  -DCMAKE_C_FLAGS="-D_WASI_EMULATED_MMAN -D_WASI_EMULATED_SIGNAL -DABSL_HAVE_MMAP" \
  -DCMAKE_CXX_FLAGS="-D_WASI_EMULATED_MMAN -D_WASI_EMULATED_SIGNAL -DABSL_HAVE_MMAP -fno-exceptions" \
  -DCMAKE_EXE_LINKER_FLAGS="-lwasi-emulated-mman -lwasi-emulated-signal" \
  -DABSL_BUILD_TESTING=OFF

cmake --build /tmp/abseil-wasi-build -j"$(nproc)"

echo "PASS: abseil-cpp builds for WASI with wasi-sdk"
