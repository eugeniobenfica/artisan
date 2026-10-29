#pragma once

// The editor's commands that have keys: each described once (its group, its name, its default
// keys), the keys the user chose kept in the settings (only where they differ). The menus take
// their keys from here, Settings › Shortcuts changes them and Help lists them.

#include <QKeySequence>
#include <QObject>

#include <string>
#include <vector>

namespace artisan::editor {

struct ActionInfo {
    const char* id;    // "save": the settings know it by this, it never changes
    const char* group; // "Files" (translated when shown)
    const char* label; // "Save"
    const char* keys;  // the default, portable text ("Ctrl+S"); "" for none
};

const std::vector<ActionInfo>& actionInfos();
const ActionInfo* findAction(const std::string& id);
QKeySequence keysOf(const std::string& id); // the user's, or the default
void setKeys(const std::string& id, const QKeySequence& keys); // saved; empty: no keys
void resetKeys(const std::string& id);
void resetAllKeys();
// Another action with the same keys (nullptr if none).
const ActionInfo* clashWith(const std::string& id);

// The mouse and the keys panels handle themselves (zoom, pan, frame...), for Help.
struct Gesture {
    const char* group;
    const char* label;
    const char* keys;
};
const std::vector<Gesture>& gestures();

// Says when keys changed (the menus take them again).
class ShortcutSignals : public QObject {
    Q_OBJECT
public:
    static ShortcutSignals& get();

signals:
    void changed();
};

} // namespace artisan::editor
