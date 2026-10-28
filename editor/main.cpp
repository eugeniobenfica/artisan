// Artisan — a minimal, fast, cross-platform 2D game engine. This is the editor.
//
//   artisan [project folder]   opens that project; else the start screen
//   artisan --export <windows|linux|macos|android> <project folder> <output folder>
//                              exports without a window (CI, scripts)

#include "app/Exporter.h"
#include "app/Platform.h"
#include "app/ProjectIO.h"
#include "app/Settings.h"
#include "artisan/core/Project.h"
#include "ui/MainWindow.h"
#include "ui/SettingsWindow.h"
#include "ui/Widgets.h"
#include "ui/WindowChrome.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QPixmap>
#include <QTimer>
#include <QtGui/qtguiglobal.h>
#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif

#include <cstdio>
#include <cstring>

using namespace artisan::editor;

namespace {

QIcon appIcon() {
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) {
        QPixmap px(size, size);
        px.fill(Qt::transparent);
        QPainter p(&px);
        ui::drawLogoMark(p, {size * 0.5f, size * 0.5f}, size * 0.48f, bestyle::rgba(232, 185, 49));
        icon.addPixmap(px);
    }
    return icon;
}

} // namespace

int exportCommand(int argc, char** argv) {
    QCoreApplication core(argc, argv); // for the program's folder (the player, the templates)
    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 5) {
        std::fprintf(stderr, "usage: artisan --export <windows|linux|macos|android> <project folder> <output folder>\n");
        return 2;
    }
    const QString name = args[2].toLower();
    ExportPlatform p = hostPlatform();
    if (name == "windows") p = ExportPlatform::Windows;
    else if (name == "linux") p = ExportPlatform::Linux;
    else if (name == "macos") p = ExportPlatform::MacOS;
    else if (name == "android") p = ExportPlatform::Android;
    else {
        std::fprintf(stderr, "unknown platform: %s\n", qPrintable(args[2]));
        return 2;
    }
    const auto dir = platform::fromQt(args[3]);
    artisan::Project project;
    std::string error;
    if (!loadProject(dir, project, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    const ExportResult r = exportGame(dir, project.name, p, platform::fromQt(args[4]));
    std::printf("%s\n%s\n", r.message.c_str(), platform::utf8(r.output).c_str());
    return r.ok ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc > 1 && !std::strcmp(argv[1], "--export")) return exportCommand(argc, argv);
    QApplication app(argc, argv);
    QApplication::setApplicationName("Artisan");
    QApplication::setOrganizationName("Artisan");
    QApplication::setApplicationVersion(ARTISAN_VERSION);
    QApplication::setWindowIcon(appIcon());

    // The interface font: Inter ships with the editor.
    for (const char* style : {"Regular", "Medium", "SemiBold", "Bold"})
        QFontDatabase::addApplicationFont(platform::toQt(platform::assetsDir() / "fonts" / (std::string("Inter-") + style + ".ttf")));
    loadSettings();
    bestyle::install(bestyle::theme::darkPalette());
    bestyle::setLogoPainter(ui::drawLogoMark);
    ui::installWindowChrome();
    applyTheme();

#if QT_CONFIG(vulkan)
    // One Vulkan instance for the editor's views (the player makes its own).
    QVulkanInstance vulkan;
    vulkan.setApiVersion(QVersionNumber(1, 1));
    if (qEnvironmentVariableIsSet("ARTISAN_VALIDATION")) vulkan.setLayers({"VK_LAYER_KHRONOS_validation"});
    MainWindow window(vulkan.create() ? &vulkan : nullptr);
#else
    MainWindow window(nullptr); // this Qt has no Vulkan: the scene view says so, the player still runs
#endif
    window.show();
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) {
        std::filesystem::path dir = platform::fromQt(args[1]);
        if (dir.filename() == artisan::kProjectFile) dir = dir.parent_path();
        window.openProject(dir);
    }
    if (const QString dir = qEnvironmentVariable("ARTISAN_CAPTURE"); !dir.isEmpty())
        QTimer::singleShot(2000, &window, [&window, dir] { window.devCapture(dir); });
    return QApplication::exec();
}
