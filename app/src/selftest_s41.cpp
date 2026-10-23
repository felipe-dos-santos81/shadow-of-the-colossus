#include "selftests.h"

#include "capture.h"
#include "door.h"
#include "encounter_rt.h"
#include "ride_points.h"
#include "road_points.h"
#include "shrine_points.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr int kS41ClimbFrames = 240;        // 4 simulated seconds at 1/60 s
constexpr float kS41MinRise = 3.0f;         // metres the proven climb must gain
constexpr int kS41ChargeFrames = 60;        // held J past the 0.9 s full charge
constexpr int kS41WeakChargeFrames = 20;    // a third of a second: a weak hit
constexpr int kS41CatchWaitFrames = 42;     // 0.7 s with no catch press
constexpr int kS41MaxFallFrames = 600;      // 10 s: the whole head-to-ground fall
constexpr int kS41MaxStaggerFrames = 120;   // the 1.2 s stagger plus margin
constexpr float kS41Eps = 1e-4f;            // monotonic and ground-contact epsilon

}  // namespace

// --s41-selftest: the S4.1 climb/fight timeline.
SelftestResult run_s41(StageCtx& ctx, RunState& run) {
    const AppOptions& opts = run.opts;
    Shell& shell = run.shell;
    Renderer& renderer = run.renderer;
    Scene& scene = ctx.scene;
    ClubAttachment& club = ctx.club;
    SceneManifest& manifest = ctx.manifest;
    Heightmap& heightmap = ctx.heightmap;
    const RigCpu& player_rig_record = *ctx.playerRigRecord;
    Camera& camera = ctx.camera;
    PlayerState& player = ctx.player;
    PlayerCombat& combat = ctx.combat;
    Encounter& encounter = ctx.encounter;
    const CombatTuning& tuning = ctx.tuning;
    CameraClimb& climbCamera = ctx.climbCamera;
    CameraFollow& follow = ctx.follow;
    const std::size_t& entityRig = ctx.entityRig;
    std::vector<Playback>& playbacks = ctx.playbacks;
    Playback& playback = *ctx.playbackPtr;
    PlayerClips& playerClips = ctx.playerClips;
    EntityClips& entityClips = ctx.entityClips;

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
