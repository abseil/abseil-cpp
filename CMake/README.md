
## Abseil CMake build instructions


### Recommended usage : incorporate Abseil into an  CMake project

    We recommended to build and use abseil in the same way than googletest
    ( https://github.com/google/googletest/blob/master/googletest/README.md )

    * Download abseil and copy it in a sub-directory in your project.

    * Or add abseil as a git-submodule in your project

    You can then use the cmake `add_subdirectory()` command to include
    abseil directly and use the abseil targets in your project.

    Abseil requires CCTZ and the googletest framework. Consequently, 
    the targets  `gtest`, `gtest_main`, `gmock` and `cctz` need
    to be declared in your project before including abseil with `add_subdirectory`. 
    You can find instructions on how to get and build these projects at these 
    URL :
        * cctz  https://github.com/google/cctz
        * googletest https://github.com/google/googletest

    

    Here is a short CMakeLists.txt example of a possible project file 
    using abseil
    
    project(my_project)
    
    add_subdirectory(googletest)
    add_subdirectory(cctz)    
    add_subdirectory(abseil-cpp)

    add_executable(my_exe source.cpp)
    target_link_libraries(my_exe base synchronization strings)




