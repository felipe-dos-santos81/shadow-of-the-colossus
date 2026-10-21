#include "app.h"

#include "anim.h"
#include "asset_io.h"
#include "camera.h"
#include "collision.h"
#include "grab.h"
#include "heightmap.h"
#include "manifest.h"
#include "player.h"
#include "player_state.h"
#include "renderer.h"
#include "scene.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
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
        // No "sotc-viewer: " prefix here: run_app's catch adds it once.
        throw std::runtime_error(std::string("SDL_GetWindowSizeInPixels: ") +
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

// The `s2-anim` line reports one rig's selected clip: its name, that rig's own
// joint count (the size of that rig's palette), whether it is the built-in
// procedural clip, and the current time (the loop wrap, or the pinned capture
// time). Printed at startup for every rig and on every player clip change; a
// rig with no clip at all prints nothing. The player's line (Scene::playerRig)
// keeps the S2 shape; every other rig's line carries a `rig=<n> ` prefix so two
// playback clocks are never confused. Real clips print procedural=0; the
// stand-in is only ever the single clip of a model the converter could not
// animate.
void print_anim_status(const Scene& scene, std::size_t rigIndex, const Playback& playback) {
    if (playback.index < 0) {
        return;
    }
    const AnimationCpu& clip = playback.clips[static_cast<size_t>(playback.index)];
    const bool procedural = playback.has_procedural && playback.index == 0;
    const std::size_t joints = rig_at(scene, rigIndex).skin.joints.size();
    const float time = playback_time(playback, clip.duration);
    if (rigIndex == scene.playerRig) {
        std::printf("s2-anim clip=%s joints=%zu procedural=%d t=%.3f\n", clip.name.c_str(),
                    joints, procedural ? 1 : 0, time);
    } else {
        std::printf("s2-anim rig=%zu clip=%s joints=%zu procedural=%d t=%.3f\n", rigIndex,
                    clip.name.c_str(), joints, procedural ? 1 : 0, time);
    }
}

// Selects a clip by index, restarts it at t = 0, and reports the change.
void select_clip(Playback& playback, const Scene& scene, std::size_t rigIndex, int index) {
    playback.index = index;
    playback.time = 0.0f;
    print_anim_status(scene, rigIndex, playback);
}

// Advances the rig's playback by `dt` and uploads the rig's palette. Called
// once per frame per rig, before the draw; a no-op when the rig has no clip
// (and the upload itself is a no-op for a rig with no skin). The palette is
// returned so a per-frame consumer that needs the pose (the collision pass)
// reads this frame's matrices instead of re-evaluating the clip.
std::vector<Mat4> update_pose(Renderer& renderer, const Scene& scene, std::size_t rigIndex,
                              Playback& playback, float dt) {
    if (playback.index < 0) {
        return {};
    }
    if (playback.playing) {
        playback.time += dt;
    }
    const AnimationCpu& clip = playback.clips[static_cast<size_t>(playback.index)];
    const float time = playback_time(playback, clip.duration);
    std::vector<Mat4> palette = animation_pose(scene, rigIndex, clip, time);
    renderer.upload_pose(rigIndex, palette);
    return palette;
}

// S4.2: Valus's club is a separate, unskinned glb authored in the bind frame of
// the entity rig's OptBone_sword. It is appended as static geometry (rig = -1)
// and placed from that joint's posed frame each frame.
constexpr const char* kClubGlb =
    "models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a_weapon.glb";
constexpr const char* kClubSocketJoint = "OptBone_sword";

// The club, once loaded into a composed scene: the entity rig it binds to, the
// socket joint resolved by name, and its half-open static mesh range. `loaded`
// stays false in single-model mode, in a manifest scene with no entity, and
// when the entity rig carries no OptBone_sword — in every case the club is
// simply not drawn.
struct ClubAttachment {
    bool loaded = false;
    std::size_t rigIndex = 0;
    std::size_t swordJoint = 0;
    std::size_t meshBegin = 0;
    std::size_t meshEnd = 0;
};

// The slot of the joint named exactly `name` in `rig`, or -1. The club socket
// is resolved by name, never by an index that could mean another bone.
int joint_index_named(const RigCpu& rig, std::string_view name) {
    for (std::size_t i = 0; i < rig.skin.joints.size(); ++i) {
        if (rig.skin.joints[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Loads the club into a composed scene and resolves its socket. The entity rig
// and its first mesh must exist (compose_scene guarantees both for an entity);
// a rig without OptBone_sword leaves the club undrawn, with one line naming the
// missing joint rather than a silently wrong-hand prop.
ClubAttachment load_club(Scene& scene, const std::filesystem::path& libraryDir,
                         std::size_t entityRig, const std::string& entityGlb) {
    ClubAttachment club{};
    if (entityRig >= scene.rigs.size()) {
        return club;
    }
    const RigCpu& rig = scene.rigs[entityRig];
    const int socket = joint_index_named(rig, kClubSocketJoint);
    if (socket < 0) {
        std::fprintf(stderr, "sotc-viewer: %s has no %s joint; the club is not drawn\n",
                     entityGlb.c_str(), kClubSocketJoint);
        return club;
    }
    if (rig.meshBegin >= scene.meshes.size()) {
        std::fprintf(stderr,
                     "sotc-viewer: the entity rig has no meshes; the club is not drawn\n");
        return club;
    }

    // Static geometry at the entity's placement; each frame its model is
    // rewritten from the posed socket (update_club_model).
    club.meshBegin = append_static_model(scene, libraryDir, kClubGlb,
                                         scene.meshes[rig.meshBegin].model);
    club.meshEnd = scene.meshes.size();
    club.loaded = true;
    club.rigIndex = entityRig;
    club.swordJoint = static_cast<std::size_t>(socket);
    return club;
}

// Places the club's meshes at the entity's posed sword joint. The entity's own
// first mesh carries the placement (manifest TRS plus the once-per-run grounding
// lift), so the club reads it instead of keeping a copy that could drift. A
// no-op when the club is not loaded or this frame has no palette for its rig.
void update_club_model(Scene& scene, const std::vector<std::vector<Mat4>>& palettes,
                       const ClubAttachment& club) {
    if (!club.loaded || club.rigIndex >= palettes.size() ||
        club.rigIndex >= scene.rigs.size()) {
        return;
    }
    const RigCpu& rig = scene.rigs[club.rigIndex];
    if (rig.meshBegin >= scene.meshes.size() || club.meshBegin >= scene.meshes.size()) {
        return;
    }
    const Mat4 model =
        club_model(scene, palettes[club.rigIndex], club.swordJoint,
                   scene.meshes[rig.meshBegin].model);
    const std::size_t end = std::min(club.meshEnd, scene.meshes.size());
    for (std::size_t i = club.meshBegin; i < end; ++i) {
        scene.meshes[i].model = model;
    }
}

// Every rig's pose for this frame: the player's (locomotion) and each entity's
// (its own loop, on its own clock), each into its own palette buffer. The
// palettes are returned so this frame's consumers (the entity collision proxy,
// the target camera's chest point) read exactly the matrices the renderer was
// given, not a second evaluation of the clip. The club, when loaded, is placed
// from the entity's fresh palette here: every draw path goes through this one
// function, so no renderer frame can show a stale club.
std::vector<std::vector<Mat4>> update_all_poses(Renderer& renderer, Scene& scene,
                                                std::vector<Playback>& playbacks, float dt,
                                                const ClubAttachment& club) {
    std::vector<std::vector<Mat4>> palettes(playbacks.size());
    for (std::size_t rigIndex = 0; rigIndex < playbacks.size(); ++rigIndex) {
        palettes[rigIndex] = update_pose(renderer, scene, rigIndex, playbacks[rigIndex], dt);
    }
    update_club_model(scene, palettes, club);
    return palettes;
}

// Frames the single-model camera on the pose that is actually drawn. With a
// clip active the palette carries the clip's root translation series, which
// holds the ground height and lifts the subject above the bind-pose bounds the
// camera was framed on at load; framing on the bind bounds cuts the posed
// subject at the top (Task 7 §5.2). A scene with no clip keeps the bind bounds.
// Manifest mode never uses this: its follow camera tracks the player instead.
void frame_single_model(Camera& camera, const Scene& scene, const Playback& playback) {
    if (playback.index < 0) {
        camera_frame(camera, scene.boundsMin, scene.boundsMax);
        return;
    }
    const AnimationCpu& clip = playback.clips[static_cast<size_t>(playback.index)];
    const float time = playback_time(playback, clip.duration);
    Vec3 bounds_min{};
    Vec3 bounds_max{};
    posed_bounds(scene, animation_pose(scene, scene.playerRig, clip, time), bounds_min, bounds_max);

    // camera_frame fits the bounds' half-diagonal with distance = r / tan(fov/2),
    // which leaves the bounding sphere tangent to the frustum only at
    // r / sin(fov/2). Pad the posed box by 1 / cos(fov/2) about its centre so the
    // subject actually fits — a tall, narrow figure (Wander is 1.25 m tall,
    // 0.45 wide) otherwise puts the top of the head on the top edge.
    const Vec3 centre{(bounds_min.x + bounds_max.x) * 0.5f,
                      (bounds_min.y + bounds_max.y) * 0.5f,
                      (bounds_min.z + bounds_max.z) * 0.5f};
    const float pad = 1.0f / std::cos(0.5f * camera.fovYDeg * kDegToRad);
    bounds_min = Vec3{centre.x + (bounds_min.x - centre.x) * pad,
                      centre.y + (bounds_min.y - centre.y) * pad,
                      centre.z + (bounds_min.z - centre.z) * pad};
    bounds_max = Vec3{centre.x + (bounds_max.x - centre.x) * pad,
                      centre.y + (bounds_max.y - centre.y) * pad,
                      centre.z + (bounds_max.z - centre.z) * pad};
    camera_frame(camera, bounds_min, bounds_max);
}

// --- Manifest mode (S3 W1.4) ------------------------------------------------

constexpr float kHeightmapCellSize = 1.0f;        // metres between ground samples
constexpr float kFixedFrameSeconds = 1.0f / 60.0f;  // the s3-selftest's fixed step
constexpr int kSelftestFrames = 240;                // 4 simulated seconds

// --- S4 encounter self-test (Task 6) ---------------------------------------

constexpr int kS4MaxWalkFrames = 1200;       // 20 simulated seconds at 1/60 s
constexpr int kS4StopFrames = 5;             // consecutive frames below the epsilon
constexpr float kS4StopEpsilon = 1e-3f;      // metres of horizontal travel per frame
constexpr float kS4GroundTolerance = 1e-3f;  // the player's feet vs the heightmap sample
constexpr float kS4FeetTolerance = 0.10f;    // the entity's lowest posed vertex vs ground
constexpr float kS4PenetrationTolerance = 0.02f;  // allowed capsule penetration (m)
constexpr float kS4StopBeforeTolerance = 1.0f;    // the stop must be before the axis point

// --- S4.1 climb/fight self-test and input ----------------------------------

constexpr int kS41ClimbFrames = 240;        // 4 simulated seconds at 1/60 s
constexpr float kS41MinRise = 3.0f;         // metres the proven climb must gain
constexpr int kS41ApproachFrames = 240;     // walk from S4's stop to the foot band
constexpr int kS41ChargeFrames = 60;        // held J past the 0.9 s full charge
constexpr int kS41WeakChargeFrames = 20;    // a third of a second: a weak hit
constexpr int kS41CatchWaitFrames = 42;     // 0.7 s with no catch press
constexpr int kS41MaxFallFrames = 600;      // 10 s: the whole head-to-ground fall
constexpr int kS41MaxStaggerFrames = 120;   // the 1.2 s stagger plus margin
constexpr float kS41Eps = 1e-4f;            // monotonic and ground-contact epsilon
constexpr float kS41StickDeadzone = 0.25f;  // gamepad axis deadzone
constexpr float kS41StickPixelsPerSecond = 900.0f;  // right stick -> drag pixels

// The R3 reaction names, plus the generic body reaction a strike on a band with
// no weakpoint plays (the pinned bands without a weakpoint are not silent). The
// player's combat clips are the binding's mode mapping. Every name is resolved
// once at load; a missing one is a load error, never a silent fallback.
constexpr const char* kHeadReactionClip = "minotaur_a_add_head_damage";
constexpr const char* kArmReactionClip = "minotaur_a_add_ude_r_damage";
constexpr const char* kBodyReactionClip = "minotaur_a_add_mune_r_damage";
constexpr const char* kDeathClip = "minotaur_a_death_front";
constexpr const char* kGripLoopClip = "player_a_grip_loop";
constexpr const char* kGripMoveClip = "player_a_grip_move_f1";
constexpr const char* kGripAttackClip = "player_a_grip_float_u_hi_attack";
constexpr const char* kFallClip = "player_a_damage_m_fly2";
constexpr const char* kHeavyHurtClip = "player_a_damage_h_fly1";
constexpr const char* kHeavyHurtEndClip = "player_a_damage_h_fly_end";
constexpr const char* kPlayerDeathClip = "player_a_fall_damage_to_death";

// The world position of one vertex under a rig's palette: the shader's
// skinning sum (worldJoint * inverseBind per influence), then the mesh's own
// model transform (placement and grounding lift included). A vertex with no
// weights stays at its bind position, like the shader's zero-weight path.
bool posed_vertex_world(const std::vector<Mat4>& palette, const MeshCpu& mesh,
                        const Vertex& vertex, Vec3& out) {
    const Vec3 bind{vertex.px, vertex.py, vertex.pz};
    Vec3 point = bind;
    float total_weight = 0.0f;
    Vec3 posed{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
        const float weight = vertex.w[i];
        const std::size_t joint = vertex.j[i];
        if (weight == 0.0f || joint >= palette.size()) {
            continue;
        }
        const Mat4& skin = palette[joint];
        posed.x += weight * (skin.m[0] * bind.x + skin.m[4] * bind.y + skin.m[8] * bind.z +
                             skin.m[12]);
        posed.y += weight * (skin.m[1] * bind.x + skin.m[5] * bind.y + skin.m[9] * bind.z +
                             skin.m[13]);
        posed.z += weight * (skin.m[2] * bind.x + skin.m[6] * bind.y + skin.m[10] * bind.z +
                             skin.m[14]);
        total_weight += weight;
    }
    if (total_weight > 0.0f) {
        point = posed;
    }
    const Mat4& model = mesh.model;
    out = Vec3{model.m[0] * point.x + model.m[4] * point.y + model.m[8] * point.z + model.m[12],
               model.m[1] * point.x + model.m[5] * point.y + model.m[9] * point.z + model.m[13],
               model.m[2] * point.x + model.m[6] * point.y + model.m[10] * point.z + model.m[14]};
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

// A rig's posed world-space bounds over its own half-open mesh range.
struct RigBounds {
    bool any = false;
    Vec3 min{};
    Vec3 max{};
};

RigBounds posed_rig_bounds(const Scene& scene, std::size_t rigIndex,
                           const std::vector<Mat4>& palette) {
    RigBounds bounds;
    const RigCpu& rig = rig_at(scene, rigIndex);
    const std::size_t end = std::min(rig.meshEnd, scene.meshes.size());
    for (std::size_t i = std::min(rig.meshBegin, end); i < end; ++i) {
        for (const Vertex& vertex : scene.meshes[i].vertices) {
            Vec3 world{};
            if (!posed_vertex_world(palette, scene.meshes[i], vertex, world)) {
                continue;
            }
            if (!bounds.any) {
                bounds.min = bounds.max = world;
                bounds.any = true;
                continue;
            }
            bounds.min.x = std::min(bounds.min.x, world.x);
            bounds.min.y = std::min(bounds.min.y, world.y);
            bounds.min.z = std::min(bounds.min.z, world.z);
            bounds.max.x = std::max(bounds.max.x, world.x);
            bounds.max.y = std::max(bounds.max.y, world.y);
            bounds.max.z = std::max(bounds.max.z, world.z);
        }
    }
    return bounds;
}

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
            const Mat4& m = viewProj;
            const float clip_x = m.m[0] * world.x + m.m[4] * world.y + m.m[8] * world.z + m.m[12];
            const float clip_y = m.m[1] * world.x + m.m[5] * world.y + m.m[9] * world.z + m.m[13];
            const float clip_w = m.m[3] * world.x + m.m[7] * world.y + m.m[11] * world.z + m.m[15];
            if (!(clip_w > 1e-6f) || !std::isfinite(clip_x) || !std::isfinite(clip_y) ||
                !std::isfinite(clip_w)) {
                continue;
            }
            const float ndc_x = clip_x / clip_w;
            const float ndc_y = clip_y / clip_w;
            if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y)) {
                continue;
            }
            int x = static_cast<int>((ndc_x * 0.5f + 0.5f) * static_cast<float>(width));
            int y = static_cast<int>((0.5f - ndc_y * 0.5f) * static_cast<float>(height));
            x = std::clamp(x, 0, width - 1);
            y = std::clamp(y, 0, height - 1);
            if (!box.any) {
                box.x0 = box.x1 = x;
                box.y0 = box.y1 = y;
                box.any = true;
                continue;
            }
            box.x0 = std::min(box.x0, x);
            box.y0 = std::min(box.y0, y);
            box.x1 = std::max(box.x1, x);
            box.y1 = std::max(box.y1, y);
        }
    }
    return box;
}

// A `key=value` token in the manifest's comment header (the S4 provenance
// labels: `placement=builder-pick`, `spawn=data-position+builder-yaw`). The
// parser ignores comments, so the self-test reads them here; an absent token
// is the empty string.
std::string manifest_header_value(const std::string& path, const std::string& key) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    const std::string prefix = key + "=";
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t hash = line.find('#');
        if (hash == std::string::npos) {
            continue;
        }
        std::size_t pos = hash + 1;
        while (pos < line.size()) {
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
                ++pos;
            }
            const std::size_t start = pos;
            while (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos]))) {
                ++pos;
            }
            const std::string token = line.substr(start, pos - start);
            if (token.rfind(prefix, 0) == 0) {
                return token.substr(prefix.size());
            }
        }
    }
    return {};
}

