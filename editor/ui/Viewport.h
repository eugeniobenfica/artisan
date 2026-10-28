#pragma once

// The scene view: the engine's own Vulkan renderer in a QVulkanWindow, so the editor shows
// exactly what the game draws (shapes, glows, lights), plus the editor's overlays: the grid, the
// game's frame, selection, lights and empty objects. Click to select, drag to move, wheel to zoom,
// middle / right button (or Space) to pan.

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

private:
    void updateZoomLabel();
    EditorContext& ctx_;
    SceneWindow* window_ = nullptr;
    QLabel* zoom_ = nullptr;
};

} // namespace artisan::editor
