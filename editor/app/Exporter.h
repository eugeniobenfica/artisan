#pragma once

// Exporting a game: the player for the platform next to the game's data (project.artisan,
// scripts/, assets/), the way market engines ship ("export templates"):
//
//   Windows   <out>/<Game>/<Game>.exe + game/
//   Linux     <out>/<Game>/<Game> + game/
//   macOS     <out>/<Game>.app (Contents/MacOS/<Game>, Contents/Resources/game)
//   Android   <out>/<Game>-android: a Gradle project (SDL's activity, the player as libmain.so,
//             the game in its assets) — `gradlew assembleRelease` makes the APK
//
// The player for this machine is the one next to the editor; the others come from
// templates/<platform>/ (built by CI on each platform).

#include <filesystem>
#include <string>

namespace artisan::editor {

namespace fs = std::filesystem;

enum class ExportPlatform { Windows, Linux, MacOS, Android };

struct ExportTarget {
    ExportPlatform platform;
    const char* name;      // "Windows"
    const char* folder;    // under templates/: "windows"
};

const ExportTarget& exportTarget(ExportPlatform p);
ExportPlatform hostPlatform();

// The player binary (or Android template) export would use; empty when none is installed.
fs::path exportTemplate(ExportPlatform p);

struct ExportResult {
    bool ok = false;
    fs::path output; // what was made (folder, .app or Gradle project)
    std::string message;
};

ExportResult exportGame(const fs::path& projectDir, const std::string& gameName, ExportPlatform p, const fs::path& outDir);

} // namespace artisan::editor
