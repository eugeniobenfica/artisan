#pragma once

// The editor's settings (settings.json in the app data folder). Keys never change meaning; new
// ones get defaults, so an older file always loads.

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace artisan::editor {

struct Settings {
    std::string theme = "Dark";      // Dark, Light, Midnight, or a custom theme's name
    std::string customTheme;         // the imported theme (JSON), when theme names it
    std::string projectsFolder;      // where projects live (chosen by the user; empty: not yet)
    std::vector<std::string> recents; // project folders, newest first
    bool showGrid = true;
    int gridSize = 32;
    bool snapToGrid = false;
    bool saveBeforePlay = true;
    bool autosave = true; // the project saves itself a moment after each change
    float uiScale = 1.0f; // the interface font, relative
    std::string uiFont = "Inter";
    bool customAccent = false; // accent: the theme's own, or `accent`
    std::string accent = "#20D4B0";
    bool advanced = false;     // Settings shows every option (each color of the theme)
    std::vector<std::string> closedCategories; // the library's closed categories ("objects:Shapes")
    std::string dockLayout;    // where the panels are (the docking library's state, base64)
    float nodeScale = 1.0f;    // nodes in the graph: Compact, Comfortable (1) or Large
    bool showPath = true;      // the navigator's Project › Scene › Object
    bool showLua = false;      // the Lua a graph becomes, beside it
    bool nodeGrid = true;      // the dots behind the node graph
    std::map<std::string, std::string> shortcuts; // action id -> keys, only where changed ("" for none)
    std::vector<std::string> disabledPlugins;    // plugin ids turned off
};

Settings& settings();
void loadSettings();
void saveSettings();
void addRecent(const std::filesystem::path& projectDir);
void removeRecent(const std::filesystem::path& projectDir);

} // namespace artisan::editor
