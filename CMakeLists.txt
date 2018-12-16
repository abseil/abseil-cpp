#
# Copyright 2017 The Abseil Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# We require 3.0 for modern, target-based CMake.  We require 3.1 for the use of
# CXX_STANDARD in our targets.
cmake_minimum_required(VERSION 3.1)

# Compiler id for Apple Clang is now AppleClang.
if (POLICY CMP0025)
  cmake_policy(SET CMP0025 NEW)
endif()

project(absl)

list(APPEND CMAKE_MODULE_PATH
  ${CMAKE_CURRENT_LIST_DIR}/CMake
  ${CMAKE_CURRENT_LIST_DIR}/absl/copts
)

include(GNUInstallDirs)
include(AbseilHelpers)


##
## Using absl targets
##
## all public absl targets are
## exported with the absl:: prefix
##
## e.g absl::base absl::synchronization absl::strings ....
##
## DO NOT rely on the internal targets outside of the prefix


# include current path
list(APPEND ABSL_COMMON_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  set(ABSL_USING_CLANG ON)
else()
  set(ABSL_USING_CLANG OFF)
endif()

# find dependencies
## pthread
find_package(Threads REQUIRED)

option(ABSL_USE_GOOGLETEST_HEAD
  "If ON, abseil will download HEAD from googletest at config time." OFF)

option(ABSL_RUN_TESTS "If ON, Abseil tests will be run." OFF)

if(${ABSL_RUN_TESTS})
  # enable CTest.  This will set BUILD_TESTING to ON unless otherwise specified
  # on the command line
  include(CTest)
  enable_testing()
endif()

## check targets
if(BUILD_TESTING)

  if(${ABSL_USE_GOOGLETEST_HEAD})
    include(CMake/DownloadGTest.cmake)
  endif()

  check_target(gtest)
  check_target(gtest_main)
  check_target(gmock)

  list(APPEND ABSL_TEST_COMMON_LIBRARIES
    gtest_main
    gtest
    gmock
    ${CMAKE_THREAD_LIBS_INIT}
  )
endif()

add_subdirectory(absl)
