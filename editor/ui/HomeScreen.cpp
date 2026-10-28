#include "ui/HomeScreen.h"

#include "app/Platform.h"
#include "app/Settings.h"
#include "ui/Widgets.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace artisan::editor {

using namespace ui;

namespace {

struct Resolution {
    const char* label;
    int width, height;
};

constexpr Resolution kResolutions[] = {
    {"HD  ·  1280 × 720", 1280, 720},
    {"Full HD  ·  1920 × 1080", 1920, 1080},
    {"Pixel art  ·  640 × 360", 640, 360},
    {"Retro  ·  320 × 180", 320, 180},
    {"Portrait (mobile)  ·  720 × 1280", 720, 1280},
    {"Square  ·  1080 × 1080", 1080, 1080},
};
constexpr int kCardMin = 240, kGap = 20;

QString relativeTime(int64_t unixSeconds) {
    using namespace std::chrono;
    const int64_t now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    const int64_t d = std::max<int64_t>(0, now - unixSeconds);
    if (d < 60) return QObject::tr("just now");
    if (d < 3600) return QObject::tr("%1 min ago").arg(d / 60);
    if (d < 86400) return QObject::tr("%1 h ago").arg(d / 3600);
    if (d < 86400 * 30) return d / 86400 == 1 ? QObject::tr("1 day ago") : QObject::tr("%1 days ago").arg(d / 86400);
    char buf[32];
    const std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::localtime(&t));
    return buf;
}

std::filesystem::path projectsFolder() { return platform::fromUtf8(settings().projectsFolder); }

// A project: its scene's preview, name and details; click to open, "..." (or right-click) for more.
class ProjectCard : public QWidget {
public:
    ProjectCard(QString name, QString meta, const QImage* image, std::function<void()> open, std::function<void(QPoint)> menu)
        : name_(std::move(name)), meta_(std::move(meta)), image_(image), open_(std::move(open)), menu_(std::move(menu)) {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setToolTip(name_);
    }
    void setCardWidth(int w) { setFixedSize(w, w * 9 / 16 + 62); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        const bool hot = underMouse();
        const float w = static_cast<float>(width()), h = static_cast<float>(height());
        const float thumbH = w * 9.0f / 16.0f;
        d.rectFilled({0, 0}, {w, h}, hot ? theme::kBgHover : theme::kBgPanel, 8.0f);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(0, 0, w, thumbH + 8), 8, 8);
        p.save();
        p.setClipPath(clip);
        p.setClipRect(QRectF(0, 0, w, thumbH), Qt::IntersectClip);
        if (image_ && !image_->isNull()) p.drawImage(QRectF(0, 0, w, thumbH), *image_);
        else {
            p.fillRect(QRectF(0, 0, w, thumbH), QColor(10, 10, 12));
            drawLogoMark(p, {w * 0.5f, thumbH * 0.5f}, 18.0f, theme::kTextFaint);
        }
        p.restore();
        d.rect({0.5f, 0.5f}, {w - 0.5f, h - 0.5f}, hot ? theme::kAccent : theme::kBorder, 8.0f, hot ? 1.5f : 1.0f);
        if (hot) {
            const QRectF r = dotsRect();
            d.rectFilled(Pt(r.topLeft()), Pt(r.bottomRight()), r.contains(mapFromGlobal(QCursor::pos())) ? theme::kBgHover : theme::shade(160), 6.0f);
            for (int i = -1; i <= 1; ++i)
                d.circleFilled({static_cast<float>(r.center().x()) + i * 6.0f, static_cast<float>(r.center().y())}, 2.0f, theme::kText);
        }
        p.setClipRect(QRectF(12, 0, w - 24, h));
        QFont bold = font();
        bold.setWeight(QFont::DemiBold);
        p.setFont(bold);
        const int room = static_cast<int>(w) - 28;
        d.text({14, thumbH + 12}, theme::kText, QFontMetrics(bold).elidedText(name_, Qt::ElideRight, room));
        p.setFont(font());
        d.text({14, thumbH + 34}, theme::kTextFaint, fontMetrics().elidedText(meta_, Qt::ElideRight, room));
    }
    void mouseMoveEvent(QMouseEvent*) override { update(); }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (!rect().contains(e->position().toPoint())) return;
        if (e->button() == Qt::RightButton || (e->button() == Qt::LeftButton && dotsRect().contains(e->position())))
            menu_(e->globalPosition().toPoint());
        else if (e->button() == Qt::LeftButton) open_();
    }

