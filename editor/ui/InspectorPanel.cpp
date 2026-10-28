#include "ui/ObjectsPanel.h"

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

Icon iconOf(const Entity& e) {
    if (e.sprite) return Icon::Image;
    if (e.shape) return e.shape->kind == ShapeKind::Circle || e.shape->kind == ShapeKind::Capsule ? Icon::Ellipse : Icon::Rectangle;
    if (e.light) return Icon::Burst;
    return Icon::Cube;
}

QWidget* hbox(std::initializer_list<QWidget*> widgets) {
    auto* w = new QWidget;
    auto* row = new QHBoxLayout(w);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(metrics::kGap);
    for (QWidget* c : widgets) row->addWidget(c, 1);
    return w;
}

} // namespace

ObjectsPanel::ObjectsPanel(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    auto* split = new QSplitter(Qt::Vertical);
    split->setChildrenCollapsible(false);
    col->addWidget(split);

    // --- the objects ------------------------------------------------------------------------
    auto* top = new QWidget;
    auto* tcol = new QVBoxLayout(top);
    tcol->setContentsMargins(metrics::kPanelMargin, metrics::kPanelMargin, metrics::kPanelMargin, 4);
    tcol->setSpacing(6);
    auto* head = new QHBoxLayout;
    head->addWidget(sectionLabel(tr("Objects")));
    head->addStretch(1);
    auto* add = new IconButton(Icon::Plus, tr("Add an object"), 26);
    connect(add, &QAbstractButton::clicked, this, [this, add] {
        QMenu menu;
        menu.addAction(menuIcon(Icon::Rectangle, theme::kTextDim), tr("Rectangle"), [this] { addObject("rectangle"); });
        menu.addAction(menuIcon(Icon::Rectangle, theme::kTextDim), tr("Rounded Rectangle"), [this] { addObject("rounded"); });
        menu.addAction(menuIcon(Icon::Ellipse, theme::kTextDim), tr("Circle"), [this] { addObject("circle"); });
        menu.addAction(menuIcon(Icon::Ellipse, theme::kTextDim), tr("Capsule"), [this] { addObject("capsule"); });
        menu.addSeparator();
        menu.addAction(menuIcon(Icon::Image, theme::kTextDim), tr("Sprite..."), [this] { addObject("sprite"); });
        menu.addAction(menuIcon(Icon::Burst, theme::kTextDim), tr("Light"), [this] { addObject("light"); });
        menu.addAction(menuIcon(Icon::Cube, theme::kTextDim), tr("Empty Object"), [this] { addObject("empty"); });
        menu.exec(add->mapToGlobal(QPoint(0, add->height())));
    });
    head->addWidget(add);
    tcol->addLayout(head);
    list_ = new QListWidget;
    list_->setFrameShape(QFrame::NoFrame);
    list_->setIconSize(QSize(16, 16));
    list_->setDragDropMode(QAbstractItemView::InternalMove); // order = drawing order within a layer
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    tcol->addWidget(list_, 1);
    split->addWidget(top);

    // The selection, not the current item: the list makes its first row current when it gets
    // the keyboard, which is not the user picking it.
    connect(list_, &QListWidget::itemSelectionChanged, this, [this] {
        if (syncing_) return;
        const auto items = list_->selectedItems();
        ctx_.select(items.isEmpty() ? 0 : items.front()->data(Qt::UserRole).toUInt());
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { list_->editItem(item); });
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (syncing_) return;
        Entity* e = ctx_.scene().find(item->data(Qt::UserRole).toUInt());
        const std::string name = item->text().trimmed().toStdString();
        if (!e || name.empty() || name == e->name) return;
        ctx_.checkpoint();
        e->name = name;
        ctx_.changed();
    });
    connect(list_->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        if (syncing_) return;
        // The list's order becomes the scene's.
        ctx_.checkpoint();
        std::vector<Entity> sorted;
        for (int i = 0; i < list_->count(); ++i)
            if (const Entity* e = ctx_.scene().find(list_->item(i)->data(Qt::UserRole).toUInt())) sorted.push_back(*e);
        if (sorted.size() == ctx_.scene().entities.size()) ctx_.scene().entities = std::move(sorted);
        ctx_.changed();
    });
    connect(list_, &QWidget::customContextMenuRequested, this, [this](QPoint at) {
        if (!list_->itemAt(at)) return;
        QMenu menu;
        menu.addAction(menuIcon(Icon::Duplicate, theme::kTextDim), tr("Duplicate"), [this] { duplicateSelected(); });
        menu.addAction(menuIcon(Icon::Edit, theme::kTextDim), tr("Rename"), [this] {
            if (list_->currentItem()) list_->editItem(list_->currentItem());
        });
        menu.addSeparator();
        menu.addAction(menuIcon(Icon::Trash, theme::kDanger), tr("Delete"), [this] { deleteSelected(); });
        menu.exec(list_->viewport()->mapToGlobal(at));
    });

    // --- the inspector ----------------------------------------------------------------------
    inspector_ = new QScrollArea;
    inspector_->setWidgetResizable(true);
    inspector_->setFrameShape(QFrame::NoFrame);
    inspector_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    split->addWidget(inspector_);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);

    connect(&ctx_, &EditorContext::sceneChanged, this, [this] {
        rebuildList();
        refreshValues();
    });
    connect(&ctx_, &EditorContext::selectionChanged, this, [this] {
        syncListSelection();
        rebuildInspector();
    });
    connect(&ctx_, &EditorContext::opened, this, [this] {
        listSignature_.clear();
        rebuildList();
        rebuildInspector();
    });
}

