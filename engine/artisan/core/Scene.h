#pragma once

// The game's data: a scene is a flat list of entities, each with a transform and the few optional
// components a 2D game needs (shape, sprite, light) plus its behavior (a Lua script file and/or a
// node graph). Kept flat and contiguous on purpose: the renderer and the scripts walk it every
// frame.

#include "artisan/core/Math.h"
#include "artisan/nodes/NodeGraph.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace artisan {

using EntityId = uint32_t;

// Shapes are drawn analytically (signed distance fields): sharp at any zoom, with free outlines
// and glows, no textures.
enum class ShapeKind { Rectangle, RoundedRectangle, Circle, Capsule };

struct Shape {
    ShapeKind kind = ShapeKind::Rectangle;
    Vec2 size{64, 64};
    Color color = Color::rgb(0xE8B931);
    float cornerRadius = 12.0f; // RoundedRectangle
    float outline = 0.0f;       // px, 0: none
    Color outlineColor = Color::rgb(0xFFFFFF);
    float glow = 0.0f;          // px of soft light around the shape, 0: none
    bool lit = true;            // shaded by the scene's lights
};

struct Sprite {
    std::string image; // path in the project ("assets/hero.png")
    Vec2 size{64, 64};
    Color tint{};
    bool lit = true;
};

struct Light {
    Color color = Color::rgb(0xFFE6B0);
    float radius = 320.0f;
    float intensity = 1.2f;
};

struct Entity {
    EntityId id = 0;
    std::string name;
    bool visible = true;
    Vec2 position;
    float rotation = 0.0f; // degrees, clockwise
    Vec2 scale{1, 1};
    int layer = 0; // drawing order: higher on top

    std::optional<Shape> shape;
    std::optional<Sprite> sprite;
    std::optional<Light> light;
    std::string script;           // a Lua file in the project ("scripts/player.lua"), or empty
    std::optional<NodeGraph> graph; // visual behavior, compiled to Lua when the game starts
};

struct Camera {
    Vec2 position; // center of the view
    float zoom = 1.0f;
};

struct Scene {
    std::string name = "Main";
    Color background = Color::rgb(0x101014);
    Color ambient{0.35f, 0.35f, 0.42f, 1.0f}; // light where no light reaches (lit objects)
    Camera camera;
    std::vector<Entity> entities;
    // The scene's own behavior (the rules of a level, a score, a spawner): it runs before the
    // objects', with no object as `self`.
    std::string script;
    std::optional<NodeGraph> graph;

    Entity* find(EntityId id);
    const Entity* find(EntityId id) const;
    Entity* findByName(const std::string& name);
    EntityId nextId() const;
    // A new entity with a unique id and name ("Rectangle 2"), appended.
    Entity& add(Entity e);
    bool remove(EntityId id);
    std::string uniqueName(const std::string& base) const;
};

} // namespace artisan
