#pragma once

// The editor's window: the start screen, or the workspace of an open project. Under the menu bar
// the navigator (back / forward, the scenes as tabs, where you are); then panels docked as in
// bemotion, each movable to any side or out of the window: the library on the left (ready-made
// objects, or the nodes in the Nodes view), the Scene / Nodes / Script views in the middle with
// the Console under them, the Hierarchy and Files over the Inspector on the right. In the top
// right corner Settings, Play and Export.

#include <QByteArray>
#include <QMainWindow>

#include <filesystem>
#include <functional>
#include <map>
#include <string>

class QLabel;
class QProcess;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QVulkanInstance;

namespace ads {
class CDockManager;
class CDockWidget;
} // namespace ads

namespace artisan::editor {

class EditorContext;
class FilesPanel;
class HierarchyPanel;
class HomeScreen;
class InspectorPanel;
class LibraryPanel;
class Navigator;
class NodesTab;
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
    void buildDocks();
    void buildMenus();
    void placeDefaultLayout(); // sizes of the first layout, once the window has its size
    void saveLayout();
    bool confirmClose(); // unsaved changes: save, discard or stay
    bool save();
    void play();
    void stop();
    void updateTitle();
    void showView(int index); // Scene, Nodes, Script
    QAction* action(const QString& text, const QKeySequence& key, std::function<void()> run);

    EditorContext* ctx_ = nullptr;
    QVulkanInstance* vulkan_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    HomeScreen* home_ = nullptr;
    QWidget* workspace_ = nullptr;
    Navigator* navigator_ = nullptr;
    ads::CDockManager* docks_ = nullptr;
    std::map<std::string, ads::CDockWidget*> dockWidgets_;
    QByteArray defaultLayout_;
    LibraryPanel* library_ = nullptr;
    HierarchyPanel* hierarchy_ = nullptr;
    FilesPanel* files_ = nullptr;
    InspectorPanel* inspector_ = nullptr;
    Viewport* viewport_ = nullptr;
    NodesTab* nodes_ = nullptr;
    ScriptTab* script_ = nullptr;
    QTabWidget* views_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QLabel* status_ = nullptr;
    QProcess* player_ = nullptr;
};

} // namespace artisan::editor
