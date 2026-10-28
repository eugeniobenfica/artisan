#include "ui/EditorContext.h"

#include "app/ProjectIO.h"

#include <algorithm>

namespace artisan::editor {

namespace {
constexpr size_t kUndoSteps = 200;
constexpr size_t kHistorySteps = 100;
constexpr qint64 kMergeMs = 800;
} // namespace

void EditorContext::select(EntityId id) {
    if (id == selection_) return;
    selection_ = id;
    remember();
    emit selectionChanged();
}

std::optional<NodeGraph>& EditorContext::graph() {
    Entity* e = selected();
    return e ? e->graph : scene().graph;
}

std::string& EditorContext::script() {
    Entity* e = selected();
    return e ? e->script : scene().script;
}

std::string EditorContext::behaviorOwner() {
    const Entity* e = selected();
    return e ? e->name : scene().name;
}

// --- scenes ------------------------------------------------------------------------------------

void EditorContext::openScene(int index) {
    if (index < 0 || index >= static_cast<int>(project.scenes.size()) || index == sceneIndex_) return;
    sceneIndex_ = index;
    selection_ = 0;
    remember();
    emit scenesChanged();
    emit selectionChanged();
    emit sceneChanged();
}

int EditorContext::addScene(Scene s) {
    // Scene names are unique, as object names are.
    const std::string base = s.name.empty() ? "Scene" : s.name;
    auto taken = [&](const std::string& n) {
        return std::any_of(project.scenes.begin(), project.scenes.end(), [&](const Scene& x) { return x.name == n; });
    };
    s.name = base;
    for (int i = 2; taken(s.name); ++i) s.name = base + " " + std::to_string(i);
    project.scenes.push_back(std::move(s));
    dirty_ = true;
    const int index = static_cast<int>(project.scenes.size()) - 1;
    sceneIndex_ = -1; // so openScene opens it, and says so
    openScene(index);
    return index;
}

void EditorContext::removeScene(int index) {
    if (project.scenes.size() < 2 || index < 0 || index >= static_cast<int>(project.scenes.size())) return;
    project.scenes.erase(project.scenes.begin() + index);
    if (project.mainScene > index || project.mainScene >= static_cast<int>(project.scenes.size())) project.mainScene = std::max(0, project.mainScene - 1);
    // The undo history and the navigator name scenes by place: what pointed past it moves down.
    auto fix = [index](int& at) { return at == index ? false : (at > index ? --at, true : true); };
    std::erase_if(undo_, [&](Snapshot& s) { return !fix(s.scene); });
    std::erase_if(redo_, [&](Snapshot& s) { return !fix(s.scene); });
    std::erase_if(history_, [&](Place& p) { return !fix(p.scene); });
    historyAt_ = static_cast<int>(history_.size()) - 1;
    dirty_ = true;
    const int next = std::min(index, static_cast<int>(project.scenes.size()) - 1);
    sceneIndex_ = -1;
    openScene(next);
}

void EditorContext::moveScene(int from, int to) {
    const int n = static_cast<int>(project.scenes.size());
    if (from == to || from < 0 || to < 0 || from >= n || to >= n) return;
    auto remap = [from, to](int i) {
        if (i == from) return to;
        if (from < to && i > from && i <= to) return i - 1;
        if (to < from && i >= to && i < from) return i + 1;
        return i;
    };
    Scene moved = std::move(project.scenes[static_cast<size_t>(from)]);
    project.scenes.erase(project.scenes.begin() + from);
    project.scenes.insert(project.scenes.begin() + to, std::move(moved));
    project.mainScene = remap(project.mainScene);
    sceneIndex_ = remap(sceneIndex_);
    for (Snapshot& s : undo_) s.scene = remap(s.scene);
    for (Snapshot& s : redo_) s.scene = remap(s.scene);
    for (Place& p : history_) p.scene = remap(p.scene);
    dirty_ = true;
    emit scenesChanged();
}

void EditorContext::setStartScene(int index) {
    if (index < 0 || index >= static_cast<int>(project.scenes.size()) || index == project.mainScene) return;
    project.mainScene = index;
    dirty_ = true;
    emit scenesChanged();
    emit sceneChanged();
}

// --- the navigator -----------------------------------------------------------------------------

void EditorContext::remember() {
    if (navigating_) return;
    const Place here{sceneIndex_, selection_};
    if (historyAt_ >= 0 && history_[static_cast<size_t>(historyAt_)] == here) return;
    history_.resize(static_cast<size_t>(historyAt_ + 1));
    history_.push_back(here);
    if (history_.size() > kHistorySteps) history_.erase(history_.begin());
    historyAt_ = static_cast<int>(history_.size()) - 1;
}

void EditorContext::go(const Place& place) {
    navigating_ = true;
    if (place.scene != sceneIndex_) openScene(place.scene);
    select(scene().find(place.entity) ? place.entity : 0);
    navigating_ = false;
}

bool EditorContext::back() {
    if (!canGoBack()) return false;
    go(history_[static_cast<size_t>(--historyAt_)]);
    emit scenesChanged(); // the navigator's buttons
    return true;
}

bool EditorContext::forward() {
    if (!canGoForward()) return false;
    go(history_[static_cast<size_t>(++historyAt_)]);
    emit scenesChanged();
    return true;
}

// --- changes and undo --------------------------------------------------------------------------

void EditorContext::checkpoint(const std::string& key) {
    if (!key.empty() && key == lastKey_ && lastTime_.isValid() && lastTime_.elapsed() < kMergeMs) {
        lastTime_.restart();
        return;
    }
    lastKey_ = key;
    lastTime_.restart();
    undo_.push_back({sceneIndex_, sceneToJson(scene())});
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

void EditorContext::restore(const Snapshot& snap) {
    Scene s;
    if (snap.scene < 0 || snap.scene >= static_cast<int>(project.scenes.size()) || !sceneFromJson(snap.json, s)) return;
    if (snap.scene != sceneIndex_) openScene(snap.scene); // undo shows where it happens
    const bool renamed = s.name != scene().name;
    scene() = std::move(s);
    lastKey_.clear();
    changed();
    if (renamed) emit scenesChanged();
    emit selectionChanged(); // the selected object's values may differ now
}

bool EditorContext::undo() {
    if (undo_.empty()) return false;
    Snapshot s = std::move(undo_.back());
    undo_.pop_back();
    if (s.scene >= 0 && s.scene < static_cast<int>(project.scenes.size()))
        redo_.push_back({s.scene, sceneToJson(project.scenes[static_cast<size_t>(s.scene)])});
    restore(s);
    return true;
}

bool EditorContext::redo() {
    if (redo_.empty()) return false;
    Snapshot s = std::move(redo_.back());
    redo_.pop_back();
    if (s.scene >= 0 && s.scene < static_cast<int>(project.scenes.size()))
        undo_.push_back({s.scene, sceneToJson(project.scenes[static_cast<size_t>(s.scene)])});
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
    sceneIndex_ = std::clamp(project.mainScene, 0, static_cast<int>(project.scenes.size()) - 1);
    selection_ = 0;
    undo_.clear();
    redo_.clear();
    history_.clear();
    historyAt_ = -1;
    remember();
    lastKey_.clear();
    dirty_ = false;
    emit opened();
    emit scenesChanged();
    emit selectionChanged();
    emit sceneChanged();
}

} // namespace artisan::editor
