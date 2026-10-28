#pragma once

// Where things are on this machine, and the few things the editor asks the system to do.
// Paths are std::filesystem::path; text going to Qt goes through toQt / fromQt (UTF-8 safe on
// Windows, where path.string() is not).

#include <QString>

#include <filesystem>
#include <optional>
#include <string>

namespace artisan::editor::platform {

namespace fs = std::filesystem;

inline QString toQt(const fs::path& p) { return QString::fromStdU16String(p.generic_u16string()); }
inline fs::path fromQt(const QString& s) { return fs::path(s.toStdU16String()); }
inline fs::path fromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
inline std::string utf8(const fs::path& p) {
    const auto u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

fs::path executableDir();
// The editor's assets (fonts...): next to the program, else the source tree's (development).
fs::path assetsDir();
// Settings and recents: ARTISAN_APPDATA when set (tests), else the system's app data folder.
fs::path appDataDir();
fs::path documentsDir();

// The player that runs this machine's games (preview, desktop export).
fs::path playerExecutable();
// Where export templates for other platforms live (templates/<platform>/), and the Android
// project template.
fs::path templatesDir();
fs::path androidTemplate();

void revealInFileManager(const fs::path& path);
void openWithDefaultApp(const QString& urlOrPath);

// Writes a whole file safely (temporary, then rename; one retry, for OneDrive locks).
bool writeFile(const fs::path& path, const std::string& data, std::string* error = nullptr);
std::optional<std::string> readFile(const fs::path& path);

} // namespace artisan::editor::platform
