#include "app.h"

#include "anim.h"
#include "asset_io.h"
#include "audio.h"
#include "audio_seam.h"
#include "audio_events.h"
#include "audio_map.h"
#include "camera.h"
#include "capture.h"
#include "collision.h"
#include "death.h"
#include "door.h"
#include "effect.h"
#include "encounter_rt.h"
#include "gamepad_input.h"
#include "gamepad_map.h"
#include "grab.h"
#include "heightmap.h"
#include "manifest.h"
#include "player.h"
#include "player_state.h"
#include "renderer.h"
#include "ride.h"
#include "ride_points.h"
#include "road_points.h"
#include "run_flags.h"
#include "scene.h"
#include "selftests.h"
#include "stage_ctx.h"
#include "shrine_points.h"
#include "sword.h"
#include "transition.h"
#include "wav_io.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
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

namespace {

// --- S4 encounter self-test (Task 6) ---------------------------------------


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


// --- S7 return and self-test (Task 9) ---------------------------------------


// --s7-e2e (Task 13): the script's three waits, and a generous budget for
// each stage (the arena's 1 + 8 + 2 + 1 s, the shrine's 1.5 + ~11.8 + 1 s).
constexpr float kS7E2eLeadSeconds = 1.0f;  // arena before the hit; Free and arena before R/exit
constexpr int kS7E2eMaxStageFrames = 3600;  // 60 s at 1/60 s
constexpr float kS7E2eFloorTolerance = 0.05f;  // the feet vs heightmap_floor at Free

// --- S8: the plain, Agro and riding (design D10-D15) --------------------------
// Authored values (source: authored) are labelled; the clip names are data.
// --s8-selftest (Task 9): the script's budgets, tolerances and scaffolding
// (authored: the probe points are placements.json's).
constexpr float kS8MaxWhistleSeconds = 30.0f;  // the ~60 m approach, neigh and neigh_end
constexpr float kS8ArriveMax = 3.5f;           // m: the whistle's arrival gate
constexpr float kS8MountSide = 1.0f;           // m beside her (within kMountReach)
constexpr float kS8MountStillSeconds = 0.8f;   // mount.png: mid stand_ride_l
constexpr float kS8GaitSeconds = 2.0f;         // each loop's speed window
constexpr float kS8Tolerance = 0.05f;          // ±5 % on the speeds and the yaw rate
constexpr float kS8SteerRampSeconds = 0.25f;   // the steer blend settles first
constexpr float kS8SteerSeconds = 2.0f;
constexpr float kS8BackSeconds = 0.5f;         // the back loop's speed window
constexpr float kS8EdgeKickGapSeconds = 0.6f;  // walk -> trot toward the edge
constexpr float kS8MaxEdgeSeconds = 30.0f;     // the 40 m edge probe, ridden or on foot
constexpr float kS8KickGapSeconds = 1.1f;      // spaced kicks: never the 3-in-1 s cooldown
constexpr float kS8RiderOffsetMax = 0.10f;     // m: the pelvis's departure from its seated rest
constexpr int kS8PerfFrames = 240;
// --s8-e2e (Task 9, authored): the walk/ride script's tolerances and budgets.
constexpr int kS8E2eMaxStageFrames = 5400;    // 90 s at 1/60 s per stage
constexpr float kS8E2eArrive = 0.15f;         // m: a scripted walk's waypoint reached
constexpr float kS8E2eStuckSeconds = 3.0f;    // no 0.5 m of progress: the walk is blocked
constexpr float kS8E2eAround = 1.8f;          // m: the waypoint round her front-left
constexpr float kS8E2eLookahead = 40.0f;      // m: the pursuit point ahead on the line
constexpr float kS8E2eSteerGainDeg = 20.0f;   // heading error for a full steer
constexpr float kS8E2eAlignDeg = 10.0f;       // turned onto the line: kick
constexpr float kS8E2eGallopSeconds = 5.0f;
// The shrine spawn -> door_shrine route round Mono's altar (shrine frame, x/z):
// a 0.25 m lattice BFS through the layered map, simplified to straight legs.
constexpr std::array<std::array<float, 2>, 3> kS8E2eShrineRoute{
    {{2.57f, 21.5f}, {2.57f, 24.5f}, {0.07f, 28.25f}}};
constexpr float kS8E2eGallopMin = 60.0f;      // m: run_start 16.5 m + 3.4 s x 14.7 m/s = 66.5
// --s9-selftest / --s9-e2e (Task 9, authored): the scripts' targets, tolerances
// and budgets; the points are road_points' (placements.json).
constexpr float kS9PartialDot = 0.80f;          // the camera ~37 deg off Valus
constexpr float kS9SettleSeconds = 2.0f;        // the partial's width, the lock's sparks
constexpr int kS9Sparks = 4;                    // 2 s at the 0.5 s cooldown
constexpr float kS9ShadeMaxSeconds = 0.120f;    // 1.0 -> 0 at 9.0/s is 0.111 s
constexpr float kS9RaiseTolerance = 1.5f / 60.0f;  // the 1.0 s raise, within a frame
constexpr float kS9SpawnTolerance = 0.05f;      // m: a door's arrival spawn
constexpr float kS9AgroMovedMax = 0.010f;       // m: Agro where she was left
constexpr float kS9WalkSeconds = 60.0f;         // a scripted walk's budget
constexpr float kS9StuckSeconds = 3.0f;         // no 0.5 m of progress: the walk is blocked
// The stairs climbed: on the corridor floor, 4 m short of door_corridor's box (the
// floor is 2.68 m at z 280 and rises past the 2.7 m gate by z 281).
constexpr float kS9EntranceZ = 282.0f;
constexpr int kS9PerfFrames = 240;
constexpr int kS9E2eMaxStageFrames = 12000;     // 200 s at 1/60 s per stage
constexpr float kS9E2eWaypoint = 12.0f;         // m: a ridden waypoint passed
constexpr float kS9E2eReinAt = 20.0f;           // m from kAgroStairs: rein (the stop slides ~14 m)

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

