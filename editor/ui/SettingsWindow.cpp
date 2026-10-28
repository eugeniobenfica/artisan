#include "ui/SettingsWindow.h"

#include "app/Exporter.h"
#include "app/Platform.h"
#include "app/Settings.h"
#include "ui/EditorContext.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDialog>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace artisan::editor {

using namespace ui;

namespace {

QWidget* hbox(QWidget* a, QWidget* b) {
    auto* w = new QWidget;
    auto* row = new QHBoxLayout(w);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(a);
    row->addWidget(b);
    return w;
}

theme::Palette paletteFor(const std::string& name) {
    if (name == "Light") return theme::lightPalette();
    if (name == "Midnight") return theme::midnightPalette();
    if (name != "Dark" && !settings().customTheme.empty()) {
        theme::Palette p;
        if (theme::paletteFromJson(settings().customTheme, p)) return p;
    }
    return theme::darkPalette();
}

// A theme as a card: its colors in a small window drawing, its name under it.
class ThemeCard : public QAbstractButton {
public:
    ThemeCard(theme::Palette palette) : palette_(theme::resolved(palette)) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(150, 112);
        setToolTip(QString::fromStdString(palette_.name));
    }
    std::string name() const { return palette_.name; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        const float w = static_cast<float>(width());
        const theme::Palette& c = palette_;
        d.rectFilled({4, 4}, {w - 4, 78}, c.bgDeep, 6);
        d.rectFilled({8, 8}, {46, 74}, c.bgPanel, 4);       // objects
        d.rectFilled({50, 8}, {w - 8, 20}, c.header, 3);    // menu bar
        d.rectFilled({w - 30, 10}, {w - 10, 18}, c.accent, 2); // export
        d.rectFilled({50, 24}, {w - 8, 74}, c.canvas, 4);   // viewport
        d.circleFilled({w * 0.62f, 49}, 10, c.accent);
        for (int i = 0; i < 4; ++i) d.rectFilled({12, 14.0f + i * 10}, {40, 19.0f + i * 10}, i == 1 ? c.accent : c.textFaint, 2);
        d.rect({4.5f, 4.5f}, {w - 4.5f, 77.5f}, isChecked() ? theme::kAccent : (underMouse() ? theme::kTextFaint : theme::kBorder), 6,
               isChecked() ? 2.0f : 1.0f);
        p.setFont(font());
        const QString n = QString::fromStdString(c.name);
        p.setPen(theme::q(isChecked() ? theme::kText : theme::kTextDim));
        p.drawText(QRectF(0, 84, w, 22), Qt::AlignCenter, n);
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    theme::Palette palette_;
};

QWidget* page(QWidget* content) {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

QWidget* appearancePage(QDialog* dialog) {
    auto* w = new QWidget;
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(24, 20, 24, 20);
    col->setSpacing(12);
    col->addWidget(label(QObject::tr("Appearance"), Tone::Text, 1.4f, true));
    col->addWidget(sectionLabel(QObject::tr("Theme")));
    auto* cards = new QHBoxLayout;
    cards->setSpacing(12);
    auto* group = new QButtonGroup(w);
    std::vector<theme::Palette> palettes = {theme::darkPalette(), theme::lightPalette(), theme::midnightPalette()};
    if (theme::Palette custom; !settings().customTheme.empty() && theme::paletteFromJson(settings().customTheme, custom)) palettes.push_back(custom);
    for (const theme::Palette& pal : palettes) {
        auto* card = new ThemeCard(pal);
        card->setChecked(settings().theme == pal.name);
        group->addButton(card);
        cards->addWidget(card);
        QObject::connect(card, &QAbstractButton::clicked, w, [card] {
            settings().theme = card->name();
            saveSettings();
            applyTheme();
        });
    }
    cards->addStretch(1);
    col->addLayout(cards);

    auto* card = new SettingsCard;
    auto* import = new QPushButton(QObject::tr("Import..."));
    QObject::connect(import, &QPushButton::clicked, dialog, [dialog] {
        const QString file = QFileDialog::getOpenFileName(dialog, QObject::tr("Import a Theme"), QString(), QObject::tr("Theme (*.json)"));
        if (file.isEmpty()) return;
        const auto text = platform::readFile(platform::fromQt(file));
        theme::Palette p;
        std::string error;
        if (!text || !theme::paletteFromJson(*text, p, &error)) {
            QMessageBox::warning(dialog, QObject::tr("Import a Theme"), QObject::tr("Not a theme file: %1").arg(QString::fromStdString(error)));
            return;
        }
        if (p.name == "Dark" || p.name == "Light" || p.name == "Midnight") p.name += " (imported)";
        settings().customTheme = theme::paletteToJson(p);
        settings().theme = p.name;
        saveSettings();
        applyTheme();
        dialog->done(2); // reopened to show the new card
    });
    auto* exportTheme = new QPushButton(QObject::tr("Save Current..."));
    QObject::connect(exportTheme, &QPushButton::clicked, dialog, [dialog] {
        const QString file = QFileDialog::getSaveFileName(dialog, QObject::tr("Save the Theme"), "theme.json", QObject::tr("Theme (*.json)"));
        if (!file.isEmpty()) platform::writeFile(platform::fromQt(file), theme::paletteToJson(theme::current()));
    });
    card->addRow(QObject::tr("Your own theme"), QObject::tr("Themes are JSON files (the same as bemotion's): import one, or save the current one to start from"),
                 hbox(import, exportTheme));
    col->addWidget(card);

    auto* sizes = new SettingsCard;
    auto* scale = new QWidget;
    {
        auto* row = new QHBoxLayout(scale);
        row->setContentsMargins(0, 0, 0, 0);
        const float steps[] = {0.9f, 1.0f, 1.15f, 1.3f};
        int current = 1;
        for (int i = 0; i < 4; ++i)
            if (std::abs(settings().uiScale - steps[i]) < 0.01f) current = i;
        row->addWidget(segmented({QObject::tr("Small"), QObject::tr("Normal"), QObject::tr("Large"), QObject::tr("Larger")}, current, [steps](int i) {
            settings().uiScale = steps[i];
            saveSettings();
            applyTheme();
        }));
    }
    sizes->addRow(QObject::tr("Text size"), QObject::tr("The interface's text and controls"), scale);
    col->addWidget(sizes);
    col->addStretch(1);
    return page(w);
}

QWidget* editorPage() {
    auto* w = new QWidget;
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(24, 20, 24, 20);
    col->setSpacing(12);
    col->addWidget(label(QObject::tr("Editor"), Tone::Text, 1.4f, true));
    auto* card = new SettingsCard;
    auto* folder = new QPushButton(settings().projectsFolder.empty() ? QObject::tr("Choose...") : QString::fromStdString(settings().projectsFolder));
    QObject::connect(folder, &QPushButton::clicked, w, [folder] {
        const QString f = QFileDialog::getExistingDirectory(folder, QObject::tr("Projects Folder"), QString::fromStdString(settings().projectsFolder));
        if (f.isEmpty()) return;
        settings().projectsFolder = platform::utf8(platform::fromQt(f));
        saveSettings();
        folder->setText(f);
    });
    card->addRow(QObject::tr("Projects folder"), QObject::tr("Where new projects are made; every project in it shows on the start screen"), folder);
    auto* grid = new DragValue;
    grid->setRange(4, 512);
    grid->setFormat("%.0f px");
    grid->setValue(static_cast<float>(settings().gridSize));
    QObject::connect(grid, &DragValue::valueChanged, w, [](float v) {
        settings().gridSize = static_cast<int>(std::lround(v));
        saveSettings();
    });
    grid->setMinimumWidth(90);
    card->addRow(QObject::tr("Grid size"), QObject::tr("The viewport's grid, and the step objects snap to"), grid);
    auto* save = new Switch;
    save->setChecked(settings().saveBeforePlay);
    QObject::connect(save, &QAbstractButton::toggled, w, [](bool on) {
        settings().saveBeforePlay = on;
        saveSettings();
    });
    card->addRow(QObject::tr("Save before playing"), QObject::tr("The preview runs the project as saved"), save);
    col->addWidget(card);
    col->addStretch(1);
    return page(w);
}

QWidget* aboutPage(const QString& gpu) {
    auto* w = new QWidget;
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(24, 20, 24, 20);
    col->setSpacing(10);
    auto* brand = new QHBoxLayout;
    brand->addWidget(new LogoMark(56));
    auto* names = new QVBoxLayout;
    names->addWidget(label("Artisan", Tone::Text, 1.8f, true));
    names->addWidget(label(QObject::tr("Version %1  ·  a minimal, fast 2D game engine").arg(ARTISAN_VERSION), Tone::TextDim));
    brand->addLayout(names, 1);
    col->addLayout(brand);
    auto* card = new SettingsCard;
    card->addRow(QObject::tr("Graphics"), gpu.isEmpty() ? QObject::tr("Vulkan") : QObject::tr("Vulkan on %1").arg(gpu), nullptr);
    card->addRow(QObject::tr("Scripting"), QObject::tr("Lua 5.4, and node graphs that compile to Lua"), nullptr);
    card->addRow(QObject::tr("Exports to"), QObject::tr("Windows, Linux, macOS and Android"), nullptr);
    card->addRow(QObject::tr("Built with"), QObject::tr("Qt %1, SDL 3, volk, Lua, nlohmann/json, stb, Inter").arg(qVersion()), nullptr);
    card->addRow(QObject::tr("License"), QObject::tr("MIT: free and open source"), nullptr);
    col->addWidget(card);
    col->addStretch(1);
    return page(w);
}

} // namespace

void applyTheme() {
    theme::Palette p = paletteFor(settings().theme);
    QFont f("Inter");
    f.setPointSizeF(9.5 * settings().uiScale);
    QApplication::setFont(f);
    theme::apply(p);
}

void openSettings(QWidget* parent, const QString& gpuName) {
    for (;;) {
        QDialog dialog(parent);
        dialog.setWindowTitle(QObject::tr("Settings"));
        dialog.resize(860, 560);
        auto* row = new QHBoxLayout(&dialog);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);
        auto* pages = new QListWidget;
        pages->setFixedWidth(190);
        pages->setFrameShape(QFrame::NoFrame);
        pages->setIconSize(QSize(18, 18));
        pages->setSpacing(2);
        auto* stack = new QStackedWidget;
        auto add = [&](Icon icon, const QString& name, QWidget* w) {
            pages->addItem(new QListWidgetItem(menuIcon(icon, theme::kTextDim), name));
            stack->addWidget(w);
        };
        add(Icon::Palette, QObject::tr("Appearance"), appearancePage(&dialog));
        add(Icon::Edit, QObject::tr("Editor"), editorPage());
        add(Icon::Help, QObject::tr("About"), aboutPage(gpuName));
        QObject::connect(pages, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
        pages->setCurrentRow(0);
        row->addWidget(pages);
        row->addWidget(stack, 1);
        if (dialog.exec() != 2) return;
    }
}

void openExportDialog(EditorContext& ctx, QWidget* parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Export Game"));
    dialog.setMinimumWidth(560);
    auto* col = new QVBoxLayout(&dialog);
    col->setContentsMargins(24, 20, 24, 18);
    col->setSpacing(10);
    col->addWidget(label(QObject::tr("Export \"%1\"").arg(QString::fromStdString(ctx.project.name)), Tone::Text, 1.3f, true));
    col->addWidget(sectionLabel(QObject::tr("Platform")));

    auto* group = new QButtonGroup(&dialog);
    auto* card = new SettingsCard;
    for (ExportPlatform p : {ExportPlatform::Windows, ExportPlatform::Linux, ExportPlatform::MacOS, ExportPlatform::Android}) {
        const bool ready = !exportTemplate(p).empty();
        auto* radio = new QRadioButton;
        radio->setEnabled(ready);
        group->addButton(radio, static_cast<int>(p));
        if (p == hostPlatform()) radio->setChecked(true);
        QString note;
        switch (p) {
        case ExportPlatform::Android: note = QObject::tr("A Gradle project with the game inside: build the APK with the Android SDK and NDK"); break;
        case ExportPlatform::MacOS: note = QObject::tr("An app bundle (Vulkan through MoltenVK)"); break;
        default: note = QObject::tr("A folder with the game, ready to zip and share"); break;
        }
        if (!ready) note = QObject::tr("Needs the %1 export template (templates/%2)").arg(exportTarget(p).name, exportTarget(p).folder);
        else if (p == hostPlatform()) note += QObject::tr("  ·  this computer");
        card->addRow(exportTarget(p).name, note, radio);
    }
    col->addWidget(card);

    col->addWidget(sectionLabel(QObject::tr("Where")));
    auto* where = new QLineEdit(platform::toQt(ctx.dir / "export"));
    auto* browse = new QPushButton(QObject::tr("Change..."));
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
        const QString f = QFileDialog::getExistingDirectory(&dialog, QObject::tr("Export to"), where->text());
        if (!f.isEmpty()) where->setText(f);
    });
    auto* whereRow = new QHBoxLayout;
    whereRow->addWidget(where, 1);
    whereRow->addWidget(browse);
    col->addLayout(whereRow);

    auto* status = label("", Tone::TextDim);
    status->setWordWrap(true);
    col->addWidget(status);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* close = new QPushButton(QObject::tr("Close"));
    auto* go = accentButton(QObject::tr("Export"));
    go->setIcon(makeIcon(Icon::Export, theme::kOnAccent, 20));
    buttons->addWidget(close);
    buttons->addWidget(go);
    col->addLayout(buttons);
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(go, &QPushButton::clicked, &dialog, [&] {
        if (group->checkedId() < 0) return;
        std::string error;
        if (ctx.dirty() && !ctx.save(&error)) {
            setTone(status, Tone::Danger);
            status->setText(QString::fromStdString(error));
            return;
        }
        const auto p = static_cast<ExportPlatform>(group->checkedId());
        const ExportResult r = exportGame(ctx.dir, ctx.project.name, p, platform::fromQt(where->text()));
        setTone(status, r.ok ? Tone::Text : Tone::Danger);
        status->setText(QString::fromStdString(r.message) + (r.ok ? "\n" + platform::toQt(r.output) : QString()));
        ctx.log(QObject::tr("Export (%1): %2").arg(exportTarget(p).name, QString::fromStdString(r.message)));
        if (r.ok) platform::revealInFileManager(r.output);
    });
    dialog.exec();
}

} // namespace artisan::editor
