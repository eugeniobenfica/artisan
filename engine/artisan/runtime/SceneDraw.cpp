#include "artisan/runtime/SceneDraw.h"

#include <cmath>

namespace artisan {

View gameView(const Scene& scene, Vec2 design, Vec2 viewport) {
    const float fit = std::min(viewport.x / std::max(design.x, 1.0f), viewport.y / std::max(design.y, 1.0f));
    return {scene.camera.position, scene.camera.zoom * fit, viewport};
}

Vec2 entitySize(const Entity& e) {
    Vec2 s{48, 48};
    if (e.shape) s = e.shape->size;
    else if (e.sprite) s = e.sprite->size;
    return {s.x * std::fabs(e.scale.x), s.y * std::fabs(e.scale.y)};
}

QuadInstance entityQuad(const Entity& e, Vec2 size) {
    QuadInstance q{};
    q.x = e.position.x;
    q.y = e.position.y;
    q.w = size.x;
    q.h = size.y;
    q.rotation = radians(e.rotation);
    q.u0 = e.scale.x < 0 ? 1.0f : 0.0f; // a negative scale mirrors
    q.u1 = 1.0f - q.u0;
    q.v0 = e.scale.y < 0 ? 1.0f : 0.0f;
    q.v1 = 1.0f - q.v0;
    q.r = q.g = q.b = q.a = 1.0f;
    return q;
}

void drawScene(Renderer2D& r, const Scene& scene, const View& view, const Renderer2D::FileReader& read, float time) {
    r.setCamera(view.center, view.zoom, view.viewport, time);
    r.setAmbient(scene.ambient);
    for (const Entity& e : scene.entities)
        if (e.visible && e.light) r.addLight(e.position, e.light->radius, e.light->intensity, e.light->color);

    // The background is air: lights tint it.
    QuadInstance bg{};
    bg.x = view.center.x;
    bg.y = view.center.y;
    bg.w = view.viewport.x / view.zoom + 4.0f;
    bg.h = view.viewport.y / view.zoom + 4.0f;
    bg.r = scene.background.r;
    bg.g = scene.background.g;
    bg.b = scene.background.b;
    bg.a = 1.0f;
    bg.lighting = static_cast<float>(Lighting::Air);
    bg.u1 = bg.v1 = 1.0f;
    r.draw(bg, INT32_MIN);

    for (const Entity& e : scene.entities) {
        if (!e.visible) continue;
        if (e.sprite) {
            const TextureId tex = r.texture(e.sprite->image, read);
            QuadInstance q = entityQuad(e, {e.sprite->size.x * std::fabs(e.scale.x), e.sprite->size.y * std::fabs(e.scale.y)});
            q.r = e.sprite->tint.r;
            q.g = e.sprite->tint.g;
            q.b = e.sprite->tint.b;
            q.a = e.sprite->tint.a;
            q.textured = tex ? 1.0f : 0.0f;
            if (!tex) q.a *= 0.35f; // a missing image shows as a faint box, not nothing
            q.lighting = static_cast<float>(e.sprite->lit ? Lighting::Lit : Lighting::None);
            r.draw(q, e.layer, tex);
        }
        if (e.shape) {
            const Shape& s = *e.shape;
            QuadInstance q = entityQuad(e, {s.size.x * std::fabs(e.scale.x), s.size.y * std::fabs(e.scale.y)});
            q.shape = static_cast<float>(s.kind == ShapeKind::Rectangle          ? QuadShape::Rectangle
                                         : s.kind == ShapeKind::RoundedRectangle ? QuadShape::RoundedRectangle
                                         : s.kind == ShapeKind::Circle           ? QuadShape::Circle
                                                                                 : QuadShape::Capsule);
            q.corner = s.cornerRadius;
            q.outline = s.outline;
            q.r = s.color.r;
            q.g = s.color.g;
            q.b = s.color.b;
            q.a = s.color.a;
            q.or_ = s.outlineColor.r;
            q.og = s.outlineColor.g;
            q.ob = s.outlineColor.b;
            q.oa = s.outlineColor.a;
            q.glow = s.glow;
            q.lighting = static_cast<float>(s.lit ? Lighting::Lit : Lighting::None);
            r.draw(q, e.layer);
        }
    }
}

} // namespace artisan
