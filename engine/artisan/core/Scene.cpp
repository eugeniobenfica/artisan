#include "artisan/core/Scene.h"

#include <algorithm>

namespace artisan {

Entity* Scene::find(EntityId id) {
    for (Entity& e : entities)
        if (e.id == id) return &e;
    return nullptr;
}

const Entity* Scene::find(EntityId id) const { return const_cast<Scene*>(this)->find(id); }

Entity* Scene::findByName(const std::string& name) {
    for (Entity& e : entities)
        if (e.name == name) return &e;
    return nullptr;
}

EntityId Scene::nextId() const {
    EntityId id = 1;
    for (const Entity& e : entities) id = std::max(id, e.id + 1);
    return id;
}

std::string Scene::uniqueName(const std::string& base) const {
    auto taken = [&](const std::string& n) {
        return std::any_of(entities.begin(), entities.end(), [&](const Entity& e) { return e.name == n; });
    };
    if (!taken(base)) return base;
    for (int i = 2;; ++i)
        if (std::string n = base + " " + std::to_string(i); !taken(n)) return n;
}

Entity& Scene::add(Entity e) {
    e.id = nextId();
    e.name = uniqueName(e.name.empty() ? "Object" : e.name);
    entities.push_back(std::move(e));
    return entities.back();
}

bool Scene::remove(EntityId id) {
    return std::erase_if(entities, [id](const Entity& e) { return e.id == id; }) > 0;
}

} // namespace artisan
