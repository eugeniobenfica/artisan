# artisan_engine: everything a game needs to run, with no Qt and no windowing (the player and the
# editor bring those). Layers point down: core <- nodes <- script <- gfx <- runtime.

add_library(artisan_engine STATIC
    artisan/core/Math.h
    artisan/core/Scene.h
    artisan/core/Scene.cpp
    artisan/core/Project.h
    artisan/core/Project.cpp
    artisan/nodes/NodeGraph.h
    artisan/nodes/NodeGraph.cpp
    artisan/nodes/NodeLibrary.h
    artisan/nodes/NodeLibrary.cpp
    artisan/nodes/NodeCompiler.h
    artisan/nodes/NodeCompiler.cpp
    artisan/script/LuaRuntime.h
    artisan/script/LuaRuntime.cpp
    artisan/gfx/Renderer2D.h
    artisan/gfx/Renderer2D.cpp
    artisan/gfx/VulkanContext.h
    artisan/gfx/VulkanContext.cpp
    artisan/runtime/Input.h
    artisan/runtime/Input.cpp
    artisan/runtime/SceneDraw.h
    artisan/runtime/SceneDraw.cpp
    artisan/runtime/Game.h
    artisan/runtime/Game.cpp)
artisan_embed_shaders(artisan_engine artisan/gfx/shaders/quad.vert artisan/gfx/shaders/quad.frag)
target_include_directories(artisan_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(artisan_engine PUBLIC volk lua PRIVATE nlohmann_json::nlohmann_json stb)
target_compile_definitions(artisan_engine PUBLIC ARTISAN_VERSION="${PROJECT_VERSION}")
target_compile_features(artisan_engine PUBLIC cxx_std_20)
