#include "artisan/script/LuaRuntime.h"

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <algorithm>
#include <cstring>

namespace artisan {

namespace {

constexpr const char* kObjectMeta = "artisan.Object";
const char kRuntimeKey = 0; // its address keys the runtime in the registry

struct ObjectRef {
    EntityId id;
};

LuaRuntime* runtimeOf(lua_State* L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kRuntimeKey);
    auto* rt = static_cast<LuaRuntime*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return rt;
}

int traceback(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg ? msg : "(error object is not a string)", 1);
    return 1;
}

void pushObject(lua_State* L, EntityId id) {
    auto* ref = static_cast<ObjectRef*>(lua_newuserdatauv(L, sizeof(ObjectRef), 0));
    ref->id = id;
    luaL_setmetatable(L, kObjectMeta);
}

std::string joinArgs(lua_State* L) {
    std::string out;
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        if (i > 1) out += "\t";
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        out.append(s, len);
        lua_pop(L, 1);
    }
    return out;
}

Key checkKey(lua_State* L, int arg) { return keyFromName(luaL_checkstring(L, arg)); }

} // namespace

// The C functions scripts call, with access to the runtime's private parts.
struct LuaApi {
    static Entity* entity(lua_State* L, int arg) {
        auto* ref = static_cast<ObjectRef*>(luaL_checkudata(L, arg, kObjectMeta));
        LuaRuntime* rt = runtimeOf(L);
        if (!rt->scene_) return nullptr;
        if (std::find(rt->destroyed_.begin(), rt->destroyed_.end(), ref->id) != rt->destroyed_.end()) return nullptr;
        return rt->scene_->find(ref->id);
    }

    static int index(lua_State* L) {
        Entity* e = entity(L, 1);
        const char* key = luaL_checkstring(L, 2);
        if (!e) return lua_pushnil(L), 1;
        // Methods first, then fields.
        if (!std::strcmp(key, "set_color")) return lua_pushcfunction(L, setColor), 1;
        if (!std::strcmp(key, "find")) return lua_pushcfunction(L, find), 1;
        if (!std::strcmp(key, "destroy")) return lua_pushcfunction(L, destroy), 1;
        if (!std::strcmp(key, "touching")) return lua_pushcfunction(L, touchingAny), 1;
        if (!std::strcmp(key, "x")) return lua_pushnumber(L, e->position.x), 1;
        if (!std::strcmp(key, "y")) return lua_pushnumber(L, e->position.y), 1;
        if (!std::strcmp(key, "rotation")) return lua_pushnumber(L, e->rotation), 1;
        if (!std::strcmp(key, "scale_x")) return lua_pushnumber(L, e->scale.x), 1;
        if (!std::strcmp(key, "scale_y")) return lua_pushnumber(L, e->scale.y), 1;
        if (!std::strcmp(key, "visible")) return lua_pushboolean(L, e->visible), 1;
        if (!std::strcmp(key, "layer")) return lua_pushinteger(L, e->layer), 1;
        if (!std::strcmp(key, "name")) return lua_pushstring(L, e->name.c_str()), 1;
        if (!std::strcmp(key, "id")) return lua_pushinteger(L, e->id), 1;
        return lua_pushnil(L), 1;
    }

    static int newIndex(lua_State* L) {
        Entity* e = entity(L, 1);
        const char* key = luaL_checkstring(L, 2);
        if (!e) return 0;
        if (!std::strcmp(key, "x")) e->position.x = static_cast<float>(luaL_checknumber(L, 3));
        else if (!std::strcmp(key, "y")) e->position.y = static_cast<float>(luaL_checknumber(L, 3));
        else if (!std::strcmp(key, "rotation")) e->rotation = static_cast<float>(luaL_checknumber(L, 3));
        else if (!std::strcmp(key, "scale_x")) e->scale.x = static_cast<float>(luaL_checknumber(L, 3));
        else if (!std::strcmp(key, "scale_y")) e->scale.y = static_cast<float>(luaL_checknumber(L, 3));
        else if (!std::strcmp(key, "visible")) e->visible = lua_toboolean(L, 3);
        else if (!std::strcmp(key, "layer")) e->layer = static_cast<int>(luaL_checkinteger(L, 3));
        else if (!std::strcmp(key, "name")) e->name = luaL_checkstring(L, 3);
        else return luaL_error(L, "objects have no field '%s' to set", key);
        return 0;
    }

