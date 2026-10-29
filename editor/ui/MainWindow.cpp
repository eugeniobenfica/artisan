#include "ui/MainWindow.h"

#include "app/Platform.h"
#include "app/Plugins.h"
#include "app/ProjectIO.h"
#include "app/Settings.h"
#include "app/Shortcuts.h"
#include "ui/EditorContext.h"
#include "ui/HomeScreen.h"
#include "ui/InspectorPanel.h"
#include "ui/Library.h"
#include "ui/Navigator.h"
#include "ui/NodeEditor.h"
#include "ui/ProjectPanels.h"
#include "ui/ScriptEditor.h"
#include "ui/SettingsWindow.h"
#include "ui/Viewport.h"
#include "ui/Widgets.h"
#include "ui/WindowChrome.h"

#include <DockAreaTitleBar.h>
#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <DockWidgetTab.h>

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QFileInfo>
#include <QLocale>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace artisan::editor {

using namespace ui;

namespace {
// The views in the middle, in their order.
enum View { SceneView, NodesView, ScriptView };
} // namespace

MainWindow::MainWindow(QVulkanInstance* vulkan, QWidget* parent) : QMainWindow(parent), vulkan_(vulkan) {
    ctx_ = new EditorContext;
    ctx_->setParent(this);
    resize(1440, 900);
    setMinimumSize(980, 600);

    stack_ = new QStackedWidget;
    setCentralWidget(stack_);
    home_ = new HomeScreen;
    home_->onOpen = [this](const std::filesystem::path& dir) { openProject(dir); };
    home_->onSettings = [this] { openSettings(this); };
    home_->onHelp = [this] { openHelp(this); };
    stack_->addWidget(home_);

    workspace_ = new QWidget;
    auto* col = new QVBoxLayout(workspace_);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    navigator_ = new Navigator(*ctx_);
    col->addWidget(navigator_);
    buildDocks();
    col->addWidget(docks_, 1);
    stack_->addWidget(workspace_);

    connect(ctx_, &EditorContext::editGraphRequested, this, [this] { showView(NodesView); });
    connect(ctx_, &EditorContext::editScriptRequested, this, [this] { showView(ScriptView); });
    connect(ctx_, &EditorContext::sceneChanged, this, [this] {
        updateTitle();
        autosaveSoon();
    });
    connect(ctx_, &EditorContext::saved, this, [this] {
        updateTitle();
        dropRecoveryCopy();
    });

    // Saved a moment after the last change; kept aside every few seconds while unsaved.
    autosave_ = new QTimer(this);
    autosave_->setSingleShot(true);
    autosave_->setInterval(1500);
    connect(autosave_, &QTimer::timeout, this, [this] {
        if (stack_->currentWidget() == workspace_ && ctx_->dirty() && settings().autosave) save(true);
    });
    auto* keeper = new QTimer(this);
    connect(keeper, &QTimer::timeout, this, [this] { keepRecoveryCopy(); });
    keeper->start(15000);

    buildMenus();
    applyShortcuts();
    connect(&ShortcutSignals::get(), &ShortcutSignals::changed, this, &MainWindow::applyShortcuts);
    connect(&PluginHost::get(), &PluginHost::changed, this, &MainWindow::buildPluginsMenu);
    connect(&SettingsSignals::get(), &SettingsSignals::resetLayoutRequested, this, &MainWindow::resetLayout);
    connect(&Appearance::get(), &Appearance::changed, this, [this] { tintTitleBar(this); });
    showHome();
}

MainWindow::~MainWindow() { stop(); }

void MainWindow::showEvent(QShowEvent* e) {
    QMainWindow::showEvent(e);
    tintTitleBar(this);
}

// --- docks -------------------------------------------------------------------------------------

