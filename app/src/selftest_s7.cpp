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

constexpr int kS7MaxArenaFrames = 1200;   // 20 s: Hold + FadeOut twice, to Load
constexpr int kS7MaxShrineFrames = 2400;  // 40 s: FadeIn + the ~11.8 s wake, to Free
constexpr int kS7MaxWallFrames = 900;     // 15 s: the scripted walk toward the wall probe
constexpr int kS7PerfFrames = 240;
constexpr float kS7WakeStillFraction = 0.85f;  // wake.png: the rise, the altar ahead
// fade.png and the injected R: just past the middle of the first FadeOut, where
// the still reads as near-white over the arena (the acceptance's luma gate).
constexpr float kS7FadeStillFade = 0.6f;

}  // namespace

// --s7-selftest: the S7 return timeline (one stage per call; carry.s7Step).
SelftestResult run_s7(StageCtx& ctx, RunState& run) {
    const AppOptions& opts = run.opts;
    Shell& shell = run.shell;
    Renderer& renderer = run.renderer;
    StageCarry& carry = run.carry;
    StageId& stage = run.stage;
    const StageId& here = ctx.here;
    Scene& scene = ctx.scene;
    ClubAttachment& club = ctx.club;
    CorpseAttachment& corpse = ctx.corpse;
    SceneManifest& manifest = ctx.manifest;
    Heightmap& heightmap = ctx.heightmap;
    const RigCpu& player_rig_record = *ctx.playerRigRecord;
    Camera& camera = ctx.camera;
    Vec3& stage_spawn = ctx.stage_spawn;
    float& stage_spawn_yaw = ctx.stage_spawn_yaw;
    PlayerState& player = ctx.player;
    PlayerCombat& combat = ctx.combat;
    Encounter& encounter = ctx.encounter;
    const CombatTuning& tuning = ctx.tuning;
    CameraFollow& follow = ctx.follow;
    const std::size_t& entityRig = ctx.entityRig;
    std::vector<Playback>& playbacks = ctx.playbacks;
    Playback& playback = *ctx.playbackPtr;
    EntityClips& entityClips = ctx.entityClips;
    ReturnTimings& returnTimings = ctx.returnTimings;
    float& wakeFirstSeconds = ctx.wakeFirstSeconds;
    const bool& manual_clip = ctx.manual_clip;

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
        apply_player_transform(scene, player_rig_record, player, ctx.player_lift());
        renderer.set_hud(ctx.hud_state(/*visible=*/true));
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
        if (here != StageId::Arena || ret.phase != ReturnPhase::Idle ||
            death.phase != DeathPhase::Idle ||
            encounter.defeated || !entityShown || !corpseHidden || valus != 100 ||
            hero != 100) {
            return false;
        }
        std::printf("s7-reset stage=arena phase=%s death=%s valus=%d player=%d\n",
                    return_phase_name(ret.phase), death_phase_name(death.phase), valus,
                    hero);
        return true;
    };

    if (here == StageId::Arena && carry.s7Step == 0) {
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
                if (ctx.press_reset(death, deathCueFired, nullptr)) {
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
        return SelftestResult::next_stage(stage);
    }

    if (here == StageId::Shrine && carry.s7Step == 1) {
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
            ctx.pose_wake();
            camera = camera_wake_pose(stage_spawn, stage_spawn_yaw, ctx.wake_clock(),
                                      returnTimings.wake);
            // The plant against the recon's pelvis heights: lying at
            // the wake's start, kneeling at the cut boundary.
            if (ret.phase == ReturnPhase::FadeIn) {
                plantLying = ctx.player_lift();
            } else if (plantKneeling < 0.0f && ret.t >= wakeFirstSeconds) {
                plantKneeling = ctx.player_lift();
            }
            plantStanding = ctx.player_lift();  // the last Wake frame's
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
        ctx.leave_wake();
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
        if (!ctx.press_reset(death, deathCueFired, nullptr)) {
            return fail("R in the shrine did not ask for the arena");
        }
        carry.s7Step = 2;
        end_stage(StageId::Arena);
        return SelftestResult::next_stage(stage);
    }

    if (here == StageId::Arena && carry.s7Step == 2) {
        // --- 9. The arena again, in its opening state ----------------
        if (!print_reset()) {
            return fail("the arena did not come back in its opening state");
        }
        carry.s7Step = 3;
        end_stage(StageId::Shrine);
        return SelftestResult::next_stage(stage);
    }

    if (here == StageId::Shrine && carry.s7Step == 3) {
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
