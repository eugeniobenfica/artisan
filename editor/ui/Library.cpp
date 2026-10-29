#include "ui/Library.h"

#include "app/Platform.h"
#include "app/Plugins.h"
#include "app/Settings.h"
#include "artisan/nodes/NodeLibrary.h"
#include "ui/EditorContext.h"
#include "ui/NodeEditor.h"
#include "ui/Widgets.h"

#include <QFileDialog>
#include <QImageReader>
#include <QLineEdit>
#include <QMimeData>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace artisan::editor {

using namespace ui;

namespace {

constexpr int kTileWidth = 84;

// A category of tiles: as many columns as fit; open or closed, remembered (by `key`).
struct Section {
    ResponsiveGrid* grid = nullptr;
    CategoryHeader* header = nullptr;
    int count = 0;
    void add(QWidget* w) {
        grid->addWidget(w);
        header->setCount(++count);
    }
};

Icon iconOfKind(const std::string& kind) {
    if (const PluginObject* o = PluginHost::get().findObject(kind)) return pluginIcon(o->icon);
    if (kind == "player") return Icon::Person;
    if (kind == "collectible") return Icon::Star;
    if (kind == "circle" || kind == "capsule") return Icon::Ellipse;
    if (kind == "sprite") return Icon::Image;
    if (kind == "light") return Icon::Bulb;
    if (kind == "empty") return Icon::Cube;
    return Icon::Rectangle;
}

Icon iconOfCategory(const std::string& category) {
    if (category == "Characters") return Icon::Person;
    if (category == "Shapes") return Icon::Polygon;
    if (category == "Media") return Icon::Image;
    if (category == "Lights") return Icon::Bulb;
    if (category == "Other") return Icon::Cube;
    return Icon::Plugin; // a plugin's category
}

// The player: a lit capsule carrying its own light, moved by the arrow keys (a node graph:
// axis × speed × dt), the camera following it.
NodeGraph playerGraph() {
    NodeGraph g;
    const NodeId update = g.add("event.update", 0, 0).id;
    const NodeId move = g.add("action.move", 840, 0).id;
    const NodeId follow = g.add("action.camera_follow", 1120, 0).id;
    g.connect({update, "then", move, "in"});
    g.connect({move, "then", follow, "in"});
    for (int axis = 0; axis < 2; ++axis) {
        const float y = 150.0f + axis * 160.0f;
        const NodeId input = g.add("input.axis", 0, y).id;
        g.find(input)->values["negative"] = axis ? "up" : "left";
        g.find(input)->values["positive"] = axis ? "down" : "right";
        const NodeId speed = g.add("math.multiply", 280, y).id;
        g.find(speed)->values["b"] = "320";
        const NodeId step = g.add("math.multiply", 560, y).id;
        g.connect({input, "value", speed, "a"});
        g.connect({speed, "result", step, "a"});
        g.connect({update, "dt", step, "b"});
        g.connect({step, "result", move, axis ? "dy" : "dx"});
    }
    return g;
}

// A collectible turns on itself: Rotate By (dt × 120).
NodeGraph spinGraph() {
    NodeGraph g;
    const NodeId update = g.add("event.update", 0, 60).id;
    const NodeId rotate = g.add("action.rotate", 300, 60).id;
    const NodeId speed = g.add("math.multiply", 40, 220).id;
    g.find(speed)->values["b"] = "120";
    g.connect({update, "then", rotate, "in"});
    g.connect({update, "dt", speed, "a"});
    g.connect({speed, "result", rotate, "degrees"});
    return g;
}

} // namespace

