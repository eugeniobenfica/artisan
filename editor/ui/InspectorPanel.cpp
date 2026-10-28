#include "ui/InspectorPanel.h"

#include "app/Platform.h"
#include "app/ProjectIO.h"
#include "artisan/runtime/SceneDraw.h"
#include "ui/EditorContext.h"
#include "ui/Widgets.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

namespace artisan::editor {

using namespace ui;

namespace {

QWidget* hbox(std::initializer_list<QWidget*> widgets) {
    auto* w = new QWidget;
    auto* row = new QHBoxLayout(w);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(metrics::kGap);
    for (QWidget* c : widgets) row->addWidget(c, 1);
    return w;
}

} // namespace

InspectorPanel::InspectorPanel(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    inspector_ = new QScrollArea;
    inspector_->setWidgetResizable(true);
    inspector_->setFrameShape(QFrame::NoFrame);
    inspector_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    col->addWidget(inspector_);

    connect(&ctx_, &EditorContext::sceneChanged, this, [this] { refreshValues(); });
    connect(&ctx_, &EditorContext::selectionChanged, this, [this] { rebuildInspector(); });
    // Later: a scene renamed from this panel's own field is not deleted while it tells.
    connect(&ctx_, &EditorContext::scenesChanged, this, [this] { rebuildInspector(); }, Qt::QueuedConnection);
    connect(&ctx_, &EditorContext::opened, this, [this] { rebuildInspector(); });
}

void InspectorPanel::refreshValues() {
    for (auto& f : refreshers_) f();
}

QWidget* InspectorPanel::section(QVBoxLayout* col, const QString& title, std::function<void()> remove) {
    auto* head = new QWidget;
    auto* row = new QHBoxLayout(head);
    row->setContentsMargins(0, metrics::kGroupGap, 0, 0);
    row->addWidget(sectionLabel(title));
    row->addStretch(1);
    if (remove) {
        auto* x = new IconButton(Icon::Trash, tr("Remove %1").arg(title.toLower()), 22);
        connect(x, &QAbstractButton::clicked, this, [remove] { remove(); });
        row->addWidget(x);
    }
    col->addWidget(head);
    return head;
}

void InspectorPanel::rebuildInspector() {
    refreshers_.clear();
    auto* body = new QWidget;
    auto* col = new QVBoxLayout(body);
    col->setContentsMargins(metrics::kPanelMargin, 0, metrics::kPanelMargin, metrics::kPanelMargin);
    col->setSpacing(metrics::kGap);
    if (Entity* e = ctx_.selected()) buildEntityInspector(col, *e);
    else buildSceneInspector(col);
    col->addStretch(1);
    inspector_->setWidget(body);
}

void InspectorPanel::buildEntityInspector(QVBoxLayout* col, Entity& entity) {
    const EntityId id = entity.id;
    auto ent = [this, id] { return ctx_.scene().find(id); };
    // A field bound to a float of the entity: scrubbing is one undo step.
    auto number = [&](const QString& name, std::function<float*(Entity&)> field, const char* format = "%.1f", float speed = 1.0f, float min = 0,
                      float max = 0) {
        auto* v = new DragValue;
        v->setFormat(format);
        v->setSpeed(speed);
        v->setRange(min, max);
        const std::string key = std::to_string(id) + name.toStdString();
        connect(v, &DragValue::valueChanged, this, [this, ent, field, key](float value) {
            Entity* e = ent();
            if (!e || !field(*e)) return;
            ctx_.checkpoint(key);
            *field(*e) = value;
            ctx_.changed();
        });
        refreshers_.push_back([ent, field, v] {
            if (Entity* e = ent(); e && field(*e) && !v->editing()) v->setValue(*field(*e));
        });
        v->setValue(field(entity) ? *field(entity) : 0.0f);
        return v;
    };
    auto color = [&](std::function<Color*(Entity&)> field) {
        auto* b = new ColorButton;
        b->setColor(*field(entity));
        const std::string key = std::to_string(id) + "color" + std::to_string(reinterpret_cast<uintptr_t>(b));
        connect(b, &ColorButton::picked, this, [this, ent, field, key](const Color& c) {
            Entity* e = ent();
            if (!e || !field(*e)) return;
            ctx_.checkpoint(key);
            *field(*e) = c;
            ctx_.changed();
        });
        refreshers_.push_back([ent, field, b] {
            if (Entity* e = ent(); e && field(*e)) b->setColor(*field(*e));
        });
        return b;
    };
    auto toggle = [&](std::function<bool*(Entity&)> field) {
        auto* s = new Switch;
        s->setChecked(*field(entity));
        connect(s, &QAbstractButton::toggled, this, [this, ent, field](bool on) {
            Entity* e = ent();
            if (!e || !field(*e) || *field(*e) == on) return;
            ctx_.checkpoint();
            *field(*e) = on;
            ctx_.changed();
        });
        refreshers_.push_back([ent, field, s] {
            if (Entity* e = ent(); e && field(*e) && s->isChecked() != *field(*e)) {
                const QSignalBlocker block(s);
                s->setChecked(*field(*e));
            }
        });
        return s;
    };
    auto rebuildLater = [this] { QMetaObject::invokeMethod(this, [this] { rebuildInspector(); }, Qt::QueuedConnection); };

    // --- name ------------------------------------------------------------------------------
    auto* name = new QLineEdit(QString::fromStdString(entity.name));
    connect(name, &QLineEdit::editingFinished, this, [this, ent, name] {
        Entity* e = ent();
        const std::string n = name->text().trimmed().toStdString();
        if (!e || n.empty() || n == e->name) return;
        ctx_.checkpoint();
        e->name = n;
        ctx_.changed();
    });
    refreshers_.push_back([ent, name] {
        if (Entity* e = ent(); e && !name->hasFocus()) name->setText(QString::fromStdString(e->name));
    });
    col->addSpacing(metrics::kPanelMargin);
    col->addWidget(name);

    // --- transform -------------------------------------------------------------------------
    section(col, tr("Transform"));
    col->addWidget(new LabeledRow(tr("Position"), hbox({number("x", [](Entity& e) { return &e.position.x; }),
                                                         number("y", [](Entity& e) { return &e.position.y; })}), 120));
    col->addWidget(new LabeledRow(tr("Rotation"), number("rot", [](Entity& e) { return &e.rotation; }, "%.1f°"), 120));
    col->addWidget(new LabeledRow(tr("Scale"), hbox({number("sx", [](Entity& e) { return &e.scale.x; }, "%.2f", 0.01f),
                                                      number("sy", [](Entity& e) { return &e.scale.y; }, "%.2f", 0.01f)}), 120));
    {
        auto* layer = new DragValue;
        layer->setFormat("%.0f");
        layer->setSpeed(0.1f);
        layer->setRange(-1000, 1000);
        layer->setValue(static_cast<float>(entity.layer));
        connect(layer, &DragValue::valueChanged, this, [this, ent](float v) {
            Entity* e = ent();
            if (!e || e->layer == static_cast<int>(std::lround(v))) return;
            ctx_.checkpoint("layer");
            e->layer = static_cast<int>(std::lround(v));
            ctx_.changed();
        });
        refreshers_.push_back([ent, layer] {
            if (Entity* e = ent(); e && !layer->editing()) layer->setValue(static_cast<float>(e->layer));
        });
        col->addWidget(new LabeledRow(tr("Layer"), layer, 120));
    }
    col->addWidget(new LabeledRow(tr("Visible"), toggle([](Entity& e) { return &e.visible; }), 120));

    // --- shape -----------------------------------------------------------------------------
    if (entity.shape) {
        section(col, tr("Shape"), [this, ent, rebuildLater] {
            ctx_.checkpoint();
            if (Entity* e = ent()) e->shape.reset();
            ctx_.changed();
            rebuildLater();
        });
        auto* kind = new QComboBox;
        kind->addItems({tr("Rectangle"), tr("Rounded rectangle"), tr("Circle"), tr("Capsule")});
        kind->setCurrentIndex(static_cast<int>(entity.shape->kind));
        connect(kind, &QComboBox::currentIndexChanged, this, [this, ent, rebuildLater](int i) {
            Entity* e = ent();
            if (!e || !e->shape || static_cast<int>(e->shape->kind) == i) return;
            ctx_.checkpoint();
            e->shape->kind = static_cast<ShapeKind>(i);
            ctx_.changed();
            rebuildLater();
        });
        col->addWidget(new LabeledRow(tr("Kind"), kind, 120));
        col->addWidget(new LabeledRow(tr("Size"), hbox({number("w", [](Entity& e) { return e.shape ? &e.shape->size.x : nullptr; }, "%.0f", 1, 0, 100000),
                                                         number("h", [](Entity& e) { return e.shape ? &e.shape->size.y : nullptr; }, "%.0f", 1, 0, 100000)}), 120));
        if (entity.shape->kind == ShapeKind::RoundedRectangle)
            col->addWidget(new LabeledRow(tr("Corners"), number("corner", [](Entity& e) { return e.shape ? &e.shape->cornerRadius : nullptr; }, "%.0f px", 0.5f, 0, 10000), 120));
        col->addWidget(new LabeledRow(tr("Color"), color([](Entity& e) { return e.shape ? &e.shape->color : nullptr; }), 120));
        col->addWidget(new LabeledRow(tr("Outline"), hbox({number("outline", [](Entity& e) { return e.shape ? &e.shape->outline : nullptr; }, "%.1f px", 0.1f, 0, 1000),
                                                            color([](Entity& e) { return e.shape ? &e.shape->outlineColor : nullptr; })}), 120));
        col->addWidget(new LabeledRow(tr("Glow"), number("glow", [](Entity& e) { return e.shape ? &e.shape->glow : nullptr; }, "%.0f px", 0.5f, 0, 2000), 120));
        col->addWidget(new LabeledRow(tr("Lit"), toggle([](Entity& e) { return e.shape ? &e.shape->lit : nullptr; }), 120));
    }

    // --- sprite ----------------------------------------------------------------------------
    if (entity.sprite) {
        section(col, tr("Sprite"), [this, ent, rebuildLater] {
            ctx_.checkpoint();
            if (Entity* e = ent()) e->sprite.reset();
            ctx_.changed();
            rebuildLater();
        });
        auto* image = new QPushButton(QString::fromStdString(entity.sprite->image.empty() ? "Choose..." : entity.sprite->image));
        connect(image, &QPushButton::clicked, this, [this, ent, rebuildLater] {
            const QString file = QFileDialog::getOpenFileName(this, tr("Choose an Image"), platform::toQt(ctx_.dir / "assets"),
                                                              tr("Images (*.png *.jpg *.jpeg *.bmp *.tga)"));
            Entity* e = ent();
            if (file.isEmpty() || !e || !e->sprite) return;
            const auto src = platform::fromQt(file);
            std::error_code ec;
            auto rel = std::filesystem::relative(src, ctx_.dir, ec);
            if (ec || rel.empty() || platform::utf8(rel).starts_with("..")) {
                rel = std::filesystem::path("assets") / src.filename();
                std::filesystem::copy_file(src, ctx_.dir / rel, std::filesystem::copy_options::overwrite_existing, ec);
            }
            ctx_.checkpoint();
            e->sprite->image = platform::utf8(rel);
            ctx_.changed();
            rebuildLater();
        });
        col->addWidget(new LabeledRow(tr("Image"), image, 120));
        col->addWidget(new LabeledRow(tr("Size"), hbox({number("sw", [](Entity& e) { return e.sprite ? &e.sprite->size.x : nullptr; }, "%.0f", 1, 0, 100000),
                                                         number("sh", [](Entity& e) { return e.sprite ? &e.sprite->size.y : nullptr; }, "%.0f", 1, 0, 100000)}), 120));
        col->addWidget(new LabeledRow(tr("Tint"), color([](Entity& e) { return e.sprite ? &e.sprite->tint : nullptr; }), 120));
        col->addWidget(new LabeledRow(tr("Lit"), toggle([](Entity& e) { return e.sprite ? &e.sprite->lit : nullptr; }), 120));
    }

    // --- light -----------------------------------------------------------------------------
    if (entity.light) {
        section(col, tr("Light"), [this, ent, rebuildLater] {
            ctx_.checkpoint();
            if (Entity* e = ent()) e->light.reset();
            ctx_.changed();
            rebuildLater();
        });
        col->addWidget(new LabeledRow(tr("Color"), color([](Entity& e) { return e.light ? &e.light->color : nullptr; }), 120));
        col->addWidget(new LabeledRow(tr("Radius"), number("radius", [](Entity& e) { return e.light ? &e.light->radius : nullptr; }, "%.0f px", 1, 0, 100000), 120));
        col->addWidget(new LabeledRow(tr("Intensity"), number("intensity", [](Entity& e) { return e.light ? &e.light->intensity : nullptr; }, "%.2f", 0.01f, 0, 50), 120));
    }

    buildBehavior(col);

    // --- more components -------------------------------------------------------------------------
    if (!entity.shape || !entity.sprite || !entity.light) {
        auto* more = new QPushButton(makeIcon(Icon::Plus, theme::kTextDim, 20), tr("Add Component"));
        connect(more, &QPushButton::clicked, this, [this, ent, more, rebuildLater] {
            Entity* e = ent();
            if (!e) return;
            QMenu menu;
            auto add = [&](const QString& name, bool has, std::function<void(Entity&)> make) {
                if (has) return;
                menu.addAction(name, [this, ent, make, rebuildLater] {
                    ctx_.checkpoint();
                    if (Entity* x = ent()) make(*x);
                    ctx_.changed();
                    rebuildLater();
                });
            };
            add(tr("Shape"), e->shape.has_value(), [](Entity& x) { x.shape = Shape{}; });
            add(tr("Sprite"), e->sprite.has_value(), [](Entity& x) { x.sprite = Sprite{}; });
            add(tr("Light"), e->light.has_value(), [](Entity& x) { x.light = Light{}; });
            menu.exec(more->mapToGlobal(QPoint(0, more->height())));
        });
        col->addSpacing(metrics::kGroupGap);
        col->addWidget(more);
    }
}

// The behavior of the selected object, or of the scene when none is: its script and its nodes.
void InspectorPanel::buildBehavior(QVBoxLayout* col) {
    section(col, tr("Behavior"));
    const std::string& current = ctx_.script();
    auto* script = new QPushButton(current.empty() ? tr("Create Lua Script") : QString::fromStdString(current));
    script->setIcon(makeIcon(Icon::Code, theme::kTextDim, 20));
    script->setToolTip(tr("A Lua script: start(self) and update(self, dt)"));
    connect(script, &QPushButton::clicked, this, [this] {
        if (ctx_.script().empty()) {
            const std::string owner = ctx_.behaviorOwner();
            std::error_code ec;
            std::string base = "scripts/" + safeFileName(owner);
            std::string path = base + ".lua";
            for (int i = 2; std::filesystem::exists(ctx_.dir / platform::fromUtf8(path), ec); ++i) path = base + " " + std::to_string(i) + ".lua";
            std::string error;
            if (!platform::writeFile(ctx_.dir / platform::fromUtf8(path), newScriptTemplate(owner), &error)) {
                QMessageBox::warning(this, tr("Script"), QString::fromStdString(error));
                return;
            }
            ctx_.checkpoint();
            ctx_.script() = path;
            ctx_.changed();
            ctx_.save();
        }
        emit ctx_.editScriptRequested();
    });
    col->addWidget(new LabeledRow(tr("Script"), script, 120));
    const std::optional<NodeGraph>& g = ctx_.graph();
    auto* graph = new QPushButton(g ? tr("Edit Nodes (%1)").arg(g->nodes.size()) : tr("Create Node Graph"));
    graph->setIcon(makeIcon(Icon::Graph, theme::kTextDim, 20));
    graph->setToolTip(tr("Visual scripting: events, actions and values joined by wires"));
    connect(graph, &QPushButton::clicked, this, [this] {
        if (!ctx_.graph()) {
            ctx_.checkpoint();
            NodeGraph fresh;
            fresh.add("event.start", 40, 40);
            fresh.add("event.update", 40, 200);
            ctx_.graph() = fresh;
            ctx_.changed();
        }
        emit ctx_.editGraphRequested();
    });
    col->addWidget(new LabeledRow(tr("Nodes"), graph, 120));
}

void InspectorPanel::buildSceneInspector(QVBoxLayout* col) {
    col->addSpacing(metrics::kPanelMargin);
    auto* name = new QLineEdit(QString::fromStdString(ctx_.scene().name));
    connect(name, &QLineEdit::editingFinished, this, [this, name] {
        const std::string n = name->text().trimmed().toStdString();
        if (n.empty() || n == ctx_.scene().name) return;
        ctx_.checkpoint();
        ctx_.scene().name = n;
        ctx_.changed();
        emit ctx_.scenesChanged();
    });
    col->addWidget(name);
    QLabel* hint = label(tr("The scene: its look, its camera and its own behavior. Select an object for its settings."), Tone::TextFaint, 0.92f);
    hint->setWordWrap(true);
    col->addWidget(hint);
    {
        auto* start = new Switch;
        start->setChecked(ctx_.sceneIndex() == ctx_.project.mainScene);
        start->setEnabled(!start->isChecked()); // another scene takes it from this one
        start->setToolTip(tr("The scene the game opens with"));
        connect(start, &QAbstractButton::toggled, this, [this](bool on) {
            if (on) ctx_.setStartScene(ctx_.sceneIndex());
        });
        col->addWidget(new LabeledRow(tr("Start scene"), start, 120));
    }

    auto colorRow = [&](const QString& name, Color Scene::*field) {
        auto* b = new ColorButton;
        b->setColor(ctx_.scene().*field);
        connect(b, &ColorButton::picked, this, [this, field](const Color& c) {
            ctx_.checkpoint("scene color");
            ctx_.scene().*field = c;
            ctx_.changed();
        });
        refreshers_.push_back([this, b, field] { b->setColor(ctx_.scene().*field); });
        col->addWidget(new LabeledRow(name, b, 120));
    };
    auto number = [&](std::function<float&()> field, const char* format, float speed, float min, float max) {
        auto* v = new DragValue;
        v->setFormat(format);
        v->setSpeed(speed);
        v->setRange(min, max);
        v->setValue(field());
        connect(v, &DragValue::valueChanged, this, [this, field](float x) {
            ctx_.checkpoint("scene value");
            field() = x;
            ctx_.changed();
        });
        refreshers_.push_back([v, field] {
            if (!v->editing()) v->setValue(field());
        });
        return v;
    };

    section(col, tr("Look"));
    colorRow(tr("Background"), &Scene::background);
    colorRow(tr("Ambient light"), &Scene::ambient);
    section(col, tr("Camera"));
    col->addWidget(new LabeledRow(tr("Position"), hbox({number([this]() -> float& { return ctx_.scene().camera.position.x; }, "%.0f", 1, 0, 0),
                                                         number([this]() -> float& { return ctx_.scene().camera.position.y; }, "%.0f", 1, 0, 0)}), 120));
    col->addWidget(new LabeledRow(tr("Zoom"), number([this]() -> float& { return ctx_.scene().camera.zoom; }, "%.2f×", 0.01f, 0.05f, 50), 120));

    section(col, tr("Game"));
    auto* w = new DragValue;
    auto* h = new DragValue;
    for (DragValue* v : {w, h}) {
        v->setFormat("%.0f px");
        v->setRange(16, 16384);
    }
    w->setValue(static_cast<float>(ctx_.project.window.width));
    h->setValue(static_cast<float>(ctx_.project.window.height));
    connect(w, &DragValue::valueChanged, this, [this](float v) {
        ctx_.project.window.width = static_cast<int>(std::lround(v));
        ctx_.changed();
    });
    connect(h, &DragValue::valueChanged, this, [this](float v) {
        ctx_.project.window.height = static_cast<int>(std::lround(v));
        ctx_.changed();
    });
    col->addWidget(new LabeledRow(tr("Resolution"), hbox({w, h}), 120));
    auto flag = [&](const QString& name, bool WindowSettings::*field) {
        auto* s = new Switch;
        s->setChecked(ctx_.project.window.*field);
        connect(s, &QAbstractButton::toggled, this, [this, field](bool on) {
            ctx_.project.window.*field = on;
            ctx_.changed();
        });
        col->addWidget(new LabeledRow(name, s, 120));
    };
    flag(tr("Pixel art"), &WindowSettings::pixelArt);
    flag(tr("Fullscreen"), &WindowSettings::fullscreen);
    flag(tr("Vertical sync"), &WindowSettings::vsync);

    buildBehavior(col);
}

} // namespace artisan::editor