        RunState run{opts, shell, renderer, audio};
        // --s8-selftest starts on the plain (D17); --s8-e2e passes --stage shrine.
        // --s9-selftest starts on the plain too (the road).
        run.stage = (opts.s8Selftest || opts.s9Selftest) ? StageId::Plain : opts.stage;
        if (run.stage != StageId::Arena) {
            // --stage shrine|plain starts with control (D11): the return is over.
            run.carry.ret.phase = ReturnPhase::Free;
        }
        StageId& stage = run.stage;
        StageCarry& carry = run.carry;
        bool& sword_fx_ready = run.sword_fx_ready;
        std::optional<HorsePose>& s9AgroLeft = run.s9AgroLeft;
        for (;;) {
            StageCtx ctx(run);
            if (const std::optional<int> exitCode = stage_setup(run, ctx)) {
                return *exitCode;
            }
            // The stage's state under the names the paths below were written with.
            const StageId& here = ctx.here;
            const char* const stage_name = ctx.stage_name;
            Scene& scene = ctx.scene;
            ClubAttachment& club = ctx.club;
            CorpseAttachment& corpse = ctx.corpse;
            bool& effects_ready = ctx.effects_ready;
            SceneManifest& manifest = ctx.manifest;
            Heightmap& heightmap = ctx.heightmap;
            const bool& follow_mode = ctx.follow_mode;
            const RunFlags& flags = ctx.flags;
            const bool& death_wired = ctx.death_wired;
            const bool& road = ctx.road;
            bool& return_wired = ctx.return_wired;
            const RigCpu& player_rig_record = *ctx.playerRigRecord;
            Camera& camera = ctx.camera;
            Vec3& stage_spawn = ctx.stage_spawn;
            float& stage_spawn_yaw = ctx.stage_spawn_yaw;
            const DoorRow* const arrival = ctx.arrival;
            const bool& door_arrival = ctx.door_arrival;
            PlayerState& player = ctx.player;
            PlayerCombat& combat = ctx.combat;
            Encounter& encounter = ctx.encounter;
            const CombatTuning& tuning = ctx.tuning;
            CameraClimb& climbCamera = ctx.climbCamera;
            CameraFollow& follow = ctx.follow;
            CameraTarget& targetCamera = ctx.targetCamera;
            Vec3& entityChest = ctx.entityChest;
            bool& haveEntityChest = ctx.haveEntityChest;
            const std::size_t& playerRig = ctx.playerRig;
            const std::size_t& entityRig = ctx.entityRig;
            std::vector<Playback>& playbacks = ctx.playbacks;
            Playback& playback = *ctx.playbackPtr;
            const std::size_t& horseRig = ctx.horseRig;
            PlayerClips& playerClips = ctx.playerClips;
            EntityClips& entityClips = ctx.entityClips;
            ReturnTimings& returnTimings = ctx.returnTimings;
            float& wakeFirstSeconds = ctx.wakeFirstSeconds;
            const bool& manual_clip = ctx.manual_clip;
            const bool& valus_behavior = ctx.valus_behavior;
            const bool& s8_wired = ctx.s8_wired;
            bool& plain_ride = ctx.plain_ride;
            PlainRide& pr = ctx.pr;
            SwordState& sword = ctx.sword;
            bool& sword_wired = ctx.sword_wired;

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
                    renderer.set_hud(ctx.hud_state(hudVisible));
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
                renderer.set_hud(ctx.hud_state(/*visible=*/true));
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
                    renderer.set_hud(ctx.hud_state(/*visible=*/true));
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
                        renderer.set_hud(ctx.hud_state(/*visible=*/true));
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

            if (opts.s7Selftest) {
                const SelftestResult result = run_s7(ctx, run);
                if (result.exitCode) {
                    return *result.exitCode;
                }
                if (result.next) {
                    stage = *result.next;
                    continue;
                }
            }

            if (opts.s8Selftest) {
                // The S8 acceptance timeline (design §8.3): fixed 1/60 s steps,
                // no window input, the plain's own frame (plain_step, the follow
                // camera, player_step on foot) driven by scripted ride keys.
                // Placements between probes are scaffolding (as S7's wall probe);
                // every ride transition is the machine's own.
                const float dt = kFixedFrameSeconds;
                const auto fail = [](const std::string& message) {
                    std::fprintf(stderr, "sotc-viewer: --s8-selftest: %s\n", message.c_str());
                    return 1;
                };
                const auto ride_phase_name = [](RidePhase phase) {
                    return phase == RidePhase::OnFoot     ? "OnFoot"
                           : phase == RidePhase::Mounting ? "Mounting"
                           : phase == RidePhase::Riding   ? "Riding"
                                                          : "Dismounting";
                };
                if (here == StageId::Arena) {
                    // --- 12. R: the arena in its opening state ---------------------
                    const int valus = static_cast<int>(std::lround(encounter.health));
                    const int hero = static_cast<int>(std::lround(combat.health));
                    if (carry.s8Step != 1 || carry.door.phase != DoorPhase::Idle ||
                        pr.ride.phase != RidePhase::OnFoot || carry.ret.phase != ReturnPhase::Idle ||
                        valus != 100 || hero != 100) {
                        return fail("R did not bring the arena back in its opening state");
                    }
                    std::printf("s8-reset stage=arena ride=%s door=%s valus=%d player=%d\n",
                                ride_phase_name(pr.ride.phase), door_phase_name(carry.door.phase),
                                valus, hero);
                    print_status_line(opts, shell.win);
                    return 0;
                }
                if (here != StageId::Plain || carry.s8Step != 0) {
                    return fail("unexpected stage step " + std::to_string(carry.s8Step));
                }
                if (!plain_ride) {
                    return fail("the plain has no Agro to ride (the S8 bake or manifest)");
                }
                if (!audio.enabled) {
                    return fail("audio is not loaded (run `make audio` for the 34-entry slice)");
                }
                namespace rp = ride_points;
                std::vector<std::vector<Mat4>> palettes =
                    update_all_poses(renderer, scene, playbacks, 0.0f, club);
                // Agro's clip as shown, and every switch of it since the last clear.
                const auto horse_clip = [&]() -> const std::string& {
                    return pr.ride.phase == RidePhase::OnFoot ? pr.agro.clip : pr.ride.horseClip;
                };
                std::vector<std::string> seen;
                // One plain frame: the interactive loop's order (the clip choice,
                // the poses, plain_step, the camera, the player step on foot).
                const auto frame = [&](RideKeys keys, PlayerInput input = PlayerInput{}) {
                    const bool rideOwns = pr.ride.phase != RidePhase::OnFoot || pr.whistleClip >= 0;
                    if (rideOwns) {
                        input = PlayerInput{};
                    } else {
                        update_combat_clips(playback, scene, combat, input, /*manualClip=*/false);
                    }
                    palettes = update_all_poses(renderer, scene, playbacks, dt, club);
                    const std::string before = horse_clip();
                    ctx.plain_step(dt, keys, /*locked=*/false, palettes);
                    if (horse_clip() != before) {
                        seen.push_back(horse_clip());
                    }
                    const bool riding = pr.ride.phase != RidePhase::OnFoot;
                    const bool moving = input.forward || input.back || input.left || input.right;
                    camera_follow(camera, follow, riding ? pr.horsePos : player.position,
                                  riding ? pr.horseYaw : player.yawDeg, 0.0f, 0.0f, 0.0f,
                                  riding ? pr.ride.speed != 0.0f : moving, dt);
                    if (!riding) {
                        const PlayerFrame f = player_frame_of(scene, entityRig, palettes, heightmap,
                                                              camera_look_yaw(camera));
                        player_step(player, combat, input, f, tuning, dt);
                        apply_player_transform(scene, player_rig_record, player, ctx.player_lift());
                    }
                    audio.update(dt, player.position, camera_look_yaw(camera));
                };
                const auto frames_until = [&](float seconds, RideKeys keys, auto&& done) {
                    for (int i = static_cast<int>(std::lround(seconds / dt)); i > 0; --i) {
                        if (done()) {
                            return true;
                        }
                        frame(keys);
                    }
                    return done();
                };
                const auto hold = [&](float seconds, RideKeys keys) {
                    for (int i = static_cast<int>(std::lround(seconds / dt)); i > 0; --i) {
                        frame(keys);
                    }
                };
                const auto press = [&](RideKeys keys) {
                    frame(keys);
                    frame(RideKeys{});
                };
                RideKeys kick;
                kick.kick = true;
                RideKeys rein;
                rein.rein = true;
                RideKeys mount;
                mount.mount = true;
                RideKeys whistle;
                whistle.whistle = true;
                const auto on_floor = [&](float x, float z, float y) {
                    Vec3 p{x, y, z};
                    const float floor = heightmap_floor(heightmap, p, kFloorStep);
                    p.y = std::isfinite(floor) ? floor : y;
                    return p;
                };
                const auto snap_camera = [&]() {
                    follow.yawOffsetDeg = 0.0f;
                    follow.pitchOffsetDeg = 0.0f;
                    const bool riding = pr.ride.phase != RidePhase::OnFoot;
                    camera_follow(camera, follow, riding ? pr.horsePos : player.position,
                                  riding ? pr.horseYaw : player.yawDeg, 0.0f, 0.0f, 0.0f,
                                  /*moving=*/false, /*dtSeconds=*/10.0f);
                };
                const auto place_player = [&](Vec3 p, float yawDeg) {
                    player = PlayerState{p, yawDeg};
                    apply_player_transform(scene, player_rig_record, player, ctx.player_lift());
                    snap_camera();
                };
                // Agro moved by hand (scaffolding): her heading is the ride's too.
                const auto place_horse = [&](Vec3 p, float yawDeg) {
                    pr.horsePos = p;
                    pr.horseYaw = yawDeg;
                    pr.ride.yawDeg = yawDeg;
                    set_rig_model(scene, scene.rigs[horseRig], ctx.horse_model());
                    snap_camera();
                };
                const auto heading_to = [](float fromX, float fromZ, float toX, float toZ) {
                    return std::atan2(toX - fromX, toZ - fromZ) / kDegToRad;
                };
                const auto render = [&]() {
                    renderer.set_hud(ctx.hud_state(/*visible=*/true));
                    renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                                          /*acquire_swapchain=*/true);
                };
                const auto capture = [&](const char* name) {
                    if (opts.s8StillsPath.empty()) {
                        return;
                    }
                    with_clocks_restored(playbacks, [&]() {
                        render();
                        write_screenshot(renderer, shell.win,
                                         still_png_path(opts.s8StillsPath, name));
                    });
                };
                const auto bone_origin = [&](std::size_t rig, const char* bone, Vec3& out) {
                    Mat4 m{};
                    if (rig >= palettes.size() ||
                        !rig_bone_world(scene, rig, palettes[rig], bone, m)) {
                        return false;
                    }
                    out = Vec3{m.m[12], m.m[13], m.m[14]};
                    return true;
                };
                const auto short_clip = [](const std::string& clip) {
                    const std::string prefix = "horse_a_uma_";
                    return clip.rfind(prefix, 0) == 0 ? clip.substr(prefix.size()) : clip;
                };
                const auto horse_named = [](const char* name) {
                    return std::string("horse_a_uma_") + name;
                };
                frame(RideKeys{});  // the first plain frame: Agro posed, the blend seeded

                // --- 1. The whistle from probes.whistle_from ----------------------
                place_player(on_floor(rp::kProbeWhistleFrom[0], rp::kProbeWhistleFrom[1],
                                      player.position.y),
                             heading_to(rp::kProbeWhistleFrom[0], rp::kProbeWhistleFrom[1],
                                        pr.horsePos.x, pr.horsePos.z));
                frame(RideKeys{});
                const float whistleDistance = ctx.horse_distance();
                press(whistle);
                if (pr.whistleClip < 0 || pr.agro.phase != AgroPhase::Approach) {
                    return fail("the whistle did not call Agro");
                }
                bool neighed = false;
                bool whistleStill = false;
                if (!frames_until(kS8MaxWhistleSeconds, RideKeys{}, [&]() {
                        neighed = neighed || pr.agro.phase == AgroPhase::Neigh;
                        if (!whistleStill && pr.agro.phase == AgroPhase::Approach &&
                            ctx.horse_distance() <= 0.5f * whistleDistance) {
                            capture("whistle");
                            whistleStill = true;
                        }
                        return neighed && pr.agro.phase == AgroPhase::Idle;
                    })) {
                    return fail("Agro did not arrive and neigh");
                }
                const float arrived = ctx.horse_distance();
                std::printf("s8-whistle dist=%.2f arrived=%.2f neigh=%d\n", whistleDistance,
                            arrived, neighed ? 1 : 0);
                if (!(arrived <= kS8ArriveMax)) {
                    return fail("Agro stopped farther than 3.5 m");
                }

                // --- 2. The mount, from her left (x < 0 in her frame) -------------
                frames_until(3.0f, RideKeys{}, [&]() { return pr.whistleClip < 0; });
                const auto beside = [&](float x) {
                    const Vec3 p = from_body_frame(Vec3{x, 0.0f, 0.0f}, pr.horsePos, pr.horseYaw);
                    return on_floor(p.x, p.z, pr.horsePos.y);
                };
                place_player(beside(-kS8MountSide), pr.horseYaw);
                frame(mount);
                if (pr.ride.phase != RidePhase::Mounting || pr.ride.side != Side::Left) {
                    return fail("E beside her left did not start the left mount");
                }
                std::printf("s8-mount side=L clip=%s\n", pr.ride.riderClip.c_str());
                bool mountStill = false;
                if (!frames_until(3.0f, RideKeys{}, [&]() {
                        if (!mountStill && pr.ride.phase == RidePhase::Mounting &&
                            pr.ride.clipTime >= kS8MountStillSeconds) {
                            capture("mount");
                            mountStill = true;
                        }
                        return pr.ride.phase == RidePhase::Riding;
                    })) {
                    return fail("the mount never reached Riding");
                }

                // The rider's seated rest (stand1_player at Stand): the pelvis over
                // OptBone_boy in Agro's frame. The clip seats the pelvis joint
                // 0.18 m above the bone (y is the clip's own); s8-rider is the
                // pelvis's largest departure from this rest during the gallop —
                // a float, a sink or a slide on the saddle.
                const auto pelvis_on_seat = [&](Vec3& out) {
                    Vec3 pelvis{};
                    Vec3 seat{};
                    if (!bone_origin(playerRig, "Bone_KOSI", pelvis) ||
                        !bone_origin(horseRig, kRiderBone, seat)) {
                        return false;
                    }
                    out = to_body_frame(pelvis, seat, pr.horseYaw);
                    return true;
                };
                // Riding is reached as the Mount -> stand1 blend starts: sample the
                // rest once the blend has finished, not two frames into it.
                hold(kClipBlendSeconds, RideKeys{});
                Vec3 seatRest{};
                if (!pelvis_on_seat(seatRest)) {
                    return fail("no Bone_KOSI on the rider or OptBone_boy on Agro");
                }

                // --- 3. Kicks: Walk, Trot, Gallop through their start clips -------
                float riderOffset = 0.0f;
                struct GaitProbe {
                    Gait gait;
                    const char* start;
                    const char* loop;
                    float expected;
                };
                for (const GaitProbe& g :
                     {GaitProbe{Gait::Walk, "walk_start", "walk", rp::kSpeedWalk},
                      GaitProbe{Gait::Trot, "run_n_start", "run_n", rp::kSpeedTrot},
                      GaitProbe{Gait::Gallop, "run_start", "run", rp::kSpeedGallop}}) {
                    seen.clear();
                    press(kick);
                    if (!frames_until(4.0f, RideKeys{},
                                      [&]() { return horse_clip() == horse_named(g.loop); }) ||
                        seen.size() != 2 || seen[0] != horse_named(g.start) ||
                        pr.ride.gait != g.gait) {
                        return fail(std::string("the kick to ") + gait_name(g.gait) +
                                    " did not run " + g.start + " -> " + g.loop);
                    }
                    float path = 0.0f;
                    const int n = static_cast<int>(std::lround(kS8GaitSeconds / dt));
                    for (int i = 0; i < n; ++i) {
                        const Vec3 before = pr.horsePos;
                        frame(RideKeys{});
                        path += std::hypot(pr.horsePos.x - before.x, pr.horsePos.z - before.z);
                        Vec3 onSeat{};
                        if (g.gait == Gait::Gallop && pelvis_on_seat(onSeat)) {
                            const Vec3 d{onSeat.x - seatRest.x, onSeat.y - seatRest.y,
                                         onSeat.z - seatRest.z};
                            riderOffset = std::max(riderOffset, std::sqrt(vec3_dot(d, d)));
                        }
                        if (g.gait == Gait::Gallop && i == n / 2) {
                            capture("gallop");
                        }
                    }
                    const float speed = path / kS8GaitSeconds;
                    std::printf("s8-gait gait=%s speed=%.3f expected=%.3f\n", gait_name(g.gait),
                                speed, g.expected);
                    if (!(std::fabs(speed - g.expected) <= kS8Tolerance * g.expected)) {
                        return fail(std::string(gait_name(g.gait)) + "'s loop speed is off");
                    }
                }

                // --- 4. Full right steer at the gallop: the yaw rate ---------------
                {
                    RideKeys right;
                    right.steer = 1.0f;
                    hold(kS8SteerRampSeconds, right);  // the 0.5-per-frame blend settles
                    float turned = 0.0f;
                    const int n = static_cast<int>(std::lround(kS8SteerSeconds / dt));
                    for (int i = 0; i < n; ++i) {
                        const float before = pr.horseYaw;
                        frame(right);
                        turned += std::remainder(pr.horseYaw - before, 360.0f);
                    }
                    const float rate = turned / kS8SteerSeconds;
                    const float expected = -kYawRateGallop;  // right turns yaw down
                    std::printf("s8-steer yaw_rate=%.2f expected=%.2f\n", rate, expected);
                    if (pr.ride.gait != Gait::Gallop ||
                        !(std::fabs(rate - expected) <= kS8Tolerance * std::fabs(expected))) {
                        return fail("the gallop's yaw rate is off");
                    }
                    hold(kS8SteerRampSeconds, RideKeys{});
                }

                // --- 5. Hold S: the gallop stops through run_stop1-3 ---------------
                seen.clear();
                hold(kReinHoldSeconds + 0.1f, rein);
                if (!frames_until(5.0f, RideKeys{},
                                  [&]() { return horse_clip() == horse_named("stand1"); })) {
                    return fail("the held S never stopped the gallop");
                }
                {
                    const std::vector<std::string> want{
                        horse_named("run_stop1"), horse_named("run_stop2"),
                        horse_named("run_stop3"), horse_named("stand1")};
                    if (seen != want) {
                        return fail("the stop did not run run_stop1 -> 2 -> 3 -> stand1");
                    }
                    std::printf("s8-stop from=Gallop clips=%s,%s,%s\n", short_clip(seen[0]).c_str(),
                                short_clip(seen[1]).c_str(), short_clip(seen[2]).c_str());
                }

                // --- 6. S at a standstill: back up ---------------------------------
                if (!frames_until(2.0f, rein,
                                  [&]() { return horse_clip() == horse_named("back"); })) {
                    return fail("S at a standstill did not back up");
                }
                {
                    const Vec3 from = pr.horsePos;
                    const Vec3 f = yaw_forward(pr.horseYaw);
                    hold(kS8BackSeconds, rein);
                    const float moved = (pr.horsePos.x - from.x) * f.x + (pr.horsePos.z - from.z) * f.z;
                    const float speed = moved / kS8BackSeconds;
                    std::printf("s8-back speed=%.3f\n", speed);
                    if (!(std::fabs(speed - rp::kSpeedBack) <=
                          kS8Tolerance * std::fabs(rp::kSpeedBack))) {
                        return fail("the back-up speed is off");
                    }
                }
                if (!frames_until(3.0f, RideKeys{},
                                  [&]() { return horse_clip() == horse_named("stand1"); })) {
                    return fail("releasing S did not end the back-up");
                }

                // --- 7. The plateau's edge, ridden: R11 stops her ------------------
                const float edgeDx = rp::kProbeEdgeTo[0] - rp::kProbeEdgeFrom[0];
                const float edgeDz = rp::kProbeEdgeTo[1] - rp::kProbeEdgeFrom[1];
                const float edgeLength = std::hypot(edgeDx, edgeDz);
                const auto along_edge = [&](Vec3 p) {
                    return ((p.x - rp::kProbeEdgeFrom[0]) * edgeDx +
                            (p.z - rp::kProbeEdgeFrom[1]) * edgeDz) /
                           edgeLength;
                };
                const float edgeYaw = heading_to(rp::kProbeEdgeFrom[0], rp::kProbeEdgeFrom[1],
                                                 rp::kProbeEdgeTo[0], rp::kProbeEdgeTo[1]);
                place_horse(on_floor(rp::kProbeEdgeFrom[0], rp::kProbeEdgeFrom[1], pr.horsePos.y),
                            edgeYaw);
                frame(RideKeys{});
                seen.clear();
                press(kick);
                hold(kS8EdgeKickGapSeconds, RideKeys{});
                press(kick);  // to the trot
                bool stopClip = false;
                if (!frames_until(kS8MaxEdgeSeconds, RideKeys{}, [&]() {
                        stopClip = stopClip || horse_clip() == horse_named("run_n_stop") ||
                                   horse_clip() == horse_named("walk_stop");
                        return stopClip && horse_clip() == horse_named("stand1");
                    })) {
                    return fail("riding into the edge did not stop Agro (R11)");
                }
                {
                    std::string clips;
                    for (const std::string& clip : seen) {
                        clips += (clips.empty() ? "" : ",") + short_clip(clip);
                    }
                    const bool blocked = along_edge(pr.horsePos) < edgeLength - 0.5f;
                    std::printf("s8-edge-ride stopped=%.2f,%.2f clips=%s blocked=%d\n", pr.horsePos.x,
                                pr.horsePos.z, clips.c_str(), blocked ? 1 : 0);
                    if (!blocked) {
                        return fail("the ride toward the edge was never refused");
                    }
                }

                // --- 8. Dismount right: the cliff on her left ----------------------
                // Turned in place so her left (x < 0) faces over the edge: the
                // mounted side has no ground, the machine picks the right.
                place_horse(pr.horsePos, std::atan2(edgeDz, -edgeDx) / kDegToRad);
                frame(RideKeys{});
                frame(mount);
                if (pr.ride.phase != RidePhase::Dismounting || pr.ride.side != Side::Right) {
                    return fail("E at the edge did not dismount on the right");
                }
                std::printf("s8-dismount side=R\n");
                if (!frames_until(3.0f, RideKeys{},
                                  [&]() { return pr.ride.phase == RidePhase::OnFoot; })) {
                    return fail("the dismount never ended on foot");
                }
                frame(RideKeys{});

                // --- 9. The edge on foot: player_step refuses the drop -------------
                {
                    place_player(on_floor(rp::kProbeEdgeFrom[0], rp::kProbeEdgeFrom[1],
                                          player.position.y),
                                 edgeYaw);
                    PlayerInput forward;
                    forward.forward = true;
                    forward.run = true;
                    const int n = static_cast<int>(std::lround(kS8MaxEdgeSeconds / dt));
                    for (int i = 0; i < n && along_edge(player.position) < edgeLength; ++i) {
                        palettes = update_all_poses(renderer, scene, playbacks, dt, club);
                        const PlayerFrame f =
                            player_frame_of(scene, entityRig, palettes, heightmap, edgeYaw);
                        player_step(player, combat, forward, f, tuning, dt);
                    }
                    const bool blocked = along_edge(player.position) < edgeLength - 0.5f;
                    std::printf("s8-edge stopped=%.2f,%.2f blocked=%d\n", player.position.x,
                                player.position.z, blocked ? 1 : 0);
                    if (!blocked) {
                        return fail("the walk toward the edge was never stopped");
                    }
                }
                std::printf("s8-rider offset=%.3f\n", riderOffset);
                if (!(riderOffset <= kS8RiderOffsetMax)) {
                    return fail("the rider's pelvis is off the saddle");
                }

                // --- 10. Back at her start: the audio, plain.png, the perf ---------
                place_horse(on_floor(rp::kProbeGallopFrom[0], rp::kProbeGallopFrom[1], pr.horsePos.y),
                            rp::kProbeGallopHeadingDeg);
                place_player(stage_spawn, 180.0f);  // facing the shrine
                hold(0.5f, RideKeys{});
                std::printf("s8-audio events=%d hooves=%d\n", audio.rideEvents, audio.hooves);
                if (audio.hooves <= 0 || audio.rideEvents <= audio.hooves) {
                    return fail("the ride played no hooves or no other cue");
                }
                {
                    const Camera followCamera = camera;
                    camera.target =
                        Vec3{stage_spawn.x, stage_spawn.y + 6.0f, stage_spawn.z - 15.0f};
                    camera.distance = 45.0f;
                    camera.yaw = 0.0f;  // the eye on +Z, looking at the shrine (-Z)
                    camera.pitch = 12.0f;
                    capture("plain");
                    camera = followCamera;
                }
                render();  // warm-up
                {
                    const Uint64 perfStart = SDL_GetTicksNS();
                    for (int i = 0; i < kS8PerfFrames; ++i) {
                        frame(RideKeys{});
                        render();
                    }
                    const double perfMs = static_cast<double>(SDL_GetTicksNS() - perfStart) / 1e6;
                    std::printf("s8-perf frames=%d frame_ms=%.3f drawn=%d culled=%d\n",
                                kS8PerfFrames, perfMs / static_cast<double>(kS8PerfFrames),
                                renderer.drawn_last_frame(), renderer.culled_last_frame());
                }

                // --- 11. Mounted from the right, at a gallop: R --------------------
                place_player(beside(kS8MountSide), pr.horseYaw);
                frame(mount);
                if (pr.ride.phase != RidePhase::Mounting || pr.ride.side != Side::Right ||
                    !frames_until(3.0f, RideKeys{},
                                  [&]() { return pr.ride.phase == RidePhase::Riding; })) {
                    return fail("the right mount did not reach Riding");
                }
                for (int k = 0; k < 3; ++k) {
                    press(kick);
                    hold(kS8KickGapSeconds, RideKeys{});
                }
                if (pr.ride.gait != Gait::Gallop || pr.ride.speed <= 0.0f) {
                    return fail("three kicks did not reach the gallop");
                }
                DeathState death = reset_death();
                bool deathCueFired = false;
                if (!ctx.press_reset(death, deathCueFired, nullptr)) {
                    return fail("R on the plain did not ask for the arena");
                }
                carry.s8Step = 1;
                renderer.release_scene();
                stage = StageId::Arena;
                continue;
            }

