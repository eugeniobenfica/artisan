#include "ui/SettingsWindow.h"

#include "app/Exporter.h"
#include "app/Platform.h"
#include "app/ProjectIO.h"
#include "app/Settings.h"
#include "ui/EditorContext.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QButtonGroup>
#include <QColorDialog>
#include <QDialog>
#include <QDir>
#include <QFontDatabase>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace artisan::editor {

using namespace ui;

namespace {

// --- themes ----------------------------------------------------------------------------------

constexpr Rgba kAccentChoices[] = {rgba(232, 185, 49), rgba(242, 140, 40), rgba(239, 111, 94), rgba(233, 85, 154),
                                   rgba(160, 110, 240), rgba(80, 150, 250), rgba(45, 196, 180), rgba(92, 196, 110)};
constexpr const char* kOwnTheme = "Custom"; // the palette made in Settings › Theme (Advanced)

std::filesystem::path themesDir() { return platform::appDataDir() / "themes"; }

// The built-in themes, then the ones in the themes folder (imported or saved), by name.
std::vector<theme::Palette> themeLibrary() {
    std::vector<theme::Palette> all = {theme::darkPalette(), theme::lightPalette(), theme::midnightPalette(), theme::sunsetPalette()};
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& f : std::filesystem::directory_iterator(themesDir(), ec))
        if (f.path().extension() == ".json") files.push_back(f.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        theme::Palette p;
        const auto text = platform::readFile(f);
        if (!text || !theme::paletteFromJson(*text, p)) continue;
        if (std::none_of(all.begin(), all.end(), [&](const theme::Palette& x) { return x.name == p.name; })) all.push_back(p);
    }
    return all;
}

theme::Palette baseTheme(const std::string& name) {
    if (name == kOwnTheme && !settings().customTheme.empty()) {
        theme::Palette p;
        if (theme::paletteFromJson(settings().customTheme, p)) return p;
    }
    for (const theme::Palette& p : themeLibrary())
        if (p.name == name) return p;
    // Before the themes folder, an imported theme lived in customTheme under its own name.
    if (theme::Palette p; !settings().customTheme.empty() && theme::paletteFromJson(settings().customTheme, p) && p.name == name) return p;
    return theme::darkPalette();
}

Rgba accentOf(const Settings& s, const theme::Palette& base) {
    Rgba c = base.accent;
    if (s.customAccent) theme::fromHex(s.accent, c);
    return c;
}

QPointer<PagedWindow> g_settings, g_help;

void rebuildSettings() {
    if (g_settings) QTimer::singleShot(0, g_settings, [] {
            if (g_settings) g_settings->rebuild();
        });
}

// Applies, saves and (`rebuild`) draws the page again: its colors come from the theme.
void changed(bool rebuild = false) {
    saveSettings();
    applyTheme();
    if (rebuild) rebuildSettings();
}

QPushButton* resetPageButton(std::function<void()> reset) {
    auto* b = new QPushButton(makeIcon(Icon::Reset, theme::kTextDim, 16), QObject::tr("Reset Page"));
    b->setToolTip(QObject::tr("Restore this page's defaults"));
    QObject::connect(b, &QPushButton::clicked, b, [reset] {
        reset();
        changed(true);
    });
    return b;
}

Switch* switchControl(bool on, std::function<void(bool)> toggled) {
    auto* sw = new Switch;
    sw->setChecked(on);
    QObject::connect(sw, &QAbstractButton::toggled, sw, std::move(toggled));
    return sw;
}

