#pragma once

// The Scripts tab: the project's, not an object's. Every Lua file in scripts/ is listed on its
// left (New Script adds one); the one picked is edited with syntax colors and saved with Ctrl+S
// (and before the game runs, or a moment after typing stops). Over it, which objects use it.
// "Edit" on an object's script in the Inspector opens that file here. The Console tab: what the
// running game prints, and its errors.

#include <QWidget>

#include <filesystem>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QStackedWidget;
class QTextDocument;
class QTimer;

namespace artisan::editor {

class EditorContext;

// Lua's keywords, strings, numbers and comments, and Artisan's API names, in the theme's colors.
void highlightLua(QTextDocument* doc);

class ScriptTab : public QWidget {
    Q_OBJECT
public:
    explicit ScriptTab(EditorContext& ctx, QWidget* parent = nullptr);
    bool saveIfChanged(); // false if it could not be written
    void openFile(const std::filesystem::path& file);

private:
    void refreshList();
    void showFile(const std::filesystem::path& file);
    void newScript();
    void updateTitle();
    EditorContext& ctx_;
    QListWidget* list_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QPlainTextEdit* text_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* users_ = nullptr;
    QLabel* empty_ = nullptr;
    QTimer* autosave_ = nullptr;
    std::filesystem::path file_;
    bool modified_ = false, loading_ = false;
};

class ConsoleTab : public QWidget {
    Q_OBJECT
public:
    explicit ConsoleTab(EditorContext& ctx, QWidget* parent = nullptr);
    void clear();

private:
    QPlainTextEdit* text_ = nullptr;
};

} // namespace artisan::editor
