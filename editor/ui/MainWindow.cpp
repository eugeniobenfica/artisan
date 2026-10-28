#include "ui/MainWindow.h"

#include "app/Platform.h"
#include "app/ProjectIO.h"
#include "app/Settings.h"
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

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

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
    connect(ctx_, &EditorContext::sceneChanged, this, [this] { updateTitle(); });
    connect(ctx_, &EditorContext::saved, this, [this] { updateTitle(); });

    buildMenus();
    showHome();
}

MainWindow::~MainWindow() { stop(); }

// --- docks -------------------------------------------------------------------------------------

void MainWindow::buildDocks() {
    // As in bemotion: no close, undock or tabs-menu buttons on the panels' headers. Panels move by
    // dragging their tab (to a side, beside another panel, out of the window); View › Panels shows
    // or hides them. The views in the middle have their own tabs: no header over them.
    ads::CDockManager::setConfigFlags(ads::CDockManager::OpaqueSplitterResize | ads::CDockManager::DockAreaHideDisabledButtons |
                                      ads::CDockManager::HideSingleCentralWidgetTitleBar);
    docks_ = new ads::CDockManager(workspace_);
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
    views_->addTab(script_, makeIcon(Icon::Code, theme::kTextDim, 32), tr("Script"));
    // In the Nodes view the library lists the nodes.
    connect(views_, &QTabWidget::currentChanged, this, [this](int i) {
        library_->setMode(i == NodesView ? LibraryPanel::Mode::Nodes : LibraryPanel::Mode::Objects);
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

void MainWindow::saveLayout() {
    if (defaultLayout_.isEmpty()) return;
    settings().dockLayout = docks_->saveState().toBase64().toStdString();
    saveSettings();
}

void MainWindow::showView(int index) {
    views_->setCurrentIndex(index);
    ads::CDockWidget* d = dockWidgets_["Views"];
    if (d && !d->isVisible()) d->toggleView(true);
}

// --- menus -------------------------------------------------------------------------------------

QAction* MainWindow::action(const QString& text, const QKeySequence& key, std::function<void()> run) {
    auto* a = new QAction(text, this);
    if (!key.isEmpty()) a->setShortcut(key);
    // Also while the viewport (a native window inside this one) or a floating panel has the keyboard.
    a->setShortcutContext(Qt::ApplicationShortcut);
    a->setAutoRepeat(false);
    connect(a, &QAction::triggered, this, [run] { run(); });
    addAction(a);
    return a;
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
    file->addAction(action(tr("Save"), QKeySequence::Save, [this] { save(); }));
    file->addAction(action(tr("New Scene"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), [this] { navigator_->addScene(); }));
    file->addAction(action(tr("Export Game..."), QKeySequence(Qt::CTRL | Qt::Key_E), [this] { openExportDialog(*ctx_, this); }));
    file->addSeparator();
    file->addAction(action(tr("Show Project Folder"), {}, [this] { platform::revealInFileManager(ctx_->dir / kProjectFile); }));
    file->addAction(action(tr("Close Project"), {}, [this] {
        if (confirmClose()) showHome();
    }));
    file->addSeparator();
    file->addAction(action(tr("Settings..."), QKeySequence(Qt::CTRL | Qt::Key_Comma), [this] { openSettings(this); }));
    file->addAction(action(tr("Quit"), QKeySequence::Quit, [this] { close(); }));

    QMenu* edit = bar->addMenu(tr("Edit"));
    edit->addAction(action(tr("Undo"), QKeySequence::Undo, [this] { ctx_->undo(); }));
    edit->addAction(action(tr("Redo"), QKeySequence(Qt::CTRL | Qt::Key_Y), [this] { ctx_->redo(); }));
    edit->addSeparator();
    edit->addAction(action(tr("Duplicate"), QKeySequence(Qt::CTRL | Qt::Key_D), [this] { hierarchy_->duplicateSelected(); }));
    edit->addAction(action(tr("Delete"), QKeySequence::Delete, [this] {
        // The node editor deletes its own nodes.
        if (views_->currentIndex() == SceneView) hierarchy_->deleteSelected();
    }));
    QAction* deselect = action(tr("Select the Scene"), QKeySequence(Qt::Key_Escape), [this] { ctx_->select(0); });
    deselect->setShortcutContext(Qt::WindowShortcut); // Esc in a dialog closes the dialog
    edit->addAction(deselect);
    edit->addSeparator();
    edit->addAction(action(tr("Back"), QKeySequence(Qt::ALT | Qt::Key_Left), [this] { ctx_->back(); }));
    edit->addAction(action(tr("Forward"), QKeySequence(Qt::ALT | Qt::Key_Right), [this] { ctx_->forward(); }));

    QMenu* add = bar->addMenu(tr("Add"));
    for (const ObjectKind& k : objectKinds()) {
        const std::string kind = k.kind;
        add->addAction(tr(k.name), this, [this, kind] { addObject(kind, *ctx_, this); });
    }

    QMenu* view = bar->addMenu(tr("View"));
    view->addAction(action(tr("Scene"), QKeySequence(Qt::CTRL | Qt::Key_1), [this] { showView(SceneView); }));
    view->addAction(action(tr("Nodes"), QKeySequence(Qt::CTRL | Qt::Key_2), [this] { showView(NodesView); }));
    view->addAction(action(tr("Script"), QKeySequence(Qt::CTRL | Qt::Key_3), [this] { showView(ScriptView); }));
    view->addSeparator();
    QMenu* panels = view->addMenu(tr("Panels"));
    for (const char* name : {"Objects", "Hierarchy", "Files", "Inspector", "Console"}) panels->addAction(dockWidgets_[name]->toggleViewAction());
    view->addAction(action(tr("Reset Layout"), {}, [this] {
        if (!defaultLayout_.isEmpty()) docks_->restoreState(defaultLayout_);
    }));
    view->addSeparator();
    view->addAction(action(tr("Fit the View"), {}, [this] { viewport_->fit(); }));

    QMenu* game = bar->addMenu(tr("Game"));
    game->addAction(action(tr("Play"), QKeySequence(Qt::Key_F5), [this] { play(); }));
    game->addAction(action(tr("Stop"), QKeySequence(Qt::SHIFT | Qt::Key_F5), [this] { stop(); }));

    QMenu* help = bar->addMenu(tr("Help"));
    help->addAction(action(tr("Getting Started"), QKeySequence(Qt::Key_F1), [this] { openHelp(this, HelpPage::Start, viewport_->deviceName()); }));
    help->addAction(action(tr("Keyboard Shortcuts"), {}, [this] { openHelp(this, HelpPage::Shortcuts, viewport_->deviceName()); }));
    help->addAction(action(tr("Scripting"), {}, [this] { openHelp(this, HelpPage::Scripting, viewport_->deviceName()); }));
    help->addSeparator();
    help->addAction(action(tr("About Artisan"), {}, [this] { openHelp(this, HelpPage::About, viewport_->deviceName()); }));

    // Right side: messages, Help, Settings, Play, Export.
    auto* corner = new QWidget;
    auto* row = new QHBoxLayout(corner);
    row->setContentsMargins(0, 2, 8, 2);
    row->setSpacing(8);
    status_ = new QLabel;
    setTone(status_, Tone::TextDim);
    row->addWidget(status_);
    auto* helpButton = new IconButton(Icon::Help, tr("Help (F1)"), 28);
    connect(helpButton, &QAbstractButton::clicked, this, [this] { openHelp(this, HelpPage::Start, viewport_->deviceName()); });
    row->addWidget(helpButton);
    auto* gear = new IconButton(Icon::Gear, tr("Settings (Ctrl+,)"), 28);
    connect(gear, &QAbstractButton::clicked, this, [this] { openSettings(this); });
    row->addWidget(gear);
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
    row->addWidget(exportButton);
    bar->setCornerWidget(corner, Qt::TopRightCorner);
}

// --- the project -------------------------------------------------------------------------------

void MainWindow::showHome() {
    stop();
    if (stack_->currentWidget() == workspace_) saveLayout();
    menuBar()->setVisible(false);
    stack_->setCurrentWidget(home_);
    home_->refresh();
    setWindowTitle("Artisan");
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
    return true;
}

void MainWindow::updateTitle() {
    if (stack_->currentWidget() != workspace_) return;
    setWindowTitle(QString("%1%2 — Artisan").arg(QString::fromStdString(ctx_->project.name), ctx_->dirty() ? " •" : ""));
}

bool MainWindow::save() {
    if (!script_->saveIfChanged()) return false;
    std::string error;
    if (!ctx_->save(&error)) {
        QMessageBox::warning(this, tr("Save"), QString::fromStdString(error));
        return false;
    }
    status_->setText(tr("Saved"));
    QTimer::singleShot(1500, status_, [this] { status_->clear(); });
    return true;
}

bool MainWindow::confirmClose() {
    if (stack_->currentWidget() != workspace_) return true;
    script_->saveIfChanged();
    if (!ctx_->dirty()) return true;
    const auto answer = QMessageBox::question(this, tr("Unsaved Changes"), tr("Save the changes to \"%1\"?").arg(QString::fromStdString(ctx_->project.name)),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    return answer == QMessageBox::Discard || save();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!confirmClose()) return e->ignore();
    if (stack_->currentWidget() == workspace_) saveLayout();
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
        if (const Entity* e = ctx_->scene().findByName("Player")) ctx_->select(e->id);
        showView(ScriptView);
    });
    at(500, [this, shoot] { shoot(this, "04-script"); });
    at(300, [this] {
        dockWidgets_["Files"]->setAsCurrentTab();
        ctx_->select(0);
        showView(SceneView);
    });
    at(600, [this, shoot] { shoot(this, "05-scene-files"); });
    at(300, [this] {
        dockWidgets_["Hierarchy"]->setAsCurrentTab();
        navigator_->addScene();
    });
    at(600, [this, shoot] { shoot(this, "06-new-scene"); });
    auto window = [&](const QString& name, std::function<QWidget*()> open) {
        at(300, [shoot, name, open] {
            QWidget* w = open();
            QTimer::singleShot(700, [shoot, name, w] {
                shoot(w, name);
                w->close();
            });
        });
        at(900, [] {});
    };
    window("07-settings", [this] { return openSettings(this); });
    window("08-help", [this] { return openHelp(this, HelpPage::Start, viewport_->deviceName()); });
    window("09-shortcuts", [this] { return openHelp(this, HelpPage::Shortcuts, viewport_->deviceName()); });
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