private:
    QRectF dotsRect() const { return {width() - 28.0 - 8, 8, 28, 28}; }
    QString name_, meta_;
    const QImage* image_;
    std::function<void()> open_;
    std::function<void(QPoint)> menu_;
};

} // namespace

HomeScreen::HomeScreen(QWidget* parent) : QWidget(parent) {
    setAutoFillBackground(true);
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    buildUi();
    // Projects saved, copied or removed in the projects folder show up by themselves.
    watcher_ = new QFileSystemWatcher(this);
    if (const auto folder = projectsFolder(); !folder.empty()) watcher_->addPath(platform::toQt(folder));
    auto* later = new QTimer(this);
    later->setSingleShot(true);
    later->setInterval(400);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, later, qOverload<>(&QTimer::start));
    connect(later, &QTimer::timeout, this, [this] {
        if (isVisible()) refresh();
    });
}

void HomeScreen::rebuild() {
    const QString search = search_ ? search_->text() : QString();
    buildUi();
    search_->setText(search);
    rebuildGrid();
}

void HomeScreen::buildUi() {
    if (content_) {
        layout()->removeWidget(content_);
        content_->deleteLater();
    }
    content_ = new QWidget;
    layout()->addWidget(content_);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, theme::q(theme::kBgDeep));
    setPalette(pal);
    columns_ = 0;
    auto* row = new QHBoxLayout(content_);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    // --- sidebar -----------------------------------------------------------------------
    auto* sidebar = new QFrame;
    sidebar->setFixedWidth(270);
    sidebar->setAutoFillBackground(true);
    QPalette sp = sidebar->palette();
    sp.setColor(QPalette::Window, theme::q(theme::kBgPanel));
    sidebar->setPalette(sp);
    auto* side = new QVBoxLayout(sidebar);
    side->setContentsMargins(22, 26, 22, 22);
    side->setSpacing(8);
    auto* brand = new QHBoxLayout;
    brand->setSpacing(12);
    brand->addWidget(new LogoMark(40));
    auto* names = new QVBoxLayout;
    names->setSpacing(0);
    names->addWidget(label("Artisan", Tone::Text, 1.6f, true));
    names->addWidget(label(QString("v%1").arg(ARTISAN_VERSION), Tone::TextFaint));
    brand->addLayout(names, 1);
    side->addLayout(brand);
    side->addSpacing(18);
    auto* create = accentButton(tr("+  New Project"));
    create->setMinimumHeight(38);
    connect(create, &QPushButton::clicked, this, &HomeScreen::openNewProjectDialog);
    side->addWidget(create);
    auto* open = new QPushButton(tr("Open Project..."));
    open->setMinimumHeight(34);
    connect(open, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Open Artisan Project"), platform::toQt(projectsFolder()),
                                                          tr("Artisan Project (project.artisan)"));
        if (!file.isEmpty() && onOpen) onOpen(platform::fromQt(file).parent_path());
    });
    side->addWidget(open);
    side->addStretch(1);
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    setTone(line, Tone::Border);
    side->addWidget(line);
    auto* folder = new QPushButton(makeIcon(Icon::Folder, theme::kTextDim, 24), tr("Projects Folder"));
    folder->setIconSize(QSize(24, 24));
    folder->setToolTip(projectsFolder().empty() ? tr("Choose where your projects live") : platform::toQt(projectsFolder()));
    connect(folder, &QPushButton::clicked, this, [this, folder] {
        if (projectsFolder().empty()) {
            chooseProjectsFolder();
            return;
        }
        QMenu menu;
        menu.addAction(menuIcon(Icon::Folder, theme::kTextDim), tr("Show in File Manager"), [] { platform::revealInFileManager(projectsFolder()); });
        menu.addAction(tr("Change Folder..."), [this] { chooseProjectsFolder(); });
        menu.exec(folder->mapToGlobal(QPoint(0, folder->height())));
    });
    side->addWidget(folder);
    side->addSpacing(2);
    auto* footer = new QHBoxLayout;
    footer->setSpacing(6);
    footer->addStretch(1);
    auto* gear = new IconButton(Icon::Gear, tr("Settings (Ctrl+,)"), 30);
    connect(gear, &QAbstractButton::clicked, this, [this] {
        if (onSettings) onSettings();
    });
    footer->addWidget(gear);
    auto* help = new IconButton(Icon::Help, tr("Documentation"), 30);
    connect(help, &QAbstractButton::clicked, this, [] { platform::openWithDefaultApp("https://github.com/eugeniobenfica/artisan#readme"); });
    footer->addWidget(help);
    side->addLayout(footer);
    row->addWidget(sidebar);

    // --- projects ----------------------------------------------------------------------
    auto* main = new QVBoxLayout;
    main->setContentsMargins(36, 30, 36, 0);
    main->setSpacing(6);
    auto* head = new QHBoxLayout;
    head->addWidget(label(tr("Projects"), Tone::Text, 1.8f, true));
    head->addStretch(1);
    search_ = new QLineEdit;
    search_->setPlaceholderText(tr("Search projects"));
    search_->setClearButtonEnabled(true);
    search_->setFixedWidth(260);
    connect(search_, &QLineEdit::textChanged, this, [this] { rebuildGrid(); });
    head->addWidget(search_);
    main->addLayout(head);

    errorRow_ = new QWidget;
    auto* er = new QHBoxLayout(errorRow_);
    er->setContentsMargins(0, 4, 0, 4);
    error_ = label("", Tone::Danger);
    error_->setWordWrap(true);
    er->addWidget(error_, 1);
    auto* dismiss = new QPushButton(tr("Dismiss"));
    connect(dismiss, &QPushButton::clicked, errorRow_, &QWidget::hide);
    er->addWidget(dismiss);
    errorRow_->hide();
    main->addWidget(errorRow_);
    main->addSpacing(8);
    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->viewport()->setAutoFillBackground(false);
    scroll_->viewport()->installEventFilter(this); // its width decides the columns
    main->addWidget(scroll_, 1);
    row->addLayout(main, 1);
}

