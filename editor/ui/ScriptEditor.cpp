#include "ui/ScriptEditor.h"

#include "app/Platform.h"
#include "ui/EditorContext.h"
#include "ui/Widgets.h"

#include <QFontDatabase>
#include <QHBoxLayout>
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

ScriptTab::ScriptTab(EditorContext& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto* bar = new QWidget;
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(10, 4, 8, 4);
    title_ = label("", Tone::TextDim);
    row->addWidget(title_, 1);
    auto* open = new IconButton(Icon::Folder, tr("Show the file"), 26);
    connect(open, &QAbstractButton::clicked, this, [this] {
        if (!file_.empty()) platform::revealInFileManager(file_);
    });
    row->addWidget(open);
    col->addWidget(bar);

    stack_ = new QStackedWidget;
    empty_ = label("", Tone::TextDim);
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    stack_->addWidget(empty_);
    text_ = new QPlainTextEdit;
    text_->setFont(codeFont());
    text_->setTabStopDistance(QFontMetricsF(codeFont()).horizontalAdvance(' ') * 4);
    text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    new LuaHighlighter(text_->document());
    stack_->addWidget(text_);
    col->addWidget(stack_, 1);

    connect(text_, &QPlainTextEdit::textChanged, this, [this] {
        if (loading_) return;
        modified_ = true;
        title_->setText(QString::fromStdString(platform::utf8(file_.filename())) + "  ●");
    });
    auto* save = new QShortcut(QKeySequence::Save, text_, nullptr, nullptr, Qt::WidgetShortcut);
    connect(save, &QShortcut::activated, this, [this] { saveIfChanged(); });
    connect(&ctx_, &EditorContext::selectionChanged, this, [this] {
        saveIfChanged();
        load();
    });
    connect(&ctx_, &EditorContext::sceneChanged, this, [this] {
        const Entity* e = ctx_.selected();
        const auto want = e && !e->script.empty() ? ctx_.dir / platform::fromUtf8(e->script) : std::filesystem::path();
        if (want != file_) {
            saveIfChanged();
            load();
        }
    });
    load();
}

void ScriptTab::load() {
    const Entity* e = ctx_.selected();
    file_ = e && !e->script.empty() ? ctx_.dir / platform::fromUtf8(e->script) : std::filesystem::path();
    modified_ = false;
    if (!e) {
        title_->setText(tr("Script"));
        empty_->setText(tr("Select an object to see its script."));
        stack_->setCurrentIndex(0);
        return;
    }
    if (file_.empty()) {
        title_->setText(tr("Script  ·  %1").arg(QString::fromStdString(e->name)));
        empty_->setText(tr("\"%1\" has no script: create one in its Behavior section.").arg(QString::fromStdString(e->name)));
        stack_->setCurrentIndex(0);
        return;
    }
    const auto code = platform::readFile(file_);
    loading_ = true;
    text_->setPlainText(code ? QString::fromStdString(*code) : QString());
    loading_ = false;
    title_->setText(QString::fromStdString(platform::utf8(file_.filename())) + (code ? "" : tr("  (not found: saving makes it)")));
    stack_->setCurrentIndex(1);
}

bool ScriptTab::saveIfChanged() {
    if (!modified_ || file_.empty()) return true;
    std::string error;
    if (!platform::writeFile(file_, text_->toPlainText().toStdString(), &error)) {
        ctx_.log(QString::fromStdString(error));
        return false;
    }
    modified_ = false;
    title_->setText(QString::fromStdString(platform::utf8(file_.filename())));
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
