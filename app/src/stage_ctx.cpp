#include "stage_ctx.h"

#include "door.h"
#include "ride_points.h"
#include "road_points.h"
#include "shrine_points.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

// One HUD view of the run for every set_hud site (design §7.7). The
// overlay belongs to an encounter, so a manifest with no entity draws
// none; Valus's bar is zero-width until the first landed hit, so its
// appearance is itself the feedback that he can be hurt.
HudState StageCtx::hud_state(bool visible) {
    const float playerHealth =
        tuning.maxHealth > 0.0f ? combat.health / tuning.maxHealth : 0.0f;
    const float maxHealth = enc().maxHealth;
    const float valusHealth = encounter.health < maxHealth ? encounter.health / maxHealth : 0.0f;
    HudState hud{visible && !manifest.entities.empty(), combat.stamina / 100.0f,
                 valusHealth, playerHealth};
    // S7: the return's full-window fade (0 = nothing drawn — every
    // run that never steps the machine).
    hud.fade = carry.ret.fade;
    hud.fadeColour = kShrineFadeColour;
    // S8 (D14): the door's fade is the larger of the two, black.
    if (carry.door.fade > hud.fade) {
        hud.fade = carry.door.fade;
        hud.fadeColour = kDoorFadeColour;
    }
    return hud;
}

Mat4 StageCtx::horse_model() {
    const float half = 0.5f * pr.horseYaw * kDegToRad;
    return mat4_from_trs(pr.horsePos, Quat{0.0f, std::sin(half), 0.0f, std::cos(half)},
                         pr.horseScale);
}

float StageCtx::horse_distance() {
    const float dx = player.position.x - pr.horsePos.x;
    const float dz = player.position.z - pr.horsePos.z;
    return std::sqrt(dx * dx + dz * dz);
}

