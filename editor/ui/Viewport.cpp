#include "ui/Viewport.h"

#include "app/Platform.h"
#include "app/Settings.h"
#include "artisan/gfx/Renderer2D.h"
#include "artisan/runtime/SceneDraw.h"
#include "ui/EditorContext.h"
#include "ui/Library.h"
#include "ui/Widgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QtGui/qtguiglobal.h>
#if QT_CONFIG(vulkan)
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <QVulkanWindow>
#else
class QVulkanInstance;
#endif

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>

namespace artisan::editor {

using namespace ui;

namespace {

Color fromRgba(Rgba c) { return {bestyle::red(c) / 255.0f, bestyle::green(c) / 255.0f, bestyle::blue(c) / 255.0f, bestyle::alpha(c) / 255.0f}; }

QuadInstance overlay(Vec2 center, Vec2 size, float rotation, QuadShape shape, Color fill, float outline, Color outlineColor) {
    QuadInstance q{};
    q.x = center.x;
    q.y = center.y;
    q.w = size.x;
    q.h = size.y;
    q.rotation = rotation;
    q.shape = static_cast<float>(shape);
    q.outline = outline;
    q.r = fill.r, q.g = fill.g, q.b = fill.b, q.a = fill.a;
    q.or_ = outlineColor.r, q.og = outlineColor.g, q.ob = outlineColor.b, q.oa = outlineColor.a;
    q.u1 = q.v1 = 1.0f;
    return q;
}

} // namespace

#if QT_CONFIG(vulkan)

class SceneRenderer : public QVulkanWindowRenderer {
public:
    SceneRenderer(SceneWindow* w, EditorContext& ctx);
    void initResources() override;
    void releaseResources() override;
    void startNextFrame() override;
    std::string device;

private:
    SceneWindow* w_;
    EditorContext& ctx_;
    Renderer2D r_;
};

class SceneWindow : public QVulkanWindow {
public:
    explicit SceneWindow(EditorContext& ctx) : ctx_(ctx) {}
    QVulkanWindowRenderer* createRenderer() override {
        renderer_ = new SceneRenderer(this, ctx_);
        return renderer_;
    }

    // The editor's own camera (not the game's).
    Vec2 center;
    float zoom = 0.5f;
    bool fitted = false;
    std::function<void()> onViewChanged;

    Vec2 toWorld(QPointF p) const { return center + Vec2(static_cast<float>(p.x() - width() / 2.0), static_cast<float>(p.y() - height() / 2.0)) / zoom; }
    void fitGame() {
        const Project& p = ctx_.project;
        center = p.scene().camera.position;
        const float z = std::min((width() - 80.0f) / p.window.width, (height() - 80.0f) / p.window.height);
        zoom = std::clamp(z * p.scene().camera.zoom, 0.02f, 32.0f);
        fitted = width() > 0;
        if (onViewChanged) onViewChanged();
        requestUpdate();
    }
    std::string deviceName() const { return renderer_ ? renderer_->device : std::string(); }

protected:
    void exposeEvent(QExposeEvent* e) override {
        if (!fitted && isExposed() && width() > 0) fitGame();
        QVulkanWindow::exposeEvent(e);
    }
    void mousePressEvent(QMouseEvent* e) override {
        last_ = e->position();
        if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton || (e->button() == Qt::LeftButton && space_)) {
            panning_ = true;
            setCursor(Qt::ClosedHandCursor);
            return;
        }
        if (e->button() != Qt::LeftButton) return;
        const Vec2 w = toWorld(e->position());
        const EntityId hit = pick(w);
        ctx_.select(hit);
        if (const Entity* sel = ctx_.selected(); sel && hit) {
            dragging_ = true;
            moved_ = false;
            grab_ = w - sel->position;
        }
        requestUpdate();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        const QPointF d = e->position() - last_;
        last_ = e->position();
        if (panning_) {
            center -= Vec2(static_cast<float>(d.x()), static_cast<float>(d.y())) / zoom;
            if (onViewChanged) onViewChanged();
            requestUpdate();
            return;
        }
        if (!dragging_) return;
        Entity* sel = ctx_.selected();
        if (!sel) return;
        if (!moved_) {
            ctx_.checkpoint("move " + std::to_string(sel->id) + " " + std::to_string(++drags_));
            moved_ = true;
        }
        Vec2 p = toWorld(e->position()) - grab_;
        if (settings().snapToGrid != static_cast<bool>(e->modifiers() & Qt::ShiftModifier)) { // Shift: the other way
            const float g = static_cast<float>(settings().gridSize);
            p = {std::round(p.x / g) * g, std::round(p.y / g) * g};
        }
        sel->position = p;
        ctx_.changed();
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        panning_ = dragging_ = false;
        unsetCursor();
    }
    void wheelEvent(QWheelEvent* e) override {
        const Vec2 before = toWorld(e->position());
        const float steps = static_cast<float>(e->angleDelta().y()) / 120.0f;
        zoom = std::clamp(zoom * std::pow(1.15f, steps), 0.02f, 32.0f);
        center += before - toWorld(e->position()); // the point under the cursor stays there
        if (onViewChanged) onViewChanged();
        requestUpdate();
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) space_ = true;
        else if (e->key() == Qt::Key_F) fitGame();
        else QVulkanWindow::keyPressEvent(e);
    }
    void keyReleaseEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) space_ = false;
        QVulkanWindow::keyReleaseEvent(e);
    }