void MainWindow::buildDocks() {
    // As in bemotion: no close, undock or tabs-menu buttons on the panels' headers, a minimize
    // button instead. Panels move by dragging their tab (to a side, beside another panel, out of
    // the window); View › Panels shows or hides them. The views in the middle have their own
    // tabs: no header over them.
    ads::CDockManager::setConfigFlags(ads::CDockManager::OpaqueSplitterResize | ads::CDockManager::DockAreaHideDisabledButtons |
                                      ads::CDockManager::HideSingleCentralWidgetTitleBar);
    docks_ = new ads::CDockManager(workspace_);
    docks_->installEventFilter(this); // the edge tabs follow its size
    connect(docks_, &ads::CDockManager::dockAreaCreated, this, &MainWindow::decorateArea);
    // The docking library's own style sheet beats the application's: the theme's colors go on top.
    const QString adsStyle = docks_->styleSheet();
    docks_->setStyleSheet(adsStyle + theme::dockStyleSheet());
    connect(&Appearance::get(), &Appearance::changed, this, [this, adsStyle] { docks_->setStyleSheet(adsStyle + theme::dockStyleSheet()); });

    library_ = new LibraryPanel(*ctx_);
    hierarchy_ = new HierarchyPanel(*ctx_);
    files_ = new FilesPanel(*ctx_);
    inspector_ = new InspectorPanel(*ctx_);

    views_ = new QTabWidget;
    views_->setDocumentMode(true);
    viewport_ = new Viewport(*ctx_, vulkan_);
    views_->addTab(viewport_, makeIcon(Icon::Image, theme::kTextDim, 32), tr("Scene"));
    nodes_ = new NodesTab(*ctx_);
    views_->addTab(nodes_, makeIcon(Icon::Graph, theme::kTextDim, 32), tr("Nodes"));
    script_ = new ScriptTab(*ctx_);
    views_->addTab(script_, makeIcon(Icon::Code, theme::kTextDim, 32), tr("Scripts"));
    views_->setTabToolTip(SceneView, tr("Place and arrange the scene's objects (Ctrl+1)"));
    views_->setTabToolTip(NodesView, tr("The selected object's node graph, or the scene's (Ctrl+2)"));
    views_->setTabToolTip(ScriptView, tr("Every Lua script of the project (Ctrl+3)"));
    // In the Nodes view the library lists the nodes. The Scripts view is the project's, not an
    // object's: the library's area steps aside (hidden, not closed: the layout itself does not
    // change, so the views' tabs and the panels stay as they were). Sharing its area with other
    // panels, it only rests.
    connect(views_, &QTabWidget::currentChanged, this, [this](int i) {
        library_->setMode(i == NodesView ? LibraryPanel::Mode::Nodes : i == ScriptView ? LibraryPanel::Mode::Scripts : LibraryPanel::Mode::Objects);
        showLibraryArea(i != ScriptView);
    });
    library_->onAddNode = [this](const std::string& type) {
        if (!ctx_->graph()) {
            ctx_->checkpoint();
            ctx_->graph() = NodeGraph{};
            ctx_->changed();
        }
        showView(NodesView);
        nodes_->canvas()->addNode(type);
    };
    library_->onAddPreset = [this](const std::string& preset) {
        showView(NodesView); // the canvas makes the graph (and the object, if none is selected)
        nodes_->canvas()->addPreset(preset);
    };
    files_->onOpenScript = [this](const std::filesystem::path& file) {
        showView(ScriptView);
        script_->openFile(file);
    };

    auto dock = [&](const char* name, QWidget* w) {
        auto* d = new ads::CDockWidget(docks_, tr(name)); // the object name stays English: saved layouts know panels by it
        d->setObjectName(name);
        d->setWidget(w, ads::CDockWidget::ForceNoScrollArea);
        d->setFeature(ads::CDockWidget::DockWidgetFloatable, true);
        dockWidgets_[name] = d;
        return d;
    };
    auto* views = dock(QT_TR_NOOP("Views"), views_);
    views->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    views->setFeature(ads::CDockWidget::DockWidgetMovable, false);
    views->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    ads::CDockAreaWidget* center = docks_->setCentralWidget(views);
    ads::CDockWidget* library = dock(QT_TR_NOOP("Objects"), library_);
    connect(library_, &LibraryPanel::titleChanged, library, [library](const QString& title) { library->setWindowTitle(title); });
    docks_->addDockWidget(ads::LeftDockWidgetArea, library);
    ads::CDockAreaWidget* right = docks_->addDockWidget(ads::RightDockWidgetArea, dock(QT_TR_NOOP("Hierarchy"), hierarchy_));
    docks_->addDockWidgetTabToArea(dock(QT_TR_NOOP("Files"), files_), right);
    docks_->addDockWidget(ads::BottomDockWidgetArea, dock(QT_TR_NOOP("Inspector"), inspector_), right);
    docks_->addDockWidget(ads::BottomDockWidgetArea, dock(QT_TR_NOOP("Console"), new ConsoleTab(*ctx_)), center);
    dockWidgets_["Hierarchy"]->setAsCurrentTab();
    // A panel moved, docked or floated: its header's buttons fit where it is now.
    auto later = [this] { QTimer::singleShot(0, this, [this] { updateAreaButtons(); }); };
    connect(docks_, &ads::CDockManager::dockAreasAdded, this, later);
    connect(docks_, &ads::CDockManager::dockAreasRemoved, this, later);
    connect(docks_, &ads::CDockManager::stateRestored, this, later);
    for (const auto& [name, d] : dockWidgets_) connect(d, &ads::CDockWidget::topLevelChanged, this, later);
    later();
}

void MainWindow::decorateArea(ads::CDockAreaWidget* area) {
    // At the right of each panel header: minimize (to a tab on the window's edge).
    ads::CDockAreaTitleBar* bar = area->titleBar();
    if (!bar) return;
    auto* min = new IconButton(Icon::Minimize, tr("Minimize panel"), 22);
    min->setObjectName("artisanMinimize");
    QPointer<ads::CDockAreaWidget> guard(area);
    connect(min, &QAbstractButton::clicked, this, [this, guard] {
        if (guard) QTimer::singleShot(0, this, [this, guard] {
                if (guard) minimizeArea(guard);
            });
    });
    const int at = bar->indexOf(bar->button(ads::TitleBarButtonTabsMenu));
    bar->insertWidget(at < 0 ? bar->layout()->count() : at, min);
}