const std::vector<ObjectKind>& objectKinds() {
    static const std::vector<ObjectKind> kinds = {
        {"player", QT_TR_NOOP("Player"), "Characters", QT_TR_NOOP("A lit character that moves with the arrow keys, the camera following it (a node graph), with a hitbox")},
        {"collectible", QT_TR_NOOP("Collectible"), "Characters", QT_TR_NOOP("A glowing gem that turns on itself (a node graph)")},
        {"platform", QT_TR_NOOP("Platform"), "Characters", QT_TR_NOOP("A long block to stand on: its hitbox is solid")},
        {"rectangle", QT_TR_NOOP("Rectangle"), "Shapes", QT_TR_NOOP("A rectangle: sharp at any zoom, with outline and glow")},
        {"rounded", QT_TR_NOOP("Rounded"), "Shapes", QT_TR_NOOP("A rectangle with rounded corners")},
        {"circle", QT_TR_NOOP("Circle"), "Shapes", QT_TR_NOOP("A circle")},
        {"capsule", QT_TR_NOOP("Capsule"), "Shapes", QT_TR_NOOP("A pill: a rectangle with round ends")},
        {"sprite", QT_TR_NOOP("Sprite"), "Media", QT_TR_NOOP("A picture (PNG, JPEG...): it is copied into the project's assets")},
        {"light", QT_TR_NOOP("Light"), "Lights", QT_TR_NOOP("A point light: lit objects around it take its color")},
        {"empty", QT_TR_NOOP("Empty"), "Other", QT_TR_NOOP("An object with nothing to draw: a place, or a holder for a script")},
    };
    return kinds;
}

Icon objectKindIcon(const std::string& kind) { return iconOfKind(kind); }

bool makeObject(const std::string& kind, EditorContext& ctx, QWidget* parent, Entity& e) {
    if (const PluginObject* o = PluginHost::get().findObject(kind)) {
        std::string error;
        if (PluginHost::get().makeObject(*o, e, &error)) return true;
        ctx.log(QObject::tr("error: %1 (%2)").arg(QString::fromStdString(error), QString::fromStdString(o->name)));
        return false;
    }
    if (kind == "sprite") {
        const QString file = QFileDialog::getOpenFileName(parent, QObject::tr("Choose an Image"), platform::toQt(ctx.dir / "assets"),
                                                          QObject::tr("Images (*.png *.jpg *.jpeg *.bmp *.tga)"));
        if (file.isEmpty()) return false;
        // Images live in the project's assets/, so the game finds them wherever it goes.
        const auto src = platform::fromQt(file);
        std::error_code ec;
        auto rel = std::filesystem::relative(src, ctx.dir, ec);
        if (ec || rel.empty() || platform::utf8(rel).starts_with("..")) {
            rel = std::filesystem::path("assets") / src.filename();
            std::filesystem::create_directories(ctx.dir / "assets", ec);
            std::filesystem::copy_file(src, ctx.dir / rel, std::filesystem::copy_options::overwrite_existing, ec);
        }
        Sprite s;
        s.image = platform::utf8(rel);
        if (const QSize size = QImageReader(file).size(); size.isValid()) s.size = {static_cast<float>(size.width()), static_cast<float>(size.height())};
        e.sprite = s;
        e.name = platform::utf8(src.stem());
    } else if (kind == "player") {
        e.name = "Player";
        e.layer = 1;
        e.shape = Shape{ShapeKind::Capsule, {56, 56}, Color::rgb(0xE8B931), 0.0f, 0.0f, {}, 14.0f, false};
        e.light = Light{Color::rgb(0xFFD58A), 380.0f, 1.3f};
        e.hitbox = Hitbox{{56, 56}, {}, false};
        e.graph = playerGraph();
    } else if (kind == "collectible") {
        e.name = "Collectible";
        e.rotation = 45.0f;
        e.shape = Shape{ShapeKind::RoundedRectangle, {34, 34}, Color::rgb(0x5FE0D0), 6.0f, 0.0f, {}, 18.0f, false};
        e.graph = spinGraph();
        e.hitbox = Hitbox{{34, 34}, {}, false};
    } else if (kind == "platform") {
        e.name = "Platform";
        e.shape = Shape{ShapeKind::RoundedRectangle, {360, 36}, Color::rgb(0x4A4F63), 6.0f, 0.0f, {}, 0.0f, true};
        e.hitbox = Hitbox{{360, 36}, {}, true}; // something to stand on
    } else if (kind == "light") {
        e.light = Light{};
        e.name = "Light";
    } else if (kind == "empty") {
        e.name = "Object";
    } else {
        Shape s;
        if (kind == "rounded") s.kind = ShapeKind::RoundedRectangle, e.name = "Rounded Rectangle";
        else if (kind == "circle") s.kind = ShapeKind::Circle, e.name = "Circle";
        else if (kind == "capsule") s.kind = ShapeKind::Capsule, s.size = {96, 48}, e.name = "Capsule";
        else e.name = "Rectangle";
        e.shape = s;
    }
    return true;
}

