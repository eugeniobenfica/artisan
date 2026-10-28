#pragma once

// Dialogs (and windows with the "chrome" property) framed by Artisan instead of the system: a
// header in the theme's colors with the title and a close button, a thin outline, resizable edges.

namespace artisan::editor::ui {

void installWindowChrome(); // once, after the QApplication exists

} // namespace artisan::editor::ui
