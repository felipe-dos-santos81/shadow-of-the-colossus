#include "app.h"

#include "anim.h"
#include "asset_io.h"
#include "camera.h"
#include "heightmap.h"
#include "manifest.h"
#include "player.h"
#include "renderer.h"
#include "scene.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <stdexcept>
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

// The camera's view-projection for the window's current drawable size,
// recomputed per frame so the aspect follows a resize (the task-3 stopgap
// froze it at startup, which stretched the image on a resized window).
Mat4 camera_view_proj(const Camera& camera, SDL_Window* win) {
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(win, &width, &height)) {
        const char* error = SDL_GetError();
        throw std::runtime_error(std::string("sotc-viewer: SDL_GetWindowSizeInPixels: ") +
                                 (error != nullptr ? error : "unknown error"));
    }
    const float aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
    return mat4_mul(camera_proj(camera, aspect), camera_view(camera));
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

// The clip list and the playback state. clips[0] is the built-in procedural
// wave when the model has a skin, followed by the model's own glTF clips;
// `time` is unwrapped seconds (every pose wraps it into the clip's duration).
struct Playback {
    std::vector<AnimationCpu> clips;
    int index = -1;  // -1 = nothing to play (no skin and no clips)
    float time = 0.0f;
    bool playing = false;
    bool has_procedural = false;
};

// Fills `playback` from the scene and starts on the first clip.
void init_playback(Playback& playback, const Scene& scene) {
    if (!scene.skin.joints.empty()) {
        playback.clips.push_back(make_procedural_wave(scene));
        playback.has_procedural = true;
    }
    playback.clips.insert(playback.clips.end(), scene.animations.begin(), scene.animations.end());
    if (!playback.clips.empty()) {
        playback.index = 0;
        playback.playing = true;
    }
}