void HomeScreen::setError(const QString& message) {
    error_->setText(message);
    errorRow_->setVisible(!message.isEmpty());
}

void HomeScreen::refresh() {
    entries_ = findProjects(projectsFolder(), settings().recents);
    // Previews: only projects changed since the last time are loaded again.
    std::map<std::filesystem::path, Preview> previews;
    for (ProjectEntry& e : entries_) {
        if (auto old = previews_.find(e.dir); old != previews_.end() && old->second.stamp == e.modified) {
            previews[e.dir] = std::move(old->second);
        } else {
            Preview pv;
            pv.stamp = e.modified;
            Project p;
            if (loadProject(e.dir, p)) {
                const qreal dpr = devicePixelRatioF();
                pv.image = QImage(QSize(static_cast<int>(400 * dpr), static_cast<int>(225 * dpr)), QImage::Format_ARGB32_Premultiplied);
                QPainter painter(&pv.image);
                paintScenePreview(painter, p.scene(), QRectF(0, 0, pv.image.width(), pv.image.height()),
                                  QSizeF(p.window.width, p.window.height));
                pv.ok = true;
                pv.meta = QString("%1 × %2  ·  %3 objects").arg(p.window.width).arg(p.window.height).arg(p.scene().entities.size());
                pv.name = p.name;
            }
            previews[e.dir] = std::move(pv);
        }
        if (!previews[e.dir].name.empty()) e.name = previews[e.dir].name;
    }
    previews_ = std::move(previews);
    rebuildGrid();
}

void HomeScreen::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    const int cols = std::max(1, (scroll_->viewport()->width() + kGap) / (kCardMin + kGap));
    if (cols != columns_) rebuildGrid();
}

