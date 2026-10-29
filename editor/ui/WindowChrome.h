#pragma once

// Dialogs (and windows with the "chrome" property) framed by Artisan instead of the system: a
// header in the theme's colors with the title and a close button, a thin outline, resizable edges.

class QWidget;

namespace artisan::editor::ui {

void installWindowChrome(); // once, after the QApplication exists

// The main window keeps the system's title bar (snapping, its buttons): on Windows 11 it takes the
// menu bar's colors. Again each time the theme changes; earlier Windows keep theirs.
void tintTitleBar(QWidget* window);

} // namespace artisan::editor::ui