// Every color a theme sets, by group; changing one makes the palette the user's own.
void addColorSections(QVBoxLayout* col) {
    struct Entry {
        const char* label;
        Rgba theme::Palette::*field;
    };
    struct Group {
        const char* title;
        std::vector<Entry> entries;
    };
    static const Group kGroups[] = {
        {QT_TR_NOOP("Surfaces"),
         {{QT_TR_NOOP("Background"), &theme::Palette::bgDeep},
          {QT_TR_NOOP("Panels"), &theme::Palette::bgPanel},
          {QT_TR_NOOP("Menu bar and tabs"), &theme::Palette::header},
          {QT_TR_NOOP("Cards and tiles"), &theme::Palette::bgRaised},
          {QT_TR_NOOP("Hover"), &theme::Palette::bgHover},
          {QT_TR_NOOP("Borders"), &theme::Palette::border},
          {QT_TR_NOOP("Canvas surround"), &theme::Palette::canvas},
          {QT_TR_NOOP("Menus and tooltips"), &theme::Palette::menu}}},
        {QT_TR_NOOP("Controls"),
         {{QT_TR_NOOP("Fields"), &theme::Palette::field},
          {QT_TR_NOOP("Field outline"), &theme::Palette::fieldBorder},
          {QT_TR_NOOP("Buttons"), &theme::Palette::button},
          {QT_TR_NOOP("Button outline"), &theme::Palette::buttonBorder},
          {QT_TR_NOOP("Focus outline"), &theme::Palette::focus},
          {QT_TR_NOOP("Selection"), &theme::Palette::selection},
          {QT_TR_NOOP("Text on accent"), &theme::Palette::onAccent}}},
        {QT_TR_NOOP("Text"),
         {{QT_TR_NOOP("Text"), &theme::Palette::text},
          {QT_TR_NOOP("Secondary text"), &theme::Palette::textDim},
          {QT_TR_NOOP("Faint text"), &theme::Palette::textFaint}}},
        {QT_TR_NOOP("Status"),
         {{QT_TR_NOOP("Danger"), &theme::Palette::danger},
          {QT_TR_NOOP("Warning"), &theme::Palette::warning},
          {QT_TR_NOOP("Success"), &theme::Palette::success}}},
    };
    const theme::Palette shown = theme::resolved(theme::current());
    for (const Group& g : kGroups) {
        SettingsCard* card = addSection(col, QObject::tr(g.title));
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(24);
        grid->setVerticalSpacing(10);
        int i = 0;
        for (const Entry& e : g.entries) {
            auto* row = new QHBoxLayout;
            row->setSpacing(10);
            auto* button = new ColorButton;
            button->setFixedSize(28, 22);
            const Rgba c = shown.*(e.field);
            button->setColor(Color{red(c) / 255.0f, green(c) / 255.0f, blue(c) / 255.0f, alpha(c) / 255.0f});
            auto field = e.field;
            QObject::connect(button, &ColorButton::picked, button, [field](const Color& v) {
                theme::Palette p = theme::current(); // what is on screen, accent included
                auto byte = [](float x) { return static_cast<int>(std::lround(std::clamp(x, 0.0f, 1.0f) * 255)); };
                // 0 means "worked out": a color picked fully transparent black stays just visible.
                p.*field = std::max<Rgba>(1, rgba(byte(v.r), byte(v.g), byte(v.b), byte(v.a)));
                p.name = kOwnTheme;
                settings().customTheme = theme::paletteToJson(p);
                settings().theme = kOwnTheme;
                settings().customAccent = false; // the accent is in the palette now
                changed();
            });
            row->addWidget(button);
            row->addWidget(label(QObject::tr(e.label)), 1);
            grid->addLayout(row, i / 2, i % 2);
            ++i;
        }
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 1);
        card->addLayout(grid);
    }
}