EntityId addObject(const std::string& kind, EditorContext& ctx, QWidget* parent, const Vec2* at) {
    Entity e;
    if (!makeObject(kind, ctx, parent, e)) return 0;
    // A new object goes where it was dropped, or where the game's camera looks.
    e.position = at ? *at : ctx.scene().camera.position;
    ctx.checkpoint();
    const EntityId id = ctx.scene().add(std::move(e)).id;
    ctx.changed();
    ctx.select(id);
    return id;
}

// --- the panel ---------------------------------------------------------------------------------

LibraryPanel::LibraryPanel(EditorContext& ctx, QWidget* parent) : QScrollArea(parent), ctx_(ctx) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rebuild();
    connect(&PluginHost::get(), &PluginHost::changed, this, [this] { rebuild(); });
}

void LibraryPanel::setMode(Mode mode) {
    if (mode == mode_) return;
    mode_ = mode;
    filter_.clear();
    rebuild();
    emit titleChanged(mode_ == Mode::Nodes ? tr("Nodes") : tr("Objects"));
}

void LibraryPanel::rebuild() {
    auto* content = new QWidget;
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(6);

    if (mode_ == Mode::Scripts) { // nothing to add to a script: the panel rests, saying why
        col->addStretch(1);
        QLabel* note = label(tr("Scripts belong to the whole project: pick one from the list beside the editor.\n\nObjects and nodes "
                                "come back here in the Scene and Nodes views."),
                             Tone::TextFaint);
        note->setWordWrap(true);
        note->setAlignment(Qt::AlignCenter);
        col->addWidget(note);
        col->addStretch(2);
        setWidget(content);
        return;
    }

    auto* search = new QLineEdit(filter_);
    search->setPlaceholderText(mode_ == Mode::Nodes ? tr("Search nodes") : tr("Search objects"));
    search->setClearButtonEnabled(true);
    col->addWidget(search);
    connect(search, &QLineEdit::textChanged, this, [this](const QString& text) {
        filter_ = text.trimmed();
        // Rebuilt a moment later, keeping the keyboard in the field.
        QTimer::singleShot(0, this, [this] {
            rebuild();
            if (auto* s = widget()->findChild<QLineEdit*>()) {
                s->setFocus();
                s->setCursorPosition(static_cast<int>(s->text().size()));
            }
        });
    });

    const QString prefix = mode_ == Mode::Nodes ? "nodes:" : "objects:";
    std::vector<std::pair<std::string, Section>> sections;
    auto section = [&](const std::string& name, Icon icon) -> Section& {
        for (auto& [n, s] : sections)
            if (n == name) return s;
        const std::string key = (prefix + QString::fromStdString(name)).toStdString();
        const auto& shut = settings().closedCategories;
        const bool closed = filter_.isEmpty() && std::find(shut.begin(), shut.end(), key) != shut.end();
        auto* header = new CategoryHeader(icon, tr(name.c_str()), 0, closed);
        header->setToolTip(tr("Click to open or close it"));
        header->toggled = [this, key] {
            std::vector<std::string>& s = settings().closedCategories;
            if (std::find(s.begin(), s.end(), key) != s.end()) std::erase(s, key);
            else s.push_back(key);
            saveSettings();
            QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
        };
        col->addWidget(header);
        auto* grid = new ResponsiveGrid(kTileWidth, 8);
        if (closed) {
            grid->setParent(content); // kept (its tiles are made anyway), never shown
            grid->hide();
        } else {
            col->addWidget(new CategoryBody(grid));
        }
        sections.push_back({name, Section{grid, header}});
        return sections.back().second;
    };
    auto matches = [this](const QString& name) { return filter_.isEmpty() || name.contains(filter_, Qt::CaseInsensitive); };

    if (mode_ == Mode::Objects) {
        for (const ObjectKind& k : objectKinds()) {
            if (!matches(tr(k.name))) continue;
            auto* t = new Tile(iconOfKind(k.kind), tr(k.name));
            t->setTileSize({kTileWidth, 62});
            t->setToolTip(tr(k.tip) + "\n" + tr("Click to add it where the camera looks, or drag it into the scene"));
            const std::string kind = k.kind;
            t->setDragData([kind] {
                auto* m = new QMimeData;
                m->setData(kObjectMime, QByteArray::fromStdString(kind));
                return m;
            });
            connect(t, &QAbstractButton::clicked, this, [this, kind] { addObject(kind, ctx_, this); });
            section(k.category, iconOfCategory(k.category)).add(t);
        }
        // What plugins add, in their own categories.
        for (const PluginObject* o : PluginHost::get().objects()) {
            if (!matches(QString::fromStdString(o->name))) continue;
            auto* t = new Tile(pluginIcon(o->icon), QString::fromStdString(o->name));
            t->setTileSize({kTileWidth, 62});
            const Plugin* p = PluginHost::get().find(o->pluginId);
            t->setToolTip((o->tip.empty() ? QString() : QString::fromStdString(o->tip) + "\n") +
                          tr("From the %1 plugin. Click to add it, or drag it into the scene").arg(QString::fromStdString(p ? p->name : o->pluginId)));
            const std::string kind = PluginHost::objectKey(*o);
            t->setDragData([kind] {
                auto* m = new QMimeData;
                m->setData(kObjectMime, QByteArray::fromStdString(kind));
                return m;
            });
            connect(t, &QAbstractButton::clicked, this, [this, kind] { addObject(kind, ctx_, this); });
            section(o->category, Icon::Plugin).add(t);
        }
    } else {
        // Presets first: pieces of graph already wired.
        for (const NodePreset& preset : nodePresets()) {
            if (!matches(tr(preset.name))) continue;
            auto* t = new Tile(preset.icon, tr(preset.name));
            t->setTileSize({kTileWidth, 62});
            t->setToolTip(tr(preset.tip) + "\n" + tr("Click to add it in the middle of the graph, or drag it where it goes"));
            const std::string id = preset.id;
            t->setDragData([id] {
                auto* m = new QMimeData;
                m->setData(kPresetMime, QByteArray::fromStdString(id));
                return m;
            });
            connect(t, &QAbstractButton::clicked, this, [this, id] {
                if (onAddPreset) onAddPreset(id);
            });
            section("Presets", Icon::Layers).add(t);
        }
        for (const std::string& cat : nodeCategories())
            for (const NodeDef& d : nodeLibrary()) {
                if (d.category != cat || !matches(QString::fromStdString(d.title))) continue;
                auto* t = new Tile(nodeCategoryIcon(cat), QString::fromStdString(d.title));
                t->setTileSize({kTileWidth, 62});
                t->setToolTip(tr("%1 (%2)\nClick to add it in the middle of the graph, or drag it where it goes")
                                  .arg(QString::fromStdString(d.title), QString::fromStdString(d.category)));
                const std::string type = d.type;
                t->setDragData([type] {
                    auto* m = new QMimeData;
                    m->setData(kNodeMime, QByteArray::fromStdString(type));
                    return m;
                });
                connect(t, &QAbstractButton::clicked, this, [this, type] {
                    if (onAddNode) onAddNode(type);
                });
                section(cat, nodeCategoryIcon(cat)).add(t);
            }
    }
    if (sections.empty()) {
        QLabel* none = label(filter_.isEmpty() ? tr("Nothing here") : tr("Nothing called \"%1\"").arg(filter_), Tone::TextFaint);
        none->setAlignment(Qt::AlignCenter);
        col->addWidget(none);
    }
    col->addStretch();
    setWidget(content);
}

} // namespace artisan::editor
