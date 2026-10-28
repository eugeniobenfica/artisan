#include "ui/NodeEditor.h"

#include "artisan/nodes/NodeCompiler.h"
#include "ui/EditorContext.h"
#include "ui/Library.h"
#include "ui/Widgets.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMimeData>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>

namespace artisan::editor {

using namespace ui;

namespace {

constexpr float kNodeWidth = 200.0f;
constexpr float kHeader = 30.0f;
constexpr float kRow = 24.0f;
constexpr float kPinR = 4.5f;

// Data pins by type; execution pins are drawn in the text color (they follow the theme).
Rgba pinColor(PinType t) {
    switch (t) {
    case PinType::Exec: return theme::kText;
    case PinType::Number: return rgba(92, 196, 110);
    case PinType::Bool: return rgba(239, 111, 94);
    case PinType::String: return rgba(233, 85, 154);
    case PinType::Any: return theme::kTextDim;
    }
    return theme::kTextDim;
}

} // namespace

const std::vector<std::string>& nodeCategories() {
    static const std::vector<std::string> order = {"Events", "Actions", "Flow", "Object", "Input", "Time", "Math", "Logic", "Values", "Variables"};
    return order;
}

Icon nodeCategoryIcon(const std::string& c) {
    static const std::map<std::string, Icon> icons = {{"Events", Icon::Play},     {"Actions", Icon::Pointer}, {"Flow", Icon::Transition},
                                                      {"Object", Icon::Cube},     {"Input", Icon::Keyboard},  {"Time", Icon::Speed},
                                                      {"Math", Icon::Expression}, {"Logic", Icon::Diamond},   {"Values", Icon::Counter},
                                                      {"Variables", Icon::Table}};
    auto it = icons.find(c);
    return it != icons.end() ? it->second : Icon::Graph;
}

Rgba nodeCategoryColor(const std::string& c) {
    static const std::map<std::string, Rgba> colors = {
        {"Events", rgba(239, 111, 94)}, {"Actions", rgba(80, 150, 250)}, {"Flow", rgba(150, 150, 165)}, {"Object", rgba(45, 196, 180)},
        {"Input", rgba(232, 185, 49)},  {"Time", rgba(242, 140, 40)},    {"Math", rgba(92, 196, 110)},  {"Logic", rgba(233, 85, 154)},
        {"Values", rgba(160, 110, 240)}, {"Variables", rgba(110, 168, 255)}};
    auto it = colors.find(c);
    return it != colors.end() ? it->second : rgba(150, 150, 165);
}

NodeCanvas::NodeCanvas(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true); // nodes from the library
    setMinimumSize(200, 200);
}

NodeGraph* NodeCanvas::graph() {
    std::optional<NodeGraph>& g = ctx_.graph();
    return g ? &*g : nullptr;
}

void NodeCanvas::addNode(const std::string& type, std::optional<QPointF> screen) {
    NodeGraph* g = graph();
    if (!g || !findNodeDef(type)) return;
    const QPointF at = toGraph(screen.value_or(QPointF(width() / 2.0, height() / 2.0)));
    ctx_.checkpoint();
    Node& n = g->add(type, static_cast<float>(at.x() - kNodeWidth / 2), static_cast<float>(at.y() - kHeader / 2));
    selected_ = n.id;
    edited();
    setFocus();
}

void NodeCanvas::dragEnterEvent(QDragEnterEvent* e) {
    if (graph() && e->mimeData()->hasFormat(kNodeMime)) e->acceptProposedAction();
}

void NodeCanvas::dragMoveEvent(QDragMoveEvent* e) {
    if (graph() && e->mimeData()->hasFormat(kNodeMime)) e->acceptProposedAction();
}

void NodeCanvas::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasFormat(kNodeMime)) return;
    addNode(e->mimeData()->data(kNodeMime).toStdString(), e->position());
    e->acceptProposedAction();
}