QWidget* themePage() {
    Settings& s = settings();
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    pageHeader(col, QObject::tr("Theme"), resetPageButton([] {
                   const Settings d;
                   settings().theme = d.theme;
                   settings().customAccent = d.customAccent;
                   settings().accent = d.accent;
               }));

    SettingsCard* themes = addSection(col, QObject::tr("Theme"));
    auto* cards = new QGridLayout;
    cards->setSpacing(10);
    cards->setAlignment(Qt::AlignLeft);
    std::vector<theme::Palette> all = themeLibrary();
    if (theme::Palette own; s.theme == kOwnTheme && theme::paletteFromJson(s.customTheme, own)) all.push_back(own);
    int n = 0;
    for (const theme::Palette& t : all) {
        auto* card = new ThemeCard(t, s.theme == t.name);
        const std::string name = t.name;
        QObject::connect(card, &QAbstractButton::clicked, card, [name] {
            settings().theme = name;
            changed(true);
        });
        cards->addWidget(card, n / 4, n % 4);
        ++n;
    }
    themes->addLayout(cards);
    auto* import = new QPushButton(QObject::tr("Import..."));
    QObject::connect(import, &QPushButton::clicked, import, [import] {
        const QString file = QFileDialog::getOpenFileName(import, QObject::tr("Import a Theme"), QString(), QObject::tr("Theme (*.json)"));
        if (file.isEmpty()) return;
        const auto text = platform::readFile(platform::fromQt(file));
        theme::Palette p;
        std::string error;
        if (!text || !theme::paletteFromJson(*text, p, &error)) {
            QMessageBox::warning(import, QObject::tr("Import a Theme"), QObject::tr("Not a theme file: %1").arg(QString::fromStdString(error)));
            return;
        }
        for (const theme::Palette& b : {theme::darkPalette(), theme::lightPalette(), theme::midnightPalette(), theme::sunsetPalette()})
            if (p.name == b.name || p.name == kOwnTheme) p.name += " (imported)";
        std::error_code ec;
        std::filesystem::create_directories(themesDir(), ec);
        platform::writeFile(themesDir() / platform::fromQt(file).filename(), theme::paletteToJson(p));
        settings().theme = p.name;
        changed(true);
    });
    auto* folder = new QPushButton(QObject::tr("Open Folder"));
    QObject::connect(folder, &QPushButton::clicked, folder, [] {
        std::error_code ec;
        std::filesystem::create_directories(themesDir(), ec);
        platform::openWithDefaultApp(platform::toQt(themesDir()));
    });
    themes->addRow(QObject::tr("More themes"), QObject::tr("Import a theme someone shared (a .json file, bemotion's too), or open the folder your themes are kept in."),
                   buttonRow({import, folder}));

    SettingsCard* accentCard = addSection(col, QObject::tr("Accent color"));
    const theme::Palette base = baseTheme(s.theme);
    auto* accents = new QHBoxLayout;
    accents->setSpacing(8);
    auto* own = new Swatch(base.accent, !s.customAccent, QObject::tr("The theme's own"));
    QObject::connect(own, &QAbstractButton::clicked, own, [] {
        settings().customAccent = false;
        changed(true);
    });
    accents->addWidget(own);
    for (Rgba c : kAccentChoices) {
        auto* sw = new Swatch(c, s.customAccent && accentOf(s, base) == c);
        QObject::connect(sw, &QAbstractButton::clicked, sw, [c] {
            settings().customAccent = true;
            settings().accent = theme::toHex(c);
            changed(true);
        });
        accents->addWidget(sw);
    }
    accents->addSpacing(8);
    auto* custom = new QPushButton(QObject::tr("Custom..."));
    custom->setToolTip(QObject::tr("Pick any color"));
    QObject::connect(custom, &QPushButton::clicked, custom, [custom] {
        const QColor c = QColorDialog::getColor(theme::q(theme::kAccent), custom, QObject::tr("Accent Color"));
        if (!c.isValid()) return;
        settings().customAccent = true;
        settings().accent = theme::toHex(rgba(c.red(), c.green(), c.blue()));
        changed(true);
    });
    accents->addWidget(custom);
    accents->addStretch();
    accentCard->addLayout(accents);

    if (!s.advanced) {
        col->addSpacing(6);
        col->addWidget(hintLabel(QObject::tr("Every color of the theme, and saving your own theme, are in Advanced options (bottom left).")));
        col->addStretch(1);
        return page;
    }
    col->addSpacing(4);
    col->addWidget(hintLabel(QObject::tr("Changing anything below turns the current theme into your own palette; Save as Theme keeps it.")));
    addColorSections(col);

    SettingsCard* share = addSection(col, QObject::tr("Save and share"));
    auto* name = new QLineEdit;
    name->setPlaceholderText(QObject::tr("Theme name"));
    name->setFixedWidth(180);
    auto* save = new QPushButton(QObject::tr("Save as Theme"));
    QObject::connect(save, &QPushButton::clicked, save, [name, save] {
        const std::string n = name->text().trimmed().toStdString();
        if (n.empty()) return name->setFocus();
        theme::Palette p = theme::current();
        p.name = n;
        std::error_code ec;
        std::filesystem::create_directories(themesDir(), ec);
        std::string error;
        if (!platform::writeFile(themesDir() / platform::fromUtf8(safeFileName(n) + ".json"), theme::paletteToJson(p), &error)) {
            QMessageBox::warning(save, QObject::tr("Save as Theme"), QString::fromStdString(error));
            return;
        }
        settings().theme = n;
        settings().customAccent = false;
        changed(true);
    });
    share->addRow(QObject::tr("Your theme"), QObject::tr("Kept in the themes folder: a .json file to share, which bemotion reads too"),
                  buttonRow({name, save}));
    col->addStretch(1);
    return page;
}

