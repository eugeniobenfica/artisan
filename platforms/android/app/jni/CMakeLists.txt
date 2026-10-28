# The Artisan player for Android, from the engine's sources (ARTISAN_ENGINE_DIR).
cmake_minimum_required(VERSION 3.21)
project(artisan_android LANGUAGES C CXX)
if(NOT ARTISAN_ENGINE_DIR)
    get_filename_component(ARTISAN_ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../.. ABSOLUTE)
endif()
add_subdirectory(${ARTISAN_ENGINE_DIR} artisan)
