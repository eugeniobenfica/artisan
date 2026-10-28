#pragma once

// The library on the left of the workspace, as bemotion's Objects panel: categories (open or
// closed, remembered) of tiles. In the Scene view it lists ready-made objects (a player that
// already moves, shapes, sprites, lights); in the Nodes view, every node. A click adds one, a drag
// puts it where it is dropped (the viewport, the node graph).

#include "artisan/core/Scene.h"

#include <QScrollArea>

#include <functional>
#include <string>
#include <vector>

class QLineEdit;
class QWidget;

namespace artisan::editor {

class EditorContext;

// Drag data: an object kind ("player", "circle"...), a node type ("action.move").
inline constexpr const char* kObjectMime = "application/x-artisan-object";
inline constexpr const char* kNodeMime = "application/x-artisan-node";

struct ObjectKind {
    std::string kind;     // "player"
    const char* name;     // "Player" (translated when shown)
    const char* category; // "Characters"
    const char* tip;
};
const std::vector<ObjectKind>& objectKinds();

// Makes a ready-made object of `kind` (a sprite asks for its picture: false if none was chosen).
// `parent` hosts the file dialog.
bool makeObject(const std::string& kind, EditorContext& ctx, QWidget* parent, Entity& out);
// Makes it, adds it to the scene at `at` (the camera's center when null), selects it: one undo step.
EntityId addObject(const std::string& kind, EditorContext& ctx, QWidget* parent, const Vec2* at = nullptr);

class LibraryPanel : public QScrollArea {
    Q_OBJECT
public:
    enum class Mode { Objects, Nodes };
    explicit LibraryPanel(EditorContext& ctx, QWidget* parent = nullptr);
    void setMode(Mode mode);
    Mode mode() const { return mode_; }

    std::function<void(const std::string& type)> onAddNode; // a node tile was clicked

signals:
    void titleChanged(const QString& title);

private:
    void rebuild();

    EditorContext& ctx_;
    Mode mode_ = Mode::Objects;
    QString filter_;
};

} // namespace artisan::editor