void ObjectsPanel::rebuildList() {
    std::string sig;
    for (const Entity& e : ctx_.scene().entities) sig += std::to_string(e.id) + ":" + e.name + (e.visible ? "" : "~") + std::to_string(static_cast<int>(iconOf(e))) + "|";
    if (sig == listSignature_) return;
    listSignature_ = sig;
    syncing_ = true;
    list_->clear();
    for (const Entity& e : ctx_.scene().entities) {
        auto* item = new QListWidgetItem(menuIcon(iconOf(e), e.visible ? theme::kTextDim : theme::kTextFaint), QString::fromStdString(e.name));
        item->setData(Qt::UserRole, e.id);
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
        if (!e.visible) item->setForeground(theme::q(theme::kTextFaint));
        list_->addItem(item);
    }
    syncing_ = false;
    syncListSelection();
}

void ObjectsPanel::syncListSelection() {
    syncing_ = true;
    list_->clearSelection();
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(Qt::UserRole).toUInt() == ctx_.selection()) {
            list_->setCurrentItem(list_->item(i), QItemSelectionModel::ClearAndSelect);
            list_->scrollToItem(list_->item(i));
        }
    syncing_ = false;
}

void ObjectsPanel::addObject(const std::string& kind) {
    Entity e;
    // A new object goes where the game's camera looks.
    e.position = ctx_.scene().camera.position;
    if (kind == "sprite") {
        const QString file = QFileDialog::getOpenFileName(this, tr("Choose an Image"), platform::toQt(ctx_.dir / "assets"),
                                                          tr("Images (*.png *.jpg *.jpeg *.bmp *.tga)"));
        if (file.isEmpty()) return;
        // Images live in the project's assets/, so the game finds them wherever it goes.
        const auto src = platform::fromQt(file);
        std::error_code ec;
        auto rel = std::filesystem::relative(src, ctx_.dir, ec);
        if (ec || rel.empty() || platform::utf8(rel).starts_with("..")) {
            rel = std::filesystem::path("assets") / src.filename();
            std::filesystem::create_directories(ctx_.dir / "assets", ec);
            std::filesystem::copy_file(src, ctx_.dir / rel, std::filesystem::copy_options::overwrite_existing, ec);
        }
        Sprite s;
        s.image = platform::utf8(rel);
        if (const QSize size = QImageReader(file).size(); size.isValid()) s.size = {static_cast<float>(size.width()), static_cast<float>(size.height())};
        e.sprite = s;
        e.name = platform::utf8(src.stem());
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
    ctx_.checkpoint();
    const EntityId id = ctx_.scene().add(std::move(e)).id;
    ctx_.changed();
    ctx_.select(id);
}

void ObjectsPanel::deleteSelected() {
    if (!ctx_.selected()) return;
    ctx_.checkpoint();
    ctx_.scene().remove(ctx_.selection());
    ctx_.changed();
}

void ObjectsPanel::duplicateSelected() {
    const Entity* e = ctx_.selected();
    if (!e) return;
    Entity copy = *e;
    copy.position += Vec2(24, 24);
    ctx_.checkpoint();
    const EntityId id = ctx_.scene().add(std::move(copy)).id;
    ctx_.changed();
    ctx_.select(id);
}

void ObjectsPanel::refreshValues() {
    for (auto& f : refreshers_) f();
}

QWidget* ObjectsPanel::section(QVBoxLayout* col, const QString& title, std::function<void()> remove) {
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

void ObjectsPanel::rebuildInspector() {
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

void ObjectsPanel::buildEntityInspector(QVBoxLayout* col, Entity& entity) {
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

    // --- behavior --------------------------------------------------------------------------
    section(col, tr("Behavior"));
    {
        auto* script = new QPushButton(entity.script.empty() ? tr("Create Lua Script") : QString::fromStdString(entity.script));
        script->setIcon(makeIcon(Icon::Edit, theme::kTextDim, 20));
        script->setToolTip(tr("A Lua script: start(self) and update(self, dt)"));
        connect(script, &QPushButton::clicked, this, [this, ent] {
            Entity* e = ent();
            if (!e) return;
            if (e->script.empty()) {
                std::error_code ec;
                std::string base = "scripts/" + safeFileName(e->name);
                std::string path = base + ".lua";
                for (int i = 2; std::filesystem::exists(ctx_.dir / platform::fromUtf8(path), ec); ++i) path = base + " " + std::to_string(i) + ".lua";
                std::string error;
                if (!platform::writeFile(ctx_.dir / platform::fromUtf8(path), newScriptTemplate(e->name), &error)) {
                    QMessageBox::warning(this, tr("Script"), QString::fromStdString(error));
                    return;
                }
                ctx_.checkpoint();
                e->script = path;
                ctx_.changed();
                ctx_.save();
            }
            emit ctx_.editScriptRequested();
        });
        col->addWidget(new LabeledRow(tr("Script"), script, 120));
        auto* graph = new QPushButton(entity.graph ? tr("Edit Nodes (%1)").arg(entity.graph->nodes.size()) : tr("Create Node Graph"));
        graph->setIcon(makeIcon(Icon::Graph, theme::kTextDim, 20));
        graph->setToolTip(tr("Visual scripting: events, actions and values joined by wires"));
        connect(graph, &QPushButton::clicked, this, [this, ent] {
            Entity* e = ent();
            if (!e) return;
            if (!e->graph) {
                ctx_.checkpoint();
                NodeGraph g;
                g.add("event.start", 40, 40);
                g.add("event.update", 40, 200);
                e->graph = g;
                ctx_.changed();
            }
            emit ctx_.editGraphRequested();
        });
        col->addWidget(new LabeledRow(tr("Nodes"), graph, 120));
    }

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

void ObjectsPanel::buildSceneInspector(QVBoxLayout* col) {
    col->addSpacing(metrics::kPanelMargin);
    col->addWidget(label(tr("Scene"), Tone::Text, 1.1f, true));
    QLabel* hint = label(tr("Nothing selected: the scene's own settings."), Tone::TextFaint, 0.92f);
    hint->setWordWrap(true);
    col->addWidget(hint);

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
}

} // namespace artisan::editor
