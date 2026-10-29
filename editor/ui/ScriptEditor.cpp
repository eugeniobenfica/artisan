#include "ui/ScriptEditor.h"

#include "app/Platform.h"
#include "app/ProjectIO.h"
#include "app/Settings.h"
#include "ui/EditorContext.h"
#include "ui/Widgets.h"

#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QListWidget>
#include <QTimer>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QStackedWidget>
#include <QSyntaxHighlighter>
#include <QVBoxLayout>

namespace artisan::editor {

using namespace ui;

namespace {

// Lua's keywords, strings, numbers and comments, and the names of Artisan's API.
class LuaHighlighter : public QSyntaxHighlighter {
public:
    explicit LuaHighlighter(QTextDocument* doc) : QSyntaxHighlighter(doc) {}

protected:
    void highlightBlock(const QString& text) override {
        static const QRegularExpression keywords(
            R"(\b(and|break|do|else|elseif|end|false|for|function|goto|if|in|local|nil|not|or|repeat|return|then|true|until|while)\b)");
        static const QRegularExpression api(R"(\b(self|input|camera|time|scene|log|print|vars|math|string|table)\b)");
        static const QRegularExpression number(R"(\b(0x[0-9a-fA-F]+|\d+(\.\d+)?([eE][+-]?\d+)?)\b)");
        static const QRegularExpression strings(R"("([^"\\]|\\.)*"|'([^'\\]|\\.)*')");
        static const QRegularExpression comment(R"(--.*$)");
        auto paint = [&](const QRegularExpression& re, Rgba color, bool bold = false) {
            QTextCharFormat f;
            f.setForeground(theme::q(color));
            if (bold) f.setFontWeight(QFont::DemiBold);
            for (auto it = re.globalMatch(text); it.hasNext();) {
                const auto m = it.next();
                setFormat(static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedLength()), f);
            }
        };
        paint(keywords, theme::kAccent, true);
        paint(api, theme::kGuide);
        paint(number, theme::kSuccess);
        paint(strings, theme::kSnap);
        paint(comment, theme::kTextFaint);
    }
};

QFont codeFont() {
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSizeF(f.pointSizeF() * 1.1);
    return f;
}

} // namespace

void highlightLua(QTextDocument* doc) { new LuaHighlighter(doc); }

ScriptTab::ScriptTab(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    // The project's scripts.
    auto* side = new QWidget;
    side->setFixedWidth(210);
    auto* sideCol = new QVBoxLayout(side);
    sideCol->setContentsMargins(8, 8, 8, 8);
    sideCol->setSpacing(6);
    auto* head = new QHBoxLayout;
    head->addWidget(sectionLabel(tr("Scripts")), 1);
    auto* add = new IconButton(Icon::Plus, tr("New script"), 24);
    connect(add, &QAbstractButton::clicked, this, &ScriptTab::newScript);
    head->addWidget(add);
    sideCol->addLayout(head);
    list_ = new QListWidget;
    list_->setFrameShape(QFrame::NoFrame);
    list_->setIconSize(QSize(14, 14));
    sideCol->addWidget(list_, 1);
    row->addWidget(side);
    auto* line = new QFrame;
    line->setFrameShape(QFrame::VLine);
    setTone(line, Tone::Border);
    row->addWidget(line);

    auto* main = new QWidget;
    auto* col = new QVBoxLayout(main);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto* bar = new QWidget;
    auto* barRow = new QHBoxLayout(bar);
    barRow->setContentsMargins(10, 4, 8, 4);
    title_ = label("", Tone::Text, 1.0f, true);
    barRow->addWidget(title_);
    users_ = label("", Tone::TextFaint);
    barRow->addWidget(users_, 1);
    auto* open = new IconButton(Icon::Folder, tr("Show the file"), 26);
    connect(open, &QAbstractButton::clicked, this, [this] {
        if (!file_.empty()) platform::revealInFileManager(file_);
    });
    barRow->addWidget(open);
    col->addWidget(bar);

    stack_ = new QStackedWidget;
    empty_ = label(tr("The project's Lua scripts are listed on the left: pick one, or make one with +.\nAn object uses a script from its "
                      "Behavior section in the Inspector."),
                   Tone::TextDim);
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    stack_->addWidget(empty_);
    text_ = new QPlainTextEdit;
    text_->setFont(codeFont());
    text_->setTabStopDistance(QFontMetricsF(codeFont()).horizontalAdvance(' ') * 4);
    text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    highlightLua(text_->document());
    stack_->addWidget(text_);
    col->addWidget(stack_, 1);
    row->addWidget(main, 1);

    // Saved a moment after typing stops, like the project.
    autosave_ = new QTimer(this);
    autosave_->setSingleShot(true);
    autosave_->setInterval(1500);
    connect(autosave_, &QTimer::timeout, this, [this] {
        if (settings().autosave) saveIfChanged();
    });
    connect(text_, &QPlainTextEdit::textChanged, this, [this] {
        if (loading_) return;
        modified_ = true;
        updateTitle();
        autosave_->start();
    });
    auto* save = new QShortcut(QKeySequence::Save, text_, nullptr, nullptr, Qt::WidgetShortcut);
    connect(save, &QShortcut::activated, this, [this] { saveIfChanged(); });
    connect(list_, &QListWidget::itemSelectionChanged, this, [this] {
        const auto items = list_->selectedItems();
        if (items.isEmpty()) return;
        const auto file = platform::fromQt(items.first()->data(Qt::UserRole).toString());
        if (file != file_) openFile(file);
    });
    // The Inspector's "Edit": the selected object's script.
    connect(&ctx_, &EditorContext::editScriptRequested, this, [this] {
        if (!ctx_.script().empty()) openFile(ctx_.dir / platform::fromUtf8(ctx_.script()));
    });
    connect(&ctx_, &EditorContext::opened, this, [this] {
        file_.clear();
        modified_ = false;
        stack_->setCurrentIndex(0);
        refreshList();
        updateTitle();
        // The first script is open, ready to read.
        if (list_->count()) openFile(platform::fromQt(list_->item(0)->data(Qt::UserRole).toString()));
    });
    connect(&ctx_, &EditorContext::sceneChanged, this, [this] { updateTitle(); }); // who uses it
    connect(&ctx_, &EditorContext::saved, this, [this] { refreshList(); });
    refreshList();
    updateTitle();
}

void ScriptTab::refreshList() {
    const QSignalBlocker block(list_);
    list_->clear();
    if (ctx_.dir.empty()) return;
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(ctx_.dir / "scripts", ec); !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec))
        if (it->path().extension() == ".lua") files.push_back(it->path());
    if (!file_.empty() && std::find(files.begin(), files.end(), file_) == files.end()) files.push_back(file_);
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        const std::string rel = platform::utf8(std::filesystem::relative(f, ctx_.dir / "scripts", ec));
        auto* item = new QListWidgetItem(menuIcon(Icon::Code, theme::kTextDim), QString::fromStdString(ec || rel.starts_with("..") ? platform::utf8(f.filename()) : rel));
        item->setData(Qt::UserRole, platform::toQt(f));
        item->setToolTip(platform::toQt(f));
        list_->addItem(item);
        if (f == file_) item->setSelected(true);
    }
}