            if (opts.s9Selftest) {
                // The S9 acceptance timeline (Task 9): fixed 1/60 s steps, no window
                // input, the interactive loop's frame order (the clip choice, the
                // poses, plain_step, the camera, the player step, sword_frame, the
                // doors) driven by scripted keys, a held C and a scripted look.
                // Placements between probes are scaffolding (as S8's); the raise,
                // the light, the lock, the doors and Agro's carried pose are the
                // production machines'. One stage per iteration (carry.s9Step).
                const float dt = kFixedFrameSeconds;
                const auto fail = [](const std::string& message) {
                    std::fprintf(stderr, "sotc-viewer: --s9-selftest: %s\n", message.c_str());
                    return 1;
                };
                namespace rd = road_points;
                if (here == StageId::Arena && carry.s9Step == 3) {
                    // --- 10. R: the arena in its opening state ---------------------
                    const int valus = static_cast<int>(std::lround(encounter.health));
                    const int hero = static_cast<int>(std::lround(combat.health));
                    if (carry.door.phase != DoorPhase::Idle || pr.ride.phase != RidePhase::OnFoot ||
                        sword.phase != SwordPhase::Lowered || carry.horse.has_value() ||
                        carry.ret.phase != ReturnPhase::Idle || valus != 100 || hero != 100) {
                        return fail("R did not bring the arena back in its opening state");
                    }
                    std::printf("s9-reset stage=arena ride=OnFoot door=%s sword=%s horse=none "
                                "valus=%d player=%d\n",
                                door_phase_name(carry.door.phase), sword_phase_name(sword.phase),
                                valus, hero);
                    print_status_line(opts, shell.win);
                    return 0;
                }
                const bool firstRoad = here == StageId::Plain && carry.s9Step == 0;
                const bool inArena = here == StageId::Arena && carry.s9Step == 1;
                const bool backOnRoad = here == StageId::Plain && carry.s9Step == 2;
                if (!firstRoad && !inArena && !backOnRoad) {
                    return fail(std::string("the ") + stage_name + " loaded out of order (step " +
                                std::to_string(carry.s9Step) + ")");
                }
                if (here == StageId::Plain) {
                    if (!road || !plain_ride || !sword_wired || !sword_fx_ready) {
                        return fail("the road has no Agro, raise clips or sword light (the S9 bake "
                                    "into the view, `make road-manifest`)");
                    }
                    if (!audio.enabled || !audio.engine.has(sword_cue(SwordSfx::Shimmer).clip) ||
                        !audio.engine.has(sword_cue(SwordSfx::Search).clip)) {
                        return fail("audio is not loaded (run `make audio` for the 36-entry slice)");
                    }
                }
                std::vector<std::vector<Mat4>> palettes =
                    update_all_poses(renderer, scene, playbacks, 0.0f, club);
                bool held = false;  // C
                // The scripted look: NaN = the follow camera; otherwise the camera
                // looks along the unit vector whose dot with the tip->chest
                // direction is aimDot (turned sideways in the horizontal).
                float aimDot = std::numeric_limits<float>::quiet_NaN();
                SwordProbe probe;
                SwordFxFrame fx;
                bool doorLoad = false;
                const Vec3 chest = valus_chest();
                const auto aim_with_dot = [&](float d) {
                    const Vec3 c = vec3_normalize(Vec3{chest.x - probe.tip.x, chest.y - probe.tip.y,
                                                       chest.z - probe.tip.z});
                    const Vec3 side = vec3_normalize(Vec3{c.z, 0.0f, -c.x});
                    const float s = std::sqrt(std::max(0.0f, 1.0f - d * d));
                    return Vec3{c.x * d + side.x * s, c.y * d, c.z * d + side.z * s};
                };
                // One frame in the interactive loop's order; basisYaw (NaN = the
                // camera's) is the heading a scripted walk moves along.
                const auto frame = [&](RideKeys keys = RideKeys{}, PlayerInput input = PlayerInput{},
                                       float basisYaw = std::numeric_limits<float>::quiet_NaN()) {
                    const bool locked = carry.door.inputLocked;
                    const bool rideOwns =
                        plain_ride && (pr.ride.phase != RidePhase::OnFoot || pr.whistleClip >= 0);
                    const bool swordFoot = sword_wired && sword_locks_movement(sword);
                    if (locked || rideOwns || swordFoot) {
                        input = PlayerInput{};
                    }
                    if (!rideOwns) {
                        if (swordFoot) {
                            playback.index = clip_index_named(playback, sword.clip);
                            playback.time = sword.clipTime;
                            playback.pinned = true;
                            playback.playing = false;
                        } else {
                            update_combat_clips(playback, scene, combat, input, /*manualClip=*/false);
                        }
                    }
                    palettes = update_all_poses(renderer, scene, playbacks, dt, club);
                    if (plain_ride) {
                        ctx.plain_step(dt, keys, locked, palettes);
                    }
                    const bool riding = plain_ride && pr.ride.phase != RidePhase::OnFoot;
                    const bool moving = input.forward || input.back || input.left || input.right;
                    camera_follow(camera, follow, riding ? pr.horsePos : player.position,
                                  riding ? pr.horseYaw : player.yawDeg, 0.0f, 0.0f, 0.0f,
                                  riding ? pr.ride.speed != 0.0f : moving, dt);
                    if (!std::isnan(aimDot)) {
                        camera_look_along(camera, aim_with_dot(aimDot));
                    }
                    const Vec3 before = player.position;
                    if (!riding) {
                        const PlayerFrame f = player_frame_of(
                            scene, entityRig, palettes, heightmap,
                            std::isnan(basisYaw) ? camera_look_yaw(camera) : basisYaw);
                        player_step(player, combat, input, f, tuning, dt);
                        apply_player_transform(scene, player_rig_record, player, ctx.player_lift());
                    }
                    fx = SwordFxFrame{};
                    if (sword_wired) {
                        probe = ctx.sword_frame(dt, held, locked, palettes, fx);
                    }
                    // The doors: the production step (the horse store included);
                    // after the Load the stage is over (the script switches it).
                    if (!doorLoad && ctx.door_frame(dt, before, riding, locked, /*live=*/true)) {
                        doorLoad = true;
                    }
                    audio.update(dt, player.position, camera_look_yaw(camera));
                };
                const auto frames_until = [&](float seconds, auto&& done, RideKeys keys = RideKeys{}) {
                    for (int i = static_cast<int>(std::lround(seconds / dt)); i > 0; --i) {
                        if (done()) {
                            return true;
                        }
                        frame(keys);
                    }
                    return done();
                };
                const auto hold = [&](float seconds, RideKeys keys = RideKeys{}) {
                    for (int i = static_cast<int>(std::lround(seconds / dt)); i > 0; --i) {
                        frame(keys);
                    }
                };
                const auto on_floor = [&](float x, float z, float y) {
                    Vec3 p{x, y, z};
                    const float floor = heightmap_floor(heightmap, p, kFloorStep);
                    p.y = std::isfinite(floor) ? floor : y;
                    return p;
                };
                const auto snap_camera = [&]() {
                    follow.yawOffsetDeg = 0.0f;
                    follow.pitchOffsetDeg = 0.0f;
                    const bool riding = plain_ride && pr.ride.phase != RidePhase::OnFoot;
                    camera_follow(camera, follow, riding ? pr.horsePos : player.position,
                                  riding ? pr.horseYaw : player.yawDeg, 0.0f, 0.0f, 0.0f,
                                  /*moving=*/false, /*dtSeconds=*/10.0f);
                };
                const auto place_player = [&](Vec3 p, float yawDeg) {
                    player = PlayerState{p, yawDeg};
                    apply_player_transform(scene, player_rig_record, player, ctx.player_lift());
                    snap_camera();
                };
                const auto place_horse = [&](Vec3 p, float yawDeg) {
                    pr.horsePos = p;
                    pr.horseYaw = yawDeg;
                    pr.ride.yawDeg = yawDeg;
                    set_rig_model(scene, scene.rigs[horseRig], ctx.horse_model());
                    snap_camera();
                };
                const auto render = [&]() {
                    renderer.set_hud(ctx.hud_state(/*visible=*/true));
                    renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                                          /*acquire_swapchain=*/true, nullptr,
                                          fx.active ? &fx : nullptr);
                };
                const auto capture = [&](const char* name) {
                    if (opts.s9StillsPath.empty()) {
                        return;
                    }
                    with_clocks_restored(playbacks, [&]() {
                        render();
                        write_screenshot(renderer, shell.win, still_png_path(opts.s9StillsPath, name));
                    });
                };
                // A scripted walk (run) toward x/z until done(); a walk that stops
                // making progress outside a door fade is a failed join.
                std::string stuckAt;
                const auto walk_until = [&](float x, float z, auto&& done) {
                    Vec3 from = player.position;
                    float still = 0.0f;
                    for (int i = static_cast<int>(std::lround(kS9WalkSeconds / dt)); i > 0; --i) {
                        if (done()) {
                            return true;
                        }
                        PlayerInput in;
                        in.forward = true;
                        in.run = true;
                        frame(RideKeys{}, in,
                              std::atan2(x - player.position.x, z - player.position.z) / kDegToRad);
                        still += dt;
                        if (std::hypot(player.position.x - from.x, player.position.z - from.z) > 0.5f) {
                            from = player.position;
                            still = 0.0f;
                        }
                        if (still > kS9StuckSeconds && !carry.door.inputLocked) {
                            char where[96];
                            std::snprintf(where, sizeof(where), "%.1f,%.1f,%.1f", player.position.x,
                                          player.position.y, player.position.z);
                            stuckAt = where;
                            return false;
                        }
                    }
                    return done();
                };
                // An arrival: the row's from/to, the Load completed, the spawn.
                const auto arrive = [&](StageId from, StageId to) {
                    if (!door_arrival || kDoors[carry.door.row].from != from ||
                        kDoors[carry.door.row].to != to) {
                        return false;
                    }
                    carry.door = step_door(carry.door, 0.0f, false, -1, /*loadDone=*/true);
                    return true;
                };
                const auto door_settled = [&]() {
                    return carry.door.phase == DoorPhase::Idle && !carry.door.inputLocked;
                };
                const auto near = [](Vec3 a, const float* b, float tolerance) {
                    return std::fabs(a.x - b[0]) <= tolerance && std::fabs(a.y - b[1]) <= tolerance &&
                           std::fabs(a.z - b[2]) <= tolerance;
                };
                const char* const shimmerClip = sword_cue(SwordSfx::Shimmer).clip;

