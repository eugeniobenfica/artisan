#include "app/Plugins.h"

#include "app/Platform.h"
#include "app/Settings.h"
#include "artisan/core/Project.h"

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <QFile>

#include <nlohmann/json.hpp>

#include <algorithm>

namespace artisan::editor {

using json = nlohmann::json;

namespace {

// What a running command works on (in the plugin's registry while it runs).
struct Run {
    Scene* scene = nullptr;
    EntityId* selection = nullptr;
    bool mayEdit = false;
};

constexpr const char* kPluginKey = "artisan.plugin"; // the Plugin* the state belongs to
constexpr const char* kRunKey = "artisan.run";       // the Run*, during a command

Plugin* pluginOf(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kPluginKey);
    auto* p = static_cast<Plugin*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return p;
}

Run& runOf(lua_State* L, bool edit) {
    lua_getfield(L, LUA_REGISTRYINDEX, kRunKey);
    auto* r = static_cast<Run*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!r || !r->scene) luaL_error(L, "the scene is only reachable while a command runs");
    if (edit && !r->mayEdit) luaL_error(L, "this plugin may not change the scene: add \"edit\" to its permissions");
    return *r;
}

// --- JSON <-> Lua ---------------------------------------------------------------------------

void pushJson(lua_State* L, const json& j) {
    switch (j.type()) {
    case json::value_t::boolean: lua_pushboolean(L, j.get<bool>()); break;
    case json::value_t::number_integer:
    case json::value_t::number_unsigned: lua_pushinteger(L, j.get<lua_Integer>()); break;
    case json::value_t::number_float: lua_pushnumber(L, j.get<double>()); break;
    case json::value_t::string: lua_pushstring(L, j.get_ref<const std::string&>().c_str()); break;
    case json::value_t::array: {
        lua_createtable(L, static_cast<int>(j.size()), 0);
        lua_Integer i = 1;
        for (const json& v : j) {
            pushJson(L, v);
            lua_rawseti(L, -2, i++);
        }
        break;
    }
    case json::value_t::object:
        lua_createtable(L, 0, static_cast<int>(j.size()));
        for (const auto& [k, v] : j.items()) {
            pushJson(L, v);
            lua_setfield(L, -2, k.c_str());
        }
        break;
    default: lua_pushnil(L); break;
    }
}

json toJson(lua_State* L, int index, int depth = 0) {
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
    case LUA_TBOOLEAN: return lua_toboolean(L, index) != 0;
    case LUA_TNUMBER: return lua_isinteger(L, index) ? json(lua_tointeger(L, index)) : json(lua_tonumber(L, index));
    case LUA_TSTRING: return lua_tostring(L, index);
    case LUA_TTABLE: {
        if (depth > 32) return nullptr;
        // A sequence (1..n) is an array, anything else an object.
        const lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L, index));
        lua_Integer keys = 0;
        lua_pushnil(L);
        while (lua_next(L, index)) {
            ++keys;
            lua_pop(L, 1);
        }
        if (n > 0 && keys == n) {
            json a = json::array();
            for (lua_Integer i = 1; i <= n; ++i) {
                lua_rawgeti(L, index, i);
                a.push_back(toJson(L, -1, depth + 1));
                lua_pop(L, 1);
            }
            return a;
        }
        json o = json::object();
        lua_pushnil(L);
        while (lua_next(L, index)) {
            if (lua_type(L, -2) == LUA_TSTRING) o[lua_tostring(L, -2)] = toJson(L, -1, depth + 1);
            lua_pop(L, 1);
        }
        return o;
    }
    default: return nullptr;
    }
}

// An entity through the project format (the plugin writes what a project file holds).
bool entityFrom(const json& j, Entity& out) {
    Scene s;
    if (!sceneFromJson(json{{"entities", json::array({j})}}.dump(), s) || s.entities.empty()) return false;
    out = std::move(s.entities.front());
    return true;
}

json entitiesOf(const Scene& scene) {
    const json j = json::parse(sceneToJson(scene), nullptr, false);
    return j.is_object() ? j.value("entities", json::array()) : json::array();
}

json entityJson(const Scene& scene, EntityId id) {
    for (const json& e : entitiesOf(scene))
        if (e.value("id", 0u) == id) return e;
    return nullptr;
}

std::string optString(lua_State* L, int table, const char* key, const std::string& def = {}) {
    lua_getfield(L, table, key);
    std::string s = lua_isstring(L, -1) ? lua_tostring(L, -1) : def;
    lua_pop(L, 1);
    return s;
}

// --- the API ----------------------------------------------------------------------------------

