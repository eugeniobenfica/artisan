# artisan: the editor. Layers point down: app (platform, settings, files, export) <- ui.

add_executable(artisan WIN32
    main.cpp
    app/Platform.h
    app/Platform.cpp
    app/Settings.h
    app/Settings.cpp
    app/ProjectIO.h
    app/ProjectIO.cpp
    app/Exporter.h
    app/Exporter.cpp
    ui/EditorContext.h
    ui/EditorContext.cpp
    ui/Widgets.h
    ui/Widgets.cpp
    ui/WindowChrome.h
    ui/WindowChrome.cpp
    ui/HomeScreen.h
    ui/HomeScreen.cpp
    ui/ObjectsPanel.h
    ui/ObjectsPanel.cpp
    ui/Viewport.h
    ui/Viewport.cpp
    ui/NodeEditor.h
    ui/NodeEditor.cpp
    ui/ScriptEditor.h
    ui/ScriptEditor.cpp
    ui/SettingsWindow.h
    ui/SettingsWindow.cpp
    ui/MainWindow.h
    ui/MainWindow.cpp)
set_target_properties(artisan PROPERTIES AUTOMOC ON MACOSX_BUNDLE ON)
target_include_directories(artisan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(artisan PRIVATE artisan_engine bestyle Qt6::Widgets Qt6::Gui nlohmann_json::nlohmann_json)
target_compile_definitions(artisan PRIVATE ARTISAN_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
# The player goes with the editor: the preview runs it, the export copies it.
add_dependencies(artisan artisan-player)

# The editor's assets next to it, so a build runs as it is.
add_custom_command(TARGET artisan POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:artisan>/assets)