// The horizontal distance from `p` to a capsule's XZ axis segment; `t` is the
// clamped segment parameter.
float capsule_horizontal_distance(const Capsule& capsule, Vec3 p, Vec3& closest) {
    const float dx = capsule.b.x - capsule.a.x;
    const float dz = capsule.b.z - capsule.a.z;
    const float length_squared = dx * dx + dz * dz;
    float t = 0.0f;
    if (length_squared > 0.0f) {
        t = ((p.x - capsule.a.x) * dx + (p.z - capsule.a.z) * dz) / length_squared;
        t = std::clamp(t, 0.0f, 1.0f);
    }
    closest = Vec3{capsule.a.x + dx * t, 0.0f, capsule.a.z + dz * t};
    const float ox = p.x - closest.x;
    const float oz = p.z - closest.z;
    return std::sqrt(ox * ox + oz * oz);
}

bool capsule_overlaps_vertically(const Capsule& capsule, float feet, float head) {
    const float radius = std::max(capsule.r, 0.0f);
    const float low = std::min(capsule.a.y, capsule.b.y) - radius;
    const float high = std::max(capsule.a.y, capsule.b.y) + radius;
    return !(high < feet || low > head);
}

// Moves the composed scene's player meshes with the player: the player rig's
// half-open mesh range (compose_scene appends the entities after it, and they
// must not follow the player). `groundOffsetY` is the lift to apply (see
// active_player_lift), so the character stands on the ground point instead of
// sinking into it.
void apply_player_transform(Scene& scene, const RigCpu& rig, const PlayerState& player,
                            float groundOffsetY) {
    const Mat4 model = player_model_matrix(player, groundOffsetY);
    for (size_t i = rig.meshBegin; i < rig.meshEnd && i < scene.meshes.size(); ++i) {
        scene.meshes[i].model = model;
    }
}

// The lift to draw the composed scene's player meshes with while the active
// clip plays: the S3 origin-to-feet lift, except when the clip carries the
// root translation series — that series already holds the character's ground
// height, so lifting on top of it would float the model by the offset
// (~0.76 m for Wander). A model with no clip at all keeps the plain lift.
float active_player_lift(const Scene& scene, const Playback& playback, const RigCpu& rig) {
    if (playback.index < 0) {
        return rig_ground_offset(scene, rig);
    }
    return effective_player_lift(scene, playback.clips[static_cast<size_t>(playback.index)],
                                 rig.meshBegin, rig.meshEnd);
}

// Applies every entity's grounding lift to its model transform, once for the
// whole run (an entity's clock never changes which clip plays). compose_scene
// baked the manifest TRS into each of the entity's mesh matrices; the manifest
// y is the ground sample, so the rig's origin-to-feet lift raises the model's
// origin to `y + lift` — 0 when the entity's clip already carries the root
// series (the S2 rule, shared with the player). The player's meshes are not
// touched here: its transform is rewritten from its own lift path each frame.
void apply_entity_lifts(Scene& scene, const std::vector<Playback>& playbacks) {
    for (std::size_t rigIndex = 0; rigIndex < scene.rigs.size(); ++rigIndex) {
        if (rigIndex == scene.playerRig) {
            continue;
        }
        float lift = rig_ground_offset(scene, scene.rigs[rigIndex]);
        if (rigIndex < playbacks.size() && playbacks[rigIndex].index >= 0) {
            const Playback& playback = playbacks[rigIndex];
            lift = effective_rig_lift(scene, rigIndex,
                                      playback.clips[static_cast<size_t>(playback.index)]);
        }
        apply_rig_lift(scene, rigIndex, lift);
    }
}

// The entity's chest joint in world space for this frame's palette — the
// target-framing camera's orbit point. False when the rig has no Bone_MUNE.
bool entity_chest_position(const Scene& scene, std::size_t rigIndex,
                           const std::vector<Mat4>& palette, Vec3& out) {
    return posed_joint_world_position(scene, rigIndex, palette, "Bone_MUNE", out);
}

// Every entity rig's collision proxy for this frame (S4.0 has one entity; the
// loop keeps the composed order general). The player's rig is skipped: its
// capsule is the controller's own, pinned by collision.h.
std::vector<Capsule> entity_capsules(const Scene& scene,
                                     const std::vector<std::vector<Mat4>>& palettes) {
    std::vector<Capsule> all;
    for (std::size_t rigIndex = scene.playerRig + 1; rigIndex < scene.rigs.size(); ++rigIndex) {
        if (rigIndex >= palettes.size()) {
            break;
        }
        std::vector<Capsule> capsules =
            entity_collision_capsules(scene, rigIndex, palettes[rigIndex]);
        all.insert(all.end(), capsules.begin(), capsules.end());
    }
    return all;
}

// Applies the controller's locomotion choice unless a clip was pinned on the
// command line (--anim or --anim-time): a hard switch — the chosen clip
// restarts at t = 0 — only when the state asks for a different clip. No
// blending, by design.
void update_locomotion(Playback& playback, const Scene& scene, const PlayerInput& input,
                       bool manualClip) {
    if (manualClip) {
        return;
    }
    const int wanted = select_clip_index(playback, input);
    if (wanted >= 0 && wanted != playback.index) {
        select_clip(playback, scene, scene.playerRig, wanted);
    }
}

// --- S4.1 wiring helpers ----------------------------------------------------

// The modes that hold the colossus; every other mode is the follow-camera's.
bool mode_is_attached(PlayerMode mode) {
    return mode == PlayerMode::Grabbing || mode == PlayerMode::Climbing ||
           mode == PlayerMode::Striking;
}