int apiLog(lua_State* L) {
    std::string line;
    for (int i = 1; i <= lua_gettop(L); ++i) {
        if (i > 1) line += " ";
        line += luaL_tolstring(L, i, nullptr);
        lua_pop(L, 1);
    }
    if (Plugin* p = pluginOf(L)) p->log.push_back(line);
    return 0;
}

int apiObject(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Plugin* p = pluginOf(L);
    PluginObject o;
    o.pluginId = p ? p->id : std::string();
    o.name = optString(L, 1, "name");
    if (o.name.empty()) return luaL_error(L, "plugin.object needs a name");
    o.category = optString(L, 1, "category", p ? p->name : "Plugins");
    o.icon = optString(L, 1, "icon", "cube");
    o.tip = optString(L, 1, "tip");
    lua_getfield(L, 1, "make");
    if (!lua_isfunction(L, -1)) return luaL_error(L, "plugin.object \"%s\" needs a make function", o.name.c_str());
    o.make = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_getfield(L, LUA_REGISTRYINDEX, "artisan.objects");
    auto* list = static_cast<std::vector<PluginObject>*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (list) list->push_back(o);
    return 0;
}

int apiCommand(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Plugin* p = pluginOf(L);
    PluginCommand c;
    c.pluginId = p ? p->id : std::string();
    c.name = optString(L, 1, "name");
    if (c.name.empty()) return luaL_error(L, "plugin.command needs a name");
    c.icon = optString(L, 1, "icon", "plugin");
    c.tip = optString(L, 1, "tip");
    lua_getfield(L, 1, "run");
    if (!lua_isfunction(L, -1)) return luaL_error(L, "plugin.command \"%s\" needs a run function", c.name.c_str());
    c.run = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_getfield(L, LUA_REGISTRYINDEX, "artisan.commands");
    auto* list = static_cast<std::vector<PluginCommand>*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (list) list->push_back(c);
    return 0;
}

int sceneObjects(lua_State* L) {
    pushJson(L, entitiesOf(*runOf(L, false).scene));
    return 1;
}

int sceneGet(lua_State* L) {
    const json e = entityJson(*runOf(L, false).scene, static_cast<EntityId>(luaL_checkinteger(L, 1)));
    pushJson(L, e);
    return 1;
}

int sceneAdd(lua_State* L) {
    Run& r = runOf(L, true);
    luaL_checktype(L, 1, LUA_TTABLE);
    json j = toJson(L, 1);
    j.erase("id");
    Entity e;
    if (!entityFrom(j, e)) return luaL_error(L, "scene.add: not an object");
    if (!j.contains("position")) e.position = r.scene->camera.position;
    lua_pushinteger(L, r.scene->add(std::move(e)).id);
    return 1;
}

int sceneSet(lua_State* L) {
    Run& r = runOf(L, true);
    const auto id = static_cast<EntityId>(luaL_checkinteger(L, 1));
    luaL_checktype(L, 2, LUA_TTABLE);
    json current = entityJson(*r.scene, id);
    if (current.is_null()) return luaL_error(L, "scene.set: no object %d", static_cast<int>(id));
    current.merge_patch(toJson(L, 2));
    current["id"] = id;
    Entity e;
    Entity* target = r.scene->find(id);
    if (!target || !entityFrom(current, e)) return luaL_error(L, "scene.set: not an object");
    e.id = id;
    *target = std::move(e);
    return 0;
}

int sceneRemove(lua_State* L) {
    Run& r = runOf(L, true);
    const auto id = static_cast<EntityId>(luaL_checkinteger(L, 1));
    std::erase_if(r.scene->entities, [id](const Entity& e) { return e.id == id; });
    if (*r.selection == id) *r.selection = 0;
    return 0;
}

int sceneSelected(lua_State* L) {
    Run& r = runOf(L, false);
    if (*r.selection) lua_pushinteger(L, *r.selection);
    else lua_pushnil(L);
    return 1;
}

int sceneSelect(lua_State* L) {
    Run& r = runOf(L, false);
    const auto id = static_cast<EntityId>(luaL_optinteger(L, 1, 0));
    *r.selection = r.scene->find(id) ? id : 0;
    return 0;
}

int sceneCamera(lua_State* L) {
    Run& r = runOf(L, false);
    lua_pushnumber(L, r.scene->camera.position.x);
    lua_pushnumber(L, r.scene->camera.position.y);
    return 2;
}

int sceneName(lua_State* L) {
    lua_pushstring(L, runOf(L, false).scene->name.c_str());
    return 1;
}