                if (inArena) {
                    // --- 8. Through the corridor door: the arena -------------------
                    if (!arrive(StageId::Plain, StageId::Arena)) {
                        return fail("the arena did not load through the corridor door");
                    }
                    const ride_points::Door& d = rd::kDoorCorridor;
                    std::printf("s9-door from=%s to=%s spawn=%.2f,%.2f,%.2f yaw=%.2f ret=%s\n",
                                stage_label(kDoors[carry.door.row].from),
                                stage_label(kDoors[carry.door.row].to), player.position.x,
                                player.position.y, player.position.z, player.yawDeg,
                                return_phase_name(carry.ret.phase));
                    if (!near(player.position, d.arrive, kS9SpawnTolerance) ||
                        std::fabs(player.yawDeg - d.arriveYawDeg) > 0.01f ||
                        carry.ret.phase != ReturnPhase::Idle) {
                        return fail("the arena's arrival is off (spawn, yaw or the return machine)");
                    }
                    if (!frames_until(3.0f, door_settled)) {
                        return fail("the arena's door fade never ended");
                    }
                    // --- 9. Into kDoorArena: back to the road ----------------------
                    const ride_points::Door& back = rd::kDoorArena;
                    if (!walk_until(0.5f * (back.min[0] + back.max[0]),
                                    0.5f * (back.min[2] + back.max[2]),
                                    [&]() { return doorLoad; })) {
                        return fail("the walk to door_arena never loaded the road" +
                                    (stuckAt.empty() ? std::string() : " (blocked at " + stuckAt + ")"));
                    }
                    carry.s9Step = 2;
                    renderer.release_scene();
                    stage = kDoors[carry.door.row].to;
                    continue;
                }

                if (backOnRoad) {
                    // --- 9. The road again: the arrival, Agro where she was left ---
                    if (!arrive(StageId::Arena, StageId::Plain)) {
                        return fail("the road did not load through the arena door");
                    }
                    if (!carry.horse) {
                        return fail("no carried pose for Agro");
                    }
                    if (!s9AgroLeft) {
                        return fail("the script never left Agro");
                    }
                    const auto distance = [](Vec3 a, Vec3 b) {
                        const Vec3 d{a.x - b.x, a.y - b.y, a.z - b.z};
                        return std::sqrt(vec3_dot(d, d));
                    };
                    // agro_moved: the reload against where the script left her; the
                    // production store (door_frame's carry.horse) is checked too.
                    const float moved = distance(pr.horsePos, s9AgroLeft->pos);
                    const float movedCarry = distance(pr.horsePos, carry.horse->pos);
                    std::printf("s9-door from=%s to=%s spawn=%.2f,%.2f,%.2f agro_moved=%.3f\n",
                                stage_label(kDoors[carry.door.row].from),
                                stage_label(kDoors[carry.door.row].to), player.position.x,
                                player.position.y, player.position.z, moved);
                    if (!near(player.position, rd::kDoorArena.arrive, kS9SpawnTolerance) ||
                        !(moved <= kS9AgroMovedMax) || !(movedCarry <= kS9AgroMovedMax) ||
                        !(std::fabs(std::remainder(pr.horseYaw - s9AgroLeft->yawDeg, 360.0f)) <=
                          0.01f)) {
                        return fail("the road's arrival is off (spawn or Agro's pose)");
                    }
                    if (!frames_until(3.0f, door_settled)) {
                        return fail("the road's door fade never ended");
                    }
                    capture("arena_door");
                    const int shimmers = audio.swordPlays[static_cast<std::size_t>(SwordSfx::Shimmer)];
                    const int searches = audio.swordPlays[static_cast<std::size_t>(SwordSfx::Search)];
                    std::printf("s9-audio shimmer_started=%d shimmer_stopped=%d search=%d\n",
                                shimmers > 0 ? 1 : 0, audio.shimmerStops > 0 ? 1 : 0, searches);
                    if (shimmers <= 0 || audio.shimmerStops <= 0 || searches <= 0) {
                        return fail("the shimmer did not start and stop, or no search cue played");
                    }
                    render();  // warm-up
                    {
                        const Uint64 perfStart = SDL_GetTicksNS();
                        for (int i = 0; i < kS9PerfFrames; ++i) {
                            frame();
                            render();
                        }
                        const double perfMs = static_cast<double>(SDL_GetTicksNS() - perfStart) / 1e6;
                        std::printf("s9-perf frames=%d frame_ms=%.3f drawn=%d culled=%d\n",
                                    kS9PerfFrames, perfMs / static_cast<double>(kS9PerfFrames),
                                    renderer.drawn_last_frame(), renderer.culled_last_frame());
                    }
                    // --- 10. R while raised, Agro's pose carried --------------------
                    held = true;
                    if (!frames_until(2.0f, [&]() { return sword.phase == SwordPhase::Raised; })) {
                        return fail("the sword did not rise before R");
                    }
                    DeathState death = reset_death();
                    bool deathCueFired = false;
                    if (!ctx.press_reset(death, deathCueFired, nullptr)) {
                        return fail("R on the road did not ask for the arena");
                    }
                    if (audio.engine.is_playing(shimmerClip)) {
                        return fail("R left the shimmer playing");
                    }
                    carry.s9Step = 3;
                    renderer.release_scene();
                    stage = StageId::Arena;
                    continue;
                }

                // --- 1. kProbeSunlit, facing away: the raise -----------------------
                frame();  // the first road frame: the poses, the camera, the probe seeded
                std::printf("s9-sun dir=%.6f,%.6f,%.6f\n", kSunDir.x, kSunDir.y, kSunDir.z);
                place_player(on_floor(rd::kProbeSunlit[0], rd::kProbeSunlit[2], rd::kProbeSunlit[1]),
                             rd::kProbeSunlitYawAwayDeg);
                frame();
                aimDot = -1.0f;  // the look exactly away from the chest
                hold(kS9SettleSeconds);  // the width the spawn's view left eases out
                held = true;
                std::string firstClip;
                int raiseFrames = 0;
                while (sword.phase != SwordPhase::Raised && raiseFrames < 120) {
                    frame();
                    ++raiseFrames;
                    if (firstClip.empty() && sword.clip != nullptr) {
                        firstClip = sword.clip;
                    }
                }
                const float raiseSeconds = static_cast<float>(raiseFrames) * dt;
                const bool shimmerLive = audio.engine.is_playing(shimmerClip);
                std::printf("s9-raise clip=%s then=%s raise=%.3f\n", firstClip.c_str(),
                            sword.clip != nullptr ? sword.clip : "none", sword.raise);
                if (sword.phase != SwordPhase::Raised || firstClip != kSwordFoot.start ||
                    std::string(sword.clip) != kSwordFoot.loop ||
                    std::fabs(raiseSeconds - kSwordFoot.startS) > kS9RaiseTolerance || !shimmerLive) {
                    return fail("the raise did not reach Raised in 1.0 s through its clips (or no "
                                "shimmer)");
                }
                std::printf("s9-fan dot=%.3f k=%.3f lean_deg=%.2f width=%.3f\n", probe.beam.dot,
                            probe.beam.k, probe.beam.leanDeg, sword.width);
                if (!(probe.beam.k >= 0.95f) || !(sword.width < 0.0005f)) {
                    return fail("facing away: the fan is not wide open and thin");
                }

