#include "ui/SettingsWindow.h"

#include "app/Exporter.h"
#include "app/Platform.h"
#include "app/Plugins.h"
#include "app/ProjectIO.h"
#include "app/Settings.h"
#include "app/Shortcuts.h"
#include "ui/EditorContext.h"
#include "ui/NodeEditor.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDir>
#include <QFontDatabase>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>

namespace artisan::editor {

using namespace ui;

SettingsSignals& SettingsSignals::get() {
    static SettingsSignals s;
    return s;
}

namespace {

// --- themes ----------------------------------------------------------------------------------

constexpr Rgba kArtisanCyan = rgba(32, 212, 176);
constexpr Rgba kAccentChoices[] = {kArtisanCyan,        rgba(80, 150, 250),  rgba(160, 110, 240), rgba(233, 85, 154),
                                   rgba(239, 111, 94),  rgba(242, 140, 40),  rgba(232, 185, 49),  rgba(92, 196, 110)};
constexpr const char* kOwnTheme = "Custom"; // the palette made in Settings › Theme (Advanced)

std::filesystem::path themesDir() { return platform::appDataDir() / "themes"; }

// bestyle's themes in Artisan's colors: a green-leaning cyan is its accent (Sunset keeps its coral).
std::vector<theme::Palette> builtinThemes() {
    theme::Palette dark = theme::darkPalette(), light = theme::lightPalette(), midnight = theme::midnightPalette();
    dark.accent = midnight.accent = kArtisanCyan;
    light.accent = rgba(13, 148, 122); // darker: readable on white
    return {dark, light, midnight, theme::sunsetPalette()};
}

// The built-in themes, then the ones in the themes folder (imported or saved), by name.
std::vector<theme::Palette> themeLibrary() {
    std::vector<theme::Palette> all = builtinThemes();
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
    return builtinThemes().front();
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
        for (const theme::Palette& b : builtinThemes())
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

QString docPath(const char* file); // the docs folder (below)

// A size of the node graph, as a card: a small node drawn at that size, its name under it.
class NodeSizeCard : public QAbstractButton {
public:
    NodeSizeCard(const QString& label, float scale, bool selected) : label_(label), scale_(scale), selected_(selected) {
        setFixedSize(128, 82);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        Draw d(painter);
        const float w = static_cast<float>(width()), h = static_cast<float>(height());
        d.rectFilled({0, 0}, {w, h}, theme::kBgRaised, 8.0f);
        const float nw = 62.0f * scale_, header = 11.0f * scale_, body = 16.0f * scale_;
        const float x = (w - nw) * 0.5f, y = 10.0f;
        const Rgba cat = nodeCategoryColor("Actions");
        d.rectFilled({x, y}, {x + nw, y + header + body}, theme::kBgHover, 4.0f);
        d.rectFilled({x, y}, {x + nw, y + header}, theme::withAlpha(cat, 120), 4.0f);
        d.rectFilled({x + 6, y + header * 0.5f - 1.5f}, {x + nw * 0.6f, y + header * 0.5f + 1.5f}, theme::kText, 1.5f);
        d.circleFilled({x, y + header + body * 0.5f}, 2.6f * scale_, rgba(92, 196, 110));
        d.circleFilled({x + nw, y + header + body * 0.5f}, 2.6f * scale_, rgba(92, 196, 110));
        d.text({10, h - d.textHeight() - 8}, selected_ || underMouse() ? theme::kText : theme::kTextDim, label_);
        if (selected_ || underMouse()) d.rect({1, 1}, {w - 1, h - 1}, selected_ ? theme::kAccent : theme::ink(80), 8.0f, selected_ ? 2.0f : 1.0f);
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    QString label_;
    float scale_;
    bool selected_;
};

QWidget* interfacePage() {
    Settings& s = settings();
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    pageHeader(col, QObject::tr("Interface"), resetPageButton([] {
                   Settings& st = settings();
                   const Settings d;
                   st.uiScale = d.uiScale, st.uiFont = d.uiFont, st.nodeScale = d.nodeScale;
                   st.showPath = d.showPath, st.showLua = d.showLua, st.nodeGrid = d.nodeGrid;
               }));

    SettingsCard* text = addSection(col, QObject::tr("Text"));
    text->addRow(QObject::tr("Text size"), QObject::tr("Every label in the interface"),
                 sliderControl(s.uiScale * 100.0f, 80.0f, 150.0f, 5.0f, "%.0f%%", [](float v) {
                     settings().uiScale = v / 100.0f;
                     saveSettings();
                     // A moment later: the slider is still being dragged, and the page is rebuilt.
                     static QTimer* later = [] {
                         auto* t = new QTimer(qApp);
                         t->setSingleShot(true);
                         QObject::connect(t, &QTimer::timeout, [] { applyTheme(); });
                         return t;
                     }();
                     later->start(250);
                 }));
    auto* font = new QComboBox;
    font->setEditable(true);
    font->setInsertPolicy(QComboBox::NoInsert);
    font->addItems(QFontDatabase::families());
    font->setCurrentText(QString::fromStdString(s.uiFont));
    font->completer()->setFilterMode(Qt::MatchContains);
    font->completer()->setCompletionMode(QCompleter::PopupCompletion);
    font->setFixedWidth(240);
    QObject::connect(font, &QComboBox::activated, font, [font](int) {
        const QString f = font->currentText();
        if (!QFontDatabase::families().contains(f)) return;
        settings().uiFont = f.toStdString();
        changed();
    });
    text->addRow(QObject::tr("Interface font"), QObject::tr("Inter ships with Artisan; the others come from your system"), font);

    SettingsCard* nodes = addSection(col, QObject::tr("Nodes"));
    auto* sizes = new QHBoxLayout;
    sizes->setSpacing(10);
    auto size = [&](const QString& label, float scale) {
        auto* card = new NodeSizeCard(label, scale, std::fabs(s.nodeScale - scale) < 0.01f);
        QObject::connect(card, &QAbstractButton::clicked, card, [scale] {
            settings().nodeScale = scale;
            changed(true);
        });
        sizes->addWidget(card);
    };
    size(QObject::tr("Compact"), 0.85f);
    size(QObject::tr("Comfortable"), 1.0f);
    size(QObject::tr("Large"), 1.2f);
    sizes->addStretch();
    nodes->addLayout(sizes);

    SettingsCard* show = addSection(col, QObject::tr("Show"));
    auto showRow = [&](const QString& name, const QString& about, bool Settings::*field) {
        show->addRow(name, about, switchControl(s.*field, [field](bool on) {
                         settings().*field = on;
                         changed();
                     }));
    };
    showRow(QObject::tr("Where you are"), QObject::tr("Project › Scene › Object, at the right of the scene tabs"), &Settings::showPath);
    showRow(QObject::tr("Lua beside the graph"), QObject::tr("The code a node graph becomes, next to it (also the ƒ button)"), &Settings::showLua);
    showRow(QObject::tr("Dots behind the nodes"), QObject::tr("A grid to line nodes up with"), &Settings::nodeGrid);

    SettingsCard* panels = addSection(col, QObject::tr("Panels"));
    auto* layout = new QPushButton(QObject::tr("Reset Layout"));
    QObject::connect(layout, &QPushButton::clicked, layout, [] { emit SettingsSignals::get().resetLayoutRequested(); });
    panels->addRow(QObject::tr("Arrange the panels"),
                   QObject::tr("Drag a panel by its tab to another side, beside another panel or out of the window; the – on its header "
                               "tucks it into a tab on the window's edge."),
                   layout);

    SettingsCard* reset = addSection(col, QObject::tr("Start over"));
    auto* resetInterface = new QPushButton(QObject::tr("Reset Interface..."));
    QObject::connect(resetInterface, &QPushButton::clicked, resetInterface, [] {
        if (QMessageBox::question(g_settings, QObject::tr("Reset Interface"), QObject::tr("Restore the entire interface to its defaults?")) != QMessageBox::Yes)
            return;
        Settings& st = settings();
        const Settings d;
        st.theme = d.theme, st.customAccent = d.customAccent, st.accent = d.accent;
        st.uiScale = d.uiScale, st.uiFont = d.uiFont, st.nodeScale = d.nodeScale;
        st.showPath = d.showPath, st.showLua = d.showLua, st.nodeGrid = d.nodeGrid;
        resetAllKeys();
        emit SettingsSignals::get().resetLayoutRequested();
        changed(true);
    });
    reset->addRow(QObject::tr("Reset the interface"),
                  QObject::tr("Theme, text, nodes, shortcuts and the panels' layout go back to their defaults. Projects, plugins and recent "
                              "files stay."),
                  resetInterface);
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
                   settings().autosave = d.autosave;
               }));
    SettingsCard* saving = addSection(col, QObject::tr("Saving"));
    saving->addRow(QObject::tr("Save after every change"), QObject::tr("A project saves itself a moment after each change"),
                   switchControl(settings().autosave, [](bool on) {
                       settings().autosave = on;
                       saveSettings();
                   }));
    saving->addRow(QObject::tr("Crash recovery"), QObject::tr("Unsaved changes are always kept aside: if Artisan closes unexpectedly, it offers "
                                                              "them back the next time the project opens"),
                   nullptr);
    saving->addRow(QObject::tr("Save before playing"), QObject::tr("The preview runs the project as saved"), switchControl(settings().saveBeforePlay, [](bool on) {
                       settings().saveBeforePlay = on;
                       saveSettings();
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

void rebuildSoon() {
    rebuildSettings();
    if (g_help) QTimer::singleShot(0, g_help, [] {
            if (g_help) g_help->rebuild();
        });
}

// Settings › Shortcuts: each action's keys as a field that records new ones, a reset when they
// changed, a bin to remove them; a clash is named beside the keys.
QWidget* shortcutsPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    pageHeader(col, QObject::tr("Shortcuts"), resetPageButton([] { resetAllKeys(); }));
    static QString filter; // kept while the page is rebuilt after a change
    auto* search = new QLineEdit(filter);
    search->setPlaceholderText(QObject::tr("Search shortcuts (a name or keys, e.g. \"Ctrl+S\")"));
    search->setClearButtonEnabled(true);
    col->addWidget(search);
    struct Row {
        QWidget* widget;
        SettingsCard* card;
        QString words;
    };
    auto rows = std::make_shared<std::vector<Row>>();
    std::vector<std::pair<SettingsCard*, QLabel*>> cards;
    const char* group = nullptr;
    SettingsCard* card = nullptr;
    for (const ActionInfo& a : actionInfos()) {
        if (!group || std::strcmp(group, a.group) != 0) {
            group = a.group;
            col->addSpacing(8);
            QLabel* title = sectionLabel(QObject::tr(group));
            col->addWidget(title);
            card = new SettingsCard;
            col->addWidget(card);
            cards.emplace_back(card, title);
        }
        const std::string id = a.id;
        auto* tools = new QWidget;
        auto* h = new QHBoxLayout(tools);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);
        if (const ActionInfo* other = clashWith(id)) {
            auto* clash = label(QObject::tr("Also %1").arg(QObject::tr(other->label)), Tone::Danger);
            h->addWidget(clash);
        }
        auto* keys = new ShortcutEdit(keysOf(id));
        QObject::connect(keys, &ShortcutEdit::chosen, keys, [id](const QKeySequence& k) {
            setKeys(id, k);
            rebuildSoon(); // clashes may have changed
        });
        h->addWidget(keys);
        const bool changedKeys = keysOf(id) != QKeySequence(QString::fromLatin1(a.keys), QKeySequence::PortableText);
        // The reset keeps its place when hidden: the key fields stay in one column.
        auto* resetSlot = new QWidget;
        resetSlot->setFixedSize(26, 26);
        auto* slot = new QHBoxLayout(resetSlot);
        slot->setContentsMargins(0, 0, 0, 0);
        auto* reset = new IconButton(Icon::Reset, QObject::tr("Back to the default"), 26);
        reset->setVisible(changedKeys);
        QObject::connect(reset, &QAbstractButton::clicked, reset, [id] {
            resetKeys(id);
            rebuildSoon();
        });
        slot->addWidget(reset);
        h->addWidget(resetSlot);
        auto* clear = new IconButton(Icon::Trash, QObject::tr("Remove this shortcut"), 26);
        clear->setEnabled(!keysOf(id).isEmpty());
        QObject::connect(clear, &QAbstractButton::clicked, clear, [id] {
            setKeys(id, QKeySequence());
            rebuildSoon();
        });
        h->addWidget(clear);
        QWidget* row = card->addRow(QObject::tr(a.label), {}, tools);
        row->setMinimumHeight(38);
        row->layout()->setContentsMargins(16, 5, 16, 5); // a long list: compact rows
        rows->push_back({row, card, (QObject::tr(a.label) + " " + keysOf(id).toString(QKeySequence::NativeText)).toLower()});
    }
    auto refresh = [rows, cards] {
        const QString f = filter.trimmed().toLower();
        std::map<SettingsCard*, int> shown;
        for (const Row& r : *rows) {
            const bool on = f.isEmpty() || r.words.contains(f);
            r.widget->setVisible(on);
            shown[r.card] += on;
        }
        for (const auto& [c, title] : cards) {
            c->setVisible(shown[c] > 0);
            title->setVisible(shown[c] > 0);
            c->update();
        }
    };
    refresh();
    QObject::connect(search, &QLineEdit::textChanged, search, [refresh](const QString& t) {
        filter = t;
        refresh();
    });
    col->addStretch(1);
    return page;
}

// --- plugins -----------------------------------------------------------------------------------

std::string g_pluginMessage; // the last install / removal, shown on the page

QLabel* pluginText(const QString& text, Tone tone = Tone::TextDim) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    setTone(l, tone);
    return l;
}

QString permissionLabel(const std::string& permission) {
    if (permission == "edit") return QObject::tr("Edits the scene");
    return QString::fromStdString(permission);
}

void reloadPlugins() {
    PluginHost::get().load();
    rebuildSettings();
}

// One plugin, as a row of its card: its picture, name and what it does, what it adds and may do,
// its switch; its folder, removal and log under it.
QWidget* pluginRow(const Plugin& p) {
    PluginHost& host = PluginHost::get();
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(16, 14, 16, 14);
    h->setSpacing(14);
    QStringList objects, commands;
    Icon icon = Icon::Plugin;
    bool iconSet = !p.icon.empty();
    if (iconSet) icon = pluginIcon(p.icon);
    for (const PluginObject* o : host.objects())
        if (o->pluginId == p.id) {
            objects << QString::fromStdString(o->name);
            if (!iconSet) icon = pluginIcon(o->icon), iconSet = true;
        }
    for (const PluginCommand* c : host.commands())
        if (c->pluginId == p.id) {
            commands << QString::fromStdString(c->name);
            if (!iconSet) icon = pluginIcon(c->icon), iconSet = true;
        }
    const bool ok = p.error.empty();
    h->addWidget(new IconTile(icon, p.enabled && ok), 0, Qt::AlignTop);

    auto* body = new QVBoxLayout;
    body->setSpacing(4);
    auto* head = new QHBoxLayout;
    head->setSpacing(8);
    head->addWidget(label(QString::fromStdString(p.name), Tone::Text, 1.0f, true));
    head->addWidget(label(QObject::tr("Version %1").arg(QString::fromStdString(p.version)) +
                              QString::fromStdString(p.author.empty() ? "" : "  ·  " + p.author),
                          Tone::TextFaint));
    head->addStretch();
    body->addLayout(head);
    if (!p.description.empty()) body->addWidget(pluginText(QString::fromStdString(p.description)));
    if (!ok) body->addWidget(pluginText(QObject::tr("Could not load: %1").arg(QString::fromStdString(p.error)), Tone::Danger));
    if (!p.enabled) body->addWidget(pluginText(QObject::tr("Off: what it adds is hidden"), Tone::TextFaint));
    if (!objects.isEmpty()) body->addWidget(pluginText(QObject::tr("Adds  %1").arg(objects.join(", ")), Tone::Text));
    if (!commands.isEmpty()) body->addWidget(pluginText(QObject::tr("Commands  %1").arg(commands.join(", ")), Tone::Text));
    if (!p.permissions.empty()) {
        auto* chips = new QHBoxLayout;
        chips->setSpacing(6);
        for (const std::string& permission : p.permissions) chips->addWidget(new Chip(permissionLabel(permission)));
        chips->addStretch();
        body->addSpacing(2);
        body->addLayout(chips);
    }
    // Its actions, quiet: they are rarely needed.
    auto* actions = new QHBoxLayout;
    actions->setSpacing(2);
    auto quiet = [](const QString& text) {
        auto* b = new QPushButton(text);
        b->setProperty("flat", true);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    auto* show = quiet(QObject::tr("Show Folder"));
    const std::filesystem::path dir = p.dir;
    QObject::connect(show, &QPushButton::clicked, show, [dir] { platform::revealInFileManager(dir / "plugin.json"); });
    actions->addWidget(show);
    const std::string id = p.id;
    if (!p.builtin) {
        auto* remove = quiet(QObject::tr("Remove..."));
        remove->setToolTip(QObject::tr("Move this plugin to the trash"));
        QObject::connect(remove, &QPushButton::clicked, remove, [id, name = QString::fromStdString(p.name)] {
            if (QMessageBox::question(g_settings, QObject::tr("Remove Plugin"), QObject::tr("Move \"%1\" to the trash?").arg(name)) != QMessageBox::Yes)
                return;
            std::string error;
            g_pluginMessage = PluginHost::get().uninstall(id, &error) ? QObject::tr("Plugin moved to the trash.").toStdString()
                                                                       : QObject::tr("Could not remove it: %1").arg(QString::fromStdString(error)).toStdString();
            rebuildSettings();
        });
        actions->addWidget(remove);
    }
    QPlainTextEdit* log = nullptr;
    if (!p.log.empty()) {
        auto* toggle = quiet(QObject::tr("Log (%1)").arg(p.log.size()));
        toggle->setCheckable(true);
        log = new QPlainTextEdit;
        log->setReadOnly(true);
        log->setMaximumHeight(140);
        QString text;
        for (const std::string& l : p.log) text += QString::fromStdString(l) + "\n";
        log->setPlainText(text);
        QObject::connect(toggle, &QPushButton::toggled, log, &QWidget::setVisible);
        actions->addWidget(toggle);
    }
    actions->addStretch();
    body->addSpacing(2);
    body->addLayout(actions);
    if (log) {
        body->addWidget(log);
        log->hide();
    }
    h->addLayout(body, 1);

    auto* enabled = new Switch;
    enabled->setChecked(p.enabled);
    enabled->setToolTip(p.enabled ? QObject::tr("On: its objects and commands are in the editor") : QObject::tr("Off: hidden from the editor"));
    QObject::connect(enabled, &QAbstractButton::toggled, enabled, [id](bool on) {
        // After this click is handled: the page (and this switch) is built again.
        QTimer::singleShot(150, qApp, [id, on] {
            PluginHost::get().setEnabled(id, on);
            rebuildSettings();
        });
    });
    h->addWidget(enabled, 0, Qt::AlignTop);
    return row;
}

QWidget* pluginsPage() {
    PluginHost& host = PluginHost::get();
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    pageHeader(col, QObject::tr("Plugins"), resetPageButton([] {
                   settings().disabledPlugins.clear();
                   PluginHost::get().load();
               }));
    col->addWidget(hintLabel(QObject::tr("Plugins are written in Lua. They add ready-made objects to the library and commands to the Plugins "
                                         "menu, and change the scene only with the permissions listed on them.")));
    auto* install = accentButton(QObject::tr("Install Plugin..."));
    install->setToolTip(QObject::tr("Choose the plugin.json file in a plugin's folder"));
    QObject::connect(install, &QPushButton::clicked, install, [install] {
        const QString file = QFileDialog::getOpenFileName(install, QObject::tr("Install Plugin (choose its plugin.json)"), QString(),
                                                          QObject::tr("Artisan plugin (plugin.json)"));
        if (file.isEmpty()) return;
        std::string error;
        g_pluginMessage = PluginHost::get().install(platform::fromQt(file), &error)
                              ? QObject::tr("Plugin installed.").toStdString()
                              : QObject::tr("Could not install it: %1").arg(QString::fromStdString(error)).toStdString();
        rebuildSettings();
    });
    auto* folder = new QPushButton(QObject::tr("Open Folder"));
    folder->setToolTip(QObject::tr("Where installed plugins are kept"));
    QObject::connect(folder, &QPushButton::clicked, folder, [] {
        std::error_code ec;
        std::filesystem::create_directories(PluginHost::userDir(), ec);
        platform::openWithDefaultApp(platform::toQt(PluginHost::userDir()));
    });
    auto* reload = new QPushButton(QObject::tr("Reload"));
    reload->setToolTip(QObject::tr("Load every plugin again (after editing one)"));
    QObject::connect(reload, &QPushButton::clicked, reload, [] { reloadPlugins(); });
    auto* docs = new QPushButton(QObject::tr("Write a Plugin"));
    docs->setProperty("flat", true);
    docs->setToolTip(QObject::tr("The plugin guide"));
    QObject::connect(docs, &QPushButton::clicked, docs, [] { platform::openWithDefaultApp(docPath("PLUGINS.md")); });
    auto* bar = new QHBoxLayout;
    bar->setSpacing(8);
    bar->addWidget(install);
    bar->addWidget(folder);
    bar->addWidget(reload);
    bar->addStretch();
    bar->addWidget(docs);
    col->addSpacing(4);
    col->addLayout(bar);
    if (!g_pluginMessage.empty()) col->addWidget(pluginText(QString::fromStdString(g_pluginMessage)));

    // What comes with Artisan first; then what was installed (or how to).
    SettingsCard* builtin = nullptr;
    for (const Plugin& p : host.plugins()) {
        if (!p.builtin) continue;
        if (!builtin) builtin = addSection(col, QObject::tr("Included with Artisan"));
        builtin->addContent(pluginRow(p))->layout()->setContentsMargins(0, 0, 0, 0);
    }
    SettingsCard* installed = addSection(col, QObject::tr("Installed"));
    bool any = false;
    for (const Plugin& p : host.plugins()) {
        if (p.builtin) continue;
        installed->addContent(pluginRow(p))->layout()->setContentsMargins(0, 0, 0, 0);
        any = true;
    }
    if (!any) installed->addContent(pluginText(QObject::tr("None yet. Install Plugin adds one from its folder (its plugin.json file)."), Tone::TextFaint));
    col->addStretch(1);
    return page;
}

// --- help --------------------------------------------------------------------------------------

constexpr const char* kLicense =
    "MIT License\n\n"
    "Copyright (c) 2026 The Artisan contributors\n\n"
    "Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files "
    "(the \"Software\"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, "
    "merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is "
    "furnished to do so, subject to the following conditions:\n\n"
    "The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.\n\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE "
    "WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR "
    "COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, "
    "ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.";

QPushButton* linkButton(const QString& text, const QString& url, Icon icon = Icon::ArrowRight) {
    auto* b = new QPushButton(makeIcon(icon, theme::kTextDim, 16), text);
    b->setCursor(Qt::PointingHandCursor);
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
    card->addRow(QObject::tr("Scenes (top)"), QObject::tr("Each scene is a tab. The + adds one; right-click a tab to rename it, duplicate it or "
                                                         "make it the one the game starts with. The arrows beside them go back and forward "
                                                         "through what you selected."),
                 nullptr);
    card->addRow(QObject::tr("Scene, Nodes, Scripts (under the tabs)"),
                 QObject::tr("The Scene view places objects. Nodes is the selected object's node graph (or the scene's): each object has "
                             "its own. Scripts is the project's Lua files, all of them, whatever is selected."),
                 nullptr);
    card->addRow(QObject::tr("Library (left)"), QObject::tr("Ready-made objects: a player that already moves, shapes, sprites, lights, and "
                                                            "what plugins add. Click one to add it, or drag it into the scene. In the Nodes "
                                                            "view it lists nodes and presets (WASD movement...)."),
                 nullptr);
    card->addRow(QObject::tr("Hierarchy, Files and Inspector (right)"),
                 QObject::tr("The scene's objects in drawing order, the project's files, and the selected object's components."), nullptr);
    card->addRow(QObject::tr("Panels anywhere"), QObject::tr("Drag a panel by its tab to another side, or out of the window. The – on its header "
                                                            "tucks it into a tab on the edge; the square fills the window with it."),
                 nullptr);
    SettingsCard* behavior = addSection(col, QObject::tr("Behavior"));
    behavior->addRow(QObject::tr("Nodes or Lua"), QObject::tr("An object, or the scene itself, can have a node graph and a Lua script. A Lua "
                                                              "Code node puts a few lines of Lua right in a graph."),
                     nullptr);
    behavior->addRow(QObject::tr("Play"), QObject::tr("F5 runs the game in its own window; what it prints shows in the Console."), nullptr);
    col->addStretch(1);
    return page;
}

// Help › Shortcuts: every action's keys as key caps, the mouse's gestures, a search.
QWidget* helpShortcutsPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    auto* edit = new QPushButton(makeIcon(Icon::Keyboard, theme::kTextDim, 16), QObject::tr("Change Shortcuts..."));
    QObject::connect(edit, &QPushButton::clicked, edit, [edit] { openSettings(edit->window()->parentWidget(), SettingsPage::Shortcuts); });
    pageHeader(col, QObject::tr("Keyboard shortcuts"), edit);
    auto* search = new QLineEdit;
    search->setPlaceholderText(QObject::tr("Search shortcuts"));
    search->setClearButtonEnabled(true);
    col->addWidget(search);
    struct Row {
        QWidget* row;
        QWidget* section; // its title and card, hidden when nothing in them matches
        QString text;
    };
    auto rows = std::make_shared<std::vector<Row>>();
    auto addGroup = [&](const QString& title, const std::vector<std::pair<QString, QString>>& items) {
        auto* holder = new QWidget;
        auto* hcol = new QVBoxLayout(holder);
        hcol->setContentsMargins(0, 0, 0, 0);
        hcol->setSpacing(6);
        SettingsCard* card = addSection(hcol, title);
        for (const auto& [action, keys] : items) {
            QWidget* control = keys.isEmpty() ? static_cast<QWidget*>(hintLabel(QObject::tr("None"))) : new KeyCaps(keys);
            QWidget* r = card->addRow(action, {}, control);
            rows->push_back({r, holder, action + " " + keys});
        }
        col->addWidget(holder);
    };
    std::vector<std::pair<QString, QString>> items;
    const char* group = nullptr;
    for (const ActionInfo& a : actionInfos()) {
        if (group && std::strcmp(group, a.group) != 0) {
            addGroup(QObject::tr(group), items);
            items.clear();
        }
        group = a.group;
        items.emplace_back(QObject::tr(a.label), keysOf(a.id).toString(QKeySequence::NativeText));
    }
    if (group) addGroup(QObject::tr(group), items);
    items.clear();
    group = nullptr;
    for (const Gesture& g : gestures()) {
        if (group && std::strcmp(group, g.group) != 0) {
            addGroup(QObject::tr(group), items);
            items.clear();
        }
        group = g.group;
        items.emplace_back(QObject::tr(g.label), QObject::tr(g.keys));
    }
    if (group) addGroup(QObject::tr(group), items);
    QObject::connect(search, &QLineEdit::textChanged, page, [rows](const QString& q) {
        std::set<QWidget*> shown;
        for (const Row& r : *rows) {
            const bool match = q.trimmed().isEmpty() || r.text.contains(q.trimmed(), Qt::CaseInsensitive);
            r.row->setVisible(match);
            if (match) shown.insert(r.section);
        }
        for (const Row& r : *rows) r.section->setVisible(shown.count(r.section) > 0);
    });
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
    card->addRow(QObject::tr("Plugins"), QObject::tr("Add objects and commands to the editor, in Lua"), linkButton(QObject::tr("Open"), docPath("PLUGINS.md")));
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
    SettingsCard* support = addSection(col, QObject::tr("Support"));
    support->addRow(QObject::tr("Free, and made with care"), QObject::tr("If Artisan helps you make games, a coffee keeps it going"),
                    linkButton(QObject::tr("Buy Me a Coffee"), kSupportUrl, Icon::Coffee));
    col->addStretch(1);
    return page;
}

QWidget* creditsPage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    pageHeader(col, QObject::tr("Credits"));
    SettingsCard* built = addSection(col, QObject::tr("Built with"));
    const std::pair<const char*, QString> libs[] = {
        {"Qt", QObject::tr("The editor's windows and controls (%1)").arg(qVersion())},
        {"Qt Advanced Docking System", QObject::tr("Panels that move anywhere")},
        {"SDL 3", QObject::tr("The player's window, input and sound")},
        {"Vulkan and volk", QObject::tr("Drawing, the same in the editor and the game")},
        {"Lua 5.4", QObject::tr("Scripts, node graphs and plugins")},
        {"nlohmann/json", QObject::tr("Projects, settings and themes")},
        {"stb", QObject::tr("Reading pictures")},
        {"Inter", QObject::tr("The interface's typeface, by Rasmus Andersson")},
    };
    for (const auto& [name, what] : libs) built->addRow(QString::fromUtf8(name), what, nullptr);
    col->addSpacing(4);
    col->addWidget(hintLabel(QObject::tr("Thank you to everyone who builds, tests, reports and shares.")));
    col->addStretch(1);
    return page;
}

QWidget* licensePage() {
    QVBoxLayout* col;
    QWidget* page = pageWidget(col);
    pageHeader(col, QObject::tr("License"));
    SettingsCard* summary = addSection(col, QObject::tr("In short"));
    summary->addRow(QObject::tr("Free for anything"), QObject::tr("Personal and commercial games alike, with no price, account or splash screen"), nullptr);
    summary->addRow(QObject::tr("Yours to change and share"), QObject::tr("As long as the notice below travels with it"), nullptr);
    summary->addRow(QObject::tr("Your games are yours"), QObject::tr("Projects and exported games belong entirely to you"), nullptr);
    SettingsCard* full = addSection(col, QObject::tr("MIT License"));
    auto* text = new QPlainTextEdit(QString::fromUtf8(kLicense));
    text->setReadOnly(true);
    text->setFrameShape(QFrame::NoFrame);
    text->setMinimumHeight(300);
    full->addContent(text);
    col->addStretch(1);
    return page;
}

} // namespace

