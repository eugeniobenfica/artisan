#pragma once

// A scene as quads and lights for the renderer: the one way scenes are drawn, by the player and
// by the editor's viewport alike, so what you edit is what you play.

#include "artisan/core/Scene.h"
#include "artisan/gfx/Renderer2D.h"

namespace artisan {

struct View {
    Vec2 center;
    float zoom = 1.0f; // screen pixels per world unit
    Vec2 viewport;     // pixels
};

// The camera of a game whose design resolution is `design` in a window of `viewport` pixels:
// the design area always fits.
View gameView(const Scene& scene, Vec2 design, Vec2 viewport);

void drawScene(Renderer2D& r, const Scene& scene, const View& view, const Renderer2D::FileReader& read, float time = 0.0f);

// The quad an entity is drawn with (shape or sprite), for picking and selection outlines.
QuadInstance entityQuad(const Entity& e, Vec2 size);
// The entity's drawn size (shape, sprite or a default for empty objects and lights).
Vec2 entitySize(const Entity& e);

} // namespace artisan