private:
    EntityId pick(Vec2 w) const {
        // The topmost object under the point: the highest layer, then the latest in the list.
        const Scene& s = ctx_.scene();
        const Entity* best = nullptr;
        int bestLayer = INT32_MIN;
        for (const Entity& e : s.entities) {
            const Vec2 local = (w - e.position).rotated(-radians(e.rotation));
            Vec2 half = entitySize(e) * 0.5f;
            if (!e.shape && !e.sprite) half = Vec2(12, 12) / zoom; // lights and empty objects: their icon
            if (std::fabs(local.x) <= half.x && std::fabs(local.y) <= half.y && e.layer >= bestLayer) {
                best = &e; // later entities win within a layer
                bestLayer = e.layer;
            }
        }
        return best ? best->id : 0;
    }

    EditorContext& ctx_;
    SceneRenderer* renderer_ = nullptr;
    QPointF last_;
    Vec2 grab_;
    bool panning_ = false, dragging_ = false, moved_ = false, space_ = false;
    int drags_ = 0;
};

SceneRenderer::SceneRenderer(SceneWindow* w, EditorContext& ctx) : w_(w), ctx_(ctx) {}

void SceneRenderer::initResources() {
    static bool loader = volkInitialize() == VK_SUCCESS;
    if (!loader) return;
    volkLoadInstance(w_->vulkanInstance()->vkInstance());
    Renderer2D::Setup s;
    s.physicalDevice = w_->physicalDevice();
    s.device = w_->device();
    s.queue = w_->graphicsQueue();
    s.queueFamily = w_->graphicsQueueFamilyIndex();
    s.renderPass = w_->defaultRenderPass();
    s.samples = w_->sampleCountFlagBits();
    s.framesInFlight = static_cast<uint32_t>(w_->concurrentFrameCount());
    s.pixelArt = ctx_.project.window.pixelArt;
    std::string error;
    if (!r_.init(s, &error)) ctx_.log(QObject::tr("Viewport: cannot draw with Vulkan (%1)").arg(QString::fromStdString(error)));
    device = w_->physicalDeviceProperties()->deviceName;
}

void SceneRenderer::releaseResources() { r_.shutdown(); }

