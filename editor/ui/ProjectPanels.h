#pragma once

// The project's organization, on the right of the workspace: the Hierarchy (the scene at the top,
// its objects under it in drawing order: select, rename, reorder, duplicate, delete) and Files
// (the project folder: open a script, add a picture as a sprite, import, show in the system).

#include <QTreeWidget>
#include <QWidget>

#include <filesystem>
#include <functional>
#include <string>

class QFileSystemModel;
class QTreeView;

namespace artisan::editor {

class EditorContext;

class HierarchyTree;

class HierarchyPanel : public QWidget {
    Q_OBJECT
public:
    explicit HierarchyPanel(EditorContext& ctx, QWidget* parent = nullptr);
    void deleteSelected();
    void duplicateSelected();

private:
    void rebuild();
    void syncSelection();

    EditorContext& ctx_;
    HierarchyTree* tree_ = nullptr;
    std::string signature_;
    bool syncing_ = false;
};

class FilesPanel : public QWidget {
    Q_OBJECT
public:
    explicit FilesPanel(EditorContext& ctx, QWidget* parent = nullptr);

    std::function<void(const std::filesystem::path&)> onOpenScript; // a .lua file was opened

private:
    void open(const std::filesystem::path& file);
    void addSprite(const std::filesystem::path& image);

    EditorContext& ctx_;
    QFileSystemModel* model_ = nullptr;
    QTreeView* view_ = nullptr;
};

} // namespace artisan::editor