// One plain frame, after this frame's update_all_poses: the presses,
// the whistle, the ride and Agro's machines, her travel over the
// ground, both rigs' blended poses, the rider on the saddle, and the
// cues. `locked` (a door fade) eats every press; a key held through
// it is not a fresh press when it ends (ride_presses).
void StageCtx::plain_step(float dt, RideKeys keys, bool locked, std::vector<std::vector<Mat4>>& palettes) {
    Playback& playback = *playbackPtr;
    const RigCpu& player_rig_record = *playerRigRecord;
    RideInput in = ride_presses(keys, pr.rideKeysBefore, locked);
    in.mountPressed = in.mountPressed && pr.whistleClip < 0;
    // S9: while the sword is up, kicks, the mount/dismount and the
    // whistle are ignored (release C to ride); `before` still tracks
    // the real keys, so a key held through the raise is no press.
    if (sword.phase != SwordPhase::Lowered) {
        in.kickPressed = in.mountPressed = in.whistlePressed = false;
    }
    // R11: a refused ridden move holds the rein (the stop chain runs).
    in = blocked_rein(in, pr.ride, pr.rideBlocked);

    // The whistle (D12): Wander's one-shot (`_near` within 15 m), on foot.
    bool whistled = false;
    if (pr.ride.phase == RidePhase::OnFoot && pr.whistleClip < 0 && in.whistlePressed) {
        pr.whistleClip = clip_index_named(playback, horse_distance() <= kWhistleNearRange
                                                     ? kWhistleNearClip
                                                     : kWhistleClip);
        pr.whistleSeconds = 0.0f;
        whistled = true;
        audio.play_ride(RideSfx::Whistle, player.position);
    } else if (pr.whistleClip >= 0) {
        pr.whistleSeconds += dt;
    }
    if (pr.whistleClip >= 0) {
        const AnimationCpu& whistle =
            playback.clips[static_cast<std::size_t>(pr.whistleClip)];
        if (pr.whistleSeconds >= whistle.duration) {
            pr.whistleClip = -1;  // the mode's clip takes over next frame
            playback.pinned = false;
            playback.playing = true;
        } else {
            playback.index = pr.whistleClip;
            playback.time = pr.whistleSeconds;
            playback.pinned = true;
            playback.playing = false;
        }
    }

    // The ride machine (D5, D9-D11): the player in Agro's frame, and
    // whether each side's landing has walkable ground.
    RideContext ctx;
    ctx.playerInHorse = to_body_frame(player.position, pr.horsePos, pr.horseYaw);
    const auto landing_walkable = [&](Side side) {
        Vec3 land = from_body_frame(dismount_offset(side), pr.horsePos, pr.horseYaw);
        land.y = pr.horsePos.y;
        return heightmap_in_bounds(heightmap, land) &&
               heightmap_walkable(heightmap, pr.horsePos, land);
    };
    ctx.leftWalkable = landing_walkable(Side::Left);
    ctx.rightWalkable = landing_walkable(Side::Right);
    if (pr.ride.phase == RidePhase::OnFoot) {
        pr.ride.yawDeg = pr.horseYaw;  // the machine reads her heading at the mount
    }
    const RideState before = pr.ride;
    pr.ride = step_ride(pr.ride, dt, in, ctx);
    const auto counted_kicks = [](const RideState& s) {
        return std::count_if(s.kicks.begin(), s.kicks.end(),
                             [](float k) { return k >= 0.0f; });
    };
    if (before.phase == RidePhase::OnFoot && pr.ride.phase == RidePhase::Mounting) {
        // The mount (D11): Agro's own machine is reset; the rider's
        // blend starts from the pose shown on foot.
        pr.agro = AgroState{};
        pr.whistleClip = -1;
        if (playback.index >= 0) {
            const AnimationCpu& shown =
                playback.clips[static_cast<std::size_t>(playback.index)];
            pr.riderBlend.shown = animation_local_pose(
                scene, playerRig, shown, playback_time(playback, shown.duration));
        }
        pr.riderBlend.clip.clear();
        pr.mountFootPending = true;  // R12
        pr.mountFootWorld = player.position;
        audio.play_ride(RideSfx::Mount, pr.horsePos);
    }
    if (in.kickPressed && (counted_kicks(pr.ride) > counted_kicks(before) ||
                           pr.ride.kickCooldown > before.kickCooldown)) {
        audio.play_ride(RideSfx::Kick, pr.horsePos);
    }
    if (pr.ride.kickRefused) {
        audio.play_ride(RideSfx::Neigh, pr.horsePos);  // the binary's refusal neigh
    }
    if (pr.ride.horseClip != before.horseClip &&
        (pr.ride.horseClip == "horse_a_uma_run_stop1" ||
         pr.ride.horseClip == "horse_a_uma_run_n_stop" ||
         pr.ride.horseClip == "horse_a_uma_walk_stop")) {
        audio.play_ride(RideSfx::StopHorse, pr.horsePos);  // the "whoa"
    }

    // Agro's heading and speed: the rider's machine while ridden, her
    // own whistle approach on foot (turning to the player, D12).
    const float distanceBefore = horse_distance();
    float speed = 0.0f;
    if (pr.ride.phase == RidePhase::OnFoot) {
        const AgroPhase was = pr.agro.phase;
        pr.agro = step_agro(pr.agro, dt, whistled, distanceBefore, pr.agroProgressed);
        if (before.phase == RidePhase::Dismounting) {
            // Off the saddle: her idle carries on from the ride's clock.
            pr.agro.clip = kAgroStandClip;
            pr.agro.clipTime = before.clipTime + dt;
        }
        if (pr.agro.warp) {
            // The binary's stuck recovery: kWarpBehind behind the player,
            // only onto ground (a hole is never sampled).
            Vec3 target = player.position;
            const Vec3 f = yaw_forward(player.yawDeg);
            target.x -= f.x * kWarpBehind;
            target.z -= f.z * kWarpBehind;
            if (heightmap_in_bounds(heightmap, target) &&
                heightmap_walkable(heightmap, target, target)) {
                const float floor = heightmap_floor(heightmap, target, kFloorStep);
                if (std::isfinite(floor)) {
                    pr.horsePos = Vec3{target.x, floor, target.z};
                }
            }
        }
        if (was != AgroPhase::Neigh && pr.agro.phase == AgroPhase::Neigh) {
            audio.play_ride(RideSfx::Neigh, pr.horsePos);
        }
        float facing = 1.0f;
        if (pr.agro.phase == AgroPhase::Approach) {
            const float want = std::atan2(player.position.x - pr.horsePos.x,
                                          player.position.z - pr.horsePos.z) /
                               kDegToRad;
            float err = std::remainder(want - pr.horseYaw, 360.0f);
            const float turn = kApproachTurnDegPerSecond * dt;
            pr.horseYaw = std::remainder(pr.horseYaw + std::clamp(err, -turn, turn), 360.0f);
            err = std::remainder(want - pr.horseYaw, 360.0f);
            // She runs only once she faces him (no orbiting him).
            facing = std::max(0.0f, std::cos(err * kDegToRad));
        }
        speed = horse_clip_speed(pr.agro.clip, pr.agro.clipTime) * facing;
    } else {
        pr.horseYaw = pr.ride.yawDeg;
        speed = pr.ride.speed;
    }
    bool refused = false;
    if (speed != 0.0f && dt > 0.0f) {
        const Vec3 f = yaw_forward(pr.horseYaw);
        Vec3 next{pr.horsePos.x + f.x * speed * dt, pr.horsePos.y,
                  pr.horsePos.z + f.z * speed * dt};
        // The player's walkability (D3): no hole, no cliff, no wall.
        if (heightmap_in_bounds(heightmap, next) &&
            heightmap_walkable(heightmap, pr.horsePos, next)) {
            const float floor = heightmap_floor(heightmap, next, kFloorStep);
            if (std::isfinite(floor)) {
                next.y = floor;
            }
            pr.horsePos = next;
        } else {
            refused = true;
        }
    }
    pr.rideBlocked = refused && pr.ride.phase == RidePhase::Riding;
    pr.agroProgressed = horse_distance() < distanceBefore - 1e-4f;

    // Agro's rig: placed, posed with the blend, hooves on touchdown.
    // Every Agro clip carries the root series (its ground height), so
    // her model takes no lift.
    const RigCpu& horseRecord = scene.rigs[horseRig];
    set_rig_model(scene, horseRecord, horse_model());
    const bool onFoot = pr.ride.phase == RidePhase::OnFoot;
    const std::string& horseClip = onFoot ? pr.agro.clip : pr.ride.horseClip;
    const float horseTime = onFoot ? pr.agro.clipTime : pr.ride.clipTime;
    const float horseBlendFrom = onFoot ? pr.agro.blendFrom : pr.ride.blendFrom;
    RideSfx hoof = RideSfx::HoofWalk;
    const float* fractions = footfall_fractions(horseClip, hoof);
    const int horseClipIndex = clip_index_named(playbacks[horseRig], horseClip);
    if (fractions != nullptr && horseClipIndex >= 0) {
        const AnimationCpu& gaitClip =
            playbacks[horseRig].clips[static_cast<std::size_t>(horseClipIndex)];
        const float prev = horseClip == pr.hoofClip ? pr.hoofTime : -1e-4f;
        for (int n = footfalls_crossed(fractions, gaitClip.duration, prev, horseTime);
             n > 0; --n) {
            audio.play_ride(hoof, pr.horsePos);
        }
    }
    pr.hoofClip = horseClip;
    pr.hoofTime = horseTime;
    std::vector<Mat4> horsePalette =
        blended_palette(scene, horseRig, playbacks[horseRig], pr.horseBlend, horseClip,
                        horseTime, horseBlendFrom);
    if (!horsePalette.empty()) {
        renderer.upload_pose(horseRig, horsePalette);
        palettes[horseRig] = std::move(horsePalette);
    }

    if (before.phase == RidePhase::Dismounting && onFoot) {
        // On foot at the dismount's end (D11): the last getoff_fall
        // frame, over the seat, on the ground.
        Mat4 seat{};
        Vec3 land = from_body_frame(dismount_offset(pr.ride.side), pr.horsePos, pr.horseYaw);
        if (rig_bone_world(scene, horseRig, palettes[horseRig], kRiderBone, seat)) {
            const Mat4 at = mat4_mul(seat, translation_of(dismount_offset(pr.ride.side)));
            land = Vec3{at.m[12], at.m[13], at.m[14]};
        }
        const float floor = heightmap_floor(heightmap, land, kFloorStep);
        land.y = std::isfinite(floor) ? floor : pr.horsePos.y;
        player = PlayerState{land, pr.horseYaw};
        playback.pinned = false;
        playback.playing = true;
        playback.time = 0.0f;
        follow.distance = CameraFollow{}.distance;
    }

    // The rider (D8): the paired clip at the ride's clock, placed on
    // OptBone_boy's world matrix times the seat-relative offset.
    if (!onFoot) {
        // S9: a sword raised in the saddle overrides the rider's clip
        // (kSwordMounted at Stand), blended in over its first 0.25 s.
        const bool swordRider = sword.phase != SwordPhase::Lowered && sword.mounted;
        const std::string riderClip = swordRider ? sword.clip : pr.ride.riderClip;
        const float riderTime = swordRider ? sword.clipTime : pr.ride.clipTime;
        const float riderBlendFrom =
            !swordRider ? pr.ride.blendFrom
            : sword.phase != SwordPhase::Raised && sword.clipTime < kClipBlendSeconds
                ? sword.clipTime
                : -1.0f;
        std::vector<Mat4> riderPalette =
            blended_palette(scene, playerRig, playback, pr.riderBlend, riderClip,
                            riderTime, riderBlendFrom);
        if (!riderPalette.empty()) {
            renderer.upload_pose(playerRig, riderPalette);
            palettes[playerRig] = std::move(riderPalette);
        }
        if (const int riderIndex = clip_index_named(playback, riderClip);
            riderIndex >= 0) {
            playback.index = riderIndex;  // the status and the clip owner
            playback.time = riderTime;
            playback.pinned = true;
            playback.playing = false;
        }
        Mat4 seat{};
        if (rig_bone_world(scene, horseRig, palettes[horseRig], kRiderBone, seat)) {
            if (pr.mountFootPending) {
                // R12: where he stood, in the seat's frame (y from the clip).
                pr.mountFoot = point_in_frame(seat, pr.mountFootWorld);
                pr.mountFoot.y = 0.0f;
                pr.mountFootPending = false;
            }
            const Vec3 offset = mount_offset(pr.ride, pr.mountFoot);
            set_rig_model(scene, player_rig_record,
                          mat4_mul(seat, translation_of(offset)));
        }
        player.position = pr.horsePos;  // the listener, the camera, R
        player.yawDeg = pr.horseYaw;
    }

    // The riding camera (D13): the distance per gait (eased by the
    // follow camera), the head point raised while mounted, eased.
    follow.distance = onFoot ? follow.distance
                      : pr.ride.gait == Gait::Gallop ? kRideCameraGallop
                      : pr.ride.gait == Gait::Trot   ? kRideCameraTrot
                                                  : kRideCameraNear;
    const float headTarget = kFollowHeadHeight + (onFoot ? 0.0f : kRideHeadLift);
    const float ease = 1.0f - std::exp(-kRideCameraEase * std::max(dt, 0.0f));
    follow.headHeight += (headTarget - follow.headHeight) * ease;
}