void MainWindow::updateAreaButtons() {
    for (ads::CDockAreaWidget* a : docks_->openedDockAreas()) {
        ads::CDockAreaTitleBar* bar = a->titleBar();
        auto* min = bar ? bar->findChild<IconButton*>("artisanMinimize", Qt::FindDirectChildrenOnly) : nullptr;
        if (!min) continue;
        // The views stay; a floating panel is a window of its own.
        const bool regular = !a->isCentralWidgetArea() && a->dockContainer() == docks_;
        if (min->isVisible() != regular) min->setVisible(regular);
    }
}

int MainWindow::edgeOf(ads::CDockAreaWidget* area, float* along) const {
    // The window edge a panel minimizes to: the side of the views it sits on.
    auto rectIn = [this](QWidget* w) { return QRect(w->mapTo(docks_, QPoint(0, 0)), w->size()); };
    const QRect r = rectIn(area);
    QRect center = docks_->rect();
    const auto views = dockWidgets_.find("Views");
    if (views != dockWidgets_.end() && views->second->dockAreaWidget()) center = rectIn(views->second->dockAreaWidget());
    int edge = 2;
    if (r.right() <= center.left() + 4) edge = 0;
    else if (r.left() >= center.right() - 4) edge = 1;
    else if (r.top() >= center.bottom() - 4) edge = 3;
    if (along)
        *along = edge < 2 ? static_cast<float>(r.center().y()) / std::max(1, docks_->height())
                          : static_cast<float>(r.center().x()) / std::max(1, docks_->width());
    return edge;
}

void MainWindow::minimizeArea(ads::CDockAreaWidget* area) {
    if (!area || area->isCentralWidgetArea()) return;
    Minimized m;
    m.edge = edgeOf(area, &m.along);
    m.current = area->currentDockWidget();
    m.size = m.edge < 2 ? area->width() : area->height();
    const QList<ads::CDockWidget*> open = area->openedDockWidgets();
    m.docks.assign(open.begin(), open.end());
    QStringList names;
    for (ads::CDockWidget* d : m.docks) names << d->windowTitle();
    auto* tab = new EdgeTab(m.edge, names.join("  ·  "), docks_);
    tab->setToolTip(tr("Show %1 again").arg(names.join(", ")));
    m.tab = tab;
    connect(tab, &QAbstractButton::clicked, this, [this, tab] {
        for (size_t i = 0; i < minimized_.size(); ++i)
            if (minimized_[i].tab == tab) return restoreMinimized(i);
    });
    minimized_.push_back(m);
    for (ads::CDockWidget* d : m.docks) d->toggleView(false);
    placeEdgeTabs();
    tab->show();
}

void MainWindow::restoreMinimized(size_t index) {
    if (index >= minimized_.size()) return;
    const Minimized m = minimized_[index];
    minimized_.erase(minimized_.begin() + static_cast<std::ptrdiff_t>(index));
    m.tab->deleteLater();
    for (ads::CDockWidget* d : m.docks) d->toggleView(true);
    if (m.current) {
        m.current->setAsCurrentTab();
        // Back at the size it had (hidden, its place shrank to nothing): the space comes from the
        // largest neighbor (usually the views).
        auto resize = [](ads::CDockAreaWidget* area, int size) {
            auto* splitter = qobject_cast<QSplitter*>(area->parentWidget());
            if (!splitter || size <= 0) return;
            QList<int> sizes = splitter->sizes();
            const int i = splitter->indexOf(area);
            if (i < 0 || i >= sizes.size()) return;
            int from = -1;
            for (int k = 0; k < sizes.size(); ++k)
                if (k != i && (from < 0 || sizes[k] > sizes[from])) from = k;
            if (from < 0) return;
            const int give = std::clamp(size - sizes[i], -sizes[i], std::max(0, sizes[from] - 80));
            sizes[i] += give;
            sizes[from] -= give;
            splitter->setSizes(sizes);
        };
        if (ads::CDockAreaWidget* area = m.current->dockAreaWidget()) {
            resize(area, m.size);
            QPointer<ads::CDockAreaWidget> guard(area);
            QTimer::singleShot(0, this, [guard, resize, size = m.size] {
                if (guard) resize(guard, size);
            });
        }
    }
    placeEdgeTabs();
}

void MainWindow::restoreAllMinimized() {
    while (!minimized_.empty()) restoreMinimized(0);
}