QWidget* interfacePage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    pageHeader(col, QObject::tr("Interface"), resetPageButton([] { settings().uiScale = Settings{}.uiScale; }));
    SettingsCard* sizes = addSection(col, QObject::tr("Size"));
    const float steps[] = {0.9f, 1.0f, 1.15f, 1.3f};
    int current = 1;
    for (int i = 0; i < 4; ++i)
        if (std::abs(settings().uiScale - steps[i]) < 0.01f) current = i;
    sizes->addRow(QObject::tr("Text size"), QObject::tr("The interface's text and controls"),
                  segmented({QObject::tr("Small"), QObject::tr("Normal"), QObject::tr("Large"), QObject::tr("Larger")}, current, [steps](int i) {
                      settings().uiScale = steps[i];
                      changed(true);
                  }));
    SettingsCard* panels = addSection(col, QObject::tr("Panels"));
    panels->addRow(QObject::tr("Arrange the panels"),
                   QObject::tr("Drag a panel by its tab to another side, beside another panel or out of the window. View › Reset Layout puts "
                               "them back: the library on the left, the hierarchy, files and inspector on the right."),
                   nullptr);
    col->addStretch(1);
    return page;
}

QWidget* editorPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    pageHeader(col, QObject::tr("Editor"), resetPageButton([] {
                   const Settings d;
                   settings().gridSize = d.gridSize;
                   settings().showGrid = d.showGrid;
                   settings().snapToGrid = d.snapToGrid;
                   settings().saveBeforePlay = d.saveBeforePlay;
               }));
    SettingsCard* projects = addSection(col, QObject::tr("Projects"));
    auto* folder = new QPushButton(settings().projectsFolder.empty() ? QObject::tr("Choose...") : QString::fromStdString(settings().projectsFolder));
    QObject::connect(folder, &QPushButton::clicked, folder, [folder] {
        const QString f = QFileDialog::getExistingDirectory(folder, QObject::tr("Projects Folder"), QString::fromStdString(settings().projectsFolder));
        if (f.isEmpty()) return;
        settings().projectsFolder = platform::utf8(platform::fromQt(f));
        saveSettings();
        folder->setText(f);
    });
    projects->addRow(QObject::tr("Projects folder"), QObject::tr("Where new projects are made; every project in it shows on the start screen"), folder);
    projects->addRow(QObject::tr("Save before playing"), QObject::tr("The preview runs the project as saved"), switchControl(settings().saveBeforePlay, [](bool on) {
                         settings().saveBeforePlay = on;
                         saveSettings();
                     }));

    SettingsCard* view = addSection(col, QObject::tr("Viewport"));
    auto* grid = new DragValue;
    grid->setRange(4, 512);
    grid->setFormat("%.0f px");
    grid->setValue(static_cast<float>(settings().gridSize));
    grid->setMinimumWidth(90);
    QObject::connect(grid, &DragValue::valueChanged, grid, [](float v) {
        settings().gridSize = static_cast<int>(std::lround(v));
        saveSettings();
    });
    view->addRow(QObject::tr("Grid size"), QObject::tr("The viewport's grid, and the step objects snap to"), grid);
    view->addRow(QObject::tr("Show the grid"), QObject::tr("Also the grid button over the viewport"), switchControl(settings().showGrid, [](bool on) {
                     settings().showGrid = on;
                     saveSettings();
                 }));
    view->addRow(QObject::tr("Snap to the grid"), QObject::tr("Dragged objects land on the grid; hold Shift for the other way"),
                 switchControl(settings().snapToGrid, [](bool on) {
                     settings().snapToGrid = on;
                     saveSettings();
                 }));
    col->addStretch(1);
    return page;
}

