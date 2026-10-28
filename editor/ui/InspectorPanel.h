#pragma once

// The left side of the workspace: the scene's objects (add, select, reorder, hide, delete) and,
// under them, the inspector of the selected object (or of the scene, when nothing is selected).

#include "artisan/core/Scene.h"

#include <QWidget>

#include <functional>
#include <string>
#include <vector>

class QListWidget;
class QScrollArea;
class QVBoxLayout;

namespace artisan::editor {

class EditorContext;

class ObjectsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ObjectsPanel(EditorContext& ctx, QWidget* parent = nullptr);

    void addObject(const std::string& kind); // "rectangle", "circle", "sprite", "light", "empty"...
    void deleteSelected();
    void duplicateSelected();

private:
    void rebuildList();
    void syncListSelection();
    void rebuildInspector();
    void refreshValues();
    void buildEntityInspector(QVBoxLayout* col, Entity& e);
    void buildSceneInspector(QVBoxLayout* col);
    QWidget* section(QVBoxLayout* col, const QString& title, std::function<void()> remove = {});

    EditorContext& ctx_;
    QListWidget* list_ = nullptr;
    QScrollArea* inspector_ = nullptr;
    std::string listSignature_;
    std::vector<std::function<void()>> refreshers_; // put the model's values in the fields
    bool syncing_ = false;
};

} // namespace artisan::editor
