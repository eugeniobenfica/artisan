#pragma once

// The Script tab: the selected object's Lua file, with syntax colors, saved with Ctrl+S (and
// before the game runs). The Console tab: what the running game prints, and its errors.

#include <QWidget>

#include <filesystem>

class QLabel;
class QPlainTextEdit;
class QStackedWidget;

namespace artisan::editor {

class EditorContext;

class ScriptTab : public QWidget {
    Q_OBJECT
public:
    explicit ScriptTab(EditorContext& ctx, QWidget* parent = nullptr);
    bool saveIfChanged(); // false if it could not be written

private:
    void load();
    EditorContext& ctx_;
    QStackedWidget* stack_ = nullptr;
    QPlainTextEdit* text_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* empty_ = nullptr;
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
