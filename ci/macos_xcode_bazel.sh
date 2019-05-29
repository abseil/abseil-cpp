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

# This script is invoked on Kokoro to test Abseil on MacOS.
# It is not hermetic and may break when Kokoro is updated.

set -euox pipefail

if [ -z ${ABSEIL_ROOT:-} ]; then
  ABSEIL_ROOT="$(realpath $(dirname ${0})/..)"
fi

# Print the default compiler and Bazel versions.
echo "---------------"
gcc -v
echo "---------------"
bazel version
echo "---------------"

cd ${ABSEIL_ROOT}

bazel test ... \
  --copt=-Werror \
  --keep_going \
  --show_timestamps \
  --test_env="TZDIR=${ABSEIL_ROOT}/absl/time/internal/cctz/testdata/zoneinfo" \
  --test_output=errors \
  --test_tag_filters=-benchmark
