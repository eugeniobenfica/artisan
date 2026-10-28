#pragma once

// A running game: the project as loaded, the live scene, input and the Lua behaviors. Platform
// layers (the SDL player) own the window and the GPU; they feed input, call update() at the
// frame rate and draw() into their render pass.

#include "artisan/core/Project.h"
#include "artisan/gfx/Renderer2D.h"
#include "artisan/runtime/Input.h"
#include "artisan/runtime/SceneDraw.h"
#include "artisan/script/LuaRuntime.h"

#include <functional>
#include <string>

namespace artisan {

class Game {
public:
    using FileReader = Renderer2D::FileReader;

    // Reads `project.artisan` and everything it uses through `read` (paths relative to the
    // project folder), compiles node graphs and loads scripts. Script errors are logged, not
    // fatal: the game runs without the broken behavior.
    bool load(FileReader read, std::string* error = nullptr);
    void start();
    void update(float dt);
    void draw(Renderer2D& r, Vec2 viewportPixels);

    Project project;
    Scene scene; // live, changed by the behaviors
    Input input;
    LuaRuntime lua;
    std::function<void(const std::string&)> onLog;
    float now = 0.0f;

private:
    FileReader read_;
};

} // namespace artisan
