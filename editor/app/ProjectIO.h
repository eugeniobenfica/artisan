#pragma once

// Project folders on disk: open, save, create from a template, find the ones in a folder.

#include "artisan/core/Project.h"

#include <filesystem>
#include <string>
#include <vector>

namespace artisan::editor {

namespace fs = std::filesystem;

enum class ProjectTemplate { Starter, Empty };

bool loadProject(const fs::path& dir, Project& out, std::string* error = nullptr);
bool saveProject(const fs::path& dir, const Project& project, std::string* error = nullptr);
// A new folder `name` in `parent` (made unique: "Game 2"), with its project file, scripts/ and
// assets/. Returns the folder, empty on failure.
fs::path createProject(const fs::path& parent, const std::string& name, ProjectTemplate kind, int width, int height,
                       std::string* error = nullptr);

struct ProjectEntry {
    fs::path dir;
    std::string name;
    int64_t modified = 0; // unix seconds (project file)
};
// The projects in `folder` (and two levels under it) plus `extra` (recents elsewhere), newest
// first, each once.
std::vector<ProjectEntry> findProjects(const fs::path& folder, const std::vector<std::string>& extra);

// A file name from a project or object name (no characters the file systems refuse).
std::string safeFileName(std::string name);

// The Lua a new script file starts with.
std::string newScriptTemplate(const std::string& objectName);

} // namespace artisan::editor
