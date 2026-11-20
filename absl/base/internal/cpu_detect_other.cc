// Copyright 2026 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/base/config.h"
#include "absl/base/internal/cpu_detect.h"

// Fallback for architectures without dedicated SIMD backends (e.g., RISC-V,
// s390x, LoongArch, MIPS, WebAssembly, or CUDA device compilation).
#if !defined(ABSL_INTERNAL_CPU_DETECT_X86) && \
    !defined(ABSL_INTERNAL_CPU_DETECT_ARM) && \
    !defined(ABSL_INTERNAL_CPU_DETECT_PPC)

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

CpuFeatures GetCpuFeatures() noexcept { return {}; }

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif
