#include "ui/NodeEditor.h"

#include "app/Settings.h"
#include "artisan/nodes/NodeCompiler.h"
#include "ui/EditorContext.h"
#include "ui/Library.h"
#include "ui/ScriptEditor.h"
#include "ui/Widgets.h"

#include <QDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
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

// A node's measures in graph units, at the size Settings › Interface chose.
struct Metrics {
    float w, header, row, pad, pinR, lineH;
};

Metrics metrics(const NodeDef& d) {
    const float s = settings().nodeScale;
    return {(d.hasCode ? 300.0f : 220.0f) * s, 36.0f * s, 26.0f * s, 10.0f * s, 5.5f * s, 15.0f * s};
}

// Rows: the execution pins share the first one; then the other outputs, then the other inputs
// (each value has the node's whole width: nothing overlaps).
bool sharesExecRow(const NodeDef& d) {
    return !d.inputs.empty() && d.inputs[0].type == PinType::Exec && !d.outputs.empty() && d.outputs[0].type == PinType::Exec;
}

int rowOf(const NodeDef& d, const std::string& pin, bool output) {
    const int share = sharesExecRow(d) ? 1 : 0;
    if (output) {
        for (size_t i = 0; i < d.outputs.size(); ++i)
            if (d.outputs[i].name == pin) return static_cast<int>(i);
        return 0;
    }
    for (size_t i = 0; i < d.inputs.size(); ++i)
        if (d.inputs[i].name == pin) return share && i == 0 ? 0 : static_cast<int>(d.outputs.size() + i) - share;
    return 0;
}

int rowCount(const NodeDef& d) { return std::max(1, static_cast<int>(d.inputs.size() + d.outputs.size()) - (sharesExecRow(d) ? 1 : 0)); }

int codeLines(const Node& n) {
    auto it = n.values.find("code");
    const int lines = it == n.values.end() ? 0 : static_cast<int>(std::count(it->second.begin(), it->second.end(), '\n')) + 1;
    return std::clamp(lines, 3, 10);
}

// Data pins by type; execution pins are drawn in the text color (they follow the theme).
Rgba pinColor(PinType t) {
    switch (t) {
    case PinType::Exec: return theme::kText;
    case PinType::Number: return rgba(92, 196, 110);
    case PinType::Bool: return rgba(239, 111, 94);
    case PinType::String: return rgba(233, 85, 154);
    case PinType::Any: return rgba(150, 160, 180);
    }
    return theme::kTextDim;
}

bool unnamed(const PinDef& p) { return p.type == PinType::Exec && (p.name == "in" || p.name == "then"); }

QFont codeFont() { return QFontDatabase::systemFont(QFontDatabase::FixedFont); }

// --- presets ---------------------------------------------------------------------------------

// On Update -> Move By, its dx and dy from two Input Axis nodes times a speed times dt.
NodeGraph movement(const char* left, const char* right, const char* up, const char* down, float speed) {
    NodeGraph g;
    const NodeId update = g.add("event.update", 0, 0).id;
    const NodeId move = g.add("action.move", 840, 0).id;
    g.connect({update, "then", move, "in"});
    for (int axis = 0; axis < 2; ++axis) {
        const float y = 150.0f + axis * 160.0f;
        const NodeId input = g.add("input.axis", 0, y).id;
        g.find(input)->values["negative"] = axis ? up : left;
        g.find(input)->values["positive"] = axis ? down : right;
        const NodeId fast = g.add("math.multiply", 280, y).id;
        g.find(fast)->values["b"] = std::to_string(static_cast<int>(speed));
        const NodeId step = g.add("math.multiply", 560, y).id;
        g.connect({input, "value", fast, "a"});
        g.connect({fast, "result", step, "a"});
        g.connect({update, "dt", step, "b"});
        g.connect({step, "result", move, axis ? "dy" : "dx"});
    }
    return g;
}

} // namespace

const std::vector<NodePreset>& nodePresets() {
    static const std::vector<NodePreset> presets = {
        {"wasd", QT_TR_NOOP("WASD Movement"), QT_TR_NOOP("W, A, S and D move the object, 300 pixels a second"), Icon::Keyboard,
         [] { return movement("a", "d", "w", "s", 300); }},
        {"arrows", QT_TR_NOOP("Arrow Keys Movement"), QT_TR_NOOP("The arrow keys move the object, 300 pixels a second"), Icon::ArrowRight,
         [] { return movement("left", "right", "up", "down", 300); }},
        {"follow", QT_TR_NOOP("Camera Follow"), QT_TR_NOOP("The camera glides after the object"), Icon::Camera,
         [] {
             NodeGraph g;
             const NodeId update = g.add("event.update", 0, 0).id;
             const NodeId follow = g.add("action.camera_follow", 300, 0).id;
             g.connect({update, "then", follow, "in"});
             return g;
         }},
        {"spin", QT_TR_NOOP("Spin"), QT_TR_NOOP("The object turns on itself, 120 degrees a second"), Icon::Reset,
         [] {
             NodeGraph g;
             const NodeId update = g.add("event.update", 0, 0).id;
             const NodeId rotate = g.add("action.rotate", 560, 0).id;
             const NodeId speed = g.add("math.multiply", 280, 140).id;
             g.find(speed)->values["b"] = "120";
             g.connect({update, "then", rotate, "in"});
             g.connect({update, "dt", speed, "a"});
             g.connect({speed, "result", rotate, "degrees"});
             return g;
         }},
        {"bob", QT_TR_NOOP("Float Up and Down"), QT_TR_NOOP("The object bobs gently, like something floating"), Icon::Wave,
         [] {
             NodeGraph g;
             const NodeId update = g.add("event.update", 0, 0).id;
             const NodeId now = g.add("time.now", 0, 150).id;
             const NodeId rate = g.add("math.multiply", 260, 150).id;
             g.find(rate)->values["b"] = "3";
             const NodeId sine = g.add("math.sin", 520, 150).id;
             const NodeId height = g.add("math.multiply", 780, 150).id;
             g.find(height)->values["b"] = "90";
             const NodeId step = g.add("math.multiply", 1040, 150).id;
             const NodeId move = g.add("action.move", 1300, 0).id;
             g.connect({update, "then", move, "in"});
             g.connect({now, "seconds", rate, "a"});
             g.connect({rate, "result", sine, "value"});
             g.connect({sine, "result", height, "a"});
             g.connect({height, "result", step, "a"});
             g.connect({update, "dt", step, "b"});
             g.connect({step, "result", move, "dy"});
             return g;
         }},
        {"lua", QT_TR_NOOP("Lua Code on Update"), QT_TR_NOOP("A Lua Code node run every frame: write any Lua in the graph"), Icon::Code,
         [] {
             NodeGraph g;
             const NodeId update = g.add("event.update", 0, 0).id;
             const NodeId code = g.add("script.lua", 300, 0).id;
             g.find(code)->values["code"] = "-- self, dt, input, camera... are all here\nself.rotation = self.rotation + 90 * dt";
             g.connect({update, "then", code, "in"});
             g.connect({update, "dt", code, "a"});
             return g;
         }},
    };
    return presets;
}

const NodePreset* findNodePreset(const std::string& id) {
    for (const NodePreset& p : nodePresets())
        if (id == p.id) return &p;
    return nullptr;
}

const std::vector<std::string>& nodeCategories() {
    static const std::vector<std::string> order = {"Events", "Actions", "Flow", "Object", "Input", "Time", "Math", "Logic", "Values", "Variables", "Collision", "Script"};
    return order;
}

Icon nodeCategoryIcon(const std::string& c) {
    static const std::map<std::string, Icon> icons = {{"Events", Icon::Play},     {"Actions", Icon::Pointer}, {"Flow", Icon::Transition},
                                                      {"Object", Icon::Cube},     {"Input", Icon::Keyboard},  {"Time", Icon::Speed},
                                                      {"Math", Icon::Expression}, {"Logic", Icon::Diamond},   {"Values", Icon::Counter},
                                                      {"Variables", Icon::Table}, {"Collision", Icon::Crop}, {"Script", Icon::Code}};
    auto it = icons.find(c);
    return it != icons.end() ? it->second : Icon::Graph;
}

Rgba nodeCategoryColor(const std::string& c) {
    static const std::map<std::string, Rgba> colors = {
        {"Events", rgba(239, 111, 94)},  {"Actions", rgba(80, 150, 250)},    {"Flow", rgba(150, 150, 165)}, {"Object", rgba(45, 196, 180)},
        {"Input", rgba(232, 185, 49)},   {"Time", rgba(242, 140, 40)},       {"Math", rgba(92, 196, 110)},  {"Logic", rgba(233, 85, 154)},
        {"Values", rgba(160, 110, 240)}, {"Variables", rgba(110, 168, 255)}, {"Collision", rgba(255, 158, 64)}, {"Script", rgba(32, 212, 176)}};
    auto it = colors.find(c);
    return it != colors.end() ? it->second : rgba(150, 150, 165);
}

NodeCanvas::NodeCanvas(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true); // nodes and presets from the library
    setMinimumSize(200, 200);
    connect(&Appearance::get(), &Appearance::changed, this, [this] { update(); });
    // Another graph: nothing of the last one stays selected.
    connect(&ctx_, &EditorContext::selectionChanged, this, [this] { selected_.clear(); });
}

NodeGraph* NodeCanvas::graph() {
    std::optional<NodeGraph>& g = ctx_.graph();
    return g ? &*g : nullptr;
}

void NodeCanvas::addNode(const std::string& type, std::optional<QPointF> screen) {
    NodeGraph* g = graph();
    const NodeDef* def = findNodeDef(type);
    if (!g || !def) return;
    const QPointF at = toGraph(screen.value_or(QPointF(width() / 2.0, height() / 2.0)));
    const Metrics m = metrics(*def);
    ctx_.checkpoint();
    Node& n = g->add(type, static_cast<float>(at.x() - m.w / 2), static_cast<float>(at.y() - m.header / 2));
    selected_ = {n.id};
    edited();
    setFocus();
}

void NodeCanvas::addPreset(const std::string& id, std::optional<QPointF> screen) {
    const NodePreset* preset = findNodePreset(id);
    if (!preset) return;
    // A preset moves, turns or follows an object (`self`): the scene's own graph has none. With
    // the scene selected, it comes with an object of its own, where the camera looks.
    bool created = false;
    if (!ctx_.selected()) {
        Entity e;
        e.name = "Player";
        e.layer = 1;
        e.shape = Shape{ShapeKind::Capsule, {56, 56}, Color::rgb(0x20D4B0), 0.0f, 0.0f, {}, 14.0f, false};
        e.hitbox = Hitbox{{56, 56}, {}, false};
        e.position = ctx_.scene().camera.position;
        ctx_.checkpoint();
        const EntityId added = ctx_.scene().add(std::move(e)).id;
        ctx_.changed();
        ctx_.select(added);
        ctx_.log(tr("\"%1\" moves an object: it went on a new one, %2").arg(tr(preset->name), QString::fromStdString(ctx_.selected()->name)));
        screen.reset(); // the view is the new object's now
        created = true;
    }
    if (!ctx_.graph()) {
        ctx_.checkpoint();
        ctx_.graph() = NodeGraph{};
        ctx_.changed();
    }
    if (!graph()) return;
    const NodeGraph part = preset->make();
    QPointF at = toGraph(screen.value_or(QPointF(width() / 2.0, height() / 2.0)));
    if (!screen) {
        // Clicked in the library: under what is there, never on top of it; then shown.
        QRectF existing, size;
        for (const Node& n : graph()->nodes)
            if (const NodeDef* d = findNodeDef(n.type)) existing = existing.isNull() ? nodeRect(n, *d) : existing.united(nodeRect(n, *d));
        for (const Node& n : part.nodes)
            if (const NodeDef* d = findNodeDef(n.type)) size = size.isNull() ? nodeRect(n, *d) : size.united(nodeRect(n, *d));
        if (!existing.isNull()) at = QPointF(existing.left() + size.width() / 2, existing.bottom() + 80 + size.height() / 2);
    }
    ctx_.checkpoint();
    insert(part, at);
    edited();
    if (created) frameAll(); // a new object: its whole graph
    else if (!screen) pan_ = QPointF(width() / 2.0, height() / 2.0) - at * zoom_;
    setFocus();
}

void NodeCanvas::insert(const NodeGraph& part, QPointF at) {
    NodeGraph* g = graph();
    if (!g || part.nodes.empty()) return;
    QRectF box;
    for (const Node& n : part.nodes)
        if (const NodeDef* d = findNodeDef(n.type)) box = box.isNull() ? nodeRect(n, *d) : box.united(nodeRect(n, *d));
    const QPointF shift = at - box.center();
    std::map<NodeId, NodeId> ids;
    selected_.clear();
    for (const Node& n : part.nodes) {
        Node& added = g->add(n.type, n.x + static_cast<float>(shift.x()), n.y + static_cast<float>(shift.y()));
        for (const auto& [k, v] : n.values) added.values[k] = v;
        ids[n.id] = added.id;
        selected_.insert(added.id);
    }
    for (const Link& l : part.links)
        if (ids.count(l.fromNode) && ids.count(l.toNode)) g->connect({ids[l.fromNode], l.fromPin, ids[l.toNode], l.toPin});
}

void NodeCanvas::selectAll() {
    if (NodeGraph* g = graph()) {
        selected_.clear();
        for (const Node& n : g->nodes) selected_.insert(n.id);
        update();
    }
}

void NodeCanvas::duplicateSelected() {
    NodeGraph* g = graph();
    if (!g || selected_.empty()) return;
    NodeGraph part;
    QRectF box;
    for (const Node& n : g->nodes)
        if (selected_.count(n.id)) {
            part.nodes.push_back(n);
            if (const NodeDef* d = findNodeDef(n.type)) box = box.isNull() ? nodeRect(n, *d) : box.united(nodeRect(n, *d));
        }
    for (const Link& l : g->links)
        if (selected_.count(l.fromNode) && selected_.count(l.toNode)) part.links.push_back(l);
    ctx_.checkpoint();
    insert(part, box.center() + QPointF(40, 40));
    edited();
}

void NodeCanvas::dragEnterEvent(QDragEnterEvent* e) {
    if (!graph()) return;
    const QMimeData* m = e->mimeData();
    if (!m->hasFormat(kNodeMime) && !m->hasFormat(kPresetMime)) return;
    dragType_ = m->hasFormat(kNodeMime) ? m->data(kNodeMime).toStdString() : std::string();
    dragPreset_ = m->hasFormat(kPresetMime) ? m->data(kPresetMime).toStdString() : std::string();
    dragAt_ = toGraph(e->position());
    e->acceptProposedAction();
    update();
}

void NodeCanvas::dragMoveEvent(QDragMoveEvent* e) {
    if (dragType_.empty() && dragPreset_.empty()) return;
    dragAt_ = toGraph(e->position());
    e->acceptProposedAction();
    update();
}

void NodeCanvas::dragLeaveEvent(QDragLeaveEvent*) {
    dragType_.clear();
    dragPreset_.clear();
    update();
}

void NodeCanvas::dropEvent(QDropEvent* e) {
    const std::string type = dragType_, preset = dragPreset_;
    dragType_.clear();
    dragPreset_.clear();
    if (!type.empty()) addNode(type, e->position());
    else if (!preset.empty()) addPreset(preset, e->position());
    else return;
    e->acceptProposedAction();
}

QRectF NodeCanvas::nodeRect(const Node& n, const NodeDef& def) const {
    const Metrics m = metrics(def);
    float h = m.header + rowCount(def) * m.row + m.pad;
    if (def.hasCode) h += codeLines(n) * m.lineH + m.pad * 2.2f;
    return {n.x, n.y, m.w, h};
}

QPointF NodeCanvas::pinPos(const Node& n, const NodeDef& def, const std::string& pin, bool output) const {
    const Metrics m = metrics(def);
    const float y = n.y + m.header + m.pad * 0.5f + rowOf(def, pin, output) * m.row + m.row * 0.5f;
    return {n.x + (output ? m.w : 0.0f), y};
}

QRectF NodeCanvas::valueRect(const Node& n, const NodeDef& def, const std::string& pin) const {
    const Metrics m = metrics(def);
    const float y = n.y + m.header + m.pad * 0.5f + rowOf(def, pin, false) * m.row;
    const float left = n.x + m.w * 0.42f;
    return {left, y + m.row * 0.16f, n.x + m.w - m.pad * 1.4f - left, m.row * 0.68f};
}

QRectF NodeCanvas::codeRect(const Node& n, const NodeDef& def) const {
    const Metrics m = metrics(def);
    const float top = n.y + m.header + m.pad * 0.5f + rowCount(def) * m.row + m.pad * 0.6f;
    return {n.x + m.pad, top, m.w - m.pad * 2, codeLines(n) * m.lineH + m.pad * 1.2f};
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
    for (auto it = g->nodes.rbegin(); it != g->nodes.rend(); ++it) {
        const Node& n = *it;
        const NodeDef* d = findNodeDef(n.type);
        if (!d) continue;
        const Metrics m = metrics(*d);
        const float reach = std::max(m.pinR * 2.0f, 10.0f / zoom_);
        for (bool out : {false, true})
            for (const PinDef& pin : out ? d->outputs : d->inputs) {
                const QPointF c = pinPos(n, *d, pin.name, out);
                // The pin itself, or its label beside it (not an input's value).
                const QRectF area = out ? QRectF(c.x() - m.w * 0.3f, c.y() - m.row / 2, m.w * 0.3f + reach, m.row)
                                        : QRectF(c.x() - reach, c.y() - m.row / 2, reach + m.w * 0.36f, m.row);
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
    zoom_ = std::clamp(static_cast<float>(std::min((width() - 80) / all.width(), (height() - 80) / all.height())), 0.3f, 1.25f);
    pan_ = QPointF(width() / 2.0, height() / 2.0) - all.center() * zoom_;
    update();
}

void NodeCanvas::edited() {
    ctx_.changed();
    emit graphEdited();
    update();
}

void NodeCanvas::drawNode(QPainter& p, const Node& n, const NodeDef& def, const NodeGraph* g, bool sel) const {
    const Metrics m = metrics(def);
    const float s = settings().nodeScale;
    const QRectF r = nodeRect(n, def);
    const Rgba cat = nodeCategoryColor(def.category);
    const float radius = (theme::current().rounding + 5.0f) * s;
    Draw d(p);
    // A raised card on a soft shadow; selected, a glow in the accent.
    d.rectFilled(Pt(r.left() + 2, r.top() + 5 * s), Pt(r.right() + 2, r.bottom() + 7 * s), theme::shade(55), radius + 2);
    if (sel) d.rect(Pt(r.left() - 3, r.top() - 3), Pt(r.right() + 3, r.bottom() + 3), theme::accent(70), radius + 3, 4.0f);
    d.rectFilled(Pt(r.topLeft()), Pt(r.bottomRight()), theme::kBgRaised, radius);
    QPainterPath card;
    card.addRoundedRect(r, radius, radius);
    p.save();
    p.setClipPath(card);
    // The header: the category's color, strong on the left, fading to the right.
    QLinearGradient tint(r.topLeft(), r.topRight());
    const bool light = theme::current().light;
    tint.setColorAt(0.0, theme::q(theme::withAlpha(cat, light ? 110 : 96)));
    tint.setColorAt(1.0, theme::q(theme::withAlpha(cat, light ? 34 : 22)));
    p.fillRect(QRectF(r.left(), r.top(), r.width(), m.header), tint);
    p.restore();
    d.line(Pt(r.left(), r.top() + m.header), Pt(r.right(), r.top() + m.header), theme::withAlpha(cat, 90), 1.0f);
    // Its icon on a small tile, the title, and for an event a mark that a chain starts here.
    const float chip = m.header * 0.62f;
    const Pt chipAt(r.left() + m.pad, r.top() + (m.header - chip) * 0.5f);
    d.rectFilled(chipAt, Pt(chipAt.x + chip, chipAt.y + chip), theme::withAlpha(theme::shade(255), light ? 30 : 70), 5.0f * s);
    drawIcon(d, nodeCategoryIcon(def.category), Pt(chipAt.x + chip * 0.5f, chipAt.y + chip * 0.5f), chip * 0.62f,
             theme::mix(cat, rgba(255, 255, 255), light ? 0.0f : 0.25f));
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * s);
    QFont bold = f;
    bold.setWeight(QFont::DemiBold);
    p.setFont(bold);
    p.setPen(theme::q(theme::kText));
    const float titleX = chipAt.x + chip + m.pad * 0.8f;
    const float kindW = def.kind == NodeDef::Kind::Event ? 46.0f * s : def.kind == NodeDef::Kind::Pure ? 18.0f * s : 0.0f;
    p.drawText(QRectF(titleX, r.top(), r.right() - titleX - m.pad - kindW, m.header), Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(QString::fromStdString(def.title), Qt::ElideRight, static_cast<int>(r.right() - titleX - m.pad - kindW)));
    p.setFont(f);
    if (def.kind == NodeDef::Kind::Event) {
        const QRectF pill(r.right() - m.pad - kindW, r.top() + m.header * 0.26f, kindW, m.header * 0.48f);
        d.rectFilled(Pt(pill.topLeft()), Pt(pill.bottomRight()), theme::withAlpha(cat, 60), static_cast<float>(pill.height()) * 0.5f);
        p.setPen(theme::q(theme::kText));
        QFont small = f;
        small.setPointSizeF(f.pointSizeF() * 0.8);
        p.setFont(small);
        p.drawText(pill, Qt::AlignCenter, QObject::tr("event"));
        p.setFont(f);
    } else if (def.kind == NodeDef::Kind::Pure) {
        drawIcon(d, Icon::Expression, Pt(r.right() - m.pad - kindW * 0.5f, r.top() + m.header * 0.5f), kindW * 0.8f, theme::kTextFaint);
    }
    d.rect(Pt(r.topLeft()), Pt(r.bottomRight()), sel ? theme::kAccent : theme::withAlpha(theme::kBorder, 255), radius, sel ? 2.0f : 1.0f);

    for (bool out : {false, true}) {
        const auto& pins = out ? def.outputs : def.inputs;
        for (const PinDef& pin : pins) {
            const QPointF c = pinPos(n, def, pin.name, out);
            const bool linked = g && (out ? !g->linksFrom(n.id, pin.name).empty() : g->linkInto(n.id, pin.name) != nullptr);
            const bool hot = hoverPin_ && hoverPin_ == PinRef{n.id, pin.name, out};
            const Rgba col = pinColor(pin.type);
            const float pr = m.pinR * (hot ? 1.3f : 1.0f);
            p.setPen(QPen(theme::q(col), 1.6 * s));
            p.setBrush(linked || hot ? theme::q(col) : theme::q(theme::kBgRaised));
            if (pin.type == PinType::Exec) { // an arrow: the order things happen
                const QPointF shape[] = {c + QPointF(-pr, -pr), c + QPointF(pr * 0.35, -pr), c + QPointF(pr * 1.3, 0), c + QPointF(pr * 0.35, pr),
                                         c + QPointF(-pr, pr)};
                p.drawPolygon(shape, 5);
            } else {
                p.drawEllipse(c, pr, pr);
            }
            if (!unnamed(pin)) {
                p.setPen(theme::q(theme::kTextDim));
                const QRectF lr = out ? QRectF(c.x() - m.w * 0.6f, c.y() - m.row / 2, m.w * 0.6f - m.pinR * 2.4f, m.row)
                                      : QRectF(c.x() + m.pinR * 2.4f, c.y() - m.row / 2, m.w * 0.42f - m.pinR * 2.8f, m.row);
                p.drawText(lr, Qt::AlignVCenter | (out ? Qt::AlignRight : Qt::AlignLeft),
                           p.fontMetrics().elidedText(QString::fromStdString(pin.name), Qt::ElideRight, static_cast<int>(lr.width())));
            }
            // A value typed in for an input with no wire: a field across the rest of the row.
            if (!out && pin.type != PinType::Exec && !linked) {
                const QRectF vr = valueRect(n, def, pin.name);
                const float fr = std::min(static_cast<float>(vr.height()) * 0.5f, theme::current().rounding + 1.0f);
                d.rectFilled(Pt(vr.topLeft()), Pt(vr.bottomRight()), theme::kField, fr);
                d.rect(Pt(vr.topLeft()), Pt(vr.bottomRight()), theme::kFieldBorder, fr, 1.0f);
                auto it = n.values.find(pin.name);
                QString text = QString::fromStdString(it != n.values.end() ? it->second : pin.defaultValue);
                if (pin.type == PinType::Bool) {
                    const bool on = text == "true";
                    const float bw = static_cast<float>(vr.height()) * 1.6f, bh = static_cast<float>(vr.height()) * 0.7f;
                    const Pt a(static_cast<float>(vr.left()) + 4, static_cast<float>(vr.center().y()) - bh * 0.5f);
                    d.rectFilled(a, Pt(a.x + bw, a.y + bh), on ? theme::kAccent : theme::kBorder, bh * 0.5f);
                    d.circleFilled(Pt(on ? a.x + bw - bh * 0.5f : a.x + bh * 0.5f, a.y + bh * 0.5f), bh * 0.38f, on ? theme::kOnAccent : theme::kTextDim);
                    text = on ? QObject::tr("true") : QObject::tr("false");
                    p.setPen(theme::q(theme::kText));
                    p.drawText(vr.adjusted(bw + 10, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
                } else {
                    if (pin.type == PinType::String) text = "\"" + text + "\"";
                    p.setPen(theme::q(theme::kText));
                    p.drawText(vr.adjusted(7 * s, 0, -5 * s, 0), Qt::AlignVCenter | Qt::AlignLeft,
                               p.fontMetrics().elidedText(text, Qt::ElideRight, static_cast<int>(vr.width() - 12 * s)));
                }
            }
        }
    }
    // A Lua Code node shows its code (double-click to write it).
    if (def.hasCode) {
        const QRectF cr = codeRect(n, def);
        d.rectFilled(Pt(cr.topLeft()), Pt(cr.bottomRight()), theme::kField, 6.0f * s);
        d.rect(Pt(cr.topLeft()), Pt(cr.bottomRight()), theme::kFieldBorder, 6.0f * s, 1.0f);
        auto it = n.values.find("code");
        const QString code = it != n.values.end() ? QString::fromStdString(it->second) : QString();
        QFont mono = codeFont();
        mono.setPointSizeF(f.pointSizeF() * 0.95);
        p.setFont(mono);
        if (code.trimmed().isEmpty()) {
            p.setPen(theme::q(theme::kTextFaint));
            p.drawText(cr.adjusted(8 * s, 6 * s, -8 * s, -6 * s), Qt::AlignLeft | Qt::AlignTop, QObject::tr("Double-click to write Lua\n(a, b in; return a result)"));
        } else {
            const QStringList lines = code.split('\n');
            for (int i = 0; i < std::min<int>(static_cast<int>(lines.size()), 10); ++i) {
                const QString line = i == 9 && lines.size() > 10 ? "…" : lines[i];
                p.setPen(theme::q(line.trimmed().startsWith("--") ? theme::kTextFaint : theme::kText));
                p.drawText(QRectF(cr.left() + 8 * s, cr.top() + 6 * s + i * m.lineH, cr.width() - 16 * s, m.lineH), Qt::AlignVCenter | Qt::AlignLeft,
                           p.fontMetrics().elidedText(line, Qt::ElideRight, static_cast<int>(cr.width() - 16 * s)));
            }
        }
        p.setFont(f);
    }
}

void NodeCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), theme::q(theme::kBgDeep));
    // Dots every 32 units, stronger every 4th.
    const float step = 32.0f * zoom_;
    if (settings().nodeGrid && step > 6) {
        const int firstX = static_cast<int>(std::ceil(-pan_.x() / step)), firstY = static_cast<int>(std::ceil(-pan_.y() / step));
        for (int ix = firstX; ix * step + pan_.x() < width(); ++ix)
            for (int iy = firstY; iy * step + pan_.y() < height(); ++iy) {
                const bool major = ix % 4 == 0 && iy % 4 == 0;
                p.setPen(QPen(theme::q(theme::ink(major ? 50 : 22)), major ? 2.5f : 1.5f, Qt::SolidLine, Qt::RoundCap));
                p.drawPoint(QPointF(ix * step + pan_.x(), iy * step + pan_.y()));
            }
    }
    NodeGraph* g = graph();
    if (!g) return;
    if (g->nodes.empty() && dragType_.empty() && dragPreset_.empty()) {
        p.setPen(theme::q(theme::kTextFaint));
        p.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap,
                   tr("Drag a node or a preset from the library, or right-click to add one"));
    }
    p.save();
    p.translate(pan_);
    p.scale(zoom_, zoom_);
    const float s = settings().nodeScale;

    auto wire = [&](QPointF a, QPointF b, Rgba c, float width) {
        const float dx = std::max(50.0f * s, static_cast<float>(std::fabs(b.x() - a.x())) * 0.5f);
        QPainterPath path(a);
        path.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(theme::q(theme::shade(70)), width + 2.5f, Qt::SolidLine, Qt::RoundCap)); // reads on any background
        p.drawPath(path);
        p.setPen(QPen(theme::q(c), width, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(path);
    };
    for (const Link& l : g->links) {
        const Node* a = g->find(l.fromNode);
        const Node* b = g->find(l.toNode);
        const NodeDef* ad = a ? findNodeDef(a->type) : nullptr;
        const NodeDef* bd = b ? findNodeDef(b->type) : nullptr;
        if (!ad || !bd || !ad->output(l.fromPin)) continue;
        const PinType t = ad->output(l.fromPin)->type;
        // The selected nodes' wires stand out.
        const bool lit = selected_.count(a->id) || selected_.count(b->id);
        const Rgba c = t == PinType::Exec ? (lit ? theme::kAccent : theme::withAlpha(theme::kText, 190)) : theme::withAlpha(pinColor(t), lit ? 255 : 190);
        wire(pinPos(*a, *ad, l.fromPin, true), pinPos(*b, *bd, l.toPin, false), c, (t == PinType::Exec ? 3.0f : 2.2f) * s);
    }
    if (drag_ == Drag::Wire && wireFrom_) {
        const Node* n = g->find(wireFrom_.node);
        const NodeDef* d = n ? findNodeDef(n->type) : nullptr;
        if (d) {
            const PinDef* pin = wireFrom_.output ? d->output(wireFrom_.pin) : d->input(wireFrom_.pin);
            const QPointF a = pinPos(*n, *d, wireFrom_.pin, wireFrom_.output), b = toGraph(mouse_);
            wire(wireFrom_.output ? a : b, wireFrom_.output ? b : a, pin ? pinColor(pin->type) : theme::kText, 2.4f * s);
        }
    }
    for (const Node& n : g->nodes)
        if (const NodeDef* def = findNodeDef(n.type)) drawNode(p, n, *def, g, selected_.count(n.id) > 0);

    // What is being dragged in from the library, where it would land.
    if (!dragType_.empty() || !dragPreset_.empty()) {
        NodeGraph ghost;
        if (const NodeDef* def = findNodeDef(dragType_)) {
            const Metrics m = metrics(*def);
            ghost.add(dragType_, static_cast<float>(dragAt_.x() - m.w / 2), static_cast<float>(dragAt_.y() - m.header / 2));
        } else if (const NodePreset* preset = findNodePreset(dragPreset_)) {
            ghost = preset->make();
            QRectF box;
            for (const Node& n : ghost.nodes)
                if (const NodeDef* d = findNodeDef(n.type)) box = box.isNull() ? nodeRect(n, *d) : box.united(nodeRect(n, *d));
            const QPointF shift = dragAt_ - box.center();
            for (Node& n : ghost.nodes) n.x += static_cast<float>(shift.x()), n.y += static_cast<float>(shift.y());
        }
        p.setOpacity(0.6);
        for (const Link& l : ghost.links) {
            const Node* a = ghost.find(l.fromNode);
            const Node* b = ghost.find(l.toNode);
            const NodeDef* ad = a ? findNodeDef(a->type) : nullptr;
            const NodeDef* bd = b ? findNodeDef(b->type) : nullptr;
            if (ad && bd && ad->output(l.fromPin))
                wire(pinPos(*a, *ad, l.fromPin, true), pinPos(*b, *bd, l.toPin, false), pinColor(ad->output(l.fromPin)->type), 2.2f * s);
        }
        for (const Node& n : ghost.nodes)
            if (const NodeDef* def = findNodeDef(n.type)) drawNode(p, n, *def, &ghost, true);
        p.setOpacity(1.0);
    }
    p.restore();

    if (drag_ == Drag::Select) { // the box being drawn
        const QRectF box = QRectF(press_, mouse_).normalized();
        Draw d(p);
        d.rectFilled(Pt(box.topLeft()), Pt(box.bottomRight()), theme::accent(28), 3.0f);
        d.rect(Pt(box.topLeft()), Pt(box.bottomRight()), theme::accent(200), 3.0f, 1.0f);
    }
}

void NodeCanvas::mousePressEvent(QMouseEvent* e) {
    setFocus();
    last_ = mouse_ = press_ = e->position();
    if (editor_) editor_->clearFocus();
    NodeGraph* g = graph();
    if (!g) return;
    const QPointF gp = toGraph(e->position());
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && space_)) {
        drag_ = Drag::Pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() == Qt::RightButton) {
        if (const NodeId id = nodeAt(gp)) {
            if (!selected_.count(id)) selected_ = {id};
            update();
            QMenu menu;
            const Node* n = g->find(id);
            const NodeDef* def = n ? findNodeDef(n->type) : nullptr;
            if (def && def->hasCode) menu.addAction(menuIcon(Icon::Code, theme::kTextDim), tr("Edit Lua Code..."), [this, id] { editCode(id); });
            menu.addAction(menuIcon(Icon::Duplicate, theme::kTextDim), selected_.size() > 1 ? tr("Duplicate %1 Nodes").arg(selected_.size()) : tr("Duplicate"),
                           [this] { duplicateSelected(); });
            menu.addSeparator();
            menu.addAction(menuIcon(Icon::Trash, theme::kDanger), selected_.size() > 1 ? tr("Delete %1 Nodes").arg(selected_.size()) : tr("Delete"),
                           [this] { removeSelected(); });
            menu.exec(e->globalPosition().toPoint());
        } else {
            drag_ = Drag::Pan; // a right drag pans; a click without moving opens the menu (on release)
            setCursor(Qt::ClosedHandCursor);
        }
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    const bool adding = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
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
    if (const NodeId id = nodeAt(gp)) {
        if (adding) { // Shift or Ctrl: in or out of the selection
            if (!selected_.erase(id)) selected_.insert(id);
            update();
            return;
        }
        if (!selected_.count(id)) selected_ = {id};
        const Node* n = g->find(id);
        const NodeDef* d = n ? findNodeDef(n->type) : nullptr;
        // A click on a value edits it.
        for (size_t i = 0; d && i < d->inputs.size(); ++i) {
            const PinDef& pin = d->inputs[i];
            if (pin.type == PinType::Exec || g->linkInto(n->id, pin.name)) continue;
            if (valueRect(*n, *d, pin.name).contains(gp)) {
                editValue(n->id, pin.name);
                update();
                return;
            }
        }
        drag_ = Drag::Move;
        ++moves_;
        // The node goes on top (drawn last).
        auto it = std::find_if(g->nodes.begin(), g->nodes.end(), [&](const Node& x) { return x.id == id; });
        if (it != g->nodes.end()) std::rotate(it, it + 1, g->nodes.end());
    } else {
        // Empty space: a box selects what it touches.
        drag_ = Drag::Select;
        before_ = adding ? selected_ : std::set<NodeId>{};
        selected_ = before_;
    }
    update();
}

void NodeCanvas::mouseMoveEvent(QMouseEvent* e) {
    const QPointF d = e->position() - last_;
    last_ = mouse_ = e->position();
    switch (drag_) {
    case Drag::Pan: pan_ += d; break;
    case Drag::Move:
        if (NodeGraph* g = graph()) {
            ctx_.checkpoint("node move " + std::to_string(moves_));
            for (Node& n : g->nodes)
                if (selected_.count(n.id)) {
                    n.x += static_cast<float>(d.x() / zoom_);
                    n.y += static_cast<float>(d.y() / zoom_);
                }
            ctx_.changed();
        }
        break;
    case Drag::Select:
        if (NodeGraph* g = graph()) {
            const QRectF box(toGraph(press_), toGraph(mouse_));
            selected_ = before_;
            for (const Node& n : g->nodes)
                if (const NodeDef* def = findNodeDef(n.type); def && nodeRect(n, *def).intersects(box.normalized())) selected_.insert(n.id);
        }
        break;
    case Drag::None: {
        const PinRef hover = pinAt(toGraph(e->position()));
        if (hover == hoverPin_) return;
        hoverPin_ = hover;
        break;
    }
    default: break;
    }
    update();
}

void NodeCanvas::leaveEvent(QEvent*) {
    if (!hoverPin_) return;
    hoverPin_ = {};
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
    if (was == Drag::Select) return update();
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
    NodeGraph* g = graph();
    if (!g) return;
    const QPointF gp = toGraph(e->position());
    if (const NodeId id = nodeAt(gp)) {
        const Node* n = g->find(id);
        if (const NodeDef* d = n ? findNodeDef(n->type) : nullptr; d && d->hasCode) editCode(id);
        return;
    }
    openAddMenu(e->globalPosition().toPoint(), gp, PinRef{});
}