bool HomeScreen::eventFilter(QObject* watched, QEvent* e) {
    if (scroll_ && watched == scroll_->viewport() && e->type() == QEvent::Resize) {
        const int cols = std::max(1, (scroll_->viewport()->width() + kGap) / (kCardMin + kGap));
        if (cols != columns_) QMetaObject::invokeMethod(this, [this] { rebuildGrid(); }, Qt::QueuedConnection);
    }
    return QWidget::eventFilter(watched, e);
}

void HomeScreen::rebuildGrid() {
    const int avail = std::max(kCardMin, scroll_->viewport()->width() - 2);
    columns_ = std::max(1, (avail + kGap) / (kCardMin + kGap));
    const int cardW = (avail - kGap * (columns_ - 1)) / columns_;

    auto* grid = new QWidget;
    auto* col = new QVBoxLayout(grid);
    col->setContentsMargins(0, 0, 0, 24);
    if (projectsFolder().empty()) {
        auto* banner = new SettingsCard;
        auto* choose = accentButton(tr("Choose Folder..."));
        connect(choose, &QPushButton::clicked, this, [this] { chooseProjectsFolder(); });
        banner->addRow(tr("Choose a folder for your projects"), tr("New projects are made there, and every project in it shows here"), choose);
        col->addWidget(banner);
        col->addSpacing(14);
    }
    if (entries_.empty()) {
        col->addStretch(1);
        QLabel* t1 = label(tr("No projects yet"), Tone::Text, 1.5f, true);
        t1->setAlignment(Qt::AlignCenter);
        col->addWidget(t1);
        QLabel* t2 = label(tr("Make your first game in a few clicks."), Tone::TextDim);
        t2->setAlignment(Qt::AlignCenter);
        col->addWidget(t2);
        col->addSpacing(12);
        auto* b = accentButton(tr("Create a project"));
        b->setFixedSize(200, 40);
        connect(b, &QPushButton::clicked, this, &HomeScreen::openNewProjectDialog);
        col->addWidget(b, 0, Qt::AlignHCenter);
        col->addStretch(2);
        scroll_->setWidget(grid);
        return;
    }
    auto* layout = new QGridLayout;
    layout->setSpacing(kGap);
    layout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    col->addLayout(layout);
    const QString search = search_->text();
    int shown = 0;
    for (const ProjectEntry& item : entries_) {
        const QString name = QString::fromStdString(item.name);
        if (!search.isEmpty() && !name.contains(search, Qt::CaseInsensitive)) continue;
        const auto it = previews_.find(item.dir);
        const Preview* pv = it != previews_.end() ? &it->second : nullptr;
        const QString meta = pv && pv->ok ? relativeTime(item.modified) + "  ·  " + pv->meta : tr("Unreadable project");
        const ProjectEntry copy = item;
        auto* card = new ProjectCard(
            name, meta, pv ? &pv->image : nullptr,
            [this, copy] {
                if (onOpen) onOpen(copy.dir);
            },
            [this, copy](QPoint at) {
                QMenu menu;
                menu.addAction(menuIcon(Icon::Play, theme::kTextDim), tr("Open"), [this, copy] {
                    if (onOpen) onOpen(copy.dir);
                });
                menu.addAction(menuIcon(Icon::Folder, theme::kTextDim), tr("Show in Folder"), [copy] { platform::revealInFileManager(copy.dir); });
                menu.addSeparator();
                menu.addAction(menuIcon(Icon::Trash, theme::kDanger), tr("Delete Project..."), [this, copy] { confirmDelete(copy); });
                menu.exec(at);
            });
        card->setCardWidth(cardW);
        layout->addWidget(card, shown / columns_, shown % columns_);
        ++shown;
    }
    if (shown == 0) col->addWidget(label(tr("No projects match \"%1\".").arg(search), Tone::TextDim));
    col->addStretch(1);
    scroll_->setWidget(grid);
}

bool HomeScreen::chooseProjectsFolder() {
    const auto current = projectsFolder();
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose a Folder for Your Projects"),
                                                             platform::toQt(current.empty() ? platform::documentsDir() : current));
    if (folder.isEmpty()) return false;
    settings().projectsFolder = platform::utf8(platform::fromQt(folder));
    saveSettings();
    if (!watcher_->directories().isEmpty()) watcher_->removePaths(watcher_->directories());
    watcher_->addPath(folder);
    rebuild();
    refresh();
    return true;
}

