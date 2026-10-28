cmake_minimum_required(VERSION 3.21)

project(artisan
    VERSION 0.1.0
    DESCRIPTION "Minimal, fast, cross-platform 2D game engine"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# The player alone (engine + runtime, no Qt): what Android and export templates build.
option(ARTISAN_PLAYER_ONLY "Build only the engine and the player (no editor)" OFF)
option(ARTISAN_BUILD_TESTS "Build the engine unit tests" ON)

# The editor and the player side by side (the editor runs and exports the player).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(cmake/Dependencies.cmake)
include(cmake/Shaders.cmake)

add_subdirectory(engine)  # artisan_engine: core, nodes, Lua, Vulkan renderer, runtime (no Qt)
add_subdirectory(player)  # artisan-player: runs a game (SDL3 + Vulkan)

if(NOT ARTISAN_PLAYER_ONLY)
    add_subdirectory(libs/bestyle) # the look, shared with bemotion
    add_subdirectory(editor)       # artisan: the editor (Qt 6 Widgets)
endif()

if(ARTISAN_BUILD_TESTS AND NOT ANDROID)
    enable_testing()
    add_subdirectory(tests)
endif()
