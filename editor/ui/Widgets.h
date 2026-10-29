#pragma once

// Editor pieces on top of bestyle's controls: the Artisan mark, a color button, labels with a
// size, and a software preview of a scene (home screen thumbnails). Settings cards and paged
// windows are bestyle's (bestyle/Pages.h).

#include "artisan/core/Scene.h"

#include "bestyle/BeStyle.h"
#include "bestyle/Pages.h"

#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QWidget>

#include <functional>

namespace artisan::editor::ui {

using namespace bestyle;
namespace theme = bestyle::theme;

// The Artisan mark: a flat triangle opened into an "A", lit from its peak, as bestyle's Icon::Logo.
// Always in the brand's teal (a green-leaning cyan), whatever the theme's accent.
void drawLogoMark(QPainter& p, Pt center, float radius, Rgba color);
inline constexpr Rgba kLogoCyan = rgba(32, 212, 176);
// The application's icon: the mark on a dark rounded square, `size` pixels wide.
void drawAppIcon(QPainter& p, float size);
// A plugin's icon by its name ("star", "magnet"...; docs/PLUGINS.md lists them).
Icon pluginIcon(const std::string& name);

class LogoMark : public QWidget {
public:
    explicit LogoMark(int size) { setFixedSize(size, size); }

protected:
    void paintEvent(QPaintEvent*) override;
};

// The mark as a button (the menu bar's way back to the projects).
class LogoButton : public QAbstractButton {
public:
    explicit LogoButton(int size, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    int size_;
};

// A label in a tone, larger (scale) and / or bold.
QLabel* label(const QString& text, Tone tone = Tone::Text, float scale = 1.0f, bool bold = false);

// A swatch: click to pick a color (with alpha).
class ColorButton : public QWidget {
    Q_OBJECT
public:
    explicit ColorButton(QWidget* parent = nullptr);
    void setColor(const Color& c) {
        color_ = c;
        update();
    }
    Color color() const { return color_; }
    QSize sizeHint() const override { return {64, 24}; }

signals:
    void picked(const Color& c); // while the dialog is open, too

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    Color color_;
};

// The scene as QPainter draws it (no GPU): shapes, sprites as boxes, lights as soft discs.
void paintScenePreview(QPainter& p, const Scene& scene, const QRectF& target, QSizeF design);

QColor qcolor(const Color& c);
Color fromQColor(const QColor& c);

} // namespace artisan::editor::ui