void HomeScreen::confirmDelete(const ProjectEntry& item) {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Delete Project"));
    dialog.setMinimumWidth(460);
    auto* col = new QVBoxLayout(&dialog);
    col->setContentsMargins(24, 22, 24, 18);
    col->setSpacing(12);
    QLabel* title = label(tr("Delete \"%1\"?").arg(QString::fromStdString(item.name)), Tone::Text, 1.25f, true);
    title->setWordWrap(true);
    col->addWidget(title);
    QLabel* where = label(platform::toQt(item.dir), Tone::TextFaint, 0.92f);
    where->setWordWrap(true);
    col->addWidget(where);
#ifdef _WIN32
    const QString bin = tr("Recycle Bin");
#else
    const QString bin = tr("Trash");
#endif
    QLabel* what = label(tr("The whole project folder goes to the %1, where you can still restore it.").arg(bin), Tone::TextDim);
    what->setWordWrap(true);
    col->addWidget(what);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* keep = new QPushButton(tr("Cancel"));
    keep->setDefault(true);
    auto* del = new QPushButton(tr("Delete Project"));
    del->setProperty("danger", true);
    del->setAutoDefault(false);
    connect(keep, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(del, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttons->addWidget(keep);
    buttons->addWidget(del);
    col->addLayout(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    if (QFile::moveToTrash(platform::toQt(item.dir))) {
        removeRecent(item.dir);
        refresh();
    } else {
        setError(tr("Could not move \"%1\" to the %2.").arg(QString::fromStdString(item.name), bin));
    }
}

void HomeScreen::openNewProjectDialog() {
    if (projectsFolder().empty() && !chooseProjectsFolder()) return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("New Project"));
    dialog.setMinimumWidth(520);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(8);
    auto* name = new QLineEdit(tr("My Game"));
    name->selectAll();
    form->addRow(tr("Name"), name);
    auto* kind = new QComboBox;
    kind->addItem(tr("Starter  ·  a lit scene, a scripted player and a node graph"));
    kind->addItem(tr("Empty"));
    form->addRow(tr("Template"), kind);
    auto* res = new QComboBox;
    for (const Resolution& r : kResolutions) res->addItem(tr(r.label));
    form->addRow(tr("Resolution"), res);
    std::filesystem::path parent = projectsFolder();
    auto* folderRow = new QHBoxLayout;
    auto* folder = new QLineEdit(platform::toQt(parent));
    folder->setReadOnly(true);
    folderRow->addWidget(folder, 1);
    auto* change = new QPushButton(tr("Change..."));
    folderRow->addWidget(change);
    form->addRow(tr("Location"), folderRow);
    connect(change, &QPushButton::clicked, &dialog, [&] {
        const QString f = QFileDialog::getExistingDirectory(&dialog, tr("Where to make the project"), platform::toQt(parent));
        if (f.isEmpty()) return;
        parent = platform::fromQt(f);
        folder->setText(f);
    });

    auto* buttons = new QDialogButtonBox;
    QPushButton* createButton = buttons->addButton(tr("Create"), QDialogButtonBox::AcceptRole);
    createButton->setProperty("accent", true);
    createButton->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* col = new QVBoxLayout(&dialog);
    col->setContentsMargins(24, 22, 24, 18);
    col->addLayout(form);
    col->addSpacing(10);
    col->addWidget(buttons);
    name->setFocus();
    if (dialog.exec() != QDialog::Accepted) return;

    const std::string projectName = name->text().trimmed().isEmpty() ? "My Game" : name->text().trimmed().toStdString();
    const Resolution& r = kResolutions[std::clamp(res->currentIndex(), 0, static_cast<int>(std::size(kResolutions)) - 1)];
    std::string error;
    const auto dir = createProject(parent, projectName, kind->currentIndex() == 0 ? ProjectTemplate::Starter : ProjectTemplate::Empty,
                                   r.width, r.height, &error);
    if (dir.empty()) {
        setError(QString::fromStdString(error));
        return;
    }
    if (onOpen) onOpen(dir);
}

} // namespace artisan::editor