QRectF NodeCanvas::nodeRect(const Node& n, const NodeDef& def) const {
    const size_t rows = std::max<size_t>({def.inputs.size(), def.outputs.size(), 1});
    return {n.x, n.y, kNodeWidth, kHeader + rows * kRow + 8};
}

QPointF NodeCanvas::pinPos(const Node& n, const NodeDef& def, const std::string& pin, bool output) const {
    const auto& pins = output ? def.outputs : def.inputs;
    for (size_t i = 0; i < pins.size(); ++i)
        if (pins[i].name == pin) return {n.x + (output ? kNodeWidth : 0.0f), n.y + kHeader + i * kRow + kRow * 0.5f + 4};
    return {n.x, n.y};
}

QRectF NodeCanvas::valueRect(const Node& n, const NodeDef& def, int i) const {
    const float y = n.y + kHeader + i * kRow + 4;
    (void)def;
    return {n.x + 84, y + 3, 72, kRow - 6};
}

NodeId NodeCanvas::nodeAt(QPointF p) const {
    const NodeGraph* g = const_cast<NodeCanvas*>(this)->graph();
    if (!g) return 0;
    for (auto it = g->nodes.rbegin(); it != g->nodes.rend(); ++it)
        if (const NodeDef* d = findNodeDef(it->type); d && nodeRect(*it, *d).contains(p)) return it->id;
    return 0;
}

NodeCanvas::PinRef NodeCanvas::pinAt(QPointF p) const {
    const NodeGraph* g = const_cast<NodeCanvas*>(this)->graph();
    if (!g) return {};
    const float reach = std::max(10.0f, 10.0f / zoom_);
    for (const Node& n : g->nodes) {
        const NodeDef* d = findNodeDef(n.type);
        if (!d) continue;
        for (bool out : {false, true})
            for (const PinDef& pin : out ? d->outputs : d->inputs) {
                const QPointF c = pinPos(n, *d, pin.name, out);
                // The pin itself, or its label beside it.
                const QRectF area = out ? QRectF(c.x() - 40, c.y() - kRow / 2, 40 + reach, kRow) : QRectF(c.x() - reach, c.y() - kRow / 2, 60, kRow);
                if (QLineF(c, p).length() < reach || area.contains(p)) return {n.id, pin.name, out};
            }
    }
    return {};
}

void NodeCanvas::frameAll() {
    NodeGraph* g = graph();
    if (!g || g->nodes.empty()) {
        pan_ = {40, 40};
        zoom_ = 1.0f;
        update();
        return;
    }
    QRectF all;
    for (const Node& n : g->nodes)
        if (const NodeDef* d = findNodeDef(n.type)) all = all.isNull() ? nodeRect(n, *d) : all.united(nodeRect(n, *d));
    zoom_ = std::clamp(static_cast<float>(std::min((width() - 60) / all.width(), (height() - 60) / all.height())), 0.3f, 1.0f);
    pan_ = QPointF(width() / 2.0, height() / 2.0) - all.center() * zoom_;
    update();
}

void NodeCanvas::edited() {
    ctx_.changed();
    emit graphEdited();
    update();
}

void NodeCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), theme::q(theme::kBgDeep));
    // Dots every 32 units, stronger every 4th.
    const float step = 32.0f * zoom_;
    if (step > 6) {
        const int firstX = static_cast<int>(std::ceil(-pan_.x() / step)), firstY = static_cast<int>(std::ceil(-pan_.y() / step));
        for (int ix = firstX; ix * step + pan_.x() < width(); ++ix)
            for (int iy = firstY; iy * step + pan_.y() < height(); ++iy) {
                const bool major = ix % 4 == 0 && iy % 4 == 0;
                p.setPen(QPen(theme::q(theme::ink(major ? 50 : 24)), major ? 2.5f : 1.5f, Qt::SolidLine, Qt::RoundCap));
                p.drawPoint(QPointF(ix * step + pan_.x(), iy * step + pan_.y()));
            }
    }
    NodeGraph* g = graph();
    if (!g) return;
    p.translate(pan_);
    p.scale(zoom_, zoom_);
    const float radius = theme::current().rounding + 3.0f;

    auto wire = [&](QPointF a, QPointF b, Rgba c, float width) {
        const float dx = std::max(40.0f, static_cast<float>(std::fabs(b.x() - a.x())) * 0.5f);
        QPainterPath path(a);
        path.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
        p.setPen(QPen(theme::q(c), width, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    };
    for (const Link& l : g->links) {
        const Node* a = g->find(l.fromNode);
        const Node* b = g->find(l.toNode);
        const NodeDef* ad = a ? findNodeDef(a->type) : nullptr;
        const NodeDef* bd = b ? findNodeDef(b->type) : nullptr;
        if (!ad || !bd || !ad->output(l.fromPin)) continue;
        const PinType t = ad->output(l.fromPin)->type;
        // The selected node's wires stand out.
        const bool lit = a->id == selected_ || b->id == selected_;
        const Rgba c = t == PinType::Exec ? (lit ? theme::kAccent : theme::withAlpha(theme::kText, 170)) : theme::withAlpha(pinColor(t), lit ? 255 : 200);
        wire(pinPos(*a, *ad, l.fromPin, true), pinPos(*b, *bd, l.toPin, false), c, t == PinType::Exec ? 2.6f : 2.0f);
    }
    if (drag_ == Drag::Wire && wireFrom_) {
        const Node* n = g->find(wireFrom_.node);
        const NodeDef* d = n ? findNodeDef(n->type) : nullptr;
        if (d) {
            const PinDef* pin = wireFrom_.output ? d->output(wireFrom_.pin) : d->input(wireFrom_.pin);
            const QPointF a = pinPos(*n, *d, wireFrom_.pin, wireFrom_.output), b = toGraph(mouse_);
            wire(wireFrom_.output ? a : b, wireFrom_.output ? b : a, pin ? pinColor(pin->type) : theme::kText, 2.0f);
        }
    }

    QFont f = font();
    QFont bold = f;
    bold.setWeight(QFont::DemiBold);
    Draw d(p);
    for (const Node& n : g->nodes) {
        const NodeDef* def = findNodeDef(n.type);
        if (!def) continue;
        const QRectF r = nodeRect(n, *def);
        const Rgba cat = nodeCategoryColor(def->category);
        const bool sel = n.id == selected_;
        // A raised card with a soft shadow, its header tinted with the category's color.
        d.rectFilled(Pt(r.left(), r.top() + 3), Pt(r.right(), r.bottom() + 3), theme::shade(70), radius);
        d.rectFilled(Pt(r.topLeft()), Pt(r.bottomRight()), theme::kBgRaised, radius);
        QPainterPath card;
        card.addRoundedRect(r, radius, radius);
        p.save();
        p.setClipPath(card);
        p.fillRect(QRectF(r.left(), r.top(), r.width(), kHeader), theme::q(theme::withAlpha(cat, theme::current().light ? 46 : 38)));
        p.fillRect(QRectF(r.left(), r.top(), 3, kHeader), theme::q(cat)); // the category's mark
        p.restore();
        d.line(Pt(r.left(), r.top() + kHeader), Pt(r.right(), r.top() + kHeader), theme::kBorder, 1.0f);
        d.rect(Pt(r.topLeft()), Pt(r.bottomRight()), sel ? theme::kAccent : theme::kBorder, radius, sel ? 2.0f : 1.0f);
        drawIcon(d, nodeCategoryIcon(def->category), Pt(r.left() + 18, r.top() + kHeader / 2), 14.0f, cat);
        p.setFont(bold);
        p.setPen(theme::q(theme::kText));
        p.drawText(QRectF(r.left() + 32, r.top(), r.width() - 40, kHeader), Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(QString::fromStdString(def->title), Qt::ElideRight, static_cast<int>(r.width() - 40)));
        p.setFont(f);

        for (bool out : {false, true}) {
            const auto& pins = out ? def->outputs : def->inputs;
            for (size_t i = 0; i < pins.size(); ++i) {
                const PinDef& pin = pins[i];
                const QPointF c = pinPos(n, *def, pin.name, out);
                const bool linked = out ? !g->linksFrom(n.id, pin.name).empty() : g->linkInto(n.id, pin.name) != nullptr;
                const Rgba col = pinColor(pin.type);
                p.setPen(QPen(theme::q(col), 1.5));
                p.setBrush(linked ? theme::q(col) : theme::q(theme::kBgRaised));
                if (pin.type == PinType::Exec) { // a small arrow: the order things happen
                    const QPointF tri[] = {c + QPointF(-kPinR, -kPinR - 1), c + QPointF(kPinR + 1, 0), c + QPointF(-kPinR, kPinR + 1)};
                    p.drawPolygon(tri, 3);
                } else {
                    p.drawEllipse(c, kPinR, kPinR);
                }
                const bool named = !(pin.type == PinType::Exec && (pin.name == "in" || pin.name == "then"));
                p.setPen(theme::q(theme::kTextDim));
                if (named) {
                    const QRectF lr = out ? QRectF(c.x() - 120, c.y() - kRow / 2, 110, kRow) : QRectF(c.x() + 12, c.y() - kRow / 2, 70, kRow);
                    p.drawText(lr, Qt::AlignVCenter | (out ? Qt::AlignRight : Qt::AlignLeft), QString::fromStdString(pin.name));
                }
                // A value typed in for an input with no wire.
                if (!out && pin.type != PinType::Exec && !linked) {
                    const QRectF vr = valueRect(n, *def, static_cast<int>(i));
                    const float fr = theme::current().rounding;
                    d.rectFilled(Pt(vr.topLeft()), Pt(vr.bottomRight()), theme::kField, fr);
                    d.rect(Pt(vr.topLeft()), Pt(vr.bottomRight()), theme::kFieldBorder, fr, 1.0f);
                    auto it = n.values.find(pin.name);
                    QString text = QString::fromStdString(it != n.values.end() ? it->second : pin.defaultValue);
                    if (pin.type == PinType::Bool) text = text == "true" ? "✓ true" : "false";
                    p.setPen(theme::q(theme::kText));
                    p.drawText(vr.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                               p.fontMetrics().elidedText(text, Qt::ElideRight, static_cast<int>(vr.width() - 10)));
                }
            }
        }
    }
}

void NodeCanvas::mousePressEvent(QMouseEvent* e) {
    setFocus();
    last_ = mouse_ = press_ = e->position();
    if (editor_) editor_->clearFocus();
    NodeGraph* g = graph();
    if (!g) return;
    const QPointF gp = toGraph(e->position());
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && e->modifiers() & Qt::ControlModifier && !nodeAt(gp))) {
        drag_ = Drag::Pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() == Qt::RightButton) {
        if (const NodeId id = nodeAt(gp)) {
            selected_ = id;
            update();
            QMenu menu;
            menu.addAction(menuIcon(Icon::Duplicate, theme::kTextDim), tr("Duplicate"), [this, id] {
                NodeGraph* gr = graph();
                const Node* n = gr ? gr->find(id) : nullptr;
                if (!n) return;
                ctx_.checkpoint();
                Node copy = *n;
                Node& added = gr->add(copy.type, copy.x + 30, copy.y + 30);
                added.values = copy.values;
                selected_ = added.id;
                edited();
            });
            menu.addAction(menuIcon(Icon::Trash, theme::kDanger), tr("Delete"), [this] { removeSelected(); });
            menu.exec(e->globalPosition().toPoint());
        } else {
            drag_ = Drag::Pan; // a right drag pans; a click without moving opens the menu (on release)
            setCursor(Qt::ClosedHandCursor);
        }
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    if (const PinRef pin = pinAt(gp)) {
        if (e->modifiers() & Qt::AltModifier) { // cut its wires
            ctx_.checkpoint();
            std::erase_if(g->links, [&](const Link& l) {
                return pin.output ? (l.fromNode == pin.node && l.fromPin == pin.pin) : (l.toNode == pin.node && l.toPin == pin.pin);
            });
            edited();
            return;
        }
        drag_ = Drag::Wire;
        wireFrom_ = pin;
        return;
    }
    selected_ = nodeAt(gp);
    if (const Node* n = selected_ ? g->find(selected_) : nullptr) {
        const NodeDef* d = findNodeDef(n->type);
        // A click on a value edits it.
        for (size_t i = 0; d && i < d->inputs.size(); ++i) {
            const PinDef& pin = d->inputs[i];
            if (pin.type == PinType::Exec || g->linkInto(n->id, pin.name)) continue;
            if (valueRect(*n, *d, static_cast<int>(i)).contains(gp)) {
                editValue(n->id, pin.name);
                update();
                return;
            }
        }
        drag_ = Drag::Move;
        // The node goes on top (drawn last).
        auto it = std::find_if(g->nodes.begin(), g->nodes.end(), [&](const Node& x) { return x.id == selected_; });
        std::rotate(it, it + 1, g->nodes.end());
    }
    update();
}

void NodeCanvas::mouseMoveEvent(QMouseEvent* e) {
    const QPointF d = e->position() - last_;
    last_ = mouse_ = e->position();
    switch (drag_) {
    case Drag::Pan: pan_ += d; break;
    case Drag::Move:
        if (NodeGraph* g = graph(); Node* n = g ? g->find(selected_) : nullptr) {
            ctx_.checkpoint("node move " + std::to_string(n->id));
            n->x += static_cast<float>(d.x() / zoom_);
            n->y += static_cast<float>(d.y() / zoom_);
            ctx_.changed();
        }
        break;
    default: break;
    }
    update();
}

void NodeCanvas::mouseReleaseEvent(QMouseEvent* e) {
    const Drag was = drag_;
    drag_ = Drag::None;
    unsetCursor();
    NodeGraph* g = graph();
    if (!g) return;
    const QPointF gp = toGraph(e->position());
    if (was == Drag::Pan && e->button() == Qt::RightButton && (e->position() - press_).manhattanLength() < 4)
        openAddMenu(e->globalPosition().toPoint(), gp, PinRef{}); // a right click, not a drag
    if (was != Drag::Wire || !wireFrom_) return;
    const PinRef from = wireFrom_;
    wireFrom_ = {};
    const PinRef to = pinAt(gp);
    if (!to) {
        if (!nodeAt(gp)) openAddMenu(e->globalPosition().toPoint(), gp, from); // dropped on nothing: add a node there
        update();
        return;
    }
    if (to.output == from.output) {
        update();
        return;
    }
    const Link link = from.output ? Link{from.node, from.pin, to.node, to.pin} : Link{to.node, to.pin, from.node, from.pin};
    NodeGraph test = *g;
    if (test.connect(link)) {
        ctx_.checkpoint();
        g->connect(link);
        edited();
    }
    update();
}

void NodeCanvas::mouseDoubleClickEvent(QMouseEvent* e) {
    if (!nodeAt(toGraph(e->position())) && graph()) openAddMenu(e->globalPosition().toPoint(), toGraph(e->position()), PinRef{});
}

void NodeCanvas::wheelEvent(QWheelEvent* e) {
    const QPointF before = toGraph(e->position());
    zoom_ = std::clamp(zoom_ * std::pow(1.12f, static_cast<float>(e->angleDelta().y()) / 120.0f), 0.25f, 2.5f);
    pan_ = e->position() - before * zoom_;
    update();
}

void NodeCanvas::keyPressEvent(QKeyEvent* e) {
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && selected_) removeSelected();
    else if (e->key() == Qt::Key_F) frameAll();
    else QWidget::keyPressEvent(e);
}

bool NodeCanvas::event(QEvent* e) {
    if (e->type() == QEvent::ShortcutOverride) {
        const int key = static_cast<QKeyEvent*>(e)->key();
        if (key == Qt::Key_Delete || key == Qt::Key_Backspace || key == Qt::Key_F) {
            e->accept();
            return true;
        }
    }
    return QWidget::event(e);
}

void NodeCanvas::removeSelected() {
    NodeGraph* g = graph();
    if (!g || !g->find(selected_)) return;
    ctx_.checkpoint();
    g->remove(selected_);
    selected_ = 0;
    edited();
}

void NodeCanvas::openAddMenu(QPoint global, QPointF at, PinRef from) {
    QMenu menu;
    // From a wire: only the nodes it can go into.
    const NodeDef* fromDef = nullptr;
    const PinDef* fromPin = nullptr;
    if (from) {
        if (const NodeGraph* g = graph(); const Node* n = g ? g->find(from.node) : nullptr) {
            fromDef = findNodeDef(n->type);
            if (fromDef) fromPin = from.output ? fromDef->output(from.pin) : fromDef->input(from.pin);
        }
    }
    auto fits = [&](const NodeDef& d) -> const PinDef* {
        if (!fromPin) return nullptr;
        for (const PinDef& p : from.output ? d.inputs : d.outputs) {
            const bool exec = p.type == PinType::Exec, fexec = fromPin->type == PinType::Exec;
            if (exec != fexec) continue;
            if (exec || p.type == fromPin->type || p.type == PinType::Any || fromPin->type == PinType::Any ||
                (from.output ? p.type == PinType::String : fromPin->type == PinType::String))
                return &p;
        }
        return nullptr;
    };
    for (const std::string& cat : nodeCategories()) {
        QMenu* sub = nullptr;
        for (const NodeDef& d : nodeLibrary()) {
            if (d.category != cat || (fromPin && !fits(d))) continue;
            if (!sub) sub = menu.addMenu(QString::fromStdString(cat));
            const std::string type = d.type;
            sub->addAction(QString::fromStdString(d.title), [this, type, at, from, fits] {
                NodeGraph* g = graph();
                const NodeDef* def = findNodeDef(type);
                if (!g || !def) return;
                ctx_.checkpoint();
                Node& n = g->add(type, static_cast<float>(at.x()), static_cast<float>(at.y() - kHeader / 2));
                selected_ = n.id;
                if (from) {
                    if (const PinDef* p = fits(*def)) {
                        const Link link = from.output ? Link{from.node, from.pin, n.id, p->name} : Link{n.id, p->name, from.node, from.pin};
                        g->connect(link);
                    }
                }
                edited();
            });
        }
    }
    if (menu.isEmpty()) menu.addAction(tr("No node fits this wire"))->setEnabled(false);
    menu.exec(global);
}

void NodeCanvas::editValue(NodeId nodeId, const std::string& pinName) {
    NodeGraph* g = graph();
    Node* n = g ? g->find(nodeId) : nullptr;
    const NodeDef* d = n ? findNodeDef(n->type) : nullptr;
    const PinDef* pin = d ? d->input(pinName) : nullptr;
    if (!pin) return;
    if (pin->type == PinType::Bool) { // a click flips it
        ctx_.checkpoint();
        n->values[pinName] = n->values[pinName] == "true" ? "false" : "true";
        edited();
        return;
    }
    int index = 0;
    for (size_t i = 0; i < d->inputs.size(); ++i)
        if (d->inputs[i].name == pinName) index = static_cast<int>(i);
    const QRectF vr = valueRect(*n, *d, index);
    delete editor_;
    editor_ = new QLineEdit(this);
    editor_->setText(QString::fromStdString(n->values[pinName]));
    editor_->setGeometry(QRectF(toScreen(vr.topLeft()), toScreen(vr.bottomRight())).toRect().adjusted(0, -2, 40, 2));
    editor_->selectAll();
    editor_->show();
    editor_->setFocus();
    QLineEdit* ed = editor_;
    connect(ed, &QLineEdit::editingFinished, this, [this, ed, nodeId, pinName] {
        if (editor_ != ed) return;
        editor_ = nullptr;
        ed->deleteLater();
        NodeGraph* gr = graph();
        Node* node = gr ? gr->find(nodeId) : nullptr;
        if (!node) return;
        const std::string v = ed->text().toStdString();
        if (node->values[pinName] == v) return;
        ctx_.checkpoint();
        node->values[pinName] = v;
        edited();
    });
}