                // --- 2. The camera ~37 deg off Valus: the partial fan --------------
                aimDot = kS9PartialDot;
                hold(kS9SettleSeconds);
                std::printf("s9-partial dot=%.3f k=%.3f lean_deg=%.2f width=%.3f\n", probe.beam.dot,
                            probe.beam.k, probe.beam.leanDeg, sword.width);
                if (!(std::fabs(probe.beam.dot - 0.80f) <= 0.01f) ||
                    !(std::fabs(probe.beam.k - 0.100f) <= 0.005f) ||
                    !(std::fabs(probe.beam.leanDeg - 7.50f) <= 0.40f) ||
                    !(std::fabs(sword.width - 0.675f) <= 0.01f)) {
                    return fail("the partial fan's curves are off");
                }
                capture("fan");

                // --- 3. The camera onto Valus's chest: the lock, the sparks --------
                aimDot = 1.0f;
                const int sparksBefore = sword.sparks;
                if (!frames_until(1.0f, [&]() {
                        return probe.beam.locked && std::isfinite(probe.hit);
                    })) {
                    return fail("the camera on Valus's chest did not lock a lit hit");
                }
                std::printf("s9-lock k=%.4f locked=%d ray_m=%.1f hit=%d hit_m=%.1f\n", probe.beam.k,
                            probe.beam.locked ? 1 : 0, probe.beam.rayLength,
                            std::isfinite(probe.hit) ? 1 : 0, probe.hit);
                // B6: the chest is beyond the 150 m ray; the hit is the entrance's bank.
                if (!(probe.beam.k < kLockK) || !(probe.hit <= kBeamRayPerRaise)) {
                    return fail("the lock's hit is off");
                }
                hold(kS9SettleSeconds - dt);  // the lock frame was the first of the 2 s
                std::printf("s9-spark count=%d seconds=%.1f\n", sword.sparks - sparksBefore,
                            kS9SettleSeconds);
                if (sword.sparks - sparksBefore != kS9Sparks) {
                    return fail("the locked hit did not spark every 0.5 s");
                }
                capture("locked");

                // --- 4. Into the corridor's shade: the light goes out --------------
                aimDot = std::numeric_limits<float>::quiet_NaN();
                const float lightFrom = sword.light;
                place_player(on_floor(rd::kProbeShade[0], rd::kProbeShade[2], rd::kProbeShade[1]),
                             player.yawDeg);
                int shadeFrames = 0;
                while (sword.light > 0.0f && shadeFrames < 60) {
                    frame();
                    ++shadeFrames;
                }
                const float shadeSeconds = static_cast<float>(shadeFrames) * dt;
                std::printf("s9-shade light_from=%.3f light=%.3f seconds=%.3f\n", lightFrom,
                            sword.light, shadeSeconds);
                if (!(lightFrom >= 0.999f) || sword.light != 0.0f ||
                    !(shadeSeconds <= kS9ShadeMaxSeconds + kS7Eps)) {
                    return fail("the shade did not put the light out within 0.120 s");
                }
                held = false;
                if (!frames_until(2.0f, [&]() { return sword.phase == SwordPhase::Lowered; })) {
                    return fail("releasing C did not lower the sword");
                }
                if (audio.engine.is_playing(shimmerClip)) {
                    return fail("lowering did not stop the shimmer");
                }

                // --- 5. Mounted at kAgroStairs: the raise at Stand, a kick ignored --
                place_horse(on_floor(rd::kAgroStairs[0], rd::kAgroStairs[2], rd::kAgroStairs[1]),
                            rd::kAgroStairsYawDeg);
                {
                    const Vec3 p = from_body_frame(Vec3{-kS8MountSide, 0.0f, 0.0f}, pr.horsePos,
                                                   pr.horseYaw);
                    place_player(on_floor(p.x, p.z, pr.horsePos.y), pr.horseYaw);
                }
                RideKeys mount;
                mount.mount = true;
                RideKeys kick;
                kick.kick = true;
                frame(mount);
                if (pr.ride.phase != RidePhase::Mounting ||
                    !frames_until(3.0f, [&]() { return pr.ride.phase == RidePhase::Riding; })) {
                    return fail("E beside Agro did not mount her");
                }
                hold(kClipBlendSeconds);
                held = true;
                if (!frames_until(3.0f, [&]() { return sword.phase == SwordPhase::Raised; }) ||
                    !sword.mounted || std::string(sword.clip) != kSwordMounted.loop ||
                    pr.ride.gait != Gait::Stand) {
                    return fail("the mounted raise at Stand did not reach its loop");
                }
                frame(kick);
                frame();
                hold(0.5f);
                const bool kickIgnored = pr.ride.gait == Gait::Stand && pr.ride.speed == 0.0f &&
                                         sword.phase == SwordPhase::Raised;
                std::printf("s9-mounted clip=%s gait=%s kick_ignored=%d\n", sword.clip,
                            gait_name(pr.ride.gait), kickIgnored ? 1 : 0);
                if (!kickIgnored) {
                    return fail("a kick while raised moved Agro");
                }
                held = false;
                if (!frames_until(3.0f, [&]() { return sword.phase == SwordPhase::Lowered; })) {
                    return fail("the mounted lower did not end");
                }
                frame(mount);
                if (pr.ride.phase != RidePhase::Dismounting ||
                    !frames_until(3.0f, [&]() { return pr.ride.phase == RidePhase::OnFoot; })) {
                    return fail("E at a stand did not dismount");
                }
                frame();
                s9AgroLeft = HorsePose{pr.horsePos, pr.horseYaw};  // where the script leaves her

                // --- 6. Up the entrance stairs on foot -----------------------------
                place_player(on_floor(rd::kStairsFoot[0], rd::kStairsFoot[2], rd::kStairsFoot[1]),
                             0.0f);
                const ride_points::Door& corridor = rd::kDoorCorridor;
                const float doorX = 0.5f * (corridor.min[0] + corridor.max[0]);
                const float doorZ = 0.5f * (corridor.min[2] + corridor.max[2]);
                if (!walk_until(doorX, doorZ, [&]() { return player.position.z >= kS9EntranceZ; })) {
                    return fail("the stairs walk never reached the corridor floor" +
                                (stuckAt.empty() ? std::string() : " (blocked at " + stuckAt + ")"));
                }
                std::printf("s9-entrance top=%.2f,%.2f,%.2f\n", player.position.x, player.position.y,
                            player.position.z);
                if (!(player.position.y >= 2.7f && player.position.y <= 5.6f) ||
                    !(player.position.x >= 300.0f && player.position.x <= 308.0f)) {
                    return fail("the stairs' top is off the corridor floor");
                }
                capture("entrance");

