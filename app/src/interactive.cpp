#include "interactive.h"

#include "capture.h"
#include "door.h"
#include "encounter_rt.h"
#include "gamepad_input.h"
#include "s11_script.h"
#include "s12_script.h"
#include "ride_points.h"
#include "road_points.h"
#include "shrine_points.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

namespace {

// --- The interactive input (S4.1) and the e2e scripts' budgets ----------------

constexpr float kS41StickDeadzone = 0.25f;  // gamepad axis deadzone
constexpr float kS41StickPixelsPerSecond = 900.0f;  // right stick -> drag pixels

// --s7-e2e (Task 13): the script's three waits, and a generous budget for
// each stage (the arena's 1 + 8 + 2 + 1 s, the shrine's 1.5 + ~11.8 + 1 s).
constexpr float kS7E2eLeadSeconds = 1.0f;  // arena before the hit; Free and arena before R/exit
constexpr int kS7E2eMaxStageFrames = 3600;  // 60 s at 1/60 s
constexpr float kS7E2eFloorTolerance = 0.05f;  // the feet vs heightmap_floor at Free

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
// --s9-e2e (Task 9, authored): its budget and the ride's distances.
constexpr int kS9E2eMaxStageFrames = 12000;     // 200 s at 1/60 s per stage
constexpr float kS9E2eWaypoint = 12.0f;         // m: a ridden waypoint passed
constexpr float kS9E2eReinAt = 20.0f;           // m from kAgroStairs: rein (the stop slides ~14 m)

}  // namespace

