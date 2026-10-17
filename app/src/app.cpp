#include "app.h"

#include "asset_io.h"
#include "renderer.h"
#include "scene.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

// stb_image_write produces the --screenshot PNG; the implementation lives in
// this executable-only translation unit (asset_io.cpp owns stb_image's). The
// diagnostic push silences a deprecated-sprintf warning from the vendored
// header's TGA writer, which nothing here calls.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma clang diagnostic pop

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

// ---------------------------------------------------------------------------
// Provisional fixed camera (task 3 only).
//
// The viewer's orbit camera — with camera_view/camera_proj and the shared
// mat4_* helpers in math3d.h — is task 4's deliverable. Until then a screenshot
// still has to show the model, so this computes a fixed view-projection from
// the scene bounds: look at the bounds centre from 2.5 x the bounds radius,
// 60 degrees vertical FOV, aspect from the drawable size. Delete it when
// camera_view/camera_proj arrive.
// ---------------------------------------------------------------------------

Mat4 mat4_product(const Mat4& a, const Mat4& b) {
    // Column-major: out[col * 4 + row] = sum_k a.m[k * 4 + row] * b.m[col * 4 + k].
    Mat4 out{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            out.m[col * 4 + row] = sum;
        }
    }
    return out;
}

Mat4 provisional_view_proj(const Scene& scene, float aspect) {
    const Vec3 center{(scene.boundsMin.x + scene.boundsMax.x) * 0.5f,
                      (scene.boundsMin.y + scene.boundsMax.y) * 0.5f,
                      (scene.boundsMin.z + scene.boundsMax.z) * 0.5f};
    const Vec3 diagonal{scene.boundsMax.x - scene.boundsMin.x,
                        scene.boundsMax.y - scene.boundsMin.y,
                        scene.boundsMax.z - scene.boundsMin.z};
    float radius = 0.5f * std::sqrt(vec3_dot(diagonal, diagonal));
    if (!(radius > 0.0f)) {
        radius = 1e-3f;  // empty scene: keep the matrix finite
    }
    if (!(aspect > 0.0f)) {
        aspect = 1.0f;
    }

    const float distance = 2.5f * radius;
    const Vec3 direction = vec3_normalize(Vec3{0.55f, 0.45f, 1.0f});
    const Vec3 eye{center.x + direction.x * distance, center.y + direction.y * distance,
                   center.z + direction.z * distance};

    // Right-handed look-at: the camera looks down its own -Z.
    const Vec3 forward = vec3_normalize(
        Vec3{center.x - eye.x, center.y - eye.y, center.z - eye.z});
    const Vec3 right = vec3_normalize(vec3_cross(forward, Vec3{0.0f, 1.0f, 0.0f}));
    const Vec3 up = vec3_cross(right, forward);

    Mat4 view{};
    view.m[0] = right.x;
    view.m[1] = up.x;
    view.m[2] = -forward.x;
    view.m[4] = right.y;
    view.m[5] = up.y;
    view.m[6] = -forward.y;
    view.m[8] = right.z;
    view.m[9] = up.z;
    view.m[10] = -forward.z;
    view.m[12] = -vec3_dot(right, eye);
    view.m[13] = -vec3_dot(up, eye);
    view.m[14] = vec3_dot(forward, eye);
    view.m[15] = 1.0f;

    // Right-handed perspective with Metal's [0, 1] depth range.
    const float near_plane = std::max(0.05f * radius, 1e-3f);
    const float far_plane = distance + 4.0f * radius;
    const float focal = 1.0f / std::tan(0.5f * 60.0f * (3.14159265358979f / 180.0f));
    Mat4 proj{};
    proj.m[0] = focal / aspect;
    proj.m[5] = focal;
    proj.m[10] = far_plane / (near_plane - far_plane);
    proj.m[11] = -1.0f;
    proj.m[14] = near_plane * far_plane / (near_plane - far_plane);

    return mat4_product(proj, view);
}

// Names the swapchain format so a run records which gamma path applies (SDR
// composition yields B8G8R8A8_UNORM on macOS; an _SRGB variant would mean the
// shader must stop encoding its output).
const char* texture_format_name(SDL_GPUTextureFormat format) {
    switch (format) {
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_INVALID: return "INVALID";
        default: return "other";
    }
}

// Per-channel min/max/mean of an RGBA8 frame's RGB bytes, so non-uniformity
// (i.e. "something was drawn") is checkable from the terminal.
struct PixelStats {
    int min_rgb[3];
    int max_rgb[3];
    double mean_rgb[3];
};

