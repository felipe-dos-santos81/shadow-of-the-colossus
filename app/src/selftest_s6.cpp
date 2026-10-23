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

// The corpse assets (Valus's EncounterDesc Corpse/Ground props) load with the club's
// in encounter_rt.h: the load precedes the renderer's one-shot upload.
constexpr int kS6MaxSwingFrames = 240;  // 4 s: walk the club clip to its swing phase
constexpr int kS6MaxDeathFrames = 600;  // 10 s: the death machine to Done
constexpr float kS6Eps = 1e-4f;         // the stills' phase-time epsilon
// The effect pass's price at the tendril phase (Task 6's perf gate): the same
// 240-frame pair the S4.1/S4.2 render gates use, once with the pass drawing
// and once with it suppressed from the same pose and camera.
constexpr int kS6PerfFrames = 240;

}  // namespace

// --s6-selftest: the S6 death timeline.
SelftestResult run_s6(StageCtx& ctx, RunState& run) {
    const AppOptions& opts = run.opts;
    Shell& shell = run.shell;
    Renderer& renderer = run.renderer;
    AudioSeam& audio = run.audio;
    Scene& scene = ctx.scene;
    ClubAttachment& club = ctx.club;
    CorpseAttachment& corpse = ctx.corpse;
    bool& effects_ready = ctx.effects_ready;
    Heightmap& heightmap = ctx.heightmap;
    const RigCpu& player_rig_record = *ctx.playerRigRecord;
    Camera& camera = ctx.camera;
    PlayerState& player = ctx.player;
    PlayerCombat& combat = ctx.combat;
    Encounter& encounter = ctx.encounter;
    const std::size_t& entityRig = ctx.entityRig;
    std::vector<Playback>& playbacks = ctx.playbacks;
    Playback& playback = *ctx.playbackPtr;
    EntityClips& entityClips = ctx.entityClips;

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
        renderer.set_hud(ctx.hud_state(/*visible=*/true));
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
                        camera_look_yaw(camera), ctx.enc(), /*entityLive=*/true);
    const PlayerFrame deadFrame =
        player_frame_of(scene, entityRig, donePalettes, heightmap,
                        camera_look_yaw(camera), ctx.enc(), /*entityLive=*/false);
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
    ctx.soft_reset_encounter(death, deathCueFired, /*cameraBaseSet=*/nullptr);
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
