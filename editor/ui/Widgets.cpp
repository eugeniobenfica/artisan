#include "ui/Widgets.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QRadialGradient>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

namespace artisan::editor::ui {

QColor qcolor(const Color& c) { return QColor::fromRgbF(c.r, c.g, c.b, c.a); }
Color fromQColor(const QColor& c) {
    return {static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF()), static_cast<float>(c.alphaF())};
}

void drawLogoMark(QPainter& p, Pt center, float radius, Rgba color) {
    // Unit coordinates (radius 1, y down): a thick triangle (the outer one less the inner one,
    // scaled about the center of its inscribed circle) whose base stops short of the right leg:
    // an upright gap at the bottom right, the leg running on down to the base's line, so the
    // triangle reads as an "A". Lit from the top: the color goes lighter toward the peak.
    const QPointF top(0.0, -0.92), left(-0.98, 0.78), right(0.98, 0.78);
    const QPointF in(0.0, 0.214); // the inscribed circle's center (radius 0.566)
    const double k = 0.47;         // the inner triangle's scale: legs 0.3 thick
    auto inner = [&](QPointF v) { return in + (v - in) * k; };
    QPainterPath outer;
    outer.addPolygon(QPolygonF({top, left, right, top}));
    QPainterPath hole;
    hole.addPolygon(QPolygonF({inner(top), inner(left), inner(right), inner(top)}));
    // The gap: a band through the base along the right leg's inner edge (slanted as the leg is),
    // from the hole down past the bottom; the leg itself stays whole.
    const QPointF corner = inner(right) + QPointF(0, -0.01), along = inner(right) - inner(top);
    const double below = right.y() + 0.05;
    const QPointF legFoot = corner + along / along.y() * (below - corner.y());
    const QPointF width(0.17, 0.0);
    QPainterPath gap;
    gap.addPolygon(QPolygonF({corner - width, corner, legFoot, legFoot - width, corner - width}));
    const QPainterPath mark = outer.subtracted(hole).subtracted(gap);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(center.x, center.y);
    p.scale(radius, radius);
    p.setPen(Qt::NoPen);
    QLinearGradient light(top, QPointF(0.0, 0.78));
    light.setColorAt(0.0, bestyle::qcolor(theme::mix(color, rgba(255, 255, 255), 0.4f)));
    light.setColorAt(1.0, bestyle::qcolor(theme::mix(color, rgba(0, 40, 60), 0.15f)));
    p.setBrush(light);
    p.drawPath(mark);
    p.restore();
}

void drawAppIcon(QPainter& p, float size) {
    // The mark on a flat dark rounded square, as bemotion's icon (#141417), for the taskbar and
    // the window.
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 20, 23));
    const float m = size * 0.03f;
    p.drawRoundedRect(QRectF(m, m, size - 2 * m, size - 2 * m), size * 0.22, size * 0.22);
    p.restore();
    drawLogoMark(p, {size * 0.5f, size * 0.53f}, size * 0.34f, kLogoCyan);
}

void LogoMark::paintEvent(QPaintEvent*) {
    QPainter p(this);
    drawLogoMark(p, {width() * 0.5f, height() * 0.5f}, width() * 0.48f, kLogoCyan);
}

Icon pluginIcon(const std::string& name) {
    static const std::map<std::string, Icon> icons = {
        {"star", Icon::Star},         {"circle", Icon::Ellipse},  {"rectangle", Icon::Rectangle}, {"person", Icon::Person},
        {"bulb", Icon::Bulb},         {"cube", Icon::Cube},       {"image", Icon::Image},         {"zigzag", Icon::Zigzag},
        {"ripple", Icon::Ripple},     {"marker", Icon::Marker},   {"magnet", Icon::Magnet},       {"align_left", Icon::AlignLeft},
        {"fit", Icon::FitView},       {"plugin", Icon::Plugin},   {"code", Icon::Code},           {"wave", Icon::Wave},
        {"bounce", Icon::Bounce},     {"layers", Icon::Layers},   {"polygon", Icon::Polygon},     {"diamond", Icon::Diamond}};
    auto it = icons.find(name);
    return it != icons.end() ? it->second : Icon::Plugin;
}

LogoButton::LogoButton(int size, QWidget* parent) : QAbstractButton(parent), size_(size) {
    setFixedSize(size, size);
    setCursor(Qt::PointingHandCursor);
}

void LogoButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    if (underMouse()) d.rectFilled({0, 0}, {static_cast<float>(width()), static_cast<float>(height())}, theme::kBgHover, theme::current().rounding);
    drawLogoMark(p, {width() * 0.5f, height() * 0.5f}, width() * 0.36f, kLogoCyan);
}

