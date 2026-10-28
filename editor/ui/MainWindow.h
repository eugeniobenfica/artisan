#pragma once

// The editor's window: the start screen, or the workspace of an open project — the objects and
// the inspector on the left; on the right the Scene (viewport), Nodes, Script and Console tabs;
// in the top right corner Settings, Play and Export.

#include <QMainWindow>

#include <filesystem>

class QLabel;
class QProcess;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QVulkanInstance;

namespace artisan::editor {

class EditorContext;
class HomeScreen;
class ObjectsPanel;
class ScriptTab;
class Viewport;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QVulkanInstance* vulkan, QWidget* parent = nullptr);
    ~MainWindow() override;
    bool openProject(const std::filesystem::path& dir);
    void showHome();
    // Development: screenshots of every screen into `dir`, then quits (ARTISAN_CAPTURE).
    void devCapture(const QString& dir);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildMenus();
    bool confirmClose(); // unsaved changes: save, discard or stay
    bool save();
    void play();
    void stop();
    void updateTitle();
    QAction* action(const QString& text, const QKeySequence& key, std::function<void()> run);

    EditorContext* ctx_ = nullptr;
    QVulkanInstance* vulkan_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    HomeScreen* home_ = nullptr;
    QWidget* workspace_ = nullptr;
    ObjectsPanel* objects_ = nullptr;
    Viewport* viewport_ = nullptr;
    ScriptTab* script_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QLabel* status_ = nullptr;
    QProcess* player_ = nullptr;
};

} // namespace artisan::editor
