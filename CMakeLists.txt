cmake_minimum_required(VERSION 3.10)
project(bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build a static bridge library by default. Consumers can link against this
add_library(bridge STATIC bridge.cpp)
target_compile_definitions(bridge PUBLIC DEBUG)
target_include_directories(bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

option(BUILD_SHARED_BRIDGE "Build shared version of bridge library" OFF)
if(BUILD_SHARED_BRIDGE)
    add_library(bridge_shared SHARED bridge.cpp)
    target_compile_definitions(bridge_shared PUBLIC DEBUG)
    target_include_directories(bridge_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()

message(STATUS "Configured bridge library (static target 'bridge')")