// --- shortcuts ---------------------------------------------------------------------------------

struct Shortcut {
    const char* action;
    const char* keys; // "Ctrl+S", alternatives with " / "
};
struct ShortcutGroup {
    const char* title;
    std::vector<Shortcut> items;
};

const std::vector<ShortcutGroup>& shortcutGroups() {
    static const std::vector<ShortcutGroup> groups = {
        {QT_TR_NOOP("Project"),
         {{QT_TR_NOOP("Save"), "Ctrl+S"},
          {QT_TR_NOOP("Export the game"), "Ctrl+E"},
          {QT_TR_NOOP("Settings"), "Ctrl+,"},
          {QT_TR_NOOP("Help"), "F1"},
          {QT_TR_NOOP("Quit"), "Ctrl+Q"}}},
        {QT_TR_NOOP("Editing"),
         {{QT_TR_NOOP("Undo"), "Ctrl+Z"},
          {QT_TR_NOOP("Redo"), "Ctrl+Y"},
          {QT_TR_NOOP("Duplicate"), "Ctrl+D"},
          {QT_TR_NOOP("Delete"), "Delete"},
          {QT_TR_NOOP("Deselect (the scene is selected)"), "Esc"},
          {QT_TR_NOOP("Back / forward in the navigator"), "Alt+Left / Alt+Right"}}},
        {QT_TR_NOOP("Game"), {{QT_TR_NOOP("Play"), "F5"}, {QT_TR_NOOP("Stop"), "Shift+F5"}}},
        {QT_TR_NOOP("Viewport"),
         {{QT_TR_NOOP("Zoom"), "Wheel"},
          {QT_TR_NOOP("Pan"), "Middle drag / Right drag / Space+drag"},
          {QT_TR_NOOP("Snap the other way"), "Shift+drag"}}},
        {QT_TR_NOOP("Nodes"),
         {{QT_TR_NOOP("Add a node"), "Right-click / Double-click"},
          {QT_TR_NOOP("Cut a pin's wires"), "Alt+click"},
          {QT_TR_NOOP("Delete the node"), "Delete"},
          {QT_TR_NOOP("Frame all nodes"), "F"}}},
    };
    return groups;
}

