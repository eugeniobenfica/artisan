#pragma once

// The Script tab: the Lua file of the selected object (or of the scene), or one opened from the
// Files panel, with syntax colors, saved with Ctrl+S (and before the game runs). The Console tab: what the running game prints, and its errors.

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
    void openFile(const std::filesystem::path& file); // until another object is selected

private:
    void load();
    void showFile(const std::filesystem::path& file);
    EditorContext& ctx_;
    QStackedWidget* stack_ = nullptr;
    QPlainTextEdit* text_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* empty_ = nullptr;
    std::filesystem::path file_;
    bool modified_ = false, loading_ = false;
    bool pinned_ = false; // a file opened by itself, not the selection's
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
