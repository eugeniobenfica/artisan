#include "bestyle/Pages.h"

#include "bestyle/BeStyle.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace bestyle {

PagedWindow::PagedWindow(const QString& title, std::vector<Page> pages, QWidget* footer, QWidget* parent)
    : QWidget(parent, Qt::Window), pages_(std::move(pages)) {
    setWindowTitle(translate(title));
    setProperty("chrome", true); // an app's own window frame, if it draws one, goes around it
    resize(860, 640);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(12, 12, 12, 12);
    row->setSpacing(12);
    auto* side = new QVBoxLayout;
    list_ = new QListWidget;
    list_->setFixedWidth(170);
    list_->setIconSize(QSize(16, 16));
    list_->setFrameShape(QFrame::NoFrame);
    for (const Page& p : pages_) {
        auto* item = new QListWidgetItem(makeIcon(p.icon, theme::kTextDim, 32), translate(p.label));
        item->setSizeHint(QSize(160, 36));
        list_->addItem(item);
    }
    connect(list_, &QListWidget::currentRowChanged, this, [this](int r) {
        if (r >= 0 && r != current_) showPage(r);
    });
    side->addWidget(list_, 1);
    if (footer) side->addWidget(footer);
    row->addLayout(side);
    auto* line = new QFrame;
    line->setFrameShape(QFrame::VLine);
    setTone(line, Tone::Border);
    row->addWidget(line);
    area_ = new QScrollArea;
    area_->setWidgetResizable(true);
    area_->setFrameShape(QFrame::NoFrame);
    area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    row->addWidget(area_, 1);
}

void PagedWindow::showPage(int index) {
    if (index < 0 || index >= static_cast<int>(pages_.size())) return;
    const bool same = index == current_;
    const int scroll = same ? area_->verticalScrollBar()->value() : 0;
    current_ = index;
    {
        const QSignalBlocker b(list_);
        list_->setCurrentRow(index);
    }
    area_->setWidget(pages_[static_cast<size_t>(index)].build());
    area_->verticalScrollBar()->setValue(scroll);
    // The new page is laid out a moment later: until then its scroll range is too short.
    if (scroll > 0) QTimer::singleShot(0, this, [this, scroll, index] {
            if (current_ == index) area_->verticalScrollBar()->setValue(scroll);
        });
}

void PagedWindow::rebuild() { showPage(current_); }

void PagedWindow::present() {
    show();
    if (isMinimized()) showNormal();
    raise();
    activateWindow();
}

void PagedWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) close();
    else QWidget::keyPressEvent(e);
}

// --- SettingsCard ----------------------------------------------------------------------------

SettingsCard::SettingsCard() {
    col_ = new QVBoxLayout(this);
    col_->setContentsMargins(0, 0, 0, 0);
    col_->setSpacing(0);
}

QWidget* SettingsCard::addRow(const QString& title, const QString& description, QWidget* control, bool below) {
    auto* row = new QWidget;
    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    auto* t = new QLabel(translate(title));
    t->setWordWrap(true);
    setTone(t, Tone::Text);
    text->addWidget(t);
    if (!description.isEmpty()) {
        auto* d = new QLabel(translate(description));
        d->setWordWrap(true);
        QFont f = d->font();
        f.setPointSizeF(f.pointSizeF() * 0.92);
        d->setFont(f);
        setTone(d, Tone::TextFaint);
        text->addWidget(d);
    }
    if (below) {
        auto* col = new QVBoxLayout(row);
        col->setContentsMargins(16, 12, 16, 12);
        col->setSpacing(8);
        col->addLayout(text);
        if (control) col->addWidget(control);
    } else {
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(16, 10, 16, 10);
        h->setSpacing(16);
        h->addLayout(text, 1);
        if (control) h->addWidget(control, 0, Qt::AlignVCenter | Qt::AlignRight);
    }
    row->setMinimumHeight(48);
    rows_.push_back(row);
    col_->addWidget(row);
    return row;
}

QWidget* SettingsCard::addContent(QWidget* content) {
    auto* row = new QWidget;
    auto* h = new QVBoxLayout(row);
    h->setContentsMargins(16, 12, 16, 12);
    h->addWidget(content);
    rows_.push_back(row);
    col_->addWidget(row);
    return row;
}

QWidget* SettingsCard::addLayout(QLayout* content) {
    auto* row = new QWidget;
    auto* h = new QVBoxLayout(row);
    h->setContentsMargins(16, 12, 16, 12);
    h->addLayout(content);
    rows_.push_back(row);
    col_->addWidget(row);
    return row;
}

