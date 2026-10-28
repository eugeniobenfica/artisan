#include "ui/ProjectPanels.h"

#include "app/Platform.h"
#include "app/ProjectIO.h"
#include "ui/EditorContext.h"
#include "ui/Library.h"
#include "ui/Widgets.h"

#include <QDropEvent>
#include <QFileDialog>
#include <QAbstractFileIconProvider>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QMenu>
#include <QMessageBox>
#include <QTreeView>
#include <QVBoxLayout>

namespace artisan::editor {

using namespace ui;

namespace {

Icon iconOf(const Entity& e) {
    if (e.sprite) return Icon::Image;
    if (e.shape) return e.shape->kind == ShapeKind::Circle || e.shape->kind == ShapeKind::Capsule ? Icon::Ellipse : Icon::Rectangle;
    if (e.light) return Icon::Bulb;
    return Icon::Cube;
}

bool isImage(const std::filesystem::path& p) {
    std::string ext = platform::utf8(p.extension());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga";
}

// The files' icons, from the editor's own set.
class FileIcons : public QAbstractFileIconProvider {
public:
    QIcon icon(IconType type) const override { return menuIcon(type == Folder ? Icon::Folder : Icon::File, theme::kTextDim); }
    QIcon icon(const QFileInfo& info) const override {
        if (info.isDir()) return menuIcon(Icon::Folder, theme::kTextDim);
        const auto path = platform::fromQt(info.filePath());
        if (path.extension() == ".lua") return menuIcon(Icon::Code, theme::kAccent);
        if (path.extension() == ".artisan") return menuIcon(Icon::Logo, theme::kTextDim);
        return menuIcon(isImage(path) ? Icon::Image : Icon::File, theme::kTextDim);
    }
};

// A panel's header row: its small title, then its buttons.
QHBoxLayout* headerRow(QVBoxLayout* col, const QString& title) {
    auto* row = new QHBoxLayout;
    row->setContentsMargins(metrics::kPanelMargin, 6, 6, 2);
    row->setSpacing(2);
    row->addWidget(sectionLabel(title));
    row->addStretch(1);
    col->addLayout(row);
    return row;
}

} // namespace

// The tree, telling when its rows were dragged to a new order.
class HierarchyTree : public QTreeWidget {
public:
    std::function<void()> reordered;

protected:
    void dropEvent(QDropEvent* e) override {
        QTreeWidget::dropEvent(e);
        if (reordered) reordered();
    }
};

// --- Hierarchy ---------------------------------------------------------------------------------

HierarchyPanel::HierarchyPanel(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    QHBoxLayout* head = headerRow(col, tr("Scene"));
    auto* add = new IconButton(Icon::Plus, tr("Add an object"), 24);
    connect(add, &QAbstractButton::clicked, this, [this, add] {
        QMenu menu;
        for (const ObjectKind& k : objectKinds()) {
            const std::string kind = k.kind;
            menu.addAction(tr(k.name), this, [this, kind] { addObject(kind, ctx_, this); });
        }
        menu.exec(add->mapToGlobal(QPoint(0, add->height())));
    });
    head->addWidget(add);

    tree_ = new HierarchyTree;
    tree_->setHeaderHidden(true);
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->setIconSize(QSize(16, 16));
    tree_->setIndentation(14);
    tree_->setRootIsDecorated(false);
    tree_->setDragDropMode(QAbstractItemView::InternalMove); // order = drawing order within a layer
    tree_->setEditTriggers(QAbstractItemView::EditKeyPressed);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    // Objects go only between the scene's other objects, never into one another or beside the scene.
    tree_->invisibleRootItem()->setFlags(tree_->invisibleRootItem()->flags() & ~Qt::ItemIsDropEnabled);
    col->addWidget(tree_, 1);

    tree_->reordered = [this] {
        if (syncing_ || !tree_->topLevelItem(0)) return;
        QTreeWidgetItem* root = tree_->topLevelItem(0);
        std::vector<Entity> sorted;
        for (int i = 0; i < root->childCount(); ++i)
            if (const Entity* e = ctx_.scene().find(root->child(i)->data(0, Qt::UserRole).toUInt())) sorted.push_back(*e);
        if (sorted.size() != ctx_.scene().entities.size()) {
            signature_.clear();
            return rebuild();
        }
        ctx_.checkpoint();
        ctx_.scene().entities = std::move(sorted);
        ctx_.changed();
    };
    // The selection, not the current item: the tree makes its first row current when it gets
    // the keyboard, which is not the user picking it.
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (syncing_) return;
        const auto items = tree_->selectedItems();
        if (!items.isEmpty()) ctx_.select(items.front()->data(0, Qt::UserRole).toUInt());
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) { tree_->editItem(item); });
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item) {
        if (syncing_) return;
        const std::string name = item->text(0).trimmed().toStdString();
        const EntityId id = item->data(0, Qt::UserRole).toUInt();
        if (name.empty()) { // no name: the old one comes back
            signature_.clear();
            QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
            return;
        }
        if (!id) { // the scene
            if (name == ctx_.scene().name) return;
            ctx_.checkpoint();
            ctx_.scene().name = name;
            ctx_.changed();
            emit ctx_.scenesChanged();
            return;
        }
        Entity* e = ctx_.scene().find(id);
        if (!e || name == e->name) return;
        ctx_.checkpoint();
        e->name = name;
        ctx_.changed();
    });
    connect(tree_, &QWidget::customContextMenuRequested, this, [this](QPoint at) {
        QTreeWidgetItem* item = tree_->itemAt(at);
        if (!item) return;
        QMenu menu;
        const EntityId id = item->data(0, Qt::UserRole).toUInt();
        if (id) {
            menu.addAction(menuIcon(Icon::Duplicate, theme::kTextDim), tr("Duplicate"), this, [this] { duplicateSelected(); });
            menu.addAction(menuIcon(Icon::Edit, theme::kTextDim), tr("Rename"), this, [this, item] { tree_->editItem(item); });
            menu.addAction(menuIcon(Icon::Graph, theme::kTextDim), tr("Edit Nodes"), this, [this] { emit ctx_.editGraphRequested(); });
            menu.addSeparator();
            menu.addAction(menuIcon(Icon::Trash, theme::kDanger), tr("Delete"), this, [this] { deleteSelected(); });
        } else {
            menu.addAction(menuIcon(Icon::Edit, theme::kTextDim), tr("Rename Scene"), this, [this, item] { tree_->editItem(item); });
            menu.addAction(menuIcon(Icon::Graph, theme::kTextDim), tr("Edit the Scene's Nodes"), this, [this] { emit ctx_.editGraphRequested(); });
        }
        menu.exec(tree_->viewport()->mapToGlobal(at));
    });

    // Later: an item renamed in the tree is not deleted while the tree tells about it.
    connect(&ctx_, &EditorContext::sceneChanged, this, [this] { rebuild(); }, Qt::QueuedConnection);
    connect(&ctx_, &EditorContext::scenesChanged, this, [this] { rebuild(); }, Qt::QueuedConnection);
    connect(&ctx_, &EditorContext::selectionChanged, this, [this] { syncSelection(); });
    connect(&Appearance::get(), &Appearance::changed, this, [this] {
        signature_.clear();
        rebuild();
    });
}