void NodeCanvas::wheelEvent(QWheelEvent* e) {
    const QPointF before = toGraph(e->position());
    zoom_ = std::clamp(zoom_ * std::pow(1.12f, static_cast<float>(e->angleDelta().y()) / 120.0f), 0.25f, 2.5f);
    pan_ = e->position() - before * zoom_;
    update();
}

void NodeCanvas::keyPressEvent(QKeyEvent* e) {
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && !selected_.empty()) removeSelected();
    else if (e->key() == Qt::Key_F) frameAll();
    else if (e->key() == Qt::Key_Escape && !selected_.empty()) {
        selected_.clear();
        update();
    } else if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        space_ = true;
        setCursor(Qt::OpenHandCursor);
    } else QWidget::keyPressEvent(e);
}

void NodeCanvas::keyReleaseEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        space_ = false;
        if (drag_ != Drag::Pan) unsetCursor();
    }
    QWidget::keyReleaseEvent(e);
}

bool NodeCanvas::event(QEvent* e) {
    if (e->type() == QEvent::ShortcutOverride) {
        const auto* k = static_cast<QKeyEvent*>(e);
        const int key = k->key();
        if (key == Qt::Key_Delete || key == Qt::Key_Backspace || key == Qt::Key_F || key == Qt::Key_Space ||
            (key == Qt::Key_Escape && !selected_.empty())) {
            if (k->modifiers() == Qt::NoModifier) {
                e->accept();
                return true;
            }
        }
    }
    return QWidget::event(e);
}

