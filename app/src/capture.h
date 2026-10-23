#pragma once

// Frame output (S10 Task 6a): the window's view-projection, the s1-status
// line, the screenshot/still writers and the screen-space boxes the capture
// guards measure.

#include "app.h"
#include "camera.h"
#include "encounter_rt.h"
#include "renderer.h"
#include "scene.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

// The camera's view-projection for the window's current drawable size,
// recomputed per frame so the aspect follows a resize (the task-3 stopgap
// froze it at startup, which stretched the image on a resized window).
Mat4 camera_view_proj(const Camera& camera, SDL_Window* win);

// Names the swapchain format so a run records which gamma path applies (SDR
// composition yields B8G8R8A8_UNORM on macOS; an _SRGB variant would mean the
// shader must stop encoding its output).
const char* texture_format_name(SDL_GPUTextureFormat format);

// Per-channel min/max/mean of an RGBA8 frame's RGB bytes, so non-uniformity
// (i.e. "something was drawn") is checkable from the terminal.
struct PixelStats {
    int min_rgb[3];
    int max_rgb[3];
    double mean_rgb[3];
};

PixelStats pixel_stats(const std::vector<uint8_t>& rgba8);

// The `s1-status` line reports *queried* state, never assumed flags:
//   windowed          — 1 when the window carries no SDL_WINDOW_FULLSCREEN flag;
//   relative_mouse    — SDL's own answer for the window (0 = off);
//   mouse_auto_capture— the value of SDL_HINT_MOUSE_AUTO_CAPTURE (0 = off);
//   frames            — the run's configured frame budget (AppOptions::frames).
void print_status_line(const AppOptions& opts, SDL_Window* win);

// The entity's projected screen region (pixels, top-down, inclusive), for the
// idle-pair guard. Every posed vertex is projected; vertices behind the camera
// (w <= 0) are skipped and on-screen coordinates are clamped to the drawable.
struct ScreenBox {
    bool any = false;
    int x0 = 0;
    int y0 = 0;
    int x1 = -1;
    int y1 = -1;
};

// Projects one world point into `box`, growing it. Points behind the camera or
// with a non-finite clip position are skipped, exactly like the draw path.
void project_box_point(const Mat4& viewProj, int width, int height, const Vec3& world,
                       ScreenBox& box);

ScreenBox project_rig_bounds(const Scene& scene, std::size_t rigIndex,
                             const std::vector<Mat4>& palette, const Mat4& viewProj, int width,
                             int height);

// The club's projected screen region. The club is unskinned static geometry:
// its live placement is one model matrix (the posed socket frame composed with
// its authored bind frame), so every vertex is that matrix applied to the bind
// position. It is a separate mesh range outside the skinned bbox — the S4
// entity bounds union it so the guards cover what the entity actually draws.
ScreenBox project_club_bounds(const Scene& scene, const ClubAttachment& club,
                              const Mat4& model, const Mat4& viewProj, int width, int height);

// The smallest box containing both inputs (an empty input is the identity).
ScreenBox union_screen_boxes(const ScreenBox& a, const ScreenBox& b);

// Reads back the last rendered frame and writes it as a PNG, printing the
// s1-pixels stats (the acceptance script's non-uniformity proof). Throws
// std::runtime_error when the readback size is unexpected or the write fails.
// The messages carry no "sotc-viewer: " prefix — run_app's catch adds it once.
void write_screenshot(Renderer& renderer, SDL_Window* win, const std::string& path);

// `<dir>/<name>.png`, whether or not `dir` carries its trailing slash.
std::string still_png_path(std::string dir, const char* name);

// A still is an observation: every rig's clock is snapshotted around `render`
// and restored, so no capture can move the clip time, the clip selection, or a
// playing/pinned flag between two timeline steps.
template <typename Render>
void with_clocks_restored(std::vector<Playback>& playbacks, Render&& render) {
    struct PlaybackClockState {
        int index;
        float time;
        bool playing;
        bool pinned;
    };
    std::vector<PlaybackClockState> saved;
    saved.reserve(playbacks.size());
    for (const Playback& p : playbacks) {
        saved.push_back(PlaybackClockState{p.index, p.time, p.playing, p.pinned});
    }
    render();
    for (std::size_t i = 0; i < saved.size() && i < playbacks.size(); ++i) {
        playbacks[i].index = saved[i].index;
        playbacks[i].time = saved[i].time;
        playbacks[i].playing = saved[i].playing;
        playbacks[i].pinned = saved[i].pinned;
    }
}