// The one soft reset (design §7.5 and S6): the player's and Valus's
// opening state, the death machine back to Idle, the corpse hidden and
// the live body shown, the cameras back to the follow pose. The
// interactive `R` handler and the S6 selftest **both call this**, so
// the acceptance proves the reset the player actually presses
// (anti-goal A2) instead of a copy of it. `cameraBaseSet` is the
// interactive path's cinematic-camera latch (the selftest has no such
// latch and passes nullptr).
void StageCtx::soft_reset_encounter(DeathState& deathState, bool& cueFired, bool* cameraBaseSet) {
    Playback& playback = *playbackPtr;
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
}

// --- S7: R, the wake, and the return's presentation ------------------
// `R` anywhere (design D11): the return machine goes back to Idle;
// in the arena it is the S6 soft reset above, and from the shrine
// (or anywhere but the arena) it reloads the arena — the new
// iteration is the opening state. Returns true when the caller must
// end the iteration with a request for the arena.
bool StageCtx::press_reset(DeathState& deathState, bool& cueFired, bool* cameraBaseSet) {
    carry.ret = reset_return();
    // S8: R from mid-door, mounted or anywhere off the arena leaves
    // both machines idle and the rider detached (the arena reloads).
    carry.door = reset_door();
    pr.ride = reset_ride();
    carry.horse.reset();  // S9 (D4): R forgets where Agro was left
    // S9: R lowers the sword at once and ends its shimmer.
    sword = reset_sword();
    swordSearchLatch = false;
    audio.stop_sword();
    if (here != StageId::Arena) {
        return true;
    }
    soft_reset_encounter(deathState, cueFired, cameraBaseSet);
    return false;
}

