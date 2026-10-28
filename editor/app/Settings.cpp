#include "app/Settings.h"

#include "app/Platform.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace artisan::editor {

using json = nlohmann::json;

Settings& settings() {
    static Settings s;
    return s;
}

namespace {

std::filesystem::path file() { return platform::appDataDir() / "settings.json"; }

} // namespace

void loadSettings() {
    Settings& s = settings();
    const auto text = platform::readFile(file());
    if (!text) return;
    const json j = json::parse(*text, nullptr, false);
    if (!j.is_object()) return;
    s.theme = j.value("theme", s.theme);
    s.customTheme = j.value("custom_theme", s.customTheme);
    s.projectsFolder = j.value("projects_folder", s.projectsFolder);
    if (auto it = j.find("recents"); it != j.end() && it->is_array())
        for (const json& r : *it)
            if (r.is_string()) s.recents.push_back(r.get<std::string>());
    s.showGrid = j.value("show_grid", s.showGrid);
    s.gridSize = std::clamp(j.value("grid_size", s.gridSize), 4, 512);
    s.snapToGrid = j.value("snap_to_grid", s.snapToGrid);
    s.saveBeforePlay = j.value("save_before_play", s.saveBeforePlay);
    s.uiScale = std::clamp(j.value("ui_scale", s.uiScale), 0.8f, 1.6f);
    s.customAccent = j.value("custom_accent", s.customAccent);
    s.accent = j.value("accent", s.accent);
    s.advanced = j.value("advanced", s.advanced);
    if (auto it = j.find("closed_categories"); it != j.end() && it->is_array())
        for (const json& c : *it)
            if (c.is_string()) s.closedCategories.push_back(c.get<std::string>());
    s.dockLayout = j.value("dock_layout", s.dockLayout);
}

void saveSettings() {
    const Settings& s = settings();
    const json j = {{"theme", s.theme},          {"custom_theme", s.customTheme}, {"projects_folder", s.projectsFolder},
                    {"recents", s.recents},      {"show_grid", s.showGrid},       {"grid_size", s.gridSize},
                    {"snap_to_grid", s.snapToGrid}, {"save_before_play", s.saveBeforePlay}, {"ui_scale", s.uiScale},
                    {"custom_accent", s.customAccent}, {"accent", s.accent}, {"advanced", s.advanced},
                    {"closed_categories", s.closedCategories}, {"dock_layout", s.dockLayout}};
    platform::writeFile(file(), j.dump(2) + "\n");
}

void addRecent(const std::filesystem::path& dir) {
    const std::string p = platform::utf8(dir);
    auto& r = settings().recents;
    std::erase(r, p);
    r.insert(r.begin(), p);
    if (r.size() > 30) r.resize(30);
    saveSettings();
}

void removeRecent(const std::filesystem::path& dir) {
    std::erase(settings().recents, platform::utf8(dir));
    saveSettings();
}

} // namespace artisan::editor