QLabel* label(const QString& text, Tone tone, float scale, bool bold) {
    auto* l = new QLabel(text);
    setTone(l, tone);
    if (scale != 1.0f || bold) {
        QFont f = l->font();
        f.setPointSizeF(f.pointSizeF() * scale);
        if (bold) f.setWeight(QFont::DemiBold);
        l->setFont(f);
    }
    return l;
}

ColorButton::ColorButton(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(40, 22);
    setToolTip(tr("Pick a color"));
}

void ColorButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    const float rad = theme::current().rounding;
    // A checkerboard under transparent colors.
    QPainterPath clip;
    clip.addRoundedRect(r, rad, rad);
    p.save();
    p.setClipPath(clip);
    for (int y = 0; y < height(); y += 6)
        for (int x = 0; x < width(); x += 6)
            p.fillRect(x, y, 6, 6, ((x + y) / 6) % 2 ? QColor(200, 200, 200) : QColor(150, 150, 150));
    p.fillRect(r, qcolor(color_));
    p.restore();
    d.rect(Pt(r.topLeft()), Pt(r.bottomRight()), underMouse() ? theme::kAccent : theme::kFieldBorder, rad, 1.0f);
}

void ColorButton::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !rect().contains(e->position().toPoint())) return;
    const Color before = color_;
    QColorDialog dialog(qcolor(color_), this);
    dialog.setOption(QColorDialog::ShowAlphaChannel);
    connect(&dialog, &QColorDialog::currentColorChanged, this, [this](const QColor& c) {
        color_ = fromQColor(c);
        update();
        emit picked(color_);
    });
    if (dialog.exec() != QDialog::Accepted) {
        color_ = before;
        update();
        emit picked(color_);
    }
}

void paintScenePreview(QPainter& p, const Scene& scene, const QRectF& target, QSizeF design) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(target);
    p.fillRect(target, qcolor(scene.background));
    const float zoom = static_cast<float>(std::min(target.width() / design.width(), target.height() / design.height())) * scene.camera.zoom;
    p.translate(target.center());
    p.scale(zoom, zoom);
    p.translate(-scene.camera.position.x, -scene.camera.position.y);
    std::vector<const Entity*> order;
    for (const Entity& e : scene.entities)
        if (e.visible) order.push_back(&e);
    std::stable_sort(order.begin(), order.end(), [](const Entity* a, const Entity* b) { return a->layer < b->layer; });
    auto lit = [&](Color c, bool on) {
        if (!on) return qcolor(c);
        return QColor::fromRgbF(std::min(1.0f, c.r * (scene.ambient.r + 0.4f)), std::min(1.0f, c.g * (scene.ambient.g + 0.4f)),
                                std::min(1.0f, c.b * (scene.ambient.b + 0.4f)), c.a);
    };
    for (const Entity* e : order) {
        p.save();
        p.translate(e->position.x, e->position.y);
        p.rotate(e->rotation);
        p.scale(e->scale.x, e->scale.y);
        if (e->sprite) {
            const QRectF r(-e->sprite->size.x / 2, -e->sprite->size.y / 2, e->sprite->size.x, e->sprite->size.y);
            p.fillRect(r, lit(e->sprite->tint, e->sprite->lit));
        }
        if (e->shape) {
            const Shape& s = *e->shape;
            const QRectF r(-s.size.x / 2, -s.size.y / 2, s.size.x, s.size.y);
            QPainterPath path;
            const float m = std::min(s.size.x, s.size.y) / 2;
            switch (s.kind) {
            case ShapeKind::Rectangle: path.addRect(r); break;
            case ShapeKind::RoundedRectangle: path.addRoundedRect(r, std::min(s.cornerRadius, m), std::min(s.cornerRadius, m)); break;
            case ShapeKind::Circle: path.addEllipse(r); break;
            case ShapeKind::Capsule: path.addRoundedRect(r, m, m); break;
            }
            if (s.glow > 0) {
                QColor g = qcolor(s.color);
                g.setAlphaF(0.25f);
                p.setPen(QPen(g, s.glow * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
            }
            p.setPen(s.outline > 0 ? QPen(qcolor(s.outlineColor), s.outline) : Qt::NoPen);
            p.setBrush(lit(s.color, s.lit));
            p.drawPath(path);
        }
        p.restore();
    }
    // Lights: soft additive discs.
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    for (const Entity* e : order) {
        if (!e->light) continue;
        QRadialGradient g(QPointF(e->position.x, e->position.y), e->light->radius);
        QColor c = qcolor(e->light->color);
        c.setAlphaF(std::clamp(0.22f * e->light->intensity, 0.0f, 1.0f));
        g.setColorAt(0, c);
        c.setAlphaF(0);
        g.setColorAt(1, c);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(QPointF(e->position.x, e->position.y), e->light->radius, e->light->radius);
    }
    p.restore();
}

} // namespace artisan::editor::ui