// S9 (D5-D7): one sword frame — the sword in the hand, its beam, the
// shade and the lock, the machine, the cues and the light — after the
// rig's model is final for this frame (on foot or in the saddle). The
// interactive loop and --s9-selftest both call it; `fx` is this frame's
// light (active only while the sword is up and the pass is ready).
SwordProbe StageCtx::sword_frame(float dt, bool held, bool inputLocked, const std::vector<std::vector<Mat4>>& palettes, SwordFxFrame& fx) {
    Playback& playback = *playbackPtr;
    SwordProbe probe;
    Mat4 bone = mat4_identity();
    probe.tip = player.position;
    if (rig_bone_world(scene, playerRig, palettes[playerRig], kSwordBone, bone)) {
        bone = mat4_mul(bone, sword_grip(sword.raise));
        for (std::size_t i = swordMeshBegin; i < swordMeshEnd; ++i) {
            scene.meshes[i].model = bone;
        }
        const float* t = road_points::kSwordTip;
        probe.tip = Vec3{bone.m[0] * t[0] + bone.m[4] * t[1] + bone.m[8] * t[2] + bone.m[12],
                         bone.m[1] * t[0] + bone.m[5] * t[1] + bone.m[9] * t[2] + bone.m[13],
                         bone.m[2] * t[0] + bone.m[6] * t[1] + bone.m[10] * t[2] + bone.m[14]};
    }
    const Vec3 tip = probe.tip;
    // The camera already carries the shoulder shift (camera_sword_shoulder).
    const Vec3 eye = camera_eye(camera);
    const Vec3 aim = vec3_normalize(Vec3{camera.target.x - eye.x,
                                         camera.target.y - eye.y,
                                         camera.target.z - eye.z});
    const SwordBeam beam = sword_beam(tip, aim, valus_chest(), sword.raise);
    probe.beam = beam;
    // Every march: a unit dir, step <= cellSize/2, ground and
    // backdrops combined with fmin (NaN = no hit; B4).
    constexpr float kStep = 0.5f;
    const auto march = [&](Vec3 dir, float length) {
        return std::fmin(ray_march(heightmap, tip, dir, length, kStep),
                         ray_march(raygrid, tip, dir, length, kStep));
    };
    const bool sunlit = std::isnan(march(kSunDir, kShadeRayMetres));
    probe.sunlit = sunlit;
    // The locked ray only while lit: a dark, lowered or shaded
    // beam passes NaN (the spark gate is raise > 0).
    const float hitMetres =
        beam.locked && sunlit && sword_intensity(sword) > 0.0f
            ? march(beam.axis, beam.rayLength)
            : std::numeric_limits<float>::quiet_NaN();
    probe.hit = hitMetres;
    // allowed: on foot (not whistling), or riding at Stand — and
    // a raise keeps the mount it was latched with, so a dismount
    // (or a gait) while raised lowers it.
    const bool footOk =
        !plain_ride || (pr.ride.phase == RidePhase::OnFoot && pr.whistleClip < 0);
    const bool standOk = plain_ride && pr.ride.phase == RidePhase::Riding &&
                         pr.ride.gait == Gait::Stand;
    SwordInput swordIn;
    swordIn.held = held && !inputLocked;
    swordIn.mounted = standOk;
    swordIn.allowed = sword.phase == SwordPhase::Lowered ? footOk || standOk
                      : sword.mounted                   ? standOk
                                                        : footOk;
    const SwordState swordBefore = sword;
    sword = step_sword(sword, dt, swordIn, beam, sunlit, hitMetres);
    const bool up = sword.phase != SwordPhase::Lowered;
    if (swordBefore.phase == SwordPhase::Lowered && up) {
        audio.play_sword(SwordSfx::Shimmer, tip);
    }
    if (swordBefore.phase != SwordPhase::Lowering &&
        sword.phase == SwordPhase::Lowering) {
        audio.stop_sword();
    }
    // Once per lock while rising or raised and lit; the latch
    // clears only past 2 kLockK (no jitter at the edge, no
    // replay while lowering).
    const bool lockLit = (sword.phase == SwordPhase::Raising ||
                          sword.phase == SwordPhase::Raised) &&
                         beam.locked && sunlit && sword_intensity(sword) > 0.0f;
    if (lockLit && !swordSearchLatch) {
        audio.play_sword(SwordSfx::Search, tip);
        swordSearchLatch = true;
    } else if (beam.k > 2.0f * kLockK) {
        swordSearchLatch = false;
    }
    if (swordBefore.phase != SwordPhase::Lowered && !up && !swordBefore.mounted) {
        playback.pinned = false;  // the mode's clip takes over next frame
        playback.playing = true;
    }
    fx = SwordFxFrame{};
    if (up && sword_fx_ready) {
        fx.active = true;
        fx.tip = tip;
        fx.lines = beam.lines;
        fx.length =
            std::isfinite(hitMetres) ? hitMetres : beam.rayLength;  // clipped at a lock
        fx.width = sword.width;
        fx.alpha = sword_intensity(sword);
        fx.hit = std::isfinite(hitMetres);
        fx.hitPoint = Vec3{tip.x + beam.axis.x * hitMetres,
                           tip.y + beam.axis.y * hitMetres,
                           tip.z + beam.axis.z * hitMetres};
        fx.atlasCols = road_points::kSwordAtlasGrid[0];
        fx.atlasRows = road_points::kSwordAtlasGrid[1];
    }
    return probe;
}

