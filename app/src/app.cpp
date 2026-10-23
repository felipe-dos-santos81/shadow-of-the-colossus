#include "app.h"

#include "anim.h"
#include "asset_io.h"
#include "audio.h"
#include "audio_events.h"
#include "audio_map.h"
#include "camera.h"
#include "collision.h"
#include "death.h"
#include "effect.h"
#include "gamepad_map.h"
#include "grab.h"
#include "heightmap.h"
#include "manifest.h"
#include "player.h"
#include "player_state.h"
#include "renderer.h"
#include "scene.h"
#include "shrine_points.h"
#include "transition.h"
#include "wav_io.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
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

// The corpse swap's assets (S6, design D5/§8.3): the already-converted dead
// body (6 meshes, the six `_dead` materials with the veins veil) and the
// dirtmound it lies on. Both load as static instances at startup, hidden, and
// appear at Collapse at the live entity's transform.
constexpr const char* kCorpseGlb =
    "models/shadow/characters/colossus_01_minotaur_a_deadbody/_cmn/c01_minotaur_a_dead_body.glb";
constexpr const char* kDirtmoundGlb =
    "models/shadow/characters/colossus_01_minotaur_a_deadbody/_cmn/c01_minotaur_a_dead_dirtmound.glb";

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

// --- S6: the corpse and the death wiring (Task 5) ---------------------------

// The dead body + dirtmound, loaded as static instances at startup and hidden
// until Collapse (design D5/§8.3). `meshBegin..meshEnd` is their half-open
// static range in the scene; `loaded` stays false in a scene with no entity
// (and outside the death-wired runs, where the corpse is never placed).
struct CorpseAttachment {
    bool loaded = false;
    std::size_t meshBegin = 0;
    std::size_t meshEnd = 0;
};

// Appends the corpse (the dead body then the dirtmound) at the entity's
// placement, hidden. The initial model only has to be sane: while the corpse is
// visible `apply_death_visibility` rewrites it from the live entity's first
// mesh, so it lands exactly where the body fell. A missing glb throws (the same
// policy as the club): the corpse is the death's payoff, not an optional prop.
CorpseAttachment load_corpse(Scene& scene, const std::filesystem::path& libraryDir,
                             std::size_t entityRig) {
    CorpseAttachment corpse{};
    if (entityRig >= scene.rigs.size()) {
        return corpse;
    }
    const RigCpu& rig = scene.rigs[entityRig];
    if (rig.meshBegin >= scene.meshes.size()) {
        return corpse;
    }
    const Mat4 model = scene.meshes[rig.meshBegin].model;
    corpse.meshBegin = append_static_model(scene, libraryDir, kCorpseGlb, model);
    append_static_model(scene, libraryDir, kDirtmoundGlb, model);
    corpse.meshEnd = scene.meshes.size();
    for (std::size_t i = corpse.meshBegin; i < corpse.meshEnd; ++i) {
        scene.meshes[i].visible = false;
    }
    corpse.loaded = true;
    return corpse;
}

// The per-mesh draw gate for a half-open range; out-of-range bounds are
// clamped, so a caller never indexes past the scene.
void set_mesh_range_visible(Scene& scene, std::size_t begin, std::size_t end, bool visible) {
    const std::size_t last = std::min(end, scene.meshes.size());
    for (std::size_t i = std::min(begin, last); i < last; ++i) {
        scene.meshes[i].visible = visible;
    }
}

// Whether every mesh of a half-open range is visible (an empty range is
// vacuously visible). The S6 reset assertion reads the mesh gate itself, not
// just the death flags, so a half-restored swap fails loudly.
bool mesh_range_visible(const Scene& scene, std::size_t begin, std::size_t end) {
    const std::size_t last = std::min(end, scene.meshes.size());
    for (std::size_t i = std::min(begin, last); i < last; ++i) {
        if (!scene.meshes[i].visible) {
            return false;
        }
    }
    return true;
}

// The corpse swap (design G3): while `corpseVisible` the live entity's meshes
// (weakpoint shells included) and the held club are hidden and the dead body +
// dirtmound are shown at the entity's current transform; before and after the
// death window the live body is shown and the corpse hidden. Every other mesh
// (the level, the player) is untouched, so an R reset restores the opening
// frame exactly.
void apply_death_visibility(Scene& scene, const CorpseAttachment& corpse,
                            const ClubAttachment& club, std::size_t entityRig,
                            bool corpseVisible) {
    if (entityRig < scene.rigs.size()) {
        const RigCpu& rig = scene.rigs[entityRig];
        set_mesh_range_visible(scene, rig.meshBegin, rig.meshEnd, !corpseVisible);
    }
    if (club.loaded) {
        set_mesh_range_visible(scene, club.meshBegin, club.meshEnd, !corpseVisible);
    }
    if (!corpse.loaded) {
        return;
    }
    set_mesh_range_visible(scene, corpse.meshBegin, corpse.meshEnd, corpseVisible);
    if (!corpseVisible || entityRig >= scene.rigs.size()) {
        return;
    }
    // The corpse follows the body that fell: the live entity's first mesh
    // carries the placement (manifest TRS * the grounding lift * the live yaw),
    // so the corpse lands where Valus fell, not at the manifest's opening yaw.
    const RigCpu& rig = scene.rigs[entityRig];
    if (rig.meshBegin >= scene.meshes.size()) {
        return;
    }
    const Mat4 model = scene.meshes[rig.meshBegin].model;
    const std::size_t end = std::min(corpse.meshEnd, scene.meshes.size());
    for (std::size_t i = std::min(corpse.meshBegin, end); i < end; ++i) {
        scene.meshes[i].model = model;
    }
}

// The stable phase token the `s6-phase` line prints (the machine never enters
// Tendrils — the burst overlaps Motion — but the name exists for completeness).
const char* death_phase_name(DeathPhase phase) {
    switch (phase) {
        case DeathPhase::Idle:
            return "Idle";
        case DeathPhase::Motion:
            return "Motion";
        case DeathPhase::Tendrils:
            return "Tendrils";
        case DeathPhase::Collapse:
            return "Collapse";
        case DeathPhase::Done:
            return "Done";
    }
    return "Idle";
}

// The cinematic key the camera is on at death-clock time `t`: the segment's
// destination key (`follow` before the pull and after the return). The
// `s6-death` line reports it.
const char* death_camera_key(float t) {
    if (!std::isfinite(t) || t <= 0.0f) {
        return "follow";
    }
    if (t <= kDeathPullSeconds) {
        return "pull";
    }
    if (t <= kDeathLowSeconds) {
        return "low";
    }
    if (t <= kDeathWideSeconds) {
        return "wide";
    }
    return "follow";
}

// The fallback chest height (above the entity's placement) for the tendril
// roots' outward direction when the rig has no Bone_MUNE.
constexpr float kEffectFallbackChestHeight = 8.0f;

// This frame's tendril roots (design §8.2): the hit weakpoint's shell first,
// the other second — the binary's one-tendril-per-hit-point list, with the
// shells as the fallback for a hit point the app does not track. Each root's
// outward direction is the shell's direction from the body centre (the curl's
// initial heading); `seed` is the root's slot, so the two curls differ. `age`
// is the death clock (the burst begins at 0.4 s); `active` is the machine's
// burst window, so the frame is the renderer's no-op outside it.
EffectFrame build_effect_frame(const Scene& scene, std::size_t entityRig,
                               const std::vector<std::vector<Mat4>>& palettes,
                               const Encounter& encounter, const DeathState& death) {
    EffectFrame frame;
    frame.age = death.total;
    frame.active = death_tendrils_active(death);
    if (!frame.active || entityRig >= palettes.size()) {
        return frame;
    }

    Vec3 centre{encounter.entityPosition.x, encounter.entityPosition.y + kEffectFallbackChestHeight,
                encounter.entityPosition.z};
    Vec3 chest{};
    if (posed_joint_world_position(scene, entityRig, palettes[entityRig], "Bone_MUNE", chest) &&
        std::isfinite(chest.x) && std::isfinite(chest.y) && std::isfinite(chest.z)) {
        centre = chest;
    }

    const int first = encounter.deathWeakpoint == 1 ? 1 : 0;
    const int order[2] = {first, 1 - first};
    for (const int weakpoint : order) {
        if (frame.rootCount >= static_cast<int>(frame.roots.size())) {
            break;
        }
        const char* material = weakpoint_material_name(weakpoint);
        if (material == nullptr) {
            continue;
        }
        Vec3 shell{};
        float radius = 0.0f;
        if (!posed_weakpoint_proxy(scene, entityRig, palettes[entityRig], material, shell,
                                   radius) ||
            !std::isfinite(shell.x) || !std::isfinite(shell.y) || !std::isfinite(shell.z)) {
            continue;
        }
        Vec3 normal{shell.x - centre.x, shell.y - centre.y, shell.z - centre.z};
        const float length = std::sqrt(vec3_dot(normal, normal));
        if (length > 1e-4f) {
            normal = Vec3{normal.x / length, normal.y / length, normal.z / length};
        } else {
            normal = Vec3{0.0f, 1.0f, 0.0f};
        }
        EffectRoot& root = frame.roots[static_cast<std::size_t>(frame.rootCount)];
        root.position = shell;
        root.normal = normal;
        root.seed = static_cast<std::uint32_t>(frame.rootCount);
        ++frame.rootCount;
    }
    return frame;
}

// The death cue's one-shot latch: true exactly on the first frame the death
// clock has reached the burst start (0.4 s), when `fired` is set. The defeat
// itself queues nothing — the roar is the burst's sound (design §8.4). The test
// is `total >= kDeathTendrilStart`, not `death_tendrils_active`: a long hitch
// can jump the clock past the render window's 3.0 s end, and the roar must still
// fire once (the ribbon window itself stays 0.4-3.0).
bool death_cue_due(const DeathState& death, bool& fired) {
    if (fired || death.phase == DeathPhase::Idle) {
        return false;
    }
    if (!(death.total >= kDeathTendrilStart)) {
        return false;
    }
    fired = true;
    return true;
}

// One frame of the death machine and its presentation state (design §8.1/§8.3):
// advance the clock, drive the pinned one-shot death clip from it, and swap the
// corpse at Collapse. The caller owns the camera, the tendril frame and the
// audio (they need this frame's poses and the caller's bookkeeping); the
// interactive loop and the S6 selftest share this, so a reset/replay runs the
// same sequence.
void advance_death_frame(DeathState& death, float dt, Playback& entityPlayback, int deathClip,
                         const CorpseAttachment& corpse, const ClubAttachment& club,
                         Scene& scene, std::size_t entityRig) {
    death = step_death(death, dt, true);
    if (deathClip >= 0 && entityPlayback.index == deathClip) {
        entityPlayback.time = std::min(death.total, kMotionSeconds);
        entityPlayback.pinned = true;
        entityPlayback.playing = false;
    }
    apply_death_visibility(scene, corpse, club, entityRig, death_corpse_visible(death));
}

