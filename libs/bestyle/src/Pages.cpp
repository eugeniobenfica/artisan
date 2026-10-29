#include "bestyle/Pages.h"

#include "bestyle/BeStyle.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QCheckBox>
#include <QMouseEvent>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <cstdio>

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

namespace {

// A click on a row picks the radio button or check box at its end (its text is part of it).
class RowClick : public QObject {
public:
    RowClick(QWidget* row, QAbstractButton* button) : QObject(row), button_(button) {}
    bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() != QEvent::MouseButtonRelease || !button_->isEnabled()) return false;
        if (static_cast<QMouseEvent*>(e)->button() != Qt::LeftButton) return false;
        if (!qobject_cast<QRadioButton*>(button_) || !button_->isChecked()) button_->click();
        return true;
    }

private:
    QAbstractButton* button_;
};

void clickableRow(QWidget* row, QWidget* control) {
    auto* button = qobject_cast<QAbstractButton*>(control);
    if (!button || !(qobject_cast<QRadioButton*>(button) || qobject_cast<QCheckBox*>(button))) return;
    row->setCursor(Qt::PointingHandCursor);
    row->installEventFilter(new RowClick(row, button));
    for (QLabel* l : row->findChildren<QLabel*>()) l->setAttribute(Qt::WA_TransparentForMouseEvents);
}

} // namespace

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
    clickableRow(row, control);
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

QWidget* sliderControl(float value, float min, float max, float step, const char* format, std::function<void(float)> changed) {
    auto* box = new QWidget;
    auto* h = new QHBoxLayout(box);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setFixedWidth(190);
    slider->setRange(0, static_cast<int>(std::lround((max - min) / step)));
    slider->setValue(static_cast<int>(std::lround((value - min) / step)));
    h->addWidget(slider);
    auto* readout = new QLabel;
    readout->setMinimumWidth(48);
    readout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setTone(readout, Tone::TextDim);
    auto show = [readout, f = std::string(format)](float v) {
        char buf[32];
        std::snprintf(buf, sizeof buf, f.c_str(), v);
        readout->setText(buf);
    };
    show(value);
    h->addWidget(readout);
    QObject::connect(slider, &QSlider::valueChanged, box, [=](int i) {
        const float v = min + static_cast<float>(i) * step;
        show(v);
        changed(v);
    });
    return box;
}

// --- KeyCaps, Chip, IconTile, ShortcutEdit -----------------------------------------------------

KeyCaps::KeyCaps(const QString& keys, QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    const QStringList combos = keys.split(" / ");
    for (int c = 0; c < combos.size(); ++c) {
        if (c) parts_.push_back({" / ", true});
        // "+" joins keys, except a key that is itself "+" ("Ctrl++").
        const QString& combo = combos[c];
        QStringList names;
        int start = 0;
        for (int i = 1; i <= combo.size(); ++i)
            if (i == combo.size() || (combo[i] == '+' && i > start)) {
                names << combo.mid(start, i - start);
                start = i + 1;
                ++i; // the key after a "+" may be "+" itself
            }
        for (int k = 0; k < names.size(); ++k) {
            if (k) parts_.push_back({"+", true});
            parts_.push_back({translate(names[k]), false});
        }
    }
}

QSize KeyCaps::sizeHint() const {
    const QFontMetricsF fm(font());
    const float h = static_cast<float>(fm.height()) + 8.0f;
    float w = 0;
    for (const Part& p : parts_)
        w += p.separator ? static_cast<float>(fm.horizontalAdvance(p.text)) + 6 : std::max(h, static_cast<float>(fm.horizontalAdvance(p.text)) + 14.0f) + 3;
    return {static_cast<int>(std::ceil(w)) + 4, fontMetrics().height() + 12};
}

