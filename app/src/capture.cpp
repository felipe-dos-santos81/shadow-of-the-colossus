#include "capture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

// stb_image_write produces the --screenshot PNG; the implementation lives in
// this executable-only translation unit (asset_io.cpp owns stb_image's). The
// diagnostic push silences a deprecated-sprintf warning from the vendored
// header's TGA writer, which nothing here calls.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma clang diagnostic pop

Mat4 camera_view_proj(const Camera& camera, SDL_Window* win) {
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(win, &width, &height)) {
        const char* error = SDL_GetError();
        // No "sotc-viewer: " prefix here: run_app's catch adds it once.
        throw std::runtime_error(std::string("SDL_GetWindowSizeInPixels: ") +
                                 (error != nullptr ? error : "unknown error"));
    }
    const float aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
    return mat4_mul(camera_proj(camera, aspect), camera_view(camera));
}

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

void print_status_line(const AppOptions& opts, SDL_Window* win) {
    const bool windowed = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) == 0;
    const bool relative_mouse = SDL_GetWindowRelativeMouseMode(win);
    const char* auto_capture = SDL_GetHint(SDL_HINT_MOUSE_AUTO_CAPTURE);
    const bool auto_capture_off = auto_capture != nullptr && std::strcmp(auto_capture, "0") == 0;

    std::printf("s1-status windowed=%d relative_mouse=%d mouse_auto_capture=%d frames=%d\n",
                windowed ? 1 : 0, relative_mouse ? 1 : 0, auto_capture_off ? 0 : 1, opts.frames);
}

void project_box_point(const Mat4& viewProj, int width, int height, const Vec3& world,
                       ScreenBox& box) {
    const Mat4& m = viewProj;
    const float clip_x = m.m[0] * world.x + m.m[4] * world.y + m.m[8] * world.z + m.m[12];
    const float clip_y = m.m[1] * world.x + m.m[5] * world.y + m.m[9] * world.z + m.m[13];
    const float clip_w = m.m[3] * world.x + m.m[7] * world.y + m.m[11] * world.z + m.m[15];
    if (!(clip_w > 1e-6f) || !std::isfinite(clip_x) || !std::isfinite(clip_y) ||
        !std::isfinite(clip_w)) {
        return;
    }
    const float ndc_x = clip_x / clip_w;
    const float ndc_y = clip_y / clip_w;
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y)) {
        return;
    }
    int x = static_cast<int>((ndc_x * 0.5f + 0.5f) * static_cast<float>(width));
    int y = static_cast<int>((0.5f - ndc_y * 0.5f) * static_cast<float>(height));
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    if (!box.any) {
        box.x0 = box.x1 = x;
        box.y0 = box.y1 = y;
        box.any = true;
        return;
    }
    box.x0 = std::min(box.x0, x);
    box.y0 = std::min(box.y0, y);
    box.x1 = std::max(box.x1, x);
    box.y1 = std::max(box.y1, y);
}

ScreenBox project_rig_bounds(const Scene& scene, std::size_t rigIndex,
                             const std::vector<Mat4>& palette, const Mat4& viewProj, int width,
                             int height) {
    ScreenBox box;
    if (width <= 0 || height <= 0) {
        return box;
    }
    const RigCpu& rig = rig_at(scene, rigIndex);
    const std::size_t end = std::min(rig.meshEnd, scene.meshes.size());
    for (std::size_t i = std::min(rig.meshBegin, end); i < end; ++i) {
        for (const Vertex& vertex : scene.meshes[i].vertices) {
            Vec3 world{};
            if (!posed_vertex_world(palette, scene.meshes[i], vertex, world)) {
                continue;
            }
            project_box_point(viewProj, width, height, world, box);
        }
    }
    return box;
}

ScreenBox project_club_bounds(const Scene& scene, const ClubAttachment& club,
                              const Mat4& model, const Mat4& viewProj, int width, int height) {
    ScreenBox box;
    if (!club.loaded || width <= 0 || height <= 0) {
        return box;
    }
    const std::size_t end = std::min(club.meshEnd, scene.meshes.size());
    for (std::size_t i = std::min(club.meshBegin, end); i < end; ++i) {
        for (const Vertex& vertex : scene.meshes[i].vertices) {
            const Vec3 bind{vertex.px, vertex.py, vertex.pz};
            const Vec3 world{
                model.m[0] * bind.x + model.m[4] * bind.y + model.m[8] * bind.z + model.m[12],
                model.m[1] * bind.x + model.m[5] * bind.y + model.m[9] * bind.z + model.m[13],
                model.m[2] * bind.x + model.m[6] * bind.y + model.m[10] * bind.z + model.m[14]};
            if (!std::isfinite(world.x) || !std::isfinite(world.y) ||
                !std::isfinite(world.z)) {
                continue;
            }
            project_box_point(viewProj, width, height, world, box);
        }
    }
    return box;
}

ScreenBox union_screen_boxes(const ScreenBox& a, const ScreenBox& b) {
    if (!a.any) {
        return b;
    }
    if (!b.any) {
        return a;
    }
    return ScreenBox{true, std::min(a.x0, b.x0), std::min(a.y0, b.y0),
                     std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
}

void write_screenshot(Renderer& renderer, SDL_Window* win, const std::string& path) {
    const std::vector<uint8_t> pixels = renderer.read_pixels();
    int pixel_width = 0;
    int pixel_height = 0;
    if (!SDL_GetWindowSizeInPixels(win, &pixel_width, &pixel_height)) {
        throw std::runtime_error(std::string("SDL_GetWindowSizeInPixels: ") +
                                 SDL_GetError());
    }
    const size_t expected = static_cast<size_t>(pixel_width) * static_cast<size_t>(pixel_height) * 4;
    if (pixels.size() != expected) {
        throw std::runtime_error("read " + std::to_string(pixels.size()) +
                                 " pixels, expected " + std::to_string(expected));
    }

    const PixelStats stats = pixel_stats(pixels);
    std::printf("s1-pixels min=%d,%d,%d max=%d,%d,%d mean=%.1f,%.1f,%.1f\n", stats.min_rgb[0],
                stats.min_rgb[1], stats.min_rgb[2], stats.max_rgb[0], stats.max_rgb[1],
                stats.max_rgb[2], stats.mean_rgb[0], stats.mean_rgb[1], stats.mean_rgb[2]);

    if (stbi_write_png(path.c_str(), pixel_width, pixel_height, 4, pixels.data(),
                       pixel_width * 4) == 0) {
        throw std::runtime_error("cannot write " + path);
    }
    std::printf("s1-screenshot path=%s width=%d height=%d\n", path.c_str(), pixel_width,
                pixel_height);
}

std::string still_png_path(std::string dir, const char* name) {
    if (!dir.empty() && dir.back() != '/') {
        dir += '/';
    }
    return dir + name + ".png";
}