void HierarchyPanel::rebuild() {
    const Scene& s = ctx_.scene();
    const bool start = ctx_.sceneIndex() == ctx_.project.mainScene;
    std::string sig = std::to_string(ctx_.sceneIndex()) + s.name + (start ? "*" : "") + (s.graph || !s.script.empty() ? "b" : "") + "|";
    for (const Entity& e : s.entities)
        sig += std::to_string(e.id) + ":" + e.name + (e.visible ? "" : "~") + (e.graph || !e.script.empty() ? "b" : "") +
               std::to_string(static_cast<int>(iconOf(e))) + "|";
    if (sig == signature_) return;
    signature_ = sig;
    syncing_ = true;
    tree_->clear();
    auto* root = new QTreeWidgetItem(tree_);
    root->setText(0, QString::fromStdString(s.name));
    root->setIcon(0, menuIcon(Icon::Layers, theme::kAccent));
    root->setData(0, Qt::UserRole, 0u);
    root->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDropEnabled);
    QFont bold = tree_->font();
    bold.setWeight(QFont::DemiBold);
    root->setFont(0, bold);
    root->setToolTip(0, (start ? tr("The scene the game starts with. ") : QString()) + tr("Select it for the scene's settings and its own behavior"));
    for (const Entity& e : s.entities) {
        auto* item = new QTreeWidgetItem(root);
        const bool behaves = e.graph || !e.script.empty();
        item->setText(0, QString::fromStdString(e.name));
        item->setIcon(0, menuIcon(iconOf(e), e.visible ? theme::kTextDim : theme::kTextFaint));
        item->setData(0, Qt::UserRole, e.id);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
        if (!e.visible) item->setForeground(0, theme::q(theme::kTextFaint));
        if (behaves) item->setToolTip(0, tr("Has behavior: %1").arg(e.graph && !e.script.empty() ? tr("nodes and a script") : e.graph ? tr("nodes") : tr("a script")));
    }
    root->setExpanded(true);
    syncing_ = false;
    syncSelection();
}

void HierarchyPanel::syncSelection() {
    syncing_ = true;
    tree_->clearSelection();
    QTreeWidgetItem* root = tree_->topLevelItem(0);
    if (root) {
        QTreeWidgetItem* pick = root;
        for (int i = 0; i < root->childCount(); ++i)
            if (root->child(i)->data(0, Qt::UserRole).toUInt() == ctx_.selection()) pick = root->child(i);
        tree_->setCurrentItem(pick, 0, QItemSelectionModel::ClearAndSelect);
        tree_->scrollToItem(pick);
    }
    syncing_ = false;
}

void HierarchyPanel::deleteSelected() {
    if (!ctx_.selected()) return;
    ctx_.checkpoint();
    ctx_.scene().remove(ctx_.selection());
    ctx_.changed();
}