// One interactive frame in manifest mode (S10 Task 6d): the death and return
// machines, the input lock, Valus's fight, the clips and poses, the plain, the
// cameras, the player step and the combat, the sword, the doors, the wake and
// death cameras, the audio and the HUD — in the order the loop always ran
// them. `in` is the frame's input (the live keys or an e2e script's); the
// result ends the stage (a request), the run (a script's check), and carries
// the frame's effect and sword light for the draw.
FrameResult frame_step(StageCtx& ctx, const FrameInput& in) {
    Renderer& renderer = ctx.renderer;
    AudioSeam& audio = ctx.audio;
    StageCarry& carry = ctx.carry;
    const StageId& here = ctx.here;
    Scene& scene = ctx.scene;
    ClubAttachment& club = ctx.club;
    CorpseAttachment& corpse = ctx.corpse;
    Heightmap& heightmap = ctx.heightmap;
    const bool& death_wired = ctx.death_wired;
    bool& return_wired = ctx.return_wired;
    const RigCpu& player_rig_record = *ctx.playerRigRecord;
    Camera& camera = ctx.camera;
    Vec3& stage_spawn = ctx.stage_spawn;
    float& stage_spawn_yaw = ctx.stage_spawn_yaw;
    PlayerState& player = ctx.player;
    PlayerCombat& combat = ctx.combat;
    Encounter& encounter = ctx.encounter;
    const CombatTuning& tuning = ctx.tuning;
    CameraClimb& climbCamera = ctx.climbCamera;
    CameraFollow& follow = ctx.follow;
    CameraTarget& targetCamera = ctx.targetCamera;
    Vec3& entityChest = ctx.entityChest;
    bool& haveEntityChest = ctx.haveEntityChest;
    const std::size_t& entityRig = ctx.entityRig;
    std::vector<Playback>& playbacks = ctx.playbacks;
    Playback& playback = *ctx.playbackPtr;
    EntityClips& entityClips = ctx.entityClips;
    ReturnTimings& returnTimings = ctx.returnTimings;
    const bool& manual_clip = ctx.manual_clip;
    const bool& valus_behavior = ctx.valus_behavior;
    const bool& s8_wired = ctx.s8_wired;
    bool& plain_ride = ctx.plain_ride;
    PlainRide& pr = ctx.pr;
    SwordState& sword = ctx.sword;
    bool& sword_wired = ctx.sword_wired;
    DeathState& death = ctx.play.death;
    bool& deathCueFired = ctx.play.deathCueFired;
    bool& deathCameraSet = ctx.play.deathCameraSet;
    Camera& deathCameraBase = ctx.play.deathCameraBase;
    const bool& audio_on = ctx.play.audioOn;
    std::vector<std::vector<Mat4>>& palettes = ctx.play.palettes;
    FrameResult result;
    const float dt = in.dt;
    const float drag_dx = in.dragDx;
    const float drag_dy = in.dragDy;
    const float wheel_delta = in.wheel;
    const bool climb_recenter = in.climbRecenter;
    // The stage still runs: no quit, R or script exit earlier this frame, and
    // no Load yet in this one.
    bool live = in.live;

    // S6: the death sequence's clock runs on the real frame time.
    // The defeat transition happens later in this frame (in
    // apply_combat_events), so the machine starts on the next frame
    // with total = 0 and does not accumulate the defeat frame's dt
    // (death.h's pinned rule). While it runs, the machine's clock
    // drives the one-shot death clip and gates the player input;
    // the corpse swap follows the same clock (advance_death_frame).
    if (death_wired && encounter.defeated && entityRig < playbacks.size()) {
        advance_death_frame(death, dt, playbacks[entityRig], entityClips.death,
                            corpse, club, scene, entityRig, ctx.enc());
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
        if (live && return_wants_load(before, carry.ret)) {
            result.request = StageRequest{false, StageId::Shrine};
            live = false;
        }
    }
    // The one input seam (FrameInput): the keyboard and gamepad or a script's;
    // --s7-e2e decides its own here, once the machines have stepped.
    PlayerInput input = in.input;
    const RideKeys rideKeys = in.rideKeys;  // S8: the plain's held ride keys
    const bool swordHeld = in.swordHeld;    // S9: C / the left shoulder, held
    if (in.afterMachines) {
        in.afterMachines(input);
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
    // S11: the ground stab roots the player while its clip plays.
    if (ctx.quad_wired && quadratus_stabbing(ctx.quad, combat)) {
        input.forward = input.back = input.left = input.right = false;
    }
    const bool moving = input.forward || input.back || input.left || input.right;
    if (in.afterLock) {
        if (const std::optional<int> code = in.afterLock(inputLocked, moving)) {
            result.exitCode = code;
            return result;
        }
    }

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
    if (death_wired && death_cue_due(death, deathCueFired, ctx.enc()) && audio_on) {
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
                player.position, ctx.enc().turnDegPerSec, valus_dt);
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
    // S11 (D9): Quadratus's fight — his clock, travel, choices and clip —
    // where Valus's would run (stage_setup decides: quad_wired).
    ctx.quadHit = QuadratusHit{};
    ctx.quadStab = QuadratusStab{};
    if (ctx.quad_wired) {
        ctx.quadFrame = quadratus_fight_step(ctx.quad, encounter, playbacks[entityRig], heightmap,
                                             player, combat,
                                             std::clamp(dt, 0.0f, kMaxCombatStepSeconds));
        update_entity_transform(scene, encounter, entityRig);
    }
    // S12 (Task 9): Gaius's fight — the world, his graph, his clip, his travel.
    ctx.knightHit = KnightHit{};
    ctx.knightHits.clear();
    ctx.knightBackstop = false;
    if (ctx.knight_wired) {
        // R-N: the S8 whistle key (Q / gamepad north) feeds his graph's whistle rows.
        knight_fight_step(ctx.knight, encounter, playbacks[entityRig], heightmap, player, combat,
                          rideKeys.whistle && !inputLocked,
                          std::clamp(dt, 0.0f, kMaxCombatStepSeconds));
        update_entity_transform(scene, encounter, entityRig);
    }
    // S7: while waking the wake clips own the player's clip.
    const bool wakeFrame = ctx.waking();
    if (wakeFrame) {
        ctx.pose_wake();
        if (in.onWake) {
            in.onWake();
        }
    } else if (!rideOwnsBody) {
        ctx.leave_wake();
        if (swordFoot) {
            // The sword's clip at the machine's clock (last frame's step).
            playback.index = clip_index_named(playback, sword.clip);
            playback.time = sword.clipTime;
            playback.pinned = true;
            playback.playing = false;
        } else if (ctx.quad_wired && quadratus_stabbing(ctx.quad, combat)) {
            // S11: the ground stab's clip at its own clock.
            if (playback.index != ctx.quad.stabClip) {
                select_clip(playback, scene, scene.playerRig, ctx.quad.stabClip);
            }
            playback.time = ctx.quad.stabSeconds;
            playback.pinned = true;
            playback.playing = false;
        } else {
            update_combat_clips(playback, scene, combat, input, manual_clip);
        }
    }
    palettes = update_all_poses(renderer, scene, playbacks, dt, club);
    if (ctx.knight_wired && entityRig < palettes.size()) {
        // S12: his palette with the switch's blend; the joints for the events.
        knight_fight_pose(ctx.knight, scene, entityRig, playbacks[entityRig],
                          std::clamp(dt, 0.0f, kMaxCombatStepSeconds), palettes[entityRig]);
        renderer.upload_pose(entityRig, palettes[entityRig]);
    }
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
                                               entityChest, ctx.enc());
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
                                     drag_dy, wheel_delta, ctx.enc());
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
    PlayerFrame frame =
        player_frame_of(scene, entityRig, palettes, heightmap,
                        camera_look_yaw(camera), ctx.enc(),
                        /*entityLive=*/!death_corpse_visible(death));
    if (ctx.quad_wired) {
        frame.shake = quadratus_shake(ctx.quad);  // S11: his refuse clips' drain
    }
    if (ctx.knight_wired) {
        // S12: his refuse clips' drain and the throw-off; the clip and frame the blade's
        // enable window reads (floor of clip seconds x 30).
        frame.shake = knight_fight_shake(ctx.knight);
        frame.entityClip = knight_clip_name(ctx.knight.core, ctx.knight.core.motion.clip);
        frame.entityFrame = static_cast<int>(std::floor(ctx.knight.core.motion.time * 30.0f));
    }
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
    if (in.lethalHit) {
        // An e2e script's one lethal hit, landed as a strike lands.
        events.damageDealt = 100;
    }
    if (events.damageDealt > 0 && entityRig < playbacks.size()) {
        if (ctx.quad_wired) {
            // S11: every hit goes through his gates first (the sigil order,
            // the hooves' kneels, nothing in Death); his fight is silent (D10).
            ctx.quadHit = quadratus_fight_hit(ctx.quad, encounter, playbacks[entityRig],
                                              entityClips, weakpoint,
                                              static_cast<float>(events.damageDealt),
                                              attached_before);
        } else if (ctx.knight_wired) {
            // S12: a sigil's pool (his table), HP 0 his death; silent (D10).
            ctx.knightHit = knight_fight_hit(ctx.knight, encounter, playbacks[entityRig],
                                             entityClips, weakpoint,
                                             static_cast<float>(events.damageDealt));
        } else {
            apply_combat_events(encounter, playbacks[entityRig], entityClips, events,
                                weakpoint, audio_on ? &frameSfx : nullptr);
        }
    }
    if (ctx.quad_wired) {
        // A swing released on him pauses the shake rule 3 s (mode 20, data);
        // a strike press on the ground is the stab at his hooves; his stomp's
        // impact lands after the player's step, like Valus's.
        if (attached_before && events.struck) {
            quadratus_fight_struck(ctx.quad);
        }
        ctx.quadStab = quadratus_ground_stab(ctx.quad, encounter, scene, entityRig,
                                             palettes[entityRig], heightmap, player, combat,
                                             input, frame.weakpointProxy);
        if (ctx.quadFrame.impactDue) {
            apply_valus_impact(combat, player, frame, tuning,
                               ctx.quadFrame.impactAttack == 0 ? ValusAttack::StompL
                                                               : ValusAttack::StompR,
                               ctx.enc(), audio_on ? &frameSfx : nullptr,
                               audio_on ? &events : nullptr);
        }
    }
    if (ctx.knight_wired) {
        // S12: his events (his damage, the spawned footsteps, the throw-off) after the
        // player's step, like Valus's impact; then the fall backstop (D9, kArena).
        ctx.knightHits = knight_fight_events(ctx.knight, player, combat, tuning,
                                             std::clamp(dt, 0.0f, kMaxCombatStepSeconds),
                                             audio_on ? &events : nullptr);
        if (combat.mode != PlayerMode::Dead &&
            player.position.y < encounters::knight_detail::kArena.fallBackstopY) {
            const float health = combat.health;
            reset_player_combat(player, combat, stage_spawn, stage_spawn_yaw, tuning);
            combat.health = health;
            ctx.knightBackstop = true;
        }
    }
    if (in.lethalHit && !encounter.defeated) {
        result.exitCode = in.lethalMissed();
        return result;
    }
    // Valus's impact runs after the player's step: a strike that
    // landed this frame has already cancelled the attack above, so
    // its pending impact cannot fire. The first frame inside the
    // window consumes it, even when the hit is then ignored.
    if (valus_behavior && encounter.attack != ValusAttack::None &&
        attack_impact_due(encounter.attack, encounter.attackSeconds, encounter.attackHit,
                          ctx.enc())) {
        encounter.attackHit = true;
        apply_valus_impact(combat, player, frame, tuning, encounter.attack, ctx.enc(),
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
                                     drag_dy, wheel_delta, ctx.enc());
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
    // The scripted look (--s9-e2e: onto Valus's chest from the last frame's
    // tip), after the follow camera, before the sword aims.
    if (in.lookAlong) {
        camera_look_along(camera, *in.lookAlong);
    }
    if (sword_wired) {
        camera_sword_shoulder(camera, sword.raise);
        result.probe = ctx.sword_frame(dt, swordHeld, inputLocked, palettes, result.sword);
    }
    // S8 (D14): the doors — the shrine's front exit and the
    // plain's shrine steps — fire on entry, on foot, through the
    // door machine; its Load ends the iteration for the other
    // stage (this frame still draws, faded).
    // R16: only while the stage still runs (R or Escape this frame
    // ended it), from the kDoors rows of this stage.
    if (s8_wired && ctx.door_frame(dt, positionBefore, riding, inputLocked, live)) {
        result.request = StageRequest{false, kDoors[carry.door.row].to};
        live = false;
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
                                       player.position, death.total, ctx.enc());
        }
        result.effect = build_effect_frame(scene, entityRig, palettes, encounter, death);
    }
    renderer.set_hud(ctx.hud_state(/*visible=*/true));
    return result;
}

