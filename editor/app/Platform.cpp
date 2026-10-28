#include "app/Platform.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

#include <fstream>
#include <sstream>

namespace artisan::editor::platform {

fs::path executableDir() { return fromQt(QCoreApplication::applicationDirPath()); }

fs::path assetsDir() {
    std::error_code ec;
    if (fs::path p = executableDir() / "assets"; fs::exists(p / "fonts", ec)) return p;
#ifdef __APPLE__
    if (fs::path p = executableDir() / ".." / "Resources" / "assets"; fs::exists(p / "fonts", ec)) return p;
#endif
    return fs::path(ARTISAN_SOURCE_DIR) / "assets";
}

fs::path appDataDir() {
    const QString env = qEnvironmentVariable("ARTISAN_APPDATA");
    fs::path dir = env.isEmpty() ? fromQt(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)) : fromQt(env);
    std::error_code ec;
    fs::create_directories(dir, ec); // the app's own folder, not the user's
    return dir;
}

fs::path documentsDir() { return fromQt(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)); }

fs::path playerExecutable() {
#ifdef _WIN32
    return executableDir() / "artisan-player.exe";
#else
    std::error_code ec;
    if (fs::path p = executableDir() / "artisan-player"; fs::exists(p, ec)) return p;
    return executableDir() / ".." / ".." / ".." / "artisan-player"; // macOS: beside Artisan.app
#endif
}

fs::path templatesDir() { return executableDir() / "templates"; }

fs::path androidTemplate() {
    std::error_code ec;
    if (fs::path p = templatesDir() / "android"; fs::exists(p / "settings.gradle", ec)) return p;
    return fs::path(ARTISAN_SOURCE_DIR) / "platforms" / "android";
}

void revealInFileManager(const fs::path& path) {
#ifdef _WIN32
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(toQt(path))});
#elif defined(__APPLE__)
    QProcess::startDetached("open", {"-R", toQt(path)});
#else
    std::error_code ec;
    QDesktopServices::openUrl(QUrl::fromLocalFile(toQt(fs::is_directory(path, ec) ? path : path.parent_path())));
#endif
}

void openWithDefaultApp(const QString& urlOrPath) {
    const QUrl url = urlOrPath.contains("://") ? QUrl(urlOrPath) : QUrl::fromLocalFile(urlOrPath);
    QDesktopServices::openUrl(url);
}

bool writeFile(const fs::path& path, const std::string& data, std::string* error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += ".tmp";
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool written;
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
            written = static_cast<bool>(out);
        }
        if (written) {
            fs::rename(tmp, path, ec);
            if (!ec) return true;
        }
        QThread::msleep(150);
    }
    fs::remove(tmp, ec);
    if (error) *error = "Cannot write " + utf8(path);
    return false;
}

std::optional<std::string> readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

} // namespace artisan::editor::platform