void SceneRenderer::startNextFrame() {
    VkCommandBuffer cmd = w_->currentCommandBuffer();
    const QSize size = w_->swapChainImageSize();
    const Scene& scene = ctx_.scene();
    VkClearValue clear[3]{};
    const Color bg = scene.background;
    clear[0].color = {{bg.r, bg.g, bg.b, 1.0f}};
    clear[1].depthStencil = {1.0f, 0};
    clear[2].color = clear[0].color;
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = w_->defaultRenderPass();
    rp.framebuffer = w_->currentFramebuffer();
    rp.renderArea.extent = {static_cast<uint32_t>(size.width()), static_cast<uint32_t>(size.height())};
    rp.clearValueCount = w_->sampleCountFlagBits() > VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
    rp.pClearValues = clear;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    if (r_.ready()) {
        const float dpr = static_cast<float>(w_->devicePixelRatio());
        const View view{w_->center, w_->zoom * dpr, {static_cast<float>(size.width()), static_cast<float>(size.height())}};
        const auto dir = ctx_.dir;
        r_.begin(static_cast<uint32_t>(w_->currentFrame()));
        drawScene(r_, scene, view, [dir](const std::string& path, std::vector<uint8_t>& out) {
            std::ifstream in(dir / platform::fromUtf8(path), std::ios::binary);
            if (!in) return false;
            out.assign(std::istreambuf_iterator<char>(in), {});
            return true;
        });

        constexpr int kOverlay = INT32_MAX - 8;
        const float px = 1.0f / view.zoom; // one screen pixel in world units
        const Color accent = fromRgba(theme::kAccent);
        if (settings().showGrid) {
            const Color ink = fromRgba(theme::ink(18)), ink2 = fromRgba(theme::ink(40));
            QuadInstance g = overlay(view.center, view.viewport / view.zoom, 0, QuadShape::Grid, ink, 0, ink2);
            g.corner = static_cast<float>(settings().gridSize);
            // Lines too dense to see are dropped: every 8th grows into the minor step.
            while (g.corner * view.zoom < 6.0f) g.corner *= 8.0f;
            r_.draw(g, kOverlay - 2);
        }
        // The game's frame: what the camera shows at the design resolution.
        const Project& p = ctx_.project;
        const Vec2 frame = Vec2(static_cast<float>(p.window.width), static_cast<float>(p.window.height)) / std::max(scene.camera.zoom, 0.01f);
        r_.draw(overlay(scene.camera.position, frame, 0, QuadShape::Rectangle, {0, 0, 0, 0}, 1.5f * px, fromRgba(theme::ink(110))), kOverlay - 1);

        for (const Entity& e : scene.entities) {
            const bool sel = e.id == ctx_.selection();
            if (e.light) {
                const Color lc{e.light->color.r, e.light->color.g, e.light->color.b, 0.9f};
                r_.draw(overlay(e.position, Vec2(18, 18) * px, 0, QuadShape::Circle, lc, 2.0f * px, fromRgba(theme::ink(200))), kOverlay);
                if (sel) r_.draw(overlay(e.position, Vec2(e.light->radius, e.light->radius) * 2.0f, 0, QuadShape::Circle, {0, 0, 0, 0}, 1.0f * px, accent), kOverlay);
            } else if (!e.shape && !e.sprite) {
                r_.draw(overlay(e.position, Vec2(16, 16) * px, radians(45), QuadShape::Rectangle, {0, 0, 0, 0}, 2.0f * px, fromRgba(theme::ink(160))), kOverlay);
            }
            if (sel && (e.shape || e.sprite)) {
                const Vec2 s = entitySize(e) + Vec2(6, 6) * px;
                r_.draw(overlay(e.position, s, radians(e.rotation), QuadShape::Rectangle, {0, 0, 0, 0}, 1.5f * px, accent), kOverlay + 1);
                r_.draw(overlay(e.position, Vec2(7, 7) * px, 0, QuadShape::Circle, accent, 0, {}), kOverlay + 1);
            }
        }
        r_.record(cmd, rp.renderArea.extent);
    }
    vkCmdEndRenderPass(cmd);
    w_->frameReady();
}

#else
class SceneWindow {}; // Qt without Vulkan: the view says so
#endif

