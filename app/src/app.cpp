#include "app.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>

namespace {

// RAII owner of the SDL shell: window, GPU device, and SDL itself.
struct Shell {
    SDL_Window* win = nullptr;
    SDL_GPUDevice* device = nullptr;
    bool window_claimed = false;

    ~Shell() {
        if (device != nullptr) {
            if (window_claimed) {
                SDL_ReleaseWindowFromGPUDevice(device, win);
            }
            SDL_DestroyGPUDevice(device);
        }
        if (win != nullptr) {
            SDL_DestroyWindow(win);
        }
        SDL_Quit();
    }
};

// Presents one frame: acquire the swapchain, submit, present. Nothing is drawn
// yet — the renderer arrives with Tasks 3–4 at which point this becomes the
// frame's render pass. Returns false only on a hard error; a frame with no
// swapchain available (frames still in flight) is skipped, not an error.
bool present_one_frame(SDL_GPUDevice* device, SDL_Window* win) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (cmd == nullptr) {
        std::fprintf(stderr, "sotc-viewer: SDL_AcquireGPUCommandBuffer: %s\n", SDL_GetError());
        return false;
    }

    SDL_GPUTexture* swapchain = nullptr;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, win, &swapchain, nullptr, nullptr)) {
        std::fprintf(stderr, "sotc-viewer: SDL_AcquireGPUSwapchainTexture: %s\n", SDL_GetError());
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }
    if (swapchain == nullptr) {
        // Too many frames in flight: not an error, skip this frame.
        SDL_CancelGPUCommandBuffer(cmd);
        return true;
    }

    if (!SDL_SubmitGPUCommandBuffer(cmd)) {
        std::fprintf(stderr, "sotc-viewer: SDL_SubmitGPUCommandBuffer: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

// The `s1-status` line reports *queried* state, never assumed flags:
//   windowed          — 1 when the window carries no SDL_WINDOW_FULLSCREEN flag;
//   relative_mouse    — SDL's own answer for the window (0 = off);
//   mouse_auto_capture— the value of SDL_HINT_MOUSE_AUTO_CAPTURE (0 = off);
//   frames            — the run's configured frame budget (AppOptions::frames).
void print_status_line(const AppOptions& opts, SDL_Window* win) {
    const bool windowed = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) == 0;
    const bool relative_mouse = SDL_GetWindowRelativeMouseMode(win);
    const char* auto_capture = SDL_GetHint(SDL_HINT_MOUSE_AUTO_CAPTURE);
    const bool auto_capture_off = auto_capture != nullptr && std::strcmp(auto_capture, "0") == 0;

    std::printf("s1-status windowed=%d relative_mouse=%d mouse_auto_capture=%d frames=%d\n",
                windowed ? 1 : 0, relative_mouse ? 1 : 0, auto_capture_off ? 0 : 1, opts.frames);
}

}  // namespace

int run_app(const AppOptions& opts) {
    if (opts.libraryDir.empty()) {
        std::fprintf(stderr, "sotc-viewer: no asset library: pass --library DIR or set SOTC_LIB\n");
        return 2;
    }
    // opts.modelRelPath and opts.screenshotPath are consumed by the loader and
    // renderer tasks; this shell only opens, presents, and quits.

    // Cursor policy, part 1: SDL must never auto-capture the mouse when the
    // window gains focus. Set the hint before SDL_Init so the video subsystem
    // starts with it in place.
    if (!SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0")) {
        std::fprintf(stderr, "sotc-viewer: could not set %s=0\n", SDL_HINT_MOUSE_AUTO_CAPTURE);
        return 1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "sotc-viewer: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    Shell shell;

    shell.win = SDL_CreateWindow("sotc-viewer", opts.width, opts.height,
                                 SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (shell.win == nullptr) {
        std::fprintf(stderr, "sotc-viewer: SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }

    // Cursor policy, part 2: the cursor is never captured — state both
    // guarantees explicitly, and fail if the platform refuses to honor them.
    if (!SDL_SetWindowRelativeMouseMode(shell.win, false) ||
        !SDL_SetWindowMouseGrab(shell.win, false)) {
        std::fprintf(stderr, "sotc-viewer: cursor policy: %s\n", SDL_GetError());
        return 1;
    }

    // MSL sources are compiled by SDL at runtime: the build machine has no
    // Xcode Metal toolchain, so no metallib is produced here (plan ruling R2).
    shell.device =
        SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, /*debug=*/false, /*name=*/nullptr);
    if (shell.device == nullptr) {
        std::fprintf(stderr, "sotc-viewer: SDL_CreateGPUDevice: %s\n", SDL_GetError());
        return 1;
    }

    if (!SDL_ClaimWindowForGPUDevice(shell.device, shell.win)) {
        std::fprintf(stderr, "sotc-viewer: SDL_ClaimWindowForGPUDevice: %s\n", SDL_GetError());
        return 1;
    }
    shell.window_claimed = true;

    if (!SDL_SetGPUSwapchainParameters(shell.device, shell.win,
                                       SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC)) {
        std::fprintf(stderr, "sotc-viewer: SDL_SetGPUSwapchainParameters: %s\n", SDL_GetError());
        return 1;
    }

    if (opts.selftest) {
        if (!present_one_frame(shell.device, shell.win)) {
            return 1;
        }
        print_status_line(opts, shell.win);
        return 0;
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }
        if (!present_one_frame(shell.device, shell.win)) {
            return 1;
        }
    }
    return 0;
}