void HierarchyPanel::duplicateSelected() {
    const Entity* e = ctx_.selected();
    if (!e) return;
    Entity copy = *e;
    copy.position += Vec2(24, 24);
    ctx_.checkpoint();
    const EntityId id = ctx_.scene().add(std::move(copy)).id;
    ctx_.changed();
    ctx_.select(id);
}

// --- Files ---------------------------------------------------------------------------------------

FilesPanel::FilesPanel(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    QHBoxLayout* head = headerRow(col, tr("Project files"));
    auto* newScript = new IconButton(Icon::Code, tr("New Lua script in scripts/"), 24);
    auto* import = new IconButton(Icon::Import, tr("Import pictures into assets/"), 24);
    auto* reveal = new IconButton(Icon::Folder, tr("Show the project folder"), 24);
    head->addWidget(newScript);
    head->addWidget(import);
    head->addWidget(reveal);

    model_ = new QFileSystemModel(this);
    model_->setReadOnly(true);
    static FileIcons icons;
    model_->setIconProvider(&icons);
    view_ = new QTreeView;
    view_->setModel(model_);
    view_->setHeaderHidden(true);
    view_->setFrameShape(QFrame::NoFrame);
    view_->setIndentation(14);
    view_->setIconSize(QSize(16, 16));
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    for (int c = 1; c < 4; ++c) view_->hideColumn(c);
    col->addWidget(view_, 1);

    auto pathOf = [this](const QModelIndex& i) { return platform::fromQt(model_->filePath(i)); };
    connect(view_, &QTreeView::doubleClicked, this, [this, pathOf](const QModelIndex& i) {
        if (!model_->isDir(i)) open(pathOf(i));
    });
    connect(view_, &QWidget::customContextMenuRequested, this, [this, pathOf](QPoint at) {
        const QModelIndex i = view_->indexAt(at);
        const auto path = i.isValid() ? pathOf(i) : ctx_.dir;
        QMenu menu;
        if (i.isValid() && !model_->isDir(i)) {
            menu.addAction(menuIcon(Icon::Edit, theme::kTextDim), tr("Open"), this, [this, path] { open(path); });
            if (isImage(path)) menu.addAction(menuIcon(Icon::Image, theme::kTextDim), tr("Add to the Scene as a Sprite"), this, [this, path] { addSprite(path); });
        }
        menu.addAction(menuIcon(Icon::Folder, theme::kTextDim), tr("Show in the File Manager"), this, [path] { platform::revealInFileManager(path); });
        menu.exec(view_->viewport()->mapToGlobal(at));
    });
    connect(reveal, &QAbstractButton::clicked, this, [this] { platform::revealInFileManager(ctx_.dir / kProjectFile); });
    connect(newScript, &QAbstractButton::clicked, this, [this] {
        std::error_code ec;
        std::string path = "scripts/New Script.lua";
        for (int i = 2; std::filesystem::exists(ctx_.dir / platform::fromUtf8(path), ec); ++i) path = "scripts/New Script " + std::to_string(i) + ".lua";
        std::string error;
        if (!platform::writeFile(ctx_.dir / platform::fromUtf8(path), newScriptTemplate("New Script"), &error))
            return (void)QMessageBox::warning(this, tr("New Script"), QString::fromStdString(error));
        open(ctx_.dir / platform::fromUtf8(path));
    });
    connect(import, &QAbstractButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import Pictures"), QString(), tr("Images (*.png *.jpg *.jpeg *.bmp *.tga)"));
        std::error_code ec;
        std::filesystem::create_directories(ctx_.dir / "assets", ec);
        for (const QString& f : files) {
            const auto src = platform::fromQt(f);
            std::filesystem::copy_file(src, ctx_.dir / "assets" / src.filename(), std::filesystem::copy_options::overwrite_existing, ec);
        }
    });
    connect(&ctx_, &EditorContext::opened, this, [this] {
        const QString root = platform::toQt(ctx_.dir);
        model_->setRootPath(root);
        view_->setRootIndex(model_->index(root));
    });
}

void FilesPanel::open(const std::filesystem::path& file) {
    if (file.extension() == ".lua" && onOpenScript) onOpenScript(file);
    else platform::openWithDefaultApp(platform::toQt(file));
}

void FilesPanel::addSprite(const std::filesystem::path& image) {
    std::error_code ec;
    const auto rel = std::filesystem::relative(image, ctx_.dir, ec);
    if (ec || rel.empty() || platform::utf8(rel).starts_with("..")) return;
    Entity e;
    e.name = platform::utf8(image.stem());
    Sprite s;
    s.image = platform::utf8(rel);
    if (const QSize size = QImageReader(platform::toQt(image)).size(); size.isValid()) s.size = {static_cast<float>(size.width()), static_cast<float>(size.height())};
    e.sprite = s;
    e.position = ctx_.scene().camera.position;
    ctx_.checkpoint();
    const EntityId id = ctx_.scene().add(std::move(e)).id;
    ctx_.changed();
    ctx_.select(id);
}

} // namespace artisan::editor