// S8 (D14): the doors — the shrine's front exit and the plain's shrine
// steps, S9's corridor and arena doors — fire on entry, on foot, through
// the door machine; true when its Load must end the iteration for the
// other stage (this frame still draws, faded). R16: an entry only while
// the stage still runs (`live`: R or Escape this frame ended it), from
// the kDoors rows of this stage. The interactive loop and --s9-selftest
// both call it.
bool StageCtx::door_frame(float dt, Vec3 positionBefore, bool riding, bool inputLocked, bool live) {
    // S9: the corridor and arena rows only when S9-wired into
    // Valus's arena (door_rows).
    // S9 (Task 7 review): no door while the return runs (its
    // 2 s Hold leaves the input unlocked).
    const bool returnSettled = carry.ret.phase == ReturnPhase::Idle ||
                               carry.ret.phase == ReturnPhase::Free;
    const int row = live && !riding && !inputLocked && returnSettled
                        ? door_entered_row(here, positionBefore, player.position,
                                           door_rows)
                        : -1;
    const DoorState doorBefore = carry.door;
    carry.door = step_door(carry.door, dt, row >= 0, row, /*loadDone=*/false);
    if (!live || !door_wants_load(doorBefore, carry.door)) {
        return false;
    }
    // S9 (D4, a labelled deviation: either door): leaving the road,
    // Agro stays where she is now.
    if (road && plain_ride) {
        carry.horse = HorsePose{pr.horsePos, pr.horseYaw};
    }
    return true;
}