Viewport::Viewport(EditorContext& ctx, QVulkanInstance* vulkan, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    auto* bar = new QWidget;
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(4);
    auto* grid = new IconButton(Icon::Table, tr("Grid"), 26);
    grid->setCheckable(true);
    grid->setActive(settings().showGrid);
    auto* snap = new IconButton(Icon::Magnet, tr("Snap to grid (Shift while dragging: the other way)"), 26);
    snap->setActive(settings().snapToGrid);
    auto* fitButton = new IconButton(Icon::FitView, tr("Fit the game's frame (F)"), 26);
    zoom_ = label("", Tone::TextDim);
    row->addWidget(grid);
    row->addWidget(snap);
    row->addStretch(1);
    row->addWidget(zoom_);
    row->addWidget(fitButton);
    col->addWidget(bar);

#if QT_CONFIG(vulkan)
    const bool usable = vulkan && vulkan->isValid();
#else
    const bool usable = false;
    (void)vulkan;
#endif
    if (!usable) {
        QLabel* none = label(tr("Vulkan is not available on this machine: the scene view needs it.\nUpdate the graphics driver."), Tone::TextDim);
        none->setAlignment(Qt::AlignCenter);
        col->addWidget(none, 1);
        return;
    }
#if QT_CONFIG(vulkan)
    window_ = new SceneWindow(ctx_);
    window_->setVulkanInstance(vulkan);
    window_->onViewChanged = [this] { updateZoomLabel(); };
    QWidget* container = QWidget::createWindowContainer(window_, this);
    container->setFocusPolicy(Qt::StrongFocus);
    // Objects dragged from the library land where they are dropped. The native view is no drop
    // site of its own: the drag comes to its container.
    container->setAcceptDrops(true);
    container->installEventFilter(this);
    container_ = container;
    col->addWidget(container, 1);

    connect(grid, &QAbstractButton::clicked, this, [this, grid] {
        settings().showGrid = !settings().showGrid;
        grid->setActive(settings().showGrid);
        saveSettings();
        refresh();
    });
    connect(snap, &QAbstractButton::clicked, this, [snap] {
        settings().snapToGrid = !settings().snapToGrid;
        snap->setActive(settings().snapToGrid);
        saveSettings();
    });
    connect(fitButton, &QAbstractButton::clicked, this, [this] { fit(); });
    connect(&ctx_, &EditorContext::sceneChanged, this, [this] { refresh(); });
    connect(&ctx_, &EditorContext::selectionChanged, this, [this] { refresh(); });
    connect(&ctx_, &EditorContext::opened, this, [this] {
        window_->fitted = false;
        shownScene_ = ctx_.sceneIndex();
        if (window_->isExposed()) window_->fitGame();
    });
    connect(&ctx_, &EditorContext::scenesChanged, this, [this] {
        if (ctx_.sceneIndex() == shownScene_) return refresh();
        shownScene_ = ctx_.sceneIndex(); // another scene: its game frame, whole
        window_->fitted = false;
        if (window_->isExposed()) window_->fitGame();
    });
    connect(&Appearance::get(), &Appearance::changed, this, [this] { refresh(); });
#endif
}

#if QT_CONFIG(vulkan)
bool Viewport::eventFilter(QObject* watched, QEvent* e) {
    if (watched != container_ || !window_) return QWidget::eventFilter(watched, e);
    switch (e->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto* d = static_cast<QDropEvent*>(e);
        if (!d->mimeData()->hasFormat(kObjectMime)) return false;
        d->acceptProposedAction();
        return true;
    }
    case QEvent::Drop: {
        auto* d = static_cast<QDropEvent*>(e);
        if (!d->mimeData()->hasFormat(kObjectMime)) return false;
        const Vec2 at = window_->toWorld(d->position());
        d->acceptProposedAction();
        addObject(d->mimeData()->data(kObjectMime).toStdString(), ctx_, this, &at);
        return true;
    }
    default: return QWidget::eventFilter(watched, e);
    }
}

void Viewport::fit() {
    if (window_) window_->fitGame();
}

void Viewport::refresh() {
    if (window_) window_->requestUpdate();
}

void Viewport::updateZoomLabel() {
    if (window_) zoom_->setText(QString("%1%").arg(std::lround(window_->zoom * 100)));
}

QString Viewport::deviceName() const { return window_ ? QString::fromStdString(window_->deviceName()) : QString(); }
#else
bool Viewport::eventFilter(QObject* watched, QEvent* e) { return QWidget::eventFilter(watched, e); }
void Viewport::fit() {}
void Viewport::refresh() {}
void Viewport::updateZoomLabel() {}
QString Viewport::deviceName() const { return {}; }
#endif

} // namespace artisan::editor
