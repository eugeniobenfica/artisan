#pragma once

#include <QString>

class QWidget;

namespace artisan::editor {

// Applies the theme the settings name (a built-in one, one from the themes folder, or the user's
// own palette) with the chosen accent, everywhere.
void applyTheme();

// Settings, as bemotion's: Theme, Interface, Editor, Shortcuts in a paged window. Changes apply at
// once and are saved. Returns the window (shown).
QWidget* openSettings(QWidget* parent);

// Help, in the same window style: getting started, shortcuts, scripting, about.
enum class HelpPage { Start, Shortcuts, Scripting, About };
QWidget* openHelp(QWidget* parent, HelpPage page = HelpPage::Start, const QString& gpuName = {});

// Export: the platform, where to, then the export itself.
class EditorContext;
void openExportDialog(EditorContext& ctx, QWidget* parent);

} // namespace artisan::editor
