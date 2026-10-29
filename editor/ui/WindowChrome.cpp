#include "ui/WindowChrome.h"

#include "ui/Widgets.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace artisan::editor::ui {

namespace {

constexpr int kHeaderHeight = 34;
constexpr int kEdge = 6; // how close to an edge a drag resizes

// The title and a close button; dragged, it moves the window.
class Header : public QWidget {
public:
    explicit Header(QWidget* window) : QWidget(window), window_(window) {
        setObjectName("artisanWindowHeader");
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(14, 0, 6, 0);
        title_ = new QLabel(window->windowTitle());
        QFont f = title_->font();
        f.setWeight(QFont::DemiBold);
        title_->setFont(f);
        setTone(title_, Tone::TextDim);
        row->addWidget(title_, 1);
        auto* close = new IconButton(Icon::Close, QObject::tr("Close"), 26);
        QObject::connect(close, &QAbstractButton::clicked, window, [window] { window->close(); });
        row->addWidget(close);
    }
    void setTitle(const QString& text) { title_->setText(text); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        d.rectFilled(Pt(0, 0), Pt(static_cast<float>(width()), static_cast<float>(height())), theme::kHeader);
        d.line(Pt(0, height() - 0.5f), Pt(static_cast<float>(width()), height() - 0.5f), theme::kBorder, 1.0f);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && window_->windowHandle()) window_->windowHandle()->startSystemMove();
    }

private:
    QWidget* window_;
    QLabel* title_;
};

// A thin outline around the whole window (a frameless window has no border of its own).
class Outline : public QWidget {
public:
    explicit Outline(QWidget* window) : QWidget(window) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setObjectName("artisanWindowOutline");
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setPen(QPen(theme::q(theme::kBorder), 1));
        p.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    }
};

bool resizable(const QWidget* w) { return w->minimumSize() != w->maximumSize(); }

Qt::Edges edgesAt(const QWidget* w, QPoint p) {
    Qt::Edges edges;
    if (p.x() < kEdge) edges |= Qt::LeftEdge;
    if (p.x() >= w->width() - kEdge) edges |= Qt::RightEdge;
    if (p.y() < kEdge) edges |= Qt::TopEdge;
    if (p.y() >= w->height() - kEdge) edges |= Qt::BottomEdge;
    return edges;
}

Qt::CursorShape cursorFor(Qt::Edges e) {
    if ((e & Qt::LeftEdge && e & Qt::TopEdge) || (e & Qt::RightEdge && e & Qt::BottomEdge)) return Qt::SizeFDiagCursor;
    if ((e & Qt::RightEdge && e & Qt::TopEdge) || (e & Qt::LeftEdge && e & Qt::BottomEdge)) return Qt::SizeBDiagCursor;
    if (e & (Qt::LeftEdge | Qt::RightEdge)) return Qt::SizeHorCursor;
    if (e & (Qt::TopEdge | Qt::BottomEdge)) return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

class Chrome : public QObject {
public:
    bool eventFilter(QObject* watched, QEvent* e) override {
        auto* w = qobject_cast<QWidget*>(watched);
        if (!w || !w->isWindow()) return false;
        // Before a window is first shown: framed by us instead of the system.
        if (e->type() == QEvent::Polish && !w->property("artisanChromed").toBool() && wanted(w)) decorate(w);
        if (!w->property("artisanChromed").toBool()) return false;
        switch (e->type()) {
        case QEvent::Resize:
        case QEvent::Show:
            layoutChrome(w);
            break;
        case QEvent::WindowTitleChange:
            if (auto* h = static_cast<Header*>(w->findChild<QWidget*>("artisanWindowHeader", Qt::FindDirectChildrenOnly))) h->setTitle(w->windowTitle());
            break;
        case QEvent::ChildAdded: // the outline stays on top of whatever comes later
            if (auto* o = w->findChild<QWidget*>("artisanWindowOutline", Qt::FindDirectChildrenOnly)) o->raise();
            break;
        case QEvent::MouseMove: { // only where no child is: the window's margins, its edges
            auto* m = static_cast<QMouseEvent*>(e);
            if (m->buttons() == Qt::NoButton) w->setCursor(resizable(w) ? cursorFor(edgesAt(w, m->position().toPoint())) : Qt::ArrowCursor);
            break;
        }
        case QEvent::Leave:
            w->unsetCursor();
            break;
        case QEvent::MouseButtonPress: {
            auto* m = static_cast<QMouseEvent*>(e);
            const Qt::Edges edges = edgesAt(w, m->position().toPoint());
            if (m->button() == Qt::LeftButton && edges && resizable(w) && w->windowHandle()) {
                w->windowHandle()->startSystemResize(edges);
                return true;
            }
            break;
        }
        default:
            break;
        }
        return false;
    }

private:
    static bool wanted(const QWidget* w) {
        const Qt::WindowType type = w->windowType();
        if (type != Qt::Dialog && type != Qt::Window) return false; // popups, tool tips, splash...
        if (w->windowFlags() & Qt::FramelessWindowHint) return false; // drawn its own way already
        return qobject_cast<const QDialog*>(w) || w->property("chrome").toBool();
    }
    static void decorate(QWidget* w) {
        w->setProperty("artisanChromed", true);
        w->setWindowFlags(w->windowFlags() | Qt::FramelessWindowHint);
        const QMargins m = w->contentsMargins();
        w->setContentsMargins(m.left(), m.top() + kHeaderHeight, m.right(), m.bottom());
        new Header(w);
        new Outline(w);
        w->setMouseTracking(true);
        layoutChrome(w);
    }
    static void layoutChrome(QWidget* w) {
        if (auto* h = static_cast<Header*>(w->findChild<QWidget*>("artisanWindowHeader", Qt::FindDirectChildrenOnly))) {
            h->setGeometry(1, 1, w->width() - 2, kHeaderHeight - 1);
            h->raise();
        }
        if (auto* o = w->findChild<QWidget*>("artisanWindowOutline", Qt::FindDirectChildrenOnly)) {
            o->setGeometry(w->rect());
            o->raise();
        }
    }
};

} // namespace

void installWindowChrome() {
    static Chrome chrome;
    qApp->installEventFilter(&chrome);
}

void tintTitleBar(QWidget* window) {
#ifdef _WIN32
    using SetAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static const auto set = [] {
        const HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        return dwm ? reinterpret_cast<SetAttribute>(reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute"))) : nullptr;
    }();
    if (!set) return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const Rgba header = theme::kHeader;
    const BOOL dark = red(header) + green(header) + blue(header) < 3 * 128; // the buttons' glyphs
    // COLORREF is 0x00BBGGRR, as Rgba without its alpha.
    const COLORREF caption = header & 0xFFFFFF, text = theme::kTextDim & 0xFFFFFF, border = theme::kBorder & 0xFFFFFF;
    set(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
    set(hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof border);
    set(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof caption);
    set(hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof text);
#else
    (void)window;
#endif
}

} // namespace artisan::editor::ui