void KeyCaps::paintEvent(QPaintEvent*) {
    // Each key a raised cap: its face in the card's color, outlined, over a darker side.
    QPainter p(this);
    Draw d(p);
    const float h = static_cast<float>(fontMetrics().height()) + 8.0f;
    float x = static_cast<float>(width() - sizeHint().width()); // against the right edge
    const float top = std::max(0.0f, (height() - h - 2) * 0.5f);
    for (const Part& part : parts_) {
        if (part.separator) {
            d.text({x + 3, top + (h - d.textHeight()) * 0.5f}, theme::kTextFaint, part.text);
            x += d.textWidth(part.text) + 6;
            continue;
        }
        const float w = std::max(h, d.textWidth(part.text) + 14.0f);
        const float r = std::min(6.0f, h * 0.25f);
        d.rectFilled({x, top + 2}, {x + w, top + h + 2}, theme::kBorder, r); // the key's side
        d.rectFilled({x, top}, {x + w, top + h}, theme::kBgRaised, r);
        d.rect({x, top}, {x + w, top + h}, theme::kBorder, r);
        d.text({x + (w - d.textWidth(part.text)) * 0.5f, top + (h - d.textHeight()) * 0.5f}, theme::kText, part.text);
        x += w + 3;
    }
}

Chip::Chip(const QString& text, Tone tone, QWidget* parent) : QWidget(parent), text_(translate(text)), tone_(tone) {
    setFixedSize(fontMetrics().horizontalAdvance(text_) + 18, fontMetrics().height() + 8);
}

void Chip::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const Rgba ink = tone_ == Tone::Accent ? theme::kAccent : tone_ == Tone::Danger ? theme::kDanger : theme::kTextDim;
    const Rgba fill = tone_ == Tone::Accent ? theme::accent(30) : tone_ == Tone::Danger ? theme::withAlpha(theme::kDanger, 30) : theme::kField;
    d.rectFilled(Pt(0, 0), Pt(static_cast<float>(width()), static_cast<float>(height())), fill, height() * 0.5f);
    d.text(Pt(9, (height() - d.textHeight()) * 0.5f), ink, text_);
}

IconTile::IconTile(Icon icon, bool lit, int size, QWidget* parent) : QWidget(parent), icon_(icon), lit_(lit) { setFixedSize(size, size); }

void IconTile::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Draw d(p);
    const float w = static_cast<float>(width());
    d.rectFilled(Pt(0, 0), Pt(w, w), lit_ ? theme::accent(38) : theme::kField, theme::current().rounding + 3.0f);
    drawIcon(d, icon_, Pt(w * 0.5f, w * 0.5f), w * 0.55f, lit_ ? theme::kAccent : theme::kTextFaint);
}

ShortcutEdit::ShortcutEdit(const QKeySequence& keys, QWidget* parent) : QPushButton(parent) {
    setFixedWidth(160);
    setProperty("keycap", true);
    setCursor(Qt::PointingHandCursor);
    setToolTip(translate("Click, then press the new keys (Esc cancels)"));
    setKeys(keys);
    connect(this, &QPushButton::clicked, this, [this] {
        capturing_ = true;
        setText(translate("Press keys..."));
        setProperty("accent", true);
        style()->unpolish(this);
        style()->polish(this);
        grabKeyboard();
    });
}

void ShortcutEdit::setKeys(const QKeySequence& keys) {
    keys_ = keys;
    if (!capturing_) setText(keys.isEmpty() ? translate("None") : keys.toString(QKeySequence::NativeText));
}

bool ShortcutEdit::event(QEvent* e) {
    // While capturing, every key (the window's shortcuts too) comes here.
    if (capturing_ && e->type() == QEvent::ShortcutOverride) {
        e->accept();
        return true;
    }
    return QPushButton::event(e);
}

void ShortcutEdit::keyPressEvent(QKeyEvent* e) {
    if (!capturing_) return QPushButton::keyPressEvent(e);
    const int key = e->key();
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_unknown) return;
    const bool cancel = key == Qt::Key_Escape && e->modifiers() == Qt::NoModifier;
    const QKeySequence pressed(e->keyCombination());
    stop();
    if (!cancel) {
        setKeys(pressed);
        emit chosen(pressed);
    }
}

void ShortcutEdit::focusOutEvent(QFocusEvent* e) {
    if (capturing_) stop();
    QPushButton::focusOutEvent(e);
}

void ShortcutEdit::stop() {
    capturing_ = false;
    releaseKeyboard();
    setProperty("accent", false);
    style()->unpolish(this);
    style()->polish(this);
    setKeys(keys_);
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
