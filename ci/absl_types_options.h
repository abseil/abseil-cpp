#ifndef ABSL_BASE_OPTIONS_H_
#define ABSL_BASE_OPTIONS_H_

// Copyright 2019 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Alternate options.h file, requesting to always use Abseil implementations
// of the workalike types, regardless of language version.

#define ABSL_OPTION_USE_STD_ANY 0
#define ABSL_OPTION_USE_STD_OPTIONAL 0
#define ABSL_OPTION_USE_STD_STRING_VIEW 0
#define ABSL_OPTION_USE_STD_VARIANT 0


#endif  // ABSL_BASE_OPTIONS_H_