// "Ctrl+S / Ctrl+Y" drawn as key caps.
class KeyCaps : public QWidget {
public:
    explicit KeyCaps(const QString& keys) {
        for (const QString& alt : keys.split(" / ")) caps_.push_back(alt.split('+'));
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.9);
        setFont(f);
        setMinimumSize(sizeHint());
    }
    QSize sizeHint() const override {
        int w = 0;
        for (size_t i = 0; i < caps_.size(); ++i) {
            if (i) w += fontMetrics().horizontalAdvance(" or ") + 8;
            for (const QString& k : caps_[i]) w += fontMetrics().horizontalAdvance(k) + 14 + 4;
        }
        return {w, fontMetrics().height() + 10};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Draw d(p);
        float x = static_cast<float>(width() - sizeHint().width());
        const float h = static_cast<float>(height());
        for (size_t i = 0; i < caps_.size(); ++i) {
            if (i) {
                const QString orText = QObject::tr("or");
                d.text({x + 4, (h - d.textHeight()) / 2}, theme::kTextFaint, orText);
                x += fontMetrics().horizontalAdvance(" or ") + 8;
            }
            for (const QString& k : caps_[i]) {
                const float w = fontMetrics().horizontalAdvance(k) + 14.0f;
                d.rectFilled({x, 2}, {x + w, h - 2}, theme::kField, 4);
                d.rect({x + 0.5f, 2.5f}, {x + w - 0.5f, h - 2.5f}, theme::kFieldBorder, 4);
                d.text({x + 7, (h - d.textHeight()) / 2}, theme::kText, k);
                x += w + 4;
            }
        }
    }

private:
    std::vector<QStringList> caps_;
};

QWidget* shortcutsPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    pageHeader(col, QObject::tr("Shortcuts"));
    auto* search = new QLineEdit;
    search->setPlaceholderText(QObject::tr("Search actions or keys"));
    search->setClearButtonEnabled(true);
    col->addWidget(search);
    std::vector<std::pair<QWidget*, QString>> rows; // each row and what the search finds it by
    std::vector<std::pair<QWidget*, std::vector<QWidget*>>> sections;
    for (const ShortcutGroup& g : shortcutGroups()) {
        const int before = col->count();
        SettingsCard* card = addSection(col, QObject::tr(g.title));
        std::vector<QWidget*> extra;
        for (int i = before; i < col->count(); ++i)
            if (QWidget* w = col->itemAt(i)->widget()) extra.push_back(w);
        std::vector<QWidget*> mine;
        for (const Shortcut& s : g.items) {
            QWidget* row = card->addRow(QObject::tr(s.action), {}, new KeyCaps(s.keys));
            row->setMinimumHeight(40);
            rows.push_back({row, (QObject::tr(s.action) + " " + s.keys).toLower()});
            mine.push_back(row);
        }
        sections.push_back({card, mine});
        for (QWidget* w : extra)
            if (w != card) sections.push_back({w, mine});
    }
    QObject::connect(search, &QLineEdit::textChanged, page, [rows, sections](const QString& text) {
        const QString t = text.trimmed().toLower();
        for (const auto& [row, words] : rows) row->setVisible(t.isEmpty() || words.contains(t));
        for (const auto& [w, mine] : sections)
            w->setVisible(std::any_of(mine.begin(), mine.end(), [](QWidget* r) { return !r->isHidden(); }));
    });
    col->addStretch(1);
    return page;
}

// --- help --------------------------------------------------------------------------------------

QPushButton* linkButton(const QString& text, const QString& url) {
    auto* b = new QPushButton(makeIcon(Icon::ArrowRight, theme::kTextDim, 16), text);
    QObject::connect(b, &QPushButton::clicked, b, [url] { platform::openWithDefaultApp(url); });
    return b;
}

QString docPath(const char* file) { return platform::toQt(std::filesystem::path(ARTISAN_SOURCE_DIR) / "docs" / file); }

