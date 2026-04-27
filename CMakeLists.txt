cmake_minimum_required(VERSION 3.20)
project(spudplate VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Allow the release workflow (or developers) to override the version with
# `-DSPUDPLATE_VERSION=v0.2.0` so the binary reports the exact tag it was
# built from. Falls back to the project version otherwise.
if(NOT DEFINED SPUDPLATE_VERSION)
    set(SPUDPLATE_VERSION "${PROJECT_VERSION}")
endif()
# Strip a leading `v` so `cmake -DSPUDPLATE_VERSION=v0.1.0` and
# `cmake -DSPUDPLATE_VERSION=0.1.0` produce the same baked string.
string(REGEX REPLACE "^v" "" SPUDPLATE_VERSION "${SPUDPLATE_VERSION}")

# Library
add_library(spudplate_lib src/lexer.cpp src/parser.cpp src/validator.cpp
                          src/interpreter.cpp src/cli.cpp src/crc32.cpp)
target_include_directories(spudplate_lib PUBLIC include)
target_compile_definitions(spudplate_lib PUBLIC
    SPUDPLATE_VERSION_STRING="${SPUDPLATE_VERSION}")

# Executable
add_executable(spudplate src/main.cpp)
target_link_libraries(spudplate PRIVATE spudplate_lib)

# Install rule — `cmake --install build` drops the binary at the configured
# prefix (default `/usr/local`). `GNUInstallDirs` resolves
# `CMAKE_INSTALL_BINDIR` so distro packagers and the release workflow get the
# expected layout.
include(GNUInstallDirs)
install(TARGETS spudplate RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Testing
include(FetchContent)
# `INSTALL_GTEST OFF` keeps googletest's headers and static libraries out
# of `cmake --install`, so the install only contains the spudplate binary.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(spudplate_tests tests/lexer_test.cpp tests/parser_test.cpp
               tests/validator_test.cpp tests/interpreter_test.cpp
               tests/cli_test.cpp tests/crc32_test.cpp)
target_link_libraries(spudplate_tests PRIVATE spudplate_lib GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(spudplate_tests)

# Documentation
find_package(Doxygen OPTIONAL_COMPONENTS dot)
if(DOXYGEN_FOUND)
    # doxygen-awesome-css gives the generated HTML a modern look. Fetched
    # only when docs are about to be built, kept entirely in the build tree.
    FetchContent_Declare(
        dox_awesome
        GIT_REPOSITORY https://github.com/jothepro/doxygen-awesome-css.git
        GIT_TAG v2.4.2
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(dox_awesome)
    if(NOT dox_awesome_POPULATED)
        FetchContent_Populate(dox_awesome)
    endif()

    add_custom_target(docs
        COMMAND ${CMAKE_COMMAND} -E env
            "DOXYGEN_AWESOME_CSS_DIR=${dox_awesome_SOURCE_DIR}"
            ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()
