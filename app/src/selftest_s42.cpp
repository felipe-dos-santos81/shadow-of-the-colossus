#include "selftests.h"

#include "capture.h"
#include "door.h"
#include "encounter_rt.h"
#include "ride_points.h"
#include "road_points.h"
#include "shrine_points.h"
#include "wav_io.h"

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

}  // namespace

// --s42-selftest and --s5-selftest (S5 woven in): the S4.2 timeline, with the audio observer under --s5-selftest.
SelftestResult run_s42(StageCtx& ctx, RunState& run) {
    const AppOptions& opts = run.opts;
    Shell& shell = run.shell;
    Renderer& renderer = run.renderer;
    AudioSeam& audio = run.audio;
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
    CameraTarget& targetCamera = ctx.targetCamera;
    const std::size_t& entityRig = ctx.entityRig;
    std::vector<Playback>& playbacks = ctx.playbacks;
    Playback& playback = *ctx.playbackPtr;
    PlayerClips& playerClips = ctx.playerClips;
    EntityClips& entityClips = ctx.entityClips;

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