bool finite_position(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// The combat clips the player's mode selector names, resolved once at load.
struct PlayerClips {
    int gripLoop = -1;
    int gripMove = -1;
    int strike = -1;
    int fall = -1;
    int hurtHeavy = -1;
    int hurtHeavyEnd = -1;
    int death = -1;
};

// The entity clips R3 names, resolved once at load. `stand` is the clip the
// manifest itself named (the entity's own loop).
struct EntityClips {
    int stand = -1;
    int head = -1;
    int arm = -1;
    int body = -1;
    int death = -1;
};

PlayerClips resolve_player_clips(const Playback& playback) {
    return PlayerClips{clip_index_named(playback, kGripLoopClip),
                       clip_index_named(playback, kGripMoveClip),
                       clip_index_named(playback, kGripAttackClip),
                       clip_index_named(playback, kFallClip),
                       clip_index_named(playback, kHeavyHurtClip),
                       clip_index_named(playback, kHeavyHurtEndClip),
                       clip_index_named(playback, kPlayerDeathClip)};
}

EntityClips resolve_entity_clips(const Playback& playback, const std::string& standClip) {
    return EntityClips{clip_index_named(playback, standClip),
                       clip_index_named(playback, kHeadReactionClip),
                       clip_index_named(playback, kArmReactionClip),
                       clip_index_named(playback, kBodyReactionClip),
                       clip_index_named(playback, kDeathClip)};
}

// Prints one load error when `name` is absent from `playback`; false means the
// caller must exit 2. The message names the model and the exact clip: the
// fight never silently falls back to the first clip.
bool require_named_clip(const Playback& playback, const std::string& model, const char* name) {
    if (clip_index_named(playback, name) >= 0) {
        return true;
    }
    std::fprintf(stderr, "sotc-viewer: %s carries no %s clip (the fight needs it)\n",
                 model.c_str(), name);
    return false;
}

// The mode-driven clip choice: Grounded keeps the existing locomotion selector,
// the attached modes play their grip/attack clip, a fall/stagger plays the
// damage clip, Hurt plays its medium/heavy reaction and Dead plays the death
// clip. The death clip is pinned and keeps playing: it runs once and holds its
// last frame instead of wrapping. The pin is cleared as soon as any live mode
// is selected again (the R soft reset). `manualClip` (--anim/--anim-time) keeps
// its S2 override meaning.
void update_combat_clips(Playback& playback, const Scene& scene, const PlayerCombat& combat,
                         const PlayerInput& input, bool manualClip) {
    if (manualClip) {
        return;
    }
    const int wanted = select_mode_clip_index(playback, combat, input);
    if (wanted >= 0 && wanted != playback.index) {
        select_clip(playback, scene, scene.playerRig, wanted);
    }
    if (combat.mode == PlayerMode::Dead) {
        // Play once, clamp on the last frame; the pose update advances this
        // clock because it is playing, unlike the entity's reaction clock.
        playback.pinned = true;
        playback.playing = true;
    } else {
        playback.pinned = false;
    }
    // wanted < 0 means the named clip is absent. The load already refused that
    // manifest (require_named_clip), so there is deliberately no fallback here.
}

// This frame's PlayerFrame: the entity's posed skeleton is the only source of
// climb bands, and its capsules are the ground-mode collision proxy. The
// camera look yaw is the Grounded movement basis (the state machine's
// player_update call), exactly as the interactive path always supplied it.
// The weakpoint lookup resolves a band's shell through posed_weakpoint_proxy on
// this frame's palette; `weakpoint_material_name` answers nullptr for anything
// but the two weakpoints, so it must be checked before building a string.
PlayerFrame player_frame_of(const Scene& scene, std::size_t entityRig,
                            const std::vector<std::vector<Mat4>>& palettes,
                            const Heightmap& heightmap, float cameraYawDeg) {
    PlayerFrame frame;
    frame.scene = &scene;
    frame.heightmap = &heightmap;
    frame.entityRig = entityRig;
    frame.cameraYawDeg = cameraYawDeg;
    if (entityRig < palettes.size()) {
        frame.jointWorld = [&scene, entityRig, &palettes](std::string_view name, Vec3& out) {
            return posed_joint_world_position(scene, entityRig, palettes[entityRig],
                                              std::string(name), out);
        };
        frame.weakpointProxy = [&scene, entityRig,
                                &palettes](int weakpoint, Vec3& center, float& radius) {
            const char* material = weakpoint_material_name(weakpoint);
            if (material == nullptr) {
                return false;
            }
            return posed_weakpoint_proxy(scene, entityRig, palettes[entityRig],
                                         std::string(material), center, radius);
        };
    }
    frame.entityCapsules = entity_capsules(scene, palettes);
    return frame;
}

// Enters the climb orbit on the first attached frame (or on F, `recenter`),
// seeded outward along the band's surface normal; every later attached frame
// just keeps the orbit on the attach point. The player position *is* the
// attach point (the state machine wrote the band surface into it). A band that
// cannot be resolved enters with a zero normal, which keeps the current yaw.
void update_climb_camera(Camera& camera, CameraClimb& climb, const PlayerState& player,
                         const PlayerCombat& combat, const PlayerFrame& frame, float dragDx,
                         float dragDy, float wheelDelta, bool recenter, float dtSeconds) {
    if (climb.active && !recenter) {
        camera_climb_update(camera, climb, player.position, dragDx, dragDy, wheelDelta,
                            dtSeconds);
        return;
    }
    Vec3 point{};
    Vec3 normal{};
    if (!climb_surface_point(climb_band(combat.band), frame.jointWorld, combat.bandT,
                             player.position, point, normal)) {
        normal = Vec3{};
    }
    camera_climb_enter(camera, climb, player.position, normal);
}

// Starts one entity one-shot clip (a reaction or the death): pinned so it can
// never loop, with its clock owned by encounter.reactionSeconds (the pose
// update does not advance a non-playing clock). `reactionSeconds == 0` is the
// clip's first frame.
void start_encounter_one_shot(Playback& playback, Encounter& encounter, int clipIndex) {
    if (clipIndex < 0 || static_cast<std::size_t>(clipIndex) >= playback.clips.size()) {
        return;
    }
    playback.index = clipIndex;
    playback.time = 0.0f;
    playback.pinned = true;    // clamp at the last frame, never wrap
    playback.playing = false;  // encounter.reactionSeconds advances this clock
    encounter.reactionClip = clipIndex;
    encounter.reactionSeconds = 0.0f;
}

// Advances the encounter's one-shot clip. A reaction returns to the entity's
// own loop when it finishes; a death freezes on its last frame (pinned, not
// playing) and stays defeated. This is the only writer of a one-shot clock, so
// the pose update and the status agree on the drawn time.
void advance_encounter_reaction(Playback& playback, Encounter& encounter, int standClip,
                                float dtSeconds) {
    if (encounter.reactionClip < 0) {
        return;
    }
    if (!(dtSeconds > 0.0f) || !std::isfinite(dtSeconds)) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(encounter.reactionClip);
    if (index >= playback.clips.size()) {
        encounter.reactionClip = -1;
        encounter.reactionSeconds = 0.0f;
        return;
    }
    const float duration = playback.clips[index].duration;
    encounter.reactionSeconds += dtSeconds;
    if (encounter.reactionSeconds < duration) {
        playback.time = encounter.reactionSeconds;
        return;
    }
    if (encounter.defeated) {
        // Death: play once, then hold the final frame.
        playback.time = duration;
        playback.pinned = true;
        playback.playing = false;
        encounter.reactionClip = -1;
        encounter.reactionSeconds = duration;
        return;
    }
    // Reaction over: back to the entity's own loop from its first frame.
    if (standClip >= 0) {
        playback.index = standClip;
        playback.time = 0.0f;
        playback.pinned = false;
        playback.playing = true;
    }
    encounter.reactionClip = -1;
    encounter.reactionSeconds = 0.0f;
}

// Applies one step's damage to the encounter and routes the entity's reaction:
// weakpoint 0 (forehead) -> the head reaction, 1 (right arm) -> the arm
// reaction, any other band -> the generic body reaction. A killing blow
// supersedes the reaction and starts the death clip. A defeated entity takes
// no further damage and no second death.
void apply_combat_events(Encounter& encounter, Playback& entityPlayback,
                         const EntityClips& clips, const StepEvents& events, int weakpoint) {
    if (events.damageDealt <= 0 || encounter.defeated) {
        return;
    }
    encounter.health =
        std::max(0.0f, encounter.health - static_cast<float>(events.damageDealt));
    if (encounter.health <= 0.0f) {
        encounter.defeated = true;
        start_encounter_one_shot(entityPlayback, encounter, clips.death);
        return;
    }
    const int reaction =
        weakpoint == 1 ? clips.arm : (weakpoint == 0 ? clips.head : clips.body);
    start_encounter_one_shot(entityPlayback, encounter, reaction);
}

// The gamepad's owned input. Polled once per frame in the interactive loop;
// open/close track hot-plug. close() drops the handle and clears every input it
// owned, so an unplug mid-grip cannot latch the grip or a pause press on.
struct GamepadInput {
    SDL_Gamepad* handle = nullptr;
    SDL_JoystickID id = 0;
    float leftX = 0.0f;
    float leftY = 0.0f;
    float rightX = 0.0f;
    float rightY = 0.0f;
    bool grab = false;
    bool strike = false;
    bool pausePressed = false;
    bool pauseWasDown = false;

    ~GamepadInput() { close(); }

    void open(SDL_JoystickID instance) {
        close();
        handle = SDL_OpenGamepad(instance);
        id = handle != nullptr ? SDL_GetGamepadID(handle) : 0;
    }

    void close() {
        if (handle != nullptr) {
            SDL_CloseGamepad(handle);
        }
        handle = nullptr;
        id = 0;
        leftX = leftY = rightX = rightY = 0.0f;
        grab = false;
        strike = false;
        pausePressed = false;
        pauseWasDown = false;
    }

    void poll() {
        leftX = leftY = rightX = rightY = 0.0f;
        grab = false;
        strike = false;
        pausePressed = false;
        if (handle == nullptr) {
            return;
        }
        const auto axis = [this](SDL_GamepadAxis which) {
            return static_cast<float>(SDL_GetGamepadAxis(handle, which)) / 32767.0f;
        };
        leftX = axis(SDL_GAMEPAD_AXIS_LEFTX);
        leftY = axis(SDL_GAMEPAD_AXIS_LEFTY);
        rightX = axis(SDL_GAMEPAD_AXIS_RIGHTX);
        rightY = axis(SDL_GAMEPAD_AXIS_RIGHTY);
        grab = SDL_GetGamepadAxis(handle, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16384;
        strike = SDL_GetGamepadButton(handle, SDL_GAMEPAD_BUTTON_SOUTH);
        const bool pauseNow = SDL_GetGamepadButton(handle, SDL_GAMEPAD_BUTTON_START);
        pausePressed = pauseNow && !pauseWasDown;
        pauseWasDown = pauseNow;
    }
};

// Opens the first already-connected gamepad (a later hot-plug arrives as an
// SDL_EVENT_GAMEPAD_ADDED in the loop).
void open_first_gamepad(GamepadInput& gamepad) {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids == nullptr) {
        return;
    }
    for (int i = 0; i < count && gamepad.handle == nullptr; ++i) {
        gamepad.open(ids[i]);
    }
    SDL_free(ids);
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
// The messages carry no "sotc-viewer: " prefix — run_app's catch adds it once.
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
    // --entity-time pins an entity's clip; entities only exist in a composed
    // manifest scene, so the flag without one is a usage error, not a no-op.
    if (opts.entityTimeSet && opts.manifestPath.empty()) {
        std::fprintf(stderr,
                     "sotc-viewer: --entity-time needs --manifest (there is no entity to pin)\n");
        return 2;
    }
    // --camera-target entity needs a composed scene with an entity to frame.
    if (opts.cameraTarget != "player" && opts.cameraTarget != "entity") {
        std::fprintf(stderr, "sotc-viewer: --camera-target expects player or entity, got: %s\n",
                     opts.cameraTarget.c_str());
        return 2;
    }
    if (opts.cameraTarget == "entity" && opts.manifestPath.empty()) {
        std::fprintf(stderr,
                     "sotc-viewer: --camera-target entity needs --manifest (there is no entity)\n");
        return 2;
    }

    // Cursor policy, part 1: SDL must never auto-capture the mouse when the
    // window gains focus. Set the hint before SDL_Init so the video subsystem
    // starts with it in place.
    if (!SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0")) {
        std::fprintf(stderr, "sotc-viewer: could not set %s=0\n", SDL_HINT_MOUSE_AUTO_CAPTURE);
        return 1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
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
        // The entity prop: loaded only in a composed scene with an entity (see
        // load_club); identity/unloaded everywhere else.
        ClubAttachment club{};
        SceneManifest manifest{};          // empty when --manifest is absent
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
            if (opts.entityTimeSet && manifest.entities.empty()) {
                std::fprintf(stderr, "sotc-viewer: --entity-time: the manifest has no entity\n");
                return 2;
            }
            if (opts.cameraTarget == "entity" && manifest.entities.empty()) {
                std::fprintf(stderr,
                             "sotc-viewer: --camera-target entity: the manifest has no entity\n");
                return 2;
            }
            scene = compose_scene(opts.libraryDir, manifest, opts.modelRelPath);

            // The ground is built from the scene's static meshes only: the
            // player's own body and the placed entities must never rasterize
            // into the ground they stand on. A scene with no ground at all (no
            // static meshes) leaves the heightmap empty — ground=none, no
            // clamp. This runs before the club is appended: the club is static
            // geometry (rig = -1) but it is held in the air, not terrain, and
            // must never raise the heightmap under Valus.
            try {
                heightmap = build_heightmap(scene, kHeightmapCellSize);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "sotc-viewer: no ground: %s\n", error.what());
            }

            // S4.2: the club, in a manifest scene that has an entity. The
            // renderer uploads its mesh list exactly once, so the append must
            // precede upload().
            if (!manifest.entities.empty()) {
                club = load_club(scene, opts.libraryDir, scene.playerRig + 1,
                                 manifest.entities[0].placement.glbRelPath);
            }
            renderer.upload(scene);
            print_scene_line(scene);
        } else if (!opts.modelRelPath.empty()) {
            scene = load_scene(opts.libraryDir, opts.modelRelPath);
            renderer.upload(scene);
            print_scene_line(scene);
        }

        // The player's rig record: its mesh range is what apply_player_transform
        // moves and what the ground lift is measured from (the entities' own
        // rigs follow in their tasks).
        const RigCpu& player_rig_record = player_rig(scene);

        // The real camera: framed on the scene bounds at load (and again on F).
        Camera camera{};
        camera_frame(camera, scene.boundsMin, scene.boundsMax);

        // The player and its follow camera (manifest mode only): spawn at the
        // manifest's spawn or the bounds centre, with the camera snapped behind
        // the player (a huge dt makes camera_follow's first blend factor
        // exactly 1 in float arithmetic; later frames smooth).
        PlayerState player{spawn_position(manifest, scene),
                           manifest.hasSpawn ? manifest.spawnYawDeg : 0.0f};
        // The combat state machine and the encounter's own state (R5) live for
        // the whole run; the climb camera is entered/left by the mode.
        PlayerCombat combat{};
        Encounter encounter{};
        CameraClimb climbCamera{};
        CameraFollow follow;
        if (follow_mode) {
            camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f, 0.0f,
                          /*moving=*/false, /*dtSeconds=*/10.0f);
        }
        // Entity target framing (T): the orbit point is refreshed from the
        // entity's posed chest every frame. The state is inert until entered.
        CameraTarget targetCamera;
        Vec3 entityChest{};
        bool haveEntityChest = false;

        // Playback: one per rig (S4). The player's slot keeps the S2 meaning —
        // the model's real clips first, the procedural wave only as the
        // fallback for a skinned model that carries none; --anim picks a clip
        // by name or index, and --anim-time pins the time and starts paused
        // (deterministic captures). Each entity's rig loops the exact clip its
        // manifest line named (compose_scene already refused a load whose model
        // does not carry it); --entity-time pins that clock, clamped into the
        // clip. A scene with no rig at all (a model-less run) keeps one slot for
        // the player path, which stays silent.
        const std::size_t playerRig = scene.rigs.empty() ? 0 : scene.playerRig;
        const std::size_t entityRig = scene.playerRig + 1;  // S4.0: one entity
        std::vector<Playback> playbacks(std::max<std::size_t>(scene.rigs.size(), 1));
        Playback& playback = playbacks[playerRig];
        build_playback(scene, playerRig, playback);
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
        // The entities follow the player in rig order (compose_scene's pinned
        // order: instances, player, entities). Each names its clip exactly.
        for (std::size_t entity = 0; entity < manifest.entities.size(); ++entity) {
            const std::size_t rigIndex = entity + 1;
            if (rigIndex >= playbacks.size()) {
                break;  // compose_scene pairs every entity with a rig
            }
            Playback& entity_playback = playbacks[rigIndex];
            build_playback(scene, rigIndex, entity_playback);
            if (const int index = clip_index_named(entity_playback, manifest.entities[entity].clip);
                index >= 0) {
                entity_playback.index = index;
            }
            if (opts.entityTimeSet && entity_playback.index >= 0) {
                const AnimationCpu& named =
                    entity_playback.clips[static_cast<std::size_t>(entity_playback.index)];
                entity_playback.time = animation_clamp_time(opts.entityTime, named.duration);
                entity_playback.playing = false;
                entity_playback.pinned = true;  // the capture samples this exact time
            }
        }
        // --s4-selftest pins the entity at t = 0 (the idle's first frame): the
        // walk's collision proxy and the status bbox must sample one known pose.
        if (opts.s4Selftest) {
            if (manifest.entities.empty() || entityRig >= scene.rigs.size() ||
                entityRig >= playbacks.size()) {
                std::fprintf(stderr,
                             "sotc-viewer: --s4-selftest: the manifest has no entity\n");
                return 2;
            }
            Playback& entity_playback = playbacks[entityRig];
            if (entity_playback.index < 0) {
                std::fprintf(stderr,
                             "sotc-viewer: --s4-selftest: the entity has no clip to pin\n");
                return 2;
            }
            entity_playback.time = 0.0f;
            entity_playback.playing = false;
            entity_playback.pinned = true;
        }
        for (std::size_t rigIndex = 0; rigIndex < playbacks.size(); ++rigIndex) {
            print_anim_status(scene, rigIndex, playbacks[rigIndex]);
        }

        // S4.1: a manifest with an entity is an encounter. Every clip the climb
        // and fight name must exist before the run starts — a missing one is a
        // load error naming the glb and the clip, never a silent fallback to the
        // first clip. (S3's arena has no entity and needs none of them.)
        PlayerClips playerClips{};
        EntityClips entityClips{};
        if (follow_mode && !manifest.entities.empty()) {
            playerClips = resolve_player_clips(playback);
            if (!require_named_clip(playback, opts.modelRelPath, kGripLoopClip) ||
                !require_named_clip(playback, opts.modelRelPath, kGripMoveClip) ||
                !require_named_clip(playback, opts.modelRelPath, kGripAttackClip) ||
                !require_named_clip(playback, opts.modelRelPath, kFallClip) ||
                !require_named_clip(playback, opts.modelRelPath, kHeavyHurtClip) ||
                !require_named_clip(playback, opts.modelRelPath, kHeavyHurtEndClip) ||
                !require_named_clip(playback, opts.modelRelPath, kPlayerDeathClip)) {
                return 2;
            }
            if (entityRig < playbacks.size()) {
                entityClips = resolve_entity_clips(playbacks[entityRig],
                                                   manifest.entities[0].clip);
                const std::string& entityModel = manifest.entities[0].placement.glbRelPath;
                if (!require_named_clip(playbacks[entityRig], entityModel,
                                        manifest.entities[0].clip.c_str()) ||
                    !require_named_clip(playbacks[entityRig], entityModel, kHeadReactionClip) ||
                    !require_named_clip(playbacks[entityRig], entityModel, kArmReactionClip) ||
                    !require_named_clip(playbacks[entityRig], entityModel, kBodyReactionClip) ||
                    !require_named_clip(playbacks[entityRig], entityModel, kDeathClip)) {
                    return 2;
                }
            }
        }

        // Single-model framing is on the pose, after --anim/--anim-time resolved
        // the start state: the clip's root series carries the ground height, so
        // the bind-pose bounds framed at load would leave the subject out of
        // frame. Manifest mode keeps its follow camera and bind-bounds framing.
        if (!follow_mode) {
            frame_single_model(camera, scene, playback);
        }

        // A clip pinned on the command line keeps manual control; otherwise the
        // player's controller state selects the loop (stand/walk/run) each
        // frame. --anim-time pins a deterministic capture, so it counts as
        // manual too: a selection must not reset its time.
        const bool manual_clip = !opts.animClip.empty() || opts.animTimeSet;

        // The composed scene's initial placement: every entity's grounding lift
        // lands on its model transform once (which clip plays never changes),
        // and the player's transform follows its controller every frame.
        // A single-model load has no entity rig, so nothing moves here.
        apply_entity_lifts(scene, playbacks);

        // The target-framing capture starts here: sample the entity's chest at
        // the initial playback time (before the first frame advances it) so the
        // very first camera update already frames the subject.
        if (follow_mode && entityRig < scene.rigs.size() && entityRig < playbacks.size() &&
            playbacks[entityRig].index >= 0) {
            const Playback& entity_playback = playbacks[entityRig];
            const AnimationCpu& clip =
                entity_playback.clips[static_cast<std::size_t>(entity_playback.index)];
            const std::vector<Mat4> palette = animation_pose(
                scene, entityRig, clip, playback_time(entity_playback, clip.duration));
            haveEntityChest = entity_chest_position(scene, entityRig, palette, entityChest);
        }
        if (opts.cameraTarget == "entity") {
            if (!haveEntityChest) {
                std::fprintf(stderr,
                             "sotc-viewer: --camera-target entity: no Bone_MUNE in the entity\n");
                return 2;
            }
            camera_target_enter(camera, follow, targetCamera, entityChest);
        }

        // The initial player transform (also the one --selftest renders with):
        // the first clip already is the active one.
        if (follow_mode) {
            apply_player_transform(scene, player_rig_record, player,
                                   active_player_lift(scene, playback, player_rig_record));
            // The HUD is a manifest-mode overlay (S4.1): both pools start full.
            // The interactive loop refreshes it every frame; the scripted paths
            // render their final frame with these starting values.
            renderer.set_hud(HudState{true, combat.stamina / 100.0f,
                                      encounter.health / 100.0f});
        }

        if (opts.s41Selftest) {
            // The deterministic S4.1 acceptance timeline: S4's approach, a real
            // grab, a 240-frame climb with a proven rise, a full-charge strike
            // on the head weakpoint, a catch, a deliberately missed catch, a
            // forced stamina fall, and the killing blow. Fixed 1/60 s steps and
            // no window input; any failed invariant exits 1 with a specific
            // message. The only teleport is the ruled scaffolding that places
            // the player on the head band's surface for the strike phases — the
            // user's walk does the whole climb.
            if (manifest.entities.empty() || entityRig >= scene.rigs.size() ||
                entityRig >= playbacks.size()) {
                std::fprintf(stderr,
                             "sotc-viewer: --s41-selftest: the manifest has no entity\n");
                return 2;
            }
            if (entityClips.stand < 0 || entityClips.head < 0 || entityClips.arm < 0 ||
                entityClips.body < 0 || entityClips.death < 0 || playerClips.gripLoop < 0 ||
                playerClips.gripMove < 0 || playerClips.strike < 0 || playerClips.fall < 0) {
                // The load gate above already refused this case; keep the
                // invariant explicit so no future path can reach a fallback.
                throw std::runtime_error(
                    "s41-selftest: a combat clip is missing despite the load gate");
            }

            Playback& entity_playback = playbacks[entityRig];
            entity_playback.index = entityClips.stand;
            entity_playback.time = 0.0f;
            entity_playback.playing = false;
            entity_playback.pinned = true;  // one known idle pose for the approach

            const CombatTuning tuning{};
            const float stepSeconds = kFixedFrameSeconds;
            int strikes = 0;
            ClimbBandId grabBand = ClimbBandId::Count;
            bool grabBandSet = false;

            // One frame of the fight: the entity's one-shot clock, the player's
            // mode clip, this frame's poses, the PlayerFrame, the step, the
            // encounter events, and the camera. Every phase below goes through
            // this so the attach point always samples the pose the step saw.
            auto step_frame = [&](const PlayerInput& input) -> StepEvents {
                advance_encounter_reaction(entity_playback, encounter, entityClips.stand,
                                           stepSeconds);
                update_combat_clips(playback, scene, combat, input, /*manualClip=*/false);
                const std::vector<std::vector<Mat4>> palettes =
                    update_all_poses(renderer, scene, playbacks, stepSeconds, club);
                const PlayerFrame frame =
                    player_frame_of(scene, entityRig, palettes, heightmap,
                                    camera_look_yaw(camera));
                const int weakpoint = combat.weakpoint;  // detach() clears it
                const StepEvents events =
                    player_step(player, combat, input, frame, tuning, stepSeconds);
                if (events.damageDealt > 0) {
                    ++strikes;
                }
                apply_combat_events(encounter, entity_playback, entityClips, events, weakpoint);
                if (mode_is_attached(combat.mode)) {
                    update_climb_camera(camera, climbCamera, player, combat, frame, 0.0f, 0.0f,
                                        0.0f, /*recenter=*/false, stepSeconds);
                } else {
                    if (climbCamera.active) {
                        camera_climb_exit(camera, climbCamera);
                    }
                    camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f,
                                  0.0f,
                                  input.forward || input.back || input.left || input.right,
                                  stepSeconds);
                }
                return events;
            };

            // --s41-stills: one rendered PNG at a named phase. The frame is
            // rebuilt from the current playback/combat state, so a still shows
            // exactly what the timeline has reached (a mid-climb grip, a held
            // charge, a head reaction); the final defeat still is written by
            // the last phase. `hudVisible` also lets the acceptance render its
            // HUD control from the same frame.
            const auto still_dir = [&]() {
                std::string dir = opts.s41StillsDir;
                if (!dir.empty() && dir.back() != '/') {
                    dir += '/';
                }
                return dir;
            };
            const auto still_path = [&](const char* name) {
                return still_dir() + name + ".png";
            };
            const auto control_path = [&](const char* name) {
                return still_dir() + "control/" + name + ".png";
            };
            const auto render_still = [&](const std::string& path, bool hudVisible) {
                // Select the mode's clip before posing: the step's own clip
                // selection runs before the step, so the grab frame still
                // carries the locomotion clip (the one-frame lag Task 6
                // records). A still is evidence of the mode, so it samples
                // the mode clip at its current time (0 on entry).
                const PlayerInput no_input;
                update_combat_clips(playback, scene, combat, no_input, /*manualClip=*/false);
                update_all_poses(renderer, scene, playbacks, 0.0f, club);
                apply_player_transform(scene, player_rig_record, player,
                                       active_player_lift(scene, playback, player_rig_record));
                renderer.set_hud(HudState{hudVisible, combat.stamina / 100.0f,
                                          encounter.health / 100.0f});
                renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                                      /*acquire_swapchain=*/true);
                write_screenshot(renderer, shell.win, path);
            };
            const auto capture_still = [&](const char* phase, const std::string& path) {
                if (opts.s41StillsDir.empty()) {
                    return;
                }
                std::printf("s41-still phase=%s path=%s\n", phase, path.c_str());
                render_still(path, /*hudVisible=*/true);
            };
            const auto capture_control = [&](const char* phase, const std::string& path,
                                             bool hudVisible) {
                if (opts.s41StillsDir.empty()) {
                    return;
                }
                std::printf("s41-control phase=%s path=%s\n", phase, path.c_str());
                render_still(path, hudVisible);
            };
            // The player's own projected bounds for a still. The picture is the
            // authority, but a radius regression that hides the player inside
            // the head is exactly the kind of bug a bytes+span check cannot
            // see; the acceptance asserts this box is non-empty and on-screen.
            // `w`/`h` are inclusive pixel counts (x + w is the right edge + 1).
            const auto print_player_px = [&](const char* phase) {
                const std::vector<std::vector<Mat4>> palettes =
                    update_all_poses(renderer, scene, playbacks, 0.0f, club);
                if (scene.playerRig >= palettes.size()) {
                    throw std::runtime_error(
                        std::string("s41-selftest: the player rig has no pose for ") + phase);
                }
                apply_player_transform(scene, player_rig_record, player,
                                       active_player_lift(scene, playback, player_rig_record));
                int pixel_width = 0;
                int pixel_height = 0;
                if (!SDL_GetWindowSizeInPixels(shell.win, &pixel_width, &pixel_height)) {
                    throw std::runtime_error(std::string("SDL_GetWindowSizeInPixels: ") +
                                             SDL_GetError());
                }
                const ScreenBox box = project_rig_bounds(
                    scene, scene.playerRig, palettes[scene.playerRig],
                    camera_view_proj(camera, shell.win), pixel_width, pixel_height);
                if (!box.any) {
                    throw std::runtime_error(
                        std::string("s41-selftest: the player has no projected screen region in ") +
                        phase);
                }
                std::printf("s41-%s player_px=%d,%d,%d,%d\n", phase, box.x0, box.y0,
                            box.x1 - box.x0 + 1, box.y1 - box.y0 + 1);
            };

            // The S4 approach, unchanged in shape: walk into the nearest
            // vertically reachable leg capsule until the resolver stops the
            // player. It ends at the knee, so a second fixed-aim walk closes the
            // final couple of metres to the foot band the grab can reach.
            auto walk_toward = [&](float aimYawDeg, int maxFrames) -> int {
                PlayerInput input;
                input.forward = true;
                input.run = true;
                int frames = 0;
                int still = 0;
                bool stopped = false;
                while (frames < maxFrames) {
                    const Vec3 previous = player.position;
                    player_update(player, input, stepSeconds, heightmap, aimYawDeg);
                    update_locomotion(playback, scene, input, /*manualClip=*/false);
                    const std::vector<std::vector<Mat4>> palettes =
                        update_all_poses(renderer, scene, playbacks, stepSeconds, club);
                    resolve_player_collision(player, entity_capsules(scene, palettes), heightmap);
                    camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f,
                                  0.0f, /*moving=*/true, stepSeconds);
                    const float dx = player.position.x - previous.x;
                    const float dz = player.position.z - previous.z;
                    ++frames;
                    if (std::sqrt(dx * dx + dz * dz) < kS4StopEpsilon) {
                        if (++still >= kS4StopFrames) {
                            stopped = true;
                            break;
                        }
                    } else {
                        still = 0;
                    }
                }
                if (!stopped) {
                    throw std::runtime_error("s41-selftest: the approach walked " +
                                             std::to_string(frames) +
                                             " frames without reaching a collision stop");
                }
                if (!finite_position(player.position)) {
                    throw std::runtime_error(
                        "s41-selftest: the approach left a non-finite player position");
                }
                const float ground = heightmap_sample(heightmap, player.position);
                if (!std::isfinite(ground) ||
                    std::fabs(player.position.y - ground) > kS4GroundTolerance) {
                    throw std::runtime_error(
                        "s41-selftest: the approach left the ground (y " +
                        std::to_string(player.position.y) + ", ground " +
                        std::to_string(ground) + ")");
                }
                return frames;
            };

            // --- 1. Approach -------------------------------------------------
            std::vector<std::vector<Mat4>> palettes =
                update_all_poses(renderer, scene, playbacks, 0.0f, club);
            if (entityRig >= palettes.size()) {
                throw std::runtime_error("s41-selftest: the entity rig has no pose");
            }
            const std::vector<Capsule> capsules =
                entity_collision_capsules(scene, entityRig, palettes[entityRig]);
            if (capsules.empty()) {
                throw std::runtime_error(
                    "s41-selftest: the entity rig has no collision capsules");
            }

            const float feet = player.position.y;
            const float head = feet + kPlayerCapsuleHeight;
            Vec3 aim{};
            float aimDistance = std::numeric_limits<float>::infinity();
            for (const Capsule& capsule : capsules) {
                if (!capsule_overlaps_vertically(capsule, feet, head)) {
                    continue;
                }
                Vec3 closest{};
                const float distance =
                    capsule_horizontal_distance(capsule, player.position, closest);
                if (distance < aimDistance) {
                    aimDistance = distance;
                    aim = closest;
                }
            }
            const float aimDx = aim.x - player.position.x;
            const float aimDz = aim.z - player.position.z;
            const float aimLength = std::sqrt(aimDx * aimDx + aimDz * aimDz);
            if (!std::isfinite(aimDistance) || !(aimLength > 0.0f)) {
                throw std::runtime_error(
                    "s41-selftest: no reachable entity capsule to walk into");
            }
            const float aimYaw = std::atan2(aimDx, aimDz) / kDegToRad;
            const int approachFrames = walk_toward(aimYaw, kS4MaxWalkFrames);

            // The foot-band walk: the S4 stop is at the knee, so aim at the
            // nearest band's surface for a fixed extra leg. The resolver stops
            // the player on the body; the grab range check below is the gate.
            palettes = update_all_poses(renderer, scene, playbacks, 0.0f, club);
            PlayerFrame footFrame =
                player_frame_of(scene, entityRig, palettes, heightmap, aimYaw);
            const ClimbBandId approachBand =
                nearest_climb_band(footFrame.jointWorld, player.position, 1000.0f);
            Vec3 footPoint{};
            Vec3 footNormal{};
            if (approachBand == ClimbBandId::Count ||
                !climb_surface_point(climb_band(approachBand), footFrame.jointWorld, 0.5f,
                                     player.position, footPoint, footNormal)) {
                throw std::runtime_error(
                    "s41-selftest: the entity has no resolvable climb band");
            }
            const float footDx = footPoint.x - player.position.x;
            const float footDz = footPoint.z - player.position.z;
            if (!(std::sqrt(footDx * footDx + footDz * footDz) > 1e-3f)) {
                throw std::runtime_error(
                    "s41-selftest: the foot band is under the player's first stop");
            }
            const float footYaw = std::atan2(footDx, footDz) / kDegToRad;
            const int footFrames = walk_toward(footYaw, kS41ApproachFrames);

            // --- 2. Grab -----------------------------------------------------
            palettes = update_all_poses(renderer, scene, playbacks, 0.0f, club);
            PlayerFrame reachFrame =
                player_frame_of(scene, entityRig, palettes, heightmap, footYaw);
            if (nearest_climb_band(reachFrame.jointWorld, player.position, tuning.grabRange) ==
                ClimbBandId::Count) {
                throw std::runtime_error(
                    "s41-selftest: the approach stopped out of grab range of every climb "
                    "band");
            }
            PlayerInput grabInput;
            grabInput.grab = true;
            const StepEvents grabEvents = step_frame(grabInput);
            if (!grabEvents.grabbed || combat.mode != PlayerMode::Grabbing ||
                combat.band == ClimbBandId::Count) {
                throw std::runtime_error(
                    "s41-selftest: the grab did not attach (mode " +
                    std::string(player_mode_name(combat.mode)) + ")");
            }
            if (!finite_position(player.position)) {
                throw std::runtime_error("s41-selftest: the grab wrote a non-finite position");
            }
            grabBand = combat.band;
            grabBandSet = true;
            std::printf("s41-grab frames=%d band=%s pos=%.3f,%.3f,%.3f\n",
                        approachFrames + footFrames, climb_band_name(combat.band),
                        player.position.x, player.position.y, player.position.z);
            // The first grab's surface point is a known reachable spot on the
            // open side of the foot; the post-landing re-grab scaffolding
            // reuses it as the surface hint (see phase 7).
            const Vec3 firstGrabPosition = player.position;
            capture_still("grab", still_path("grab"));

            // --- 3. Climb ----------------------------------------------------
            const float climbFromY = player.position.y;
            float previousY = climbFromY;
            PlayerInput climbInput;
            climbInput.grab = true;
            climbInput.forward = true;
            for (int frame = 0; frame < kS41ClimbFrames; ++frame) {
                const StepEvents events = step_frame(climbInput);
                if (combat.mode != PlayerMode::Climbing) {
                    throw std::runtime_error(
                        "s41-selftest: the climb left Climbing at frame " +
                        std::to_string(frame) + " (mode " +
                        std::string(player_mode_name(combat.mode)) + ", gripBroke " +
                        (events.gripBroke ? "1" : "0") + ")");
                }
                if (!finite_position(player.position)) {
                    throw std::runtime_error(
                        "s41-selftest: the climb wrote a non-finite position at frame " +
                        std::to_string(frame));
                }
                if (player.position.y + kS41Eps < previousY) {
                    throw std::runtime_error(
                        "s41-selftest: the climb went backwards at frame " +
                        std::to_string(frame) + " (" + std::to_string(previousY) + " -> " +
                        std::to_string(player.position.y) + ")");
                }
                previousY = player.position.y;
                if (frame == kS41ClimbFrames / 2) {
                    capture_still("mid-climb", still_path("mid-climb"));
                }
            }
            const float climbRise = player.position.y - climbFromY;
            if (!std::isfinite(climbRise) || climbRise < kS41MinRise) {
                throw std::runtime_error(
                    "s41-selftest: the climb rose " + std::to_string(climbRise) +
                    " m in " + std::to_string(kS41ClimbFrames) + " frames (need " +
                    std::to_string(kS41MinRise) + " m)");
            }
            std::printf("s41-climb frames=%d from_y=%.3f to_y=%.3f band=%s\n",
                        kS41ClimbFrames, climbFromY, player.position.y,
                        climb_band_name(combat.band));

            // The ruled scaffolding for the strike phases: place the player on
            // the head band's surface (weakpoint 0) and re-attach there. The
            // surface hint points from the head's front towards the spawn (the
            // side the forehead shell faces), not from the last shin position:
            // the latter is a sideways radial where the head's side is wider
            // than the band, and it hid the player inside the shell. The real
            // walk climbs the whole route from the neck, on this front side.
            auto teleport_to_head = [&]() {
                const std::vector<std::vector<Mat4>> now =
                    update_all_poses(renderer, scene, playbacks, 0.0f, club);
                const PlayerFrame frame =
                    player_frame_of(scene, entityRig, now, heightmap, 0.0f);
                Vec3 hint = player.position;
                if (!manifest.entities.empty()) {
                    const Vec3 entity_pos = manifest.entities.front().placement.position;
                    const float facing_x = manifest.spawnPosition.x - entity_pos.x;
                    const float facing_z = manifest.spawnPosition.z - entity_pos.z;
                    const float facing_length =
                        std::sqrt(facing_x * facing_x + facing_z * facing_z);
                    Vec3 kubi{};
                    Vec3 kao{};
                    if (facing_length > 1e-3f && entityRig < now.size() &&
                        posed_joint_world_position(scene, entityRig, now[entityRig],
                                                   "Bone_KUBI", kubi) &&
                        posed_joint_world_position(scene, entityRig, now[entityRig],
                                                   "Bone_KAO", kao)) {
                        const float mid_x = (kubi.x + kao.x) * 0.5f;
                        const float mid_y = (kubi.y + kao.y) * 0.5f;
                        const float mid_z = (kubi.z + kao.z) * 0.5f;
                        hint = Vec3{mid_x + facing_x / facing_length * 6.0f, mid_y,
                                    mid_z + facing_z / facing_length * 6.0f};
                    }
                }
                Vec3 point{};
                Vec3 normal{};
                if (!climb_surface_point(climb_band(ClimbBandId::Head), frame.jointWorld, 0.5f,
                                         hint, point, normal)) {
                    throw std::runtime_error(
                        "s41-selftest: the head band has no surface point to teleport to");
                }
                player.position = point;
                player.yawDeg = std::atan2(-normal.x, -normal.z) / kDegToRad;
                combat.mode = PlayerMode::Grabbing;
                combat.modeSeconds = 0.0f;
                combat.band = ClimbBandId::Head;
                combat.bandT = 0.5f;
                combat.weakpoint = climb_band(ClimbBandId::Head).weakpoint;
                combat.charge = 0.0f;
                combat.strikeHit = false;
                combat.catchSecondsLeft = 0.0f;
            };

            // --- 4. Full-charge strike on the forehead -----------------------
            teleport_to_head();
            PlayerInput holdGrab;
            holdGrab.grab = true;
            const StepEvents attachedAtHead = step_frame(holdGrab);
            if (combat.mode != PlayerMode::Grabbing ||
                combat.band != ClimbBandId::Head || combat.weakpoint != 0) {
                throw std::runtime_error(
                    "s41-selftest: the head teleport did not hold the head band");
            }
            (void)attachedAtHead;
            const int strikeWeakpoint = combat.weakpoint;

            PlayerInput chargeInput;
            chargeInput.grab = true;
            chargeInput.strike = true;
            for (int frame = 0; frame < kS41ChargeFrames; ++frame) {
                const StepEvents events = step_frame(chargeInput);
                if (events.damageDealt != 0) {
                    throw std::runtime_error(
                        "s41-selftest: the strike dealt damage while still charging at "
                        "frame " + std::to_string(frame));
                }
                if (combat.mode != PlayerMode::Striking) {
                    throw std::runtime_error(
                        "s41-selftest: the charge left Striking at frame " +
                        std::to_string(frame) + " (mode " +
                        std::string(player_mode_name(combat.mode)) + ")");
                }
            }
            if (combat.charge + 1e-4f < tuning.chargeSeconds) {
                throw std::runtime_error(
                    "s41-selftest: the charge reached only " + std::to_string(combat.charge) +
                    " s (need " + std::to_string(tuning.chargeSeconds) + " s)");
            }
            capture_still("strike", still_path("strike"));
            print_player_px("strike");

            const int healthBeforeFirstHit = static_cast<int>(std::lround(encounter.health));
            PlayerInput releaseInput;
            releaseInput.grab = true;
            releaseInput.strike = false;
            const StepEvents firstHit = step_frame(releaseInput);
            if (firstHit.damageDealt != static_cast<int>(std::lround(tuning.damageFull))) {
                throw std::runtime_error(
                    "s41-selftest: the full strike dealt " +
                    std::to_string(firstHit.damageDealt) + " damage, not " +
                    std::to_string(static_cast<int>(std::lround(tuning.damageFull))));
            }
            if (!firstHit.gripBroke || combat.mode != PlayerMode::Falling) {
                throw std::runtime_error(
                    "s41-selftest: the landed strike did not shake the player into Falling");
            }
            if (std::fabs(combat.catchSecondsLeft - tuning.catchSeconds) > 1e-4f) {
                throw std::runtime_error(
                    "s41-selftest: the catch window is " +
                    std::to_string(combat.catchSecondsLeft) + " s, not " +
                    std::to_string(tuning.catchSeconds) + " s");
            }
            if (encounter.defeated || entity_playback.index != entityClips.head) {
                throw std::runtime_error(
                    "s41-selftest: the head hit did not play the head reaction");
            }
            std::printf("s41-strike swing=1 damage=%d health=%d->%d weakpoint=%d\n",
                        firstHit.damageDealt, healthBeforeFirstHit,
                        static_cast<int>(std::lround(encounter.health)), strikeWeakpoint);

            // --- 5. Catch with a fresh grab press -----------------------------
            PlayerInput noInput;
            const StepEvents falling = step_frame(noInput);  // release the key
            if (combat.mode != PlayerMode::Falling) {
                throw std::runtime_error(
                    "s41-selftest: the fall left Falling before the catch");
            }
            (void)falling;
            PlayerInput catchInput;
            catchInput.grab = true;
            const StepEvents caught = step_frame(catchInput);
            if (!caught.grabbed || combat.mode != PlayerMode::Grabbing ||
                combat.band == ClimbBandId::Count) {
                throw std::runtime_error(
                    "s41-selftest: the fresh grab press did not catch a band (mode " +
                    std::string(player_mode_name(combat.mode)) + ")");
            }
            if (!finite_position(player.position)) {
                throw std::runtime_error("s41-selftest: the catch wrote a non-finite position");
            }
            std::printf("s41-catch band=%s\n", climb_band_name(combat.band));
            // The reaction still is the re-grip frame: Valus's head reaction
            // is still playing, the HUD reads 66, and Wander is visible on the
            // front of the head (the pre-R10 still at the release point was
            // occluded by the shell).
            capture_still("reaction", still_path("reaction"));
            // The HUD control for the reaction still: the same frame without
            // the overlay, so the acceptance can prove both bars were drawn.
            capture_control("reaction-nohud", control_path("reaction-nohud"),
                            /*hudVisible=*/false);

            // --- 6. Deliberately miss the next catch -------------------------
            // A one-third charge is a weak hit (20 damage): it still breaks the
            // grip and opens the window, which this phase lets expire.
            PlayerInput weakCharge;
            weakCharge.grab = true;
            weakCharge.strike = true;
            for (int frame = 0; frame < kS41WeakChargeFrames; ++frame) {
                const StepEvents events = step_frame(weakCharge);
                if (events.damageDealt != 0) {
                    throw std::runtime_error(
                        "s41-selftest: the weak strike dealt damage while charging");
                }
            }
            PlayerInput weakRelease;
            weakRelease.grab = true;
            const int healthBeforeWeakHit = static_cast<int>(std::lround(encounter.health));
            const int weakWeakpoint = combat.weakpoint;  // the catch band's; -1 off a weakpoint
            const StepEvents weakHit = step_frame(weakRelease);
            if (weakHit.damageDealt != static_cast<int>(std::lround(tuning.damageWeak))) {
                throw std::runtime_error(
                    "s41-selftest: the weak strike dealt " +
                    std::to_string(weakHit.damageDealt) + " damage, not " +
                    std::to_string(static_cast<int>(std::lround(tuning.damageWeak))));
            }
            if (!weakHit.gripBroke || combat.mode != PlayerMode::Falling) {
                throw std::runtime_error(
                    "s41-selftest: the weak hit did not start the missed-catch fall");
            }
            std::printf("s41-strike swing=2 damage=%d health=%d->%d weakpoint=%d\n",
                        weakHit.damageDealt, healthBeforeWeakHit,
                        static_cast<int>(std::lround(encounter.health)), weakWeakpoint);

            // 0.7 s without a press: the 0.6 s window is gone before the ground.
            int fallFrames = 0;
            for (int frame = 0; frame < kS41CatchWaitFrames; ++frame) {
                const StepEvents events = step_frame(noInput);
                ++fallFrames;
                if (combat.mode != PlayerMode::Falling || events.landed) {
                    throw std::runtime_error(
                        "s41-selftest: the deliberate miss left Falling inside the catch "
                        "window");
                }
            }
            if (combat.catchSecondsLeft > 0.0f) {
                throw std::runtime_error(
                    "s41-selftest: the catch window is still open after 0.7 s");
            }
            bool landed = false;
            while (fallFrames < kS41MaxFallFrames) {
                const StepEvents events = step_frame(noInput);
                ++fallFrames;
                if (events.landed) {
                    landed = true;
                    break;
                }
                if (combat.mode != PlayerMode::Falling) {
                    throw std::runtime_error(
                        "s41-selftest: the fall left Falling without a landing (mode " +
                        std::string(player_mode_name(combat.mode)) + ")");
                }
                if (!finite_position(player.position)) {
                    throw std::runtime_error(
                        "s41-selftest: the fall wrote a non-finite position");
                }
            }
            if (!landed || combat.mode != PlayerMode::Staggered) {
                throw std::runtime_error(
                    "s41-selftest: the missed catch did not reach the ground in " +
                    std::to_string(kS41MaxFallFrames) + " frames");
            }
            const float landingGround = heightmap_sample(heightmap, player.position);
            if (!std::isfinite(landingGround) ||
                std::fabs(player.position.y - landingGround) > kS41Eps) {
                throw std::runtime_error(
                    "s41-selftest: the landing is not on the ground (y " +
                    std::to_string(player.position.y) + ", ground " +
                    std::to_string(landingGround) + ")");
            }
            std::printf("s41-fall frames=%d landed=1\n", fallFrames);

            // --- 7. Zero stamina while attached ------------------------------
            for (int frame = 0; frame < kS41MaxStaggerFrames; ++frame) {
                if (combat.mode == PlayerMode::Grounded) {
                    break;
                }
                step_frame(noInput);
            }
            if (combat.mode != PlayerMode::Grounded) {
                throw std::runtime_error(
                    "s41-selftest: the stagger did not return to Grounded");
            }
            // R10's larger head band moves the catch to the head, and a
            // head-height fall lands on the foot/leg axis; the body's leg
            // capsules then block a scripted straight walk to a band (the
            // resolver pushes the player off the surface before the grab).
            // The fall and landing above are the evidence; the re-grab is the
            // setup for the zero-stamina assertion, so use the same ruled
            // scaffolding class as the head teleport: place the player on the
            // nearest band's surface and grab. The user's walk does the real
            // re-climb.
            palettes = update_all_poses(renderer, scene, playbacks, 0.0f, club);
            PlayerFrame landingFrame =
                player_frame_of(scene, entityRig, palettes, heightmap, 0.0f);
            const ClimbBandId returnBand =
                nearest_climb_band(landingFrame.jointWorld, player.position, 1000.0f);
            Vec3 returnPoint{};
            Vec3 returnNormal{};
            if (returnBand == ClimbBandId::Count ||
                !climb_surface_point(climb_band(returnBand), landingFrame.jointWorld, 0.5f,
                                     firstGrabPosition, returnPoint, returnNormal)) {
                throw std::runtime_error(
                    "s41-selftest: no band to re-grab after the landing");
            }
            player.position = returnPoint;
            player.yawDeg = std::atan2(-returnNormal.x, -returnNormal.z) / kDegToRad;
            combat.mode = PlayerMode::Grounded;
            combat.modeSeconds = 0.0f;
            combat.band = ClimbBandId::Count;
            combat.weakpoint = -1;
            combat.catchSecondsLeft = 0.0f;
            combat.fallVelocity = 0.0f;
            const StepEvents groundedGrab = step_frame(grabInput);
            if (!groundedGrab.grabbed || combat.mode != PlayerMode::Grabbing) {
                const std::vector<std::vector<Mat4>> now =
                    update_all_poses(renderer, scene, playbacks, 0.0f, club);
                const PlayerFrame frame =
                    player_frame_of(scene, entityRig, now, heightmap, 0.0f);
                const ClimbBandId nearest =
                    nearest_climb_band(frame.jointWorld, player.position, 1000.0f);
                throw std::runtime_error(
                    "s41-selftest: the player could not grab again after landing (pos " +
                    std::to_string(player.position.x) + "," +
                    std::to_string(player.position.y) + "," +
                    std::to_string(player.position.z) + " nearest " +
                    climb_band_name(nearest) + " mode " +
                    player_mode_name(combat.mode) + " stamina " +
                    std::to_string(combat.stamina) + ")");
            }
            combat.stamina = 0.0f;  // the forced-empty pool of the design
            const StepEvents forcedFall = step_frame(grabInput);
            if (!forcedFall.gripBroke || combat.mode != PlayerMode::Falling) {
                throw std::runtime_error(
                    "s41-selftest: a zero-stamina grip did not break into Falling");
            }
            // Exactly once: the next step still holds E and must not report a
            // second break.
            const StepEvents afterForcedFall = step_frame(grabInput);
            if (afterForcedFall.gripBroke) {
                throw std::runtime_error(
                    "s41-selftest: the zero-stamina fall reported a second grip break");
            }
            std::printf("s41-stamina fall=1\n");

            // --- 8. The killing blow -----------------------------------------
            combat.stamina = 100.0f;  // restore the pool the forced fall emptied
            const StepEvents releasedAfterFall = step_frame(noInput);
            (void)releasedAfterFall;
            PlayerInput catchAgain;
            catchAgain.grab = true;
            const StepEvents recaught = step_frame(catchAgain);
            if (!recaught.grabbed || combat.mode != PlayerMode::Grabbing) {
                throw std::runtime_error(
                    "s41-selftest: the post-stamina re-grab did not attach");
            }
            teleport_to_head();
            const StepEvents attachedForKill = step_frame(holdGrab);
            (void)attachedForKill;
            if (combat.band != ClimbBandId::Head || combat.weakpoint != 0) {
                throw std::runtime_error(
                    "s41-selftest: the killing-blow teleport did not hold the head band");
            }
            // The next full hit must kill: set the health to exactly one plunge.
            encounter.health = tuning.damageFull;

            for (int frame = 0; frame < kS41ChargeFrames; ++frame) {
                const StepEvents events = step_frame(chargeInput);
                if (events.damageDealt != 0) {
                    throw std::runtime_error(
                        "s41-selftest: the killing strike dealt damage while charging");
                }
            }
            const int healthBeforeKill = static_cast<int>(std::lround(encounter.health));
            const int killWeakpoint = combat.weakpoint;
            const StepEvents deathBlow = step_frame(releaseInput);
            if (deathBlow.damageDealt != static_cast<int>(std::lround(tuning.damageFull))) {
                throw std::runtime_error(
                    "s41-selftest: the killing strike dealt " +
                    std::to_string(deathBlow.damageDealt) + " damage");
            }
            std::printf("s41-strike swing=3 damage=%d health=%d->%d weakpoint=%d\n",
                        deathBlow.damageDealt, healthBeforeKill,
                        static_cast<int>(std::lround(encounter.health)), killWeakpoint);
            if (!encounter.defeated || encounter.health > 0.0f) {
                throw std::runtime_error(
                    "s41-selftest: the killing blow did not defeat Valus (health " +
                    std::to_string(encounter.health) + ")");
            }
            if (entity_playback.index != entityClips.death || !entity_playback.pinned) {
                throw std::runtime_error(
                    "s41-selftest: the killing blow did not start the pinned death clip");
            }
            // It plays once, then freezes on its last frame (pinned, not
            // playing) instead of looping.
            const float deathDuration = entity_playback.clips[
                static_cast<std::size_t>(entityClips.death)].duration;
            int deathFrames = 0;
            while (encounter.reactionClip >= 0 && deathFrames < 1200) {
                advance_encounter_reaction(entity_playback, encounter, entityClips.stand,
                                           stepSeconds);
                ++deathFrames;
            }
            if (encounter.reactionClip >= 0 || entity_playback.playing ||
                !entity_playback.pinned ||
                std::fabs(playback_time(entity_playback, deathDuration) - deathDuration) >
                    1e-3f) {
                throw std::runtime_error(
                    "s41-selftest: the death clip did not freeze on its last frame");
            }
            std::printf("s41-death clip=%s defeated=1\n",
                        entity_playback.clips[static_cast<std::size_t>(entityClips.death)]
                            .name.c_str());

            // --- 8b. The killing blow's fall lands (ruling R9a) ---------------
            // The death blow shakes the player off the head. Let the same
            // no-input step carry the fall to the ground and the landing
            // stagger back to Grounded, so the defeat still shows a landed
            // player instead of one suspended at the head.
            if (combat.mode != PlayerMode::Falling) {
                throw std::runtime_error(
                    "s41-selftest: the killing blow did not leave the player falling (mode " +
                    std::string(player_mode_name(combat.mode)) + ")");
            }
            int settleFrames = 0;
            while (combat.mode != PlayerMode::Grounded && settleFrames < kS41MaxFallFrames) {
                step_frame(noInput);
                ++settleFrames;
                if (combat.mode != PlayerMode::Falling &&
                    combat.mode != PlayerMode::Staggered &&
                    combat.mode != PlayerMode::Grounded) {
                    throw std::runtime_error(
                        "s41-selftest: the post-kill settle entered " +
                        std::string(player_mode_name(combat.mode)) + " at frame " +
                        std::to_string(settleFrames));
                }
            }
            if (combat.mode != PlayerMode::Grounded) {
                throw std::runtime_error(
                    "s41-selftest: the post-kill fall did not reach the ground in " +
                    std::to_string(kS41MaxFallFrames) + " frames");
            }
            if (!finite_position(player.position)) {
                throw std::runtime_error(
                    "s41-selftest: the post-kill settle wrote a non-finite position");
            }
            const float settledGround = heightmap_sample(heightmap, player.position);
            if (!std::isfinite(settledGround) ||
                std::fabs(player.position.y - settledGround) > kS41Eps) {
                throw std::runtime_error(
                    "s41-selftest: the post-kill landing is not on the ground (y " +
                    std::to_string(player.position.y) + ", ground " +
                    std::to_string(settledGround) + ")");
            }
            std::printf("s41-settle frames=%d grounded=1\n", settleFrames);

            // --- 9. The final frame and status -------------------------------
            if (!grabBandSet || grabBand == ClimbBandId::Count) {
                throw std::runtime_error(
                    "s41-selftest: the status has no resolved grab band");
            }
            update_combat_clips(playback, scene, combat, noInput, /*manualClip=*/false);
            const std::vector<std::vector<Mat4>> finalPalettes =
                update_all_poses(renderer, scene, playbacks, 0.0f, club);
            apply_player_transform(scene, player_rig_record, player,
                                   active_player_lift(scene, playback, player_rig_record));
            renderer.set_hud(HudState{true, combat.stamina / 100.0f,
                                      encounter.health / 100.0f});
            // The defeat still pulls the follow camera out to its 12 m wheel
            // ceiling so the landed player and the fallen body share the
            // frame; the arm's-length follow view points into the corpse. The
            // interactive camera is untouched — the selftest ends here.
            follow.distance = 12.0f;
            camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f, 0.0f,
                          /*moving=*/false, 1.0f);
            const Mat4 finalViewProj = camera_view_proj(camera, shell.win);
            int pixel_width = 0;
            int pixel_height = 0;
            if (!SDL_GetWindowSizeInPixels(shell.win, &pixel_width, &pixel_height)) {
                throw std::runtime_error(std::string("SDL_GetWindowSizeInPixels: ") +
                                         SDL_GetError());
            }
            const ScreenBox defeat_bbox =
                entityRig < finalPalettes.size()
                    ? project_rig_bounds(scene, entityRig, finalPalettes[entityRig],
                                         finalViewProj, pixel_width, pixel_height)
                    : ScreenBox{};
            if (!defeat_bbox.any) {
                throw std::runtime_error(
                    "s41-selftest: the defeated entity has no projected screen region");
            }
            std::printf("s41-defeat bbox=%d,%d,%d,%d screen=%dx%d\n", defeat_bbox.x0,
                        defeat_bbox.y0, defeat_bbox.x1, defeat_bbox.y1, pixel_width,
                        pixel_height);
            renderer.render_frame(scene, finalViewProj, /*acquire_swapchain=*/true);
            const std::string defeat_path =
                !opts.screenshotPath.empty() ? opts.screenshotPath : still_path("defeat");
            if (!opts.s41StillsDir.empty() || !opts.screenshotPath.empty()) {
                std::printf("s41-still phase=defeat path=%s\n", defeat_path.c_str());
                write_screenshot(renderer, shell.win, defeat_path);
            }
            if (!opts.s41StillsDir.empty()) {
                // The presence control: the same final frame with the entity's
                // meshes translated out of view. Its diff against the defeat
                // still proves the entity is drawn and locates it inside
                // s41-defeat's bbox.
                const RigCpu& entity_rig = rig_at(scene, entityRig);
                const std::size_t entity_mesh_end =
                    std::min(entity_rig.meshEnd, scene.meshes.size());
                // Hide the club with the body it is attached to: it is static
                // geometry, so the entity mesh range alone would leave it
                // floating in the control frame.
                std::vector<std::size_t> hidden;
                for (std::size_t i = std::min(entity_rig.meshBegin, entity_mesh_end);
                     i < entity_mesh_end; ++i) {
                    hidden.push_back(i);
                }
                for (std::size_t i = club.meshBegin; club.loaded && i < club.meshEnd &&
                                                      i < scene.meshes.size();
                     ++i) {
                    hidden.push_back(i);
                }
                std::vector<Mat4> saved_models;
                saved_models.reserve(hidden.size());
                for (const std::size_t i : hidden) {
                    saved_models.push_back(scene.meshes[i].model);
                    scene.meshes[i].model.m[12] += 100000.0f;
                }
                capture_control("defeat-noentity", control_path("defeat-noentity"),
                                /*hudVisible=*/true);
                for (std::size_t slot = 0; slot < hidden.size(); ++slot) {
                    scene.meshes[hidden[slot]].model = saved_models[slot];
                }
                // The HUD control: the same frame with the overlay hidden.
                capture_control("defeat-nohud", control_path("defeat-nohud"),
                                /*hudVisible=*/false);
                renderer.set_hud(HudState{true, combat.stamina / 100.0f,
                                          encounter.health / 100.0f});
            }
            print_status_line(opts, shell.win);
            std::printf("s41-status grab=%s climb=%.2f strikes=%d entity_health=%d mode=%s "
                        "defeated=%d\n",
                        climb_band_name(grabBand), climbRise, strikes,
                        static_cast<int>(std::lround(encounter.health)),
                        player_mode_name(combat.mode), encounter.defeated ? 1 : 0);
            return 0;
        }

        if (opts.s4Selftest) {
            // The S4 acceptance walk: one pinned entity pose (t = 0), a guard
            // that the manifest's spawn is outside every entity capsule, the
            // feet-on-ground measurement, the screen bbox the idle-pair pixel
            // guard uses, and a walk into the nearest leg capsule that must
            // stop with bounded penetration while staying grounded.
            const std::string placement = manifest_header_value(opts.manifestPath, "placement");
            const std::string spawn_provenance = manifest_header_value(opts.manifestPath, "spawn");
            if (placement.empty() || spawn_provenance.empty()) {
                std::fprintf(stderr,
                             "sotc-viewer: s4-selftest: %s carries no placement=/spawn= "
                             "provenance header\n",
                             opts.manifestPath.c_str());
                return 1;
            }

            const Playback& entity_playback = playbacks[entityRig];
            const AnimationCpu& entity_clip =
                entity_playback.clips[static_cast<std::size_t>(entity_playback.index)];
            const float entity_time = playback_time(entity_playback, entity_clip.duration);
            const std::vector<Mat4> entity_palette =
                animation_pose(scene, entityRig, entity_clip, entity_time);

            const std::vector<Capsule> capsules =
                entity_collision_capsules(scene, entityRig, entity_palette);
            if (capsules.empty()) {
                std::fprintf(stderr,
                             "sotc-viewer: s4-selftest: the entity rig has no collision capsules\n");
                return 1;
            }

            const Vec3 spawn = player.position;
            const float feet = player.position.y;
            const float head = feet + kPlayerCapsuleHeight;
            float spawn_clearance = std::numeric_limits<float>::infinity();
            for (const Capsule& capsule : capsules) {
                if (!capsule_overlaps_vertically(capsule, feet, head)) {
                    continue;
                }
                Vec3 closest{};
                const float distance = capsule_horizontal_distance(capsule, spawn, closest);
                spawn_clearance = std::min(
                    spawn_clearance, distance - (std::max(capsule.r, 0.0f) + kPlayerCapsuleRadius));
            }
            const bool capsules_reachable = std::isfinite(spawn_clearance);
            const bool spawn_clear = capsules_reachable && spawn_clearance >= 0.0f;

            const RigBounds entity_bounds = posed_rig_bounds(scene, entityRig, entity_palette);
            const Vec3 entity_position = manifest.entities[0].placement.position;
            const float entity_ground = heightmap_sample(heightmap, entity_position);
            const float lowest = entity_bounds.any ? entity_bounds.min.y : 0.0f;
            const float feet_delta = lowest - entity_ground;
            const bool feet_ok = entity_bounds.any && std::isfinite(entity_ground) &&
                                 std::fabs(feet_delta) <= kS4FeetTolerance;

            // The idle pair's camera, reconstructed exactly as the capture path
            // builds it (`--camera-target player` at the manifest spawn): the
            // follow snap puts the orbit target at the player's head
            // (camera.h: playerPos + 1.4 m), the yaw at spawnYaw + 180, pitch 0
            // and distance 4.5 m. It does not depend on the entity's pose, so
            // the two idle captures share it while Valus animates.
            Camera idle_camera{};
            idle_camera.target = Vec3{spawn.x, spawn.y + 1.4f, spawn.z};
            idle_camera.yaw = manifest.spawnYawDeg + 180.0f;
            idle_camera.pitch = 0.0f;
            idle_camera.distance = CameraFollow{}.distance;
            int pixel_width = 0;
            int pixel_height = 0;
            if (!SDL_GetWindowSizeInPixels(shell.win, &pixel_width, &pixel_height)) {
                std::fprintf(stderr, "sotc-viewer: s4-selftest: %s\n", SDL_GetError());
                return 1;
            }
            const float pixel_aspect =
                static_cast<float>(pixel_width) / static_cast<float>(std::max(pixel_height, 1));
            const Mat4 idle_view_proj =
                mat4_mul(camera_proj(idle_camera, pixel_aspect), camera_view(idle_camera));
            const ScreenBox entity_bbox = project_rig_bounds(scene, entityRig, entity_palette,
                                                             idle_view_proj, pixel_width,
                                                             pixel_height);

            // The walk's aim: the closest point on the nearest vertically
            // reachable capsule axis. Approaching it head-on makes the capsule
            // push exactly opposite the motion, so the stop is a stop, not a
            // slide around the leg. Other manifest entities (none in S4.0) are
            // not part of this aim.
            Vec3 aim{};
            float aim_distance = std::numeric_limits<float>::infinity();
            for (const Capsule& capsule : capsules) {
                if (!capsule_overlaps_vertically(capsule, feet, head)) {
                    continue;
                }
                Vec3 closest{};
                const float distance = capsule_horizontal_distance(capsule, spawn, closest);
                if (distance < aim_distance) {
                    aim_distance = distance;
                    aim = closest;
                }
            }
            const bool have_aim = std::isfinite(aim_distance);
            const float aim_dx = aim.x - spawn.x;
            const float aim_dz = aim.z - spawn.z;
            const float aim_length = std::sqrt(aim_dx * aim_dx + aim_dz * aim_dz);
            if (!have_aim || !(aim_length > 0.0f)) {
                std::fprintf(stderr,
                             "sotc-viewer: s4-selftest: no reachable entity capsule to walk into\n");
                return 1;
            }
            const Vec3 aim_dir{aim_dx / aim_length, 0.0f, aim_dz / aim_length};
            const float aim_yaw =
                std::atan2(aim_dx, aim_dz) / kDegToRad;  // the app's yaw convention

            PlayerInput input;
            input.forward = true;
            input.run = true;
            int frames = 0;
            int still_frames = 0;
            bool stopped = false;
            float max_ground_deviation = 0.0f;
            bool grounded = true;
            while (frames < kS4MaxWalkFrames) {
                const Vec3 previous = player.position;
                player_update(player, input, kFixedFrameSeconds, heightmap, aim_yaw);
                update_locomotion(playback, scene, input, manual_clip);
                update_all_poses(renderer, scene, playbacks, kFixedFrameSeconds, club);
                resolve_player_collision(player, capsules, heightmap);

                if (targetCamera.active) {
                    if (haveEntityChest) {
                        camera_target_update(camera, targetCamera, entityChest, 0.0f, 0.0f, 0.0f);
                    }
                } else {
                    camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f,
                                  0.0f, /*moving=*/true, kFixedFrameSeconds);
                }

                const float ground = heightmap_sample(heightmap, player.position);
                if (!std::isfinite(player.position.y) || !std::isfinite(ground)) {
                    grounded = false;
                } else {
                    max_ground_deviation =
                        std::max(max_ground_deviation, std::fabs(player.position.y - ground));
                }

                const float move_dx = player.position.x - previous.x;
                const float move_dz = player.position.z - previous.z;
                const float moved = std::sqrt(move_dx * move_dx + move_dz * move_dz);
                ++frames;
                if (moved < kS4StopEpsilon) {
                    if (++still_frames >= kS4StopFrames) {
                        stopped = true;
                        break;
                    }
                } else {
                    still_frames = 0;
                }
            }

            // The final frame is the drawn one: player transform, then the
            // capture (when asked).
            apply_player_transform(scene, player_rig_record, player,
                                   active_player_lift(scene, playback, player_rig_record));
            renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                                  /*acquire_swapchain=*/true);
            if (!opts.screenshotPath.empty()) {
                write_screenshot(renderer, shell.win, opts.screenshotPath);
            }

            float final_clearance = std::numeric_limits<float>::infinity();
            for (const Capsule& capsule : capsules) {
                if (!capsule_overlaps_vertically(capsule, player.position.y,
                                                 player.position.y + kPlayerCapsuleHeight)) {
                    continue;
                }
                Vec3 closest{};
                const float distance =
                    capsule_horizontal_distance(capsule, player.position, closest);
                final_clearance = std::min(
                    final_clearance, distance - (std::max(capsule.r, 0.0f) + kPlayerCapsuleRadius));
            }
            const bool clearance_finite = std::isfinite(final_clearance);
            const float penetration = clearance_finite ? std::max(0.0f, -final_clearance) : 0.0f;
            const bool bounded = clearance_finite && penetration <= kS4PenetrationTolerance;
            const float progress = (player.position.x - spawn.x) * aim_dir.x +
                                   (player.position.z - spawn.z) * aim_dir.z;
            const float stop_before = aim_length - progress;
            const bool no_pass_through =
                progress > 0.0f && stop_before >= kS4StopBeforeTolerance;
            const bool grounded_ok = grounded && max_ground_deviation <= kS4GroundTolerance;

            std::printf("s4-spawn capsules=%zu clearance=%.3f spawn=%.3f,%.3f,%.3f\n",
                        capsules.size(), capsules_reachable ? spawn_clearance : -1.0f, spawn.x,
                        spawn.y, spawn.z);
            std::printf("s4-entity clip=%s joints=%zu lowest=%.3f ground=%.3f delta=%.3f "
                        "bbox=%d,%d,%d,%d screen=%dx%d\n",
                        entity_clip.name.c_str(),
                        rig_at(scene, entityRig).skin.joints.size(), lowest, entity_ground,
                        feet_delta, entity_bbox.x0, entity_bbox.y0, entity_bbox.x1,
                        entity_bbox.y1, pixel_width, pixel_height);
            std::printf("s4-walk frames=%d stop=%s aim=%.3f,%.3f,%.3f pos=%.3f,%.3f,%.3f "
                        "clearance=%.3f penetration=%.3f stop_before=%.3f grounded=%.4f\n",
                        frames, stopped ? "yes" : "no", aim.x, aim.y, aim.z, player.position.x,
                        player.position.y, player.position.z,
                        clearance_finite ? final_clearance : -1.0f, penetration, stop_before,
                        max_ground_deviation);

            if (!spawn_clear) {
                std::fprintf(stderr,
                             "sotc-viewer: s4-selftest: the spawn is inside an entity capsule "
                             "(clearance %.3f m)\n",
                             capsules_reachable ? spawn_clearance : 0.0f);
                return 1;
            }
            if (!feet_ok) {
                std::fprintf(stderr,
                             "sotc-viewer: s4-selftest: entity feet are %.3f m off the ground "
                             "(lowest %.3f, ground %.3f; tolerance %.2f)\n",
                             feet_delta, lowest, entity_ground, kS4FeetTolerance);
                return 1;
            }
            if (!entity_bbox.any) {
                std::fprintf(stderr,
                             "sotc-viewer: s4-selftest: the entity has no projected screen "
                             "region\n");
                return 1;
            }
            if (!stopped) {
                std::fprintf(stderr,
                             "sotc-viewer: s4-selftest: the player did not stop within %d "
                             "frames\n",
                             kS4MaxWalkFrames);
                return 1;
            }
            if (!bounded || !no_pass_through) {
                std::fprintf(stderr,
                             "sotc-viewer: s4-selftest: collision stop failed (clearance %.3f, "
                             "penetration %.3f, stop_before %.3f)\n",
                             final_clearance, penetration, stop_before);
                return 1;
            }
            if (!grounded_ok) {
                std::fprintf(stderr,
                             "sotc-viewer: s4-selftest: the player left the ground (max "
                             "deviation %.4f m)\n",
                             max_ground_deviation);
                return 1;
            }

            print_status_line(opts, shell.win);
            std::printf("s4-status instances=%zu rigs=%zu entity=%s placement=%s spawn=%s "
                        "feet=%s entity_bbox=%d,%d,%d,%d pos=%.3f,%.3f,%.3f frames=%d\n",
                        manifest.instances.size(), scene.rigs.size(), entity_clip.name.c_str(),
                        placement.c_str(), spawn_provenance.c_str(), feet_ok ? "ok" : "none",
                        entity_bbox.x0, entity_bbox.y0, entity_bbox.x1, entity_bbox.y1,
                        player.position.x, player.position.y, player.position.z, frames);
            return 0;
        }

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
                if (targetCamera.active) {
                    if (haveEntityChest) {
                        camera_target_update(camera, targetCamera, entityChest, 0.0f, 0.0f, 0.0f);
                    }
                } else {
                    camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f, 0.0f,
                                  /*moving=*/true, kFixedFrameSeconds);
                }
                player_update(player, input, kFixedFrameSeconds, heightmap,
                              camera_look_yaw(camera));
                update_locomotion(playback, scene, input, manual_clip);
                const std::vector<std::vector<Mat4>> palettes =
                    update_all_poses(renderer, scene, playbacks, kFixedFrameSeconds, club);
                // The collision hook: after this frame's pose/playback update,
                // before the player transform that is drawn below the loop.
                resolve_player_collision(player, entity_capsules(scene, palettes), heightmap);
                if (entityRig < scene.rigs.size() && entityRig < palettes.size()) {
                    haveEntityChest = entity_chest_position(scene, entityRig, palettes[entityRig],
                                                           entityChest);
                    if (targetCamera.active && haveEntityChest) {
                        camera_target_update(camera, targetCamera, entityChest, 0.0f, 0.0f, 0.0f);
                    }
                }
            }
            if (targetCamera.active && haveEntityChest) {
                camera_target_update(camera, targetCamera, entityChest, 0.0f, 0.0f, 0.0f);
            }
            apply_player_transform(scene, player_rig_record, player,
                                   active_player_lift(scene, playback, player_rig_record));

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
            update_all_poses(renderer, scene, playbacks, /*dt=*/0.0f, club);
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
                    const PlayerInput still;
                    update_locomotion(playback, scene, still, manual_clip);
                    const std::vector<std::vector<Mat4>> palettes =
                        update_all_poses(renderer, scene, playbacks, dt, club);
                    if (entityRig < scene.rigs.size() && entityRig < palettes.size()) {
                        haveEntityChest = entity_chest_position(scene, entityRig,
                                                               palettes[entityRig], entityChest);
                    }
                    if (targetCamera.active) {
                        if (haveEntityChest) {
                            camera_target_update(camera, targetCamera, entityChest, 0.0f, 0.0f,
                                                 0.0f);
                        }
                    } else if (targetCamera.returning) {
                        camera_target_return(camera, follow, targetCamera, player.position,
                                             player.yawDeg, 0.0f, 0.0f, 0.0f,
                                             /*moving=*/false, dt);
                    } else {
                        camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f,
                                      0.0f, /*moving=*/false, dt);
                    }
                    player_update(player, still, dt, heightmap, camera_look_yaw(camera));
                    // The collision hook: after player_update, before the
                    // player transform the frame is drawn with.
                    resolve_player_collision(player, entity_capsules(scene, palettes), heightmap);
                    apply_player_transform(scene, player_rig_record, player,
                                           active_player_lift(scene, playback,
                                                              player_rig_record));
                } else {
                    update_all_poses(renderer, scene, playbacks, dt, club);
                }
                renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                                      /*acquire_swapchain=*/true);
            }

            write_screenshot(renderer, shell.win, opts.screenshotPath);
            return 0;
        }

        // Interactive: left-drag orbits (single-model mode) or turns the follow
        // camera's view (manifest mode), the wheel zooms, F reframes (single
        // model), recenters behind the player (manifest mode) or re-seeds the
        // climb orbit while gripping, T orbits the entity's chest and back
        // (inert when the manifest has no entity), Space plays/pauses, [ and ]
        // switch clips, R restarts the clip (single model) or soft-resets the
        // whole encounter (manifest mode), Escape or the close button quits.
        // In manifest mode W/A/S/D walk (Shift runs), E grabs (held) and J
        // charges a strike (held; released to plunge); the camera follows. A
        // standard SDL3 gamepad is polled once per frame: left stick moves,
        // right stick turns the camera, RT grips, X/square strikes, Start
        // pauses; hot-plug opens/closes it. Input needs no capture: plain
        // mouse-motion events with SDL_BUTTON_LMASK held (never relative mode,
        // never SDL_CaptureMouse) — the cursor stays visible and free.
        bool running = true;
        bool climb_recenter = false;
        GamepadInput gamepad;
        open_first_gamepad(gamepad);
        const CombatTuning tuning{};
        std::deque<Uint64> frame_ticks;  // frame arrival times within the last second
        Uint64 last_title_update = SDL_GetTicks();
        Uint64 last_ticks = SDL_GetTicks();
        while (running) {
            // This frame's drag and wheel deltas (motion events arrive several
            // per frame; the follow camera takes their sum).
            float drag_dx = 0.0f;
            float drag_dy = 0.0f;
            float wheel_delta = 0.0f;
            climb_recenter = false;

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                    case SDL_EVENT_QUIT:
                        running = false;
                        break;
                    case SDL_EVENT_GAMEPAD_ADDED:
                        if (gamepad.handle == nullptr) {
                            gamepad.open(event.gdevice.which);
                        }
                        break;
                    case SDL_EVENT_GAMEPAD_REMOVED:
                        // Drop the handle and every input it owned: an unplug
                        // mid-grip must read as a released E, not a latched one.
                        if (gamepad.handle != nullptr && gamepad.id == event.gdevice.which) {
                            gamepad.close();
                        }
                        break;
                    case SDL_EVENT_KEY_DOWN:
                        if (event.key.key == SDLK_ESCAPE) {
                            running = false;
                        } else if (event.key.key == SDLK_F) {
                            if (follow_mode) {
                                if (mode_is_attached(combat.mode) && climbCamera.active) {
                                    // While gripping, F re-seeds the climb orbit
                                    // outward at 6 m — the follow camera's F
                                    // recenter, applied to the climb camera.
                                    climb_recenter = true;
                                } else {
                                    // Recenter the follow camera behind the player.
                                    follow.yawOffsetDeg = 0.0f;
                                    follow.pitchOffsetDeg = 0.0f;
                                    follow.distance = CameraFollow{}.distance;
                                }
                            } else {
                                // Keep the whole subject in frame when the clip's root series
                                // carries the ground height: frame the current pose, not the
                                // bind bounds.
                                frame_single_model(camera, scene, playback);
                            }
                        } else if (event.key.key == SDLK_SPACE) {
                            playback.playing = !playback.playing;
                        } else if (event.key.key == SDLK_T) {
                            // Camera-only framing: orbit the entity's chest. T
                            // is inert when the manifest has no entity (the S3
                            // arena) or its chest did not resolve — otherwise
                            // the camera would park on the zero target.
                            if (entity_target_key_allowed(follow_mode, haveEntityChest)) {
                                camera_target_toggle(camera, follow, targetCamera, entityChest);
                            }
                        } else if (event.key.key == SDLK_R) {
                            if (follow_mode) {
                                // The full soft reset (design §7.5): the player
                                // and Valus go back to their opening state, the
                                // death pin/hurt one-shot is left behind, and
                                // the follow camera snaps behind the spawn.
                                reset_player_combat(player, combat,
                                                    spawn_position(manifest, scene),
                                                    manifest.hasSpawn
                                                        ? manifest.spawnYawDeg
                                                        : 0.0f,
                                                    tuning);
                                reset_encounter(encounter);
                                if (entityRig < playbacks.size() &&
                                    entityClips.stand >= 0) {
                                    Playback& entityPlayback = playbacks[entityRig];
                                    entityPlayback.index = entityClips.stand;
                                    entityPlayback.time = 0.0f;
                                    entityPlayback.pinned = false;
                                    entityPlayback.playing = true;
                                }
                                climbCamera.active = false;
                                targetCamera = CameraTarget{};
                                follow = CameraFollow{};
                                camera_follow(camera, follow, player.position,
                                              player.yawDeg, 0.0f, 0.0f, 0.0f,
                                              /*moving=*/false, /*dtSeconds=*/10.0f);
                                if (!manual_clip) {
                                    playback.pinned = false;
                                    playback.playing = true;
                                }
                                playback.time = 0.0f;
                            } else {
                                playback.time = 0.0f;
                            }
                        } else if (!playback.clips.empty() &&
                                   (event.key.key == SDLK_LEFTBRACKET ||
                                    event.key.key == SDLK_RIGHTBRACKET)) {
                            // Arena mode selects by controller state every frame, so a manual
                            // clip change would be reverted on the next frame: keep the keys
                            // for single-model browsing only.
                            if (!follow_mode) {
                                const int count = static_cast<int>(playback.clips.size());
                                const int step = (event.key.key == SDLK_LEFTBRACKET) ? count - 1 : 1;
                                select_clip(playback, scene, playerRig, (playback.index + step) % count);
                            }
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

            gamepad.poll();
            if (gamepad.pausePressed) {
                playback.playing = !playback.playing;
            }

            if (follow_mode) {
                const bool* keys = SDL_GetKeyboardState(nullptr);
                const auto deadzoned = [](float value) {
                    return std::fabs(value) > kS41StickDeadzone ? value : 0.0f;
                };
                PlayerInput input;
                input.forward = keys[SDL_SCANCODE_W] || deadzoned(gamepad.leftY) < 0.0f;
                input.back = keys[SDL_SCANCODE_S] || deadzoned(gamepad.leftY) > 0.0f;
                input.left = keys[SDL_SCANCODE_A] || deadzoned(gamepad.leftX) < 0.0f;
                input.right = keys[SDL_SCANCODE_D] || deadzoned(gamepad.leftX) > 0.0f;
                input.run = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
                input.grab = keys[SDL_SCANCODE_E] || gamepad.grab;
                input.strike = keys[SDL_SCANCODE_J] || gamepad.strike;
                const bool moving = input.forward || input.back || input.left || input.right;

                // The right stick feeds the cameras' drag channel; clamp the
                // frame time so a hitch cannot fling the view.
                const float stick_dt = std::clamp(dt, 0.0f, 0.05f);
                drag_dx += deadzoned(gamepad.rightX) * kS41StickPixelsPerSecond * stick_dt;
                drag_dy += deadzoned(gamepad.rightY) * kS41StickPixelsPerSecond * stick_dt;

                // The encounter's one-shot clock, then the clip choice (from the
                // mode the previous step left) and this frame's poses: the
                // target camera, the collision proxy and the climb attach point
                // all read the palettes drawn today.
                if (entityRig < playbacks.size()) {
                    advance_encounter_reaction(playbacks[entityRig], encounter,
                                               entityClips.stand, dt);
                }
                update_combat_clips(playback, scene, combat, input, manual_clip);
                const std::vector<std::vector<Mat4>> palettes =
                    update_all_poses(renderer, scene, playbacks, dt, club);
                if (entityRig < scene.rigs.size() && entityRig < palettes.size()) {
                    haveEntityChest = entity_chest_position(scene, entityRig, palettes[entityRig],
                                                           entityChest);
                }

                // While gripping, the climb orbit owns the camera: it is updated
                // after the step, on this frame's attach point. Otherwise the
                // camera runs before player_update (its yaw is the movement
                // basis, and in target mode that is the orbit around the chest).
                const bool attached_before = mode_is_attached(combat.mode);
                if (!attached_before) {
                    if (targetCamera.active) {
                        if (haveEntityChest) {
                            camera_target_update(camera, targetCamera, entityChest, drag_dx,
                                                 drag_dy, wheel_delta);
                        }
                    } else if (targetCamera.returning) {
                        camera_target_return(camera, follow, targetCamera, player.position,
                                             player.yawDeg, drag_dx, drag_dy, wheel_delta,
                                             moving, dt);
                    } else {
                        camera_follow(camera, follow, player.position, player.yawDeg, drag_dx,
                                      drag_dy, wheel_delta, moving, dt);
                    }
                }

                // The player step: the state machine runs the Grounded
                // controller + capsule resolver in the S3/S4 order, or recomputes
                // the attach point from this frame's entity palette.
                const PlayerFrame frame =
                    player_frame_of(scene, entityRig, palettes, heightmap,
                                    camera_look_yaw(camera));
                const int weakpoint = combat.weakpoint;  // detach() clears it
                const StepEvents events =
                    player_step(player, combat, input, frame, tuning, dt);
                if (events.damageDealt > 0 && entityRig < playbacks.size()) {
                    apply_combat_events(encounter, playbacks[entityRig], entityClips, events,
                                        weakpoint);
                }

                if (mode_is_attached(combat.mode)) {
                    update_climb_camera(camera, climbCamera, player, combat, frame, drag_dx,
                                        drag_dy, wheel_delta, climb_recenter, dt);
                } else if (climbCamera.active) {
                    camera_climb_exit(camera, climbCamera);
                    // The camera was held by the climb orbit this frame: run one
                    // follow step now so the view eases back without a snap.
                    if (targetCamera.active) {
                        if (haveEntityChest) {
                            camera_target_update(camera, targetCamera, entityChest, drag_dx,
                                                 drag_dy, wheel_delta);
                        }
                    } else if (targetCamera.returning) {
                        camera_target_return(camera, follow, targetCamera, player.position,
                                             player.yawDeg, drag_dx, drag_dy, wheel_delta,
                                             moving, dt);
                    } else {
                        camera_follow(camera, follow, player.position, player.yawDeg, drag_dx,
                                      drag_dy, wheel_delta, moving, dt);
                    }
                }

                apply_player_transform(scene, player_rig_record, player,
                                       active_player_lift(scene, playback, player_rig_record));
                renderer.set_hud(HudState{true, combat.stamina / 100.0f,
                                          encounter.health / 100.0f});
            } else {
                update_all_poses(renderer, scene, playbacks, dt, club);
            }

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
