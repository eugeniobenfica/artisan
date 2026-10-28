// artisan-player — runs an Artisan game: SDL3 for the window, input and files, Vulkan to draw.
//
//   artisan-player [project folder]   the folder with project.artisan; default: "game" next to
//                                     the program (an exported game), or the APK's assets
//   --preview                         started by the editor: Esc closes the window
//   --frames N                        quit after N frames (smoke tests)
//   --validation                      the Vulkan validation layer, when installed

#include "artisan/gfx/Renderer2D.h"
#include "artisan/gfx/VulkanContext.h"
#include "artisan/runtime/Game.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace artisan;

namespace {

Key keyOf(SDL_Scancode sc) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return static_cast<Key>(static_cast<int>(Key::A) + (sc - SDL_SCANCODE_A));
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return static_cast<Key>(static_cast<int>(Key::Num1) + (sc - SDL_SCANCODE_1));
    switch (sc) {
    case SDL_SCANCODE_0: return Key::Num0;
    case SDL_SCANCODE_LEFT: return Key::Left;
    case SDL_SCANCODE_RIGHT: return Key::Right;
    case SDL_SCANCODE_UP: return Key::Up;
    case SDL_SCANCODE_DOWN: return Key::Down;
    case SDL_SCANCODE_SPACE: return Key::Space;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return Key::Enter;
    case SDL_SCANCODE_ESCAPE: return Key::Escape;
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT: return Key::Shift;
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL: return Key::Ctrl;
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT: return Key::Alt;
    case SDL_SCANCODE_TAB: return Key::Tab;
    case SDL_SCANCODE_BACKSPACE: return Key::Backspace;
    default: return Key::Unknown;
    }
}

Key mouseKey(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT: return Key::MouseLeft;
    case SDL_BUTTON_RIGHT: return Key::MouseRight;
    case SDL_BUTTON_MIDDLE: return Key::MouseMiddle;
    default: return Key::Unknown;
    }
}

void showError(const std::string& message) {
    SDL_Log("%s", message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Artisan", message.c_str(), nullptr);
}

} // namespace

int main(int argc, char** argv) {
    std::string root;
    bool preview = false, validation = false;
    long maxFrames = -1;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--preview")) preview = true;
        else if (!std::strcmp(argv[i], "--validation")) validation = true;
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::strtol(argv[++i], nullptr, 10);
        else root = argv[i]; // SDL3 hands main UTF-8 arguments on every platform
    }
    if (root.empty()) {
#ifdef SDL_PLATFORM_ANDROID
        root = "game"; // inside the APK's assets
#else
        root = std::string(SDL_GetBasePath()) + "game";
#endif
    }
    if (root.size() > 15 && root.ends_with("project.artisan")) root.resize(root.size() - 16);

    // Log lines to stdout too: the editor shows a preview's output in its console.
    SDL_SetLogOutputFunction([](void*, int, SDL_LogPriority, const char* message) {
        std::fprintf(stdout, "%s\n", message);
        std::fflush(stdout);
    }, nullptr);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        showError(std::string("Cannot start: ") + SDL_GetError());
        return 1;
    }

    Game game;
    game.onLog = [](const std::string& line) { SDL_Log("%s", line.c_str()); };
    auto read = [root](const std::string& path, std::vector<uint8_t>& out) {
        size_t size = 0;
        void* data = SDL_LoadFile((root + "/" + path).c_str(), &size);
        if (!data) return false;
        out.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        SDL_free(data);
        return true;
    };
    std::string error;
    if (!game.load(read, &error)) {
        showError("Cannot open the game in \"" + root + "\": " + error);
        SDL_Quit();
        return 1;
    }

    const WindowSettings& ws = game.project.window;
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (ws.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    SDL_Window* window = SDL_CreateWindow(game.project.name.c_str(), ws.width, ws.height, flags);
    if (!window) {
        showError(std::string("Cannot open a window: ") + SDL_GetError());
        SDL_Quit();
        return 1;
    }

    VulkanContext vk;
    VulkanContext::Options opt;
    Uint32 extCount = 0;
    const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&extCount);
    opt.instanceExtensions.assign(exts, exts + extCount);
    opt.createSurface = [window](VkInstance instance) {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        return SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface) ? surface : VK_NULL_HANDLE;
    };
    opt.loader = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    opt.vsync = ws.vsync;
    opt.validation = validation;
    opt.appName = game.project.name.c_str();
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    Renderer2D renderer;
    Renderer2D::Setup setup;
    if (vk.init(opt, static_cast<uint32_t>(pw), static_cast<uint32_t>(ph), &error)) {
        setup.physicalDevice = vk.physicalDevice();
        setup.device = vk.device();
        setup.queue = vk.queue();
        setup.queueFamily = vk.queueFamily();
        setup.renderPass = vk.renderPass();
        setup.framesInFlight = VulkanContext::kFramesInFlight;
        setup.pixelArt = ws.pixelArt;
    }
    if (!vk.device() || !renderer.init(setup, &error)) {
        showError("Cannot draw with Vulkan: " + error + ". Update the graphics driver.");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_Log("Artisan %s on %s", ARTISAN_VERSION, vk.deviceName().c_str());

    game.start();
    Uint64 last = SDL_GetTicksNS();
    long frames = 0;
    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                vk.resize(static_cast<uint32_t>(ev.window.data1), static_cast<uint32_t>(ev.window.data2));
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST: game.input.releaseAll(); break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_ESCAPE && preview) running = false;
                if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_F11)
                    SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
                game.input.set(keyOf(ev.key.scancode), ev.type == SDL_EVENT_KEY_DOWN);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: game.input.set(mouseKey(ev.button.button), ev.button.down); break;
            case SDL_EVENT_MOUSE_MOTION: {
                const float density = SDL_GetWindowPixelDensity(window);
                game.input.mouse = {ev.motion.x * density, ev.motion.y * density};
                break;
            }
            default: break;
            }
        }

        const Uint64 now = SDL_GetTicksNS();
        const float dt = std::min(static_cast<float>(now - last) * 1e-9f, 0.1f); // a stall is not a jump
        last = now;
        game.update(dt);

        const Color& bg = game.scene.background;
        const float clear[4] = {bg.r, bg.g, bg.b, 1.0f};
        if (VkCommandBuffer cmd = vk.begin(clear)) {
            const VkExtent2D ext = vk.extent();
            renderer.begin(vk.frameIndex());
            game.draw(renderer, {static_cast<float>(ext.width), static_cast<float>(ext.height)});
            renderer.record(cmd, ext);
            vk.end();
        } else {
            SDL_Delay(16); // minimized
        }
        if (maxFrames >= 0 && ++frames >= maxFrames) running = false;
    }

    vkDeviceWaitIdle(vk.device());
    renderer.shutdown();
    vk.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
