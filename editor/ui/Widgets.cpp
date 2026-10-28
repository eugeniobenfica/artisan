#include "ui/Widgets.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QRadialGradient>
#include <QVBoxLayout>

#include <algorithm>

namespace artisan::editor::ui {

QColor qcolor(const Color& c) { return QColor::fromRgbF(c.r, c.g, c.b, c.a); }
Color fromQColor(const QColor& c) {
    return {static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF()), static_cast<float>(c.alphaF())};
}

void drawLogoMark(QPainter& p, Pt center, float radius, Rgba color) {
    // Unit coordinates (radius 1, y down): an "A" of two legs and a bar, cut out of the disc.
    QPainterPath a;
    a.moveTo(-0.44, 0.56);
    a.lineTo(0.0, -0.56);
    a.lineTo(0.44, 0.56);
    a.moveTo(-0.22, 0.16);
    a.lineTo(0.22, 0.16);
    QPainterPathStroker stroker;
    stroker.setWidth(0.2);
    stroker.setCapStyle(Qt::FlatCap);
    stroker.setJoinStyle(Qt::MiterJoin);
    QPainterPath disc;
    disc.addEllipse(QPointF(0, 0), 1.0, 1.0);
    const QPainterPath mark = disc.subtracted(stroker.createStroke(a));
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(center.x, center.y);
    p.scale(radius, radius);
    p.setPen(Qt::NoPen);
    p.setBrush(bmstyle::qcolor(color));
    p.drawPath(mark);
    p.restore();
}

void LogoMark::paintEvent(QPaintEvent*) {
    QPainter p(this);
    drawLogoMark(p, {width() * 0.5f, height() * 0.5f}, width() * 0.48f, theme::kAccent);
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

SettingsCard::SettingsCard() {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(16, 12, 16, 12);
    col->setSpacing(10);
}

void SettingsCard::addRow(const QString& title, const QString& note, QWidget* control) {
    auto* row = new QHBoxLayout;
    row->setSpacing(16);
    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    text->addWidget(label(title));
    if (!note.isEmpty()) {
        QLabel* n = label(note, Tone::TextFaint, 0.92f);
        n->setWordWrap(true);
        text->addWidget(n);
    }
    row->addLayout(text, 1);
    if (control) row->addWidget(control, 0, Qt::AlignVCenter);
    static_cast<QVBoxLayout*>(layout())->addLayout(row);
}

void SettingsCard::addContent(QWidget* w) { layout()->addWidget(w); }

void SettingsCard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const float w = static_cast<float>(width()), h = static_cast<float>(height());
    d.rectFilled({0.5f, 0.5f}, {w - 0.5f, h - 0.5f}, theme::kBgRaised, metrics::kTileRadius);
    d.rect({0.5f, 0.5f}, {w - 0.5f, h - 0.5f}, theme::kBorder, metrics::kTileRadius, 1.0f);
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