void NodeCanvas::removeSelected() {
    NodeGraph* g = graph();
    if (!g || selected_.empty()) return;
    ctx_.checkpoint();
    for (NodeId id : selected_) g->remove(id);
    selected_.clear();
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
            if (!sub) sub = menu.addMenu(menuIcon(nodeCategoryIcon(cat), nodeCategoryColor(cat)), tr(cat.c_str()));
            const std::string type = d.type;
            sub->addAction(QString::fromStdString(d.title), [this, type, at, from, fits] {
                NodeGraph* g = graph();
                const NodeDef* def = findNodeDef(type);
                if (!g || !def) return;
                ctx_.checkpoint();
                Node& n = g->add(type, static_cast<float>(at.x()), static_cast<float>(at.y() - metrics(*def).header / 2));
                selected_ = {n.id};
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
    if (!from) {
        menu.addSeparator();
        QMenu* presets = menu.addMenu(menuIcon(Icon::Layers, theme::kAccent), tr("Presets"));
        for (const NodePreset& preset : nodePresets()) {
            const std::string id = preset.id;
            presets->addAction(menuIcon(preset.icon, theme::kTextDim), tr(preset.name), [this, id, at] {
                ctx_.checkpoint();
                insert(findNodePreset(id)->make(), at);
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
    const QRectF vr = valueRect(*n, *d, pinName);
    delete editor_;
    editor_ = new QLineEdit(this);
    editor_->setText(QString::fromStdString(n->values[pinName]));
    editor_->setGeometry(QRectF(toScreen(vr.topLeft()), toScreen(vr.bottomRight())).toRect().adjusted(-2, -3, 30, 3));
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

void NodeCanvas::editCode(NodeId nodeId) {
    NodeGraph* g = graph();
    Node* n = g ? g->find(nodeId) : nullptr;
    if (!n) return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Lua Code"));
    dialog.resize(620, 440);
    auto* col = new QVBoxLayout(&dialog);
    col->setContentsMargins(18, 14, 18, 16);
    col->setSpacing(8);
    auto* hint = label(tr("Runs when the wire reaches it. a and b are its inputs; self, dt, input, camera, vars and the whole scripting API "
                          "are in reach. What it returns comes out of result."),
                       Tone::TextDim);
    hint->setWordWrap(true);
    col->addWidget(hint);
    auto* text = new QPlainTextEdit;
    QFont mono = codeFont();
    mono.setPointSizeF(mono.pointSizeF() * 1.1);
    text->setFont(mono);
    text->setTabStopDistance(QFontMetricsF(mono).horizontalAdvance(' ') * 4);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    highlightLua(text->document());
    text->setPlainText(QString::fromStdString(n->values["code"]));
    col->addWidget(text, 1);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* cancel = new QPushButton(tr("Cancel"));
    auto* ok = accentButton(tr("Done"));
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    col->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    text->setFocus();
    if (dialog.exec() != QDialog::Accepted) return;
    g = graph();
    n = g ? g->find(nodeId) : nullptr;
    const std::string code = text->toPlainText().toStdString();
    if (!n || n->values["code"] == code) return;
    ctx_.checkpoint();
    n->values["code"] = code;
    edited();
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
    showLua->setActive(settings().showLua);
    showLua_ = showLua;
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
    highlightLua(lua_->document());
    lua_->setVisible(settings().showLua);
    split->addWidget(canvas_);
    split->addWidget(lua_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    stack_->addWidget(split);

    connect(showLua, &QAbstractButton::clicked, this, [this, showLua] {
        settings().showLua = !settings().showLua;
        saveSettings();
        showLua->setActive(settings().showLua);
        lua_->setVisible(settings().showLua);
        refresh();
    });
    // Settings › Interface turns it on or off too.
    connect(&Appearance::get(), &Appearance::changed, this, [this, showLua] {
        showLua->setActive(settings().showLua);
        lua_->setVisible(settings().showLua);
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
                                   "in the Hierarchy to give it behavior: each object has its own graph.")
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
