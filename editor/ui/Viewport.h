#pragma once

// The scene view: the engine's own Vulkan renderer in a QVulkanWindow, so the editor shows
// exactly what the game draws (shapes, glows, lights), plus the editor's overlays: the grid, the
// game's frame, selection, lights and empty objects. Click to select, drag to move, wheel to zoom,
// middle / right button (or Space) to pan. Objects dragged from the library land where dropped.

#include "artisan/core/Math.h"

#include <QWidget>

class QLabel;
class QVulkanInstance;

namespace artisan::editor {

class EditorContext;
class SceneWindow;

class Viewport : public QWidget {
    Q_OBJECT
public:
    Viewport(EditorContext& ctx, QVulkanInstance* vulkan, QWidget* parent = nullptr);
    void fit();      // the game's frame, whole
    void refresh();  // draw again
    QString deviceName() const;

protected:
    bool eventFilter(QObject* watched, QEvent* e) override; // drops on the native view's container

private:
    void updateZoomLabel();
    EditorContext& ctx_;
    SceneWindow* window_ = nullptr;
    QLabel* zoom_ = nullptr;
    QWidget* container_ = nullptr;
    int shownScene_ = -1;
};

} // namespace artisan::editor
