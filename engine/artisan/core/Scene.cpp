#include "artisan/core/Scene.h"

#include <algorithm>
#include <cmath>

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

std::optional<Box> hitboxOf(const Entity& e) {
    if (!e.hitbox) return std::nullopt;
    const Vec2 half(std::fabs(e.hitbox->size.x * e.scale.x) * 0.5f, std::fabs(e.hitbox->size.y * e.scale.y) * 0.5f);
    const Vec2 c = e.position + Vec2(e.hitbox->offset.x * e.scale.x, e.hitbox->offset.y * e.scale.y);
    return Box{c - half, c + half};
}

namespace {
constexpr float kContact = 0.5f; // side by side this close still touches (a player standing on a floor)

bool touch(const Box& a, const Box& b) {
    return a.min.x <= b.max.x + kContact && b.min.x <= a.max.x + kContact && a.min.y <= b.max.y + kContact && b.min.y <= a.max.y + kContact;
}
} // namespace

bool touching(const Entity& a, const Entity& b) {
    const auto ba = hitboxOf(a), bb = hitboxOf(b);
    return ba && bb && a.id != b.id && touch(*ba, *bb);
}

std::vector<std::pair<EntityId, EntityId>> resolveHitboxes(Scene& scene) {
    for (Entity& m : scene.entities) {
        if (!m.hitbox || m.hitbox->solid) continue;
        for (const Entity& s : scene.entities) {
            if (!s.hitbox || !s.hitbox->solid || s.id == m.id) continue;
            const Box a = *hitboxOf(m), b = *hitboxOf(s);
            const float dx = std::min(a.max.x - b.min.x, b.max.x - a.min.x);
            const float dy = std::min(a.max.y - b.min.y, b.max.y - a.min.y);
            if (dx <= 0 || dy <= 0) continue; // not inside
            // Out along the axis it went in least, away from the solid's center.
            if (dx < dy) m.position.x += (a.min.x + a.max.x < b.min.x + b.max.x) ? -dx : dx;
            else m.position.y += (a.min.y + a.max.y < b.min.y + b.max.y) ? -dy : dy;
        }
    }
    std::vector<std::pair<EntityId, EntityId>> pairs;
    for (size_t i = 0; i < scene.entities.size(); ++i)
        for (size_t k = i + 1; k < scene.entities.size(); ++k) {
            const Entity& a = scene.entities[i];
            const Entity& b = scene.entities[k];
            if (touching(a, b)) pairs.emplace_back(std::min(a.id, b.id), std::max(a.id, b.id));
        }
    return pairs;
}

} // namespace artisan