QWidget* startPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    pageHeader(col, QObject::tr("Getting started"));
    col->addWidget(paragraph(QObject::tr("A game is a project folder: its scenes, the scripts in scripts/ and the pictures in assets/. Here is "
                                         "how the editor is laid out.")));
    SettingsCard* card = addSection(col, QObject::tr("The workspace"));
    card->addRow(QObject::tr("Library (left)"), QObject::tr("Ready-made objects: a player that already moves, shapes, sprites, lights. Click "
                                                            "one to add it where the camera looks. In the Nodes view it lists the nodes instead: "
                                                            "click to add, or drag one onto the graph."),
                 nullptr);
    card->addRow(QObject::tr("Scenes (top)"), QObject::tr("Each scene is a tab. The + adds one; right-click a tab to rename it, duplicate it or "
                                                         "make it the one the game starts with. The navigator beside it goes back and forward "
                                                         "through what you selected."),
                 nullptr);
    card->addRow(QObject::tr("Hierarchy and Files (right)"), QObject::tr("The scene and its objects, in drawing order (drag to reorder); the "
                                                                       "project's files, to open a script or add a picture as a sprite."),
                 nullptr);
    card->addRow(QObject::tr("Inspector (right)"), QObject::tr("The selected object's components, or the scene's settings when the scene is "
                                                              "selected."),
                 nullptr);
    card->addRow(QObject::tr("Panels anywhere"), QObject::tr("Drag a panel by its tab to another side, or out of the window. View › Reset "
                                                            "Layout brings the first layout back."),
                 nullptr);
    SettingsCard* behavior = addSection(col, QObject::tr("Behavior"));
    behavior->addRow(QObject::tr("Nodes or Lua"), QObject::tr("An object, or the scene itself, can have a node graph and a Lua script. Select "
                                                              "it, then open the Nodes or Script view."),
                     nullptr);
    behavior->addRow(QObject::tr("Play"), QObject::tr("F5 runs the game in its own window; what it prints shows in the Console."), nullptr);
    col->addStretch(1);
    return page;
}

QWidget* scriptingPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    pageHeader(col, QObject::tr("Scripting"));
    SettingsCard* card = addSection(col, QObject::tr("Guides"));
    card->addRow(QObject::tr("Scripting reference"), QObject::tr("Every Lua function and node: objects, input, time, the camera, the scene"),
                 linkButton(QObject::tr("Open"), docPath("SCRIPTING.md")));
    card->addRow(QObject::tr("How Artisan works"), QObject::tr("The engine, the player and the editor; how scenes are drawn"),
                 linkButton(QObject::tr("Open"), docPath("ARCHITECTURE.md")));
    card->addRow(QObject::tr("Online"), QObject::tr("The project's page, its documentation and news"),
                 linkButton(QObject::tr("Visit"), "https://github.com/eugeniobenfica/artisan#readme"));
    SettingsCard* lua = addSection(col, QObject::tr("A script, in short"));
    auto* code = new QLabel("function start(self)\n    log(\"Hello from \" .. self.name)\nend\n\nfunction update(self, dt)\n"
                            "    if input.down(\"right\") then self.x = self.x + 300 * dt end\nend");
    code->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    code->setTextInteractionFlags(Qt::TextSelectableByMouse);
    setTone(code, Tone::Text);
    lua->addContent(code);
    col->addStretch(1);
    return page;
}

QWidget* aboutPage(const QString& gpu) {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    auto* brand = new QHBoxLayout;
    brand->setSpacing(14);
    brand->addWidget(new LogoMark(56));
    auto* names = new QVBoxLayout;
    names->addWidget(label("Artisan", Tone::Text, 1.8f, true));
    names->addWidget(label(QObject::tr("Version %1  ·  a minimal, fast 2D game engine").arg(ARTISAN_VERSION), Tone::TextDim));
    brand->addLayout(names, 1);
    col->addLayout(brand);
    SettingsCard* card = addSection(col, QObject::tr("About"));
    card->addRow(QObject::tr("Graphics"), gpu.isEmpty() ? QObject::tr("Vulkan") : QObject::tr("Vulkan on %1").arg(gpu), nullptr);
    card->addRow(QObject::tr("Scripting"), QObject::tr("Lua 5.4, and node graphs that compile to Lua"), nullptr);
    card->addRow(QObject::tr("Exports to"), QObject::tr("Windows, Linux, macOS and Android"), nullptr);
    SettingsCard* credits = addSection(col, QObject::tr("Credits"));
    credits->addRow(QObject::tr("Built with"), QObject::tr("Qt %1, Qt Advanced Docking System, SDL 3, volk, Lua, nlohmann/json, stb, Inter").arg(qVersion()),
                    nullptr);
    credits->addRow(QObject::tr("The look"), QObject::tr("bestyle, shared with bemotion"), nullptr);
    credits->addRow(QObject::tr("License"), QObject::tr("MIT: free and open source"), nullptr);
    col->addStretch(1);
    return page;
}

} // namespace

