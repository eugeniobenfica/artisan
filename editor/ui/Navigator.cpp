#include "ui/Navigator.h"

#include "ui/EditorContext.h"
#include "ui/Widgets.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QTabBar>

namespace artisan::editor {

using namespace ui;

Navigator::Navigator(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    setFixedHeight(36);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 3, 10, 0);
    row->setSpacing(2);

    auto* back = new IconButton(Icon::ChevronLeft, tr("Back (Alt+Left)"), 26);
    auto* forward = new IconButton(Icon::ChevronRight, tr("Forward (Alt+Right)"), 26);
    connect(back, &QAbstractButton::clicked, this, [this] { ctx_.back(); });
    connect(forward, &QAbstractButton::clicked, this, [this] { ctx_.forward(); });
    backButton_ = back;
    forwardButton_ = forward;
    row->addWidget(back, 0, Qt::AlignVCenter);
    row->addWidget(forward, 0, Qt::AlignVCenter);
    row->addSpacing(8);

    tabs_ = new QTabBar;
    tabs_->setDocumentMode(true);
    tabs_->setDrawBase(false);
    tabs_->setExpanding(false);
    tabs_->setMovable(true);
    tabs_->setUsesScrollButtons(true);
    tabs_->setElideMode(Qt::ElideRight);
    tabs_->setIconSize(QSize(14, 14));
    tabs_->setContextMenuPolicy(Qt::CustomContextMenu);
    row->addWidget(tabs_, 0, Qt::AlignBottom);
    auto* add = new IconButton(Icon::Plus, tr("New scene"), 24);
    connect(add, &QAbstractButton::clicked, this, &Navigator::addScene);
    row->addWidget(add, 0, Qt::AlignVCenter);
    row->addStretch(1);

    path_ = new QLabel;
    path_->setTextFormat(Qt::RichText);
    path_->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    row->addWidget(path_, 0, Qt::AlignVCenter);

    connect(tabs_, &QTabBar::currentChanged, this, [this](int i) {
        if (!syncing_) ctx_.openScene(i);
    });
    connect(tabs_, &QTabBar::tabMoved, this, [this](int from, int to) {
        if (syncing_) return;
        syncing_ = true;
        ctx_.moveScene(from, to);
        syncing_ = false;
    });
    connect(tabs_, &QTabBar::tabBarDoubleClicked, this, [this](int i) {
        if (i >= 0) renameScene(i);
        else addScene(); // the empty part of the bar
    });
    connect(tabs_, &QWidget::customContextMenuRequested, this, [this](QPoint at) {
        const int i = tabs_->tabAt(at);
        if (i >= 0) sceneMenu(i, tabs_->mapToGlobal(at));
    });
    connect(path_, &QLabel::linkActivated, this, [this](const QString& link) {
        if (link == "scene") ctx_.select(0);
    });

    connect(&ctx_, &EditorContext::scenesChanged, this, [this] {
        if (!syncing_) rebuildTabs();
        updatePath();
    });
    connect(&ctx_, &EditorContext::selectionChanged, this, [this] { updatePath(); });
    connect(&ctx_, &EditorContext::sceneChanged, this, [this] { updatePath(); });
    connect(&Appearance::get(), &Appearance::changed, this, [this] {
        rebuildTabs();
        updatePath();
    });
}

void Navigator::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), theme::q(theme::kHeader));
    p.fillRect(QRect(0, height() - 1, width(), 1), theme::q(theme::kBorder));
}

void Navigator::rebuildTabs() {
    syncing_ = true;
    while (tabs_->count()) tabs_->removeTab(0);
    for (size_t i = 0; i < ctx_.project.scenes.size(); ++i) {
        const bool start = static_cast<int>(i) == ctx_.project.mainScene;
        const int t = tabs_->addTab(menuIcon(start ? Icon::Play : Icon::Layers, start ? theme::kAccent : theme::kTextDim),
                                    QString::fromStdString(ctx_.project.scenes[i].name));
        tabs_->setTabToolTip(t, start ? tr("The game starts with this scene. Right-click for more") : tr("Right-click to rename, duplicate or delete it"));
    }
    tabs_->setCurrentIndex(ctx_.sceneIndex());
    syncing_ = false;
}

void Navigator::updatePath() {
    backButton_->setEnabled(ctx_.canGoBack());
    forwardButton_->setEnabled(ctx_.canGoForward());
    if (ctx_.dir.empty()) return path_->clear();
    const QString faint = theme::q(theme::kTextFaint).name(), dim = theme::q(theme::kTextDim).name(), text = theme::q(theme::kText).name();
    const QString sep = QString("<span style='color:%1'>&nbsp;›&nbsp;</span>").arg(faint);
    QString html = QString("<span style='color:%1'>%2</span>").arg(dim, QString::fromStdString(ctx_.project.name).toHtmlEscaped()) + sep;
    const QString scene = QString::fromStdString(ctx_.scene().name).toHtmlEscaped();
    if (const Entity* e = ctx_.selected()) {
        html += QString("<a href='scene' style='color:%1; text-decoration:none'>%2</a>").arg(dim, scene) + sep;
        html += QString("<span style='color:%1'><b>%2</b></span>").arg(text, QString::fromStdString(e->name).toHtmlEscaped());
    } else {
        html += QString("<span style='color:%1'><b>%2</b></span>").arg(text, scene);
    }
    path_->setText(html);
}

void Navigator::addScene() {
    Scene s;
    s.name = "Scene";
    // Like the scene being edited: the same look, an empty stage.
    s.background = ctx_.scene().background;
    s.ambient = ctx_.scene().ambient;
    ctx_.addScene(std::move(s));
}

void Navigator::renameScene(int index) {
    if (index < 0 || index >= static_cast<int>(ctx_.project.scenes.size())) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Scene"), tr("Name"), QLineEdit::Normal,
                                               QString::fromStdString(ctx_.project.scenes[static_cast<size_t>(index)].name), &ok)
                             .trimmed();
    if (!ok || name.isEmpty()) return;
    ctx_.openScene(index);
    ctx_.checkpoint();
    ctx_.scene().name = name.toStdString();
    ctx_.changed();
    emit ctx_.scenesChanged();
}

void Navigator::sceneMenu(int index, QPoint global) {
    QMenu menu;
    menu.addAction(menuIcon(Icon::Edit, theme::kTextDim), tr("Rename..."), this, [this, index] { renameScene(index); });
    menu.addAction(menuIcon(Icon::Duplicate, theme::kTextDim), tr("Duplicate"), this, [this, index] {
        Scene copy = ctx_.project.scenes[static_cast<size_t>(index)];
        ctx_.addScene(std::move(copy));
    });
    QAction* start = menu.addAction(menuIcon(Icon::Play, theme::kTextDim), tr("Start the Game with It"), this, [this, index] { ctx_.setStartScene(index); });
    start->setEnabled(index != ctx_.project.mainScene);
    menu.addSeparator();
    QAction* remove = menu.addAction(menuIcon(Icon::Trash, theme::kDanger), tr("Delete"), this, [this, index] {
        const QString name = QString::fromStdString(ctx_.project.scenes[static_cast<size_t>(index)].name);
        if (QMessageBox::question(this, tr("Delete Scene"), tr("Delete the scene \"%1\" and its objects? This cannot be undone.").arg(name)) !=
            QMessageBox::Yes)
            return;
        ctx_.removeScene(index);
    });
    remove->setEnabled(ctx_.project.scenes.size() > 1);
    menu.exec(global);
}

} // namespace artisan::editor
