#pragma once

// Editor pieces on top of bmstyle's controls: the Artisan mark, a color button, the settings
// card, labels with a size, and a software preview of a scene (home screen thumbnails).

#include "artisan/core/Scene.h"

#include "bmstyle/BmStyle.h"

#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QWidget>

#include <functional>

namespace artisan::editor::ui {

using namespace bmstyle;
namespace theme = bmstyle::theme;

// The Artisan mark: a disc with an "A" cut through it, as bmstyle's Icon::Logo.
void drawLogoMark(QPainter& p, Pt center, float radius, Rgba color);

class LogoMark : public QWidget {
public:
    explicit LogoMark(int size) { setFixedSize(size, size); }

protected:
    void paintEvent(QPaintEvent*) override;
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

// A group of settings rows in a raised card (Settings, dialogs).
class SettingsCard : public QFrame {
public:
    SettingsCard();
    void addRow(const QString& title, const QString& note, QWidget* control);
    void addContent(QWidget* w);

protected:
    void paintEvent(QPaintEvent*) override;
};

// The scene as QPainter draws it (no GPU): shapes, sprites as boxes, lights as soft discs.
void paintScenePreview(QPainter& p, const Scene& scene, const QRectF& target, QSizeF design);

QColor qcolor(const Color& c);
Color fromQColor(const QColor& c);

} // namespace artisan::editor::ui