void SettingsCard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const float r = theme::current().rounding + 3.0f;
    d.rectFilled(Pt(0, 0), Pt(static_cast<float>(width()), static_cast<float>(height())), theme::kBgRaised, r);
    // Thin lines between the visible rows.
    bool first = true;
    for (QWidget* row : rows_) {
        if (!row->isVisible()) continue;
        if (!first) d.line(Pt(16, row->y() + 0.5f), Pt(width() - 16.0f, row->y() + 0.5f), theme::kBorder, 1.0f);
        first = false;
    }
}

SettingsCard* addSection(QVBoxLayout* col, const QString& title, const QString& hint) {
    if (col->count() > 1) col->addSpacing(10);
    if (!title.isEmpty()) col->addWidget(sectionLabel(title));
    if (!hint.isEmpty()) col->addWidget(hintLabel(hint));
    auto* card = new SettingsCard;
    col->addWidget(card);
    return card;
}

QWidget* pageWidget(QVBoxLayout*& col) {
    auto* w = new QWidget;
    col = new QVBoxLayout(w);
    col->setContentsMargins(8, 4, 16, 20);
    col->setSpacing(6);
    col->setAlignment(Qt::AlignTop);
    return w;
}

QLabel* pageTitle(const QString& text) {
    auto* l = new QLabel(translate(text));
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 1.15);
    f.setWeight(QFont::DemiBold);
    l->setFont(f);
    setTone(l, Tone::Text);
    return l;
}

void pageHeader(QVBoxLayout* col, const QString& title, QWidget* right) {
    auto* row = new QHBoxLayout;
    row->addWidget(pageTitle(title), 1);
    if (right) row->addWidget(right);
    col->addLayout(row);
    col->addSpacing(2);
}

QLabel* paragraph(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    l->setOpenExternalLinks(true);
    setTone(l, Tone::TextDim);
    return l;
}

QLabel* hintLabel(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    setTone(l, Tone::TextFaint);
    return l;
}

QWidget* buttonRow(std::initializer_list<QWidget*> buttons) {
    auto* box = new QWidget;
    auto* h = new QHBoxLayout(box);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);
    for (QWidget* b : buttons) h->addWidget(b);
    return box;
}

// --- Swatch, ThemeCard -------------------------------------------------------------------------

Swatch::Swatch(Rgba color, bool selected, const QString& tip, QWidget* parent) : QAbstractButton(parent), color_(color), selected_(selected) {
    setFixedSize(28, 28);
    setCursor(Qt::PointingHandCursor);
    if (!tip.isEmpty()) setToolTip(translate(tip));
}

void Swatch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const Pt c(14, 14);
    const float r = 12.0f;
    if (selected_) d.circle(c, r, theme::kText, 2.0f);
    d.circleFilled(c, r - (selected_ ? 3.5f : 1.0f), color_);
    if (underMouse() && !selected_) d.circle(c, r, theme::ink(90));
}

ThemeCard::ThemeCard(const theme::Palette& palette, bool selected, QWidget* parent)
    : QAbstractButton(parent), palette_(theme::resolved(palette)), selected_(selected) {
    setFixedSize(std::max(120, fontMetrics().horizontalAdvance(translate(QString::fromStdString(palette_.name))) + 20),
                 50 + fontMetrics().height() + 12);
    setCursor(Qt::PointingHandCursor);
    setToolTip(translate(QString::fromStdString(palette_.name)));
}

void ThemeCard::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    Draw d(painter);
    const theme::Palette& c = palette_;
    const float w = static_cast<float>(width()), h = static_cast<float>(height());
    d.rectFilled({0, 0}, {w, h}, c.light ? c.bgRaised : c.bgDeep, 8.0f);
    d.rectFilled({8, 8}, {52, 46}, c.bgPanel, 4.0f);
    d.rectFilled({58, 8}, {w - 8, 24}, c.light ? c.bgDeep : c.bgRaised, 4.0f);
    d.rectFilled({58, 30}, {w - 24, 42}, c.accent, 3.0f);
    d.rectFilled({14, 16}, {44, 20}, c.textDim, 2.0f);
    d.rectFilled({14, 26}, {36, 30}, c.textFaint, 2.0f);
    d.text({8, h - d.textHeight() - 6}, c.text, translate(QString::fromStdString(c.name)));
    if (selected_ || underMouse()) d.rect({1, 1}, {w - 1, h - 1}, selected_ ? theme::kAccent : theme::ink(60), 8.0f, selected_ ? 2.0f : 1.0f);
}

} // namespace bestyle