std::shared_ptr<lua_State> newState(Plugin* plugin) {
    lua_State* L = luaL_newstate();
    // No io, os or package: a plugin reaches only what it is given.
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_settop(L, 0);
    for (const char* unsafe : {"dofile", "loadfile", "require", "collectgarbage"}) {
        lua_pushnil(L);
        lua_setglobal(L, unsafe);
    }
    lua_pushlightuserdata(L, plugin);
    lua_setfield(L, LUA_REGISTRYINDEX, kPluginKey);
    lua_pushcfunction(L, apiLog);
    lua_setglobal(L, "log");
    lua_pushcfunction(L, apiLog);
    lua_setglobal(L, "print");
    const luaL_Reg pluginApi[] = {{"object", apiObject}, {"command", apiCommand}, {nullptr, nullptr}};
    luaL_newlib(L, pluginApi);
    lua_pushstring(L, plugin->id.c_str());
    lua_setfield(L, -2, "id");
    lua_setglobal(L, "plugin");
    const luaL_Reg sceneApi[] = {{"objects", sceneObjects}, {"get", sceneGet},           {"add", sceneAdd},
                                 {"set", sceneSet},         {"remove", sceneRemove},     {"selected", sceneSelected},
                                 {"select", sceneSelect},   {"camera", sceneCamera},     {"name", sceneName},
                                 {nullptr, nullptr}};
    luaL_newlib(L, sceneApi);
    lua_setglobal(L, "scene");
    return std::shared_ptr<lua_State>(L, lua_close);
}

// Calls the function on the top of the stack with `args` below it; the error goes to the log.
bool call(lua_State* L, Plugin& p, int args, int results, std::string* error) {
    if (lua_pcall(L, args, results, 0) == LUA_OK) return true;
    const std::string message = lua_isstring(L, -1) ? lua_tostring(L, -1) : "error";
    lua_pop(L, 1);
    p.log.push_back("error: " + message);
    if (error) *error = message;
    return false;
}

} // namespace

PluginHost& PluginHost::get() {
    static PluginHost host;
    return host;
}

std::filesystem::path PluginHost::builtinDir() { return platform::assetsDir() / "plugins"; }
std::filesystem::path PluginHost::userDir() { return platform::appDataDir() / "plugins"; }

void PluginHost::load() {
    objects_.clear();
    commands_.clear();
    plugins_.clear();
    plugins_.reserve(64); // each state keeps a pointer to its Plugin
    for (const auto& [dir, builtin] : {std::pair{builtinDir(), true}, std::pair{userDir(), false}}) {
        std::error_code ec;
        std::vector<std::filesystem::path> folders;
        for (const auto& f : std::filesystem::directory_iterator(dir, ec))
            if (f.is_directory(ec) && std::filesystem::exists(f.path() / "plugin.json", ec)) folders.push_back(f.path());
        std::sort(folders.begin(), folders.end());
        for (const auto& f : folders)
            if (plugins_.size() < plugins_.capacity()) loadOne(f, builtin);
    }
    emit changed();
}

void PluginHost::loadOne(const std::filesystem::path& dir, bool builtin) {
    Plugin p;
    p.dir = dir;
    p.builtin = builtin;
    p.id = platform::utf8(dir.filename());
    const auto text = platform::readFile(dir / "plugin.json");
    const json m = text ? json::parse(*text, nullptr, false) : json();
    if (!m.is_object()) {
        p.name = p.id;
        p.error = "plugin.json is not valid JSON";
    } else {
        p.id = m.value("id", p.id);
        p.name = m.value("name", p.id);
        p.version = m.value("version", std::string("1.0.0"));
        p.author = m.value("author", std::string());
        p.description = m.value("description", std::string());
        p.icon = m.value("icon", std::string());
        if (auto it = m.find("permissions"); it != m.end() && it->is_array())
            for (const json& x : *it)
                if (x.is_string()) p.permissions.push_back(x.get<std::string>());
    }
    if (find(p.id)) return; // an installed copy of one that comes with Artisan: the first wins
    const auto& off = settings().disabledPlugins;
    p.enabled = std::find(off.begin(), off.end(), p.id) == off.end();
    plugins_.push_back(std::move(p));
    Plugin& plugin = plugins_.back();
    if (!plugin.error.empty() || !plugin.enabled) return;

    const auto code = platform::readFile(dir / "main.lua");
    if (!code) {
        plugin.error = "main.lua is missing";
        return;
    }
    plugin.lua = newState(&plugin);
    lua_State* L = plugin.lua.get();
    std::vector<PluginObject> objects;
    std::vector<PluginCommand> commands;
    lua_pushlightuserdata(L, &objects);
    lua_setfield(L, LUA_REGISTRYINDEX, "artisan.objects");
    lua_pushlightuserdata(L, &commands);
    lua_setfield(L, LUA_REGISTRYINDEX, "artisan.commands");
    const std::string chunk = "@" + plugin.id + "/main.lua";
    std::string error;
    if (luaL_loadbuffer(L, code->data(), code->size(), chunk.c_str()) != LUA_OK) {
        plugin.error = lua_tostring(L, -1);
        plugin.lua.reset();
        return;
    }
    if (!call(L, plugin, 0, 0, &error)) {
        plugin.error = error;
        plugin.lua.reset();
        return;
    }
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "artisan.objects");
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "artisan.commands");
    objects_.insert(objects_.end(), objects.begin(), objects.end());
    commands_.insert(commands_.end(), commands.begin(), commands.end());
}

