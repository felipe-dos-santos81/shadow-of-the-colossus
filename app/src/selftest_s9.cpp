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
constexpr int kS9PerfFrames = 240;

}  // namespace

// --s9-selftest: the S9 road timeline (one stage per call; carry.s9Step).
SelftestResult run_s9(StageCtx& ctx, RunState& run) {
    const AppOptions& opts = run.opts;
    Shell& shell = run.shell;
    Renderer& renderer = run.renderer;
    AudioSeam& audio = run.audio;
    StageCarry& carry = run.carry;
    StageId& stage = run.stage;
    const bool& sword_fx_ready = run.sword_fx_ready;
    std::optional<HorsePose>& s9AgroLeft = run.s9AgroLeft;
    const StageId& here = ctx.here;
    const char* const stage_name = ctx.stage_name;
    Scene& scene = ctx.scene;
    ClubAttachment& club = ctx.club;
    Heightmap& heightmap = ctx.heightmap;
    const bool& road = ctx.road;
    const RigCpu& player_rig_record = *ctx.playerRigRecord;
    Camera& camera = ctx.camera;
    const bool& door_arrival = ctx.door_arrival;
    PlayerState& player = ctx.player;
    PlayerCombat& combat = ctx.combat;
    Encounter& encounter = ctx.encounter;
    const CombatTuning& tuning = ctx.tuning;
    CameraFollow& follow = ctx.follow;
    const std::size_t& entityRig = ctx.entityRig;
    std::vector<Playback>& playbacks = ctx.playbacks;
    Playback& playback = *ctx.playbackPtr;
    const std::size_t& horseRig = ctx.horseRig;
    bool& plain_ride = ctx.plain_ride;
    PlainRide& pr = ctx.pr;
    SwordState& sword = ctx.sword;
    bool& sword_wired = ctx.sword_wired;

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
                std::isnan(basisYaw) ? camera_look_yaw(camera) : basisYaw, ctx.enc());
            player_step(player, combat, input, f, tuning, dt);
            apply_player_transform(scene, player_rig_record, player, ctx.player_lift());
        }
        fx = SwordFxFrame{};
        if (sword_wired) {
            camera_sword_shoulder(camera, sword.raise);
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
        return SelftestResult::next_stage(stage);
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
        return SelftestResult::next_stage(stage);
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
    return SelftestResult::next_stage(stage);
}
