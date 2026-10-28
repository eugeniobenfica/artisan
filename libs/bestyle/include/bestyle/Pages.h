#pragma once

// Windows of pages, as bemotion's Settings and Help: a list of pages on the left (icon and name,
// with an optional footer under it), the chosen page on the right, scrolling. Pages are made of
// sections: a small capitals title over a SettingsCard, a rounded card of rows (a title and a
// short description on the left, the control on the right), as in the system settings of Windows
// and macOS. Also the theme cards and color swatches a Theme page shows.
//
// Not included by BeStyle.h: an app with pages of its own includes it where it uses it.

#include "bestyle/Theme.h"
#include "bestyle/Widgets.h"

#include <QAbstractButton>
#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QLayout;
class QListWidget;
class QScrollArea;
class QVBoxLayout;

namespace bestyle {

class PagedWindow : public QWidget {
    Q_OBJECT
public:
    struct Page {
        QString label;
        Icon icon;
        std::function<QWidget*()> build; // made when shown, again by rebuild()
    };
    PagedWindow(const QString& title, std::vector<Page> pages, QWidget* footer = nullptr, QWidget* parent = nullptr);
    void showPage(int index);
    int currentPage() const { return current_; }
    void rebuild(); // the current page again, keeping its scroll position
    void present(); // shows, raises and focuses the window

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    std::vector<Page> pages_;
    QListWidget* list_ = nullptr;
    QScrollArea* area_ = nullptr;
    int current_ = -1;
};

class SettingsCard : public QWidget {
public:
    SettingsCard();
    // `control` on the right (or, `below`, under the text: wide controls). Returns the row.
    QWidget* addRow(const QString& title, const QString& description, QWidget* control, bool below = false);
    QWidget* addContent(QWidget* content); // a row of its own, padded (grids, previews, lists)
    QWidget* addLayout(QLayout* content);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QVBoxLayout* col_;
    std::vector<QWidget*> rows_;
};

// A section: its small capitals title (and a hint), then its card, added to `col`.
SettingsCard* addSection(QVBoxLayout* col, const QString& title, const QString& hint = {});

QWidget* pageWidget(QVBoxLayout*& col); // a page and its column
QLabel* pageTitle(const QString& text);  // the page's bold title
// The title with a button on its right (Reset Page...), added to `col`.
void pageHeader(QVBoxLayout* col, const QString& title, QWidget* right = nullptr);
QLabel* paragraph(const QString& text); // wrapped secondary text
QLabel* hintLabel(const QString& text); // wrapped faint text
QWidget* buttonRow(std::initializer_list<QWidget*> buttons);

// A round color choice (accent colors); `selected` draws a ring around it.
class Swatch : public QAbstractButton {
public:
    Swatch(Rgba color, bool selected, const QString& tip = {}, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    Rgba color_;
    bool selected_;
};

// A theme as a card: a small drawing of its panels, text and accent, its name under it.
class ThemeCard : public QAbstractButton {
public:
    ThemeCard(const theme::Palette& palette, bool selected, QWidget* parent = nullptr);
    const theme::Palette& palette() const { return palette_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    theme::Palette palette_;
    bool selected_;
};

} // namespace bestyle