    static int setColor(lua_State* L) {
        Entity* e = entity(L, 1);
        if (!e) return 0;
        const Color c{static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3)),
                      static_cast<float>(luaL_checknumber(L, 4)), static_cast<float>(luaL_optnumber(L, 5, 1.0))};
        if (e->shape) e->shape->color = c;
        if (e->sprite) e->sprite->tint = c;
        if (e->light && !e->shape && !e->sprite) e->light->color = c;
        return 0;
    }

    static int find(lua_State* L) {
        LuaRuntime* rt = runtimeOf(L);
        const char* name = luaL_checkstring(L, lua_gettop(L)); // self:find(name) and scene.find(name)
        Entity* e = rt->scene_ ? rt->scene_->findByName(name) : nullptr;
        if (!e) return lua_pushnil(L), 1;
        pushObject(L, e->id);
        return 1;
    }

    // self:touching() — any hitbox; self:touching("Coin") — the one named so (or an object).
    static int touchingAny(lua_State* L) {
        Entity* e = entity(L, 1);
        LuaRuntime* rt = runtimeOf(L);
        if (!e || !rt->scene_) return lua_pushboolean(L, false), 1;
        if (lua_isuserdata(L, 2)) {
            const Entity* other = entity(L, 2);
            return lua_pushboolean(L, other && touching(*e, *other)), 1;
        }
        const char* name = luaL_optstring(L, 2, "");
        for (const Entity& other : rt->scene_->entities)
            if ((!*name || other.name == name) && touching(*e, other)) return lua_pushboolean(L, true), 1;
        return lua_pushboolean(L, false), 1;
    }

    static int destroy(lua_State* L) {
        Entity* e = entity(L, 1);
        if (e) runtimeOf(L)->destroyed_.push_back(e->id);
        return 0;
    }

    static int log(lua_State* L) {
        LuaRuntime* rt = runtimeOf(L);
        const std::string text = joinArgs(L);
        if (rt->onLog) rt->onLog(text);
        return 0;
    }

    static int keyDown(lua_State* L) {
        const Input* in = runtimeOf(L)->input_;
        return lua_pushboolean(L, in && in->down(checkKey(L, 1))), 1;
    }
    static int keyPressed(lua_State* L) {
        const Input* in = runtimeOf(L)->input_;
        return lua_pushboolean(L, in && in->pressed(checkKey(L, 1))), 1;
    }
    static int keyReleased(lua_State* L) {
        const Input* in = runtimeOf(L)->input_;
        return lua_pushboolean(L, in && in->released(checkKey(L, 1))), 1;
    }
};

