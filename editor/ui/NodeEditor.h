#pragma once

// The node graph editor, in the manner of Unreal's Blueprints: nodes with execution pins (arrows,
// the order things happen) and data pins (colored by type), wires between them. Right-click (or
// drop a wire on empty space) to add a node, drag a node to move it (and every selected one),
// drag on empty space to select several (Shift adds), drag from a pin to wire it, Alt+click a pin
// to cut its wires, click a value to type it, double-click a Lua Code node to write its code,
// Delete removes the selection. Nodes and presets dragged from the library show where they will
// land before they are dropped.
//
// The graph being edited is the selected object's, or the scene's when the scene is selected;
// the Lua it compiles to shows beside it. Nodes look like the rest of the editor: raised cards, a
// header tinted with the category's color and its icon; their size is a setting.

#include "artisan/nodes/NodeGraph.h"
#include "artisan/nodes/NodeLibrary.h"

#include "bestyle/Icons.h"

#include <QPointF>
#include <QWidget>

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

class QLineEdit;
class QPlainTextEdit;
class QStackedWidget;
class QLabel;

namespace artisan::editor {

class EditorContext;

// The node categories in the order they are listed, and how they look (canvas and library).
const std::vector<std::string>& nodeCategories();
bestyle::Icon nodeCategoryIcon(const std::string& category);
bestyle::Rgba nodeCategoryColor(const std::string& category);

// Ready-made pieces of graph (WASD movement, a camera that follows...): nodes already wired,
// placed around (0, 0).
struct NodePreset {
    const char* id;   // "wasd"
    const char* name; // "WASD Movement"
    const char* tip;
    bestyle::Icon icon;
    std::function<NodeGraph()> make;
};
const std::vector<NodePreset>& nodePresets();
const NodePreset* findNodePreset(const std::string& id);

class NodeCanvas : public QWidget {
    Q_OBJECT
public:
    explicit NodeCanvas(EditorContext& ctx, QWidget* parent = nullptr);
    void frameAll();
    // A node of `type`, at `screen` (the middle of the view when none): selected, one undo step.
    void addNode(const std::string& type, std::optional<QPointF> screen = std::nullopt);
    // A preset's nodes, the same way.
    void addPreset(const std::string& id, std::optional<QPointF> screen = std::nullopt);
    void selectAll();
    void removeSelected();
    void duplicateSelected();

signals:
    void graphEdited();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void leaveEvent(QEvent*) override;
    bool event(QEvent* e) override; // its keys before the window's shortcuts
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    struct PinRef {
        NodeId node = 0;
        std::string pin;
        bool output = false;
        explicit operator bool() const { return node != 0; }
        bool operator==(const PinRef&) const = default;
    };
    NodeGraph* graph();
    QPointF toScreen(QPointF graphPoint) const { return graphPoint * zoom_ + pan_; }
    QPointF toGraph(QPointF screenPoint) const { return (screenPoint - pan_) / zoom_; }
    QRectF nodeRect(const Node& n, const NodeDef& def) const; // in graph units
    QPointF pinPos(const Node& n, const NodeDef& def, const std::string& pin, bool output) const;
    QRectF valueRect(const Node& n, const NodeDef& def, const std::string& pin) const;
    QRectF codeRect(const Node& n, const NodeDef& def) const; // a Lua Code node's code
    NodeId nodeAt(QPointF graphPoint) const;
    PinRef pinAt(QPointF graphPoint) const;
    void drawNode(QPainter& p, const Node& n, const NodeDef& def, const NodeGraph* g, bool selected) const;
    void openAddMenu(QPoint global, QPointF graphPoint, PinRef from);
    void editValue(NodeId node, const std::string& pin);
    void editCode(NodeId node);
    void insert(const NodeGraph& part, QPointF at); // nodes (re-numbered) with their wires
    void edited(); // the graph changed: the scene too

    EditorContext& ctx_;
    QPointF pan_{40, 40};
    float zoom_ = 1.0f;
    std::set<NodeId> selected_;
    std::set<NodeId> before_; // the selection when a box selection started (Shift adds to it)
    enum class Drag { None, Pan, Move, Wire, Select } drag_ = Drag::None;
    QPointF last_, mouse_, press_;
    PinRef wireFrom_, hoverPin_;
    bool space_ = false;
    int moves_ = 0;
    // A node or preset being dragged in from the library: drawn where it would land.
    std::string dragType_, dragPreset_;
    QPointF dragAt_;
    QLineEdit* editor_ = nullptr;
};

// The Nodes tab: the canvas, a note when there is no graph, the compiled Lua beside it.
class NodesTab : public QWidget {
    Q_OBJECT
public:
    explicit NodesTab(EditorContext& ctx, QWidget* parent = nullptr);
    NodeCanvas* canvas() const { return canvas_; }

private:
    void refresh();
    EditorContext& ctx_;
    QStackedWidget* stack_ = nullptr;
    NodeCanvas* canvas_ = nullptr;
    QPlainTextEdit* lua_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* empty_ = nullptr;
    QWidget* createButton_ = nullptr;
    QWidget* showLua_ = nullptr;
};

} // namespace artisan::editor
