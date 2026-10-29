#pragma once

// Lua 5.4 for game behavior. One Lua state per game; every entity's script (and compiled node
// graph) runs in an environment of its own, so `local` and global names in one script never clash
// with another's, and each object keeps its own state.
//
// The API scripts see (docs/SCRIPTING.md):
//   self.x, self.y, self.rotation, self.scale_x, self.scale_y, self.visible, self.layer, self.name
//   self:set_color(r, g, b, a)   self:find(name) -> another object or nil   self:destroy()
//   self:touching([name or object]) -> its hitbox touches one   hit(self, other) when one starts to
//   input.down(key) input.pressed(key) input.released(key) input.mouse_x/mouse_y (world)
//   time.dt time.now   camera.x camera.y camera.zoom   log(...)   vars (the object's variables)

#include "artisan/core/Scene.h"
#include "artisan/runtime/Input.h"

#include <functional>
#include <string>
#include <vector>

struct lua_State;

namespace artisan {

class LuaRuntime {
public:
    LuaRuntime();
    ~LuaRuntime();
    LuaRuntime(const LuaRuntime&) = delete;
    LuaRuntime& operator=(const LuaRuntime&) = delete;

    // Where log() and errors go (the player prints them; the editor shows them).
    std::function<void(const std::string&)> onLog;

    // The scene and input scripts work on, for the state's whole life.
    void bind(Scene* scene, const Input* input);
    // Loads `code` as the behavior of entity `id` (`chunkName` names it in errors). Calls nothing.
    bool attach(EntityId id, const std::string& code, const std::string& chunkName);
    // start() of every attached behavior, once.
    void start();
    // update(self, dt) of every attached behavior; time and camera are synced around it.
    void update(float dt, float now);
    size_t behaviors() const { return behaviors_.size(); }

private:
    void call(int behavior, const char* function, float dt, bool withDt, EntityId other = 0);
    void collide(); // after update: hitboxes, and hit(self, other) for the new contacts
    void syncGlobalsIn(float dt, float now);
    void syncCameraOut();
    void error(const std::string& message);

    lua_State* L_ = nullptr;
    Scene* scene_ = nullptr;
    const Input* input_ = nullptr;
    struct Behavior {
        EntityId entity;
        int env; // registry reference of its environment table
        std::string name;
        bool failed = false; // stopped after an error, not spamming the log every frame
    };
    std::vector<Behavior> behaviors_;
    std::vector<EntityId> destroyed_;
    std::vector<std::pair<EntityId, EntityId>> contacts_; // hitboxes touching after the last frame
    friend struct LuaApi;
};

} // namespace artisan