// Resolves an `--anim` value: an exact clip name, or a 0-based index. -1 when
// nothing matches.
int resolve_clip(const std::vector<AnimationCpu>& clips, const std::string& want) {
    char* end = nullptr;
    const long index = std::strtol(want.c_str(), &end, 10);
    if (!want.empty() && end != want.c_str() && *end == '\0') {
        return (index >= 0 && static_cast<size_t>(index) < clips.size()) ? static_cast<int>(index)
                                                                         : -1;
    }
    for (size_t i = 0; i < clips.size(); ++i) {
        if (clips[i].name == want) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// The `s2-anim` line reports the selected clip: its name, the palette's joint
// count, whether it is the built-in procedural clip, and the current (wrapped)
// time. Printed at startup and on every clip change; a model with no clip at
// all prints nothing.
void print_anim_status(const Playback& playback, const Scene& scene) {
    if (playback.index < 0) {
        return;
    }
    const AnimationCpu& clip = playback.clips[static_cast<size_t>(playback.index)];
    const bool procedural = playback.has_procedural && playback.index == 0;
    std::printf("s2-anim clip=%s joints=%zu procedural=%d t=%.3f\n", clip.name.c_str(),
                scene.skin.joints.size(), procedural ? 1 : 0,
                animation_loop_time(playback.time, clip.duration));
}

// Selects a clip by index, restarts it at t = 0, and reports the change.
void select_clip(Playback& playback, const Scene& scene, int index) {
    playback.index = index;
    playback.time = 0.0f;
    print_anim_status(playback, scene);
}

// Advances playback by `dt` and uploads the resulting pose. Called once per
// frame, before the draw; a no-op when the model has no clip (and the palette
// upload itself is a no-op when the scene has no skin).
void update_pose(Renderer& renderer, const Scene& scene, Playback& playback, float dt) {
    if (playback.index < 0) {
        return;
    }
    if (playback.playing) {
        playback.time += dt;
    }
    const AnimationCpu& clip = playback.clips[static_cast<size_t>(playback.index)];
    const float wrapped = animation_loop_time(playback.time, clip.duration);
    renderer.upload_pose(animation_pose(scene, clip, wrapped));
}

// --- Manifest mode (S3 W1.4) ------------------------------------------------

constexpr float kHeightmapCellSize = 1.0f;        // metres between ground samples
constexpr float kFixedFrameSeconds = 1.0f / 60.0f;  // the s3-selftest's fixed step
constexpr int kSelftestFrames = 240;                // 4 simulated seconds

// The camera's look-direction yaw in degrees — the convention player_update
// takes. The orbit Camera stores the opposite direction (the eye sits at
// `yaw`, so the look direction is yaw - 180).
float camera_view_yaw(const Camera& camera) { return camera.yaw - 180.0f; }

// Moves the composed scene's player meshes with the player: meshes
// [playerMeshBegin, end) are the player's (compose_scene appends them last).
// `groundOffsetY` is the player model's origin-to-feet lift (see
// player_ground_offset), so the character stands on the ground point instead
// of sinking into it.
void apply_player_transform(Scene& scene, size_t playerMeshBegin, const PlayerState& player,
                            float groundOffsetY) {
    const Mat4 model = player_model_matrix(player, groundOffsetY);
    for (size_t i = playerMeshBegin; i < scene.meshes.size(); ++i) {
        scene.meshes[i].model = model;
    }
}

// The spawn the s3-selftest and the interactive loop start from: the
// manifest's own spawn when it has one, otherwise the composed scene's bounds
// centre.
Vec3 spawn_position(const SceneManifest& manifest, const Scene& scene) {
    if (manifest.hasSpawn) {
        return manifest.spawnPosition;
    }
    return Vec3{(scene.boundsMin.x + scene.boundsMax.x) * 0.5f,
                (scene.boundsMin.y + scene.boundsMax.y) * 0.5f,
                (scene.boundsMin.z + scene.boundsMax.z) * 0.5f};
}

// The `s1-scene` line: what the loaded scene holds (one line for either load
// path — a single glb or a composed manifest).
void print_scene_line(const Scene& scene) {
    size_t vertices = 0;
    for (const MeshCpu& mesh : scene.meshes) {
        vertices += mesh.vertices.size();
    }
    std::printf("s1-scene meshes=%zu vertices=%zu materials=%zu textures=%zu\n",
                scene.meshes.size(), vertices, scene.materials.size(), scene.textures.size());
}

// The `s3-status` line the W1.5 acceptance script greps: the manifest's
// instance count, the chosen spawn, the player's position after the simulated
// frames, and whether the ground under it is real (a finite heightmap sample).
void print_s3_status(const SceneManifest& manifest, Vec3 spawn, Vec3 position, bool ground_ok) {
    std::printf("s3-status instances=%zu spawn=%.3f,%.3f,%.3f pos=%.3f,%.3f,%.3f ground=%s "
                "frames=%d\n",
                manifest.instances.size(), spawn.x, spawn.y, spawn.z, position.x, position.y,
                position.z, ground_ok ? "ok" : "none", kSelftestFrames);
}

// Reads back the last rendered frame and writes it as a PNG, printing the
// s1-pixels stats (the acceptance script's non-uniformity proof). Throws
// std::runtime_error when the readback size is unexpected or the write fails.
void write_screenshot(Renderer& renderer, SDL_Window* win, const std::string& path) {
    const std::vector<uint8_t> pixels = renderer.read_pixels();
    int pixel_width = 0;
    int pixel_height = 0;
    if (!SDL_GetWindowSizeInPixels(win, &pixel_width, &pixel_height)) {
        throw std::runtime_error(std::string("sotc-viewer: SDL_GetWindowSizeInPixels: ") +
                                 SDL_GetError());
    }
    const size_t expected = static_cast<size_t>(pixel_width) * static_cast<size_t>(pixel_height) * 4;
    if (pixels.size() != expected) {
        throw std::runtime_error("sotc-viewer: read " + std::to_string(pixels.size()) +
                                 " pixels, expected " + std::to_string(expected));
    }

    const PixelStats stats = pixel_stats(pixels);
    std::printf("s1-pixels min=%d,%d,%d max=%d,%d,%d mean=%.1f,%.1f,%.1f\n", stats.min_rgb[0],
                stats.min_rgb[1], stats.min_rgb[2], stats.max_rgb[0], stats.max_rgb[1],
                stats.max_rgb[2], stats.mean_rgb[0], stats.mean_rgb[1], stats.mean_rgb[2]);

    if (stbi_write_png(path.c_str(), pixel_width, pixel_height, 4, pixels.data(),
                       pixel_width * 4) == 0) {
        throw std::runtime_error("sotc-viewer: cannot write " + path);
    }
    std::printf("s1-screenshot path=%s width=%d height=%d\n", path.c_str(), pixel_width,
                pixel_height);
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
    // Manifest mode: --model names the player's glb (required); --s3-selftest
    // only makes sense for a composed scene.
    if (!opts.manifestPath.empty() && opts.modelRelPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --manifest needs --model (the player glb)\n");
        return 2;
    }
    if (opts.s3Selftest && opts.manifestPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --s3-selftest needs --manifest\n");
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

        // Scene: a single glb (--model alone) or a composed manifest scene
        // (--manifest, with --model as the player's glb). `Scene scene{}` keeps
        // zeroed bounds for a model-less run, so camera_frame stays finite even
        // when there is nothing to draw.
        Scene scene{};
        SceneManifest manifest{};          // empty when --manifest is absent
        size_t player_mesh_begin = 0;      // the player's first mesh in a composed scene
        Heightmap heightmap{};             // an empty heightmap = no ground
        const bool follow_mode = !opts.manifestPath.empty();
        if (follow_mode) {
            // The manifest is user data: a malformed line is a usage error
            // (exit 2) carrying the file and line, never a crash.
            try {
                manifest = parse_manifest(opts.manifestPath);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "sotc-viewer: %s\n", error.what());
                return 2;
            }
            scene = compose_scene(opts.libraryDir, manifest, opts.modelRelPath,
                                  &player_mesh_begin);
            renderer.upload(scene);
            print_scene_line(scene);

            // The ground is built from the scene's instance meshes only: the
            // player's own body must never rasterize into the ground it walks
            // on (it stands on it). A scene with no ground at all (no instance
            // meshes) leaves the heightmap empty — ground=none, no clamp.
            try {
                heightmap = build_heightmap(scene, kHeightmapCellSize, player_mesh_begin);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "sotc-viewer: no ground: %s\n", error.what());
            }
        } else if (!opts.modelRelPath.empty()) {
            scene = load_scene(opts.libraryDir, opts.modelRelPath);
            renderer.upload(scene);
            print_scene_line(scene);
        }

        // The real camera: framed on the scene bounds at load (and again on F).
        Camera camera{};
        camera_frame(camera, scene.boundsMin, scene.boundsMax);

        // The player and its follow camera (manifest mode only): spawn at the
        // manifest's spawn or the bounds centre, with the camera snapped behind
        // the player (a huge dt makes camera_follow's first blend factor
        // exactly 1 in float arithmetic; later frames smooth).
        PlayerState player{spawn_position(manifest, scene),
                           manifest.hasSpawn ? manifest.spawnYawDeg : 0.0f};
        CameraFollow follow;
        float player_ground = 0.0f;  // the model's origin-to-feet lift
        if (follow_mode) {
            player_ground = player_ground_offset(scene, player_mesh_begin);
            camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f, 0.0f,
                          /*moving=*/false, /*dtSeconds=*/10.0f);
            apply_player_transform(scene, player_mesh_begin, player, player_ground);
        }

        // Playback: the clip list starts with the built-in procedural wave when
        // the model has a skin; --anim picks a clip by name or index, and
        // --anim-time pins the time and starts paused (deterministic captures).
        Playback playback;
        init_playback(playback, scene);
        if (!opts.animClip.empty()) {
            const int index = resolve_clip(playback.clips, opts.animClip);
            if (index < 0) {
                std::fprintf(stderr, "sotc-viewer: --anim %s: no such clip", opts.animClip.c_str());
                if (playback.clips.empty()) {
                    std::fprintf(stderr, " (the model has no animation clips)\n");
                } else {
                    std::fprintf(stderr, "; available clips:");
                    for (size_t i = 0; i < playback.clips.size(); ++i) {
                        std::fprintf(stderr, " %zu=%s", i, playback.clips[i].name.c_str());
                    }
                    std::fprintf(stderr, "\n");
                }
                return 2;
            }
            playback.index = index;
        }
        if (opts.animTimeSet) {
            playback.time = opts.animTime;
            playback.playing = false;
        }
        print_anim_status(playback, scene);

        if (opts.s3Selftest) {
            // The pinned W1.4 self-test: 240 fixed 1/60 s frames of
            // forward+run (camera-relative movement, the follow camera
            // recentring), then one rendered frame and the s3-status line. With
            // --screenshot the frame is also captured (the F3/wave check).
            const Vec3 spawn = player.position;
            PlayerInput input;
            input.forward = true;
            input.run = true;
            for (int frame = 0; frame < kSelftestFrames; ++frame) {
                camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f, 0.0f,
                              /*moving=*/true, kFixedFrameSeconds);
                player_update(player, input, kFixedFrameSeconds, heightmap,
                              camera_view_yaw(camera));
                update_pose(renderer, scene, playback, kFixedFrameSeconds);
            }
            apply_player_transform(scene, player_mesh_begin, player, player_ground);

            renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                                  /*acquire_swapchain=*/true);
            if (!opts.screenshotPath.empty()) {
                write_screenshot(renderer, shell.win, opts.screenshotPath);
            }
            print_status_line(opts, shell.win);
            print_s3_status(manifest, spawn, player.position,
                            std::isfinite(heightmap_sample(heightmap, player.position)));
            return 0;
        }

        if (opts.selftest) {
            update_pose(renderer, scene, playback, /*dt=*/0.0f);
            renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                                  /*acquire_swapchain=*/true);
            print_status_line(opts, shell.win);
            return 0;
        }

        if (!opts.screenshotPath.empty()) {
            // A capture run: in manifest mode the player stands at its spawn
            // (ground-followed, camera behind it) while the pose advances with
            // the real frame times; then one PNG is written.
            Uint64 last_ticks = SDL_GetTicks();
            for (int frame = 0; frame < opts.frames; ++frame) {
                const Uint64 now = SDL_GetTicks();
                const float dt = static_cast<float>(now - last_ticks) / 1000.0f;
                last_ticks = now;
                if (follow_mode) {
                    camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f, 0.0f,
                                  /*moving=*/false, dt);
                    player_update(player, PlayerInput{}, dt, heightmap, camera_view_yaw(camera));
                    apply_player_transform(scene, player_mesh_begin, player, player_ground);
                }
                update_pose(renderer, scene, playback, dt);
                renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                                      /*acquire_swapchain=*/true);
            }

            write_screenshot(renderer, shell.win, opts.screenshotPath);
            return 0;
        }

        // Interactive: left-drag orbits (single-model mode) or turns the follow
        // camera's view (manifest mode), the wheel zooms, F reframes (single
        // model) or recenters behind the player (manifest mode), Space
        // plays/pauses, [ and ] switch clips, R restarts the clip, Escape or
        // the close button quits. In manifest mode W/A/S/D walk (Shift runs)
        // and the camera follows. Input needs no capture: plain mouse-motion
        // events with SDL_BUTTON_LMASK held (never relative mode, never
        // SDL_CaptureMouse) — the cursor stays visible and free.
        bool running = true;
        std::deque<Uint64> frame_ticks;  // frame arrival times within the last second
        Uint64 last_title_update = SDL_GetTicks();
        Uint64 last_ticks = SDL_GetTicks();
        while (running) {
            // This frame's drag and wheel deltas (motion events arrive several
            // per frame; the follow camera takes their sum).
            float drag_dx = 0.0f;
            float drag_dy = 0.0f;
            float wheel_delta = 0.0f;

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                    case SDL_EVENT_QUIT:
                        running = false;
                        break;
                    case SDL_EVENT_KEY_DOWN:
                        if (event.key.key == SDLK_ESCAPE) {
                            running = false;
                        } else if (event.key.key == SDLK_F) {
                            if (follow_mode) {
                                // Recenter the follow camera behind the player.
                                follow.yawOffsetDeg = 0.0f;
                                follow.pitchOffsetDeg = 0.0f;
                                follow.distance = CameraFollow{}.distance;
                            } else {
                                camera_frame(camera, scene.boundsMin, scene.boundsMax);
                            }
                        } else if (event.key.key == SDLK_SPACE) {
                            playback.playing = !playback.playing;
                        } else if (event.key.key == SDLK_R) {
                            playback.time = 0.0f;
                        } else if (!playback.clips.empty() &&
                                   (event.key.key == SDLK_LEFTBRACKET ||
                                    event.key.key == SDLK_RIGHTBRACKET)) {
                            const int count = static_cast<int>(playback.clips.size());
                            const int step = (event.key.key == SDLK_LEFTBRACKET) ? count - 1 : 1;
                            select_clip(playback, scene, (playback.index + step) % count);
                        }
                        break;
                    case SDL_EVENT_MOUSE_MOTION:
                        // SDL fills xrel/yrel from successive positions even
                        // without relative mode, so a plain motion event with
                        // the button held is all a drag needs.
                        if ((event.motion.state & SDL_BUTTON_LMASK) != 0) {
                            if (follow_mode) {
                                drag_dx += event.motion.xrel;
                                drag_dy += event.motion.yrel;
                            } else {
                                camera_orbit(camera, event.motion.xrel, event.motion.yrel);
                            }
                        }
                        break;
                    case SDL_EVENT_MOUSE_WHEEL:
                        if (follow_mode) {
                            wheel_delta += event.wheel.y;
                        } else {
                            camera_zoom(camera, event.wheel.y);
                        }
                        break;
                    default:
                        break;
                }
            }

            const Uint64 now = SDL_GetTicks();
            const float dt = static_cast<float>(now - last_ticks) / 1000.0f;
            last_ticks = now;

            if (follow_mode) {
                const bool* keys = SDL_GetKeyboardState(nullptr);
                PlayerInput input;
                input.forward = keys[SDL_SCANCODE_W];
                input.back = keys[SDL_SCANCODE_S];
                input.left = keys[SDL_SCANCODE_A];
                input.right = keys[SDL_SCANCODE_D];
                input.run = keys[SDL_SCANCODE_LSHIFT];
                const bool moving = input.forward || input.back || input.left || input.right;

                // The camera first: player_update's movement basis is the
                // camera's current look direction.
                camera_follow(camera, follow, player.position, player.yawDeg, drag_dx, drag_dy,
                              wheel_delta, moving, dt);
                player_update(player, input, dt, heightmap, camera_view_yaw(camera));
                apply_player_transform(scene, player_mesh_begin, player, player_ground);
            }

            update_pose(renderer, scene, playback, dt);

            renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                                  /*acquire_swapchain=*/true);

            // The S1 fps HUD: the window title shows the rolling average over
            // the last second, refreshed twice a second (drawn text arrives
            // with SDL_ttf when a HUD needs it).
            frame_ticks.push_back(now);
            while (!frame_ticks.empty() && frame_ticks.front() + 1000 < now) {
                frame_ticks.pop_front();
            }
            if (now - last_title_update >= 500) {
                char title[64];
                std::snprintf(title, sizeof(title), "sotc-viewer | %.1f fps",
                              static_cast<double>(frame_ticks.size()));
                SDL_SetWindowTitle(shell.win, title);
                last_title_update = now;
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "sotc-viewer: %s\n", error.what());
        return 1;
    }
}
