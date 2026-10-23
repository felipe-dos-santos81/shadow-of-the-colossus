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

constexpr float kS8MountStillSeconds = 0.8f;   // mount.png: mid stand_ride_l
constexpr float kS8GaitSeconds = 2.0f;         // each loop's speed window
constexpr float kS8Tolerance = 0.05f;          // ±5 % on the speeds and the yaw rate
constexpr float kS8SteerRampSeconds = 0.25f;   // the steer blend settles first
constexpr float kS8SteerSeconds = 2.0f;
constexpr float kS8BackSeconds = 0.5f;         // the back loop's speed window
constexpr float kS8EdgeKickGapSeconds = 0.6f;  // walk -> trot toward the edge
constexpr float kS8MaxEdgeSeconds = 30.0f;     // the 40 m edge probe, ridden or on foot
constexpr float kS8RiderOffsetMax = 0.10f;     // m: the pelvis's departure from its seated rest
constexpr int kS8PerfFrames = 240;

}  // namespace

// --s8-selftest: the S8 plain timeline (one stage per call; carry.s8Step).
SelftestResult run_s8(StageCtx& ctx, RunState& run) {
    const AppOptions& opts = run.opts;
    Shell& shell = run.shell;
    Renderer& renderer = run.renderer;
    AudioSeam& audio = run.audio;
    StageCarry& carry = run.carry;
    StageId& stage = run.stage;
    const StageId& here = ctx.here;
    Scene& scene = ctx.scene;
    ClubAttachment& club = ctx.club;
    Heightmap& heightmap = ctx.heightmap;
    const RigCpu& player_rig_record = *ctx.playerRigRecord;
    Camera& camera = ctx.camera;
    Vec3& stage_spawn = ctx.stage_spawn;
    PlayerState& player = ctx.player;
    PlayerCombat& combat = ctx.combat;
    Encounter& encounter = ctx.encounter;
    const CombatTuning& tuning = ctx.tuning;
    CameraFollow& follow = ctx.follow;
    const std::size_t& playerRig = ctx.playerRig;
    const std::size_t& entityRig = ctx.entityRig;
    std::vector<Playback>& playbacks = ctx.playbacks;
    Playback& playback = *ctx.playbackPtr;
    const std::size_t& horseRig = ctx.horseRig;
    bool& plain_ride = ctx.plain_ride;
    PlainRide& pr = ctx.pr;

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
                                                  camera_look_yaw(camera), ctx.enc());
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
                player_frame_of(scene, entityRig, palettes, heightmap, edgeYaw, ctx.enc());
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
    return SelftestResult::next_stage(stage);
}