NodesTab::NodesTab(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto* bar = new QWidget;
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(10, 4, 8, 4);
    title_ = label("", Tone::TextDim);
    row->addWidget(title_, 1);
    auto* showLua = new IconButton(Icon::Expression, tr("Show the Lua this graph becomes"), 26);
    showLua->setCheckable(true);
    auto* fit = new IconButton(Icon::FitView, tr("Frame all nodes (F)"), 26);
    row->addWidget(showLua);
    row->addWidget(fit);
    col->addWidget(bar);

    stack_ = new QStackedWidget;
    col->addWidget(stack_, 1);

    auto* none = new QWidget;
    auto* ncol = new QVBoxLayout(none);
    ncol->addStretch(1);
    empty_ = label("", Tone::TextDim);
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    ncol->addWidget(empty_);
    auto* create = accentButton(tr("Create Node Graph"));
    create->setFixedWidth(200);
    createButton_ = create;
    connect(create, &QPushButton::clicked, this, [this] {
        ctx_.checkpoint();
        NodeGraph g;
        g.add("event.start", 40, 40);
        g.add("event.update", 40, 200);
        ctx_.graph() = g;
        ctx_.changed();
        refresh();
        canvas_->frameAll();
    });
    ncol->addWidget(create, 0, Qt::AlignHCenter);
    ncol->addStretch(2);
    stack_->addWidget(none);

    auto* split = new QSplitter;
    canvas_ = new NodeCanvas(ctx_);
    lua_ = new QPlainTextEdit;
    lua_->setReadOnly(true);
    lua_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    lua_->setMinimumWidth(220);
    lua_->hide();
    split->addWidget(canvas_);
    split->addWidget(lua_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    stack_->addWidget(split);

    connect(showLua, &QAbstractButton::clicked, this, [this, showLua] {
        showLua->setActive(!showLua->active());
        lua_->setVisible(showLua->active());
        refresh();
    });
    connect(fit, &QAbstractButton::clicked, canvas_, &NodeCanvas::frameAll);
    connect(canvas_, &NodeCanvas::graphEdited, this, [this] { refresh(); });
    connect(&ctx_, &EditorContext::selectionChanged, this, [this] {
        refresh();
        canvas_->frameAll();
    });
    connect(&ctx_, &EditorContext::sceneChanged, this, [this] { refresh(); });
    connect(&ctx_, &EditorContext::scenesChanged, this, [this] {
        refresh();
        canvas_->frameAll();
    });
    refresh();
}

void NodesTab::refresh() {
    const QString owner = QString::fromStdString(ctx_.behaviorOwner());
    const bool scene = !ctx_.selected();
    title_->setText(scene ? tr("Nodes  ·  %1 (the scene)").arg(owner) : tr("Nodes  ·  %1").arg(owner));
    const std::optional<NodeGraph>& g = ctx_.graph();
    if (!g) {
        empty_->setText(scene ? tr("The scene \"%1\" has no node graph yet: its own rules (a score, a spawner) go there.\nOr select an object "
                                   "in the Hierarchy to give it behavior.")
                                    .arg(owner)
                              : tr("\"%1\" has no node graph yet.").arg(owner));
        stack_->setCurrentIndex(0);
        return;
    }
    stack_->setCurrentIndex(1);
    canvas_->update();
    if (lua_->isVisible()) {
        const CompiledGraph c = compileGraph(*g);
        QString text = QString::fromStdString(c.lua);
        for (const std::string& w : c.warnings) text += "\n-- warning: " + QString::fromStdString(w);
        if (lua_->toPlainText() != text) lua_->setPlainText(text);
    }
}

} // namespace artisan::editor
