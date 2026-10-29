#pragma once

// The editor's window: the start screen, or the workspace of an open project. Under the menu bar
// the navigator (back / forward, the scenes as tabs, where you are); then panels docked as in
// bemotion, each movable to any side or out of the window, or minimized to a tab on the window's
// edge: the library on the left (ready-made objects, or the nodes in the Nodes view; hidden in
// the Scripts view), the Scene / Nodes / Scripts views in the middle with the Console under them,
// the Hierarchy and Files over the Inspector on the right. In the top right corner
// Settings, Play and Export.
//
// A project saves itself a moment after each change (Settings › Editor); unsaved changes are
// kept aside every few seconds, and offered back when the project opens after a crash.

#include <QByteArray>
#include <QMainWindow>
#include <QPointer>

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

class QLabel;
class QMenu;
class QProcess;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QVulkanInstance;

namespace ads {
class CDockAreaWidget;
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
    void showEvent(QShowEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override; // the dock manager's size: edge tabs follow

private:
    void buildDocks();
    void buildMenus();
    void buildPluginsMenu();
    void placeDefaultLayout(); // sizes of the first layout, once the window has its size
    void saveLayout();
    void resetLayout();
    void showLibraryArea(bool show); // hidden while the Scripts view shows
    bool confirmClose(); // unsaved changes: save, discard or stay
    bool save(bool quiet = false);
    void autosaveSoon();
    void keepRecoveryCopy();
    void dropRecoveryCopy();
    std::filesystem::path recoveryFile() const;
    void play();
    void stop();
    void updateTitle();
    void showView(int index); // Scene, Nodes, Scripts
    void runPluginCommand(const std::string& pluginId, const std::string& name);
    QAction* action(const char* id, const QString& text, std::function<void()> run);
    void applyShortcuts();

    // Panels minimized to a tab on the window's edge.
    void decorateArea(ads::CDockAreaWidget* area); // its minimize button
    void updateAreaButtons();
    void minimizeArea(ads::CDockAreaWidget* area);
    void restoreMinimized(size_t index);
    void restoreAllMinimized();
    void placeEdgeTabs();
    int edgeOf(ads::CDockAreaWidget* area, float* along) const; // 0 left, 1 right, 2 top, 3 bottom
    struct Minimized {
        std::vector<ads::CDockWidget*> docks;
        ads::CDockWidget* current = nullptr;
        int edge = 0;       // 0 left, 1 right, 2 top, 3 bottom
        float along = 0.5f; // where along that edge (0..1)
        int size = 0;       // its width / height, given back when it returns
        QWidget* tab = nullptr;
    };
    std::vector<Minimized> minimized_;
    QPointer<ads::CDockAreaWidget> hiddenLibraryArea_; // the library's area, while the Scripts view shows

    EditorContext* ctx_ = nullptr;
    QVulkanInstance* vulkan_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    HomeScreen* home_ = nullptr;
    QWidget* workspace_ = nullptr;
    Navigator* navigator_ = nullptr;
    ads::CDockManager* docks_ = nullptr;
    std::map<std::string, ads::CDockWidget*> dockWidgets_;
    std::map<std::string, QAction*> actions_;
    QByteArray defaultLayout_;
    LibraryPanel* library_ = nullptr;
    HierarchyPanel* hierarchy_ = nullptr;
    FilesPanel* files_ = nullptr;
    InspectorPanel* inspector_ = nullptr;
    Viewport* viewport_ = nullptr;
    NodesTab* nodes_ = nullptr;
    ScriptTab* script_ = nullptr;
    QTabWidget* views_ = nullptr;
    QMenu* pluginsMenu_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QLabel* status_ = nullptr;
    QProcess* player_ = nullptr;
    QTimer* autosave_ = nullptr;
};

} // namespace artisan::editor
