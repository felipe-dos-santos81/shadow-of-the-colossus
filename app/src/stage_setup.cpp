#include "stage_ctx.h"

#include "door.h"
#include "ride_points.h"
#include "road_points.h"
#include "shrine_points.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <limits>
#include <string>
#include <system_error>

// The stage loop's opening (S10 Task 6b): the code that composed each stage at
// the top of run_app's iteration, moved whole; the locals the rest of the
// iteration reads are StageCtx members, named here through references.
std::optional<int> stage_setup(RunState& run, StageCtx& ctx) {
    const AppOptions& opts = run.opts;
    Renderer& renderer = run.renderer;
    AudioSeam& audio = run.audio;
    StageCarry& carry = run.carry;
    const StageId stage = run.stage;
    bool& first_stage = run.first_stage;
    bool& return_available = run.return_available;
    bool& sword_fx_ready = run.sword_fx_ready;
    bool& sword_fx_warned = run.sword_fx_warned;
    StageId& here = ctx.here;
    const char*& stage_name = ctx.stage_name;
    Scene& scene = ctx.scene;
    ClubAttachment& club = ctx.club;
    CorpseAttachment& corpse = ctx.corpse;
    bool& effects_ready = ctx.effects_ready;
    SceneManifest& manifest = ctx.manifest;
    Heightmap& heightmap = ctx.heightmap;
    Heightmap& raygrid = ctx.raygrid;
    std::size_t& swordMeshBegin = ctx.swordMeshBegin;
    std::size_t& swordMeshEnd = ctx.swordMeshEnd;
    bool& follow_mode = ctx.follow_mode;
    RunFlags& flags = ctx.flags;
    bool& death_wired = ctx.death_wired;
    bool& road = ctx.road;
    int& door_rows = ctx.door_rows;
    bool& return_wired = ctx.return_wired;
    Camera& camera = ctx.camera;
    Vec3& stage_spawn = ctx.stage_spawn;
    float& stage_spawn_yaw = ctx.stage_spawn_yaw;
    const DoorRow*& arrival = ctx.arrival;
    bool& door_arrival = ctx.door_arrival;
    PlayerState& player = ctx.player;
    Encounter& encounter = ctx.encounter;
    CameraFollow& follow = ctx.follow;
    CameraTarget& targetCamera = ctx.targetCamera;
    Vec3& entityChest = ctx.entityChest;
    bool& haveEntityChest = ctx.haveEntityChest;
    std::size_t& playerRig = ctx.playerRig;
    std::size_t& entityRig = ctx.entityRig;
    std::vector<Playback>& playbacks = ctx.playbacks;
    std::size_t& horseRig = ctx.horseRig;
    PlayerClips& playerClips = ctx.playerClips;
    EntityClips& entityClips = ctx.entityClips;
    ReturnTimings& returnTimings = ctx.returnTimings;
    int& wakeFirst = ctx.wakeFirst;
    int& wakeSecond = ctx.wakeSecond;
    float& wakeFirstSeconds = ctx.wakeFirstSeconds;
    bool& manual_clip = ctx.manual_clip;
    bool& valus_behavior = ctx.valus_behavior;
    bool& s8_wired = ctx.s8_wired;
    bool& plain_ride = ctx.plain_ride;
    PlainRide& pr = ctx.pr;
    bool& sword_wired = ctx.sword_wired;

    // The stage's manifest: --manifest for the arena, the shrine's own
    // composed manifest for the shrine.
    // S8 (D2): the stage loop switches on StageId (no shrine-vs-rest
    // booleans: a third stage must never run as the arena).
    here = stage;
    stage_name = here == StageId::Shrine  ? "shrine"
                                   : here == StageId::Plain ? "plain"
                                                            : "arena";
    if (!first_stage) {
        audio.new_stage();
    }
    first_stage = false;
    const Uint64 stage_load_start = SDL_GetTicksNS();

    follow_mode = !opts.manifestPath.empty();
    // S10: the run-mode flags (run_flags.h); the scene's own flags
    // (scene_flags) come after the manifest and playbacks load.
    flags = run_flags(opts, here, follow_mode, return_available);
    // S6: whether this run wires the death sequence (the interactive game
    // and the --s6-selftest). The frozen scripted timelines (s3/s4/s41/
    // s42/s5), the pinned captures and the plain screenshot runs must not
    // load the corpse or the effect textures: their outputs and pixel
    // stats are the regression.
    death_wired = flags.deathWired;
    // S7: interactive play steps the return machine (unless --no-return,
    // D13), and so does --s7-selftest. No frozen selftest (--selftest,
    // --s3 ... --s6-selftest) or capture ever does: S4.1's selftest
    // defeats Valus for real, and its output is the regression.
    const bool interactive_run = flags.interactiveRun;
    // S9: interactive play (not the frozen --s7-e2e/--s8-e2e) and the S9
    // runs load the road, step the corridor and arena doors and carry
    // Agro's pose; no frozen run ever does.
    const bool s9_wired = flags.s9Wired;
    // S8 (D2) / S9 (D2): the Plain stage is the road when S9-wired.
    road = flags.road;
    // S9's corridor/arena door rows only into Valus's arena (door_row_count).
    door_rows =
        door_row_count(s9_wired && follow_mode, opts.manifestPath);
    const std::string& stage_manifest_path =
        here == StageId::Shrine  ? opts.shrineManifestPath
        : road                   ? opts.roadManifestPath
        : here == StageId::Plain ? opts.plainManifestPath
                                 : opts.manifestPath;
    return_wired = flags.returnWired;
    if (follow_mode) {
        // The manifest is user data: a malformed line is a usage error
        // (exit 2) carrying the file and line, never a crash.
        try {
            manifest = parse_manifest(stage_manifest_path);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "sotc-viewer: %s%s\n", error.what(),
                     here == StageId::Shrine
                         ? " (compose it with `make shrine-manifest`)"
                     : road ? " (compose it with `make road-manifest`)"
                     : here == StageId::Plain
                         ? " (compose it with `python3 tools/re/stage/probe.py compose "
                           "--stage plain`)"
                         : "");
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
        const bool pump_load = flags.pumpLoad;
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
            heightmap = build_heightmap(scene, kHeightmapCellSize,
                                        /*layered=*/here == StageId::Shrine);
            // S8 (D3): the plain's plateau ends in cliffs; its map
            // refuses holes, cliffs and walls of rock.
            heightmap.refuseHoles = here == StageId::Plain;
            // S9 (D6): the beam and shade rays also march the backdrops
            // (the cliffs the ground map leaves out), built once here.
            if (road) {
                raygrid = build_backdrop_grid(scene, kHeightmapCellSize);
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr, "sotc-viewer: no ground: %s\n", error.what());
        }
        // S9: the sword, after both grids (a static mesh with ground = true:
        // appended earlier it would rasterize into them) and before
        // upload(). It is a hand prop, not scene extent: the bounds keep
        // the stage's own.
        if (road) {
            const Vec3 boundsMin = scene.boundsMin, boundsMax = scene.boundsMax;
            swordMeshBegin = append_static_model(scene, opts.libraryDir, kSwordGlb);
            swordMeshEnd = scene.meshes.size();
            scene.boundsMin = boundsMin;
            scene.boundsMax = boundsMax;
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
        // S9 (D7): the sword's light — fatal where the light is the proof.
        if (road && !sword_fx_ready && !sword_fx_warned) {
            std::string why = "the sword pass was not created";
            try {
                sword_fx_ready = renderer.upload_sword_textures(
                    load_sword_textures(opts.libraryDir, road_points::kSwordTextures));
            } catch (const std::exception& error) {
                why = error.what();
            }
            if (!sword_fx_ready) {
                if (opts.s9Selftest || opts.s9E2e) {
                    std::fprintf(stderr, "sotc-viewer: --s9-%s: %s\n",
                                 opts.s9Selftest ? "selftest" : "e2e", why.c_str());
                    return 1;
                }
                std::fprintf(stderr, "sotc-viewer: the sword's light is unavailable: %s\n",
                             why.c_str());
                sword_fx_warned = true;
            }
        }
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
                        stage_name, manifest.instances.size(),
                        scene.meshes.size(), load_ms);
            if (opts.s7E2e) {
                std::printf("s7e2e-load stage=%s meshes=%zu ms=%.1f\n",
                            stage_name, scene.meshes.size(), load_ms);
            }
        }
        if (opts.s9Selftest && road && carry.s9Step == 0) {
            std::printf("s9-load stage=plain manifest=road instances=%zu backdrops=%zu "
                        "meshes=%zu raygrid=%dx%d ms=%.1f\n",
                        manifest.instances.size(), manifest.backdrops.size(),
                        scene.meshes.size(), raygrid.width, raygrid.height,
                        static_cast<double>(SDL_GetTicksNS() - stage_load_start) / 1e6);
        }
        if (opts.s8Selftest && here == StageId::Plain) {
            std::printf("s8-load stage=plain instances=%zu meshes=%zu ms=%.1f\n",
                        manifest.instances.size(), scene.meshes.size(),
                        static_cast<double>(SDL_GetTicksNS() - stage_load_start) / 1e6);
        }
    } else if (!opts.modelRelPath.empty()) {
        scene = load_scene(opts.libraryDir, opts.modelRelPath);
        renderer.upload(scene);
        print_scene_line(scene);
    }

    // The player's rig record: its mesh range is what apply_player_transform
    // moves and what the ground lift is measured from (the entities' own
    // rigs follow in their tasks).
    ctx.playerRigRecord = &player_rig(scene);
            const RigCpu& player_rig_record = *ctx.playerRigRecord;

    camera_frame(camera, scene.boundsMin, scene.boundsMax);

    // The player and its follow camera (manifest mode only): spawn at the
    // manifest's spawn or the bounds centre, with the camera snapped behind
    // the player (a huge dt makes camera_follow's first blend factor
    // exactly 1 in float arithmetic; later frames smooth).
    // The shrine's spawn stands on the floor under it (the layered
    // ground's floor, not the column's roof); the arena's is the
    // manifest's own, unchanged.
    stage_spawn = spawn_position(manifest, scene);
    stage_spawn_yaw = manifest.hasSpawn ? manifest.spawnYawDeg : 0.0f;
    // S8 (D14): a stage entered through a door starts at that door's
    // arrival spawn (on its floor), not at the stage's start.
    // The arrival is the kDoors row recorded at entry (R16); any other
    // start resets the machine (a door entered on the frame R ended the
    // last stage must not lock this one).
    carry.door = door_stage_start(carry.door, here);
    arrival = arriving_door(carry.door, here);
    door_arrival = arrival != nullptr;
    // S9 (D3): the arena entered from the road's corridor is a fresh
    // encounter: the return machine starts over, so Valus's death returns
    // to the shrine even when the loop began there (ret at Free).
    if (arrival != nullptr && arrival->from == StageId::Plain && here == StageId::Arena) {
        carry.ret = reset_return();
    }
    if (door_arrival) {
        const ride_points::Door& door = *arrival->door;
        stage_spawn = Vec3{door.arrive[0], door.arrive[1], door.arrive[2]};
        stage_spawn_yaw = door.arriveYawDeg;
    }
    if (here == StageId::Shrine || door_arrival) {
        const float floor = heightmap_floor(heightmap, stage_spawn, kFloorStep);
        if (std::isfinite(floor)) {
            stage_spawn.y = floor;
        }
    }
    player = PlayerState{stage_spawn, stage_spawn_yaw};
    if (follow_mode) {
        camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f, 0.0f,
                      /*moving=*/false, /*dtSeconds=*/10.0f);
    }

    // Playback: one per rig (S4). The player's slot keeps the S2 meaning —
    // the model's real clips first, the procedural wave only as the
    // fallback for a skinned model that carries none; --anim picks a clip
    // by name or index, and --anim-time pins the time and starts paused
    // (deterministic captures). Each entity's rig loops the exact clip its
    // manifest line named (compose_scene already refused a load whose model
    // does not carry it); --entity-time pins that clock, clamped into the
    // clip. A scene with no rig at all (a model-less run) keeps one slot for
    // the player path, which stays silent.
    playerRig = scene.rigs.empty() ? 0 : scene.playerRig;
    // S4.0: one entity, right after the player. S8 (D17): no entity, no
    // index — a stage without one (the plain's rig 1 is Agro) has none.
    entityRig = manifest.entities.empty()
                                      ? std::numeric_limits<std::size_t>::max()
                                      : scene.playerRig + 1;
    playbacks = std::vector<Playback>(std::max<std::size_t>(scene.rigs.size(), 1));
    ctx.playbackPtr = &playbacks[playerRig];
            Playback& playback = *ctx.playbackPtr;
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
    // S8 (D4): Agro's rig (never an encounter entity) idles on stand1;
    // on the plain the ride wiring poses her from here on.
    horseRig = scene.horseRig >= 0
                                     ? static_cast<std::size_t>(scene.horseRig)
                                     : std::numeric_limits<std::size_t>::max();
    if (horseRig < playbacks.size()) {
        build_playback(scene, horseRig, playbacks[horseRig]);
        playbacks[horseRig].index = clip_index_named(playbacks[horseRig], kAgroStandClip);
        playbacks[horseRig].playing = true;
    }
    for (std::size_t rigIndex = 0; rigIndex < playbacks.size(); ++rigIndex) {
        print_anim_status(scene, rigIndex, playbacks[rigIndex]);
    }

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
    returnTimings = ReturnTimings{kShrineHoldSeconds, kShrineFadeOutSeconds,
                                kShrineFadeInSeconds, 0.0f};
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
    // S8 (D15): the plain wants its sound too (no entity there).
    const bool audio_wanted =
        scene_flags(opts, here, follow_mode, !manifest.entities.empty(),
                    entityRig < playbacks.size())
            .audioWanted;
    if (audio_wanted) {
        // The S5 slice is the load gate for --s5-selftest and interactive
        // play: both must load the frozen eight-entry manifest beside the
        // S6 cue table. The S6 slice's own gate (--s6-selftest) passes
        // includeS6 = true and requires the death and club-swing rows too.
        if (!audio.load(opts.libraryDir, /*includeS6=*/opts.s6Selftest)) {
            // The engine (or the cue gate) already printed the reason. A
            // missing curated clip is fatal when the sound is the artifact;
            // interactive play degrades to silence with that one warning.
            if (flags.audioStrict) {
                return 1;
            }
        }
        audio.log = flags.audioStrict;
        if (opts.s6Selftest) {
            audio.logPrefix = "s6-event";
            // The S6 slice's rows are mandatory in this run: a missing one
            // at play time is the fatal error it has always been.
            audio.requireS6 = true;
        }
    }
    if (flags.audioStrict && !audio.enabled) {
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
    manual_clip = !opts.animClip.empty() || opts.animTimeSet;

    // Ruling R8's autonomous fight (turn, stomp, club) runs in free
    // interactive play. The scripted S3/S4/S4.1 acceptance timelines pin the
    // entity and own every step — a stomp would race their assertions — and
    // --entity-time pins the clip for a capture, so behavior is off there.
    // Task 9's s42 timeline opts in with its own fixed steps.
    valus_behavior =
        scene_flags(opts, here, follow_mode, !manifest.entities.empty(),
                    entityRig < playbacks.size())
            .valusBehavior;

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

    // --- S8: the plain — Agro, the whistle, riding (design D4-D15) -------
    // Stepped only in interactive play (not --s7-e2e): no frozen selftest,
    // --s7-selftest or --s7-e2e ever meets the doors or the ride (D17).
    s8_wired = flags.s8Wired;
    plain_ride = s8_wired && here == StageId::Plain && horseRig < playbacks.size() &&
                      !manifest.horses.empty();
    if (plain_ride &&
        (clip_index_named(playbacks[horseRig], kAgroStandClip) < 0 ||
         clip_index_named(playback, "player_a_uma_stand1_player") < 0 ||
         clip_index_named(playback, kWhistleClip) < 0 ||
         clip_index_named(playback, kWhistleNearClip) < 0)) {
        // The S8 bake is missing (the shared library predates it): Agro
        // stands, the plain plays on foot.
        std::fprintf(stderr,
                     "sotc-viewer: the plain: the library lacks the S8 ride clips (re-run "
                     "`make bake`); riding is off\n");
        plain_ride = false;
    }
    if (!manifest.horses.empty()) {
        const ManifestInstance& placed = manifest.horses.front().placement;
        pr.horsePos = placed.position;
        pr.horseYaw = entity_yaw_deg(placed.rotation);
        pr.horseScale = placed.scale;
        // S9 (D4): on the road, Agro waits where the player left her.
        if (road && carry.horse) {
            pr.horsePos = carry.horse->pos;
            pr.horseYaw = carry.horse->yawDeg;
        }
        const float floor = heightmap_floor(heightmap, pr.horsePos, kFloorStep);
        if (std::isfinite(floor)) {
            pr.horsePos.y = floor;
        }
    }
    sword_wired = road && swordMeshEnd > swordMeshBegin;
    if (sword_wired) {
        for (const SwordClips* c : {&kSwordFoot, &kSwordMounted}) {
            for (const char* name : {c->start, c->loop, c->end}) {
                sword_wired = sword_wired && clip_index_named(playback, name) >= 0;
            }
        }
        if (!sword_wired) {
            std::fprintf(stderr,
                         "sotc-viewer: the road: the library lacks the raise clips "
                         "(re-run `make bake` into the view); the sword stays lowered\n");
        } else if (playerRig >= scene.rigs.size() ||
                   joint_index_named(scene.rigs[playerRig], kSwordBone) < 0) {
            std::fprintf(stderr, "sotc-viewer: the road: the player rig has no %s; the "
                                 "sword stays lowered\n", kSwordBone);
            sword_wired = false;
        }
    }
    if (plain_ride) {
        set_rig_model(scene, scene.rigs[horseRig], ctx.horse_model());
    }
    return std::nullopt;
}