// The wake (D9, ruling R5): FadeIn shows its first frame, Wake plays
// cut04 then cut05 once, pinned to the machine's clock, in place at
// the spawn. The clips carry no root series (the pelvis sits at the
// model origin), so the body is lifted by the pose's own pelvis
// height: the lowest of the recon's body joints is planted on the
// floor every frame — the measure `wake.pelvis_m` was taken with
// (0.10 m lying -> 0.72 m standing; --s7-selftest checks it).
bool StageCtx::waking() {
    return return_wired && here == StageId::Shrine &&
           (carry.ret.phase == ReturnPhase::FadeIn ||
            carry.ret.phase == ReturnPhase::Wake);
}

float StageCtx::wake_clock() {
    return carry.ret.phase == ReturnPhase::Wake ? carry.ret.t : 0.0f;
}

void StageCtx::pose_wake() {
    Playback& playback = *playbackPtr;
    const float t = wake_clock();
    const bool first = t < wakeFirstSeconds;
    playback.index = first ? wakeFirst : wakeSecond;
    playback.time = first ? t : t - wakeFirstSeconds;
    playback.pinned = true;
    playback.playing = false;
}

// Leaving the wake: the clip clock runs again and the mode's clip
// (the stand loop) takes over on the next update_combat_clips.
void StageCtx::leave_wake() {
    Playback& playback = *playbackPtr;
    if (wakeFirst >= 0 && (playback.index == wakeFirst || playback.index == wakeSecond)) {
        playback.pinned = false;
        playback.playing = true;
    }
}

float StageCtx::wake_plant_lift() {
    Playback& playback = *playbackPtr;
    const RigCpu& player_rig_record = *playerRigRecord;
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
}

float StageCtx::player_lift() {
    Playback& playback = *playbackPtr;
    const RigCpu& player_rig_record = *playerRigRecord;
    return waking() ? wake_plant_lift()
                    : active_player_lift(scene, playback, player_rig_record);
}