PixelStats pixel_stats(const std::vector<uint8_t>& rgba8) {
    PixelStats stats{{255, 255, 255}, {0, 0, 0}, {0.0, 0.0, 0.0}};
    double sums[3] = {0.0, 0.0, 0.0};
    const size_t pixel_count = rgba8.size() / 4;
    for (size_t i = 0; i < pixel_count; ++i) {
        for (int channel = 0; channel < 3; ++channel) {
            const uint8_t value = rgba8[i * 4 + static_cast<size_t>(channel)];
            stats.min_rgb[channel] = std::min(stats.min_rgb[channel], static_cast<int>(value));
            stats.max_rgb[channel] = std::max(stats.max_rgb[channel], static_cast<int>(value));
            sums[channel] += value;
        }
    }
    for (int channel = 0; channel < 3; ++channel) {
        stats.mean_rgb[channel] = pixel_count > 0 ? sums[channel] / static_cast<double>(pixel_count) : 0.0;
    }
    return stats;
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
    if (!opts.screenshotPath.empty() && opts.modelRelPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --screenshot needs --model: there is nothing to capture\n");
        return 2;
    }

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

    try {
        // Constructing the renderer compiles the MSL shader (loudly failing
        // when mesh.metal is missing or does not compile).
        Renderer renderer(shell.device, shell.win);
        std::printf("s1-gpu format=%s\n",
                    texture_format_name(SDL_GetGPUSwapchainTextureFormat(shell.device, shell.win)));

        Scene scene;
        if (!opts.modelRelPath.empty()) {
            scene = load_scene(opts.libraryDir, opts.modelRelPath);
            renderer.upload(scene);
            size_t vertices = 0;
            for (const MeshCpu& mesh : scene.meshes) {
                vertices += mesh.vertices.size();
            }
            std::printf("s1-scene meshes=%zu vertices=%zu materials=%zu textures=%zu\n",
                        scene.meshes.size(), vertices, scene.materials.size(),
                        scene.textures.size());
        }

        int pixel_width = 0;
        int pixel_height = 0;
        if (!SDL_GetWindowSizeInPixels(shell.win, &pixel_width, &pixel_height)) {
            std::fprintf(stderr, "sotc-viewer: SDL_GetWindowSizeInPixels: %s\n", SDL_GetError());
            return 1;
        }
        const Mat4 view_proj = provisional_view_proj(
            scene, static_cast<float>(pixel_width) / static_cast<float>(std::max(pixel_height, 1)));

        if (opts.selftest) {
            renderer.render_frame(scene, view_proj, /*acquire_swapchain=*/true);
            print_status_line(opts, shell.win);
            return 0;
        }

        if (!opts.screenshotPath.empty()) {
            for (int frame = 0; frame < opts.frames; ++frame) {
                renderer.render_frame(scene, view_proj, /*acquire_swapchain=*/true);
            }

            const std::vector<uint8_t> pixels = renderer.read_pixels();
            if (!SDL_GetWindowSizeInPixels(shell.win, &pixel_width, &pixel_height)) {
                std::fprintf(stderr, "sotc-viewer: SDL_GetWindowSizeInPixels: %s\n", SDL_GetError());
                return 1;
            }
            const size_t expected = static_cast<size_t>(pixel_width) * static_cast<size_t>(pixel_height) * 4;
            if (pixels.size() != expected) {
                std::fprintf(stderr, "sotc-viewer: read %zu pixels, expected %zu (%dx%d)\n",
                             pixels.size(), expected, pixel_width, pixel_height);
                return 1;
            }

            const PixelStats stats = pixel_stats(pixels);
            std::printf("s1-pixels min=%d,%d,%d max=%d,%d,%d mean=%.1f,%.1f,%.1f\n",
                        stats.min_rgb[0], stats.min_rgb[1], stats.min_rgb[2],
                        stats.max_rgb[0], stats.max_rgb[1], stats.max_rgb[2],
                        stats.mean_rgb[0], stats.mean_rgb[1], stats.mean_rgb[2]);

            if (stbi_write_png(opts.screenshotPath.c_str(), pixel_width, pixel_height, 4,
                               pixels.data(), pixel_width * 4) == 0) {
                std::fprintf(stderr, "sotc-viewer: cannot write %s\n", opts.screenshotPath.c_str());
                return 1;
            }
            std::printf("s1-screenshot path=%s width=%d height=%d\n", opts.screenshotPath.c_str(),
                        pixel_width, pixel_height);
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
            renderer.render_frame(scene, view_proj, /*acquire_swapchain=*/true);
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "sotc-viewer: %s\n", error.what());
        return 1;
    }
}
