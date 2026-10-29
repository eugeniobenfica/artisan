#pragma once

#include <QObject>
#include <QString>

class QWidget;

namespace artisan::editor {

// Applies the theme the settings name (a built-in one, one from the themes folder, or the user's
// own palette) with the chosen accent and the interface font, everywhere.
void applyTheme();

// Settings, as bemotion's: Theme, Interface, Editor, Shortcuts, Plugins in a paged window. Changes
// apply at once and are saved. Returns the window (shown).
enum class SettingsPage { Theme, Interface, Editor, Shortcuts, Plugins };
QWidget* openSettings(QWidget* parent, SettingsPage page = SettingsPage::Theme);

// Help, in the same window style: getting started, shortcuts, scripting, about, credits, license.
enum class HelpPage { Start, Shortcuts, Scripting, About, Credits, License };
QWidget* openHelp(QWidget* parent, HelpPage page = HelpPage::Start, const QString& gpuName = {});

// What Settings asks of the workspace.
class SettingsSignals : public QObject {
    Q_OBJECT
public:
    static SettingsSignals& get();

signals:
    void resetLayoutRequested(); // the panels where they first were
};

// Export: the platform, where to, then the export itself.
class EditorContext;
void openExportDialog(EditorContext& ctx, QWidget* parent);

inline constexpr const char* kSupportUrl = "https://buymeacoffee.com/eugeniobenfica";

} // namespace artisan::editor
