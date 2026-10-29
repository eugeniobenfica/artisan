#pragma once

#include "app/ProjectIO.h"

#include <QImage>
#include <QWidget>

#include <filesystem>
#include <functional>
#include <map>

class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QScrollArea;

namespace artisan::editor {

// The start screen, as bemotion's: a sidebar (new, open, the projects folder, settings) and the
// grid of projects (every project in the projects folder plus the ones opened from elsewhere),
// each with a preview of its scene.
class HomeScreen : public QWidget {
    Q_OBJECT
public:
    explicit HomeScreen(QWidget* parent = nullptr);

    std::function<void(const std::filesystem::path&)> onOpen;
    std::function<void()> onSettings;
    std::function<void()> onHelp;

    void refresh(); // when the screen shows
    void rebuild(); // after a theme change
    void setError(const QString& message);
    void openNewProjectDialog();
    bool chooseProjectsFolder();

protected:
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    struct Preview {
        QImage image;
        int64_t stamp = 0;
        bool ok = false;
        QString meta;
        std::string name; // the project's own name (the folder's may differ)
    };
    void buildUi();
    void rebuildGrid();
    void confirmDelete(const ProjectEntry& entry);

    std::vector<ProjectEntry> entries_;
    std::map<std::filesystem::path, Preview> previews_;
    QWidget* content_ = nullptr;
    QFileSystemWatcher* watcher_ = nullptr;
    QLineEdit* search_ = nullptr;
    QLabel* error_ = nullptr;
    QWidget* errorRow_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    int columns_ = 0;
};

} // namespace artisan::editor
