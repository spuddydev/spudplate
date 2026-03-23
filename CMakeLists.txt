cmake_minimum_required(VERSION 3.20)
project(spudplate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Library
add_library(spudplate_lib src/lexer.cpp)
target_include_directories(spudplate_lib PUBLIC include)

# Executable
add_executable(spudplate src/main.cpp)
target_link_libraries(spudplate PRIVATE spudplate_lib)

# Testing
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(spudplate_tests tests/lexer_test.cpp)
target_link_libraries(spudplate_tests PRIVATE spudplate_lib GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(spudplate_tests)
