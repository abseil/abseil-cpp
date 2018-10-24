#
# Copyright 2017 The Abseil Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

include(CMakeParseArguments)

# The IDE folder for Abseil that will be used if Abseil is included in a CMake
# project that sets
#    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
# For example, Visual Studio supports folders.
set(ABSL_IDE_FOLDER Abseil)

# CMake function to imitate Bazel's cc_library rule.
#
# Parameters:
# NAME: name of target (see Note)
# HDRS: List of public header files for the library
# SRCS: List of source files for the library
# DEPS: List of other libraries to be linked in to the binary targets
# COPTS: List of private compile options
# DEFINES: List of public defines
# LINKOPTS: List of link options
# PUBLIC: Add this so that this library will be exported under absl:: (see Note).
# TESTONLY: When added, this target will only be built if user passes -DABSL_RUN_TESTS=ON to CMake.
#
# Note:
# By default, absl_cc_library will always create a library named absl_internal_${NAME},
# which means other targets can only depend this library as absl_internal_${NAME}, not ${NAME}.
# This is to reduce namespace pollution.
#
# absl_cc_library(
#   NAME awesome_lib
#   HDRS "a.h"
#   SRCS "a.cc"
# )
# absl_cc_library(
#   NAME fantastic_lib
#   SRCS "b.cc"
#   DEPS absl_internal_awesome_lib # not "awesome_lib"!
# )
#
# If PUBLIC is set, absl_cc_library will instead create a target named
# absl_${NAME} and an alias absl::${NAME}.
#
# absl_cc_library(
#   NAME
#     main_lib
#   ...
#   PUBLIC
# )
#
# User can then use the library as absl::main_lib (although absl_main_lib is defined too).
#
# TODO: Implement "ALWAYSLINK"
function(absl_cc_library)
  cmake_parse_arguments(ABSL_CC_LIB
    "DISABLE_INSTALL;PUBLIC;TESTONLY"
    "NAME"
    "HDRS;SRCS;COPTS;DEFINES;LINKOPTS;DEPS"
    ${ARGN}
  )

  if (NOT ABSL_CC_LIB_TESTONLY OR ABSL_RUN_TESTS)
    if (ABSL_CC_LIB_PUBLIC)
      set(_NAME "absl_${ABSL_CC_LIB_NAME}")
    else()
      set(_NAME "absl_internal_${ABSL_CC_LIB_NAME}")
    endif()

    # Check if this is a header-only library
    if ("${ABSL_CC_LIB_SRCS}" STREQUAL "")
      set(ABSL_CC_LIB_IS_INTERFACE 1)
    else()
      set(ABSL_CC_LIB_IS_INTERFACE 0)
    endif()

    if(NOT ABSL_CC_LIB_IS_INTERFACE)
      add_library(${_NAME} STATIC "")
      target_sources(${_NAME} PRIVATE ${ABSL_CC_LIB_SRCS} ${ABSL_CC_LIB_HDRS})
      target_include_directories(${_NAME}
        PUBLIC ${ABSL_COMMON_INCLUDE_DIRS})
      # TODO(rongjiecomputer): Revisit ABSL_COMPILE_CXXFLAGS when fixing GH#123
      target_compile_options(${_NAME}
        PRIVATE ${ABSL_COMPILE_CXXFLAGS} ${ABSL_CC_LIB_COPTS})
      target_link_libraries(${_NAME}
        PUBLIC ${ABSL_CC_LIB_DEPS}
        PRIVATE ${ABSL_CC_LIB_LINKOPTS}
      )
      target_compile_definitions(${_NAME} PUBLIC ${ABSL_CC_LIB_DEFINES})

      # Add all Abseil targets to a a folder in the IDE for organization.
      set_property(TARGET ${_NAME} PROPERTY FOLDER ${ABSL_IDE_FOLDER})
    else()
      # Generating header-only library
      add_library(${_NAME} INTERFACE)
      target_include_directories(${_NAME}
        INTERFACE ${ABSL_COMMON_INCLUDE_DIRS})
      target_link_libraries(${_NAME}
        INTERFACE ${ABSL_CC_LIB_DEPS} ${ABSL_CC_LIB_LINKOPTS}
      )
      target_compile_definitions(${_NAME} INTERFACE ${ABSL_CC_LIB_DEFINES})
    endif()

    if(ABSL_CC_LIB_PUBLIC OR ABSL_CC_LIB_TESTONLY)
      add_library(absl::${ABSL_CC_LIB_NAME} ALIAS ${_NAME})
    endif()
  endif()
endfunction()

# CMake function to imitate Bazel's cc_test rule.
#
# Parameters:
# NAME: name of target (see Note)
# SRCS: List of source files for the binary
# DEPS: List of other libraries to be linked in to the binary targets
# COPTS: List of private compile options
# DEFINES: List of public defines
# LINKOPTS: List of link options
#
# Note:
# absl_cc_test_library(
#   NAME awesome_test
#   SRCS "awesome_test.cc"
# )
function(absl_cc_test)
  cmake_parse_arguments(ABSL_CC_TEST
    ""
    "NAME"
    "SRCS;COPTS;DEFINES;LINKOPTS;DEPS"
    ${ARGN}
  )

  if(ABSL_RUN_TESTS)
    set(_NAME "absl_${ABSL_CC_TEST_NAME}")
    add_executable(${_NAME} "")
    target_sources(${_NAME} PRIVATE ${ABSL_CC_TEST_SRCS})
    target_include_directories(${_NAME}
      PUBLIC ${ABSL_COMMON_INCLUDE_DIRS}
      PRIVATE ${GMOCK_INCLUDE_DIRS} ${GTEST_INCLUDE_DIRS}
    )
    # TODO(rongjiecomputer): Revisit ABSL_COMPILE_CXXFLAGS when fixing GH#123
    target_compile_definitions(${_NAME}
      PUBLIC ${ABSL_CC_TEST_DEFINES}
    )
    target_compile_options(${_NAME}
      PRIVATE ${ABSL_COMPILE_CXXFLAGS} ${ABSL_CC_TEST_COPTS}
    )
    target_link_libraries(${_NAME}
      PUBLIC ${ABSL_CC_TEST_DEPS}
      PRIVATE ${ABSL_CC_TEST_LINKOPTS}
    )
    # Add all Abseil targets to a a folder in the IDE for organization.
    set_property(TARGET ${_NAME} PROPERTY FOLDER ${ABSL_IDE_FOLDER})

    add_test(gtest_${_NAME} ${_NAME})
  endif()
endfunction()

function(check_target my_target)

  if(NOT TARGET ${my_target})
    message(FATAL_ERROR " ABSL: compiling absl requires a ${my_target} CMake target in your project,
                   see CMake/README.md for more details")
  endif(NOT TARGET ${my_target})

endfunction()
