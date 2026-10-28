#pragma once

// A game project: a folder with `project.artisan` (JSON: settings and scenes), `scripts/` (Lua)
// and `assets/` (images). Paths inside the project are relative to its folder, with forward
// slashes, so a project moves between machines and platforms as it is.
//
// The format only changes compatibly: new optional fields with defaults, never renames.

#include "artisan/core/Scene.h"

#include <string>
#include <vector>

namespace artisan {

inline constexpr const char* kProjectFile = "project.artisan";
inline constexpr int kFormatVersion = 1;

struct WindowSettings {
    int width = 1280, height = 720; // the game's design resolution and first window size
    bool fullscreen = false;
    bool vsync = true;
    bool pixelArt = false; // nearest-neighbor sampling for sprites
};

struct Project {
    std::string name = "Untitled";
    WindowSettings window;
    std::vector<Scene> scenes{Scene{}};
    int mainScene = 0;

    Scene& scene() { return scenes[static_cast<size_t>(mainScene)]; }
    const Scene& scene() const { return scenes[static_cast<size_t>(mainScene)]; }
};

std::string projectToJson(const Project& project);
bool projectFromJson(const std::string& json, Project& out, std::string* error = nullptr);

// Scenes alone (the editor's undo history keeps them as text).
std::string sceneToJson(const Scene& scene);
bool sceneFromJson(const std::string& json, Scene& out);

// A new project with something to look at and play: a lit player moved by a script, a spinning
// shape driven by a node graph, two lights.
Project starterProject(const std::string& name, int width, int height);
// The script the starter's player uses.
extern const char* const kStarterPlayerScript;

} // namespace artisan
