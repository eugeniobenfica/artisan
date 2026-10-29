#include "app/Shortcuts.h"

#include "app/Settings.h"

namespace artisan::editor {

const std::vector<ActionInfo>& actionInfos() {
    static const std::vector<ActionInfo> actions = {
        {"save", QT_TR_NOOP("Files"), QT_TR_NOOP("Save"), "Ctrl+S"},
        {"new_scene", QT_TR_NOOP("Files"), QT_TR_NOOP("New Scene"), "Ctrl+Shift+N"},
        {"export", QT_TR_NOOP("Files"), QT_TR_NOOP("Export Game"), "Ctrl+E"},
        {"show_folder", QT_TR_NOOP("Files"), QT_TR_NOOP("Show Project Folder"), ""},
        {"close_project", QT_TR_NOOP("Files"), QT_TR_NOOP("Close Project"), "Ctrl+W"},
        {"settings", QT_TR_NOOP("Files"), QT_TR_NOOP("Settings"), "Ctrl+,"},
        {"help", QT_TR_NOOP("Files"), QT_TR_NOOP("Help"), "F1"},
        {"quit", QT_TR_NOOP("Files"), QT_TR_NOOP("Quit"), "Ctrl+Q"},
        {"undo", QT_TR_NOOP("Editing"), QT_TR_NOOP("Undo"), "Ctrl+Z"},
        {"redo", QT_TR_NOOP("Editing"), QT_TR_NOOP("Redo"), "Ctrl+Y"},
        {"duplicate", QT_TR_NOOP("Editing"), QT_TR_NOOP("Duplicate"), "Ctrl+D"},
        {"delete", QT_TR_NOOP("Editing"), QT_TR_NOOP("Delete"), "Del"},
        {"select_all", QT_TR_NOOP("Editing"), QT_TR_NOOP("Select All Nodes"), "Ctrl+A"},
        {"deselect", QT_TR_NOOP("Editing"), QT_TR_NOOP("Select the Scene"), "Esc"},
        {"back", QT_TR_NOOP("Editing"), QT_TR_NOOP("Back"), "Alt+Left"},
        {"forward", QT_TR_NOOP("Editing"), QT_TR_NOOP("Forward"), "Alt+Right"},
        {"view_scene", QT_TR_NOOP("View"), QT_TR_NOOP("Scene"), "Ctrl+1"},
        {"view_nodes", QT_TR_NOOP("View"), QT_TR_NOOP("Nodes"), "Ctrl+2"},
        {"view_scripts", QT_TR_NOOP("View"), QT_TR_NOOP("Scripts"), "Ctrl+3"},
        {"fit_view", QT_TR_NOOP("View"), QT_TR_NOOP("Fit the View"), ""},
        {"reset_layout", QT_TR_NOOP("View"), QT_TR_NOOP("Reset Layout"), ""},
        {"play", QT_TR_NOOP("Game"), QT_TR_NOOP("Play"), "F5"},
        {"stop", QT_TR_NOOP("Game"), QT_TR_NOOP("Stop"), "Shift+F5"},
    };
    return actions;
}

const std::vector<Gesture>& gestures() {
    static const std::vector<Gesture> list = {
        {QT_TR_NOOP("Scene view"), QT_TR_NOOP("Zoom"), "Wheel"},
        {QT_TR_NOOP("Scene view"), QT_TR_NOOP("Pan"), "Middle drag / Right drag / Space+Drag"},
        {QT_TR_NOOP("Scene view"), QT_TR_NOOP("Snap the other way"), "Shift+Drag"},
        {QT_TR_NOOP("Scene view"), QT_TR_NOOP("Fit the game's frame"), "F"},
        {QT_TR_NOOP("Nodes"), QT_TR_NOOP("Add a node"), "Right click / Double click"},
        {QT_TR_NOOP("Nodes"), QT_TR_NOOP("Select several"), "Drag on empty space / Shift+Click"},
        {QT_TR_NOOP("Nodes"), QT_TR_NOOP("Cut a pin's wires"), "Alt+Click"},
        {QT_TR_NOOP("Nodes"), QT_TR_NOOP("Edit a Lua Code node"), "Double click"},
        {QT_TR_NOOP("Nodes"), QT_TR_NOOP("Pan"), "Middle drag / Right drag"},
        {QT_TR_NOOP("Nodes"), QT_TR_NOOP("Frame all nodes"), "F"},
    };
    return list;
}

const ActionInfo* findAction(const std::string& id) {
    for (const ActionInfo& a : actionInfos())
        if (id == a.id) return &a;
    return nullptr;
}

QKeySequence keysOf(const std::string& id) {
    const auto& user = settings().shortcuts;
    if (auto it = user.find(id); it != user.end()) return QKeySequence(QString::fromStdString(it->second), QKeySequence::PortableText);
    const ActionInfo* a = findAction(id);
    return a ? QKeySequence(QString::fromLatin1(a->keys), QKeySequence::PortableText) : QKeySequence();
}

void setKeys(const std::string& id, const QKeySequence& keys) {
    const ActionInfo* a = findAction(id);
    if (!a) return;
    const std::string text = keys.toString(QKeySequence::PortableText).toStdString();
    if (QKeySequence(QString::fromLatin1(a->keys), QKeySequence::PortableText) == keys) settings().shortcuts.erase(id);
    else settings().shortcuts[id] = text;
    saveSettings();
    emit ShortcutSignals::get().changed();
}

void resetKeys(const std::string& id) {
    settings().shortcuts.erase(id);
    saveSettings();
    emit ShortcutSignals::get().changed();
}

void resetAllKeys() {
    settings().shortcuts.clear();
    saveSettings();
    emit ShortcutSignals::get().changed();
}

const ActionInfo* clashWith(const std::string& id) {
    const QKeySequence mine = keysOf(id);
    if (mine.isEmpty()) return nullptr;
    for (const ActionInfo& a : actionInfos())
        if (id != a.id && keysOf(a.id) == mine) return &a;
    return nullptr;
}

ShortcutSignals& ShortcutSignals::get() {
    static ShortcutSignals s;
    return s;
}

} // namespace artisan::editor