LuaRuntime::LuaRuntime() {
    L_ = luaL_newstate();
    // Safe libraries only: no io, os.execute or loading files from disk — the host gives code.
    luaL_requiref(L_, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L_, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(L_, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(L_, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(L_, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    luaL_requiref(L_, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_pop(L_, 6);
    for (const char* unsafe : {"dofile", "loadfile", "load", "require", "collectgarbage"}) {
        lua_pushnil(L_);
        lua_setglobal(L_, unsafe);
    }

    lua_pushlightuserdata(L_, this);
    lua_rawsetp(L_, LUA_REGISTRYINDEX, &kRuntimeKey);

    luaL_newmetatable(L_, kObjectMeta);
    lua_pushcfunction(L_, LuaApi::index);
    lua_setfield(L_, -2, "__index");
    lua_pushcfunction(L_, LuaApi::newIndex);
    lua_setfield(L_, -2, "__newindex");
    lua_pop(L_, 1);

    lua_pushcfunction(L_, LuaApi::log);
    lua_setglobal(L_, "log");
    lua_pushcfunction(L_, LuaApi::log);
    lua_setglobal(L_, "print");

    lua_newtable(L_); // input
    lua_pushcfunction(L_, LuaApi::keyDown);
    lua_setfield(L_, -2, "down");
    lua_pushcfunction(L_, LuaApi::keyPressed);
    lua_setfield(L_, -2, "pressed");
    lua_pushcfunction(L_, LuaApi::keyReleased);
    lua_setfield(L_, -2, "released");
    lua_setglobal(L_, "input");

    lua_newtable(L_); // scene
    lua_pushcfunction(L_, LuaApi::find);
    lua_setfield(L_, -2, "find");
    lua_setglobal(L_, "scene");

    lua_newtable(L_);
    lua_setglobal(L_, "time");
    lua_newtable(L_);
    lua_setglobal(L_, "camera");
}

LuaRuntime::~LuaRuntime() {
    if (L_) lua_close(L_);
}

void LuaRuntime::bind(Scene* scene, const Input* input) {
    scene_ = scene;
    input_ = input;
    if (scene_) {
        lua_getglobal(L_, "camera");
        lua_pushnumber(L_, scene_->camera.position.x);
        lua_setfield(L_, -2, "x");
        lua_pushnumber(L_, scene_->camera.position.y);
        lua_setfield(L_, -2, "y");
        lua_pushnumber(L_, scene_->camera.zoom);
        lua_setfield(L_, -2, "zoom");
        lua_pop(L_, 1);
    }
    syncGlobalsIn(0.0f, 0.0f);
}

void LuaRuntime::error(const std::string& message) {
    if (onLog) onLog("error: " + message);
}

bool LuaRuntime::attach(EntityId id, const std::string& code, const std::string& chunkName) {
    const int top = lua_gettop(L_);
    if (luaL_loadbufferx(L_, code.data(), code.size(), ("@" + chunkName).c_str(), "t") != LUA_OK) {
        error(lua_tostring(L_, -1));
        lua_settop(L_, top);
        return false;
    }
    // Its environment: its own globals, reading through to the shared API.
    lua_newtable(L_);
    lua_newtable(L_);
    lua_pushglobaltable(L_);
    lua_setfield(L_, -2, "__index");
    lua_setmetatable(L_, -2);
    lua_newtable(L_);
    lua_setfield(L_, -2, "vars");
    pushObject(L_, id);
    lua_setfield(L_, -2, "self");
    lua_pushvalue(L_, -1);
    const int env = luaL_ref(L_, LUA_REGISTRYINDEX); // pops the copy
    lua_setupvalue(L_, -2, 1);                        // the chunk's _ENV
    lua_pushcfunction(L_, traceback);
    lua_insert(L_, -2);
    if (lua_pcall(L_, 0, 0, -2) != LUA_OK) {
        error(lua_tostring(L_, -1));
        luaL_unref(L_, LUA_REGISTRYINDEX, env);
        lua_settop(L_, top);
        return false;
    }
    lua_settop(L_, top);
    behaviors_.push_back({id, env, chunkName});
    return true;
}

void LuaRuntime::call(int b, const char* function, float dt, bool withDt, EntityId other) {
    Behavior& beh = behaviors_[static_cast<size_t>(b)];
    if (beh.failed) return;
    const int top = lua_gettop(L_);
    lua_pushcfunction(L_, traceback);
    lua_rawgeti(L_, LUA_REGISTRYINDEX, beh.env);
    if (lua_getfield(L_, -1, function) != LUA_TFUNCTION) {
        lua_settop(L_, top);
        return;
    }
    lua_getfield(L_, -2, "self");
    int args = 1;
    if (withDt) {
        lua_pushnumber(L_, dt);
        ++args;
    }
    if (other) {
        pushObject(L_, other);
        ++args;
    }
    if (lua_pcall(L_, args, 0, top + 1) != LUA_OK) {
        error(lua_tostring(L_, -1));
        beh.failed = true;
    }
    lua_settop(L_, top);
}

void LuaRuntime::syncGlobalsIn(float dt, float now) {
    lua_getglobal(L_, "time");
    lua_pushnumber(L_, dt);
    lua_setfield(L_, -2, "dt");
    lua_pushnumber(L_, now);
    lua_setfield(L_, -2, "now");
    lua_pop(L_, 1);
    if (input_) {
        lua_getglobal(L_, "input");
        lua_pushnumber(L_, input_->mouseWorld.x);
        lua_setfield(L_, -2, "mouse_x");
        lua_pushnumber(L_, input_->mouseWorld.y);
        lua_setfield(L_, -2, "mouse_y");
        lua_pop(L_, 1);
    }
}

void LuaRuntime::syncCameraOut() {
    if (!scene_) return;
    lua_getglobal(L_, "camera");
    lua_getfield(L_, -1, "x");
    lua_getfield(L_, -2, "y");
    lua_getfield(L_, -3, "zoom");
    scene_->camera.position.x = static_cast<float>(lua_tonumberx(L_, -3, nullptr));
    scene_->camera.position.y = static_cast<float>(lua_tonumberx(L_, -2, nullptr));
    if (const float z = static_cast<float>(lua_tonumberx(L_, -1, nullptr)); z > 0.001f) scene_->camera.zoom = z;
    lua_pop(L_, 4);
}

void LuaRuntime::start() {
    syncGlobalsIn(0.0f, 0.0f);
    for (size_t i = 0; i < behaviors_.size(); ++i) call(static_cast<int>(i), "start", 0.0f, false);
    syncCameraOut();
}

void LuaRuntime::update(float dt, float now) {
    syncGlobalsIn(dt, now);
    for (size_t i = 0; i < behaviors_.size(); ++i) call(static_cast<int>(i), "update", dt, true);
    syncCameraOut();
    if (!destroyed_.empty() && scene_) {
        for (EntityId id : destroyed_) {
            scene_->remove(id);
            std::erase_if(behaviors_, [&](const Behavior& b) {
                if (b.entity != id) return false;
                luaL_unref(L_, LUA_REGISTRYINDEX, b.env);
                return true;
            });
        }
        destroyed_.clear();
    }
    collide();
}

void LuaRuntime::collide() {
    if (!scene_) return;
    // Hitboxes pushed out of solid ones; each pair that starts touching calls hit() on both.
    const std::vector<std::pair<EntityId, EntityId>> now = resolveHitboxes(*scene_);
    for (const auto& [a, b] : now) {
        if (std::find(contacts_.begin(), contacts_.end(), std::pair{a, b}) != contacts_.end()) continue;
        for (size_t i = 0; i < behaviors_.size(); ++i) {
            const EntityId self = behaviors_[i].entity;
            if (self == a) call(static_cast<int>(i), "hit", 0.0f, false, b);
            else if (self == b) call(static_cast<int>(i), "hit", 0.0f, false, a);
        }
    }
    contacts_ = now;
    if (!destroyed_.empty()) { // destroyed in hit()
        for (EntityId id : destroyed_) {
            scene_->remove(id);
            std::erase_if(behaviors_, [&](const Behavior& b) {
                if (b.entity != id) return false;
                luaL_unref(L_, LUA_REGISTRYINDEX, b.env);
                return true;
            });
        }
        destroyed_.clear();
    }
}

} // namespace artisan
