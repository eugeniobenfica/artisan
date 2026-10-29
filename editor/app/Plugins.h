#pragma once

// Plugins: folders with a plugin.json (who it is, what it may do) and a main.lua that adds
// ready-made objects to the library and commands to the Plugins menu. Each plugin runs in a Lua
// state of its own, with no files or system calls: it reaches the scene only through the `scene`
// table, and changes it only with the "edit" permission. Those that come with Artisan are in its
// assets/plugins; installed ones in the app data folder's plugins/. See docs/PLUGINS.md.

#include "artisan/core/Scene.h"

#include <QObject>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct lua_State;

namespace artisan::editor {

struct Plugin {
    std::string id, name, version, author, description;
    std::string icon; // its picture on the Plugins page (an icon name), else its first object's or command's
    std::vector<std::string> permissions; // "edit"
    std::filesystem::path dir;
    bool builtin = false;
    bool enabled = true;
    std::string error;            // it could not load
    std::vector<std::string> log; // what it printed, its errors
    std::shared_ptr<lua_State> lua;
};

struct PluginObject {
    std::string pluginId, name, category, icon, tip;
    int make = 0; // the Lua function (a registry reference)
};

struct PluginCommand {
    std::string pluginId, name, icon, tip;
    int run = 0;
};

class PluginHost : public QObject {
    Q_OBJECT
public:
    static PluginHost& get();
    static std::filesystem::path builtinDir();
    static std::filesystem::path userDir();

    void load(); // every plugin again (after an install, an edit, a switch)
    const std::vector<Plugin>& plugins() const { return plugins_; }
    const Plugin* find(const std::string& id) const;
    std::vector<const PluginObject*> objects() const; // of the plugins turned on
    std::vector<const PluginCommand*> commands() const;
    const PluginObject* findObject(const std::string& key) const; // by objectKey()
    static std::string objectKey(const PluginObject& o) { return "plugin:" + o.pluginId + ":" + o.name; }

    void setEnabled(const std::string& id, bool on);
    bool install(const std::filesystem::path& manifest, std::string* error); // a copy of its folder
    bool uninstall(const std::string& id, std::string* error);                // to the trash

    // Runs the object's `make`: false (and why) if it failed.
    bool makeObject(const PluginObject& o, Entity& out, std::string* error);
    // Runs a command on `scene` (a copy the editor keeps only if it changed); `selection` is the
    // selected object (0: none), and may be changed by the command.
    bool runCommand(const PluginCommand& c, Scene& scene, EntityId& selection, std::string* error);

signals:
    void changed(); // loaded again, turned on or off

private:
    PluginHost() = default;
    void loadOne(const std::filesystem::path& dir, bool builtin);
    Plugin* findMutable(const std::string& id);

    std::vector<Plugin> plugins_;
    std::vector<PluginObject> objects_;
    std::vector<PluginCommand> commands_;
};

} // namespace artisan::editor
