#include "artisan/runtime/Game.h"

#include "artisan/nodes/NodeCompiler.h"

namespace artisan {

bool Game::load(FileReader read, std::string* error) {
    read_ = std::move(read);
    std::vector<uint8_t> bytes;
    if (!read_ || !read_(kProjectFile, bytes)) {
        if (error) *error = std::string("Cannot read ") + kProjectFile;
        return false;
    }
    if (!projectFromJson(std::string(bytes.begin(), bytes.end()), project, error)) return false;
    scene = project.scene();
    lua.onLog = [this](const std::string& line) {
        if (onLog) onLog(line);
    };
    lua.bind(&scene, &input);
    // The scene's behavior first (no object: id 0), then the objects'.
    if (scene.graph && !scene.graph->nodes.empty()) {
        const CompiledGraph g = compileGraph(*scene.graph);
        for (const std::string& w : g.warnings) lua.onLog("warning: " + scene.name + ": " + w);
        lua.attach(0, g.lua, scene.name + " (nodes)");
    }
    if (!scene.script.empty()) {
        if (std::vector<uint8_t> code; read_(scene.script, code)) lua.attach(0, std::string(code.begin(), code.end()), scene.script);
        else lua.onLog("error: " + scene.name + ": cannot read " + scene.script);
    }
    for (const Entity& e : scene.entities) {
        if (e.graph && !e.graph->nodes.empty()) {
            const CompiledGraph g = compileGraph(*e.graph);
            for (const std::string& w : g.warnings) lua.onLog("warning: " + e.name + ": " + w);
            lua.attach(e.id, g.lua, e.name + " (nodes)");
        }
        if (!e.script.empty()) {
            if (std::vector<uint8_t> code; read_(e.script, code)) lua.attach(e.id, std::string(code.begin(), code.end()), e.script);
            else lua.onLog("error: " + e.name + ": cannot read " + e.script);
        }
    }
    return true;
}

void Game::start() {
    now = 0.0f;
    lua.start();
    input.endFrame();
}

void Game::update(float dt) {
    now += dt;
    lua.update(dt, now);
    input.endFrame();
}

void Game::draw(Renderer2D& r, Vec2 viewport) {
    const View v = gameView(scene, {static_cast<float>(project.window.width), static_cast<float>(project.window.height)}, viewport);
    // The mouse in the scene, for next frame's scripts.
    input.mouseWorld = v.center + (input.mouse - viewport * 0.5f) / v.zoom;
    drawScene(r, scene, v, read_, now);
}

} // namespace artisan