// The interactive loop (and, through it, the --s7/--s8/--s9-e2e scripts): one
// stage until Escape/the close button (exitCode 0), a script's verdict, or a
// stage request (`next`; the scene is released).
SelftestResult run_interactive(StageCtx& ctx, RunState& run) {
    const AppOptions& opts = run.opts;
    Shell& shell = run.shell;
    Renderer& renderer = run.renderer;
    AudioSeam& audio = run.audio;
    StageCarry& carry = run.carry;
    const bool& sword_fx_ready = run.sword_fx_ready;
    const StageId& here = ctx.here;
    const char* const stage_name = ctx.stage_name;
    Scene& scene = ctx.scene;
    ClubAttachment& club = ctx.club;
    CorpseAttachment& corpse = ctx.corpse;
    Heightmap& heightmap = ctx.heightmap;
    const bool& follow_mode = ctx.follow_mode;
    const RunFlags& flags = ctx.flags;
    const bool& death_wired = ctx.death_wired;
    bool& return_wired = ctx.return_wired;
    Camera& camera = ctx.camera;
    const DoorRow* const arrival = ctx.arrival;
    const bool& door_arrival = ctx.door_arrival;
    PlayerState& player = ctx.player;
    PlayerCombat& combat = ctx.combat;
    Encounter& encounter = ctx.encounter;
    CameraClimb& climbCamera = ctx.climbCamera;
    CameraFollow& follow = ctx.follow;
    CameraTarget& targetCamera = ctx.targetCamera;
    Vec3& entityChest = ctx.entityChest;
    bool& haveEntityChest = ctx.haveEntityChest;
    const std::size_t& playerRig = ctx.playerRig;
    const std::size_t& entityRig = ctx.entityRig;
    std::vector<Playback>& playbacks = ctx.playbacks;
    Playback& playback = *ctx.playbackPtr;
    EntityClips& entityClips = ctx.entityClips;
    bool& plain_ride = ctx.plain_ride;
    PlainRide& pr = ctx.pr;
    SwordState& sword = ctx.sword;
    bool& sword_wired = ctx.sword_wired;

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
    // The frame state (FrameState, stage_ctx.h): the death machine and its
    // latches, the audio switch, the poses.
    DeathState& death = ctx.play.death;
    bool& deathCueFired = ctx.play.deathCueFired;
    bool& deathCameraSet = ctx.play.deathCameraSet;
    ctx.play.audioOn = audio.enabled;
    GamepadInput gamepad;
    open_first_gamepad(gamepad);
    ctx.play.palettes = update_all_poses(renderer, scene, playbacks, /*dt=*/0.0f, club);
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
    // --s11-e2e (Task 10): the shared S11 script (s11_script.h) picks the keys;
    // the loop below is the interactive one; R is the key's own handler.
    S11Script s11s;
    s11s.prefix = "s11e2e";
    if (opts.s11E2e && !s11_start(s11s, ctx, ctx.loadMs)) {
        std::fprintf(stderr, "sotc-viewer: --s11-e2e: the --manifest arena is not Quadratus's "
                             "(encounter quadratus, his bake) or his fight is unwired\n");
        return 1;
    }
    // --s12-e2e (Task 9): the shared S12 script (s12_script.h), the same way.
    S12Script s12s;
    s12s.prefix = "s12e2e";
    if (opts.s12E2e && !s12_start(s12s, ctx, ctx.loadMs)) {
        std::fprintf(stderr, "sotc-viewer: --s12-e2e: the --manifest arena is not Gaius's "
                             "(encounter knight, his bake and graph) or his fight is unwired\n");
        return 1;
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
                            camera_target_toggle(camera, follow, targetCamera, entityChest,
                                                 ctx.enc());
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
            // The frame's input (FrameInput): the keyboard and gamepad, or an e2e
            // script's keys, look and lethal hit; --s7-e2e's mid-frame checks are hooks.
            const auto deadzoned = [](float value) {
                return std::fabs(value) > kS41StickDeadzone ? value : 0.0f;
            };
            FrameInput in;
            in.dt = dt;
            in.live = running;
            if (opts.s8E2e) {
                in.input = s8In;
                in.rideKeys = s8Keys;
            } else if (opts.s9E2e) {
                in.input = s9In;
                in.rideKeys = s9Keys;
                in.swordHeld = s9Held;
            } else if (opts.s11E2e) {
                in.input = s11_input(s11s, ctx);
            } else if (opts.s12E2e) {
                in.input = s12_input(s12s, ctx);
            } else if (!opts.s7E2e) {
                const bool* keys = SDL_GetKeyboardState(nullptr);
                // S8 (G4): W kicks, S reins, A/D steer, E mounts and
                // dismounts, Q whistles; the gamepad's left stick forward
                // kicks, back reins, X steers, south mounts, north whistles.
                in.rideKeys.kick =
                    keys[SDL_SCANCODE_W] || gamepad.leftY < -kStickPressThreshold;
                in.rideKeys.rein =
                    keys[SDL_SCANCODE_S] || gamepad.leftY > kStickPressThreshold;
                in.rideKeys.mount = keys[SDL_SCANCODE_E] || gamepad.south;
                in.rideKeys.whistle = keys[SDL_SCANCODE_Q] || gamepad.north;
                const float steerKeys = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) -
                                        (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
                in.rideKeys.steer = std::clamp(steerKeys + gamepad.leftX, -1.0f, 1.0f);
                in.input.forward = keys[SDL_SCANCODE_W] || deadzoned(gamepad.leftY) < 0.0f;
                in.input.back = keys[SDL_SCANCODE_S] || deadzoned(gamepad.leftY) > 0.0f;
                in.input.left = keys[SDL_SCANCODE_A] || deadzoned(gamepad.leftX) < 0.0f;
                in.input.right = keys[SDL_SCANCODE_D] || deadzoned(gamepad.leftX) > 0.0f;
                in.input.run = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
                in.input.grab = keys[SDL_SCANCODE_E] || gamepad.grab;
                in.input.strike = keys[SDL_SCANCODE_J] || gamepad.strike;
                in.swordHeld = keys[SDL_SCANCODE_C] || gamepad.raise;
            }
            // The right stick feeds the cameras' drag channel; clamp the
            // frame time so a hitch cannot fling the view.
            const float stick_dt = std::clamp(dt, 0.0f, 0.05f);
            drag_dx += deadzoned(gamepad.rightX) * kS41StickPixelsPerSecond * stick_dt;
            drag_dy += deadzoned(gamepad.rightY) * kS41StickPixelsPerSecond * stick_dt;
            in.dragDx = drag_dx;
            in.dragDy = drag_dy;
            in.wheel = wheel_delta;
            in.climbRecenter = climb_recenter;
            // --s7-e2e watches the transitions the machines just made (until its R),
            // holds W while the return should lock it (the lock must eat it) and
            // expects the lock from FadeOut through Wake.
            bool e2eExpectLocked = false;
            if (opts.s7E2e) {
                in.afterMachines = [&](PlayerInput& input) {
                    if (carry.e2eStep < 2) {
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
                    }
                    input.forward = e2eExpectLocked;
                };
                in.afterLock = [&](bool inputLocked, bool moving) -> std::optional<int> {
                    if (carry.e2eStep < 2) {
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
                    return std::nullopt;
                };
                in.onWake = [&]() {
                    if (carry.ret.phase == ReturnPhase::Wake && playback.index != e2e.wakeClip) {
                        e2e.wakeClip = playback.index;
                        ++e2e.wakeClips;
                        std::printf("s7e2e-wake clip=%s t=%.3f\n",
                                    playback.clips[static_cast<std::size_t>(playback.index)]
                                        .name.c_str(),
                                    ctx.wake_clock());
                    }
                };
            }
            // The scripts' one lethal hit: --s7-e2e's 1 s into the arena, --s9-e2e's
            // 1 s into the arena entered through the corridor.
            const bool e2eHitNow = opts.s7E2e && carry.e2eStep < 2 && here == StageId::Arena &&
                                   !e2e.hit && e2e.stageSeconds + kS7Eps >= kS7E2eLeadSeconds;
            const bool s9HitNow = opts.s9E2e && here == StageId::Arena && !s9e.hit &&
                                  s9e.seconds + kS7Eps >= kS7E2eLeadSeconds;
            if (e2eHitNow || s9HitNow) {
                in.lethalHit = true;
                if (s9HitNow) {
                    s9e.hit = true;
                    in.lethalMissed = [&]() { return s9e_fail("the lethal hit did not defeat Valus"); };
                } else {
                    e2e.hit = true;
                    in.lethalMissed = [&]() { return e2e_fail("the lethal hit did not defeat Valus"); };
                }
            }
            // --s9-e2e: the look onto Valus's chest from the last frame's tip.
            if (opts.s9E2e && s9e.aim) {
                const Vec3 chest = valus_chest();
                in.lookAlong = vec3_normalize(Vec3{chest.x - s9e.probe.tip.x,
                                                   chest.y - s9e.probe.tip.y,
                                                   chest.z - s9e.probe.tip.z});
            }
            const FrameResult result = frame_step(ctx, in);
            if (result.exitCode) {
                return *result.exitCode;
            }
            if (opts.s11E2e) {
                std::optional<int> code = s11_after(s11s, ctx);
                if (!code && s11s.wantR) {
                    press_r_key();  // the R key's own handler
                    s11s.wantR = false;
                    code = s11_after(s11s, ctx);
                }
                if (code) {
                    if (*code != 0) {
                        return *code;
                    }
                    std::printf("s11e2e-done frames=%d seconds=%.1f\n", s11s.frames, s11s.seconds);
                    print_status_line(opts, shell.win);
                    return 0;
                }
            }
            if (opts.s12E2e) {
                std::optional<int> code = s12_after(s12s, ctx);
                if (!code && s12s.wantR) {
                    press_r_key();  // the R key's own handler
                    s12s.wantR = false;
                    code = s12_after(s12s, ctx);
                }
                if (code) {
                    if (*code != 0) {
                        return *code;
                    }
                    std::printf("s12e2e-done frames=%d seconds=%.1f\n", s12s.frames, s12s.seconds);
                    print_status_line(opts, shell.win);
                    return 0;
                }
            }
            if (result.request) {
                request = *result.request;
                running = false;
            }
            if (result.probe) {
                s9e.probe = *result.probe;
            }
            effectFrame = result.effect;
            if (effectFrame.active) {
                effectFramePtr = &effectFrame;
            }
            swordFrame = result.sword;
            if (swordFrame.active) {
                swordFramePtr = &swordFrame;
            }
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
    return SelftestResult::next_stage(request.next);
}
