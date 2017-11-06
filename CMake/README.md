
## Abseil CMake build instructions


### Recommended usage : incorporate Abseil into an  CMake project

  For API / ABI compatibility reasons, it is recommended to build
  and use abseil in a subdirectory of your project or as an embedded
  dependency

  This is similar to the recommended usage of the googletest framework
  ( https://github.com/google/googletest/blob/master/googletest/README.md )

  Build and use step-by-step


  1- Download abseil and copy it in a sub-directory in your project.
      or add abseil as a git-submodule in your project

  2- If not done yet, download and copy in your project the two dependencies of
      abseil `cctz` and `googletest`

    * cctz  https://github.com/google/cctz
    * googletest https://github.com/google/googletest

  3- You can then use the cmake command `add_subdirectory()` to include
  abseil directly and use the abseil targets in your project.

    Note: Abseil requires CCTZ and the googletest framework. Consequently,
    the targets  `gtest`, `gtest_main`, `gmock` and `cctz` need
    to be declared in your project before including abseil with `add_subdirectory`.


  4- Add the absl:: target you wish to use to the `target_link_libraries()`
    section of your executable or of your library


      Here is a short CMakeLists.txt example of a possible project file
      using abseil

      cmake_minimum_required(VERSION 2.8.12)
      project(my_project)

      set(CMAKE_CXX_FLAGS "-std=c++11 -stdlib=libc++ ${CMAKE_CXX_FLAGS}")

      add_subdirectory(googletest)
      add_subdirectory(cctz)
      add_subdirectory(abseil-cpp)

      add_executable(my_exe source.cpp)
      target_link_libraries(my_exe absl::base absl::synchronization absl::strings)


You will need to create your own CMake files for cctz until https://github.com/google/cctz/pull/54 lands.  As of this writing, that pull request requires -DBUILD_TESTING=OFF as it doesn't correctly export cctz's dependency on Google Benchmark.

    You will find here a non exhaustive list of absl public targets

      absl::base
      absl::algorithm
      absl::container
      absl::debugging
      absl::memory
      absl::meta
      absl::numeric
      absl::strings
      absl::synchronization
      absl::time
      absl::utility