void applyTheme() {
    const Settings& s = settings();
    theme::Palette p = baseTheme(s.theme);
    if (s.customAccent && theme::fromHex(s.accent, p.accent)) p.onAccent = 0; // worked out for the new accent
    QFont f(QString::fromStdString(s.uiFont));
    f.setPointSizeF(9.5 * s.uiScale);
    QApplication::setFont(f);
    theme::apply(p);
}

QWidget* openSettings(QWidget* parent, SettingsPage which) {
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
                                      {QObject::tr("Shortcuts"), Icon::Keyboard, shortcutsPage},
                                      {QObject::tr("Plugins"), Icon::Plugin, pluginsPage}},
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
            resetAllKeys();
            PluginHost::get().load();
            changed(true);
        });
        // The pages show what the plugins and the keys are now.
        QObject::connect(&PluginHost::get(), &PluginHost::changed, g_settings, [] { rebuildSettings(); });
        g_settings->showPage(static_cast<int>(which));
    } else {
        g_settings->showPage(static_cast<int>(which));
    }
    g_settings->present();
    return g_settings;
}

QWidget* openHelp(QWidget* parent, HelpPage which, const QString& gpuName) {
    if (!g_help) {
        g_help = new PagedWindow(QObject::tr("Help"),
                                 {{QObject::tr("Getting started"), Icon::Play, startPage},
                                  {QObject::tr("Shortcuts"), Icon::Keyboard, helpShortcutsPage},
                                  {QObject::tr("Scripting"), Icon::Code, scriptingPage},
                                  {QObject::tr("About"), Icon::Help, [gpuName] { return aboutPage(gpuName); }},
                                  {QObject::tr("Credits"), Icon::Star, creditsPage},
                                  {QObject::tr("License"), Icon::Lock, licensePage}},
                                 nullptr, parent);
        g_help->setAttribute(Qt::WA_DeleteOnClose, false);
        QObject::connect(&ShortcutSignals::get(), &ShortcutSignals::changed, g_help, [] {
            if (g_help && g_help->currentPage() == static_cast<int>(HelpPage::Shortcuts)) g_help->rebuild();
        });
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
