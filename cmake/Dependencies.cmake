# Third-party dependencies.
#
# Installed on the system (MSYS2 UCRT64 on Windows, the package manager elsewhere; see README):
#   Vulkan headers  - the graphics API (the loader is opened at run time by volk, never linked)
#   glslc           - compiles the GLSL shaders to SPIR-V at build time (shaderc)
#   Qt 6            - the editor only (Widgets, Svg)
#
# Fetched at configure time and pinned to exact versions:
#   SDL3            - the player's window, input, files and Android glue
#   volk            - Vulkan function loader
#   Lua 5.4         - game scripts (node graphs compile to Lua too)
#   nlohmann/json   - the .artisan project format
#   stb             - image loading for sprites

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- Vulkan headers -------------------------------------------------------------
find_package(Vulkan QUIET COMPONENTS glslc)
if(NOT TARGET Vulkan::Headers)
    find_path(ARTISAN_VULKAN_INCLUDE vulkan/vulkan_core.h REQUIRED)
    add_library(Vulkan::Headers INTERFACE IMPORTED)
    target_include_directories(Vulkan::Headers INTERFACE ${ARTISAN_VULKAN_INCLUDE})
endif()

# --- SDL3 (the player) ------------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN ON CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.8
    GIT_SHALLOW    TRUE)

# --- volk -------------------------------------------------------------------------
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        vulkan-sdk-1.4.363.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  no-cmake) # built below against the system headers

# --- nlohmann/json ------------------------------------------------------------------
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# --- stb (images) -------------------------------------------------------------------
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d)

# --- Lua (scripts) ------------------------------------------------------------------
FetchContent_Declare(lua
    URL      https://www.lua.org/ftp/lua-5.4.9.tar.gz
    URL_HASH SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(SDL3 volk json stb lua)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

add_library(volk STATIC ${volk_SOURCE_DIR}/volk.c)
target_include_directories(volk SYSTEM PUBLIC ${volk_SOURCE_DIR})
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)
target_link_libraries(volk PUBLIC Vulkan::Headers ${CMAKE_DL_LIBS})
if(APPLE)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_METAL_EXT)
endif()

# Lua ships a Makefile only: the library (no interpreter or compiler) is built here, as C++, so
# script errors unwind with exceptions (C++ destructors run) instead of longjmp. Include lua.h &
# co directly, not lua.hpp.
file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM LUA_SOURCES ${lua_SOURCE_DIR}/src/lua.c ${lua_SOURCE_DIR}/src/luac.c)
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua SYSTEM PUBLIC ${lua_SOURCE_DIR}/src)
set_target_properties(lua PROPERTIES LINKER_LANGUAGE CXX)
if(UNIX AND NOT APPLE AND NOT ANDROID)
    target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
elseif(APPLE)
    target_compile_definitions(lua PRIVATE LUA_USE_MACOSX)
endif()

# --- Qt (the editor) ------------------------------------------------------------------
if(NOT ARTISAN_PLAYER_ONLY)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Svg)
    qt_standard_project_setup()
endif()
