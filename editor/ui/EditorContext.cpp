#include "ui/EditorContext.h"

#include "app/ProjectIO.h"

namespace artisan::editor {

namespace {
constexpr size_t kUndoSteps = 200;
constexpr qint64 kMergeMs = 800;
} // namespace

void EditorContext::select(EntityId id) {
    if (id == selection_) return;
    selection_ = id;
    emit selectionChanged();
}

void EditorContext::checkpoint(const std::string& key) {
    if (!key.empty() && key == lastKey_ && lastTime_.isValid() && lastTime_.elapsed() < kMergeMs) {
        lastTime_.restart();
        return;
    }
    lastKey_ = key;
    lastTime_.restart();
    undo_.push_back(sceneToJson(scene()));
    if (undo_.size() > kUndoSteps) undo_.erase(undo_.begin());
    redo_.clear();
}

void EditorContext::changed() {
    dirty_ = true;
    if (!scene().find(selection_) && selection_) {
        selection_ = 0;
        emit selectionChanged();
    }
    emit sceneChanged();
}

void EditorContext::restore(const std::string& json) {
    Scene s;
    if (!sceneFromJson(json, s)) return;
    scene() = std::move(s);
    lastKey_.clear();
    changed();
    emit selectionChanged(); // the selected object's values may differ now
}

bool EditorContext::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(sceneToJson(scene()));
    const std::string s = std::move(undo_.back());
    undo_.pop_back();
    restore(s);
    return true;
}

bool EditorContext::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(sceneToJson(scene()));
    const std::string s = std::move(redo_.back());
    redo_.pop_back();
    restore(s);
    return true;
}

bool EditorContext::save(std::string* error) {
    if (dir.empty()) return false;
    if (!saveProject(dir, project, error)) return false;
    dirty_ = false;
    emit saved();
    return true;
}

void EditorContext::reset(Project p, std::filesystem::path folder) {
    project = std::move(p);
    dir = std::move(folder);
    selection_ = 0;
    undo_.clear();
    redo_.clear();
    lastKey_.clear();
    dirty_ = false;
    emit opened();
    emit selectionChanged();
    emit sceneChanged();
}

} // namespace artisan::editor
