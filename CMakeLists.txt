cmake_minimum_required(VERSION 3.28)

# Read the version from config.hpp so it has a single source of truth.

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/enttx/config.hpp" ENTTX_CONFIG_HPP)

foreach(ENTTX_VERSION_PART MAJOR MINOR PATCH)
    string(
        REGEX MATCH "#define ENTTX_VERSION_${ENTTX_VERSION_PART} ([0-9]+)"
        _ "${ENTTX_CONFIG_HPP}"
    )
    set(ENTTX_VERSION_${ENTTX_VERSION_PART} "${CMAKE_MATCH_1}")
endforeach()

project(
    EnTTx
    VERSION ${ENTTX_VERSION_MAJOR}.${ENTTX_VERSION_MINOR}.${ENTTX_VERSION_PATCH}
    DESCRIPTION "Extensions for EnTT"
    HOMEPAGE_URL "https://github.com/AsherFarag/enttx"
    LANGUAGES CXX
)

# Project configuration

option(ENTTX_BUILD_TESTING "Enable building tests." OFF)
option(ENTTX_BUILD_EXAMPLES "Enable building examples." OFF)
option(ENTTX_ENABLE_CLANG_TIDY "Enable static analysis with clang-tidy." OFF)

# EnTTx target

add_library(EnTTx INTERFACE)
add_library(EnTTx::EnTTx ALIAS EnTTx)

target_include_directories(
    EnTTx
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(EnTTx INTERFACE cxx_std_20)

# Static analysis

if(ENTTX_ENABLE_CLANG_TIDY)
    find_program(ENTTX_CLANG_TIDY_EXECUTABLE "clang-tidy")

    if(NOT ENTTX_CLANG_TIDY_EXECUTABLE)
        message(VERBOSE
            "The option ENTTX_ENABLE_CLANG_TIDY is set but "
            "clang-tidy executable is not available."
        )
    endif()
endif()

if(ENTTX_CLANG_TIDY_EXECUTABLE)
    set(
        ENTTX_CLANG_TIDY_OPTIONS
        ";--config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy"
        ";--header-filter=${CMAKE_CURRENT_SOURCE_DIR}/include/enttx/.*"
    )

    if(MSVC AND NOT (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang"))
        set(
            ENTTX_CLANG_TIDY_OPTIONS
            "${ENTTX_CLANG_TIDY_OPTIONS}"
            ";--extra-arg=/EHsc"
            ";--extra-arg=/wd4996"
        )
    endif()

    set(
        CMAKE_CXX_CLANG_TIDY
        "${ENTTX_CLANG_TIDY_EXECUTABLE}${ENTTX_CLANG_TIDY_OPTIONS}"
    )
endif()

include(cmake/ClangFormat.cmake)

# Dependencies for tests and examples

if(ENTTX_BUILD_TESTING OR ENTTX_BUILD_EXAMPLES)
    include(FetchContent)

    FetchContent_Declare(
        EnTT
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG v4.0.0
    )

    FetchContent_MakeAvailable(EnTT)
endif()

# Tests

if(ENTTX_BUILD_TESTING)
    include(CTest)
    add_subdirectory(tests)
endif()

# Examples

if(ENTTX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()