#pragma once

// The navigator, under the menu bar: back and forward through what was selected, the project's
// scenes as tabs (the + adds one; right-click to rename, duplicate, delete or make it the start
// scene; drag to reorder), and where you are: Project › Scene › Object, each part clickable.

#include <QWidget>

class QLabel;
class QTabBar;

namespace artisan::editor {

class EditorContext;

class Navigator : public QWidget {
    Q_OBJECT
public:
    explicit Navigator(EditorContext& ctx, QWidget* parent = nullptr);
    void addScene();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void rebuildTabs();
    void updatePath();
    void renameScene(int index);
    void sceneMenu(int index, QPoint global);

    EditorContext& ctx_;
    QWidget* backButton_ = nullptr;
    QWidget* forwardButton_ = nullptr;
    QTabBar* tabs_ = nullptr;
    QLabel* path_ = nullptr;
    bool syncing_ = false;
};

} // namespace artisan::editor