void applyTheme() {
    const Settings& s = settings();
    theme::Palette p = baseTheme(s.theme);
    if (s.customAccent && theme::fromHex(s.accent, p.accent)) p.onAccent = 0; // worked out for the new accent
    QFont f("Inter");
    f.setPointSizeF(9.5 * s.uiScale);
    QApplication::setFont(f);
    theme::apply(p);
}

QWidget* openSettings(QWidget* parent) {
    if (!g_settings) {
        auto* footer = new QWidget;
        auto* footerCol = new QVBoxLayout(footer);
        footerCol->setContentsMargins(0, 0, 0, 0);
        footerCol->setSpacing(6);
        auto* advancedRow = new QHBoxLayout;
        advancedRow->setContentsMargins(8, 0, 4, 0);
        auto* advancedLabel = label(QObject::tr("Advanced options"));
        advancedLabel->setToolTip(QObject::tr("Every page shows all its options (each color of the theme)"));
        advancedRow->addWidget(advancedLabel, 1);
        auto* advanced = switchControl(settings().advanced, [](bool on) {
            settings().advanced = on;
            saveSettings();
            rebuildSettings();
        });
        advanced->setToolTip(advancedLabel->toolTip());
        advancedRow->addWidget(advanced);
        footerCol->addLayout(advancedRow);
        auto* resetAll = new QPushButton(QObject::tr("Reset All"));
        resetAll->setToolTip(QObject::tr("Restore every setting to its default"));
        footerCol->addWidget(resetAll);
        g_settings = new PagedWindow(QObject::tr("Settings"),
                                     {{QObject::tr("Theme"), Icon::Palette, themePage},
                                      {QObject::tr("Interface"), Icon::Sliders, interfacePage},
                                      {QObject::tr("Editor"), Icon::Edit, editorPage},
                                      {QObject::tr("Shortcuts"), Icon::Keyboard, shortcutsPage}},
                                     footer, parent);
        g_settings->setAttribute(Qt::WA_DeleteOnClose, false);
        QObject::connect(resetAll, &QPushButton::clicked, g_settings, [] {
            if (QMessageBox::question(g_settings, QObject::tr("Reset All"), QObject::tr("Reset every setting to its default? Projects and recent files stay.")) !=
                QMessageBox::Yes)
                return;
            Settings fresh;
            fresh.projectsFolder = settings().projectsFolder;
            fresh.recents = settings().recents;
            fresh.advanced = settings().advanced; // a way of looking, not a preference
            fresh.dockLayout = settings().dockLayout;
            settings() = fresh;
            changed(true);
        });
        g_settings->showPage(0);
    }
    g_settings->present();
    return g_settings;
}

QWidget* openHelp(QWidget* parent, HelpPage which, const QString& gpuName) {
    if (!g_help) {
        g_help = new PagedWindow(QObject::tr("Help"),
                                 {{QObject::tr("Getting started"), Icon::Play, startPage},
                                  {QObject::tr("Shortcuts"), Icon::Keyboard, shortcutsPage},
                                  {QObject::tr("Scripting"), Icon::Code, scriptingPage},
                                  {QObject::tr("About"), Icon::Help, [gpuName] { return aboutPage(gpuName); }}},
                                 nullptr, parent);
        g_help->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    g_help->showPage(static_cast<int>(which));
    g_help->present();
    return g_help;
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
