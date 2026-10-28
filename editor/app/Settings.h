#pragma once

// The editor's settings (settings.json in the app data folder). Keys never change meaning; new
// ones get defaults, so an older file always loads.

#include <filesystem>
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
    float uiScale = 1.0f; // the interface font, relative
};

Settings& settings();
void loadSettings();
void saveSettings();
void addRecent(const std::filesystem::path& projectDir);
void removeRecent(const std::filesystem::path& projectDir);

} // namespace artisan::editor
