#pragma once

// The Inspector: the selected object's components (transform, shape, sprite, light, behavior),
// or, when the scene itself is selected, the scene's settings and its own behavior.

#include "artisan/core/Scene.h"

#include <QWidget>

#include <functional>
#include <vector>

class QScrollArea;
class QVBoxLayout;

namespace artisan::editor {

class EditorContext;

class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(EditorContext& ctx, QWidget* parent = nullptr);

private:
    void rebuildInspector();
    void refreshValues();
    void buildEntityInspector(QVBoxLayout* col, Entity& e);
    void buildSceneInspector(QVBoxLayout* col);
    void buildBehavior(QVBoxLayout* col);
    QWidget* section(QVBoxLayout* col, const QString& title, std::function<void()> remove = {});

    EditorContext& ctx_;
    QScrollArea* inspector_ = nullptr;
    std::vector<std::function<void()>> refreshers_; // put the model's values in the fields
};

} // namespace artisan::editor