const Plugin* PluginHost::find(const std::string& id) const {
    for (const Plugin& p : plugins_)
        if (p.id == id) return &p;
    return nullptr;
}

Plugin* PluginHost::findMutable(const std::string& id) { return const_cast<Plugin*>(find(id)); }

std::vector<const PluginObject*> PluginHost::objects() const {
    std::vector<const PluginObject*> out;
    for (const PluginObject& o : objects_) out.push_back(&o);
    return out;
}

std::vector<const PluginCommand*> PluginHost::commands() const {
    std::vector<const PluginCommand*> out;
    for (const PluginCommand& c : commands_) out.push_back(&c);
    return out;
}

const PluginObject* PluginHost::findObject(const std::string& key) const {
    for (const PluginObject& o : objects_)
        if (objectKey(o) == key) return &o;
    return nullptr;
}

void PluginHost::setEnabled(const std::string& id, bool on) {
    auto& off = settings().disabledPlugins;
    std::erase(off, id);
    if (!on) off.push_back(id);
    saveSettings();
    load();
}

bool PluginHost::install(const std::filesystem::path& manifest, std::string* error) {
    const std::filesystem::path from = manifest.parent_path();
    const auto text = platform::readFile(manifest);
    const json m = text ? json::parse(*text, nullptr, false) : json();
    if (!m.is_object()) {
        if (error) *error = "not a plugin.json";
        return false;
    }
    const std::string id = m.value("id", platform::utf8(from.filename()));
    std::error_code ec;
    const std::filesystem::path to = userDir() / platform::fromUtf8(id);
    if (std::filesystem::equivalent(from, to, ec)) return load(), true; // already there
    std::filesystem::create_directories(userDir(), ec);
    std::filesystem::remove_all(to, ec);
    std::filesystem::copy(from, to, std::filesystem::copy_options::recursive, ec);
    if (ec) {
        if (error) *error = ec.message();
        return false;
    }
    load();
    return true;
}

bool PluginHost::uninstall(const std::string& id, std::string* error) {
    const Plugin* p = find(id);
    if (!p || p->builtin) {
        if (error) *error = "it comes with Artisan";
        return false;
    }
    const std::filesystem::path dir = p->dir;
    plugins_.clear(); // its Lua state lets go of nothing on disk, but load() runs anyway
    if (!QFile::moveToTrash(platform::toQt(dir))) {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        if (ec) {
            if (error) *error = ec.message();
            load();
            return false;
        }
    }
    load();
    return true;
}

bool PluginHost::makeObject(const PluginObject& o, Entity& out, std::string* error) {
    Plugin* p = findMutable(o.pluginId);
    if (!p || !p->lua) {
        if (error) *error = "the plugin is not loaded";
        return false;
    }
    lua_State* L = p->lua.get();
    lua_rawgeti(L, LUA_REGISTRYINDEX, o.make);
    lua_newtable(L); // what it fills (or it returns its own table)
    lua_pushstring(L, o.name.c_str());
    lua_setfield(L, -2, "name");
    lua_pushvalue(L, -1);
    lua_insert(L, -3); // the table stays under the call
    if (!call(L, *p, 1, 1, error)) {
        lua_pop(L, 1);
        return false;
    }
    const json j = toJson(L, lua_istable(L, -1) ? -1 : -2);
    lua_pop(L, 2);
    if (!entityFrom(j, out)) {
        if (error) *error = "make did not give an object";
        return false;
    }
    return true;
}

bool PluginHost::runCommand(const PluginCommand& c, Scene& scene, EntityId& selection, std::string* error) {
    Plugin* p = findMutable(c.pluginId);
    if (!p || !p->lua) {
        if (error) *error = "the plugin is not loaded";
        return false;
    }
    lua_State* L = p->lua.get();
    Run run{&scene, &selection, std::find(p->permissions.begin(), p->permissions.end(), "edit") != p->permissions.end()};
    lua_pushlightuserdata(L, &run);
    lua_setfield(L, LUA_REGISTRYINDEX, kRunKey);
    lua_rawgeti(L, LUA_REGISTRYINDEX, c.run);
    const bool ok = call(L, *p, 0, 0, error);
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, kRunKey);
    return ok;
}

} // namespace artisan::editor
