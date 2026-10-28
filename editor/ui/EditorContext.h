#pragma once

// The open project as the editor's panels share it: the project and its folder, the selection,
// undo / redo, and the signals that keep every panel in step. Panels change the scene, then call
// changed(); before a change they call checkpoint() so it can be undone.

#include "artisan/core/Project.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <filesystem>
#include <string>
#include <vector>

namespace artisan::editor {

class EditorContext : public QObject {
    Q_OBJECT
public:
    Project project;
    std::filesystem::path dir;

    Scene& scene() { return project.scene(); }
    Entity* selected() { return scene().find(selection_); }
    EntityId selection() const { return selection_; }
    void select(EntityId id);

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
    void sceneChanged();
    void selectionChanged();
    void saved();
    void opened();
    void logLine(const QString& line);
    void editGraphRequested();  // the selected object's node graph, in the Nodes tab
    void editScriptRequested(); // its script, in the Script tab

private:
    void restore(const std::string& json);

    EntityId selection_ = 0;
    std::vector<std::string> undo_, redo_;
    std::string lastKey_;
    QElapsedTimer lastTime_;
    bool dirty_ = false;
};

} // namespace artisan::editor
