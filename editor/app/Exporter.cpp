#include "app/Exporter.h"

#include "app/Platform.h"
#include "app/ProjectIO.h"

#include <cctype>

namespace artisan::editor {

namespace {

constexpr ExportTarget kTargets[] = {
    {ExportPlatform::Windows, "Windows", "windows"},
    {ExportPlatform::Linux, "Linux", "linux"},
    {ExportPlatform::MacOS, "macOS", "macos"},
    {ExportPlatform::Android, "Android", "android"},
};

const char* playerName(ExportPlatform p) { return p == ExportPlatform::Windows ? "artisan-player.exe" : "artisan-player"; }

// The game's own files: never the editor's leftovers (temporary saves).
bool copyGameData(const fs::path& projectDir, const fs::path& to, std::string& error) {
    std::error_code ec;
    fs::create_directories(to, ec);
    fs::copy_file(projectDir / kProjectFile, to / kProjectFile, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "Cannot copy the project file: " + ec.message();
        return false;
    }
    for (const char* sub : {"scripts", "assets"}) {
        if (!fs::is_directory(projectDir / sub, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(projectDir / sub, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file() || it->path().extension() == ".tmp") continue;
            const fs::path rel = fs::relative(it->path(), projectDir, ec);
            fs::create_directories((to / rel).parent_path(), ec);
            fs::copy_file(it->path(), to / rel, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                error = "Cannot copy " + platform::utf8(rel) + ": " + ec.message();
                return false;
            }
        }
    }
    return true;
}

std::string androidId(const std::string& name) {
    std::string id;
    for (char ch : name)
        if (std::isalnum(static_cast<unsigned char>(ch))) id += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (id.empty() || std::isdigit(static_cast<unsigned char>(id[0]))) id = "game" + id;
    return "org.artisan." + id;
}

} // namespace

const ExportTarget& exportTarget(ExportPlatform p) { return kTargets[static_cast<int>(p)]; }

ExportPlatform hostPlatform() {
#ifdef _WIN32
    return ExportPlatform::Windows;
#elif defined(__APPLE__)
    return ExportPlatform::MacOS;
#else
    return ExportPlatform::Linux;
#endif
}

fs::path exportTemplate(ExportPlatform p) {
    std::error_code ec;
    if (p == ExportPlatform::Android) {
        const fs::path t = platform::androidTemplate();
        return fs::exists(t / "settings.gradle", ec) ? t : fs::path();
    }
    if (p == hostPlatform() && fs::exists(platform::playerExecutable(), ec)) return platform::playerExecutable();
    const fs::path t = platform::templatesDir() / exportTarget(p).folder / playerName(p);
    return fs::exists(t, ec) ? t : fs::path();
}

ExportResult exportGame(const fs::path& projectDir, const std::string& gameName, ExportPlatform p, const fs::path& outDir) {
    ExportResult r;
    const fs::path tmpl = exportTemplate(p);
    if (tmpl.empty()) {
        r.message = std::string("No ") + exportTarget(p).name + " export template: build the player on " + exportTarget(p).name +
                    " (or take it from a release) and put it in " + platform::utf8(platform::templatesDir() / exportTarget(p).folder);
        return r;
    }
    const std::string name = safeFileName(gameName);
    std::error_code ec;
    std::string error;
    switch (p) {
    case ExportPlatform::Windows:
    case ExportPlatform::Linux: {
        r.output = outDir / platform::fromUtf8(name);
        fs::create_directories(r.output, ec);
        const fs::path exe = r.output / platform::fromUtf8(p == ExportPlatform::Windows ? name + ".exe" : name);
        fs::copy_file(tmpl, exe, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            r.message = "Cannot copy the player: " + ec.message();
            return r;
        }
        fs::permissions(exe, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
        if (!copyGameData(projectDir, r.output / "game", error)) {
            r.message = error;
            return r;
        }
        break;
    }
    case ExportPlatform::MacOS: {
        r.output = outDir / platform::fromUtf8(name + ".app");
        const fs::path contents = r.output / "Contents";
        fs::create_directories(contents / "MacOS", ec);
        fs::create_directories(contents / "Resources", ec);
        fs::copy_file(tmpl, contents / "MacOS" / platform::fromUtf8(name), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            r.message = "Cannot copy the player: " + ec.message();
            return r;
        }
        fs::permissions(contents / "MacOS" / platform::fromUtf8(name), fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add, ec);
        const std::string plist = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>)" + name + R"(</string>
  <key>CFBundleName</key><string>)" + name + R"(</string>
  <key>CFBundleIdentifier</key><string>)" + androidId(name) + R"(</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
)";
        if (!platform::writeFile(contents / "Info.plist", plist, &error) || !copyGameData(projectDir, contents / "Resources" / "game", error)) {
            r.message = error;
            return r;
        }
        break;
    }
    case ExportPlatform::Android: {
        r.output = outDir / platform::fromUtf8(name + "-android");
        fs::create_directories(r.output, ec);
        fs::copy(tmpl, r.output, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) {
            r.message = "Cannot copy the Android project: " + ec.message();
            return r;
        }
        if (!copyGameData(projectDir, r.output / "app" / "src" / "main" / "assets" / "game", error)) {
            r.message = error;
            return r;
        }
        // What the Gradle project builds: the engine's sources, under this name and id.
        const std::string props = "artisan.appName=" + name + "\nartisan.appId=" + androidId(name) + "\nartisan.engineDir=" +
                                  platform::utf8(fs::path(ARTISAN_SOURCE_DIR)) + "\n";
        if (!platform::writeFile(r.output / "artisan.properties", props, &error)) {
            r.message = error;
            return r;
        }
        r.ok = true;
        r.message = "Android project ready: open it in Android Studio, or run \"gradlew assembleRelease\" in it (Android SDK and NDK needed)";
        return r;
    }
    }
    r.ok = true;
    r.message = std::string("Exported for ") + exportTarget(p).name;
    return r;
}

} // namespace artisan::editor
