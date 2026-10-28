#pragma once

// The open project as the editor's panels share it: the project and its folder, the scene being
// edited (scenes are tabs), the selection, undo / redo, the navigator's history, and the signals
// that keep every panel in step. Panels change the scene, then call changed(); before a change
// they call checkpoint() so it can be undone.
//
// Behavior (a node graph, a Lua script) belongs to the selected object, or to the scene itself
// when no object is selected: graph() and script() are the one being edited.

#include "artisan/core/Project.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace artisan::editor {

class EditorContext : public QObject {
    Q_OBJECT
public:
    Project project;
    std::filesystem::path dir;

    Scene& scene() { return project.scenes[static_cast<size_t>(sceneIndex_)]; }
    int sceneIndex() const { return sceneIndex_; }
    void openScene(int index); // edit another scene (its tab)
    int addScene(Scene s);     // appended, opened
    void removeScene(int index);
    void moveScene(int from, int to);
    void setStartScene(int index);

    Entity* selected() { return scene().find(selection_); }
    EntityId selection() const { return selection_; }
    void select(EntityId id); // 0: the scene itself

    // The behavior being edited: the selected object's, or the scene's.
    std::optional<NodeGraph>& graph();
    std::string& script();
    std::string behaviorOwner(); // the object's name, or the scene's

    // The navigator: back and forward through the scenes and objects that were selected.
    bool back();
    bool forward();
    bool canGoBack() const { return historyAt_ > 0; }
    bool canGoForward() const { return historyAt_ + 1 < static_cast<int>(history_.size()); }

    // Saves the scene as it is now, for undo. Changes under the same `key` close together (a
    // drag, a value being scrubbed) make one step.
    void checkpoint(const std::string& key = {});
    void changed(); // the scene changed: panels refresh, the project is unsaved
    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    bool dirty() const { return dirty_; }
    bool save(std::string* error = nullptr);
    void reset(Project p, std::filesystem::path folder); // a project was opened

    // Text for the console (play output, errors, notes).
    void log(const QString& line) { emit logLine(line); }

signals:
    void scenesChanged(); // scenes added, removed, renamed, reordered, or another one opened
    void sceneChanged();
    void selectionChanged();
    void saved();
    void opened();
    void logLine(const QString& line);
    void editGraphRequested();  // the selected object's node graph, in the Nodes tab
    void editScriptRequested(); // its script, in the Script tab

private:
    struct Snapshot {
        int scene;
        std::string json;
    };
    struct Place {
        int scene;
        EntityId entity;
        bool operator==(const Place&) const = default;
    };
    void restore(const Snapshot& s);
    void remember(); // the place now, in the navigator's history
    void go(const Place& place);

    int sceneIndex_ = 0;
    EntityId selection_ = 0;
    std::vector<Snapshot> undo_, redo_;
    std::vector<Place> history_;
    int historyAt_ = -1;
    bool navigating_ = false;
    std::string lastKey_;
    QElapsedTimer lastTime_;
    bool dirty_ = false;
};

} // namespace artisan::editor