                // --- 7. Into kDoorCorridor: the arena ------------------------------
                if (!walk_until(doorX, doorZ, [&]() { return doorLoad; })) {
                    return fail("the walk to door_corridor never loaded the arena" +
                                (stuckAt.empty() ? std::string() : " (blocked at " + stuckAt + ")"));
                }
                carry.s9Step = 1;
                renderer.release_scene();
                stage = kDoors[carry.door.row].to;
                continue;
            }

            if (opts.s6Selftest) {
                const SelftestResult result = run_s6(ctx, run);
                if (result.exitCode) {
                    return *result.exitCode;
                }
                if (result.next) {
                    stage = *result.next;
                    continue;
                }
            }

            if (opts.s4Selftest) {
                const SelftestResult result = run_s4(ctx, run);
                if (result.exitCode) {
                    return *result.exitCode;
                }
                if (result.next) {
                    stage = *result.next;
                    continue;
                }
            }

            if (opts.s3Selftest) {
                const SelftestResult result = run_s3(ctx, run);
                if (result.exitCode) {
                    return *result.exitCode;
                }
                if (result.next) {
                    stage = *result.next;
                    continue;
                }
            }

            if (opts.selftest) {
                const SelftestResult result = run_selftest(ctx, run);
                if (result.exitCode) {
                    return *result.exitCode;
                }
                if (result.next) {
                    stage = *result.next;
                    continue;
                }
            }

            if (!opts.screenshotPath.empty()) {
                const SelftestResult result = run_screenshot(ctx, run);
                if (result.exitCode) {
                    return *result.exitCode;
                }
                if (result.next) {
                    stage = *result.next;
                    continue;
                }
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
                    if (ctx.press_reset(death, deathCueFired, &deathCameraSet)) {
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
                    (here == StageId::Arena &&
                     (!death_wired || !corpse.loaded || entityRig >= playbacks.size() ||
                      entityClips.death < 0))) {
                    return e2e_fail("the arena has no death or return to drive");
                }
                e2e.ret = carry.ret.phase;
                if (here == StageId::Shrine) {
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
            // --s8-e2e (Task 9): the script's view of this stage. The loop below
            // is the interactive one; the script picks the input (the keys a
            // player would hold, the heading he walks), counts the gallop and
            // checks each join: the shrine's door into the plain, the plain's
            // door back into the shrine, R into the arena.
            struct S8E2eScript {
                int frames = 0;
                float seconds = 0.0f;  // fixed-step seconds in this stage
                int phase = 0;         // the stage's own steps (see below)
                float t = 0.0f;        // seconds in the phase
                float kickWait = 0.0f;
                float gallopPath = 0.0f;
                bool neighed = false;
                Vec3 lastHorse{};
                Vec3 progressFrom{};
                float progressSeconds = 0.0f;
            } s8e;
            const auto s8e_fail = [](const std::string& step) {
                std::fprintf(stderr, "sotc-viewer: --s8-e2e: %s\n", step.c_str());
                return 1;
            };
            if (opts.s8E2e) {
                if (here == StageId::Shrine && carry.s8Step == 0 && !door_arrival) {
                    // The start: --stage shrine, in Free.
                } else if (here == StageId::Plain && carry.s8Step == 0 && door_arrival) {
                    std::printf("s8e2e-door from=shrine to=plain\n");
                    carry.s8Step = 1;
                } else if (here == StageId::Shrine && carry.s8Step == 1 && door_arrival) {
                    std::printf("s8e2e-door from=plain to=shrine\n");
                    carry.s8Step = 2;
                } else if (here == StageId::Arena && carry.s8Step == 2 && !door_arrival) {
                    carry.s8Step = 3;
                } else {
                    return s8e_fail(std::string("the ") + stage_name +
                                    " loaded out of order (step " + std::to_string(carry.s8Step) +
                                    ")");
                }
                if (here == StageId::Plain && !plain_ride) {
                    return s8e_fail("the plain has no Agro to ride");
                }
            }
            // --s9-e2e (Task 9): the script's view of this stage — the S8 e2e's
            // seam, its own state. The loop below is the interactive one; the
            // script picks the keys, the held C and (for the lock) the look, lands
            // the one lethal hit in the arena, and checks each join: the shrine's
            // door onto the road, the corridor door into the arena, the return to
            // the shrine.
            struct S9E2eScript {
                int frames = 0;
                float seconds = 0.0f;  // fixed-step seconds in this stage
                int phase = 0;         // the stage's own steps (see below)
                float t = 0.0f;        // seconds in the phase
                float kickWait = 0.0f;
                float gallopPath = 0.0f;
                bool neighed = false;
                Vec3 lastHorse{};
                Vec3 progressFrom{};
                float progressSeconds = 0.0f;
                std::size_t leg = 0;     // the ridden route's waypoint
                bool aim = false;        // the scripted look onto Valus's chest
                bool hit = false;        // the arena's lethal hit landed
                bool deathDone = false;  // s9e2e-death printed
                float freeSeconds = 0.0f;
                SwordProbe probe;        // the last sword frame's readings
            } s9e;
            const auto s9e_fail = [](const std::string& step) {
                std::fprintf(stderr, "sotc-viewer: --s9-e2e: %s\n", step.c_str());
                return 1;
            };
            if (opts.s9E2e) {
                const DoorRow* const row = door_arrival ? arrival : nullptr;
                if (here == StageId::Shrine && carry.s9Step == 0 && row == nullptr) {
                    // The start: --stage shrine, in Free.
                } else if (here == StageId::Plain && carry.s9Step == 0 && row != nullptr &&
                           row->from == StageId::Shrine) {
                    std::printf("s9e2e-door from=%s to=%s\n", stage_label(row->from),
                                stage_label(row->to));
                    carry.s9Step = 1;
                } else if (here == StageId::Arena && carry.s9Step == 1 && row != nullptr &&
                           row->from == StageId::Plain) {
                    std::printf("s9e2e-door from=%s to=%s\n", stage_label(row->from),
                                stage_label(row->to));
                    carry.s9Step = 2;
                } else if (here == StageId::Shrine && carry.s9Step == 2 && row == nullptr &&
                           carry.ret.phase == ReturnPhase::Load) {
                    std::printf("s9e2e-return to=%s\n", stage_label(here));
                    carry.s9Step = 3;
                } else {
                    return s9e_fail(std::string("the ") + stage_name + " loaded out of order (step " +
                                    std::to_string(carry.s9Step) + ", return " +
                                    return_phase_name(carry.ret.phase) + ")");
                }
                if (here == StageId::Plain && (!plain_ride || !sword_wired || !sword_fx_ready)) {
                    return s9e_fail("the road has no Agro, raise clips or sword light");
                }
                if (here == StageId::Arena &&
                    (!return_wired || !death_wired || !corpse.loaded ||
                     entityRig >= playbacks.size() || entityClips.death < 0)) {
                    return s9e_fail("the arena has no death or return to drive");
                }
            }
            // S8 (D14): a stage entered through a door completes the door's Load
            // here; FadeIn runs on this stage's frames.
            if (door_arrival) {
                carry.door = step_door(carry.door, 0.0f, false, -1, /*loadDone=*/true);
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
                    if (flags.scriptedE2e && event.type != SDL_EVENT_QUIT) {
                        continue;  // --s7-e2e/--s8-e2e/--s9-e2e: the script owns the input
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
                const float dt = flags.scriptedE2e
                                     ? kFixedFrameSeconds
                                     : static_cast<float>(now - last_ticks) / 1000.0f;
                last_ticks = now;

                if (!flags.scriptedE2e) {
                    gamepad.poll();
                }
                if (gamepad.pausePressed) {
                    playback.playing = !playback.playing;
                }

                // S6: this frame's tendril frame — nullptr unless the burst is
                // active (the renderer's no-op for every other frame).
                EffectFrame effectFrame;
                const EffectFrame* effectFramePtr = nullptr;
                // S9: this frame's sword light — nullptr unless the sword is up.
                SwordFxFrame swordFrame;
                const SwordFxFrame* swordFramePtr = nullptr;
                if (follow_mode) {
                    // --s7-e2e: the stage's clock, the budget, and the script's
                    // two late actions — R 1 s into Free (the key's handler),
                    // and the exit 1 s into the reloaded arena.
                    if (opts.s7E2e) {
                        ++carry.e2eFrame;
                        if (++e2e.frames > kS7E2eMaxStageFrames) {
                            return e2e_fail(std::string("the ") + stage_name +
                                            " never finished its step (death " +
                                            death_phase_name(death.phase) + ", return " +
                                            return_phase_name(carry.ret.phase) + ")");
                        }
                        e2e.stageSeconds += dt;
                        if (here == StageId::Shrine &&
                            e2e.freeSeconds + kS7Eps >= kS7E2eLeadSeconds) {
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
                        } else if (here == StageId::Arena && carry.e2eStep == 2 &&
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
                    // --s8-e2e: this frame's scripted keys (decided from the state the
                    // last frame left), the late R and the exit.
                    PlayerInput s8In;
                    RideKeys s8Keys;
                    if (opts.s8E2e) {
                        namespace rp = ride_points;
                        if (++s8e.frames > kS8E2eMaxStageFrames) {
                            return s8e_fail(std::string("the ") + stage_name +
                                            " never finished its step (phase " +
                                            std::to_string(s8e.phase) + ")");
                        }
                        s8e.seconds += dt;
                        s8e.t += dt;
                        const auto next_phase = [&]() {
                            ++s8e.phase;
                            s8e.t = 0.0f;
                        };
                        // Walk (or run) toward a point: the heading is the script's,
                        // the camera looks along it, W does the walking.
                        const auto walk_to = [&](float x, float z, bool run) {
                            const float dx = x - player.position.x;
                            const float dz = z - player.position.z;
                            if (std::hypot(dx, dz) <= kS8E2eArrive) {
                                return true;
                            }
                            const float h = std::atan2(dx, dz) / kDegToRad;
                            player.yawDeg = h;
                            follow.yawOffsetDeg = 0.0f;
                            follow.pitchOffsetDeg = 0.0f;
                            camera.yaw = std::remainder(h + 180.0f, 360.0f);
                            s8In.forward = true;
                            s8In.run = run;
                            return false;
                        };
                        // A walk that stops making progress is a failed join.
                        const auto stuck = [&]() {
                            s8e.progressSeconds += dt;
                            if (std::hypot(player.position.x - s8e.progressFrom.x,
                                           player.position.z - s8e.progressFrom.z) > 0.5f) {
                                s8e.progressFrom = player.position;
                                s8e.progressSeconds = 0.0f;
                            }
                            return s8e.progressSeconds > kS8E2eStuckSeconds;
                        };
                        // Agro onto the gallop line: steer at a point 40 m ahead on it.
                        const auto pursue = [&]() {
                            const float hx = std::sin(rp::kProbeGallopHeadingDeg * kDegToRad);
                            const float hz = std::cos(rp::kProbeGallopHeadingDeg * kDegToRad);
                            const float along = (pr.horsePos.x - rp::kProbeGallopFrom[0]) * hx +
                                                (pr.horsePos.z - rp::kProbeGallopFrom[1]) * hz;
                            const float at = std::clamp(along + kS8E2eLookahead, 0.0f,
                                                        rp::kProbeGallopLength);
                            const float want =
                                std::atan2(rp::kProbeGallopFrom[0] + hx * at - pr.horsePos.x,
                                           rp::kProbeGallopFrom[1] + hz * at - pr.horsePos.z) /
                                kDegToRad;
                            const float err = std::remainder(want - pr.horseYaw, 360.0f);
                            s8Keys.steer = std::clamp(-err / kS8E2eSteerGainDeg, -1.0f, 1.0f);
                            return std::fabs(err);
                        };
                        const bool doorIdle = carry.door.phase == DoorPhase::Idle;
                        if (here == StageId::Shrine && carry.s8Step == 0) {
                            // Out through the front door (its box is ~43 m ahead).
                            if (doorIdle) {
                                // Round Mono's altar (a lattice route through the
                                // layered map), then straight out to the door box.
                                const ride_points::Door& d = rp::kDoorShrine;
                                const std::size_t leg = static_cast<std::size_t>(s8e.phase);
                                const bool last = leg >= kS8E2eShrineRoute.size();
                                const float x = last ? 0.5f * (d.min[0] + d.max[0])
                                                     : kS8E2eShrineRoute[leg][0];
                                const float z = last ? 0.5f * (d.min[2] + d.max[2])
                                                     : kS8E2eShrineRoute[leg][1];
                                if (walk_to(x, z, /*run=*/true) && !last) {
                                    next_phase();
                                }
                                if (stuck()) {
                                    char where[96];
                                    std::snprintf(where, sizeof(where),
                                                  "the walk to door_shrine is blocked at "
                                                  "%.1f,%.1f,%.1f",
                                                  player.position.x, player.position.y,
                                                  player.position.z);
                                    return s8e_fail(where);
                                }
                            }
                        } else if (here == StageId::Plain) {
                            switch (s8e.phase) {
                                case 0:  // the door's FadeIn
                                    if (doorIdle && !carry.door.inputLocked) {
                                        next_phase();
                                    }
                                    break;
                                case 1:  // whistle
                                    s8Keys.whistle = true;
                                    next_phase();
                                    break;
                                case 2:  // she comes, neighs, stands
                                    s8e.neighed = s8e.neighed || pr.agro.phase == AgroPhase::Neigh;
                                    if (s8e.neighed && pr.agro.phase == AgroPhase::Idle &&
                                        pr.whistleClip < 0) {
                                        if (!(ctx.horse_distance() <= kS8ArriveMax)) {
                                            return s8e_fail("Agro stopped farther than 3.5 m");
                                        }
                                        next_phase();
                                    } else if (s8e.t > kS8MaxWhistleSeconds) {
                                        return s8e_fail("the whistle: Agro never arrived");
                                    }
                                    break;
                                case 3: {  // around her front-left, then beside her left
                                    const Vec3 a = from_body_frame(Vec3{-kS8E2eAround, 0.0f,
                                                                        kS8E2eAround},
                                                                   pr.horsePos, pr.horseYaw);
                                    if (walk_to(a.x, a.z, /*run=*/false)) {
                                        next_phase();
                                    }
                                    break;
                                }
                                case 4: {
                                    const Vec3 b = from_body_frame(
                                        Vec3{-kS8MountSide, 0.0f, 0.0f}, pr.horsePos, pr.horseYaw);
                                    if (walk_to(b.x, b.z, /*run=*/false)) {
                                        next_phase();
                                    }
                                    break;
                                }
                                case 5:  // E: the mount
                                    s8Keys.mount = true;
                                    next_phase();
                                    break;
                                case 6:
                                    if (pr.ride.phase == RidePhase::OnFoot) {
                                        return s8e_fail("E beside her did not mount");
                                    }
                                    if (pr.ride.phase == RidePhase::Riding) {
                                        next_phase();
                                    }
                                    break;
                                case 7:  // turn onto the gallop line at a standstill
                                    if (pursue() < kS8E2eAlignDeg) {
                                        next_phase();
                                    } else if (s8e.t > 10.0f) {
                                        return s8e_fail("Agro never turned onto the gallop line");
                                    }
                                    break;
                                case 8:  // spaced kicks up to the gallop
                                    pursue();
                                    if (pr.ride.gait == Gait::Gallop) {
                                        s8e.lastHorse = pr.horsePos;
                                        s8e.gallopPath = 0.0f;
                                        next_phase();
                                    } else if ((s8e.kickWait -= dt) <= 0.0f) {
                                        s8Keys.kick = true;
                                        s8e.kickWait = kS8KickGapSeconds;
                                    }
                                    if (s8e.t > 10.0f) {
                                        return s8e_fail("the kicks never reached the gallop");
                                    }
                                    break;
                                case 9:  // 5 s at the gallop along the line
                                    s8e.gallopPath += std::hypot(pr.horsePos.x - s8e.lastHorse.x,
                                                                 pr.horsePos.z - s8e.lastHorse.z);
                                    s8e.lastHorse = pr.horsePos;
                                    if (s8e.t >= kS8E2eGallopSeconds) {
                                        std::printf("s8e2e-ride gallop_m=%.1f\n", s8e.gallopPath);
                                        if (pr.ride.gait != Gait::Gallop ||
                                            !(s8e.gallopPath >= kS8E2eGallopMin)) {
                                            return s8e_fail("the gallop was cut short");
                                        }
                                        next_phase();
                                    } else {
                                        pursue();
                                    }
                                    break;
                                case 10:  // hold S past the rein's 0.5 s, then let go
                                    s8Keys.rein = s8e.t <= kReinHoldSeconds + 0.1f;
                                    if (!s8Keys.rein && pr.ride.gait == Gait::Stand &&
                                        pr.ride.horseClip == kAgroStandClip) {
                                        next_phase();
                                    } else if (s8e.t > 8.0f) {
                                        return s8e_fail("S never stopped the gallop");
                                    }
                                    break;
                                case 11:  // E: the dismount
                                    s8Keys.mount = true;
                                    next_phase();
                                    break;
                                case 12:
                                    if (pr.ride.phase == RidePhase::Riding) {
                                        return s8e_fail("E at a standstill did not dismount");
                                    }
                                    if (pr.ride.phase == RidePhase::OnFoot) {
                                        s8e.progressFrom = player.position;
                                        next_phase();
                                    }
                                    break;
                                case 13:  // on foot, back to the shrine steps
                                    if (doorIdle) {
                                        const ride_points::Door& d = rp::kDoorPlain;
                                        walk_to(0.5f * (d.min[0] + d.max[0]),
                                                0.5f * (d.min[2] + d.max[2]), /*run=*/true);
                                        if (stuck()) {
                                            char where[96];
                                            std::snprintf(where, sizeof(where),
                                                          "the walk to door_plain is blocked at "
                                                          "%.1f,%.1f",
                                                          player.position.x, player.position.z);
                                            return s8e_fail(where);
                                        }
                                    }
                                    break;
                                default:
                                    break;
                            }
                        } else if (here == StageId::Shrine && carry.s8Step == 2) {
                            // Back in the shrine: the fade-in, a second, then R.
                            if (s8e.phase == 0) {
                                if (doorIdle && !carry.door.inputLocked) {
                                    next_phase();
                                }
                            } else if (s8e.t >= kS7E2eLeadSeconds) {
                                press_r_key();
                                if (running) {
                                    return s8e_fail("R in the shrine did not ask for the arena");
                                }
                            }
                        } else if (here == StageId::Arena && carry.s8Step == 3 &&
                                   s8e.seconds + kS7Eps >= kS7E2eLeadSeconds) {
                            const int valus = static_cast<int>(std::lround(encounter.health));
                            const int hero = static_cast<int>(std::lround(combat.health));
                            if (carry.ret.phase != ReturnPhase::Idle ||
                                carry.door.phase != DoorPhase::Idle ||
                                pr.ride.phase != RidePhase::OnFoot ||
                                death.phase != DeathPhase::Idle || encounter.defeated ||
                                valus != 100 || hero != 100) {
                                return s8e_fail("R did not restore the arena's opening state");
                            }
                            std::printf("s8e2e-reset stage=arena ride=OnFoot door=%s valus=%d "
                                        "player=%d\n",
                                        door_phase_name(carry.door.phase), valus, hero);
                            print_status_line(opts, shell.win);
                            request = StageRequest{};
                            running = false;
                        }
                    }
                    // --s9-e2e: this frame's scripted keys, held C and look (decided
                    // from the state the last frame left), the joins and the exit.
                    PlayerInput s9In;
                    RideKeys s9Keys;
                    bool s9Held = false;
                    if (opts.s9E2e) {
                        namespace rd = road_points;
                        ++carry.e2eFrame;
                        if (++s9e.frames > kS9E2eMaxStageFrames) {
                            // The arena and the shrine after the return run on the
                            // death and return machines; the road and the first
                            // shrine on the script's phases.
                            const std::string step =
                                here == StageId::Arena
                                    ? std::string("the death (") + death_phase_name(death.phase) +
                                          ") and the return (" +
                                          return_phase_name(carry.ret.phase) + ")"
                                : carry.s9Step == 3
                                    ? std::string("the return to Free (") +
                                          return_phase_name(carry.ret.phase) + ")"
                                    : "script phase " + std::to_string(s9e.phase);
                            return s9e_fail(std::string("the ") + stage_name +
                                            " never finished its step: " + step);
                        }
                        s9e.seconds += dt;
                        s9e.t += dt;
                        const auto next_phase = [&]() {
                            ++s9e.phase;
                            s9e.t = 0.0f;
                            s9e.progressFrom = player.position;
                            s9e.progressSeconds = 0.0f;
                        };
                        const auto where = [&](const char* what, Vec3 p) {
                            char text[128];
                            std::snprintf(text, sizeof(text), "%s at %.1f,%.1f,%.1f", what, p.x, p.y,
                                          p.z);
                            return std::string(text);
                        };
                        // On foot toward a point: the heading is the script's, the
                        // camera looks along it, W does the walking.
                        const auto walk_to = [&](float x, float z, bool run) {
                            const float dx = x - player.position.x;
                            const float dz = z - player.position.z;
                            if (std::hypot(dx, dz) <= kS8E2eArrive) {
                                return true;
                            }
                            const float h = std::atan2(dx, dz) / kDegToRad;
                            player.yawDeg = h;
                            follow.yawOffsetDeg = 0.0f;
                            follow.pitchOffsetDeg = 0.0f;
                            camera.yaw = std::remainder(h + 180.0f, 360.0f);
                            s9In.forward = true;
                            s9In.run = run;
                            return false;
                        };
                        // No 0.5 m of progress (the body, or Agro while mounted) for
                        // kS9StuckSeconds: the walk or the ride is blocked.
                        const auto stuck = [&](Vec3 p) {
                            s9e.progressSeconds += dt;
                            if (std::hypot(p.x - s9e.progressFrom.x, p.z - s9e.progressFrom.z) > 0.5f) {
                                s9e.progressFrom = p;
                                s9e.progressSeconds = 0.0f;
                            }
                            return s9e.progressSeconds > kS9StuckSeconds;
                        };
                        // Agro's steer toward a point; returns the heading error.
                        const auto steer_to = [&](float x, float z) {
                            const float want =
                                std::atan2(x - pr.horsePos.x, z - pr.horsePos.z) / kDegToRad;
                            const float err = std::remainder(want - pr.horseYaw, 360.0f);
                            s9Keys.steer = std::clamp(-err / kS8E2eSteerGainDeg, -1.0f, 1.0f);
                            return std::fabs(err);
                        };
                        const auto horse_to = [&](float x, float z) {
                            return std::hypot(x - pr.horsePos.x, z - pr.horsePos.z);
                        };
                        // The ridden route: kE2eRoute's legs, its last point swapped
                        // for Agro's stand by the stairs (the lock is done there).
                        const auto route_point = [&](std::size_t leg, float& x, float& z) {
                            if (leg + 1 < static_cast<std::size_t>(rd::kE2eRouteCount)) {
                                x = rd::kE2eRoute[leg][0];
                                z = rd::kE2eRoute[leg][1];
                            } else {
                                x = rd::kAgroStairs[0];
                                z = rd::kAgroStairs[2];
                            }
                        };
                        const bool doorIdle = carry.door.phase == DoorPhase::Idle;
                        const bool settled = doorIdle && !carry.door.inputLocked;
                        if (here == StageId::Shrine && carry.s9Step == 0) {
                            // Out through the front door, round Mono's altar (S8's route).
                            if (doorIdle) {
                                const ride_points::Door& d = ride_points::kDoorShrine;
                                const std::size_t leg = static_cast<std::size_t>(s9e.phase);
                                const bool last = leg >= kS8E2eShrineRoute.size();
                                const float x = last ? 0.5f * (d.min[0] + d.max[0])
                                                     : kS8E2eShrineRoute[leg][0];
                                const float z = last ? 0.5f * (d.min[2] + d.max[2])
                                                     : kS8E2eShrineRoute[leg][1];
                                if (walk_to(x, z, /*run=*/true) && !last) {
                                    next_phase();
                                }
                                if (stuck(player.position)) {
                                    return s9e_fail(where("the walk to door_shrine is blocked",
                                                          player.position));
                                }
                            }
                        } else if (here == StageId::Plain) {
                            switch (s9e.phase) {
                                case 0:  // the door's FadeIn
                                    if (settled) {
                                        next_phase();
                                    }
                                    break;
                                case 1:  // whistle
                                    s9Keys.whistle = true;
                                    next_phase();
                                    break;
                                case 2:  // she comes, neighs, stands
                                    s9e.neighed = s9e.neighed || pr.agro.phase == AgroPhase::Neigh;
                                    if (s9e.neighed && pr.agro.phase == AgroPhase::Idle &&
                                        pr.whistleClip < 0) {
                                        next_phase();
                                    } else if (s9e.t > kS8MaxWhistleSeconds) {
                                        return s9e_fail("the whistle: Agro never arrived");
                                    }
                                    break;
                                case 3: {  // round her front-left, then beside her left
                                    const Vec3 a = from_body_frame(
                                        Vec3{-kS8E2eAround, 0.0f, kS8E2eAround}, pr.horsePos,
                                        pr.horseYaw);
                                    if (walk_to(a.x, a.z, /*run=*/false)) {
                                        next_phase();
                                    }
                                    break;
                                }
                                case 4: {
                                    const Vec3 b = from_body_frame(
                                        Vec3{-kS8MountSide, 0.0f, 0.0f}, pr.horsePos, pr.horseYaw);
                                    if (walk_to(b.x, b.z, /*run=*/false)) {
                                        next_phase();
                                    }
                                    break;
                                }
                                case 5:  // E: the mount
                                    s9Keys.mount = true;
                                    next_phase();
                                    break;
                                case 6:
                                    if (pr.ride.phase == RidePhase::OnFoot) {
                                        return s9e_fail("E beside her did not mount");
                                    }
                                    if (pr.ride.phase == RidePhase::Riding) {
                                        std::printf("s9e2e-mount side=%s\n",
                                                    pr.ride.side == Side::Left ? "L" : "R");
                                        next_phase();
                                    }
                                    break;
                                case 7: {  // turn onto the route at a standstill
                                    float x = 0.0f, z = 0.0f;
                                    route_point(s9e.leg, x, z);
                                    if (steer_to(x, z) < kS8E2eAlignDeg) {
                                        next_phase();
                                    } else if (s9e.t > 10.0f) {
                                        return s9e_fail("Agro never turned onto the route");
                                    }
                                    break;
                                }
                                case 8:  // spaced kicks up to the gallop, on the route
                                case 9: {  // the gallop along the route to the stairs
                                    float x = 0.0f, z = 0.0f;
                                    route_point(s9e.leg, x, z);
                                    const bool lastLeg =
                                        s9e.leg + 1 >= static_cast<std::size_t>(rd::kE2eRouteCount);
                                    if (!lastLeg && horse_to(x, z) <= kS9E2eWaypoint) {
                                        ++s9e.leg;
                                        route_point(s9e.leg, x, z);
                                    }
                                    steer_to(x, z);
                                    if (s9e.phase == 8) {
                                        if (pr.ride.gait == Gait::Gallop) {
                                            s9e.lastHorse = pr.horsePos;
                                            s9e.gallopPath = 0.0f;
                                            next_phase();
                                        } else if ((s9e.kickWait -= dt) <= 0.0f) {
                                            s9Keys.kick = true;
                                            s9e.kickWait = kS8KickGapSeconds;
                                        }
                                        if (s9e.t > 10.0f) {
                                            return s9e_fail("the kicks never reached the gallop");
                                        }
                                        break;
                                    }
                                    s9e.gallopPath += std::hypot(pr.horsePos.x - s9e.lastHorse.x,
                                                                 pr.horsePos.z - s9e.lastHorse.z);
                                    s9e.lastHorse = pr.horsePos;
                                    if (pr.ride.gait != Gait::Gallop || stuck(pr.horsePos)) {
                                        return s9e_fail(where("the gallop was cut short",
                                                              pr.horsePos));
                                    }
                                    if (lastLeg && horse_to(x, z) <= kS9E2eReinAt) {
                                        std::printf("s9e2e-ride gallop_m=%.1f\n", s9e.gallopPath);
                                        next_phase();
                                    }
                                    break;
                                }
                                case 10:  // hold S past the rein's 0.5 s, then let go
                                    steer_to(rd::kAgroStairs[0], rd::kAgroStairs[2]);
                                    s9Keys.rein = s9e.t <= kReinHoldSeconds + 0.1f;
                                    if (!s9Keys.rein && pr.ride.gait == Gait::Stand &&
                                        pr.ride.horseClip == kAgroStandClip) {
                                        next_phase();
                                    } else if (s9e.t > 8.0f) {
                                        return s9e_fail("S never stopped the gallop");
                                    }
                                    break;
                                case 11:  // raise at Stand, the look onto Valus's chest
                                    s9Held = true;
                                    s9e.aim = true;
                                    if (sword.phase == SwordPhase::Raised && s9e.probe.beam.locked &&
                                        std::isfinite(s9e.probe.hit)) {
                                        next_phase();
                                    } else if (s9e.t > 4.0f) {
                                        next_phase();  // printed and refused below
                                    }
                                    if (s9e.phase == 12) {
                                        const bool hit = std::isfinite(s9e.probe.hit);
                                        std::printf("s9e2e-lock k=%.4f locked=%d hit=%d hit_m=%.1f\n",
                                                    s9e.probe.beam.k, s9e.probe.beam.locked ? 1 : 0,
                                                    hit ? 1 : 0, s9e.probe.hit);
                                        if (!s9e.probe.beam.locked || !hit ||
                                            !(s9e.probe.hit <= kBeamRayPerRaise) || !sword.mounted) {
                                            return s9e_fail(where("the mounted raise did not lock a "
                                                                  "lit hit",
                                                                  pr.horsePos));
                                        }
                                    }
                                    break;
                                case 12:  // lower
                                    s9e.aim = false;
                                    if (sword.phase == SwordPhase::Lowered) {
                                        next_phase();
                                    } else if (s9e.t > 3.0f) {
                                        return s9e_fail("releasing C did not lower the sword");
                                    }
                                    break;
                                case 13:  // one kick: the walk
                                    s9Keys.kick = true;
                                    s9e.leg = 0;
                                    next_phase();
                                    break;
                                case 14: {  // up the stairs at the walk
                                    const float x = rd::kStairsFoot[0];
                                    const float z = s9e.leg == 0 ? rd::kStairsFoot[2]
                                                                 : kS9EntranceZ + 1.0f;
                                    if (s9e.leg == 0 && horse_to(x, z) <= 1.5f) {
                                        s9e.leg = 1;
                                    }
                                    steer_to(x, z);
                                    if (pr.horsePos.z >= kS9EntranceZ) {
                                        std::printf("s9e2e-stairs mounted=%d top=%.2f,%.2f,%.2f\n",
                                                    pr.ride.phase == RidePhase::Riding ? 1 : 0,
                                                    pr.horsePos.x, pr.horsePos.y, pr.horsePos.z);
                                        if (pr.ride.phase != RidePhase::Riding ||
                                            !(pr.horsePos.y >= 2.7f && pr.horsePos.y <= 5.6f)) {
                                            return s9e_fail(where("the ride up the stairs ended off "
                                                                  "the corridor floor",
                                                                  pr.horsePos));
                                        }
                                        next_phase();
                                    } else if (stuck(pr.horsePos)) {
                                        return s9e_fail(where("the ride up the stairs is blocked",
                                                              pr.horsePos));
                                    }
                                    break;
                                }
                                case 15:  // hold S: stop on the corridor floor
                                    s9Keys.rein = s9e.t <= kReinHoldSeconds + 0.1f;
                                    if (!s9Keys.rein && pr.ride.gait == Gait::Stand &&
                                        pr.ride.horseClip == kAgroStandClip) {
                                        next_phase();
                                    } else if (s9e.t > 8.0f) {
                                        return s9e_fail("S never stopped the walk");
                                    }
                                    break;
                                case 16:  // E: the dismount
                                    s9Keys.mount = true;
                                    next_phase();
                                    break;
                                case 17:
                                    if (pr.ride.phase == RidePhase::Riding) {
                                        return s9e_fail("E at a stand did not dismount");
                                    }
                                    if (pr.ride.phase == RidePhase::OnFoot) {
                                        next_phase();
                                    }
                                    break;
                                case 18:  // on foot into the corridor's door
                                    if (doorIdle) {
                                        const ride_points::Door& d = rd::kDoorCorridor;
                                        walk_to(0.5f * (d.min[0] + d.max[0]),
                                                0.5f * (d.min[2] + d.max[2]), /*run=*/true);
                                        if (stuck(player.position)) {
                                            return s9e_fail(where("the walk to door_corridor is "
                                                                  "blocked",
                                                                  player.position));
                                        }
                                    }
                                    break;
                                default:
                                    break;
                            }
                        } else if (here == StageId::Arena) {
                            // The lethal hit lands below (1 s in); the death runs to Done.
                            if (death.phase == DeathPhase::Done && !s9e.deathDone) {
                                std::printf("s9e2e-death phase=%s\n", death_phase_name(death.phase));
                                s9e.deathDone = true;
                            }
                        } else if (here == StageId::Shrine && carry.s9Step == 3) {
                            if (carry.ret.phase == ReturnPhase::Free) {
                                s9e.freeSeconds += dt;
                            }
                            if (s9e.freeSeconds + kS7Eps >= kS7E2eLeadSeconds) {
                                std::printf("s9e2e-done frames=%d\n", carry.e2eFrame);
                                print_status_line(opts, shell.win);
                                request = StageRequest{};
                                running = false;
                            }
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
                                                /*loadDone=*/here == StageId::Shrine);
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
                        e2eHitNow = here == StageId::Arena && !e2e.hit &&
                                    e2e.stageSeconds + kS7Eps >= kS7E2eLeadSeconds;
                    }
                    const auto deadzoned = [](float value) {
                        return std::fabs(value) > kS41StickDeadzone ? value : 0.0f;
                    };
                    // The one input seam: the keyboard and gamepad, or under
                    // --s7-e2e the script's (empty, except a held W while the
                    // return should lock it — the lock below must eat it).
                    PlayerInput input;
                    RideKeys rideKeys;  // S8: the plain's held ride keys
                    bool swordHeld = false;  // S9: C / the left shoulder, held
                    if (opts.s7E2e) {
                        input.forward = e2eExpectLocked;
                    } else if (opts.s8E2e) {
                        input = s8In;
                        rideKeys = s8Keys;
                    } else if (opts.s9E2e) {
                        input = s9In;
                        rideKeys = s9Keys;
                        swordHeld = s9Held;
                    } else {
                        const bool* keys = SDL_GetKeyboardState(nullptr);
                        // S8 (G4): W kicks, S reins, A/D steer, E mounts and
                        // dismounts, Q whistles; the gamepad's left stick forward
                        // kicks, back reins, X steers, south mounts, north whistles.
                        rideKeys.kick =
                            keys[SDL_SCANCODE_W] || gamepad.leftY < -kStickPressThreshold;
                        rideKeys.rein =
                            keys[SDL_SCANCODE_S] || gamepad.leftY > kStickPressThreshold;
                        rideKeys.mount = keys[SDL_SCANCODE_E] || gamepad.south;
                        rideKeys.whistle = keys[SDL_SCANCODE_Q] || gamepad.north;
                        const float steerKeys = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) -
                                                (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
                        rideKeys.steer = std::clamp(steerKeys + gamepad.leftX, -1.0f, 1.0f);
                        input.forward = keys[SDL_SCANCODE_W] || deadzoned(gamepad.leftY) < 0.0f;
                        input.back = keys[SDL_SCANCODE_S] || deadzoned(gamepad.leftY) > 0.0f;
                        input.left = keys[SDL_SCANCODE_A] || deadzoned(gamepad.leftX) < 0.0f;
                        input.right = keys[SDL_SCANCODE_D] || deadzoned(gamepad.leftX) > 0.0f;
                        input.run = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
                        input.grab = keys[SDL_SCANCODE_E] || gamepad.grab;
                        input.strike = keys[SDL_SCANCODE_J] || gamepad.strike;
                        swordHeld = keys[SDL_SCANCODE_C] || gamepad.raise;
                    }
                    // S6 D7: the death cinematic locks the body — movement, grab
                    // and strike are ignored while the camera runs; R, M and Escape
                    // are handled above and stay live. Control returns in Done.
                    // S7: the return locks it from FadeOut through Wake.
                    const bool inputLocked = death_locks_input(death) || carry.ret.inputLocked ||
                                             carry.door.inputLocked;
                    // S8: on the plain the ride owns the body while mounted (no
                    // player_step, no mode clip) and the whistle holds it still.
                    const bool rideOwnsBody =
                        plain_ride && (pr.ride.phase != RidePhase::OnFoot || pr.whistleClip >= 0);
                    if (inputLocked || rideOwnsBody) {
                        input = PlayerInput{};
                    }
                    // S9: on foot a raised sword owns the clip and turns the move
                    // input into a facing (zero speed; the camera aims the beam).
                    const bool swordFoot = sword_wired && sword_locks_movement(sword);
                    if (swordFoot) {
                        const float ahead = (input.forward ? 1.0f : 0.0f) - (input.back ? 1.0f : 0.0f);
                        const float side = (input.right ? 1.0f : 0.0f) - (input.left ? 1.0f : 0.0f);
                        if (ahead != 0.0f || side != 0.0f) {
                            // player_update's basis and turn rate (540 deg/s, its dt clamp).
                            const float cy = camera_look_yaw(camera) * kDegToRad;
                            const float want =
                                std::atan2(std::sin(cy) * ahead - std::cos(cy) * side,
                                           std::cos(cy) * ahead + std::sin(cy) * side) / kDegToRad;
                            const float turn = 540.0f * std::clamp(dt, 0.0f, 0.05f);
                            player.yawDeg = std::remainder(
                                player.yawDeg +
                                    std::clamp(std::remainder(want - player.yawDeg, 360.0f), -turn, turn),
                                360.0f);
                        }
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
                        if (inputLocked && !e2e.lockPrinted && here == StageId::Arena) {
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
                    const bool wakeFrame = ctx.waking();
                    if (wakeFrame) {
                        ctx.pose_wake();
                        if (opts.s7E2e && carry.ret.phase == ReturnPhase::Wake &&
                            playback.index != e2e.wakeClip) {
                            e2e.wakeClip = playback.index;
                            ++e2e.wakeClips;
                            std::printf("s7e2e-wake clip=%s t=%.3f\n",
                                        playback.clips[static_cast<std::size_t>(playback.index)]
                                            .name.c_str(),
                                        ctx.wake_clock());
                        }
                    } else if (!rideOwnsBody) {
                        ctx.leave_wake();
                        if (swordFoot) {
                            // The sword's clip at the machine's clock (last frame's step).
                            playback.index = clip_index_named(playback, sword.clip);
                            playback.time = sword.clipTime;
                            playback.pinned = true;
                            playback.playing = false;
                        } else {
                            update_combat_clips(playback, scene, combat, input, manual_clip);
                        }
                    }
                    palettes = update_all_poses(renderer, scene, playbacks, dt, club);
                    if (plain_ride) {
                        ctx.plain_step(dt, rideKeys, inputLocked, palettes);
                    }
                    const bool riding = plain_ride && pr.ride.phase != RidePhase::OnFoot;
                    // The follow camera's subject: Agro while mounted (D13).
                    const Vec3 followPos = riding ? pr.horsePos : player.position;
                    const float followYaw = riding ? pr.horseYaw : player.yawDeg;
                    const bool followMoving = riding ? pr.ride.speed != 0.0f : moving;
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
                            camera_follow(camera, follow, followPos, followYaw, drag_dx, drag_dy,
                                          wheel_delta, followMoving, dt);
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
                    const Vec3 positionBefore = player.position;
                    // S8 (D8): no player_step while mounted — the ride places him.
                    StepEvents events{};
                    if (!riding) {
                        events = player_step(player, combat, input, frame, tuning, dt);
                    }
                    if (audio_on) {
                        append_player_sfx(events, player.position, frameSfx);
                    }
                    // --s9-e2e's: 1 s into the arena entered through the corridor.
                    const bool s9HitNow = opts.s9E2e && here == StageId::Arena && !s9e.hit &&
                                          s9e.seconds + kS7Eps >= kS7E2eLeadSeconds;
                    if (e2eHitNow || s9HitNow) {
                        // --s7-e2e's one lethal hit, landed as a strike lands.
                        events.damageDealt = 100;
                        if (s9HitNow) {
                            s9e.hit = true;
                        } else {
                            e2e.hit = true;
                        }
                    }
                    if (events.damageDealt > 0 && entityRig < playbacks.size()) {
                        apply_combat_events(encounter, playbacks[entityRig], entityClips, events,
                                            weakpoint, audio_on ? &frameSfx : nullptr);
                    }
                    if (e2eHitNow && !encounter.defeated) {
                        return e2e_fail("the lethal hit did not defeat Valus");
                    }
                    if (s9HitNow && !encounter.defeated) {
                        return s9e_fail("the lethal hit did not defeat Valus");
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
                            camera_follow(camera, follow, followPos, followYaw, drag_dx, drag_dy,
                                          wheel_delta, followMoving, dt);
                        }
                    }

                    if (!riding) {
                        apply_player_transform(scene, player_rig_record, player, ctx.player_lift());
                    }
                    // S9 (D5-D7): the sword in the hand, its beam, the shade and the
                    // lock, the machine, the cues and the light — after the rig's
                    // model is final for this frame (on foot or in the saddle).
                    // --s9-e2e: the scripted look onto Valus's chest (from the last
                    // frame's tip), after the follow camera, before the sword aims.
                    if (opts.s9E2e && s9e.aim) {
                        const Vec3 chest = valus_chest();
                        camera_look_along(camera, vec3_normalize(Vec3{chest.x - s9e.probe.tip.x,
                                                                      chest.y - s9e.probe.tip.y,
                                                                      chest.z - s9e.probe.tip.z}));
                    }
                    if (sword_wired) {
                        s9e.probe = ctx.sword_frame(dt, swordHeld, inputLocked, palettes, swordFrame);
                        if (swordFrame.active) {
                            swordFramePtr = &swordFrame;
                        }
                    }
                    // S8 (D14): the doors — the shrine's front exit and the
                    // plain's shrine steps — fire on entry, on foot, through the
                    // door machine; its Load ends the iteration for the other
                    // stage (this frame still draws, faded).
                    // R16: only while the stage still runs (R or Escape this frame
                    // ended it), from the kDoors rows of this stage.
                    if (s8_wired && ctx.door_frame(dt, positionBefore, riding, inputLocked, running)) {
                        request = StageRequest{false, kDoors[carry.door.row].to};
                        running = false;
                    }
                    if (wakeFrame) {
                        camera = camera_wake_pose(stage_spawn, stage_spawn_yaw, ctx.wake_clock(),
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
                    renderer.set_hud(ctx.hud_state(/*visible=*/true));
                } else {
                    update_all_poses(renderer, scene, playbacks, dt, club);
                }

                renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                                      /*acquire_swapchain=*/true, effectFramePtr, swordFramePtr);

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