void ScriptTab::updateTitle() {
    if (file_.empty()) {
        title_->setText(tr("Scripts"));
        users_->clear();
        return;
    }
    title_->setText(QString::fromStdString(platform::utf8(file_.filename())) + (modified_ ? "  ●" : ""));
    // Which objects (or scenes) run it.
    std::error_code ec;
    const std::string rel = platform::utf8(std::filesystem::relative(file_, ctx_.dir, ec));
    QStringList who;
    for (const Scene& s : ctx_.project.scenes) {
        if (s.script == rel) who << tr("the scene %1").arg(QString::fromStdString(s.name));
        for (const Entity& e : s.entities)
            if (e.script == rel) who << QString::fromStdString(e.name);
    }
    users_->setText(who.isEmpty() ? tr("   used by no object yet") : tr("   used by %1").arg(who.join(", ")));
}

void ScriptTab::openFile(const std::filesystem::path& file) {
    saveIfChanged();
    showFile(file);
    refreshList();
}

void ScriptTab::showFile(const std::filesystem::path& file) {
    file_ = file;
    modified_ = false;
    const auto code = platform::readFile(file_);
    loading_ = true;
    text_->setPlainText(code ? QString::fromStdString(*code) : QString());
    loading_ = false;
    stack_->setCurrentIndex(1);
    updateTitle();
    if (!code) title_->setText(title_->text() + tr("  (not found: saving makes it)"));
}

void ScriptTab::newScript() {
    if (ctx_.dir.empty()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Script"), tr("Name"), QLineEdit::Normal, tr("script"), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    std::error_code ec;
    std::filesystem::create_directories(ctx_.dir / "scripts", ec);
    const std::string base = safeFileName(name.toStdString());
    std::filesystem::path file = ctx_.dir / "scripts" / platform::fromUtf8(base + ".lua");
    for (int i = 2; std::filesystem::exists(file, ec); ++i) file = ctx_.dir / "scripts" / platform::fromUtf8(base + " " + std::to_string(i) + ".lua");
    platform::writeFile(file, newScriptTemplate(name.toStdString()));
    openFile(file);
}

bool ScriptTab::saveIfChanged() {
    autosave_->stop();
    if (!modified_ || file_.empty()) return true;
    std::string error;
    if (!platform::writeFile(file_, text_->toPlainText().toStdString(), &error)) {
        ctx_.log(QString::fromStdString(error));
        return false;
    }
    modified_ = false;
    updateTitle();
    return true;
}

ConsoleTab::ConsoleTab(EditorContext& ctx, QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto* bar = new QWidget;
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(10, 4, 8, 4);
    row->addWidget(label(tr("Output of the game and the editor"), Tone::TextDim), 1);
    auto* clearButton = new IconButton(Icon::Trash, tr("Clear"), 26);
    connect(clearButton, &QAbstractButton::clicked, this, &ConsoleTab::clear);
    row->addWidget(clearButton);
    col->addWidget(bar);
    text_ = new QPlainTextEdit;
    text_->setReadOnly(true);
    text_->setFont(codeFont());
    text_->setMaximumBlockCount(5000);
    col->addWidget(text_, 1);
    connect(&ctx, &EditorContext::logLine, this, [this](const QString& line) {
        const bool bad = line.startsWith("error") || line.contains(": error");
        const QString html = line.toHtmlEscaped();
        text_->appendHtml(bad ? QString("<span style='color:%1'>%2</span>").arg(theme::q(theme::kDanger).name(), html) : html);
    });
}

void ConsoleTab::clear() { text_->clear(); }

} // namespace artisan::editor