// S6: the club whoosh, fired once at the swing phase of an in-progress club
// attack (`kValusClubSwingSeconds`, the impact's lead — not the telegraph).
// The app calls this after `advance_valus_attack`; the frozen S5 timeline does
// not, so its 20-event contract cannot gain a club_swing.
void maybe_club_swing(Encounter& encounter, SfxQueue* sfx) {
    if (encounter.attack != ValusAttack::Club || encounter.attackSwinged) {
        return;
    }
    if (!(encounter.attackSeconds >= kValusClubSwingSeconds)) {
        return;
    }
    encounter.attackSwinged = true;
    if (sfx != nullptr) {
        append_encounter_sfx(EncounterSfx::ClubSwing, encounter.entityPosition, *sfx);
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
// The player state machine clamps a frame to this; Valus's attack clock and yaw
// use the same ceiling so one long frame cannot skip an impact window.
constexpr float kMaxCombatStepSeconds = 0.05f;
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

// --- S4.2 acceptance timeline (--s42-selftest) ------------------------------

constexpr int kS42ChargeFrames = 60;          // held J past the 0.9 s full charge
constexpr int kS42MaxRouteFrames = 600;       // 10 s: chest -> shoulder -> arm
constexpr int kS42MaxClimbFrames = 1800;      // 30 s: the full foot -> head climb
constexpr int kS42MaxReactionFrames = 600;    // 10 s: wait out a reaction one-shot
constexpr int kS42MaxAttackFrames = 900;      // 15 s: walk an attack past its window
constexpr int kS42MaxDeathFrames = 600;       // 10 s: the player death clip
constexpr float kS42StompStandoff = 1.0f;     // m from the impact foot (direct hit)
constexpr float kS42ClubMissStandoff = 4.0f;   // m from OptBone_hit: outside the 2.5 m hit
constexpr float kS42StompWaveStandoff = 3.0f;  // m from the foot: 2.5 < d <= the 6 m wave
constexpr float kS42ArmStrikeT = 1.0f;        // bandT on the right arm that reaches shell 1
constexpr float kS42MinClimbRise = 10.0f;     // m the full foot -> head route must gain
constexpr float kS42Eps = 1e-3f;              // shell, ground and contact epsilon
constexpr int kS42TelegraphLeadFrames = 5;    // stills: capture this many steps before an impact

// --- S6 death wiring and self-test (Task 5) ---------------------------------

// The corpse assets (kCorpseGlb/kDirtmoundGlb) are declared with the club's,
// above: the load precedes the renderer's one-shot upload.
constexpr int kS6MaxSwingFrames = 240;  // 4 s: walk the club clip to its swing phase
constexpr int kS6MaxDeathFrames = 600;  // 10 s: the death machine to Done
constexpr float kS6Eps = 1e-4f;         // the stills' phase-time epsilon
// The effect pass's price at the tendril phase (Task 6's perf gate): the same
// 240-frame pair the S4.1/S4.2 render gates use, once with the pass drawing
// and once with it suppressed from the same pose and camera.
constexpr int kS6PerfFrames = 240;

// --- S7 return and self-test (Task 9) ---------------------------------------

constexpr int kS7MaxArenaFrames = 1200;   // 20 s: Hold + FadeOut twice, to Load
constexpr int kS7MaxShrineFrames = 2400;  // 40 s: FadeIn + the ~11.8 s wake, to Free
constexpr int kS7MaxWallFrames = 900;     // 15 s: the scripted walk toward the wall probe
constexpr int kS7PerfFrames = 240;
constexpr float kS7Eps = 1e-4f;
constexpr float kS7WakeStillFraction = 0.85f;  // wake.png: the rise, the altar ahead
// fade.png and the injected R: just past the middle of the first FadeOut, where
// the still reads as near-white over the arena (the acceptance's luma gate).
constexpr float kS7FadeStillFade = 0.6f;

// --s7-e2e (Task 13): the script's three waits, and a generous budget for
// each stage (the arena's 1 + 8 + 2 + 1 s, the shrine's 1.5 + ~11.8 + 1 s).
constexpr float kS7E2eLeadSeconds = 1.0f;  // arena before the hit; Free and arena before R/exit
constexpr int kS7E2eMaxStageFrames = 3600;  // 60 s at 1/60 s
constexpr float kS7E2eFloorTolerance = 0.05f;  // the feet vs heightmap_floor at Free

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
// The S4.2 Valus attack clips (design §7.4): the two stomps and the club.
constexpr const char* kStompLeftClip = "minotaur_a_atk_leg_l";
constexpr const char* kStompRightClip = "minotaur_a_atk_leg_r";
constexpr const char* kClubSwingClip = "minotaur_a_atk_weapon2_a";

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

// Projects one world point into `box`, growing it. Points behind the camera or
// with a non-finite clip position are skipped, exactly like the draw path.
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

// The club's projected screen region. The club is unskinned static geometry:
// its live placement is one model matrix (the posed socket frame composed with
// its authored bind frame), so every vertex is that matrix applied to the bind
// position. It is a separate mesh range outside the skinned bbox — the S4
// entity bounds union it so the guards cover what the entity actually draws.
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

// The smallest box containing both inputs (an empty input is the identity).
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
// whole run. compose_scene baked the manifest TRS into each of the entity's
// mesh matrices; the manifest y is the ground sample, so the rig's
// origin-to-feet lift raises the model's origin to `y + lift` — 0 when the
// entity's clip already carries the root series (the S2 rule, shared with the
// player). The player's meshes are not touched here: its transform is rewritten
// from its own lift path each frame. In an encounter the first entity's model is
// immediately replaced by update_entity_transform (ruling R8), which recomposes
// the same product from the stored transform; the lift it uses is the same one
// computed here.
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

// Ruling R8: recompose an entity's mesh models from the transform captured in
// Encounter and the live `entityYawDeg`. compose_scene baked T * R(yaw) * S
// into every mesh, and apply_rig_lift appended the rig-local grounding lift;
// this rebuilds that product with the current yaw, so the whole body — and the
// club, which reads the live first-mesh model — turns. All meshes of one
// entity share its placement model (compose_scene gives them the same TRS), so
// one matrix covers the rig's range. A no-op until the encounter captured a
// transform, and for a rig index the scene does not have.
void update_entity_transform(Scene& scene, const Encounter& encounter,
                             std::size_t rigIndex) {
    if (!encounter.entityTransformSet || rigIndex >= scene.rigs.size()) {
        return;
    }
    const RigCpu& rig = scene.rigs[rigIndex];
    const std::size_t end = std::min(rig.meshEnd, scene.meshes.size());
    const float half_angle = 0.5f * encounter.entityYawDeg * kDegToRad;
    const Quat rotation{0.0f, std::sin(half_angle), 0.0f, std::cos(half_angle)};
    const Mat4 base =
        mat4_from_trs(encounter.entityPosition, rotation, encounter.entityScale);
    const Mat4 lift =
        mat4_from_trs(Vec3{0.0f, encounter.entityLift, 0.0f},
                      Quat{0.0f, 0.0f, 0.0f, 1.0f}, Vec3{1.0f, 1.0f, 1.0f});
    const Mat4 model = mat4_mul(base, lift);
    for (std::size_t i = std::min(rig.meshBegin, end); i < end; ++i) {
        scene.meshes[i].model = model;
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
// The heavy hurt pair (`player_a_damage_h_fly1` / `..._h_fly_end`) is not a
// field: the mode selector plays it as one timed pair, and the load gate below
// requires both by name through kHeavyHurtClip / kHeavyHurtEndClip.
struct PlayerClips {
    int gripLoop = -1;
    int gripMove = -1;
    int strike = -1;
    int fall = -1;
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
    int stompL = -1;
    int stompR = -1;
    int club = -1;
};

PlayerClips resolve_player_clips(const Playback& playback) {
    return PlayerClips{clip_index_named(playback, kGripLoopClip),
                       clip_index_named(playback, kGripMoveClip),
                       clip_index_named(playback, kGripAttackClip),
                       clip_index_named(playback, kFallClip),
                       clip_index_named(playback, kPlayerDeathClip)};
}

EntityClips resolve_entity_clips(const Playback& playback, const std::string& standClip) {
    return EntityClips{clip_index_named(playback, standClip),
                       clip_index_named(playback, kHeadReactionClip),
                       clip_index_named(playback, kArmReactionClip),
                       clip_index_named(playback, kBodyReactionClip),
                       clip_index_named(playback, kDeathClip),
                       clip_index_named(playback, kStompLeftClip),
                       clip_index_named(playback, kStompRightClip),
                       clip_index_named(playback, kClubSwingClip)};
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
//
// `entityLive = false` is the S6 corpse state (design §8.3): the dead colossus
// is a static prop, so the frame carries no climb joints, no weakpoint shells
// and no collision capsules — it cannot be grabbed, climbed, struck or walked
// into. The lookups are left empty (player_state/climb_band_segment guard an
// empty lookup) and the frozen timelines pass the default true.
PlayerFrame player_frame_of(const Scene& scene, std::size_t entityRig,
                            const std::vector<std::vector<Mat4>>& palettes,
                            const Heightmap& heightmap, float cameraYawDeg,
                            bool entityLive = true) {
    PlayerFrame frame;
    frame.scene = &scene;
    frame.heightmap = &heightmap;
    frame.entityRig = entityRig;
    frame.cameraYawDeg = cameraYawDeg;
    if (entityLive && entityRig < palettes.size()) {
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
    if (entityLive) {
        frame.entityCapsules = entity_capsules(scene, palettes);
    }
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
        // Death: play once, then hold the final frame. No cooldown survives a
        // defeat: there is no next attack.
        playback.time = duration;
        playback.pinned = true;
        playback.playing = false;
        encounter.reactionClip = -1;
        encounter.reactionSeconds = duration;
        encounter.pendingCooldownSeconds = 0.0f;
        return;
    }
    // Reaction over: back to the entity's own loop from its first frame.
    if (standClip >= 0) {
        playback.index = standClip;
        playback.time = 0.0f;
        playback.pinned = false;
        playback.playing = true;
    }
    // An attack this reaction interrupted starts its cooldown now, after the
    // reaction, not when the strike cut the clip short (design §7.4).
    if (encounter.pendingCooldownSeconds > 0.0f) {
        encounter.cooldownSeconds = encounter.pendingCooldownSeconds;
        encounter.pendingCooldownSeconds = 0.0f;
    }
    encounter.reactionClip = -1;
    encounter.reactionSeconds = 0.0f;
}

// --- S4.2 Valus attacks -----------------------------------------------------

// The clip an attack plays, or -1 when the resolved table lacks it. The load
// gate below refuses a fight manifest without all three, so -1 is defensive.
int valus_attack_clip(ValusAttack attack, const EntityClips& clips) {
    switch (attack) {
        case ValusAttack::StompL:
            return clips.stompL;
        case ValusAttack::StompR:
            return clips.stompR;
        case ValusAttack::Club:
            return clips.club;
        case ValusAttack::None:
            break;
    }
    return -1;
}

// The posed joint the attack's damage is measured from: the attacking foot for
// a stomp, the club's tip for the swing (design §7.4).
const char* valus_attack_damage_joint(ValusAttack attack) {
    switch (attack) {
        case ValusAttack::StompL:
            return "Bone_TUMASAKI_L";
        case ValusAttack::StompR:
            return "Bone_TUMASAKI_R";
        case ValusAttack::Club:
            return "OptBone_hit";
        case ValusAttack::None:
            break;
    }
    return nullptr;
}

// Cuts an in-progress attack short. The cooldown it would have started is
// parked in pendingCooldownSeconds: design §7.4 starts it after the reaction,
// not at the interruption. The caller owns the playback (the reaction clip
// replaces the attack clip in the same step). S5's `ClubCancel` cue is deferred
// to silence, but the transition is still reported.
void cancel_valus_attack(Encounter& encounter, SfxQueue* sfx = nullptr) {
    if (encounter.attack == ValusAttack::None) {
        return;
    }
    encounter.pendingCooldownSeconds = valus_attack_cooldown_seconds(encounter.attack);
    encounter.attack = ValusAttack::None;
    encounter.attackSeconds = 0.0f;
    encounter.attackHit = false;
    encounter.attackSwinged = false;
    if (sfx != nullptr) {
        append_encounter_sfx(EncounterSfx::ClubCancel, encounter.entityPosition, *sfx);
    }
}

// Starts one attack at its first frame. The attack clock is owned by
// encounter.attackSeconds, exactly like the reaction clock: update_pose only
// advances a playing clip, so the playback flag stays false and the app writes
// the sampled time each frame. That keeps the drawn pose and the impact window
// on one clock — a playing flag plus a second clock would drift a frame apart.
void start_valus_attack(Encounter& encounter, Playback& playback, const EntityClips& clips,
                        ValusAttack attack, SfxQueue* sfx = nullptr) {
    const int clip = valus_attack_clip(attack, clips);
    if (clip < 0 || static_cast<std::size_t>(clip) >= playback.clips.size()) {
        return;
    }
    encounter.attack = attack;
    encounter.attackSeconds = 0.0f;
    encounter.attackHit = false;
    encounter.attackSwinged = false;
    playback.index = clip;
    playback.time = 0.0f;
    playback.pinned = false;
    playback.playing = false;  // encounter.attackSeconds advances this clip
    if (sfx != nullptr) {
        // The first frame of the clip is the telegraph: the pose that warns is
        // also the sound that warns.
        append_encounter_sfx(attack == ValusAttack::Club ? EncounterSfx::ClubStart
                                                         : EncounterSfx::StompStart,
                             encounter.entityPosition, *sfx);
    }
}

// One frame of Valus's attack state: tick the cooldown, start the selected
// attack, advance the active clip and return to stand when it ends. The impact
// is deliberately not applied here — the caller checks attack_impact_due after
// the player's step, so a strike that lands this frame cancels the attack and
// its pending impact first. A reaction clip owns the body while it plays: no
// new attack starts until it has returned to stand.
void advance_valus_attack(Encounter& encounter, Playback& playback,
                          const EntityClips& clips, const ValusAttackInput& input,
                          float dtSeconds, SfxQueue* sfx = nullptr) {
    if (encounter.defeated) {
        // Defeat drops a pending swing without a cooldown: there is no next
        // attack, and the death clip owns the playback.
        encounter.attack = ValusAttack::None;
        encounter.attackSeconds = 0.0f;
        encounter.attackHit = false;
        encounter.attackSwinged = false;
        encounter.pendingCooldownSeconds = 0.0f;
        return;
    }

    if (std::isfinite(dtSeconds) && dtSeconds > 0.0f) {
        encounter.cooldownSeconds = std::max(0.0f, encounter.cooldownSeconds - dtSeconds);
    } else if (!std::isfinite(encounter.cooldownSeconds)) {
        encounter.cooldownSeconds = 0.0f;
    }

    if (encounter.attack == ValusAttack::None) {
        if (encounter.reactionClip >= 0) {
            return;
        }
        const ValusAttack chosen = select_valus_attack(input);
        if (chosen == ValusAttack::None) {
            return;
        }
        const int clip = valus_attack_clip(chosen, clips);
        if (clip < 0 || static_cast<std::size_t>(clip) >= playback.clips.size() ||
            !(playback.clips[static_cast<std::size_t>(clip)].duration > 0.0f)) {
            return;  // nothing to play: never start a zero-length telegraph
        }
        start_valus_attack(encounter, playback, clips, chosen, sfx);
        if (chosen == ValusAttack::StompL || chosen == ValusAttack::StompR) {
            encounter.stompLeftNext = chosen == ValusAttack::StompR;
        }
        return;  // t = 0 is outside both impact windows
    }

    const int clip = valus_attack_clip(encounter.attack, clips);
    if (clip < 0 || static_cast<std::size_t>(clip) >= playback.clips.size()) {
        encounter.attack = ValusAttack::None;
        encounter.attackSeconds = 0.0f;
        encounter.attackHit = false;
        encounter.attackSwinged = false;
        return;
    }
    const float duration = playback.clips[static_cast<std::size_t>(clip)].duration;
    if (std::isfinite(dtSeconds) && dtSeconds > 0.0f) {
        encounter.attackSeconds += dtSeconds;
    }
    if (encounter.attackSeconds >= duration) {
        // Clip over: back to the entity's own loop, then the cooldown starts.
        encounter.cooldownSeconds = valus_attack_cooldown_seconds(encounter.attack);
        encounter.attack = ValusAttack::None;
        encounter.attackSeconds = 0.0f;
        encounter.attackHit = false;
        encounter.attackSwinged = false;
        if (clips.stand >= 0) {
            playback.index = clips.stand;
            playback.time = 0.0f;
            playback.pinned = false;
            playback.playing = true;
        }
        return;
    }
    playback.time = encounter.attackSeconds;  // the drawn pose and the window agree
}

// Applies one attack's damage at the frame its window first opens (the caller
// marks attackHit before calling, so it can never fire twice). The reach is the
// distance from the posed damage joint to the player's capsule segment (feet to
// feet + kPlayerCapsuleHeight), measured by player_capsule_distance — the same
// shape and the same function the weakpoint gate uses. An attached player takes
// no damage (design §7.4); Dead and the i-frame window are player_take_damage's
// own gates.
//
// S5: `sfx`, when given, banks the impact once per call — the world sound fires
// even on a miss, because the club really hits the ground. `events`, when
// given, receives the hurt/death report flags for the same hit.
void apply_valus_impact(PlayerCombat& combat, const PlayerState& player,
                        const PlayerFrame& frame, const CombatTuning& tuning,
                        ValusAttack attack, SfxQueue* sfx = nullptr,
                        StepEvents* events = nullptr) {
    if (attack == ValusAttack::None) {
        return;
    }
    const char* joint = valus_attack_damage_joint(attack);
    Vec3 point{};
    const bool resolved =
        joint != nullptr && static_cast<bool>(frame.jointWorld) && frame.jointWorld(joint, point);
    if (sfx != nullptr && resolved) {
        append_encounter_sfx(attack == ValusAttack::Club ? EncounterSfx::ClubImpact
                                                         : EncounterSfx::StompImpact,
                             point, *sfx);
    }
    if (mode_is_attached(combat.mode)) {
        return;
    }
    if (!resolved) {
        return;
    }
    const float distance = player_capsule_distance(player.position, point);
    if (!std::isfinite(distance)) {
        return;
    }

    float damage = 0.0f;
    bool heavy = false;
    switch (attack) {
        case ValusAttack::StompL:
        case ValusAttack::StompR:
            if (distance <= kValusStompDirectRangeMeters) {
                damage = tuning.stompDamage;
            } else if (distance <= kValusStompWaveRangeMeters) {
                damage = tuning.stompWaveDamage;
            }
            break;
        case ValusAttack::Club:
            if (distance <= kValusClubHitRangeMeters) {
                damage = tuning.clubDamage;
                heavy = true;
            }
            break;
        case ValusAttack::None:
            break;
    }
    if (!(damage > 0.0f)) {
        return;
    }
    const bool hitLanded = player_take_damage(combat, damage, heavy, tuning, events);
    if (sfx != nullptr && hitLanded) {
        // The reaction is the player's, pushed at the moment the damage lands so
        // the cue order is impact -> hurt. A ground-lethal hit is the death; an
        // air-lethal hit stays a hurt here and reports `died` on landing.
        const Sfx reaction = combat.mode == PlayerMode::Dead
                                 ? Sfx::Death
                                 : (heavy ? Sfx::HurtHeavy : Sfx::HurtLight);
        sfx->push(SfxEvent{reaction, 1.0f, player.position, /*spatial=*/true});
    }
}

// The per-frame attack decision input: the posed feet from the last palette
// (posed_joint_world_position reads the live mesh model, so a yaw step taken
// earlier this frame is already included), the planar player/entity transform,
// and the mode gate. A foot that cannot resolve is pushed to infinity, which
// reads as out of range rather than as a stomp at the world origin.
ValusAttackInput valus_attack_input(const Scene& scene, std::size_t entityRig,
                                    const std::vector<std::vector<Mat4>>& palettes,
                                    const PlayerState& player, const PlayerCombat& combat,
                                    const Encounter& encounter) {
    ValusAttackInput input;
    input.playerGrounded = combat.mode == PlayerMode::Grounded;
    input.playerAttached = mode_is_attached(combat.mode);
    input.defeated = encounter.defeated;
    input.cooldownSeconds = encounter.cooldownSeconds;
    input.playerPos = player.position;
    input.entityPos = encounter.entityPosition;
    input.entityYawDeg = encounter.entityYawDeg;
    input.stompLeftNext = encounter.stompLeftNext;
    const float far = std::numeric_limits<float>::infinity();
    input.footL = Vec3{far, far, far};
    input.footR = Vec3{far, far, far};
    if (entityRig < palettes.size()) {
        Vec3 foot{};
        if (posed_joint_world_position(scene, entityRig, palettes[entityRig],
                                       "Bone_TUMASAKI_L", foot)) {
            input.footL = foot;
        }
        if (posed_joint_world_position(scene, entityRig, palettes[entityRig],
                                       "Bone_TUMASAKI_R", foot)) {
            input.footR = foot;
        }
    }
    return input;
}

// Applies one step's damage to the encounter and routes the entity's reaction:
// weakpoint 0 (forehead) -> the head reaction, 1 (right arm) -> the arm
// reaction, any other band -> the generic body reaction. A killing blow
// supersedes the reaction and starts the death clip. A defeated entity takes
// no further damage and no second death. A landed strike also cancels Valus's
// in-progress attack and its pending impact (design §7.4); a whiff never has
// damageDealt > 0, so it reaches none of this.
//
// S5/S6: `sfx`, when given, banks Valus's reaction at the entity's world
// position. The defeat itself queues nothing: its roar is the tendril burst's
// sound at 0.4 s, and the app queues it then (S6, design §8.4). The killing
// blow's weakpoint is recorded for the tendril roots.
void apply_combat_events(Encounter& encounter, Playback& entityPlayback,
                         const EntityClips& clips, const StepEvents& events, int weakpoint,
                         SfxQueue* sfx = nullptr) {
    if (events.damageDealt <= 0 || encounter.defeated) {
        return;
    }
    if (valus_attack_cancelled(/*landedStrike=*/true, encounter.attack)) {
        cancel_valus_attack(encounter, sfx);
    }
    encounter.health =
        std::max(0.0f, encounter.health - static_cast<float>(events.damageDealt));
    if (encounter.health <= 0.0f) {
        encounter.defeated = true;
        encounter.pendingCooldownSeconds = 0.0f;
        encounter.deathWeakpoint = weakpoint;
        start_encounter_one_shot(entityPlayback, encounter, clips.death);
        return;
    }
    const int reaction =
        weakpoint == 1 ? clips.arm : (weakpoint == 0 ? clips.head : clips.body);
    start_encounter_one_shot(entityPlayback, encounter, reaction);
    if (sfx != nullptr) {
        append_encounter_sfx(EncounterSfx::ValusReaction, encounter.entityPosition, *sfx);
    }
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
        strike = SDL_GetGamepadButton(handle, kStrikeGamepadButton);
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

// `<dir>/<name>.png`, whether or not `dir` carries its trailing slash.
std::string still_png_path(std::string dir, const char* name) {
    if (!dir.empty() && dir.back() != '/') {
        dir += '/';
    }
    return dir + name + ".png";
}

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

// --- S5 audio seam (Task 5) -------------------------------------------------

// The app's one audio object: the mixer, the music lifecycle, and — for
// --s5-selftest — the ordered cue log and the WAV capture. One translation path
// (play_event) serves the interactive loop and the shared scripted timeline, so
// their sound cannot drift. With the engine off (`enabled == false`) every
// method is a no-op, which is what keeps the default S4.2 run byte-identical.
struct AudioSeam {
    AudioEngine engine;
    bool enabled = false;       // the manifest and every mapped clip loaded
    bool log = false;           // --s5-selftest/--s6-selftest: one line per played cue
    // The log line's tag: `s5-event` for the frozen S5 acceptance, `s6-event`
    // for the S6 selftest. The format is otherwise identical (Task 6 parses it).
    const char* logPrefix = "s5-event";
    bool failed = false;        // a mapped clip went missing at play time
    bool musicStarted = false;  // the music voice is live
    bool musicEverStarted = false;
    bool defeatStop = false;    // --s5-selftest's post-reset defeat phase ran
    float clock = 0.0f;         // simulated seconds since the capture began
    int eventCount = 0;         // s5-event lines emitted

    // The S6 slice's two rows are optional for the interactive game: an
    // S5-only manifest still loads and plays, and a missing S6-only clip warns
    // **once** and stays silent with the mixer running. The S6 selftest
    // demands the rows, so there the same condition is the fatal error it has
    // always been (`requireS6`).
    bool requireS6 = false;
    std::array<bool, static_cast<std::size_t>(Sfx::Count)> warnedMissing{};

    // Loads `libraryDir/audio/audio.manifest` and validates the cue table. The
    // engine names its own manifest failure; a rejected cue names its `Sfx` and
    // the clip. The caller picks the policy (fatal in --s5-selftest, one warning
    // and a silent game otherwise) and the slice: `includeS6` false requires the
    // S5 rows only, so the frozen S5 acceptance's eight-entry manifest stays
    // loadable beside the S6 cue table; true requires every row, including the
    // death and the club swing (Task 5's --s6-selftest).
    bool load(const std::string& libraryDir, bool includeS6) {
        enabled = false;
        const std::string manifest = libraryDir + "/audio/audio.manifest";
        if (!engine.load_manifest(manifest, libraryDir)) {
            return false;
        }
        const Sfx missing = missing_cue(
            [this](const char* clip) { return engine.has(clip); }, includeS6);
        if (missing != Sfx::Count) {
            const SoundCue* cue = cue_for(missing);
            std::fprintf(stderr,
                         "sotc-viewer: audio: cue %s names clip '%s', which %s does not load\n",
                         sfx_name(missing), cue != nullptr ? cue->clip : "?", manifest.c_str());
            engine.reset();
            return false;
        }
        // The music is not an `Sfx`, so `missing_cue` cannot see it; the score
        // is as mandatory as the fight cues (D5), so gate it here — a manifest
        // without it fails at load, not as "a cue clip was missing at play time".
        if (!engine.has(kMusicClip)) {
            std::fprintf(
                stderr,
                "sotc-viewer: audio: music cue names clip '%s', which %s does not load\n",
                kMusicClip, manifest.c_str());
            engine.reset();
            return false;
        }
        enabled = true;
        return true;
    }

    // The one place a queued cue becomes a voice (and an s5-event line).
    void play_event(const SfxEvent& event) {
        const SoundCue* cue = cue_for(event.id);
        if (cue == nullptr || !enabled) {
            return;  // deferred, or audio off: deliberately silent
        }
        if (!engine.has(cue->clip)) {
            if (cue->s6Only && !requireS6) {
                // The interactive game with an S5-only manifest: the S6 rows
                // are optional there, so the cue warns ONCE and stays silent
                // (the mixer keeps running; the game is otherwise intact).
                // The S6 selftest demands the row, so there the same condition
                // is the fatal error below.
                const std::size_t index = static_cast<std::size_t>(event.id);
                if (index < warnedMissing.size() && !warnedMissing[index]) {
                    warnedMissing[index] = true;
                    std::fprintf(stderr,
                                 "sotc-viewer: audio: cue %s names missing clip '%s' — the cue "
                                 "stays silent\n",
                                 sfx_name(event.id), cue->clip);
                }
                return;
            }
            std::fprintf(stderr, "sotc-viewer: audio: cue %s names missing clip '%s'\n",
                         sfx_name(event.id), cue->clip);
            failed = true;
            return;
        }
        const float gain = cue->gain * event.gain;
        if (event.spatial) {
            engine.play(cue->clip, event.position, gain);
        } else {
            engine.play_ui(cue->clip, gain);
        }
        if (log) {
            // `clip=` is the R1 manifest `name=` (e.g. `valus.impact`), not
            // `bank/name`: the engine indexes clips by that name and exposes no
            // bank. Ledger R16 makes the manifest name the log contract; Task 6
            // parses these tokens.
            std::printf("%s t=%.3f event=%s clip=%s gain=%.2f\n", logPrefix, clock,
                        sfx_name(event.id), cue->clip, gain);
            ++eventCount;
        }
    }

    void play_all(SfxQueue& queue) {
        for (int i = 0; i < queue.count; ++i) {
            play_event(queue.items[static_cast<std::size_t>(i)]);
        }
        queue.clear();
    }

    void start_music() {
        if (!enabled || musicStarted) {
            return;
        }
        if (!engine.has(kMusicClip)) {
            failed = true;
            return;
        }
        engine.play_music(kMusicClip, kMusicGain);
        musicStarted = true;
        musicEverStarted = true;
        if (log) {
            std::printf("%s t=%.3f event=music_start clip=%s gain=%.2f\n", logPrefix, clock,
                        kMusicClip, kMusicGain);
            ++eventCount;
        }
    }

    void stop_music() {
        // Idempotent by design: the defeat path calls this every frame while
        // `encounter.defeated`, and only the transition may touch the mixer or
        // the log. The first stop also leaves the queued SFX alone (audio.cpp).
        if (!enabled || !musicStarted) {
            return;
        }
        engine.stop_music();
        musicStarted = false;
        if (log) {
            std::printf("%s t=%.3f event=music_stop clip=%s gain=0.00\n", logPrefix, clock,
                        kMusicClip);
            ++eventCount;
        }
    }

    void reset() {
        if (!enabled) {
            return;
        }
        // D5's reset stop must be observable: the reset emits the same
        // `music_stop` the defeat path does (only while the music is actually
        // live — stop_music is idempotent), then every voice goes.
        stop_music();
        engine.reset();  // every voice, music included
    }

    // The stage loop (S7): each stage after the first starts with the seam's
    // per-stage state cleared and every voice stopped. The engine (its device
    // and mute) persists; a stage that wants sound loads the manifest again
    // (the shrine has no encounter, so it stays silent — design D10).
    void new_stage() {
        if (enabled) {
            engine.reset();
        }
        enabled = false;
        log = false;
        logPrefix = "s5-event";
        failed = false;
        musicStarted = false;
        musicEverStarted = false;
        defeatStop = false;
        clock = 0.0f;
        eventCount = 0;
        requireS6 = false;
        warnedMissing = {};
    }

    void toggle_mute() {
        if (enabled) {
            engine.set_muted(!engine.muted());
        }
    }

    void update(float dt, const Vec3& listener, float yaw) {
        if (!enabled) {
            return;
        }
        engine.update(dt, listener, yaw);
        if (std::isfinite(dt) && dt > 0.0f) {
            clock += std::min(dt, 1.0f);  // the engine's own dt clamp
        }
    }
};

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
        // The S5 audio engine lives for the run; each stage decides what it loads.
        AudioSeam audio;

        // The stage loop (S7 D6): the window, the device, the renderer and the
        // audio engine outlive every stage; each iteration composes one stage,
        // runs it and ends with a StageRequest. The frozen selftests still
        // return from inside the iteration and never request a switch; the
        // return to the shrine (interactive play, --s7-selftest) and R from the
        // shrine do. `carry` is what crosses (design D6).
        StageId stage = opts.stage;
        StageCarry carry;
        if (stage == StageId::Shrine) {
            // --stage shrine starts with control (D11): the return is over.
            carry.ret.phase = ReturnPhase::Free;
        }
        bool first_stage = true;
        // R16: cleared (once, with one warning) when interactive play finds the
        // shrine manifest or the wake clips missing; the run then plays on
        // without the return, as --no-return would.
        bool return_available = true;
        for (;;) {
            // The stage's manifest: --manifest for the arena, the shrine's own
            // composed manifest for the shrine.
            const bool shrine = stage == StageId::Shrine;
            const std::string& stage_manifest_path =
                shrine ? opts.shrineManifestPath : opts.manifestPath;
            if (!first_stage) {
                audio.new_stage();
            }
            first_stage = false;
            const Uint64 stage_load_start = SDL_GetTicksNS();

            // Scene: a single glb (--model alone) or a composed manifest scene
            // (--manifest, with --model as the player's glb). `Scene scene{}` keeps
            // zeroed bounds for a model-less run, so camera_frame stays finite even
            // when there is nothing to draw.
            Scene scene{};
            // The entity prop: loaded only in a composed scene with an entity (see
            // load_club); identity/unloaded everywhere else.
            ClubAttachment club{};
            // The S6 corpse prop and the tendril pass's readiness: both are loaded
            // only when the death is wired (see below).
            CorpseAttachment corpse{};
            bool effects_ready = false;
            SceneManifest manifest{};          // empty when --manifest is absent
            Heightmap heightmap{};             // an empty heightmap = no ground
            const bool follow_mode = !opts.manifestPath.empty();
            // S6: whether this run wires the death sequence (the interactive game
            // and the --s6-selftest). The frozen scripted timelines (s3/s4/s41/
            // s42/s5), the pinned captures and the plain screenshot runs must not
            // load the corpse or the effect textures: their outputs and pixel
            // stats are the regression.
            const bool death_wired =
                opts.s6Selftest ||
                (!opts.selftest && !opts.s3Selftest && !opts.s4Selftest && !opts.s41Selftest &&
                 !opts.s42Selftest && !opts.s5Selftest && !opts.entityTimeSet &&
                 opts.screenshotPath.empty());
            // S7: interactive play steps the return machine (unless --no-return,
            // D13), and so does --s7-selftest. No frozen selftest (--selftest,
            // --s3 ... --s6-selftest) or capture ever does: S4.1's selftest
            // defeats Valus for real, and its output is the regression.
            const bool interactive_run =
                !opts.selftest && !opts.s3Selftest && !opts.s4Selftest && !opts.s41Selftest &&
                !opts.s42Selftest && !opts.s5Selftest && !opts.s6Selftest && !opts.s7Selftest &&
                !opts.entityTimeSet && opts.screenshotPath.empty();
            bool return_wired = follow_mode && return_available &&
                                (opts.s7Selftest || (interactive_run && !opts.noReturn));
            if (follow_mode) {
                // The manifest is user data: a malformed line is a usage error
                // (exit 2) carrying the file and line, never a crash.
                try {
                    manifest = parse_manifest(stage_manifest_path);
                } catch (const std::exception& error) {
                    std::fprintf(stderr, "sotc-viewer: %s%s\n", error.what(),
                                 shrine ? " (compose it with `make shrine-manifest`)" : "");
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
                // The load runs on this thread: keep the window answering the
                // system meanwhile (no "not responding" during the shrine load).
                // Only the S7 runs pump — the frozen timelines load as before.
                const bool pump_load = opts.s7Selftest || interactive_run;
                scene = compose_scene(opts.libraryDir, manifest, opts.modelRelPath, nullptr,
                                      pump_load ? std::function<void()>([] { SDL_PumpEvents(); })
                                                : std::function<void()>());

                // The ground is built from the scene's static meshes only: the
                // player's own body and the placed entities must never rasterize
                // into the ground they stand on. A scene with no ground at all (no
                // static meshes) leaves the heightmap empty — ground=none, no
                // clamp. This runs before the club is appended: the club is static
                // geometry (rig = -1) but it is held in the air, not terrain, and
                // must never raise the heightmap under Valus.
                try {
                    // The shrine is a building (floors, a roof, walls): its
                    // ground is layered (design D4/D12); the arena's is not.
                    heightmap = build_heightmap(scene, kHeightmapCellSize, /*layered=*/shrine);
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
                // S6: the corpse, hidden until Collapse. Also a static append, so it
                // must precede upload() too; a scene the death cannot reach never
                // loads it (the frozen timelines' mesh counts stay untouched).
                if (death_wired && !manifest.entities.empty()) {
                    corpse = load_corpse(scene, opts.libraryDir, scene.playerRig + 1);
                }
                renderer.upload(scene);
                print_scene_line(scene);
                // S6: the tendril pass's four shipped textures (design §8.2). The
                // S6 selftest cannot prove the burst without them, so a load
                // failure there is fatal; the interactive game degrades to a death
                // without tendrils with one warning (never throws — Task 4's
                // contract).
                if (death_wired && !manifest.entities.empty()) {
                    try {
                        effects_ready =
                            renderer.upload_effect_textures(load_effect_textures(opts.libraryDir));
                    } catch (const std::exception& error) {
                        if (opts.s6Selftest) {
                            std::fprintf(stderr, "sotc-viewer: --s6-selftest: %s\n", error.what());
                            return 1;
                        }
                        std::fprintf(stderr, "sotc-viewer: the tendril effect is unavailable: %s\n",
                                     error.what());
                    }
                }
                if (opts.s7Selftest || interactive_run) {
                    const double load_ms =
                        static_cast<double>(SDL_GetTicksNS() - stage_load_start) / 1e6;
                    std::printf("s7-load stage=%s instances=%zu meshes=%zu ms=%.1f\n",
                                shrine ? "shrine" : "arena", manifest.instances.size(),
                                scene.meshes.size(), load_ms);
                    if (opts.s7E2e) {
                        std::printf("s7e2e-load stage=%s meshes=%zu ms=%.1f\n",
                                    shrine ? "shrine" : "arena", scene.meshes.size(), load_ms);
                    }
                }
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
            // The shrine's spawn stands on the floor under it (the layered
            // ground's floor, not the column's roof); the arena's is the
            // manifest's own, unchanged.
            Vec3 stage_spawn = spawn_position(manifest, scene);
            const float stage_spawn_yaw = manifest.hasSpawn ? manifest.spawnYawDeg : 0.0f;
            if (shrine) {
                const float floor = heightmap_floor(heightmap, stage_spawn, kFloorStep);
                if (std::isfinite(floor)) {
                    stage_spawn.y = floor;
                }
            }
            PlayerState player{stage_spawn, stage_spawn_yaw};
            // The combat state machine and the encounter's own state (R5) live for
            // the whole run; the climb camera is entered/left by the mode.
            PlayerCombat combat{};
            Encounter encounter{};
            // One tuning table for every step path — the scripted timelines and the
            // interactive loop — and for the HUD's health fractions.
            const CombatTuning tuning{};
            // One HUD view of the run for every set_hud site (design §7.7). The
            // overlay belongs to an encounter, so a manifest with no entity draws
            // none; Valus's bar is zero-width until the first landed hit, so its
            // appearance is itself the feedback that he can be hurt.
            const auto hud_state = [&](bool visible) {
                const float playerHealth =
                    tuning.maxHealth > 0.0f ? combat.health / tuning.maxHealth : 0.0f;
                const float valusHealth = encounter.health < kEncounterMaxHealth
                                              ? encounter.health / kEncounterMaxHealth
                                              : 0.0f;
                HudState hud{visible && !manifest.entities.empty(), combat.stamina / 100.0f,
                             valusHealth, playerHealth};
                // S7: the return's full-window fade (0 = nothing drawn — every
                // run that never steps the machine).
                hud.fade = carry.ret.fade;
                hud.fadeColour = kShrineFadeColour;
                return hud;
            };
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
                        !require_named_clip(playbacks[entityRig], entityModel, kDeathClip) ||
                        !require_named_clip(playbacks[entityRig], entityModel, kStompLeftClip) ||
                        !require_named_clip(playbacks[entityRig], entityModel, kStompRightClip) ||
                        !require_named_clip(playbacks[entityRig], entityModel, kClubSwingClip)) {
                        return 2;
                    }
                }
            }

            // S7: the return's timings. Hold, fade out and fade in are the
            // shrine's authored flow; Wake is the two baked wake clips played
            // back to back (ruling R5), so their loaded durations are the truth.
            // --s7-selftest and --s7-e2e refuse a player glb without them;
            // interactive play (R16) warns once and plays on without the return.
            ReturnTimings returnTimings{kShrineHoldSeconds, kShrineFadeOutSeconds,
                                        kShrineFadeInSeconds, 0.0f};
            int wakeFirst = -1;
            int wakeSecond = -1;
            float wakeFirstSeconds = 0.0f;
            if (return_wired && !opts.s7Selftest && !opts.s7E2e) {
                std::error_code ec;
                const std::string warning = return_unavailable_warning(
                    opts.shrineManifestPath,
                    std::filesystem::is_regular_file(opts.shrineManifestPath, ec),
                    opts.modelRelPath,
                    clip_index_named(playback, kWakeClipFirst) >= 0 &&
                        clip_index_named(playback, kWakeClipSecond) >= 0);
                if (!warning.empty()) {
                    std::fputs(warning.c_str(), stderr);
                    return_available = false;
                    return_wired = false;
                }
            }
            if (return_wired) {
                if (!require_named_clip(playback, opts.modelRelPath, kWakeClipFirst) ||
                    !require_named_clip(playback, opts.modelRelPath, kWakeClipSecond)) {
                    return 2;
                }
                wakeFirst = clip_index_named(playback, kWakeClipFirst);
                wakeSecond = clip_index_named(playback, kWakeClipSecond);
                wakeFirstSeconds = playback.clips[static_cast<std::size_t>(wakeFirst)].duration;
                returnTimings.wake =
                    wakeFirstSeconds + playback.clips[static_cast<std::size_t>(wakeSecond)].duration;
            }

            // --- S5 audio (Task 5) ------------------------------------------------
            // One engine for the run. The curated manifest loads when the encounter
            // composes — the interactive fight and --s5-selftest. The scripted
            // S3/S4/S4.1/S4.2 timelines and the pinned captures stay silent: they do
            // not even open the manifest, so their `s4-*`/`s41-*`/`s42-*` values
            // cannot move.
            if (opts.s5Selftest && opts.audioMixdownPath.empty()) {
                std::fprintf(stderr, "sotc-viewer: --s5-selftest needs --audio-mixdown PATH\n");
                return 2;
            }
            if (opts.s5Selftest && (manifest.entities.empty() || entityRig >= scene.rigs.size() ||
                                    entityRig >= playbacks.size())) {
                std::fprintf(stderr,
                             "sotc-viewer: --s5-selftest: the manifest has no entity\n");
                return 2;
            }
            if (opts.s6Selftest && (manifest.entities.empty() || entityRig >= scene.rigs.size() ||
                                    entityRig >= playbacks.size())) {
                std::fprintf(stderr,
                             "sotc-viewer: --s6-selftest: the manifest has no entity\n");
                return 2;
            }
            const bool audio_wanted =
                follow_mode && !manifest.entities.empty() &&
                (opts.s5Selftest || opts.s6Selftest ||
                 (!opts.selftest && !opts.s3Selftest && !opts.s4Selftest && !opts.s41Selftest &&
                  !opts.s42Selftest && !opts.entityTimeSet));
            if (audio_wanted) {
                // The S5 slice is the load gate for --s5-selftest and interactive
                // play: both must load the frozen eight-entry manifest beside the
                // S6 cue table. The S6 slice's own gate (--s6-selftest) passes
                // includeS6 = true and requires the death and club-swing rows too.
                if (!audio.load(opts.libraryDir, /*includeS6=*/opts.s6Selftest)) {
                    // The engine (or the cue gate) already printed the reason. A
                    // missing curated clip is fatal when the sound is the artifact;
                    // interactive play degrades to silence with that one warning.
                    if (opts.s5Selftest || opts.s6Selftest) {
                        return 1;
                    }
                }
                audio.log = opts.s5Selftest || opts.s6Selftest;
                if (opts.s6Selftest) {
                    audio.logPrefix = "s6-event";
                    // The S6 slice's rows are mandatory in this run: a missing one
                    // at play time is the fatal error it has always been.
                    audio.requireS6 = true;
                }
            }
            if ((opts.s5Selftest || opts.s6Selftest) && !audio.enabled) {
                std::fprintf(stderr, "sotc-viewer: %s: audio is not loaded\n",
                             opts.s6Selftest ? "--s6-selftest" : "--s5-selftest");
                return 1;
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

            // Ruling R8's autonomous fight (turn, stomp, club) runs in free
            // interactive play. The scripted S3/S4/S4.1 acceptance timelines pin the
            // entity and own every step — a stomp would race their assertions — and
            // --entity-time pins the clip for a capture, so behavior is off there.
            // Task 9's s42 timeline opts in with its own fixed steps.
            const bool valus_behavior = follow_mode && !manifest.entities.empty() &&
                                        entityRig < playbacks.size() && !opts.s3Selftest &&
                                        !opts.s4Selftest && !opts.s41Selftest &&
                                        !opts.s42Selftest && !opts.s5Selftest &&
                                        !opts.entityTimeSet;

            // The composed scene's initial placement: every entity's grounding lift
            // lands on its model transform once, and the player's transform follows
            // its controller every frame. A single-model load has no entity rig, so
            // nothing moves here.
            apply_entity_lifts(scene, playbacks);

            // Ruling R8: capture the encounter entity's manifest transform and
            // recompose its mesh models from `entityYawDeg` (the manifest yaw for
            // now). The lift is the same rig-local grounding offset apply_entity_lifts
            // just applied, measured for the stand clip the entity opens on. All
            // later frames rebuild the same product with the slewed yaw, so the body
            // turns and the club (which reads the live model) follows.
            if (follow_mode && !manifest.entities.empty() && entityRig < scene.rigs.size()) {
                const ManifestEntity& entity = manifest.entities.front();
                encounter.entityPosition = entity.placement.position;
                encounter.entityScale = entity.placement.scale;
                encounter.entityBaseYawDeg = entity_yaw_deg(entity.placement.rotation);
                encounter.entityYawDeg = encounter.entityBaseYawDeg;
                encounter.entityLift = 0.0f;
                if (entityClips.stand >= 0 &&
                    static_cast<std::size_t>(entityClips.stand) <
                        playbacks[entityRig].clips.size()) {
                    encounter.entityLift = effective_rig_lift(
                        scene, entityRig,
                        playbacks[entityRig]
                            .clips[static_cast<std::size_t>(entityClips.stand)]);
                }
                encounter.entityTransformSet = true;
                update_entity_transform(scene, encounter, entityRig);
            }

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
                // No HUD here: a plain --screenshot capture (the S3/S4 acceptance
                // stills) shows the scene alone. The paths that want the overlay —
                // the interactive loop and the --s41-stills renders — feed
                // hud_state() themselves immediately before rendering.
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
                const auto render_still = [&](const std::string& path, bool hudVisible,
                                              bool includeClub = true) {
                    // Select the mode's clip before posing: the step's own clip
                    // selection runs before the step, so the grab frame still
                    // carries the locomotion clip (the one-frame lag Task 6
                    // records). A still is evidence of the mode, so it samples
                    // the mode clip at its current time (0 on entry).
                    const PlayerInput no_input;
                    update_combat_clips(playback, scene, combat, no_input, /*manualClip=*/false);
                    // A control that has translated the club out of view passes
                    // includeClub=false: update_all_poses would otherwise re-place
                    // the club from the socket after that offset.
                    update_all_poses(renderer, scene, playbacks, 0.0f,
                                     includeClub ? club : ClubAttachment{});
                    apply_player_transform(scene, player_rig_record, player,
                                           active_player_lift(scene, playback, player_rig_record));
                    renderer.set_hud(hud_state(hudVisible));
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
                                                 bool hudVisible, bool includeClub = true) {
                    if (opts.s41StillsDir.empty()) {
                        return;
                    }
                    std::printf("s41-control phase=%s path=%s\n", phase, path.c_str());
                    render_still(path, hudVisible, includeClub);
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
                renderer.set_hud(hud_state(/*visible=*/true));
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
                    // floating in the control frame. The control's pose update
                    // skips the club too (includeClub=false), or update_club_model
                    // would re-place it from the socket after this offset.
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
                                    /*hudVisible=*/true, /*includeClub=*/false);
                    for (std::size_t slot = 0; slot < hidden.size(); ++slot) {
                        scene.meshes[hidden[slot]].model = saved_models[slot];
                    }
                    // The HUD control: the same frame with the overlay hidden.
                    capture_control("defeat-nohud", control_path("defeat-nohud"),
                                    /*hudVisible=*/false);
                    renderer.set_hud(hud_state(/*visible=*/true));
                }
                print_status_line(opts, shell.win);
                std::printf("s41-status grab=%s climb=%.2f strikes=%d entity_health=%d mode=%s "
                            "defeated=%d\n",
                            climb_band_name(grabBand), climbRise, strikes,
                            static_cast<int>(std::lround(encounter.health)),
                            player_mode_name(combat.mode), encounter.defeated ? 1 : 0);
                return 0;
            }

            if (opts.s42Selftest || opts.s5Selftest) {
                // The S4.2 acceptance timeline: shell proxies, the foot/head damage
                // gate, the chest->shoulder->arm branch, a stomp with its i-frame
                // gate, a club hit, the player's lethal hit and pinned death clip,
                // the full soft reset, and the real foot->head stamina climb. Fixed
                // 1/60 s steps and no window input. The attack phases place the
                // player (the target) and drive `advance_valus_attack` themselves:
                // the autonomous selection is off for this run so nothing races the
                // assertions. Only the stamina phase is teleport-free; the gate and
                // route phases use the same attach scaffolding S4.1 established.
                //
                // --s5-selftest runs this same stepper with the S5 observer on
                // (design §7.4, ruling D11): the cues are translated once per step,
                // the mix is captured, and `s5-event`/`s5-status` are printed. With
                // audio off every audio branch is skipped, so the `s42-*` values
                // are byte-identical (the audio-off diff is the proof).
                const bool audioOn = audio.enabled;
                // One shared stepper serves two flags: name the invoked one in the
                // load/shape errors so an S5 run never blames --s42-selftest.
                const char* const selftestFlag =
                    opts.s5Selftest ? "--s5-selftest" : "--s42-selftest";
                if (manifest.entities.empty() || entityRig >= scene.rigs.size() ||
                    entityRig >= playbacks.size()) {
                    std::fprintf(stderr, "sotc-viewer: %s: the manifest has no entity\n",
                                 selftestFlag);
                    return 2;
                }
                if (entityClips.stand < 0 || entityClips.head < 0 || entityClips.arm < 0 ||
                    entityClips.body < 0 || entityClips.death < 0 || entityClips.stompL < 0 ||
                    entityClips.stompR < 0 || entityClips.club < 0 || playerClips.gripLoop < 0 ||
                    playerClips.gripMove < 0 || playerClips.strike < 0 || playerClips.fall < 0 ||
                    playerClips.death < 0) {
                    // The load gate above already refused this case; keep the
                    // invariant explicit so no future path can reach a fallback.
                    throw std::runtime_error(std::string(selftestFlag) +
                                             ": a fight clip is missing despite the load gate");
                }

                Playback& entity_playback = playbacks[entityRig];
                const Vec3 spawn = spawn_position(manifest, scene);
                const float spawnYaw = manifest.hasSpawn ? manifest.spawnYawDeg : 0.0f;
                const float stepSeconds = kFixedFrameSeconds;
                const PlayerInput noInput;

                // The S5 frame queue and capture. The capture starts before the
                // first play (a cue would otherwise be missed) and is the disk sink
                // for the whole timeline: `begin_capture` also keeps the engine from
                // opening a device. With audio off the queue is a no-op and no
                // capture exists.
                SfxQueue sfx;
                if (audioOn && !audio.engine.begin_capture(opts.audioMixdownPath)) {
                    throw std::runtime_error("s5-selftest: " + audio.engine.last_error());
                }
                const auto flush_sfx = [&]() {
                    if (audioOn) {
                        audio.play_all(sfx);
                    } else {
                        sfx.clear();
                    }
                };
                // A frame that does not step the state machine (the approach walk)
                // still advances the mix clock, so the capture's duration and the
                // timeline's clock agree.
                const auto tick_audio = [&]() {
                    if (!audioOn) {
                        return;
                    }
                    flush_sfx();
                    audio.update(stepSeconds, player.position, camera_look_yaw(camera));
                };

                // Every shell/route/climb phase samples one pinned stand frame, so
                // the bands and the shells do not drift between steps. The attack
                // phases replace the clip themselves; the reaction wait restores it.
                const auto pin_stand = [&]() {
                    entity_playback.index = entityClips.stand;
                    entity_playback.time = 0.0f;
                    entity_playback.playing = false;
                    entity_playback.pinned = true;
                };
                pin_stand();

                const auto palettes_now = [&]() {
                    return update_all_poses(renderer, scene, playbacks, 0.0f, club);
                };
                const auto frame_of = [&](const std::vector<std::vector<Mat4>>& palettes) {
                    return player_frame_of(scene, entityRig, palettes, heightmap,
                                           camera_look_yaw(camera));
                };

                // One frame of the timeline: the entity's one-shot clock, the
                // attack clip when `driveAttack` is set, the player's mode clip,
                // this frame's poses, the player step, the encounter events, the
                // pending attack impact, and the camera. A placed attack target does
                // not collide: the phase is measuring the damage gate, not walking.
                const auto step_frame = [&](const PlayerInput& input,
                                            bool driveAttack) -> StepEvents {
                    advance_encounter_reaction(entity_playback, encounter, entityClips.stand,
                                               stepSeconds);
                    if (driveAttack && encounter.attack != ValusAttack::None) {
                        ValusAttackInput attackInput;
                        attackInput.playerGrounded = combat.mode == PlayerMode::Grounded;
                        attackInput.playerAttached = mode_is_attached(combat.mode);
                        attackInput.defeated = encounter.defeated;
                        attackInput.cooldownSeconds = encounter.cooldownSeconds;
                        attackInput.playerPos = player.position;
                        attackInput.entityPos = encounter.entityPosition;
                        attackInput.entityYawDeg = encounter.entityYawDeg;
                        advance_valus_attack(encounter, entity_playback, entityClips, attackInput,
                                             stepSeconds, audioOn ? &sfx : nullptr);
                    }
                    update_combat_clips(playback, scene, combat, input, /*manualClip=*/false);
                    const std::vector<std::vector<Mat4>> palettes =
                        update_all_poses(renderer, scene, playbacks, stepSeconds, club);
                    PlayerFrame frame = frame_of(palettes);
                    if (driveAttack) {
                        frame.entityCapsules.clear();
                    }
                    const int weakpoint = combat.weakpoint;  // detach() clears it
                    StepEvents events =
                        player_step(player, combat, input, frame, tuning, stepSeconds);
                    if (audioOn) {
                        // The step's own cues come first: a swing then the hit it
                        // lands, before the encounter's reaction to it. Hurt/death
                        // arrive later, at the impact that caused them.
                        append_player_sfx(events, player.position, sfx);
                    }
                    apply_combat_events(encounter, entity_playback, entityClips, events, weakpoint,
                                        audioOn ? &sfx : nullptr);
                    // Valus's impact runs after the player's step, like the
                    // interactive loop: a landed strike cancels the attack first.
                    if (driveAttack && encounter.attack != ValusAttack::None &&
                        attack_impact_due(encounter.attack, encounter.attackSeconds,
                                          encounter.attackHit)) {
                        encounter.attackHit = true;
                        apply_valus_impact(combat, player, frame, tuning, encounter.attack,
                                           audioOn ? &sfx : nullptr,
                                           audioOn ? &events : nullptr);
                    }
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
                    apply_player_transform(scene, player_rig_record, player,
                                           active_player_lift(scene, playback, player_rig_record));
                    if (audioOn) {
                        flush_sfx();
                        // The first landed hit is the fight music's downbeat (D5);
                        // only Valus's defeat stops it. Wander's death does not: the
                        // score runs through the death clip (D5 names defeat and
                        // reset, not the player's death). Both transitions land on a
                        // step boundary, so the log order is stable.
                        if (events.damageDealt > 0) {
                            audio.start_music();
                        }
                        if (encounter.defeated) {
                            audio.stop_music();
                        }
                        audio.update(stepSeconds, player.position, camera_look_yaw(camera));
                    }
                    return events;
                };

                // Waits out the entity one-shot a landed strike started and returns
                // the body to one pinned stand frame (the next phase's bands must
                // not animate under the route).
                const auto wait_for_reaction = [&]() {
                    int frames = 0;
                    while (encounter.reactionClip >= 0 && frames < kS42MaxReactionFrames) {
                        step_frame(noInput, /*driveAttack=*/false);
                        ++frames;
                    }
                    if (encounter.reactionClip >= 0) {
                        throw std::runtime_error(
                            "s42-selftest: a reaction clip did not finish in " +
                            std::to_string(kS42MaxReactionFrames) + " frames");
                    }
                    pin_stand();
                };

                // Puts the player on `band` at parameter `t` and enters Grabbing,
                // the ruled attach scaffolding of the S4.1 strike phases.
                const auto attach_band_at = [&](ClimbBandId band, float t, Vec3 hint,
                                                const char* phase) {
                    const std::vector<std::vector<Mat4>> palettes = palettes_now();
                    const PlayerFrame frame = frame_of(palettes);
                    Vec3 point{};
                    Vec3 normal{};
                    if (!climb_surface_point(climb_band(band), frame.jointWorld, t, hint, point,
                                             normal)) {
                        throw std::runtime_error(std::string("s42-selftest: ") + phase +
                                                 ": no surface point on the " +
                                                 climb_band_name(band) + " band");
                    }
                    player.position = point;
                    player.yawDeg = std::atan2(-normal.x, -normal.z) / kDegToRad;
                    combat.mode = PlayerMode::Grabbing;
                    combat.modeSeconds = 0.0f;
                    combat.band = band;
                    combat.bandT = t;
                    combat.weakpoint = climb_band(band).weakpoint;
                    combat.charge = 0.0f;
                    combat.strikeHit = false;
                    combat.catchSecondsLeft = 0.0f;
                    combat.fallVelocity = 0.0f;
                };

                // Holds J to a full charge on the current band and releases. The
                // swing always breaks the grip (ruling R7), so the caller samples
                // the damage and the mode before this helper's release step.
                const auto charge_and_release = [&](const char* phase) -> StepEvents {
                    PlayerInput holdGrab;
                    holdGrab.grab = true;
                    step_frame(holdGrab, /*driveAttack=*/false);
                    if (!mode_is_attached(combat.mode)) {
                        throw std::runtime_error(std::string("s42-selftest: ") + phase +
                                                 ": the attach did not hold");
                    }
                    PlayerInput charge;
                    charge.grab = true;
                    charge.strike = true;
                    for (int frame = 0; frame < kS42ChargeFrames; ++frame) {
                        const StepEvents events = step_frame(charge, /*driveAttack=*/false);
                        if (events.damageDealt != 0) {
                            throw std::runtime_error(
                                std::string("s42-selftest: ") + phase +
                                ": the strike dealt damage while still charging at frame " +
                                std::to_string(frame));
                        }
                        if (combat.mode != PlayerMode::Striking) {
                            throw std::runtime_error(
                                std::string("s42-selftest: ") + phase +
                                ": the charge left Striking at frame " + std::to_string(frame) +
                                " (mode " + player_mode_name(combat.mode) + ")");
                        }
                    }
                    if (combat.charge + 1e-4f < tuning.chargeSeconds) {
                        throw std::runtime_error(
                            std::string("s42-selftest: ") + phase + ": the charge reached only " +
                            std::to_string(combat.charge) + " s (need " +
                            std::to_string(tuning.chargeSeconds) + " s)");
                    }
                    PlayerInput release;
                    release.grab = true;
                    return step_frame(release, /*driveAttack=*/false);
                };

                const auto clear_attack = [&]() {
                    encounter.attack = ValusAttack::None;
                    encounter.attackSeconds = 0.0f;
                    encounter.attackHit = false;
                    encounter.cooldownSeconds = 0.0f;
                    encounter.pendingCooldownSeconds = 0.0f;
                    pin_stand();
                };

                // --s42-stills: one rendered PNG at a named phase, HUD visible (the
                // hurt and death stills must show Wander's amber bar). The frame is
                // rebuilt from the current playback/combat state, so a still shows
                // exactly what the timeline has reached, and every rig's clock is
                // restored after it (with_clocks_restored).
                const auto still_path = [&](const char* name) {
                    return still_png_path(opts.s42StillsDir, name);
                };
                const auto capture_still = [&](const char* phase, const std::string& path) {
                    if (opts.s42StillsDir.empty()) {
                        return;
                    }
                    std::printf("s42-still phase=%s path=%s\n", phase, path.c_str());
                    with_clocks_restored(playbacks, [&]() {
                        const PlayerInput no_input;
                        update_combat_clips(playback, scene, combat, no_input, /*manualClip=*/false);
                        update_all_poses(renderer, scene, playbacks, 0.0f, club);
                        apply_player_transform(scene, player_rig_record, player,
                                               active_player_lift(scene, playback, player_rig_record));
                        renderer.set_hud(hud_state(/*visible=*/true));
                        renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                                              /*acquire_swapchain=*/true);
                        write_screenshot(renderer, shell.win, path);
                    });
                };

                // --- 1. Proxy: both shells on the pinned stand frame --------------
                const std::vector<std::vector<Mat4>> standPalettes = palettes_now();
                if (entityRig >= standPalettes.size()) {
                    throw std::runtime_error("s42-selftest: the entity rig has no stand pose");
                }
                std::vector<Vec3> shellCenters(2);
                int proxyCount = 0;
                for (int weakpoint = 0; weakpoint < 2; ++weakpoint) {
                    const char* material = weakpoint_material_name(weakpoint);
                    if (material == nullptr) {
                        throw std::runtime_error("s42-selftest: weakpoint " +
                                                 std::to_string(weakpoint) +
                                                 " has no shell material");
                    }
                    Vec3 center{};
                    float radius = 0.0f;
                    if (!posed_weakpoint_proxy(scene, entityRig, standPalettes[entityRig],
                                               material, center, radius)) {
                        throw std::runtime_error("s42-selftest: weakpoint " +
                                                 std::to_string(weakpoint) + " (" + material +
                                                 ") did not resolve on the stand pose");
                    }
                    if (!finite_position(center) || !std::isfinite(radius) || !(radius > 0.0f)) {
                        throw std::runtime_error("s42-selftest: weakpoint " +
                                                 std::to_string(weakpoint) +
                                                 " resolved a non-finite sphere");
                    }
                    shellCenters[weakpoint] = center;
                    ++proxyCount;
                    std::printf("s42-proxy weakpoint=%d radius=%.3f\n", weakpoint, radius);
                }

                // The head strike's front hint: the forehead faces the spawn (the
                // side S4.1 proved); the arm route starts on the shell's side.
                const Vec3 headHint = [&]() {
                    Vec3 hint = spawn;
                    const Vec3 entityPos = manifest.entities.front().placement.position;
                    const float fx = spawn.x - entityPos.x;
                    const float fz = spawn.z - entityPos.z;
                    const float length = std::sqrt(fx * fx + fz * fz);
                    Vec3 kubi{};
                    Vec3 kao{};
                    if (length > 1e-3f &&
                        posed_joint_world_position(scene, entityRig, standPalettes[entityRig],
                                                   "Bone_KUBI", kubi) &&
                        posed_joint_world_position(scene, entityRig, standPalettes[entityRig],
                                                   "Bone_KAO", kao)) {
                        hint = Vec3{(kubi.x + kao.x) * 0.5f + fx / length * 6.0f,
                                    (kubi.y + kao.y) * 0.5f,
                                    (kubi.z + kao.z) * 0.5f + fz / length * 6.0f};
                    }
                    return hint;
                }();

                // --- 2. Gate: a foot strike is a clang, a head strike is 34 ------
                attach_band_at(ClimbBandId::LeftFoot, 0.35f, spawn, "gate");
                if (combat.band != ClimbBandId::LeftFoot || combat.weakpoint != -1) {
                    throw std::runtime_error(
                        "s42-selftest: the foot attach did not hold the foot band");
                }
                const int healthBeforeFoot = static_cast<int>(std::lround(encounter.health));
                const StepEvents footStrike = charge_and_release("gate");
                if (footStrike.damageDealt != 0) {
                    throw std::runtime_error(
                        "s42-selftest: the foot strike dealt " +
                        std::to_string(footStrike.damageDealt) + " damage, not 0");
                }
                if (!footStrike.gripBroke || combat.mode != PlayerMode::Falling) {
                    throw std::runtime_error(
                        "s42-selftest: the foot swing did not shake the player off");
                }
                if (static_cast<int>(std::lround(encounter.health)) != healthBeforeFoot) {
                    throw std::runtime_error(
                        "s42-selftest: the foot strike moved Valus's health");
                }

                attach_band_at(ClimbBandId::Head, 0.5f, headHint, "gate");
                if (combat.band != ClimbBandId::Head || combat.weakpoint != 0) {
                    throw std::runtime_error(
                        "s42-selftest: the head attach did not hold weakpoint 0");
                }
                const int healthBeforeHead = static_cast<int>(std::lround(encounter.health));
                const StepEvents headStrike = charge_and_release("gate");
                if (!headStrike.gripBroke || combat.mode != PlayerMode::Falling) {
                    throw std::runtime_error(
                        "s42-selftest: the head swing did not shake the player off");
                }
                if (headStrike.damageDealt != static_cast<int>(std::lround(tuning.damageFull))) {
                    throw std::runtime_error(
                        "s42-selftest: the head strike dealt " +
                        std::to_string(headStrike.damageDealt) + " damage, not " +
                        std::to_string(static_cast<int>(std::lround(tuning.damageFull))));
                }
                if (static_cast<int>(std::lround(encounter.health)) !=
                    healthBeforeHead - static_cast<int>(std::lround(tuning.damageFull))) {
                    throw std::runtime_error(
                        "s42-selftest: the head strike did not subtract 34 from Valus");
                }
                std::printf("s42-gate foot=%d head=%d\n", footStrike.damageDealt,
                            headStrike.damageDealt);

                // --- 3. Arm route: chest top -> shoulder -> arm, weakpoint 1 -----
                wait_for_reaction();
                attach_band_at(ClimbBandId::Chest, 1.0f, shellCenters[1], "arm");
                if (combat.band != ClimbBandId::Chest || combat.bandT < 1.0f - kS42Eps) {
                    throw std::runtime_error(
                        "s42-selftest: the arm route did not start at the chest top");
                }
                {
                    PlayerInput holdGrab;
                    holdGrab.grab = true;
                    step_frame(holdGrab, /*driveAttack=*/false);
                }
                PlayerInput branchInput;
                branchInput.grab = true;
                branchInput.forward = true;
                branchInput.right = true;
                PlayerInput armClimb;
                armClimb.grab = true;
                armClimb.forward = true;
                bool sawShoulder = false;
                int routeFrames = 0;
                for (; routeFrames < kS42MaxRouteFrames; ++routeFrames) {
                    step_frame(sawShoulder ? armClimb : branchInput, /*driveAttack=*/false);
                    if (!mode_is_attached(combat.mode)) {
                        throw std::runtime_error(
                            "s42-selftest: the arm route left the body at frame " +
                            std::to_string(routeFrames) + " (mode " +
                            player_mode_name(combat.mode) + ")");
                    }
                    if (combat.band == ClimbBandId::RightShoulder) {
                        sawShoulder = true;
                    }
                    if (combat.band == ClimbBandId::RightArm) {
                        break;
                    }
                }
                if (routeFrames >= kS42MaxRouteFrames || !sawShoulder ||
                    combat.band != ClimbBandId::RightArm) {
                    throw std::runtime_error(
                        "s42-selftest: the chest -> shoulder -> arm route did not reach the arm in " +
                        std::to_string(kS42MaxRouteFrames) + " frames (band " +
                        climb_band_name(combat.band) + ")");
                }
                for (int frame = 0; frame < kS42MaxRouteFrames &&
                                    combat.bandT < kS42ArmStrikeT - kS42Eps;
                     ++frame) {
                    step_frame(armClimb, /*driveAttack=*/false);
                    if (combat.band != ClimbBandId::RightArm) {
                        throw std::runtime_error(
                            "s42-selftest: the arm climb left the right arm at frame " +
                            std::to_string(frame) + " (band " + climb_band_name(combat.band) + ")");
                    }
                }
                if (combat.band != ClimbBandId::RightArm ||
                    combat.bandT < kS42ArmStrikeT - kS42Eps) {
                    throw std::runtime_error(
                        "s42-selftest: the arm climb did not reach the shell's band position");
                }
                const int armWeakpoint = combat.weakpoint;
                const ClimbBandId armBand = combat.band;
                const int healthBeforeArm = static_cast<int>(std::lround(encounter.health));
                const StepEvents armStrike = charge_and_release("arm");
                if (armWeakpoint != 1) {
                    throw std::runtime_error(
                        "s42-selftest: the right-arm band reports weakpoint " +
                        std::to_string(armWeakpoint) + ", not 1");
                }
                if (!armStrike.gripBroke || combat.mode != PlayerMode::Falling) {
                    throw std::runtime_error(
                        "s42-selftest: the arm swing did not shake the player off");
                }
                if (armStrike.damageDealt != static_cast<int>(std::lround(tuning.damageFull))) {
                    throw std::runtime_error(
                        "s42-selftest: the arm strike dealt " +
                        std::to_string(armStrike.damageDealt) + " damage, not " +
                        std::to_string(static_cast<int>(std::lround(tuning.damageFull))));
                }
                if (static_cast<int>(std::lround(encounter.health)) !=
                    healthBeforeArm - static_cast<int>(std::lround(tuning.damageFull))) {
                    throw std::runtime_error(
                        "s42-selftest: the arm strike did not subtract 34 from Valus");
                }
                std::printf("s42-arm band=%s weakpoint=%d damage=%d\n",
                            climb_band_name(armBand), armWeakpoint, armStrike.damageDealt);
                // The arm weakpoint is live on the shell: capture before the
                // reaction wait, while the strike's fall and the arm reaction are
                // the frame the timeline has just asserted.
                capture_still("arm-weakpoint", still_path("arm-weakpoint"));
                wait_for_reaction();

                const auto clip_at = [&](int index, const char* phase) -> const AnimationCpu& {
                    if (index < 0 ||
                        static_cast<std::size_t>(index) >= entity_playback.clips.size()) {
                        throw std::runtime_error(std::string("s42-selftest: ") + phase +
                                                 ": the attack clip is missing");
                    }
                    return entity_playback.clips[static_cast<std::size_t>(index)];
                };
                const auto impact_pose_time = [&](float begin) {
                    return std::ceil(begin / stepSeconds) * stepSeconds;
                };
                const auto place_grounded = [&](float x, float z, const char* phase) {
                    const float ground = heightmap_sample(heightmap, Vec3{x, 0.0f, z});
                    if (!std::isfinite(ground)) {
                        throw std::runtime_error(std::string("s42-selftest: ") + phase +
                                                 ": no ground under the placed target");
                    }
                    player.position = Vec3{x, ground, z};
                    player.yawDeg = 0.0f;
                };

                // --- 4. Stomp: 35 damage, Hurt, then i-frames block a second hit -
                reset_player_combat(player, combat, spawn, spawnYaw, tuning);
                const float stompPoseSeconds =
                    impact_pose_time(kValusStompImpactBegin);
                std::vector<Mat4> stompPalette =
                    animation_pose(scene, entityRig, clip_at(entityClips.stompL, "stomp"),
                                   stompPoseSeconds);
                Vec3 stompFoot{};
                if (!posed_joint_world_position(scene, entityRig, stompPalette, "Bone_TUMASAKI_L",
                                               stompFoot)) {
                    throw std::runtime_error(
                        "s42-selftest: the stomp pose has no Bone_TUMASAKI_L");
                }
                float footDx = stompFoot.x - encounter.entityPosition.x;
                float footDz = stompFoot.z - encounter.entityPosition.z;
                const float footLength = std::sqrt(footDx * footDx + footDz * footDz);
                if (!(footLength > 1e-3f)) {
                    footDx = 1.0f;
                    footDz = 0.0f;
                } else {
                    footDx /= footLength;
                    footDz /= footLength;
                }
                place_grounded(stompFoot.x + footDx * kS42StompStandoff,
                               stompFoot.z + footDz * kS42StompStandoff, "stomp");
                const int healthBeforeStomp = static_cast<int>(std::lround(combat.health));
                start_valus_attack(encounter, entity_playback, entityClips, ValusAttack::StompL,
                                   audioOn ? &sfx : nullptr);
                flush_sfx();  // the telegraph sounds at its real start time
                if (encounter.attack != ValusAttack::StompL) {
                    throw std::runtime_error("s42-selftest: the stomp attack did not start");
                }
                bool stompLanded = false;
                bool stompTelegraphCaptured = false;
                for (int frame = 0; frame < kS42MaxAttackFrames; ++frame) {
                    step_frame(noInput, /*driveAttack=*/true);
                    if (!stompTelegraphCaptured &&
                        encounter.attack == ValusAttack::StompL &&
                        encounter.attackSeconds >=
                            kValusStompImpactBegin -
                                kS42TelegraphLeadFrames * stepSeconds) {
                        // The telegraph is the stomp clip itself; this frame is a
                        // few steps before the [0.55, 0.70] impact window.
                        capture_still("stomp-telegraph", still_path("stomp-telegraph"));
                        stompTelegraphCaptured = true;
                    }
                    if (static_cast<int>(std::lround(combat.health)) != healthBeforeStomp) {
                        stompLanded = true;
                        break;
                    }
                    if (encounter.attack == ValusAttack::None) {
                        throw std::runtime_error(
                            "s42-selftest: the stomp clip ended before its impact window");
                    }
                }
                if (!stompLanded) {
                    throw std::runtime_error(
                        "s42-selftest: the stomp never landed in " +
                        std::to_string(kS42MaxAttackFrames) + " frames");
                }
                if (!opts.s42StillsDir.empty() && !stompTelegraphCaptured) {
                    throw std::runtime_error(
                        "s42-selftest: the stomp impact arrived before its telegraph still");
                }
                const int stompDamage = healthBeforeStomp -
                                        static_cast<int>(std::lround(combat.health));
                if (stompDamage != static_cast<int>(std::lround(tuning.stompDamage))) {
                    throw std::runtime_error(
                        "s42-selftest: the stomp dealt " + std::to_string(stompDamage) +
                        " damage, not " + std::to_string(static_cast<int>(
                                                 std::lround(tuning.stompDamage))) +
                        " (was the target inside the direct range?)");
                }
                if (combat.mode != PlayerMode::Hurt) {
                    throw std::runtime_error(
                        "s42-selftest: the stomp did not leave the player Hurt (mode " +
                        std::string(player_mode_name(combat.mode)) + ")");
                }
                std::printf("s42-stomp damage=%d health=%d->%d mode=%s\n", stompDamage,
                            healthBeforeStomp,
                            static_cast<int>(std::lround(combat.health)),
                            player_mode_name(combat.mode));
                // Immediately after the impact: Wander is Hurt at 65 HP and the
                // stomp frame is still the entity's current pose.
                capture_still("stomp-impact", still_path("stomp-impact"));

                // A second immediate hit is the same attack's impact applied again
                // on this frame: the i-frame window, not the once-only mark, is what
                // must ignore it.
                if (!(combat.invulnSeconds > 0.0f)) {
                    throw std::runtime_error(
                        "s42-selftest: the stomp did not start the i-frame window");
                }
                {
                    const std::vector<std::vector<Mat4>> hitPalettes = palettes_now();
                    const PlayerFrame hitFrame = frame_of(hitPalettes);
                    const float healthBeforeBlocked = combat.health;
                    const PlayerMode modeBeforeBlocked = combat.mode;
                    apply_valus_impact(combat, player, hitFrame, tuning, ValusAttack::StompL);
                    if (combat.health != healthBeforeBlocked ||
                        combat.mode != modeBeforeBlocked) {
                        throw std::runtime_error(
                            "s42-selftest: the i-frames did not block a second immediate hit "
                            "(health " + std::to_string(healthBeforeBlocked) + " -> " +
                            std::to_string(combat.health) + ")");
                    }
                }
                clear_attack();

                // --- 5. Club: 45 damage ------------------------------------------
                for (int frame = 0;
                     frame < kS42MaxAttackFrames &&
                     (combat.mode != PlayerMode::Grounded || combat.invulnSeconds > 0.0f);
                     ++frame) {
                    step_frame(noInput, /*driveAttack=*/false);
                }
                if (combat.mode != PlayerMode::Grounded || combat.invulnSeconds > 0.0f) {
                    throw std::runtime_error(
                        "s42-selftest: the player did not recover before the club phase (mode " +
                        std::string(player_mode_name(combat.mode)) + ")");
                }
                const float clubPoseSeconds = impact_pose_time(kValusClubImpactBegin);
                std::vector<Mat4> clubPalette =
                    animation_pose(scene, entityRig, clip_at(entityClips.club, "club"),
                                   clubPoseSeconds);
                Vec3 clubTip{};
                if (!posed_joint_world_position(scene, entityRig, clubPalette, "OptBone_hit",
                                               clubTip)) {
                    throw std::runtime_error("s42-selftest: the club pose has no OptBone_hit");
                }

                // --- 4b. The attack range split --------------------------------
                // The gates above prove the damage numbers; these probes prove the
                // distances those numbers live at, through the production
                // apply_valus_impact. A club tip 4 m to the side of a grounded
                // player is beyond its 2.5 m hit radius (0 damage), and a stomp
                // 3 m from the foot is inside the 6 m shockwave (15). The probes
                // place the player and fix the post-stomp HP/mode/i-frames by
                // hand: scaffolding, not the production gate.
                const auto probe_baseline = [&]() {
                    combat.mode = PlayerMode::Grounded;
                    combat.modeSeconds = 0.0f;
                    combat.invulnSeconds = 0.0f;
                    combat.heavyHit = false;
                    combat.health = static_cast<float>(healthBeforeStomp) - tuning.stompDamage;
                };
                float clubLateralX = clubTip.x - encounter.entityPosition.x;
                float clubLateralZ = clubTip.z - encounter.entityPosition.z;
                const float clubLateral =
                    std::sqrt(clubLateralX * clubLateralX + clubLateralZ * clubLateralZ);
                if (!(clubLateral > 1e-3f)) {
                    clubLateralX = 1.0f;
                    clubLateralZ = 0.0f;
                } else {
                    clubLateralX /= clubLateral;
                    clubLateralZ /= clubLateral;
                }
                int clubMissDamage = 0;
                {
                    std::vector<std::vector<Mat4>> missPalettes = palettes_now();
                    if (entityRig < missPalettes.size()) {
                        missPalettes[entityRig] = clubPalette;
                    }
                    const PlayerFrame missFrame = frame_of(missPalettes);
                    place_grounded(clubTip.x + clubLateralX * kS42ClubMissStandoff,
                                   clubTip.z + clubLateralZ * kS42ClubMissStandoff,
                                   "club-miss");
                    probe_baseline();
                    const float healthBeforeMiss = combat.health;
                    apply_valus_impact(combat, player, missFrame, tuning, ValusAttack::Club);
                    clubMissDamage =
                        static_cast<int>(std::lround(healthBeforeMiss - combat.health));
                    if (clubMissDamage != 0 || combat.mode != PlayerMode::Grounded) {
                        throw std::runtime_error(
                            "s42-selftest: the club hit a target " +
                            std::to_string(kS42ClubMissStandoff) + " m from its tip (damage " +
                            std::to_string(clubMissDamage) + ", mode " +
                            std::string(player_mode_name(combat.mode)) + ")");
                    }
                }
                int stompWaveDamage = 0;
                {
                    std::vector<std::vector<Mat4>> wavePalettes = palettes_now();
                    if (entityRig < wavePalettes.size()) {
                        wavePalettes[entityRig] = stompPalette;
                    }
                    const PlayerFrame waveFrame = frame_of(wavePalettes);
                    place_grounded(stompFoot.x + footDx * kS42StompWaveStandoff,
                                   stompFoot.z + footDz * kS42StompWaveStandoff,
                                   "stomp-wave");
                    probe_baseline();
                    const float healthBeforeWave = combat.health;
                    apply_valus_impact(combat, player, waveFrame, tuning, ValusAttack::StompL);
                    stompWaveDamage =
                        static_cast<int>(std::lround(healthBeforeWave - combat.health));
                    const int expectedWave =
                        static_cast<int>(std::lround(tuning.stompWaveDamage));
                    if (stompWaveDamage != expectedWave) {
                        throw std::runtime_error(
                            "s42-selftest: the stomp dealt " + std::to_string(stompWaveDamage) +
                            " damage " + std::to_string(kS42StompWaveStandoff) +
                            " m from the foot, not the " + std::to_string(expectedWave) +
                            " shockwave");
                    }
                    if (combat.mode != PlayerMode::Hurt) {
                        throw std::runtime_error(
                            "s42-selftest: the stomp shockwave did not leave the player Hurt (mode " +
                            std::string(player_mode_name(combat.mode)) + ")");
                    }
                }
                std::printf("s42-miss club=%d stomp=%d\n", clubMissDamage, stompWaveDamage);
                probe_baseline();  // the club phase resumes from the 65 HP post-stomp state

                place_grounded(clubTip.x, clubTip.z, "club");
                const int healthBeforeClub = static_cast<int>(std::lround(combat.health));
                start_valus_attack(encounter, entity_playback, entityClips, ValusAttack::Club,
                                   audioOn ? &sfx : nullptr);
                flush_sfx();  // the telegraph sounds at its real start time
                if (encounter.attack != ValusAttack::Club) {
                    throw std::runtime_error("s42-selftest: the club attack did not start");
                }
                bool clubLanded = false;
                bool clubSwingCaptured = false;
                for (int frame = 0; frame < kS42MaxAttackFrames; ++frame) {
                    step_frame(noInput, /*driveAttack=*/true);
                    if (!clubSwingCaptured && encounter.attack == ValusAttack::Club &&
                        encounter.attackSeconds >=
                            kValusClubImpactBegin - kS42TelegraphLeadFrames * stepSeconds) {
                        // The club's own clip is the telegraph; this frame is a few
                        // steps before the [0.45, 0.65] impact window.
                        capture_still("club-swing", still_path("club-swing"));
                        clubSwingCaptured = true;
                    }
                    if (static_cast<int>(std::lround(combat.health)) != healthBeforeClub) {
                        clubLanded = true;
                        break;
                    }
                    if (encounter.attack == ValusAttack::None) {
                        throw std::runtime_error(
                            "s42-selftest: the club clip ended before its impact window");
                    }
                }
                if (!clubLanded) {
                    throw std::runtime_error(
                        "s42-selftest: the club never landed in " +
                        std::to_string(kS42MaxAttackFrames) + " frames");
                }
                if (!opts.s42StillsDir.empty() && !clubSwingCaptured) {
                    throw std::runtime_error(
                        "s42-selftest: the club impact arrived before its swing still");
                }
                const int clubDamage =
                    healthBeforeClub - static_cast<int>(std::lround(combat.health));
                if (clubDamage != static_cast<int>(std::lround(tuning.clubDamage))) {
                    throw std::runtime_error(
                        "s42-selftest: the club dealt " + std::to_string(clubDamage) +
                        " damage, not " +
                        std::to_string(static_cast<int>(std::lround(tuning.clubDamage))));
                }
                if (combat.mode != PlayerMode::Hurt) {
                    throw std::runtime_error(
                        "s42-selftest: the club did not leave the player Hurt (mode " +
                        std::string(player_mode_name(combat.mode)) + ")");
                }
                std::printf("s42-club damage=%d health=%d->%d\n", clubDamage,
                            healthBeforeClub,
                            static_cast<int>(std::lround(combat.health)));
                // Immediately after the club impact: Wander is Hurt and the amber
                // player bar reads 20 HP.
                capture_still("hurt", still_path("hurt"));

                // --- 6. Death: one more club at 45 HP ---------------------------
                clear_attack();
                for (int frame = 0;
                     frame < kS42MaxAttackFrames &&
                     (combat.mode != PlayerMode::Grounded || combat.invulnSeconds > 0.0f);
                     ++frame) {
                    step_frame(noInput, /*driveAttack=*/false);
                }
                if (combat.mode != PlayerMode::Grounded || combat.invulnSeconds > 0.0f) {
                    throw std::runtime_error(
                        "s42-selftest: the player did not recover before the death phase (mode " +
                        std::string(player_mode_name(combat.mode)) + ")");
                }
                combat.health = tuning.clubDamage;  // the pinned lethal setup
                place_grounded(clubTip.x, clubTip.z, "death");
                start_valus_attack(encounter, entity_playback, entityClips, ValusAttack::Club,
                                   audioOn ? &sfx : nullptr);
                flush_sfx();  // the telegraph sounds at its real start time
                bool deathLanded = false;
                for (int frame = 0; frame < kS42MaxAttackFrames; ++frame) {
                    step_frame(noInput, /*driveAttack=*/true);
                    if (combat.mode == PlayerMode::Dead) {
                        deathLanded = true;
                        break;
                    }
                    if (encounter.attack == ValusAttack::None) {
                        throw std::runtime_error(
                            "s42-selftest: the lethal club ended before its impact window");
                    }
                }
                if (!deathLanded || combat.health > 0.0f) {
                    throw std::runtime_error(
                        "s42-selftest: the lethal club did not kill the player (health " +
                        std::to_string(combat.health) + ", mode " +
                        player_mode_name(combat.mode) + ")");
                }
                update_combat_clips(playback, scene, combat, noInput, /*manualClip=*/false);
                if (playback.index != playerClips.death || !playback.pinned) {
                    throw std::runtime_error(
                        "s42-selftest: the player death clip is not pinned (index " +
                        std::to_string(playback.index) + ", expected " +
                        std::to_string(playerClips.death) + ")");
                }
                clear_attack();
                // The death clip plays once and holds its last frame: step it to its
                // end and prove the pin, so a later loop cannot restart it.
                const float deathDuration =
                    playback.clips[static_cast<std::size_t>(playerClips.death)].duration;
                for (int frame = 0; frame < kS42MaxDeathFrames; ++frame) {
                    step_frame(noInput, /*driveAttack=*/false);
                    if (playback_time(playback, deathDuration) >= deathDuration - kS42Eps) {
                        break;
                    }
                }
                if (combat.mode != PlayerMode::Dead || !playback.pinned ||
                    std::fabs(playback_time(playback, deathDuration) - deathDuration) >
                        kS42Eps) {
                    throw std::runtime_error(
                        "s42-selftest: the death clip did not hold its pinned last frame");
                }
                std::printf("s42-death clip=%s mode=%s\n",
                            playback.clips[static_cast<std::size_t>(playerClips.death)].name.c_str(),
                            player_mode_name(combat.mode));
                // The death clip has played once and is asserted pinned on its last
                // frame; the still is that frame, with the HUD's depleted amber bar.
                capture_still("death", still_path("death"));

                // --- 7. Full soft reset (the R key's own sequence) --------------
                reset_player_combat(player, combat, spawn, spawnYaw, tuning);
                reset_encounter(encounter);
                audio.reset();  // every voice, the music included (R stops everything)
                entity_playback.index = entityClips.stand;
                entity_playback.time = 0.0f;
                entity_playback.pinned = false;
                entity_playback.playing = true;
                climbCamera.active = false;
                targetCamera = CameraTarget{};
                follow = CameraFollow{};
                update_entity_transform(scene, encounter, entityRig);
                camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f, 0.0f,
                              /*moving=*/false, /*dtSeconds=*/10.0f);
                playback.pinned = false;
                playback.playing = true;
                playback.time = 0.0f;
                const int resetHealth = static_cast<int>(std::lround(combat.health));
                const int resetStamina = static_cast<int>(std::lround(combat.stamina));
                const int resetValus = static_cast<int>(std::lround(encounter.health));
                const char* resetMode = player_mode_name(combat.mode);
                if (resetHealth != 100 || resetStamina != 100 ||
                    combat.mode != PlayerMode::Grounded || resetValus != 100 ||
                    encounter.defeated) {
                    throw std::runtime_error(
                        "s42-selftest: the reset did not restore the opening state");
                }
                std::printf("s42-reset health=%d stamina=%d mode=%s valus=%d\n", resetHealth,
                            resetStamina, resetMode, resetValus);
                pin_stand();  // the climb needs one frozen body, like the S4.1 walk

                // --- 8. Stamina: the real foot -> head climb, no teleport --------
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
                        resolve_player_collision(player, entity_capsules(scene, palettes),
                                                 heightmap);
                        camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f,
                                      0.0f, /*moving=*/true, stepSeconds);
                        tick_audio();  // the mix clock keeps the capture in step
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
                        throw std::runtime_error(
                            "s42-selftest: the approach walked " + std::to_string(frames) +
                            " frames without reaching a collision stop");
                    }
                    if (!finite_position(player.position)) {
                        throw std::runtime_error(
                            "s42-selftest: the approach left a non-finite player position");
                    }
                    const float ground = heightmap_sample(heightmap, player.position);
                    if (!std::isfinite(ground) ||
                        std::fabs(player.position.y - ground) > kS4GroundTolerance) {
                        throw std::runtime_error(
                            "s42-selftest: the approach left the ground (y " +
                            std::to_string(player.position.y) + ", ground " +
                            std::to_string(ground) + ")");
                    }
                    return frames;
                };

                std::vector<std::vector<Mat4>> palettes = palettes_now();
                const std::vector<Capsule> capsules =
                    entity_collision_capsules(scene, entityRig, palettes[entityRig]);
                if (capsules.empty()) {
                    throw std::runtime_error(
                        "s42-selftest: the entity rig has no collision capsules");
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
                        "s42-selftest: no reachable entity capsule to walk into");
                }
                const float aimYaw = std::atan2(aimDx, aimDz) / kDegToRad;
                walk_toward(aimYaw, kS4MaxWalkFrames);

                palettes = palettes_now();
                PlayerFrame footFrame = frame_of(palettes);
                const ClimbBandId approachBand =
                    nearest_climb_band(footFrame.jointWorld, player.position, 1000.0f);
                Vec3 footPoint{};
                Vec3 footNormal{};
                if (approachBand == ClimbBandId::Count ||
                    !climb_surface_point(climb_band(approachBand), footFrame.jointWorld, 0.5f,
                                         player.position, footPoint, footNormal)) {
                    throw std::runtime_error(
                        "s42-selftest: the entity has no resolvable climb band");
                }
                const float footDx2 = footPoint.x - player.position.x;
                const float footDz2 = footPoint.z - player.position.z;
                if (!(std::sqrt(footDx2 * footDx2 + footDz2 * footDz2) > 1e-3f)) {
                    throw std::runtime_error(
                        "s42-selftest: the foot band is under the player's first stop");
                }
                const float footYaw = std::atan2(footDx2, footDz2) / kDegToRad;
                walk_toward(footYaw, kS41ApproachFrames);

                palettes = palettes_now();
                PlayerFrame reachFrame = frame_of(palettes);
                if (nearest_climb_band(reachFrame.jointWorld, player.position, tuning.grabRange) ==
                    ClimbBandId::Count) {
                    throw std::runtime_error(
                        "s42-selftest: the approach stopped out of grab range of every climb band");
                }
                PlayerInput grabInput;
                grabInput.grab = true;
                const StepEvents grabEvents = step_frame(grabInput, /*driveAttack=*/false);
                if (!grabEvents.grabbed || combat.mode != PlayerMode::Grabbing) {
                    throw std::runtime_error(
                        "s42-selftest: the foot grab did not attach (mode " +
                        std::string(player_mode_name(combat.mode)) + ")");
                }
                if (combat.band != ClimbBandId::LeftFoot) {
                    throw std::runtime_error(
                        "s42-selftest: the climb did not start at the foot (band " +
                        std::string(climb_band_name(combat.band)) + ")");
                }
                const float climbFromY = player.position.y;
                float previousY = climbFromY;
                PlayerInput climbInput;
                climbInput.grab = true;
                climbInput.forward = true;
                int climbFrames = 0;
                bool reachedHead = false;
                for (; climbFrames < kS42MaxClimbFrames; ++climbFrames) {
                    step_frame(climbInput, /*driveAttack=*/false);
                    if (combat.mode != PlayerMode::Climbing && combat.mode != PlayerMode::Grabbing) {
                        throw std::runtime_error(
                            "s42-selftest: the full climb left the climb modes at frame " +
                            std::to_string(climbFrames) + " (mode " +
                            player_mode_name(combat.mode) + ", band " +
                            climb_band_name(combat.band) + ", stamina " +
                            std::to_string(combat.stamina) + ")");
                    }
                    if (!finite_position(player.position)) {
                        throw std::runtime_error(
                            "s42-selftest: the full climb wrote a non-finite position at frame " +
                            std::to_string(climbFrames));
                    }
                    if (player.position.y + kS42Eps < previousY) {
                        throw std::runtime_error(
                            "s42-selftest: the full climb went backwards at frame " +
                            std::to_string(climbFrames) + " (" + std::to_string(previousY) +
                            " -> " + std::to_string(player.position.y) + ")");
                    }
                    previousY = player.position.y;
                    if (combat.band == ClimbBandId::Head &&
                        combat.bandT >= 1.0f - kS42Eps) {
                        reachedHead = true;
                        break;
                    }
                }
                if (!reachedHead) {
                    throw std::runtime_error(
                        "s42-selftest: the full climb did not reach the top of the head in " +
                        std::to_string(kS42MaxClimbFrames) + " frames (band " +
                        climb_band_name(combat.band) + ", stamina " +
                        std::to_string(combat.stamina) + ")");
                }
                const float climbToY = player.position.y;
                const float climbRise = climbToY - climbFromY;
                if (!std::isfinite(climbRise) || climbRise < kS42MinClimbRise) {
                    throw std::runtime_error(
                        "s42-selftest: the full climb rose only " + std::to_string(climbRise) +
                        " m (need " + std::to_string(kS42MinClimbRise) + " m)");
                }
                const int staminaRemaining = static_cast<int>(std::lround(combat.stamina));
                if (staminaRemaining <= 0) {
                    throw std::runtime_error(
                        "s42-selftest: the full climb emptied the stamina pool");
                }
                std::printf("s42-stamina from_y=%.3f to_y=%.3f remaining=%d\n", climbFromY,
                            climbToY, staminaRemaining);

                // --- 9. Status ---------------------------------------------------
                if (encounter.defeated ||
                    static_cast<int>(std::lround(encounter.health)) != 100) {
                    throw std::runtime_error(
                        "s42-selftest: the post-climb encounter is not the restored one");
                }
                renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                                      /*acquire_swapchain=*/true);
                print_status_line(opts, shell.win);
                std::printf(
                    "s42-status proxy=%d gate_foot=%d gate_head=%d arm_band=%s arm_weakpoint=%d "
                    "arm_damage=%d stomp_damage=%d stomp_health=%d club_damage=%d club_health=%d "
                    "death_clip=%s reset_health=%d reset_stamina=%d reset_mode=%s reset_valus=%d "
                    "stamina_remaining=%d defeated=%d\n",
                    proxyCount, footStrike.damageDealt, headStrike.damageDealt,
                    climb_band_name(armBand), armWeakpoint,
                    armStrike.damageDealt, stompDamage,
                    healthBeforeStomp - stompDamage, clubDamage, healthBeforeClub - clubDamage,
                    playback.clips[static_cast<std::size_t>(playerClips.death)].name.c_str(),
                    resetHealth, resetStamina, resetMode, resetValus, staminaRemaining,
                    encounter.defeated ? 1 : 0);

                // --- 10. S5 defeat-stop phase (R18) -----------------------------
                // The shared S4.2 timeline never defeats Valus (its weakpoints are
                // struck, the health is restored, and the acceptance's real defeat
                // lives in S4.1 with audio off). This synthetic phase proves the
                // stop *wiring* only: it starts the score a second time and runs one
                // shared-timeline step with `encounter.defeated` set, so the phase
                // executes the same `if (encounter.defeated) audio.stop_music()`
                // the interactive loop and step_frame use. No weakpoint is struck,
                // no damage is dealt, and the entity's death clip is not involved;
                // identity and the real defeat remain the S4.1 acceptance + the
                // user's listening walk.
                if (opts.s5Selftest) {
                    audio.start_music();  // the second music_start, after the s42 status
                    encounter.defeated = true;
                    step_frame(noInput, /*driveAttack=*/false);
                    if (audio.musicStarted) {
                        throw std::runtime_error(
                            "s5-selftest: the defeat step did not stop the music voice");
                    }
                    audio.defeatStop = true;
                }

                // --- 11. S5 finish: write the mixdown and its proof --------------
                // The same timeline just ran with the audio observer on. A cue that
                // pointed at a missing clip, a write failure, or a capture with no
                // audible window is a failed S5 run (exit 1); the acceptance reads
                // the ordered s5-event lines and this line.
                if (opts.s5Selftest) {
                    if (audio.failed) {
                        std::fprintf(stderr,
                                     "sotc-viewer: s5-selftest: a cue clip was missing at play "
                                     "time\n");
                        return 1;
                    }
                    if (!audio.engine.end_capture()) {
                        std::fprintf(stderr, "sotc-viewer: s5-selftest: %s\n",
                                     audio.engine.last_error().c_str());
                        return 1;
                    }
                    AudioClip mixed;
                    std::string wav_error;
                    if (!read_wav(opts.audioMixdownPath, mixed, &wav_error)) {
                        std::fprintf(stderr, "sotc-viewer: s5-selftest: %s\n", wav_error.c_str());
                        return 1;
                    }
                    double sum = 0.0;
                    float mix_peak = 0.0f;
                    for (const float sample : mixed.samples) {
                        sum += static_cast<double>(sample) * static_cast<double>(sample);
                        mix_peak = std::max(mix_peak, std::fabs(sample));
                    }
                    const float mix_rms =
                        mixed.samples.empty()
                            ? 0.0f
                            : static_cast<float>(std::sqrt(
                                  sum / static_cast<double>(mixed.samples.size())));
                    // The loudest one-second window: a fully silent capture (or one
                    // whose only sound is a click) has no audible window.
                    constexpr int kWindowFrames = 48000;
                    constexpr double kNonSilentWindowRms = 0.01;  // -40 dBFS
                    constexpr float kNonSilentPeak = 0.05f;       // -26 dBFS
                    float best_window_rms = 0.0f;
                    const std::size_t frames = mixed.frames();
                    for (std::size_t start = 0; start < frames;
                         start += static_cast<std::size_t>(kWindowFrames)) {
                        const std::size_t count = std::min<std::size_t>(
                            static_cast<std::size_t>(kWindowFrames), frames - start);
                        double window_sum = 0.0;
                        for (std::size_t frame = start; frame < start + count; ++frame) {
                            for (int channel = 0; channel < 2; ++channel) {
                                const double value = mixed.samples[
                                    frame * static_cast<std::size_t>(mixed.channels) +
                                    static_cast<std::size_t>(channel)];
                                window_sum += value * value;
                            }
                        }
                        best_window_rms = std::max(
                            best_window_rms,
                            static_cast<float>(std::sqrt(
                                window_sum / static_cast<double>(count * 2))));
                    }
                    std::printf("s5-mix frames=%zu rms=%.4f peak=%.4f window_rms=%.4f\n",
                                frames, mix_rms, mix_peak, best_window_rms);
                    if (!(mix_peak >= kNonSilentPeak) ||
                        !(best_window_rms >= kNonSilentWindowRms)) {
                        std::fprintf(stderr,
                                     "sotc-viewer: s5-selftest: the mixdown is silent (peak "
                                     "%.4f, window rms %.4f): %s\n",
                                     mix_peak, best_window_rms, opts.audioMixdownPath.c_str());
                        return 1;
                    }
                    std::printf("s5-status mixdown=%s events=%d music=%d defeat_stop=%d\n",
                                opts.audioMixdownPath.c_str(), audio.eventCount,
                                audio.musicEverStarted ? 1 : 0, audio.defeatStop ? 1 : 0);
                }
                return 0;
            }

            // The one soft reset (design §7.5 and S6): the player's and Valus's
            // opening state, the death machine back to Idle, the corpse hidden and
            // the live body shown, the cameras back to the follow pose. The
            // interactive `R` handler and the S6 selftest **both call this**, so
            // the acceptance proves the reset the player actually presses
            // (anti-goal A2) instead of a copy of it. `cameraBaseSet` is the
            // interactive path's cinematic-camera latch (the selftest has no such
            // latch and passes nullptr).
            const auto soft_reset_encounter = [&](DeathState& deathState, bool& cueFired,
                                                  bool* cameraBaseSet) {
                reset_player_combat(player, combat, spawn_position(manifest, scene),
                                    manifest.hasSpawn ? manifest.spawnYawDeg : 0.0f, tuning);
                reset_encounter(encounter);
                audio.reset();  // R stops every voice, music too
                deathState = reset_death();
                cueFired = false;
                if (cameraBaseSet != nullptr) {
                    *cameraBaseSet = false;
                }
                apply_death_visibility(scene, corpse, club, entityRig, /*corpseVisible=*/false);
                if (entityRig < playbacks.size() && entityClips.stand >= 0) {
                    Playback& entityPlayback = playbacks[entityRig];
                    entityPlayback.index = entityClips.stand;
                    entityPlayback.time = 0.0f;
                    entityPlayback.pinned = false;
                    entityPlayback.playing = true;
                }
                climbCamera.active = false;
                targetCamera = CameraTarget{};
                follow = CameraFollow{};
                // R8: snap the body back to the manifest's opening yaw (the state
                // was restored; the model is composed from it).
                update_entity_transform(scene, encounter, entityRig);
                camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f, 0.0f,
                              /*moving=*/false, /*dtSeconds=*/10.0f);
                if (!manual_clip) {
                    playback.pinned = false;
                    playback.playing = true;
                }
                playback.time = 0.0f;
            };

            // --- S7: R, the wake, and the return's presentation ------------------
            // `R` anywhere (design D11): the return machine goes back to Idle;
            // in the arena it is the S6 soft reset above, and from the shrine
            // (or anywhere but the arena) it reloads the arena — the new
            // iteration is the opening state. Returns true when the caller must
            // end the iteration with a request for the arena.
            const auto press_reset = [&](DeathState& deathState, bool& cueFired,
                                         bool* cameraBaseSet) {
                carry.ret = reset_return();
                if (shrine) {
                    return true;
                }
                soft_reset_encounter(deathState, cueFired, cameraBaseSet);
                return false;
            };
            // The wake (D9, ruling R5): FadeIn shows its first frame, Wake plays
            // cut04 then cut05 once, pinned to the machine's clock, in place at
            // the spawn. The clips carry no root series (the pelvis sits at the
            // model origin), so the body is lifted by the pose's own pelvis
            // height: the lowest of the recon's body joints is planted on the
            // floor every frame — the measure `wake.pelvis_m` was taken with
            // (0.10 m lying -> 0.72 m standing; --s7-selftest checks it).
            const auto waking = [&]() {
                return return_wired && shrine &&
                       (carry.ret.phase == ReturnPhase::FadeIn ||
                        carry.ret.phase == ReturnPhase::Wake);
            };
            const auto wake_clock = [&]() {
                return carry.ret.phase == ReturnPhase::Wake ? carry.ret.t : 0.0f;
            };
            const auto pose_wake = [&]() {
                const float t = wake_clock();
                const bool first = t < wakeFirstSeconds;
                playback.index = first ? wakeFirst : wakeSecond;
                playback.time = first ? t : t - wakeFirstSeconds;
                playback.pinned = true;
                playback.playing = false;
            };
            // Leaving the wake: the clip clock runs again and the mode's clip
            // (the stand loop) takes over on the next update_combat_clips.
            const auto leave_wake = [&]() {
                if (wakeFirst >= 0 && (playback.index == wakeFirst || playback.index == wakeSecond)) {
                    playback.pinned = false;
                    playback.playing = true;
                }
            };
            const auto wake_plant_lift = [&]() {
                const AnimationCpu& clip = playback.clips[static_cast<std::size_t>(playback.index)];
                const std::vector<Mat4> pose = animation_pose(
                    scene, playerRig, clip, playback_time(playback, clip.duration));
                // World joints through the rig's current model; its rotation is
                // about +Y only, so subtracting its translation's y leaves the
                // joint's height above the model origin.
                const float originY = player_rig_record.meshBegin < scene.meshes.size()
                                          ? scene.meshes[player_rig_record.meshBegin].model.m[13]
                                          : 0.0f;
                float lowest = std::numeric_limits<float>::infinity();
                for (const char* name : kWakeBodyJoints) {
                    Vec3 world{};
                    if (posed_joint_world_position(scene, playerRig, pose, name, world)) {
                        lowest = std::min(lowest, world.y - originY);
                    }
                }
                return std::isfinite(lowest) ? -lowest : kWakePelvisLying;
            };
            const auto player_lift = [&]() {
                return waking() ? wake_plant_lift()
                                : active_player_lift(scene, playback, player_rig_record);
            };

            if (opts.s7Selftest) {
                // The S7 acceptance timeline (design §8.6): fixed 1/60 s steps,
                // no window input, one stage per iteration (s7Step: 0 the arena
                // and the return, 1 the shrine and its probes, 2 the arena after
                // the shrine's R, 3 the shrine reloaded).
                const float stepSeconds = kFixedFrameSeconds;
                ReturnState& ret = carry.ret;
                const auto fail = [](const std::string& message) {
                    std::fprintf(stderr, "sotc-viewer: --s7-selftest: %s\n", message.c_str());
                    return 1;
                };
                const auto print_phase = [&]() {
                    std::printf("s7-phase phase=%s t=%.3f fade=%.3f lock=%d\n",
                                return_phase_name(ret.phase), ret.t, ret.fade,
                                ret.inputLocked ? 1 : 0);
                };
                const auto render_stage_frame = [&]() {
                    update_all_poses(renderer, scene, playbacks, 0.0f, club);
                    apply_player_transform(scene, player_rig_record, player, player_lift());
                    renderer.set_hud(hud_state(/*visible=*/true));
                    renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                                          /*acquire_swapchain=*/true);
                };
                const auto capture = [&](const char* name) {
                    if (opts.s7StillsPath.empty()) {
                        return;
                    }
                    with_clocks_restored(playbacks, [&]() {
                        render_stage_frame();
                        write_screenshot(renderer, shell.win, still_png_path(opts.s7StillsPath, name));
                    });
                };
                const auto end_stage = [&](StageId next) {
                    renderer.release_scene();
                    stage = next;
                };
                DeathState death = reset_death();
                bool deathCueFired = false;
                // The arena's opening state (after either R), checked on the
                // draw gate as well as the flags, like s6-reset.
                const auto print_reset = [&]() {
                    const bool entityShown =
                        entityRig < scene.rigs.size() &&
                        mesh_range_visible(scene, scene.rigs[entityRig].meshBegin,
                                           scene.rigs[entityRig].meshEnd);
                    const bool corpseHidden =
                        !corpse.loaded || !mesh_range_visible(scene, corpse.meshBegin, corpse.meshEnd);
                    const int valus = static_cast<int>(std::lround(encounter.health));
                    const int hero = static_cast<int>(std::lround(combat.health));
                    if (shrine || ret.phase != ReturnPhase::Idle || death.phase != DeathPhase::Idle ||
                        encounter.defeated || !entityShown || !corpseHidden || valus != 100 ||
                        hero != 100) {
                        return false;
                    }
                    std::printf("s7-reset stage=arena phase=%s death=%s valus=%d player=%d\n",
                                return_phase_name(ret.phase), death_phase_name(death.phase), valus,
                                hero);
                    return true;
                };

                if (!shrine && carry.s7Step == 0) {
                    // --- 1. Valus dead, the return, R mid-fade, the return again --
                    if (!corpse.loaded || entityRig >= playbacks.size() || entityClips.death < 0) {
                        return fail("the arena has no corpse or death clip to seed");
                    }
                    Playback& entity_playback = playbacks[entityRig];
                    // The seed: the production lethal event, then the death
                    // machine straight to Done through the interactive loop's own
                    // frame helper (S6 proves the sequence itself).
                    const auto seed_dead = [&]() {
                        StepEvents lethal;
                        lethal.damageDealt = 100;
                        apply_combat_events(encounter, entity_playback, entityClips, lethal,
                                            /*weakpoint=*/-1, nullptr);
                        advance_death_frame(death, stepSeconds, entity_playback, entityClips.death,
                                            corpse, club, scene, entityRig);
                        advance_death_frame(death, kDeathEnd, entity_playback, entityClips.death,
                                            corpse, club, scene, entityRig);
                        return encounter.defeated && death.phase == DeathPhase::Done &&
                               death_corpse_visible(death);
                    };
                    print_phase();  // Idle
                    if (!seed_dead()) {
                        return fail("the seed did not reach the corpse at Done");
                    }
                    bool resetDone = false;
                    bool loadWanted = false;
                    for (int frame = 0; frame < kS7MaxArenaFrames && !loadWanted; ++frame) {
                        const ReturnState before = ret;
                        ret = step_return(ret, stepSeconds, returnTimings,
                                          death.phase == DeathPhase::Done, /*loadDone=*/false);
                        if (ret.phase != before.phase) {
                            print_phase();
                        }
                        loadWanted = return_wants_load(before, ret);
                        if (!resetDone && ret.phase == ReturnPhase::FadeOut &&
                            ret.fade + kS7Eps >= kS7FadeStillFade) {
                            // Mid-fade: the still, then R (the interactive key's
                            // own path), then the seed again.
                            capture("fade");
                            resetDone = true;
                            if (press_reset(death, deathCueFired, nullptr)) {
                                return fail("R in the arena asked for a stage switch");
                            }
                            if (!print_reset()) {
                                return fail("R mid-fade did not restore the arena's opening state");
                            }
                            print_phase();  // Idle again
                            if (!seed_dead()) {
                                return fail("the re-seed did not reach the corpse at Done");
                            }
                        }
                    }
                    if (!resetDone || !loadWanted || ret.phase != ReturnPhase::Load ||
                        ret.fade < 1.0f) {
                        return fail(std::string("the return did not reach Load (phase ") +
                                    return_phase_name(ret.phase) + ")");
                    }
                    carry.s7Step = 1;
                    end_stage(StageId::Shrine);
                    continue;
                }

                if (shrine && carry.s7Step == 1) {
                    // --- 2. The shrine: FadeIn, the wake, Free -------------------
                    carry.shrineMeshes = scene.meshes.size();
                    bool wakeStill = false;
                    float plantLying = -1.0f;
                    float plantKneeling = -1.0f;
                    float plantStanding = -1.0f;
                    for (int frame = 0; frame < kS7MaxShrineFrames; ++frame) {
                        const ReturnState before = ret;
                        ret = step_return(ret, stepSeconds, returnTimings, /*deathDone=*/false,
                                          /*loadDone=*/true);
                        if (ret.phase != before.phase) {
                            print_phase();
                        }
                        if (ret.phase == ReturnPhase::Free) {
                            break;
                        }
                        pose_wake();
                        camera = camera_wake_pose(stage_spawn, stage_spawn_yaw, wake_clock(),
                                                  returnTimings.wake);
                        // The plant against the recon's pelvis heights: lying at
                        // the wake's start, kneeling at the cut boundary.
                        if (ret.phase == ReturnPhase::FadeIn) {
                            plantLying = player_lift();
                        } else if (plantKneeling < 0.0f && ret.t >= wakeFirstSeconds) {
                            plantKneeling = player_lift();
                        }
                        plantStanding = player_lift();  // the last Wake frame's
                        if (!wakeStill && ret.phase == ReturnPhase::Wake &&
                            ret.t + kS7Eps >= kS7WakeStillFraction * returnTimings.wake) {
                            capture("wake");
                            wakeStill = true;
                        }
                    }
                    if (ret.phase != ReturnPhase::Free || ret.inputLocked) {
                        return fail(std::string("the shrine did not reach Free (phase ") +
                                    return_phase_name(ret.phase) + ")");
                    }
                    const auto near_pelvis = [](float got, float want) {
                        return std::fabs(got - want) <= 0.05f;
                    };
                    if (!near_pelvis(plantLying, kWakePelvisLying) ||
                        !near_pelvis(plantKneeling, kWakePelvisKneeling) ||
                        !near_pelvis(plantStanding, kWakePelvisStanding)) {
                        char message[160];
                        std::snprintf(message, sizeof(message),
                                      "the wake's foot plant lifts %.3f/%.3f/%.3f m, the recon "
                                      "measured %.2f/%.2f/%.2f",
                                      plantLying, plantKneeling, plantStanding, kWakePelvisLying,
                                      kWakePelvisKneeling, kWakePelvisStanding);
                        return fail(message);
                    }
                    leave_wake();
                    update_combat_clips(playback, scene, combat, PlayerInput{}, manual_clip);

                    // --- 3. The ground: the floor under the feet, not the roof ---
                    // Each probe is the floor a body walking there from the
                    // spawn stands on: the feet follow heightmap_floor in
                    // 0.25 m steps along the straight line (the hall's centre
                    // line), as player_update does every frame — against the
                    // column's maximum (the roof, the garden above).
                    const auto print_ground = [&](const char* name, float x, float z) {
                        const float dx = x - stage_spawn.x;
                        const float dz = z - stage_spawn.z;
                        const int steps = std::max(
                            1, static_cast<int>(std::ceil(std::sqrt(dx * dx + dz * dz) / 0.25f)));
                        float floor = stage_spawn.y;
                        for (int k = 1; k <= steps; ++k) {
                            const float u = static_cast<float>(k) / static_cast<float>(steps);
                            const float next = heightmap_floor(
                                heightmap,
                                Vec3{stage_spawn.x + dx * u, floor, stage_spawn.z + dz * u},
                                kFloorStep);
                            if (std::isfinite(next)) {
                                floor = next;
                            }
                        }
                        const float columnMax = heightmap_sample(heightmap, Vec3{x, 0.0f, z});
                        std::printf("s7-ground probe=%s floor=%.3f column_max=%.3f\n", name, floor,
                                    columnMax);
                        return std::isfinite(floor) && std::isfinite(columnMax);
                    };
                    if (!print_ground("spawn", stage_spawn.x, stage_spawn.z) ||
                        !print_ground("altar_step", kProbeAltarStep.x, kProbeAltarStep.z) ||
                        !print_ground("hall", kProbeHall.x, kProbeHall.z) ||
                        !print_ground("stair", kProbeStair.x, kProbeStair.z)) {
                        return fail("a ground probe has no floor");
                    }

                    // --- 4. The wall: a forward walk through player_step stops ---
                    {
                        const float dx = kProbeWallTo.x - kProbeWallFrom.x;
                        const float dz = kProbeWallTo.z - kProbeWallFrom.z;
                        const float length = std::sqrt(dx * dx + dz * dz);
                        const float walkYaw = std::atan2(dx, dz) / kDegToRad;
                        Vec3 from{kProbeWallFrom.x, stage_spawn.y, kProbeWallFrom.z};
                        from.y = heightmap_floor(heightmap, from, kFloorStep);
                        player.position = from;
                        player.yawDeg = walkYaw;
                        const std::vector<std::vector<Mat4>> palettes =
                            update_all_poses(renderer, scene, playbacks, 0.0f, club);
                        PlayerInput forward;
                        forward.forward = true;
                        for (int frame = 0; frame < kS7MaxWallFrames; ++frame) {
                            const PlayerFrame wallFrame =
                                player_frame_of(scene, entityRig, palettes, heightmap, walkYaw);
                            player_step(player, combat, forward, wallFrame, tuning, stepSeconds);
                            const float along = ((player.position.x - from.x) * dx +
                                                 (player.position.z - from.z) * dz) /
                                                length;
                            if (along >= length) {
                                break;  // reached the target: nothing stopped the walk
                            }
                        }
                        const float along = ((player.position.x - from.x) * dx +
                                             (player.position.z - from.z) * dz) /
                                            length;
                        const bool blocked = along < length - 0.5f;
                        std::printf("s7-wall from=%.2f,%.2f to=%.2f,%.2f stopped=%.2f,%.2f "
                                    "blocked=%d\n",
                                    kProbeWallFrom.x, kProbeWallFrom.z, kProbeWallTo.x,
                                    kProbeWallTo.z, player.position.x, player.position.z,
                                    blocked ? 1 : 0);
                        if (!blocked) {
                            return fail("the walk toward the wall probe was never stopped");
                        }
                        // Back to the spawn for the stills and the perf frames.
                        reset_player_combat(player, combat, stage_spawn, stage_spawn_yaw, tuning);
                        follow = CameraFollow{};
                        camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f,
                                      0.0f, /*moving=*/false, /*dtSeconds=*/10.0f);
                    }

                    // --- 5. The idols and Mono, from the composed manifest -------
                    int idols = 0;
                    int brokenSlot = 0;
                    for (const ManifestInstance& instance : manifest.instances) {
                        const std::string& glb = instance.glbRelPath;
                        if (glb.find("/statues/") == std::string::npos ||
                            glb.find("_skirt") != std::string::npos) {
                            continue;  // not an idol, or the broken idol's skirt
                        }
                        ++idols;
                        if (brokenSlot == 0 && glb.find("_broke") != std::string::npos) {
                            brokenSlot = idols;
                        }
                    }
                    std::printf("s7-idols count=%d broken_slot=%d\n", idols, brokenSlot);
                    int monoMeshes = 0;
                    if (manifest.posed.size() == 1) {
                        const ManifestPosed& mono = manifest.posed.front();
                        const Mat4 model = mat4_from_trs(mono.position, mono.rotation, mono.scale);
                        for (const MeshCpu& mesh : scene.meshes) {
                            if (mesh.rig < 0 &&
                                std::equal(std::begin(mesh.model.m), std::end(mesh.model.m),
                                           std::begin(model.m))) {
                                ++monoMeshes;
                            }
                        }
                    }
                    if (monoMeshes == 0) {
                        return fail("the posed Mono is not in the composed scene");
                    }
                    std::printf("s7-mono clip=%s present=1\n", manifest.posed.front().clip.c_str());

                    // --- 6. The stills: the altar, the idols, the hall -----------
                    const auto orbit = [](Vec3 target, float distance, float yaw, float pitch) {
                        Camera c{};
                        c.target = target;
                        c.distance = distance;
                        c.yaw = yaw;
                        c.pitch = pitch;
                        return c;
                    };
                    const Camera followCamera = camera;
                    const ManifestPosed& mono = manifest.posed.front();
                    camera = orbit(mono.position, 7.0f, 180.0f, 20.0f);
                    capture("altar");
                    camera = orbit(Vec3{14.0f, stage_spawn.y + 2.5f, -9.0f}, 14.0f, -60.0f, 8.0f);
                    capture("idols");
                    camera = orbit(Vec3{0.0f, stage_spawn.y + 5.0f, -15.0f}, 30.0f, 0.0f, 12.0f);
                    capture("hall");
                    camera = followCamera;

                    // --- 7. The perf: 240 frames of the play view, culled --------
                    render_stage_frame();  // warm-up
                    const Uint64 perfStart = SDL_GetTicksNS();
                    for (int i = 0; i < kS7PerfFrames; ++i) {
                        render_stage_frame();
                    }
                    const double perfMs = static_cast<double>(SDL_GetTicksNS() - perfStart) / 1e6;
                    std::printf("s7-perf frames=%d frame_ms=%.3f drawn=%d culled=%d\n",
                                kS7PerfFrames, perfMs / static_cast<double>(kS7PerfFrames),
                                renderer.drawn_last_frame(), renderer.culled_last_frame());
                    std::printf("s7-status stage=shrine phase=%s lock=%d\n",
                                return_phase_name(ret.phase), ret.inputLocked ? 1 : 0);

                    // --- 8. R: back to the arena ---------------------------------
                    if (!press_reset(death, deathCueFired, nullptr)) {
                        return fail("R in the shrine did not ask for the arena");
                    }
                    carry.s7Step = 2;
                    end_stage(StageId::Arena);
                    continue;
                }

                if (!shrine && carry.s7Step == 2) {
                    // --- 9. The arena again, in its opening state ----------------
                    if (!print_reset()) {
                        return fail("the arena did not come back in its opening state");
                    }
                    carry.s7Step = 3;
                    end_stage(StageId::Shrine);
                    continue;
                }

                if (shrine && carry.s7Step == 3) {
                    // --- 10. The shrine once more: the same scene ----------------
                    if (scene.meshes.size() != carry.shrineMeshes) {
                        return fail("the shrine reloaded with " + std::to_string(scene.meshes.size()) +
                                    " meshes, the first load had " +
                                    std::to_string(carry.shrineMeshes));
                    }
                    print_status_line(opts, shell.win);
                    return 0;
                }
                return fail("unexpected stage step " + std::to_string(carry.s7Step));
            }

            if (opts.s6Selftest) {
                // The S6 acceptance timeline (design §8.5): a club swing (so the
                // whoosh fires on the production path), a scripted lethal event
                // through the production `apply_combat_events` (ruling R6: the real
                // three-strike defeat stays the frozen S4.1 acceptance), the death
                // phase machine to Done with the cinematic camera, the corpse swap
                // and the tendril burst, four stills at the phase times, and the R
                // soft reset. Fixed 1/60 s steps and no window input.
                if (entityClips.stand < 0 || entityClips.club < 0 || entityClips.death < 0) {
                    // The load gate above already refused this case; keep the
                    // invariant explicit so no future path can reach a fallback.
                    throw std::runtime_error(
                        "s6-selftest: a fight clip is missing despite the load gate");
                }
                if (!corpse.loaded) {
                    std::fprintf(stderr, "sotc-viewer: --s6-selftest: the corpse did not load\n");
                    return 1;
                }
                if (!effects_ready) {
                    std::fprintf(stderr,
                                 "sotc-viewer: --s6-selftest: the tendril pass is not available\n");
                    return 1;
                }
                if (!audio.enabled) {
                    std::fprintf(stderr, "sotc-viewer: --s6-selftest: audio is not loaded\n");
                    return 1;
                }

                Playback& entity_playback = playbacks[entityRig];
                const float stepSeconds = kFixedFrameSeconds;

                // The death clip must be the baked motion the machine pins: a
                // re-baked clip that moved would silently shift every phase
                // boundary (R8). The comparison is relative (1e-5), not equality.
                const float deathDuration =
                    entity_playback.clips[static_cast<std::size_t>(entityClips.death)].duration;
                if (!(std::fabs(deathDuration - kMotionSeconds) <= kMotionSeconds * 1e-5f)) {
                    std::fprintf(stderr,
                                 "sotc-viewer: --s6-selftest: the death clip is %.6f s, expected "
                                 "%.6f s\n",
                                 deathDuration, kMotionSeconds);
                    return 1;
                }

                // The follow pose the cinematic starts from and returns to: the
                // snapped follow camera the setup left (the player stands at the
                // spawn). Passed as `base` every frame, so the path cannot feed
                // back on itself.
                const Camera deathBase = camera;

                // One pinned stand frame for the swing phase, then the club attack.
                entity_playback.index = entityClips.stand;
                entity_playback.time = 0.0f;
                entity_playback.pinned = true;
                entity_playback.playing = false;

                SfxQueue sfx;
                DeathState death = reset_death();
                bool deathCueFired = false;

                const auto print_phase = [&]() {
                    std::printf("s6-phase phase=%s t=%.3f total=%.3f lock=%d corpse=%d roots=%d\n",
                                death_phase_name(death.phase), death.t, death.total,
                                death_locks_input(death) ? 1 : 0,
                                death_corpse_visible(death) ? 1 : 0, death.roots);
                };
                const auto flush = [&]() { audio.play_all(sfx); };

                // The opening line: Idle, nothing running.
                print_phase();

                // --- 1. The club swing: the whoosh fires on the production path ---
                start_valus_attack(encounter, entity_playback, entityClips, ValusAttack::Club, &sfx);
                if (encounter.attack != ValusAttack::Club) {
                    std::fprintf(stderr,
                                 "sotc-viewer: --s6-selftest: the club attack did not start\n");
                    return 1;
                }
                flush();  // the telegraph, at the sequence clock 0.000
                bool swingHeard = false;
                for (int frame = 0; frame < kS6MaxSwingFrames && !swingHeard; ++frame) {
                    ValusAttackInput attackInput;
                    attackInput.playerGrounded = true;
                    attackInput.playerPos = player.position;
                    attackInput.entityPos = encounter.entityPosition;
                    attackInput.entityYawDeg = encounter.entityYawDeg;
                    advance_valus_attack(encounter, entity_playback, entityClips, attackInput,
                                         stepSeconds, &sfx);
                    maybe_club_swing(encounter, &sfx);
                    swingHeard = encounter.attackSwinged;
                    audio.update(stepSeconds, player.position, camera_look_yaw(camera));
                    flush();
                }
                if (!swingHeard) {
                    std::fprintf(stderr, "sotc-viewer: --s6-selftest: the club swing never fired\n");
                    return 1;
                }
                // Cut the attack short: the timeline needs the swing, not the
                // impact (which would fire a club_impact cue and could hurt the
                // player). The state clears exactly like a cancelled attack, minus
                // the deferred cancel cue.
                encounter.attack = ValusAttack::None;
                encounter.attackSeconds = 0.0f;
                encounter.attackHit = false;
                encounter.attackSwinged = false;
                encounter.cooldownSeconds = 0.0f;
                encounter.pendingCooldownSeconds = 0.0f;
                entity_playback.index = entityClips.stand;
                entity_playback.time = 0.0f;
                entity_playback.pinned = true;
                entity_playback.playing = false;

                // --- 2. The scripted lethal event (R6) ----------------------------
                StepEvents lethal;
                lethal.damageDealt = 100;  // the pinned lethal damage
                apply_combat_events(encounter, entity_playback, entityClips, lethal,
                                    /*weakpoint=*/-1, &sfx);
                if (!encounter.defeated || encounter.health > 0.0f) {
                    std::fprintf(stderr,
                                 "sotc-viewer: --s6-selftest: the lethal event did not defeat "
                                 "Valus\n");
                    return 1;
                }
                death = step_death(death, stepSeconds, true);
                // The death sequence's clock starts here: from the defeat on the
                // event log's t is the death clock (music_stop at 0.000, the roar
                // at 0.400), which is also what the phase lines print. The defeat
                // frame's own mixer step runs first, so the clock and `death.total`
                // stay in step from the next frame on.
                audio.update(stepSeconds, player.position, camera_look_yaw(camera));
                audio.clock = 0.0f;
                flush();  // nothing is queued at the defeat itself
                audio.start_music();  // the landed hit is the score's downbeat (D5)
                audio.stop_music();   // ... and the defeat stops it (D5)
                if (audio.musicStarted) {
                    std::fprintf(stderr,
                                 "sotc-viewer: --s6-selftest: the defeat did not stop the music\n");
                    return 1;
                }
                print_phase();  // Motion, total = 0.000

                // --- 3. The death phases to Done, with the four stills ------------
                struct StillDue {
                    const char* name;
                    float total;
                    // The tendril still also prices the effect pass (Task 6): the
                    // timed 240-frame pair and the control frame (the same frame
                    // with the pass suppressed) are taken here, at the burst's
                    // peak visibility.
                    bool perf;
                };
                // The stills sit on the cinematic's keys: the motion's first beat,
                // the pull-back key (12 m / 4 m up), the burst, and the corpse wide
                // just after Collapse. The burst still is at 1.200 s: inside the
                // 0.4-3.0 s window, two thirds grown, with both roots and both
                // tendrils still in frame (the camera has not yet closed on the
                // body). That is where the burst reads, and where the effect pass
                // is measured.
                const StillDue stills[4] = {
                    {"motion", 0.200f, false},
                    {"camera", kDeathPullSeconds, false},
                    {"tendrils", 1.200f, true},
                    {"corpse", 6.200f, false},
                };
                int stillIndex = 0;
                int effectRoots = 0;
                DeathPhase printed = death.phase;

                const auto still_path = [&](const char* name) {
                    return still_png_path(opts.s6StillsPath, name);
                };
                // One frame of the death scene, rendered with or without the
                // effect pass: the same pose, the same camera, the same HUD, so
                // the two differ in exactly the effect's pixels. **Every** capture
                // in this timeline goes through it — the stills and the control
                // alike — so the burst's still and its control are provably the
                // same frame, not two copies of a render sequence that happen to
                // agree today.
                const auto render_death_frame = [&](bool withEffect) {
                    const std::vector<std::vector<Mat4>> palettes =
                        update_all_poses(renderer, scene, playbacks, 0.0f, club);
                    apply_player_transform(scene, player_rig_record, player,
                                           active_player_lift(scene, playback, player_rig_record));
                    const EffectFrame frame =
                        build_effect_frame(scene, entityRig, palettes, encounter, death);
                    renderer.set_hud(hud_state(/*visible=*/true));
                    renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                                          /*acquire_swapchain=*/true,
                                          (withEffect && frame.active) ? &frame : nullptr);
                };
                const auto capture_still = [&](const char* name) {
                    if (opts.s6StillsPath.empty()) {
                        return;
                    }
                    with_clocks_restored(playbacks, [&]() {
                        render_death_frame(/*withEffect=*/true);
                        write_screenshot(renderer, shell.win, still_path(name));
                    });
                };

                bool done = false;
                for (int frame = 0; frame < kS6MaxDeathFrames; ++frame) {
                    advance_death_frame(death, stepSeconds, entity_playback, entityClips.death,
                                        corpse, club, scene, entityRig);
                    if (death_cue_due(death, deathCueFired)) {
                        append_encounter_sfx(EncounterSfx::ValusDefeat, encounter.entityPosition,
                                             sfx);
                    }
                    camera = camera_death_pose(deathBase, encounter.entityPosition, player.position,
                                               death.total);
                    const std::vector<std::vector<Mat4>> palettes =
                        update_all_poses(renderer, scene, playbacks, 0.0f, club);
                    apply_player_transform(scene, player_rig_record, player,
                                           active_player_lift(scene, playback, player_rig_record));
                    const EffectFrame effectFrame =
                        build_effect_frame(scene, entityRig, palettes, encounter, death);
                    effectRoots = std::max(effectRoots, effectFrame.rootCount);
                    audio.update(stepSeconds, player.position, camera_look_yaw(camera));
                    flush();
                    if (death.phase != printed) {
                        printed = death.phase;
                        print_phase();
                    }
                    while (stillIndex < 4 &&
                           death.total + kS6Eps >= stills[stillIndex].total) {
                        capture_still(stills[stillIndex].name);
                        if (stills[stillIndex].perf) {
                            // --- the effect pass's price, and its control frame ---
                            // The burst is the milestone's first per-frame dynamic
                            // geometry: two 402-vertex ribbons and 40 billboards,
                            // rebuilt and uploaded every frame. The gate times the
                            // frame at the burst: 240 frames with the pass drawing
                            // and 240 with it suppressed from the same pose and
                            // camera, so the marginal is the pass's own cost. The
                            // suppressed frame is written as the control still: its
                            // difference with the tendrils still is exactly the
                            // effect's pixels (design §8.5).
                            render_death_frame(/*withEffect=*/true);  // warm-up
                            const Uint64 effectStart = SDL_GetTicksNS();
                            for (int i = 0; i < kS6PerfFrames; ++i) {
                                render_death_frame(/*withEffect=*/true);
                            }
                            const Uint64 baseStart = SDL_GetTicksNS();
                            for (int i = 0; i < kS6PerfFrames; ++i) {
                                render_death_frame(/*withEffect=*/false);
                            }
                            const Uint64 end = SDL_GetTicksNS();
                            // The control frame: the last base render is the frame
                            // on screen, so the capture is the pass-less twin of
                            // the tendrils still.
                            if (!opts.s6StillsPath.empty()) {
                                render_death_frame(/*withEffect=*/false);
                                write_screenshot(renderer, shell.win,
                                                 still_path("tendrils-control"));
                            }
                            const double effectMs =
                                static_cast<double>(baseStart - effectStart) / 1e6;
                            const double baseMs =
                                static_cast<double>(end - baseStart) / 1e6;
                            const double marginalMs =
                                (effectMs - baseMs) / static_cast<double>(kS6PerfFrames);
                            const double frameMs = effectMs / static_cast<double>(kS6PerfFrames);
                            std::printf(
                                "s6-perf frames=%d effect_ms=%.3f base_ms=%.3f marginal_ms=%.3f "
                                "frame_ms=%.3f\n",
                                kS6PerfFrames, effectMs, baseMs, marginalMs, frameMs);
                            if (!(frameMs > 0.0) || !(marginalMs < 1000.0)) {
                                std::fprintf(stderr,
                                             "sotc-viewer: --s6-selftest: the effect pass's timing "
                                             "is degenerate (effect %.3f ms, base %.3f ms)\n",
                                             effectMs, baseMs);
                                return 1;
                            }
                        }
                        ++stillIndex;
                    }
                    if (death.phase == DeathPhase::Done) {
                        done = true;
                        break;
                    }
                }
                if (!done || death.phase != DeathPhase::Done) {
                    std::fprintf(stderr,
                                 "sotc-viewer: --s6-selftest: the death did not reach Done in %d "
                                 "frames (phase %s, total %.3f)\n",
                                 kS6MaxDeathFrames, death_phase_name(death.phase), death.total);
                    return 1;
                }
                if (stillIndex != 4) {
                    std::fprintf(stderr,
                                 "sotc-viewer: --s6-selftest: only %d of the 4 stills fired\n",
                                 stillIndex);
                    return 1;
                }
                if (effectRoots != kDeathRoots) {
                    std::fprintf(stderr,
                                 "sotc-viewer: --s6-selftest: the tendril roots resolved %d of %d\n",
                                 effectRoots, kDeathRoots);
                    return 1;
                }
                if (audio.failed) {
                    // The S5 finish's own gate (a mapped cue went missing at play
                    // time): the S6 run must fail too, not print a clean status.
                    std::fprintf(stderr,
                                 "sotc-viewer: --s6-selftest: a cue clip was missing at play "
                                 "time\n");
                    return 1;
                }

                // The dead colossus must be a static prop in Done (design §8.3):
                // the player frame carries no entity capsules, no climb joints and
                // no weakpoint shells, so it cannot be walked into, climbed or
                // struck. The live control (the same pose, entityLive = true)
                // proves the geometry is still there and only the death gate is off.
                const std::vector<std::vector<Mat4>> donePalettes =
                    update_all_poses(renderer, scene, playbacks, 0.0f, club);
                const PlayerFrame liveFrame =
                    player_frame_of(scene, entityRig, donePalettes, heightmap,
                                    camera_look_yaw(camera), /*entityLive=*/true);
                const PlayerFrame deadFrame =
                    player_frame_of(scene, entityRig, donePalettes, heightmap,
                                    camera_look_yaw(camera), /*entityLive=*/false);
                if (liveFrame.entityCapsules.empty() || !deadFrame.entityCapsules.empty() ||
                    deadFrame.jointWorld || deadFrame.weakpointProxy) {
                    std::fprintf(stderr,
                                 "sotc-viewer: --s6-selftest: the corpse left the dead colossus "
                                 "collidable or climbable (live capsules %zu, dead capsules %zu)\n",
                                 liveFrame.entityCapsules.size(), deadFrame.entityCapsules.size());
                    return 1;
                }

                const int deathHealth = static_cast<int>(std::lround(encounter.health));
                if (deathHealth != 0) {
                    std::fprintf(stderr,
                                 "sotc-viewer: --s6-selftest: Valus's health at Done is %d, "
                                 "expected 0\n",
                                 deathHealth);
                    return 1;
                }
                const std::string deathClip =
                    entity_playback.clips[static_cast<std::size_t>(entityClips.death)].name;
                std::printf("s6-death clip=%s total=%.3f corpse=%d health=%d camera=%s\n",
                            deathClip.c_str(), death.total, death_corpse_visible(death) ? 1 : 0,
                            deathHealth, death_camera_key(death.total));
                print_status_line(opts, shell.win);
                if (!encounter.defeated || !death_corpse_visible(death) ||
                    death.roots != kDeathRoots || combat.mode == PlayerMode::Dead) {
                    std::fprintf(stderr,
                                 "sotc-viewer: --s6-selftest: the post-death state is wrong "
                                 "(defeated %d, corpse %d, roots %d, mode %s)\n",
                                 encounter.defeated ? 1 : 0, death_corpse_visible(death) ? 1 : 0,
                                 death.roots, player_mode_name(combat.mode));
                    return 1;
                }
                std::printf("s6-status defeated=%d corpse=%d roots=%d player_dead=%d\n",
                            encounter.defeated ? 1 : 0, death_corpse_visible(death) ? 1 : 0,
                            death.roots, combat.mode == PlayerMode::Dead ? 1 : 0);

                // --- 4. The R soft reset (the interactive handler's own helper) --
                // The *same* helper the interactive `R` key calls, so the proof and
                // the behaviour cannot drift apart (anti-goal A2).
                soft_reset_encounter(death, deathCueFired, /*cameraBaseSet=*/nullptr);
                // The reset must restore the actual draw gate, not just the flags:
                // the live entity and its club visible, the corpse hidden.
                const bool entityRestored =
                    entityRig < scene.rigs.size() &&
                    mesh_range_visible(scene, scene.rigs[entityRig].meshBegin,
                                       scene.rigs[entityRig].meshEnd);
                const bool corpseHidden =
                    !corpse.loaded || !mesh_range_visible(scene, corpse.meshBegin, corpse.meshEnd);
                const bool clubRestored =
                    !club.loaded || mesh_range_visible(scene, club.meshBegin, club.meshEnd);
                if (death.phase != DeathPhase::Idle || death_corpse_visible(death) ||
                    death_locks_input(death) || !entityRestored || !corpseHidden ||
                    !clubRestored ||
                    static_cast<int>(std::lround(encounter.health)) != 100 ||
                    static_cast<int>(std::lround(combat.health)) != 100) {
                    std::fprintf(stderr,
                                 "sotc-viewer: --s6-selftest: the reset did not restore the opening "
                                 "state (entity visible %d, corpse hidden %d, club visible %d, "
                                 "valus %d, player %d)\n",
                                 entityRestored ? 1 : 0, corpseHidden ? 1 : 0,
                                 clubRestored ? 1 : 0,
                                 static_cast<int>(std::lround(encounter.health)),
                                 static_cast<int>(std::lround(combat.health)));
                    return 1;
                }
                std::printf("s6-reset phase=%s corpse=%d lock=%d valus=%d player=%d\n",
                            death_phase_name(death.phase), death_corpse_visible(death) ? 1 : 0,
                            death_locks_input(death) ? 1 : 0,
                            static_cast<int>(std::lround(encounter.health)),
                            static_cast<int>(std::lround(combat.health)));

                // --- 5. The arm-root ordering (design §8.2) -----------------------
                // The run above used the route-less weakpoint -1, so the burst
                // started at the forehead. A killing blow on weakpoint 1 must put
                // the right arm's shell first; this silent second death proves the
                // hit weakpoint drives the order, then resets the run again.
                StepEvents armLethal;
                armLethal.damageDealt = 100;
                apply_combat_events(encounter, entity_playback, entityClips, armLethal,
                                    /*weakpoint=*/1, nullptr);
                death = step_death(death, stepSeconds, true);
                for (int frame = 0; frame < 32 && !death_tendrils_active(death); ++frame) {
                    death = step_death(death, stepSeconds, true);
                }
                const std::vector<std::vector<Mat4>> armPalettes =
                    update_all_poses(renderer, scene, playbacks, 0.0f, club);
                const EffectFrame armFrame =
                    build_effect_frame(scene, entityRig, armPalettes, encounter, death);
                const char* armMaterial = weakpoint_material_name(1);
                const char* headMaterial = weakpoint_material_name(0);
                Vec3 armShell{};
                Vec3 headShell{};
                float armRadius = 0.0f;
                float headRadius = 0.0f;
                const bool armResolved =
                    armMaterial != nullptr && entityRig < armPalettes.size() &&
                    posed_weakpoint_proxy(scene, entityRig, armPalettes[entityRig], armMaterial,
                                          armShell, armRadius);
                const bool headResolved =
                    headMaterial != nullptr && entityRig < armPalettes.size() &&
                    posed_weakpoint_proxy(scene, entityRig, armPalettes[entityRig], headMaterial,
                                          headShell, headRadius);
                const auto near = [](Vec3 a, Vec3 b) {
                    return std::fabs(a.x - b.x) <= 1e-3f && std::fabs(a.y - b.y) <= 1e-3f &&
                           std::fabs(a.z - b.z) <= 1e-3f;
                };
                if (!death_tendrils_active(death) || armFrame.rootCount != kDeathRoots ||
                    !armResolved || !headResolved || !near(armFrame.roots[0].position, armShell) ||
                    !near(armFrame.roots[1].position, headShell)) {
                    std::fprintf(stderr,
                                 "sotc-viewer: --s6-selftest: the weakpoint-1 death did not put the "
                                 "arm shell first (roots %d, burst %d, arm %d, head %d)\n",
                                 armFrame.rootCount, death_tendrils_active(death) ? 1 : 0,
                                 armResolved ? 1 : 0, headResolved ? 1 : 0);
                    return 1;
                }
                // Leave the run in the opening state (no output: this check is an
                // assertion, not a status line).
                reset_encounter(encounter);
                death = reset_death();
                apply_death_visibility(scene, corpse, club, entityRig, /*corpseVisible=*/false);
                entity_playback.index = entityClips.stand;
                entity_playback.time = 0.0f;
                entity_playback.pinned = false;
                entity_playback.playing = true;
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
                // The entity's *drawn* region is the skinned rig plus the club it
                // holds. The club is a separate static mesh range outside the rig's
                // bbox, and it is what the entity-less presence control removes, so
                // leaving it out would put its pixels outside the guard's region.
                // Place it from the same pinned pose the captures draw.
                ScreenBox entity_bbox = project_rig_bounds(scene, entityRig, entity_palette,
                                                           idle_view_proj, pixel_width,
                                                           pixel_height);
                const RigCpu& entity_rig_record = rig_at(scene, entityRig);
                if (club.loaded && entity_rig_record.meshBegin < scene.meshes.size()) {
                    const Mat4 club_placement =
                        club_model(scene, entity_palette, club.swordJoint,
                                   scene.meshes[entity_rig_record.meshBegin].model);
                    entity_bbox = union_screen_boxes(
                        entity_bbox, project_club_bounds(scene, club, club_placement, idle_view_proj,
                                                         pixel_width, pixel_height));
                }

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
            // The S6 death sequence's own state (design §8.1): the phase machine,
            // the one-shot burst-cue latch, and the follow pose the cinematic
            // starts from. All three are reset by R.
            DeathState death = reset_death();
            bool deathCueFired = false;
            bool deathCameraSet = false;
            Camera deathCameraBase{};
            // The S5 audio observer's on/off switch for the whole loop: false when
            // no curated manifest loaded (the game then stays silent).
            const bool audio_on = audio.enabled;
            GamepadInput gamepad;
            open_first_gamepad(gamepad);
            // This frame's rig poses. Hoisted so Valus's attack decision can read
            // the previous frame's posed feet before this frame's clip is chosen.
            std::vector<std::vector<Mat4>> palettes =
                update_all_poses(renderer, scene, playbacks, /*dt=*/0.0f, club);
            std::deque<Uint64> frame_ticks;  // frame arrival times within the last second
            Uint64 last_title_update = SDL_GetTicks();
            Uint64 last_ticks = SDL_GetTicks();
            // How the iteration ends: quit (Escape, the close button), or —
            // S7 — the shrine at the return's Load, or the arena on R.
            StageRequest request{};
            // The R key's handler. In manifest mode it is the full soft reset
            // (design §7.5): the player and Valus go back to their opening
            // state, the death pin/hurt one-shot is left behind, and the follow
            // camera snaps behind the spawn. The shared helper is the one the S6
            // selftest calls, so the acceptance proves this path. S7: from the
            // shrine R reloads the arena. --s7-e2e's scripted R calls this too.
            const auto press_r_key = [&]() {
                if (follow_mode) {
                    if (press_reset(death, deathCueFired, &deathCameraSet)) {
                        request = StageRequest{false, StageId::Arena};
                        running = false;
                    }
                } else {
                    playback.time = 0.0f;
                }
            };
            // --s7-e2e (Task 13): the script's view of this stage. The loop
            // below is the interactive one; the script only picks the input
            // and dt, injects the one lethal hit and the one R, and prints and
            // checks the transitions it sees.
            struct E2eScript {
                int frames = 0;             // this stage's frames, against the budget
                float stageSeconds = 0.0f;  // fixed-step seconds in this stage
                float freeSeconds = 0.0f;   // seconds the shrine has held Free
                bool hit = false;
                bool lockPrinted = false;
                bool unlockPrinted = false;
                int wakeClip = -1;
                int wakeClips = 0;
                DeathPhase death = DeathPhase::Idle;
                ReturnPhase ret = ReturnPhase::Idle;
            } e2e;
            const auto e2e_fail = [](const std::string& message) {
                std::fprintf(stderr, "sotc-viewer: --s7-e2e: %s\n", message.c_str());
                return 1;
            };
            const auto e2e_print_death = [&]() {
                std::printf("s7e2e-death phase=%s total=%.3f\n", death_phase_name(death.phase),
                            death.total);
                e2e.death = death.phase;
            };
            const auto e2e_print_return = [&]() {
                std::printf("s7e2e-return phase=%s t=%.3f fade=%.3f lock=%d\n",
                            return_phase_name(carry.ret.phase), carry.ret.t, carry.ret.fade,
                            carry.ret.inputLocked ? 1 : 0);
                e2e.ret = carry.ret.phase;
            };
            if (opts.s7E2e) {
                if (!return_wired ||
                    (!shrine && (!death_wired || !corpse.loaded || entityRig >= playbacks.size() ||
                                 entityClips.death < 0))) {
                    return e2e_fail("the arena has no death or return to drive");
                }
                e2e.ret = carry.ret.phase;
                if (shrine) {
                    // The join: the shrine is the next iteration of the arena's
                    // StageRequest, entered in Load and nowhere else.
                    if (carry.e2eStep != 0 || carry.ret.phase != ReturnPhase::Load) {
                        return e2e_fail(std::string("the shrine loaded outside the return's Load "
                                                    "(phase ") +
                                        return_phase_name(carry.ret.phase) + ")");
                    }
                    carry.e2eStep = 1;
                } else if (carry.e2eStep == 0) {
                    e2e_print_death();
                    e2e_print_return();
                }
            }
            while (running) {
                // This frame's drag and wheel deltas (motion events arrive several
                // per frame; the follow camera takes their sum).
                float drag_dx = 0.0f;
                float drag_dy = 0.0f;
                float wheel_delta = 0.0f;
                climb_recenter = false;

                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    if (opts.s7E2e && event.type != SDL_EVENT_QUIT) {
                        continue;  // --s7-e2e: the script owns the input
                    }
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
                            } else if (event.key.key == SDLK_M) {
                                // S5: mute the whole mixer (design D6: no new UI).
                                // Key repeat must not flap the mixer: only the
                                // physical press toggles.
                                if (!event.key.repeat) {
                                    audio.toggle_mute();
                                }
                            } else if (event.key.key == SDLK_T) {
                                // Camera-only framing: orbit the entity's chest. T
                                // is inert when the manifest has no entity (the S3
                                // arena) or its chest did not resolve — otherwise
                                // the camera would park on the zero target.
                                if (entity_target_key_allowed(follow_mode, haveEntityChest)) {
                                    camera_target_toggle(camera, follow, targetCamera, entityChest);
                                }
                            } else if (event.key.key == SDLK_R) {
                                press_r_key();
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
                // --s7-e2e steps the real loop at the selftests' fixed 1/60 s.
                const float dt = opts.s7E2e ? kFixedFrameSeconds
                                            : static_cast<float>(now - last_ticks) / 1000.0f;
                last_ticks = now;

                if (!opts.s7E2e) {
                    gamepad.poll();
                }
                if (gamepad.pausePressed) {
                    playback.playing = !playback.playing;
                }

                // S6: this frame's tendril frame — nullptr unless the burst is
                // active (the renderer's no-op for every other frame).
                EffectFrame effectFrame;
                const EffectFrame* effectFramePtr = nullptr;
                if (follow_mode) {
                    // --s7-e2e: the stage's clock, the budget, and the script's
                    // two late actions — R 1 s into Free (the key's handler),
                    // and the exit 1 s into the reloaded arena.
                    if (opts.s7E2e) {
                        ++carry.e2eFrame;
                        if (++e2e.frames > kS7E2eMaxStageFrames) {
                            return e2e_fail(std::string(shrine ? "the shrine" : "the arena") +
                                            " never finished its step (death " +
                                            death_phase_name(death.phase) + ", return " +
                                            return_phase_name(carry.ret.phase) + ")");
                        }
                        e2e.stageSeconds += dt;
                        if (shrine && e2e.freeSeconds + kS7Eps >= kS7E2eLeadSeconds) {
                            const float floor =
                                heightmap_floor(heightmap, player.position, kFloorStep);
                            std::printf("s7e2e-status stage=shrine phase=%s lock=%d player_y=%.3f "
                                        "floor=%.3f\n",
                                        return_phase_name(carry.ret.phase),
                                        carry.ret.inputLocked ? 1 : 0, player.position.y, floor);
                            if (e2e.wakeClips != 2) {
                                return e2e_fail("the wake did not play both clips");
                            }
                            if (!(std::fabs(player.position.y - floor) <= kS7E2eFloorTolerance)) {
                                return e2e_fail("the player is not on the floor at Free");
                            }
                            carry.e2eStep = 2;
                            press_r_key();
                            if (running) {
                                return e2e_fail("R in the shrine did not ask for the arena");
                            }
                        } else if (!shrine && carry.e2eStep == 2 &&
                                   e2e.stageSeconds + kS7Eps >= kS7E2eLeadSeconds) {
                            const bool entityShown =
                                entityRig < scene.rigs.size() &&
                                mesh_range_visible(scene, scene.rigs[entityRig].meshBegin,
                                                   scene.rigs[entityRig].meshEnd);
                            const bool corpseHidden =
                                !corpse.loaded ||
                                !mesh_range_visible(scene, corpse.meshBegin, corpse.meshEnd);
                            const int valus = static_cast<int>(std::lround(encounter.health));
                            const int hero = static_cast<int>(std::lround(combat.health));
                            if (carry.ret.phase != ReturnPhase::Idle ||
                                death.phase != DeathPhase::Idle || encounter.defeated ||
                                !entityShown || !corpseHidden || valus != 100 || hero != 100) {
                                return e2e_fail("R did not restore the arena's opening state");
                            }
                            std::printf("s7e2e-reset stage=arena valus=%d player=%d death=%s "
                                        "return=%s\n",
                                        valus, hero, death_phase_name(death.phase),
                                        return_phase_name(carry.ret.phase));
                            print_status_line(opts, shell.win);
                            request = StageRequest{};
                            running = false;
                        }
                    }
                    // S6: the death sequence's clock runs on the real frame time.
                    // The defeat transition happens later in this frame (in
                    // apply_combat_events), so the machine starts on the next frame
                    // with total = 0 and does not accumulate the defeat frame's dt
                    // (death.h's pinned rule). While it runs, the machine's clock
                    // drives the one-shot death clip and gates the player input;
                    // the corpse swap follows the same clock (advance_death_frame).
                    if (death_wired && encounter.defeated && entityRig < playbacks.size()) {
                        advance_death_frame(death, dt, playbacks[entityRig], entityClips.death,
                                            corpse, club, scene, entityRig);
                    }
                    // S7: the return (design §8.4) on the real frame time. The
                    // shrine's iteration is the completed load; entering Load in
                    // the arena ends the iteration with the request for it (this
                    // frame still draws, white).
                    if (return_wired) {
                        const ReturnState before = carry.ret;
                        carry.ret = step_return(carry.ret, dt, returnTimings,
                                                death.phase == DeathPhase::Done,
                                                /*loadDone=*/shrine);
                        // A quit (Escape, the close button) this frame wins
                        // over the stage request.
                        if (running && return_wants_load(before, carry.ret)) {
                            request = StageRequest{false, StageId::Shrine};
                            running = false;
                        }
                    }
                    // --s7-e2e watches the transitions the loop just made (until
                    // its R) and expects the lock from FadeOut through Wake.
                    bool e2eExpectLocked = false;
                    bool e2eHitNow = false;
                    if (opts.s7E2e && carry.e2eStep < 2) {
                        if (death.phase != e2e.death) {
                            e2e_print_death();
                        }
                        if (carry.ret.phase != e2e.ret) {
                            e2e_print_return();
                        }
                        if (carry.ret.phase == ReturnPhase::Free) {
                            e2e.freeSeconds += dt;
                        }
                        const ReturnPhase phase = carry.ret.phase;
                        e2eExpectLocked = phase == ReturnPhase::FadeOut ||
                                          phase == ReturnPhase::Load ||
                                          phase == ReturnPhase::FadeIn || phase == ReturnPhase::Wake;
                        e2eHitNow = !shrine && !e2e.hit &&
                                    e2e.stageSeconds + kS7Eps >= kS7E2eLeadSeconds;
                    }
                    const auto deadzoned = [](float value) {
                        return std::fabs(value) > kS41StickDeadzone ? value : 0.0f;
                    };
                    // The one input seam: the keyboard and gamepad, or under
                    // --s7-e2e the script's (empty, except a held W while the
                    // return should lock it — the lock below must eat it).
                    PlayerInput input;
                    if (opts.s7E2e) {
                        input.forward = e2eExpectLocked;
                    } else {
                        const bool* keys = SDL_GetKeyboardState(nullptr);
                        input.forward = keys[SDL_SCANCODE_W] || deadzoned(gamepad.leftY) < 0.0f;
                        input.back = keys[SDL_SCANCODE_S] || deadzoned(gamepad.leftY) > 0.0f;
                        input.left = keys[SDL_SCANCODE_A] || deadzoned(gamepad.leftX) < 0.0f;
                        input.right = keys[SDL_SCANCODE_D] || deadzoned(gamepad.leftX) > 0.0f;
                        input.run = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
                        input.grab = keys[SDL_SCANCODE_E] || gamepad.grab;
                        input.strike = keys[SDL_SCANCODE_J] || gamepad.strike;
                    }
                    // S6 D7: the death cinematic locks the body — movement, grab
                    // and strike are ignored while the camera runs; R, M and Escape
                    // are handled above and stay live. Control returns in Done.
                    // S7: the return locks it from FadeOut through Wake.
                    const bool inputLocked = death_locks_input(death) || carry.ret.inputLocked;
                    if (inputLocked) {
                        input = PlayerInput{};
                    }
                    const bool moving = input.forward || input.back || input.left || input.right;
                    if (opts.s7E2e && carry.e2eStep < 2) {
                        if (e2eExpectLocked && moving) {
                            return e2e_fail(std::string("input is not locked in ") +
                                            return_phase_name(carry.ret.phase));
                        }
                        if (carry.ret.phase == ReturnPhase::Free && inputLocked) {
                            return e2e_fail("input is still locked at Free");
                        }
                        if (inputLocked && !e2e.lockPrinted && !shrine) {
                            std::printf("s7e2e-input frame=%d locked=1\n", carry.e2eFrame);
                            e2e.lockPrinted = true;
                        }
                        if (!inputLocked && !e2e.unlockPrinted &&
                            carry.ret.phase == ReturnPhase::Free) {
                            std::printf("s7e2e-input frame=%d locked=0\n", carry.e2eFrame);
                            e2e.unlockPrinted = true;
                        }
                    }

                    // The right stick feeds the cameras' drag channel; clamp the
                    // frame time so a hitch cannot fling the view.
                    const float stick_dt = std::clamp(dt, 0.0f, 0.05f);
                    drag_dx += deadzoned(gamepad.rightX) * kS41StickPixelsPerSecond * stick_dt;
                    drag_dy += deadzoned(gamepad.rightY) * kS41StickPixelsPerSecond * stick_dt;

                    // The encounter's one-shot clock, then Valus's own fight state
                    // (R8), then the clip choice (from the mode the previous step
                    // left) and this frame's poses: the target camera, the collision
                    // proxy and the climb attach point all read the palettes drawn
                    // today. Valus reads the previous palette's posed feet; the
                    // model transform is already this frame's, because the yaw step
                    // below runs before the joint query.
                    SfxQueue frameSfx;  // S5: this frame's cues, drained once below
                    if (entityRig < playbacks.size() && !encounter.defeated) {
                        advance_encounter_reaction(playbacks[entityRig], encounter,
                                                   entityClips.stand, dt);
                    }
                    // S6: the death roar fires at the tendril burst (0.4 s), not at
                    // the defeat — the machine's clock owns the moment.
                    if (death_wired && death_cue_due(death, deathCueFired) && audio_on) {
                        append_encounter_sfx(EncounterSfx::ValusDefeat, encounter.entityPosition,
                                             frameSfx);
                    }
                    if (valus_behavior) {
                        const float valus_dt = std::clamp(dt, 0.0f, kMaxCombatStepSeconds);
                        // He turns only for an exactly grounded, living target: while
                        // the player is on him the body holds still, or the climber
                        // would slide around the band and the route would move.
                        if (combat.mode == PlayerMode::Grounded && !encounter.defeated) {
                            encounter.entityYawDeg = valus_yaw_slew(
                                encounter.entityYawDeg, encounter.entityPosition,
                                player.position, kValusYawSlewDegPerSecond, valus_dt);
                        }
                        update_entity_transform(scene, encounter, entityRig);
                        const ValusAttackInput attack_input = valus_attack_input(
                            scene, entityRig, palettes, player, combat, encounter);
                        advance_valus_attack(encounter, playbacks[entityRig], entityClips,
                                             attack_input, valus_dt,
                                             audio_on ? &frameSfx : nullptr);
                        // S6: the club's mid-swing whoosh (the telegraph already
                        // sounded at the clip's start).
                        maybe_club_swing(encounter, audio_on ? &frameSfx : nullptr);
                    }
                    // S7: while waking the wake clips own the player's clip.
                    const bool wakeFrame = waking();
                    if (wakeFrame) {
                        pose_wake();
                        if (opts.s7E2e && carry.ret.phase == ReturnPhase::Wake &&
                            playback.index != e2e.wakeClip) {
                            e2e.wakeClip = playback.index;
                            ++e2e.wakeClips;
                            std::printf("s7e2e-wake clip=%s t=%.3f\n",
                                        playback.clips[static_cast<std::size_t>(playback.index)]
                                            .name.c_str(),
                                        wake_clock());
                        }
                    } else {
                        leave_wake();
                        update_combat_clips(playback, scene, combat, input, manual_clip);
                    }
                    palettes = update_all_poses(renderer, scene, playbacks, dt, club);
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
                    // the attach point from this frame's entity palette. Once the
                    // corpse has replaced the body (S6, design §8.3) the frame
                    // carries no live entity: nothing to walk into, climb or strike.
                    const PlayerFrame frame =
                        player_frame_of(scene, entityRig, palettes, heightmap,
                                        camera_look_yaw(camera),
                                        /*entityLive=*/!death_corpse_visible(death));
                    const int weakpoint = combat.weakpoint;  // detach() clears it
                    StepEvents events =
                        player_step(player, combat, input, frame, tuning, dt);
                    if (audio_on) {
                        append_player_sfx(events, player.position, frameSfx);
                    }
                    if (e2eHitNow) {
                        // --s7-e2e's one lethal hit, landed as a strike lands.
                        events.damageDealt = 100;
                        e2e.hit = true;
                    }
                    if (events.damageDealt > 0 && entityRig < playbacks.size()) {
                        apply_combat_events(encounter, playbacks[entityRig], entityClips, events,
                                            weakpoint, audio_on ? &frameSfx : nullptr);
                    }
                    if (e2eHitNow && !encounter.defeated) {
                        return e2e_fail("the lethal hit did not defeat Valus");
                    }
                    // Valus's impact runs after the player's step: a strike that
                    // landed this frame has already cancelled the attack above, so
                    // its pending impact cannot fire. The first frame inside the
                    // window consumes it, even when the hit is then ignored.
                    if (valus_behavior && encounter.attack != ValusAttack::None &&
                        attack_impact_due(encounter.attack, encounter.attackSeconds,
                                          encounter.attackHit)) {
                        encounter.attackHit = true;
                        apply_valus_impact(combat, player, frame, tuning, encounter.attack,
                                           audio_on ? &frameSfx : nullptr,
                                           audio_on ? &events : nullptr);
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

                    apply_player_transform(scene, player_rig_record, player, player_lift());
                    if (wakeFrame) {
                        camera = camera_wake_pose(stage_spawn, stage_spawn_yaw, wake_clock(),
                                                  returnTimings.wake);
                    }
                    if (audio_on) {
                        // Cues were pushed in time order (step, reaction, impact,
                        // hurt); drain once, then the mix advances.
                        audio.play_all(frameSfx);
                        if (events.damageDealt > 0) {
                            audio.start_music();
                        }
                        // D5: the score stops on Valus's defeat and on the R reset,
                        // never on Wander's death — it runs through the death clip.
                        if (encounter.defeated) {
                            audio.stop_music();
                        }
                        audio.update(dt, player.position, camera_look_yaw(camera));
                    }
                    // S6: the cinematic camera while the death runs (D5/D7), then
                    // this frame's tendril burst. The corpse swap runs with the
                    // machine at the top of the frame (advance_death_frame); the
                    // camera override comes last: it owns the view through Motion
                    // and Collapse, and Done hands it back to the follow camera.
                    if (death_wired && encounter.defeated) {
                        if (death_locks_input(death)) {
                            if (!deathCameraSet) {
                                // The pose the cinematic starts from: this frame's
                                // camera — the follow pose in the ordinary defeat,
                                // the climb/target pose if that owned the view on
                                // the first locked frame.
                                deathCameraBase = camera;
                                deathCameraSet = true;
                            }
                            camera = camera_death_pose(deathCameraBase, encounter.entityPosition,
                                                       player.position, death.total);
                        }
                        effectFrame =
                            build_effect_frame(scene, entityRig, palettes, encounter, death);
                        if (effectFrame.active) {
                            effectFramePtr = &effectFrame;
                        }
                    }
                    renderer.set_hud(hud_state(/*visible=*/true));
                } else {
                    update_all_poses(renderer, scene, playbacks, dt, club);
                }

                renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                                      /*acquire_swapchain=*/true, effectFramePtr);

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
            if (request.quit) {
                return 0;
            }
            renderer.release_scene();
            stage = request.next;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "sotc-viewer: %s\n", error.what());
        return 1;
    }
}
