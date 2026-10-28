#include "ui/MainWindow.h"

#include "app/Platform.h"
#include "app/ProjectIO.h"
#include "app/Settings.h"
#include "ui/EditorContext.h"
#include "ui/HomeScreen.h"
#include "ui/NodeEditor.h"
#include "ui/ObjectsPanel.h"
#include "ui/ScriptEditor.h"
#include "ui/SettingsWindow.h"
#include "ui/Viewport.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QScreen>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>

namespace artisan::editor {

using namespace ui;

MainWindow::MainWindow(QVulkanInstance* vulkan, QWidget* parent) : QMainWindow(parent), vulkan_(vulkan) {
    ctx_ = new EditorContext;
    ctx_->setParent(this);
    resize(1440, 900);
    setMinimumSize(900, 560);

    stack_ = new QStackedWidget;
    setCentralWidget(stack_);
    home_ = new HomeScreen;
    home_->onOpen = [this](const std::filesystem::path& dir) { openProject(dir); };
    home_->onSettings = [this] { openSettings(this, viewport_ ? viewport_->deviceName() : QString()); };
    stack_->addWidget(home_);

    // --- the workspace: objects on the left, the viewport (and its tabs) on the right ----------
    workspace_ = new QWidget;
    auto* row = new QHBoxLayout(workspace_);
    row->setContentsMargins(0, 0, 0, 0);
    auto* split = new QSplitter;
    split->setChildrenCollapsible(false);
    objects_ = new ObjectsPanel(*ctx_);
    objects_->setMinimumWidth(260);
    split->addWidget(objects_);
    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(true);
    viewport_ = new Viewport(*ctx_, vulkan_);
    tabs_->addTab(viewport_, makeIcon(Icon::Image, theme::kTextDim, 32), tr("Scene"));
    tabs_->addTab(new NodesTab(*ctx_), makeIcon(Icon::Graph, theme::kTextDim, 32), tr("Nodes"));
    script_ = new ScriptTab(*ctx_);
    tabs_->addTab(script_, makeIcon(Icon::Edit, theme::kTextDim, 32), tr("Script"));
    tabs_->addTab(new ConsoleTab(*ctx_), makeIcon(Icon::Text, theme::kTextDim, 32), tr("Console"));
    split->addWidget(tabs_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({320, 1100});
    row->addWidget(split);
    stack_->addWidget(workspace_);

    connect(ctx_, &EditorContext::editGraphRequested, this, [this] { tabs_->setCurrentIndex(1); });
    connect(ctx_, &EditorContext::editScriptRequested, this, [this] { tabs_->setCurrentIndex(2); });
    connect(ctx_, &EditorContext::sceneChanged, this, [this] { updateTitle(); });
    connect(ctx_, &EditorContext::saved, this, [this] { updateTitle(); });

    buildMenus();
    showHome();
}

MainWindow::~MainWindow() { stop(); }

QAction* MainWindow::action(const QString& text, const QKeySequence& key, std::function<void()> run) {
    auto* a = new QAction(text, this);
    if (!key.isEmpty()) a->setShortcut(key);
    // Also while the viewport (a native window inside this one) has the keyboard.
    a->setShortcutContext(Qt::ApplicationShortcut);
    a->setAutoRepeat(false);
    connect(a, &QAction::triggered, this, [run] { run(); });
    addAction(a);
    return a;
}

void MainWindow::buildMenus() {
    QMenuBar* bar = menuBar();
    // The mark: back to the projects.
    auto* home = new IconButton(Icon::Logo, tr("Back to projects"), 28);
    connect(home, &QAbstractButton::clicked, this, [this] {
        if (confirmClose()) showHome();
    });
    auto* homeCorner = new QWidget;
    auto* homeRow = new QHBoxLayout(homeCorner);
    homeRow->setContentsMargins(8, 2, 2, 2);
    homeRow->addWidget(home, 0, Qt::AlignVCenter);
    bar->setCornerWidget(homeCorner, Qt::TopLeftCorner);

    QMenu* file = bar->addMenu(tr("File"));
    file->addAction(action(tr("Save"), QKeySequence::Save, [this] { save(); }));
    file->addAction(action(tr("Export Game..."), QKeySequence(Qt::CTRL | Qt::Key_E), [this] { openExportDialog(*ctx_, this); }));
    file->addSeparator();
    file->addAction(action(tr("Show Project Folder"), {}, [this] { platform::revealInFileManager(ctx_->dir / kProjectFile); }));
    file->addAction(action(tr("Close Project"), {}, [this] {
        if (confirmClose()) showHome();
    }));
    file->addSeparator();
    file->addAction(action(tr("Settings..."), QKeySequence(Qt::CTRL | Qt::Key_Comma), [this] { openSettings(this, viewport_->deviceName()); }));
    file->addAction(action(tr("Quit"), QKeySequence::Quit, [this] { close(); }));

    QMenu* edit = bar->addMenu(tr("Edit"));
    edit->addAction(action(tr("Undo"), QKeySequence::Undo, [this] { ctx_->undo(); }));
    edit->addAction(action(tr("Redo"), QKeySequence(Qt::CTRL | Qt::Key_Y), [this] { ctx_->redo(); }));
    edit->addSeparator();
    edit->addAction(action(tr("Duplicate"), QKeySequence(Qt::CTRL | Qt::Key_D), [this] { objects_->duplicateSelected(); }));
    edit->addAction(action(tr("Delete"), QKeySequence::Delete, [this] {
        // The node editor deletes its own nodes.
        if (tabs_->currentIndex() == 0) objects_->deleteSelected();
    }));
    QAction* deselect = action(tr("Deselect"), QKeySequence(Qt::Key_Escape), [this] { ctx_->select(0); });
    deselect->setShortcutContext(Qt::WindowShortcut); // Esc in a dialog closes the dialog
    edit->addAction(deselect);

    QMenu* add = bar->addMenu(tr("Add"));
    const std::pair<const char*, const char*> kinds[] = {{"Rectangle", "rectangle"}, {"Rounded Rectangle", "rounded"}, {"Circle", "circle"},
                                                         {"Capsule", "capsule"},     {"Sprite...", "sprite"},       {"Light", "light"},
                                                         {"Empty Object", "empty"}};
    for (const auto& [name, kind] : kinds) {
        const std::string k = kind;
        add->addAction(tr(name), this, [this, k] { objects_->addObject(k); });
    }

    QMenu* game = bar->addMenu(tr("Game"));
    game->addAction(action(tr("Play"), QKeySequence(Qt::Key_F5), [this] { play(); }));
    game->addAction(action(tr("Stop"), QKeySequence(Qt::SHIFT | Qt::Key_F5), [this] { stop(); }));
    game->addSeparator();
    game->addAction(action(tr("Fit the View"), {}, [this] { viewport_->fit(); }));

    QMenu* help = bar->addMenu(tr("Help"));
    help->addAction(tr("Documentation"), this, [] { platform::openWithDefaultApp("https://github.com/eugeniobenfica/artisan#readme"); });
    help->addAction(tr("Scripting Reference"), this, [] { platform::openWithDefaultApp(platform::toQt(std::filesystem::path(ARTISAN_SOURCE_DIR) / "docs" / "SCRIPTING.md")); });

    // Right side: messages, Settings, Play, Export.
    auto* corner = new QWidget;
    auto* row = new QHBoxLayout(corner);
    row->setContentsMargins(0, 2, 8, 2);
    row->setSpacing(8);
    status_ = new QLabel;
    setTone(status_, Tone::TextDim);
    row->addWidget(status_);
    auto* gear = new IconButton(Icon::Gear, tr("Settings (Ctrl+,)"), 28);
    connect(gear, &QAbstractButton::clicked, this, [this] { openSettings(this, viewport_->deviceName()); });
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

void MainWindow::showHome() {
    stop();
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
    tabs_->setCurrentIndex(0);
    updateTitle();
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
    if (confirmClose()) e->accept();
    else e->ignore();
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
            if (line.startsWith("error")) tabs_->setCurrentIndex(3);
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
    at(0, [this, shoot] { shoot(this, "1-scene"); });
    at(400, [this] {
        if (const Entity* e = ctx_->scene().findByName("Spinner")) ctx_->select(e->id);
    });
    at(600, [this, shoot] { shoot(this, "2-selected"); });
    for (int tab = 1; tab < 4; ++tab) {
        at(300, [this, tab] { tabs_->setCurrentIndex(tab); });
        at(500, [this, shoot, tab] { shoot(this, QString("%1-tab%2").arg(2 + tab).arg(tab)); });
    }
    at(300, [this] {
        if (const Entity* e = ctx_->scene().findByName("Player")) ctx_->select(e->id);
        tabs_->setCurrentIndex(2);
    });
    at(500, [this, shoot] { shoot(this, "6-script"); });
    auto dialog = [&](const QString& name, std::function<void()> open) {
        at(300, [shoot, name, open] {
            QTimer::singleShot(700, [shoot, name] {
                if (QWidget* d = QApplication::activeModalWidget()) {
                    shoot(d, name);
                    d->close();
                }
            });
            open();
        });
    };
    dialog("7-settings", [this] { openSettings(this, viewport_->deviceName()); });
    dialog("8-export", [this] { openExportDialog(*ctx_, this); });
    at(1200, [this] {
        ctx_->reset(ctx_->project, ctx_->dir); // nothing unsaved to ask about
        showHome();
    });
    at(800, [this, shoot] { shoot(this, "9-home"); });
    at(300, [] { QApplication::quit(); });
    for (auto& [ms, f] : steps) QTimer::singleShot(ms, this, f);
}

} // namespace artisan::editor
