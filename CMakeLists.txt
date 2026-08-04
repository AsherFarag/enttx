cmake_minimum_required(VERSION 3.20)

project(EnTTx LANGUAGES CXX)

add_library(EnTTx INTERFACE)

target_include_directories(EnTTx
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(EnTTx
    INTERFACE
        cxx_std_20
)

option(ENTTX_BUILD_TESTS "Build tests" OFF)
option(ENTTX_BUILD_EXAMPLES "Build examples" OFF)

if(ENTTX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ENTTX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()