void MainWindow::placeEdgeTabs() {
    // Where each panel was along its edge; tabs on the same edge do not overlap. An edge with tabs
    // keeps a strip of its own: the scene view is a native window, which nothing can cover.
    bool used[4] = {false, false, false, false};
    for (const Minimized& m : minimized_) used[m.edge] = true;
    constexpr int strip = 28;
    docks_->setContentsMargins(used[0] ? strip : 0, used[2] ? strip : 0, used[1] ? strip : 0, used[3] ? strip : 0);
    const QRect area = docks_->rect();
    std::vector<QRect> placed;
    for (const Minimized& m : minimized_) {
        const QSize size = m.tab->size();
        QPoint at = m.edge < 2 ? QPoint(m.edge == 0 ? area.left() + 2 : area.right() - size.width() - 1,
                                        std::clamp(static_cast<int>(area.height() * m.along) - size.height() / 2, 4,
                                                   std::max(4, area.height() - size.height() - 4)))
                               : QPoint(std::clamp(static_cast<int>(area.width() * m.along) - size.width() / 2, 4,
                                                   std::max(4, area.width() - size.width() - 4)),
                                        m.edge == 2 ? area.top() + 2 : area.bottom() - size.height() - 1);
        QRect r(at, size);
        for (bool moved = true; moved;) {
            moved = false;
            for (const QRect& other : placed)
                if (other.intersects(r.adjusted(-4, -4, 4, 4))) {
                    if (m.edge < 2) r.moveTop(other.bottom() + 8);
                    else r.moveLeft(other.right() + 8);
                    moved = true;
                }
        }
        placed.push_back(r);
        m.tab->move(r.topLeft());
        m.tab->raise();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* e) {
    if (watched == docks_ && e->type() == QEvent::Resize) placeEdgeTabs();
    return QMainWindow::eventFilter(watched, e);
}

void MainWindow::placeDefaultLayout() {
    // Once, with the window at its size: the library narrow on the left, the views wide, the
    // project's panels on the right; then the user's own layout, if one was saved.
    if (!defaultLayout_.isEmpty()) return;
    const int w = docks_->width(), h = docks_->height();
    if (ads::CDockAreaWidget* a = dockWidgets_["Objects"]->dockAreaWidget()) docks_->setSplitterSizes(a, {w * 19 / 100, w * 57 / 100, w * 24 / 100});
    if (ads::CDockAreaWidget* a = dockWidgets_["Console"]->dockAreaWidget()) docks_->setSplitterSizes(a, {h * 78 / 100, h * 22 / 100});
    if (ads::CDockAreaWidget* a = dockWidgets_["Inspector"]->dockAreaWidget()) docks_->setSplitterSizes(a, {h * 38 / 100, h * 62 / 100});
    defaultLayout_ = docks_->saveState();
    const std::string& saved = settings().dockLayout;
    if (saved.empty()) return;
    const QByteArray state = QByteArray::fromBase64(QByteArray::fromStdString(saved));
    // A layout from before a panel existed would lose it: only a complete one comes back.
    bool complete = true;
    for (const auto& [name, d] : dockWidgets_) complete = complete && state.contains(QByteArray("\"") + name.c_str() + "\"");
    if (complete) docks_->restoreState(state);
}

void MainWindow::showLibraryArea(bool show) {
    if (!show) {
        ads::CDockWidget* lib = dockWidgets_["Objects"];
        ads::CDockAreaWidget* area = lib ? lib->dockAreaWidget() : nullptr;
        if (!area || lib->isClosed() || area->dockContainer() != docks_ || area->openDockWidgetsCount() != 1) return;
        hiddenLibraryArea_ = area;
        area->hide();
    } else if (hiddenLibraryArea_) {
        hiddenLibraryArea_->show();
        hiddenLibraryArea_ = nullptr;
    }
}

void MainWindow::saveLayout() {
    if (defaultLayout_.isEmpty()) return;
    restoreAllMinimized(); // minimized panels come back with the next project
    showLibraryArea(true);
    settings().dockLayout = docks_->saveState().toBase64().toStdString();
    saveSettings();
}

void MainWindow::resetLayout() {
    restoreAllMinimized();
    showLibraryArea(true);
    if (!defaultLayout_.isEmpty()) docks_->restoreState(defaultLayout_);
    for (auto& [name, d] : dockWidgets_) d->toggleView(true);
    showLibraryArea(views_->currentIndex() != ScriptView);
}

void MainWindow::showView(int index) {
    views_->setCurrentIndex(index);
    ads::CDockWidget* d = dockWidgets_["Views"];
    if (d && !d->isVisible()) d->toggleView(true);
}

// --- menus -------------------------------------------------------------------------------------

QAction* MainWindow::action(const char* id, const QString& text, std::function<void()> run) {
    auto* a = new QAction(text, this);
    // Also while the viewport (a native window inside this one) or a floating panel has the keyboard.
    a->setShortcutContext(Qt::ApplicationShortcut);
    a->setAutoRepeat(false);
    connect(a, &QAction::triggered, this, [run] { run(); });
    addAction(a);
    actions_[id] = a;
    return a;
}

void MainWindow::applyShortcuts() {
    for (auto& [id, a] : actions_) a->setShortcut(keysOf(id));
}

void MainWindow::buildMenus() {
    QMenuBar* bar = menuBar();
    // The mark: back to the projects.
    auto* home = new LogoButton(30);
    home->setToolTip(tr("Back to projects"));
    connect(home, &QAbstractButton::clicked, this, [this] {
        if (confirmClose()) showHome();
    });
    auto* homeCorner = new QWidget;
    auto* homeRow = new QHBoxLayout(homeCorner);
    homeRow->setContentsMargins(6, 1, 2, 1);
    homeRow->addWidget(home, 0, Qt::AlignVCenter);
    bar->setCornerWidget(homeCorner, Qt::TopLeftCorner);

    QMenu* file = bar->addMenu(tr("File"));
    file->addAction(action("save", tr("Save"), [this] { save(); }));
    file->addAction(action("new_scene", tr("New Scene"), [this] { navigator_->addScene(); }));
    file->addAction(action("export", tr("Export Game..."), [this] { openExportDialog(*ctx_, this); }));
    file->addSeparator();
    file->addAction(action("show_folder", tr("Show Project Folder"), [this] { platform::revealInFileManager(ctx_->dir / kProjectFile); }));
    file->addAction(action("close_project", tr("Close Project"), [this] {
        if (confirmClose()) showHome();
    }));
    file->addSeparator();
    file->addAction(action("settings", tr("Settings..."), [this] { openSettings(this); }));
    file->addAction(action("quit", tr("Quit"), [this] { close(); }));

    QMenu* edit = bar->addMenu(tr("Edit"));
    edit->addAction(action("undo", tr("Undo"), [this] { ctx_->undo(); }));
    edit->addAction(action("redo", tr("Redo"), [this] { ctx_->redo(); }));
    edit->addSeparator();
    edit->addAction(action("duplicate", tr("Duplicate"), [this] {
        if (views_->currentIndex() == NodesView) nodes_->canvas()->duplicateSelected();
        else hierarchy_->duplicateSelected();
    }));
    edit->addAction(action("delete", tr("Delete"), [this] {
        if (views_->currentIndex() == SceneView) hierarchy_->deleteSelected();
        else if (views_->currentIndex() == NodesView) nodes_->canvas()->removeSelected();
    }));
    edit->addAction(action("select_all", tr("Select All Nodes"), [this] {
        if (views_->currentIndex() == NodesView) nodes_->canvas()->selectAll();
    }));
    QAction* deselect = action("deselect", tr("Select the Scene"), [this] { ctx_->select(0); });
    deselect->setShortcutContext(Qt::WindowShortcut); // Esc in a dialog closes the dialog
    edit->addAction(deselect);
    edit->addSeparator();
    edit->addAction(action("back", tr("Back"), [this] { ctx_->back(); }));
    edit->addAction(action("forward", tr("Forward"), [this] { ctx_->forward(); }));

    QMenu* add = bar->addMenu(tr("Add"));
    for (const ObjectKind& k : objectKinds()) {
        const std::string kind = k.kind;
        add->addAction(menuIcon(objectKindIcon(kind), theme::kTextDim), tr(k.name), this, [this, kind] { addObject(kind, *ctx_, this); });
    }

    QMenu* view = bar->addMenu(tr("View"));
    view->addAction(action("view_scene", tr("Scene"), [this] { showView(SceneView); }));
    view->addAction(action("view_nodes", tr("Nodes"), [this] { showView(NodesView); }));
    view->addAction(action("view_scripts", tr("Scripts"), [this] { showView(ScriptView); }));
    view->addSeparator();
    QMenu* panels = view->addMenu(tr("Panels"));
    for (const char* name : {"Objects", "Hierarchy", "Files", "Inspector", "Console"}) panels->addAction(dockWidgets_[name]->toggleViewAction());
    view->addAction(action("reset_layout", tr("Reset Layout"), [this] { resetLayout(); }));
    view->addSeparator();
    view->addAction(action("fit_view", tr("Fit the View"), [this] {
        if (views_->currentIndex() == NodesView) nodes_->canvas()->frameAll();
        else viewport_->fit();
    }));

    QMenu* game = bar->addMenu(tr("Game"));
    game->addAction(action("play", tr("Play"), [this] { play(); }));
    game->addAction(action("stop", tr("Stop"), [this] { stop(); }));

    pluginsMenu_ = bar->addMenu(tr("Plugins"));
    buildPluginsMenu();

    QMenu* help = bar->addMenu(tr("Help"));
    help->addAction(action("help", tr("Getting Started"), [this] { openHelp(this, HelpPage::Start, viewport_->deviceName()); }));
    help->addAction(tr("Keyboard Shortcuts"), this, [this] { openHelp(this, HelpPage::Shortcuts, viewport_->deviceName()); });
    help->addAction(tr("Scripting"), this, [this] { openHelp(this, HelpPage::Scripting, viewport_->deviceName()); });
    help->addSeparator();
    help->addAction(menuIcon(Icon::Coffee, theme::kTextDim), tr("Support Artisan..."), this, [] { platform::openWithDefaultApp(kSupportUrl); });
    help->addAction(tr("About Artisan"), this, [this] { openHelp(this, HelpPage::About, viewport_->deviceName()); });

    // Right side: messages, Settings, Play, Export.
    auto* corner = new QWidget;
    auto* row = new QHBoxLayout(corner);
    row->setContentsMargins(0, 2, 8, 2);
    row->setSpacing(6);
    status_ = new QLabel;
    setTone(status_, Tone::TextDim);
    row->addWidget(status_);
    row->addSpacing(4);
    auto* gear = new IconButton(Icon::Gear, tr("Settings (Ctrl+,)"), 28);
    connect(gear, &QAbstractButton::clicked, this, [this] { openSettings(this); });
    row->addWidget(gear);
    row->addSpacing(2);
    playButton_ = new QPushButton(tr("Play"));
    playButton_->setIcon(makeIcon(Icon::Play, theme::kText, 20));
    playButton_->setIconSize(QSize(20, 20));
    playButton_->setFixedHeight(28);
    playButton_->setToolTip(tr("Run the game in its own window (F5)"));
    connect(playButton_, &QPushButton::clicked, this, [this] {
        if (player_) stop();
        else play();
    });
    row->addWidget(playButton_);
    auto* exportButton = accentButton(tr("Export"));
    exportButton->setIcon(makeIcon(Icon::Export, theme::kOnAccent, 20));
    exportButton->setIconSize(QSize(20, 20));
    exportButton->setFixedHeight(28);
    exportButton->setToolTip(tr("Export the game for Windows, Linux, macOS or Android (Ctrl+E)"));
    connect(exportButton, &QPushButton::clicked, this, [this] { openExportDialog(*ctx_, this); });
    // Its icon is drawn in the text color on the accent, which changes with the theme.
    connect(&Appearance::get(), &Appearance::changed, exportButton, [exportButton] { exportButton->setIcon(makeIcon(Icon::Export, theme::kOnAccent, 20)); });
    row->addWidget(exportButton);
    bar->setCornerWidget(corner, Qt::TopRightCorner);
}

void MainWindow::buildPluginsMenu() {
    if (!pluginsMenu_) return;
    pluginsMenu_->clear();
    std::string group;
    for (const PluginCommand* c : PluginHost::get().commands()) {
        if (c->pluginId != group) {
            if (!group.empty()) pluginsMenu_->addSeparator();
            group = c->pluginId;
            if (const Plugin* p = PluginHost::get().find(group)) pluginsMenu_->addSection(QString::fromStdString(p->name));
        }
        QAction* a = pluginsMenu_->addAction(menuIcon(pluginIcon(c->icon), theme::kTextDim), QString::fromStdString(c->name), this,
                                             [this, id = c->pluginId, name = c->name] { runPluginCommand(id, name); });
        a->setToolTip(QString::fromStdString(c->tip));
    }
    if (pluginsMenu_->isEmpty()) pluginsMenu_->addAction(tr("No plugin commands"))->setEnabled(false);
    pluginsMenu_->addSeparator();
    pluginsMenu_->addAction(menuIcon(Icon::Plugin, theme::kTextDim), tr("Manage Plugins..."), this, [this] { openSettings(this, SettingsPage::Plugins); });
    pluginsMenu_->setToolTipsVisible(true);
}

void MainWindow::runPluginCommand(const std::string& pluginId, const std::string& name) {
    if (stack_->currentWidget() != workspace_) return;
    const PluginCommand* command = nullptr;
    for (const PluginCommand* c : PluginHost::get().commands())
        if (c->pluginId == pluginId && c->name == name) command = c;
    if (!command) return;
    // On a copy: kept (one undo step) only if the command ran and changed something.
    Scene copy = ctx_->scene();
    EntityId selection = ctx_->selection();
    std::string error;
    if (!PluginHost::get().runCommand(*command, copy, selection, &error)) {
        ctx_->log(tr("error: %1 (%2)").arg(QString::fromStdString(error), QString::fromStdString(name)));
        status_->setText(tr("%1 failed: see the Console").arg(QString::fromStdString(name)));
        QTimer::singleShot(3000, status_, [this] { status_->clear(); });
        return;
    }
    if (sceneToJson(copy) != sceneToJson(ctx_->scene())) {
        ctx_->checkpoint();
        ctx_->scene() = std::move(copy);
        ctx_->changed();
    }
    if (selection != ctx_->selection()) ctx_->select(selection);
    if (const Plugin* p = PluginHost::get().find(pluginId); p && !p->log.empty()) ctx_->log(QString::fromStdString(p->name + ": " + p->log.back()));
}

// --- the project -------------------------------------------------------------------------------

void MainWindow::showHome() {
    stop();
    if (stack_->currentWidget() == workspace_) {
        saveLayout();
        dropRecoveryCopy(); // closed on purpose: nothing to recover
    }
    menuBar()->setVisible(false);
    stack_->setCurrentWidget(home_);
    home_->refresh();
    setWindowTitle("Artisan");
}

std::filesystem::path MainWindow::recoveryFile() const {
    // One file per project folder, named by a hash of its path.
    const size_t h = std::hash<std::string>{}(platform::utf8(ctx_->dir));
    return platform::appDataDir() / "recovery" / (QString::number(static_cast<qulonglong>(h), 16).toStdString() + ".artisan");
}

void MainWindow::keepRecoveryCopy() {
    if (stack_->currentWidget() != workspace_ || !ctx_->dirty() || ctx_->dir.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(recoveryFile().parent_path(), ec);
    platform::writeFile(recoveryFile(), projectToJson(ctx_->project));
}

void MainWindow::dropRecoveryCopy() {
    if (ctx_->dir.empty()) return;
    std::error_code ec;
    std::filesystem::remove(recoveryFile(), ec);
}

bool MainWindow::openProject(const std::filesystem::path& dir) {
    Project p;
    std::string error;
    if (!loadProject(dir, p, &error)) {
        home_->setError(QString::fromStdString(error));
        showHome();
        return false;
    }
    addRecent(dir);
    ctx_->reset(std::move(p), dir);
    menuBar()->setVisible(true);
    stack_->setCurrentWidget(workspace_);
    showView(SceneView);
    updateTitle();
    QTimer::singleShot(0, this, [this] { placeDefaultLayout(); });
    ctx_->log(tr("Opened %1").arg(platform::toQt(dir)));

    // Changes kept aside when Artisan last closed without saving them: offered back.
    std::error_code ec;
    const auto kept = recoveryFile();
    if (std::filesystem::exists(kept, ec)) {
        const auto keptTime = std::filesystem::last_write_time(kept, ec);
        const auto savedTime = std::filesystem::last_write_time(dir / kProjectFile, ec);
        Project recovered;
        const auto text = platform::readFile(kept);
        if (keptTime > savedTime && text && projectFromJson(*text, recovered)) {
            const QString when = QFileInfo(platform::toQt(kept)).lastModified().toString(QLocale().dateTimeFormat(QLocale::ShortFormat));
            const auto answer = QMessageBox::question(this, tr("Recover Changes"),
                                                      tr("Artisan closed before \"%1\" was saved (%2). Bring the unsaved changes back?")
                                                          .arg(QString::fromStdString(ctx_->project.name), when),
                                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (answer == QMessageBox::Yes) {
                ctx_->reset(std::move(recovered), dir);
                ctx_->changed(); // unsaved until saved
                ctx_->log(tr("Recovered the unsaved changes"));
                return true;
            }
        }
        std::filesystem::remove(kept, ec);
    }
    return true;
}

void MainWindow::updateTitle() {
    if (stack_->currentWidget() != workspace_) return;
    setWindowTitle(QString("%1%2 — Artisan").arg(QString::fromStdString(ctx_->project.name), ctx_->dirty() ? " •" : ""));
}

void MainWindow::autosaveSoon() {
    if (settings().autosave && ctx_->dirty()) autosave_->start();
}

bool MainWindow::save(bool quiet) {
    if (!script_->saveIfChanged()) return false;
    std::string error;
    if (!ctx_->save(&error)) {
        if (quiet) {
            ctx_->log(tr("error: could not save (%1)").arg(QString::fromStdString(error)));
            return false;
        }
        QMessageBox::warning(this, tr("Save"), QString::fromStdString(error));
        return false;
    }
    status_->setText(quiet ? tr("Saved automatically") : tr("Saved"));
    QTimer::singleShot(1500, status_, [this] { status_->clear(); });
    return true;
}

bool MainWindow::confirmClose() {
    if (stack_->currentWidget() != workspace_) return true;
    script_->saveIfChanged();
    if (!ctx_->dirty()) return true;
    if (settings().autosave) return save(true) || QMessageBox::question(this, tr("Unsaved Changes"), tr("The project could not be saved. Close anyway?")) == QMessageBox::Yes;
    const auto answer = QMessageBox::question(this, tr("Unsaved Changes"), tr("Save the changes to \"%1\"?").arg(QString::fromStdString(ctx_->project.name)),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    return answer == QMessageBox::Discard || save();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!confirmClose()) return e->ignore();
    if (stack_->currentWidget() == workspace_) {
        saveLayout();
        dropRecoveryCopy();
    }
    // The docking library's floating panels are windows of their own: they go with this one.
    for (QWidget* w : QApplication::topLevelWidgets())
        if (w != this && w->isVisible()) w->close();
    e->accept();
}

void MainWindow::play() {
    if (stack_->currentWidget() != workspace_) return;
    stop();
    // The player runs the project as saved.
    if (settings().saveBeforePlay || ctx_->dirty()) {
        if (!save()) return;
    } else {
        script_->saveIfChanged();
    }
    const auto exe = platform::playerExecutable();
    std::error_code ec;
    if (!std::filesystem::exists(exe, ec)) {
        QMessageBox::warning(this, tr("Play"), tr("The player is missing: %1").arg(platform::toQt(exe)));
        return;
    }
    player_ = new QProcess(this);
    player_->setProcessChannelMode(QProcess::MergedChannels);
    connect(player_, &QProcess::readyRead, this, [this] {
        while (player_ && player_->canReadLine()) {
            const QString line = QString::fromUtf8(player_->readLine()).trimmed();
            ctx_->log(line);
            if (line.startsWith("error")) dockWidgets_["Console"]->toggleView(true);
        }
    });
    connect(player_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        ctx_->log(tr("The game stopped (%1)").arg(code));
        player_->deleteLater();
        player_ = nullptr;
        playButton_->setText(tr("Play"));
        playButton_->setIcon(makeIcon(Icon::Play, theme::kText, 20));
    });
    ctx_->log(tr("Playing %1").arg(QString::fromStdString(ctx_->project.name)));
    player_->start(platform::toQt(exe), {platform::toQt(ctx_->dir), "--preview"});
    playButton_->setText(tr("Stop"));
    playButton_->setIcon(makeIcon(Icon::Pause, theme::kText, 20));
}

void MainWindow::stop() {
    if (!player_) return;
    QProcess* p = player_;
    player_ = nullptr;
    p->disconnect(this);
    p->kill();
    p->waitForFinished(2000);
    p->deleteLater();
    if (playButton_) {
        playButton_->setText(tr("Play"));
        playButton_->setIcon(makeIcon(Icon::Play, theme::kText, 20));
    }
}

} // namespace artisan::editor

namespace artisan::editor {

void MainWindow::devCapture(const QString& dir) {
    // From the screen, so the Vulkan viewport (a native window) is in the pictures too.
    auto shoot = [dir](QWidget* w, const QString& name) {
        w->raise();
        w->activateWindow();
        QApplication::processEvents();
        const QRect r = w->frameGeometry();
        w->screen()->grabWindow(0, r.x(), r.y(), r.width(), r.height()).save(dir + "/" + name + ".png");
    };
    std::vector<std::pair<int, std::function<void()>>> steps;
    int t = 0;
    auto at = [&](int ms, std::function<void()> f) { steps.push_back({t += ms, std::move(f)}); };
    at(0, [dir] { // the application's icon, large
        QPixmap icon(256, 256);
        icon.fill(Qt::transparent);
        QPainter p(&icon);
        drawAppIcon(p, 256.0f);
        p.end();
        icon.save(dir + "/00-icon.png");
    });
    at(300, [this, shoot] { shoot(this, "01-scene"); });
    at(300, [this] {
        if (const Entity* e = ctx_->scene().findByName("Spinner")) ctx_->select(e->id);
    });
    at(500, [this, shoot] { shoot(this, "02-selected"); });
    at(300, [this] {
        addObject("player", *ctx_, this); // the library's player, with its graph
        showView(NodesView);
    });
    at(600, [this] { nodes_->canvas()->frameAll(); });
    at(400, [this, shoot] { shoot(this, "03-nodes"); });
    at(300, [this] {
        nodes_->canvas()->addPreset("wasd");
        nodes_->canvas()->addNode("script.lua");
        nodes_->canvas()->frameAll();
    });
    at(600, [this, shoot] { shoot(this, "03b-nodes-presets"); });
    at(300, [this] {
        if (const Entity* e = ctx_->scene().findByName("Player")) ctx_->select(e->id);
        showView(ScriptView);
    });
    at(500, [this, shoot] { shoot(this, "04-scripts"); });
    at(300, [this] { showView(NodesView); });
    at(500, [this, shoot] { shoot(this, "04b-scripts-to-nodes"); });
    at(300, [this] { showView(ScriptView); });
    at(300, [this] { showView(SceneView); });
    at(500, [this, shoot] { shoot(this, "04c-scripts-to-scene"); });
    at(300, [this] {
        ctx_->select(0); // the scene: the preset brings its own object
        library_->onAddPreset("wasd");
    });
    at(600, [this, shoot] { shoot(this, "04d-preset-on-scene"); });
    at(300, [this] {
        dockWidgets_["Files"]->setAsCurrentTab();
        ctx_->select(0);
        showView(SceneView);
    });
    at(600, [this, shoot] { shoot(this, "05-scene-files"); });
    at(300, [this] {
        runPluginCommand("level-tools", "Scatter Coins");
    });
    at(600, [this, shoot] { shoot(this, "05a-plugins-ran"); });
    at(300, [this] {
        if (ads::CDockAreaWidget* a = dockWidgets_["Inspector"]->dockAreaWidget()) minimizeArea(a);
        if (ads::CDockAreaWidget* a = dockWidgets_["Objects"]->dockAreaWidget()) minimizeArea(a);
    });
    at(600, [this, shoot] { shoot(this, "05b-minimized"); });
    at(300, [this] {
        restoreAllMinimized();
        dockWidgets_["Hierarchy"]->setAsCurrentTab();
        navigator_->addScene();
    });
    at(600, [this, shoot] { shoot(this, "06-new-scene"); });
    auto window = [&](const QString& name, std::function<QWidget*()> open) {
        at(300, [shoot, name, open] {
            QWidget* w = open();
            QTimer::singleShot(700, [shoot, name, w] { shoot(w, name); });
        });
        at(900, [] {});
    };
    window("07-settings", [this] { return openSettings(this); });
    window("07b-interface", [this] { return openSettings(this, SettingsPage::Interface); });
    window("07c-editor", [this] { return openSettings(this, SettingsPage::Editor); });
    window("07d-shortcuts", [this] { return openSettings(this, SettingsPage::Shortcuts); });
    window("07e-plugins", [this] {
        QWidget* w = openSettings(this, SettingsPage::Plugins);
        QTimer::singleShot(1400, w, &QWidget::close);
        return w;
    });
    window("08-help", [this] { return openHelp(this, HelpPage::Start, viewport_->deviceName()); });
    window("09-shortcuts", [this] { return openHelp(this, HelpPage::Shortcuts, viewport_->deviceName()); });
    window("09b-license", [this] {
        QWidget* w = openHelp(this, HelpPage::License, viewport_->deviceName());
        QTimer::singleShot(1400, w, &QWidget::close);
        return w;
    });
    at(300, [shoot] {
        QTimer::singleShot(700, [shoot] {
            if (QWidget* d = QApplication::activeModalWidget()) {
                shoot(d, "10-export");
                d->close();
            }
        });
    });
    at(0, [this] { openExportDialog(*ctx_, this); });
    at(1200, [this] {
        ctx_->reset(ctx_->project, ctx_->dir); // nothing unsaved to ask about
        showHome();
    });
    at(800, [this, shoot] { shoot(this, "11-home"); });
    at(300, [] { QApplication::quit(); });
    for (auto& [ms, f] : steps) QTimer::singleShot(ms, this, f);
}

} // namespace artisan::editor
