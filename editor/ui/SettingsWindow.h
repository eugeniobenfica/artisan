#pragma once

#include <QString>

class QWidget;

namespace artisan::editor {

// Applies the theme the settings name (Dark, Light, Midnight or the imported one), everywhere.
void applyTheme();

// Settings: Appearance (themes), Editor, About. Changes apply at once and are saved.
void openSettings(QWidget* parent, const QString& gpuName = {});

// Export: the platform, where to, then the export itself.
class EditorContext;
void openExportDialog(EditorContext& ctx, QWidget* parent);

} // namespace artisan::editor
