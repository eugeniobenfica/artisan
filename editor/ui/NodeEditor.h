#pragma once

// The node graph editor, in the manner of Unreal's Blueprints: nodes with execution pins (white
// arrows, the order things happen) and data pins (colored by type), wires between them. Right-
// click (or drop a wire on empty space) to add a node, drag headers to move, drag from a pin to
// wire it, Alt+click a pin to cut its wires, click a value to type it, Delete removes a node.
// The graph being edited is the selected object's; the Lua it compiles to shows beside it.

#include "artisan/nodes/NodeGraph.h"
#include "artisan/nodes/NodeLibrary.h"

#include <QPointF>
#include <QWidget>

class QLineEdit;
class QPlainTextEdit;
class QStackedWidget;
class QLabel;

namespace artisan::editor {

class EditorContext;

class NodeCanvas : public QWidget {
    Q_OBJECT
public:
    explicit NodeCanvas(EditorContext& ctx, QWidget* parent = nullptr);
    void frameAll();

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
    bool event(QEvent* e) override; // its keys before the window's shortcuts

private:
    struct PinRef {
        NodeId node = 0;
        std::string pin;
        bool output = false;
        explicit operator bool() const { return node != 0; }
    };
    NodeGraph* graph();
    QPointF toScreen(QPointF graphPoint) const { return graphPoint * zoom_ + pan_; }
    QPointF toGraph(QPointF screenPoint) const { return (screenPoint - pan_) / zoom_; }
    QRectF nodeRect(const Node& n, const NodeDef& def) const;   // in graph units
    QPointF pinPos(const Node& n, const NodeDef& def, const std::string& pin, bool output) const;
    QRectF valueRect(const Node& n, const NodeDef& def, int inputIndex) const;
    NodeId nodeAt(QPointF graphPoint) const;
    PinRef pinAt(QPointF graphPoint) const;
    void openAddMenu(QPoint global, QPointF graphPoint, PinRef from);
    void editValue(NodeId node, const std::string& pin);
    void edited(); // the graph changed: the scene too
    void removeSelected();

    EditorContext& ctx_;
    QPointF pan_{40, 40};
    float zoom_ = 1.0f;
    NodeId selected_ = 0;
    enum class Drag { None, Pan, Move, Wire } drag_ = Drag::None;
    QPointF last_, mouse_, press_;
    PinRef wireFrom_;
    QLineEdit* editor_ = nullptr;
};

// The Nodes tab: the canvas, a note when there is no graph, the compiled Lua beside it.
class NodesTab : public QWidget {
    Q_OBJECT
public:
    explicit NodesTab(EditorContext& ctx, QWidget* parent = nullptr);

private:
    void refresh();
    EditorContext& ctx_;
    QStackedWidget* stack_ = nullptr;
    NodeCanvas* canvas_ = nullptr;
    QPlainTextEdit* lua_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* empty_ = nullptr;
    QWidget* createButton_ = nullptr;
};

} // namespace artisan::editor
