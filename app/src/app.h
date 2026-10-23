#pragma once

#include <optional>
#include <string>

#include "door.h"
#include "manifest.h"
#include "ride_points.h"
#include "road_points.h"
#include "transition.h"
#include "valus.h"

// The encounter's own state (ruling R5): Valus's health and whether he is down,
// the one reaction/death clip currently playing and how long it has run, and —
// since S4.2 — the attack in progress, its pinned clock and cooldown.
// `reactionClip` is an index into the entity rig's Playback (-1 = none), never a
// clip name: the app resolves the R3 names once at load.
// `desc` is the colossus's table (S11 Task 7): stage_setup points it at the
// manifest's `encounter`; the runtime helpers that take an Encounter read their
// joints, clips, materials and timings from it. The HUD's max health is Valus's.
inline constexpr float kEncounterMaxHealth = encounters::kValus.maxHealth;

struct Encounter {
    const EncounterDesc* desc = &encounters::kValus;
    float health = kEncounterMaxHealth;
    bool defeated = false;
    int reactionClip = -1;
    float reactionSeconds = 0.0f;

    // S4.2 attacks (design §7.4): the active attack, its clip time (owned here,
    // like reactionSeconds), the cooldown that starts when the clip ends, and
    // the once-only impact mark. `pendingCooldownSeconds` carries the cooldown
    // of an attack a landed strike interrupted: the design starts it after the
    // reaction clip, not when the attack was cut short.
    ValusAttack attack = ValusAttack::None;
    float attackSeconds = 0.0f;
    float cooldownSeconds = 0.0f;
    bool attackHit = false;
    // S6: the club's swing-phase whoosh fired for this attack (once per clip;
    // see the club row's swingSeconds). Reset when the attack starts, is cancelled
    // or ends, and by the soft reset.
    bool attackSwinged = false;
    float pendingCooldownSeconds = 0.0f;
    bool stompLeftNext = true;  // alternation seed (ruling R8's testable input)
    // S6: the weakpoint the killing blow hit (0 forehead, 1 right arm, -1 when
    // the defeat came from another route). The tendril roots order the burst
    // from this weakpoint first.
    int deathWeakpoint = -1;

    // Ruling R8: the manifest entity transform, recomposed each frame from
    // entityYawDeg so Valus can turn (and the club, which reads the live mesh
    // model, turns with him). The scale is the manifest's; the lift is the
    // rig-local grounding offset apply_rig_lift once baked in.
    bool entityTransformSet = false;
    Vec3 entityPosition{};
    Vec3 entityScale{1.0f, 1.0f, 1.0f};
    float entityBaseYawDeg = 0.0f;  // the manifest yaw; the soft reset returns here
    float entityYawDeg = 0.0f;      // the slewed facing
    float entityLift = 0.0f;
};

// The encounter half of the R soft reset (design §7.5): Valus back to full
// health, undefeated, with no reaction or death one-shot left to advance, no
// attack or cooldown, and — since R8 owns the body transform — his opening
// facing. The caller also returns the entity playback to its manifest stand
// clip and recomposes the model; this helper owns the state, not the clip.
inline void reset_encounter(Encounter& encounter) {
    encounter.health = encounter.desc->maxHealth;
    encounter.defeated = false;
    encounter.reactionClip = -1;
    encounter.reactionSeconds = 0.0f;
    encounter.attack = ValusAttack::None;
    encounter.attackSeconds = 0.0f;
    encounter.cooldownSeconds = 0.0f;
    encounter.attackHit = false;
    encounter.attackSwinged = false;
    encounter.pendingCooldownSeconds = 0.0f;
    encounter.stompLeftNext = true;
    encounter.deathWeakpoint = -1;
    encounter.entityYawDeg = encounter.entityBaseYawDeg;
}

// The stages run_app can compose (S7 D6, S8 D2): the --manifest arena, the
// shrine, or the plain in front of it.
enum class StageId { Arena, Shrine, Plain };

// One viewer run's parameters, filled by the argument parser in main.cpp.
struct AppOptions {
    std::string libraryDir;      // converted asset library (--library, or $SOTC_LIB)
    std::string modelRelPath;    // model to load, relative to libraryDir (--model)
    std::string screenshotPath;  // PNG destination; empty = no screenshot (--screenshot)
    int frames = 3;              // frames the --screenshot path renders first (--frames)
    bool selftest = false;       // --selftest: render one frame, print s1-status, exit
    int width = 1280;            // window width in points (--width)
    int height = 720;            // window height in points (--height)
    std::string animClip;        // clip to play, by name or 0-based index (--anim)
    float animTime = 0.0f;       // fixed playback time in seconds (--anim-time)
    bool animTimeSet = false;    // --anim-time was given: start paused at animTime
    // --entity-time: pin every manifest entity's clip to this time (clamped into
    // the clip's duration) for deterministic captures; the entity starts paused.
    // --anim/--anim-time keep their player/single-model meaning.
    float entityTime = 0.0f;
    bool entityTimeSet = false;
    // --manifest: compose the scene from a scene manifest; modelRelPath is then
    // the player's glb (required), the ground heightmap is built from the
    // player-less part of the scene, and the interactive loop drives the player
    // controller with the follow camera.
    std::string manifestPath;
    // --s3-selftest: compose, spawn the player (manifest spawn or the bounds
    // centre), simulate 240 fixed 1/60 s frames of forward+run, render one
    // frame, print `s3-status ...`, and exit 0. Requires --manifest and --model.
    bool s3Selftest = false;
    // --s4-selftest: compose the encounter manifest, pin the entity at t = 0,
    // walk the player into the entity's collision proxy, assert the stop and the
    // entity's feet on the ground, render the final frame, print `s4-status`,
    // and exit 0. Requires --manifest (with an entity) and --model.
    bool s4Selftest = false;
    // --s41-selftest: compose the encounter manifest, reuse the S4 approach,
    // then grab, climb, strike, catch, miss, force a stamina fall, and land the
    // killing blow through fixed 1/60 s steps with no window input. Prints the
    // `s41-*` event lines and `s41-status`, and exits 0. Requires --manifest
    // (with an entity) and --model.
    bool s41Selftest = false;
    // --s42-selftest: the S4.2 acceptance timeline. Compose the encounter
    // manifest, resolve both weakpoint shells, test the foot/head damage gate,
    // climb the chest->shoulder->arm branch, take a stomp and a club hit (with
    // the i-frame gate), die to a lethal club, reset, and climb foot->head on
    // the pinned stamina drains. Fixed 1/60 s steps and no window input. Prints
    // the `s42-*` event lines and `s42-status`, and exits 0. Requires --manifest
    // (with an entity) and --model.
    bool s42Selftest = false;
    // --s5-selftest: the same S4.2 timeline with the S5 audio observer enabled
    // (design §7.4, ruling D11). Every played cue prints an `s5-event` line, the
    // mix is captured to `audioMixdownPath` (required) as a 48 kHz float WAV,
    // and `s5-status` closes the run. A missing cue clip or a silent mixdown is
    // fatal (exit 1). The simulation is untouched: with audio off the `s42-*`
    // lines are byte-identical.
    bool s5Selftest = false;
    // --audio-mixdown PATH: the --s5-selftest capture destination.
    std::string audioMixdownPath;
    // --s41-stills DIR: with --s41-selftest, write one PNG per fight phase
    // (grab, mid-climb, strike, reaction) into DIR, plus the defeat still and
    // its two pixel controls under DIR/control/. The acceptance script creates
    // both DIR and DIR/control/; an absent one is a write failure, never a
    // silent skip.
    std::string s41StillsDir;
    // --s42-stills DIR: with --s42-selftest, write one PNG per fight phase
    // (stomp telegraph, stomp impact, club swing, hurt, death, arm weakpoint)
    // into DIR, HUD visible. The acceptance script creates DIR; an absent one
    // is a write failure, never a silent skip. Each capture snapshots and
    // restores the playback clocks around its render, so the timeline's
    // `s42-*` event values are identical with and without stills.
    std::string s42StillsDir;
    // --camera-target player|entity: the initial framing for captures. `player`
    // is the normal follow camera; `entity` starts in the chest-framing target
    // mode (requires a manifest whose scene has an entity). Interactive runs
    // can still toggle the mode with T.
    std::string cameraTarget = "player";
    // --s6-selftest: the S6 acceptance timeline (design §8.5). Compose the
    // encounter manifest, hear a club swing's whoosh, drive a scripted lethal
    // `apply_combat_events` (damageDealt = 100; the real three-strike defeat
    // stays the frozen S4.1 acceptance, ruling R6), run the death phase machine
    // to Done with the cinematic camera, the corpse swap and the tendril
    // effect, print the `s6-phase`/`s6-event`/`s6-death`/`s6-status` lines,
    // soft-reset with R, print `s6-reset`, and exit 0. Fixed 1/60 s steps and
    // no window input. A missing clip, manifest, effect texture or corpse is
    // fatal. Requires --manifest (with an entity) and --model.
    bool s6Selftest = false;
    // --s6-stills DIR: with --s6-selftest, write one PNG per death phase into
    // DIR: `motion.png` (the death motion), `camera.png` (the pull-back key),
    // `tendrils.png` (the burst), `corpse.png` (the corpse wide). The
    // acceptance script creates DIR; an absent one is a write failure, never a
    // silent skip. Each capture is an observation: the playback clocks are
    // snapshotted and restored around it, so the timeline's values are
    // identical with and without stills.
    std::string s6StillsPath;
    // S7 (design D11/D13): --stage arena|shrine is the first stage (the shrine
    // starts in Free; a dev and walk aid); --no-return keeps S6's end state in
    // interactive play (the return machine never runs); --shrine-manifest is
    // the shrine's composed manifest.
    StageId stage = StageId::Arena;
    bool noReturn = false;
    std::string shrineManifestPath = "out/re/stage/shrine.manifest";
    // S8 (D2): --stage plain starts on the plain (Agro, riding, the shrine's
    // door); --plain-manifest is the plain's composed manifest.
    std::string plainManifestPath = "out/re/stage/plain.manifest";
    // --s7-selftest: the S7 acceptance timeline (design §8.6). In the arena,
    // seed Valus dead (death Done, the corpse shown), run the return machine
    // at fixed 1/60 s steps (an R during the first FadeOut, then again to
    // Load), load the shrine, finish FadeIn/Wake/Free, print the ground, wall,
    // idol, Mono and perf probes, press R back to the arena and load the
    // shrine once more. Prints the `s7-*` lines and exits 0. Requires
    // --manifest (the arena encounter) and --model.
    bool s7Selftest = false;
    // --s7-stills DIR: with --s7-selftest, write fade.png, wake.png,
    // altar.png, idols.png and hall.png into DIR (the script creates it).
    std::string s7StillsPath;
    // --s7-e2e: the end-to-end proof (Task 13). The *interactive* loop runs
    // with a fixed 1/60 s step and a scripted input source instead of the
    // keyboard and gamepad: after 1 s in the arena one lethal hit lands
    // through the production combat-event path, then the S6 death, the return,
    // the real stage switch, the shrine's wake and Free play out untouched; 1 s
    // into Free the script presses R (the key's own handler) and exits 0 1 s
    // into the reloaded arena. Prints the `s7e2e-*` lines. Requires --manifest
    // and --model; runs alone (no selftest, capture, --stage shrine or
    // --no-return).
    bool s7E2e = false;
    // --s8-selftest (S8 Task 9, D17): starts on the plain at fixed 1/60 s with
    // no window input — the whistle from probes.whistle_from, a left mount,
    // kicks through Walk/Trot/Gallop (each loop's speed measured), a full-right
    // steer at the gallop, a held S through the stop clips, S at a standstill
    // backing up, a ride into the plateau's edge (R11's stop), a right dismount
    // there, the on-foot edge walk, the rider/audio/perf probes, then R mounted
    // at a gallop and the arena's opening state. Prints the `s8-*` lines and
    // exits 0. Requires --manifest (the arena R returns to) and --model.
    bool s8Selftest = false;
    // --s8-stills DIR: with --s8-selftest, write mount.png, gallop.png,
    // whistle.png and plain.png into DIR (the caller creates it).
    std::string s8StillsPath;
    // --s8-e2e: the interactive loop at fixed 1/60 s with a scripted input
    // source (its own script state, a ride-input path): from --stage shrine it
    // walks into the shrine's door, whistles on the plain, mounts, gallops 5 s
    // along probes.gallop_line, stops, dismounts, walks into the plain's door,
    // presses R in the shrine and exits 1 s into the arena. Prints the
    // `s8e2e-*` lines; a failed join exits 1 naming the step.
    bool s8E2e = false;
    // S9 (D2): the road — the plain + the arena's entrance — is the Plain stage's
    // manifest in interactive play and the S9 runs (plain.manifest otherwise).
    std::string roadManifestPath = "out/re/stage/road.manifest";
    // --s9-selftest / --s9-stills DIR / --s9-e2e (S9 Task 9's timelines; the flag
    // rules live in main.cpp).
    bool s9Selftest = false;
    std::string s9StillsPath;
    bool s9E2e = false;
};

// Whether the interactive `T` key may toggle entity target framing. The mode
// needs a composed scene (follow mode) *and* an entity chest that resolved this
// run. A manifest with no entity — the S3 arena is one — leaves T inert rather
// than parking the camera on an unset target.
inline bool entity_target_key_allowed(bool followMode, bool entityChestResolved) {
    return followMode && entityChestResolved;
}

// The doors (S8 D14, ruling R16): one row per door — the stage its box is in, the
// box and the arrival spawn (`ride_points::Door`, the arrival is on `to`'s floor),
// and the stage it leads to. Entry iterates the rows of the current stage; the
// arrival is the row recorded at entry (DoorState::row).
struct DoorRow {
    StageId from;
    const ride_points::Door* door;
    StageId to;
};
// S8's rows come first; S9's (the road's corridor, the arena's way back) follow
// and are live only in S9-wired runs: a frozen run passes kS8DoorCount.
inline constexpr DoorRow kDoors[] = {
    {StageId::Shrine, &ride_points::kDoorShrine, StageId::Plain},  // the shrine's front exit
    {StageId::Plain, &ride_points::kDoorPlain, StageId::Shrine},   // the plain's shrine steps
    {StageId::Plain, &road_points::kDoorCorridor, StageId::Arena}, // S9: the corridor's dead end
    {StageId::Arena, &road_points::kDoorArena, StageId::Plain},    // S9: the arena's way back
};
inline constexpr int kDoorCount = static_cast<int>(sizeof(kDoors) / sizeof(kDoors[0]));
inline constexpr int kS8DoorCount = 2;

// How many kDoors rows a run steps (S9 final review): S9's corridor and arena
// rows (2-3) lead into Valus's arena, so they are live only when S9-wired and the
// arena manifest's `encounter` is absent or `valus` (read with the opt-in). Any
// other encounter, or an arena manifest that does not parse (its own load reports
// that), keeps the S8 rows.
inline int door_row_count(bool s9Wired, const std::filesystem::path& arenaManifest) {
    if (!s9Wired) {
        return kS8DoorCount;
    }
    try {
        return parse_manifest(arenaManifest, /*acceptEncounter=*/true).encounter ==
                       encounters::kValus.id
                   ? kDoorCount
                   : kS8DoorCount;
    } catch (const std::exception&) {
        return kS8DoorCount;
    }
}

// The row (among the first `rowCount`) of a door on `here` whose box the step
// prev -> now entered (box_entered), or -1. The first row in table order wins.
// `rowCount` is required: a frozen run passes kS8DoorCount, an S9-wired one door_row_count.
inline int door_entered_row(StageId here, Vec3 prev, Vec3 now, int rowCount) {
    for (int i = 0; i < rowCount && i < kDoorCount; ++i) {
        const ride_points::Door& d = *kDoors[i].door;
        if (kDoors[i].from == here &&
            box_entered(prev, now, Vec3{d.min[0], d.min[1], d.min[2]},
                        Vec3{d.max[0], d.max[1], d.max[2]})) {
            return i;
        }
    }
    return -1;
}

// The row a stage starting at `here` arrives through: the machine waits in Load
// with a valid row whose destination is `here`; otherwise nullptr.
inline const DoorRow* arriving_door(const DoorState& s, StageId here) {
    if (s.phase != DoorPhase::Load || s.row < 0 || s.row >= kDoorCount) return nullptr;
    const DoorRow& r = kDoors[s.row];
    return r.to == here ? &r : nullptr;
}

// The door machine as a stage starts (R16): a door arrival keeps it; any other
// start (R, the return, the first stage) resets it, so a door entered on the frame
// that ended the last stage can never leave the next one locked.
inline DoorState door_stage_start(const DoorState& s, StageId here) {
    return arriving_door(s, here) ? s : reset_door();
}

// The stage loop (S7 D6): run_app composes one stage per iteration; the
// iteration ends with a request — quit, or release the scene and load `next`.
struct StageRequest { bool quit = true; StageId next = StageId::Arena; };
// Agro's pose where the player left her (S9 D4): position (her floor) and yaw.
struct HorsePose {
    Vec3 pos{};
    float yawDeg = 0.0f;
};

// What crosses an iteration: the return machine's state, the --s7-selftest's
// progress, and the first shrine load's mesh count (a reload must match it).
// `e2eStep`/`e2eFrame` are --s7-e2e's progress (0 the first arena, 1 the
// shrine, 2 the arena after R) and its frame count across stages.
struct StageCarry {
    ReturnState ret{};
    // S8 (D14): the door machine crosses the stage switch (FadeOut in one
    // stage, Load at the switch, FadeIn in the next); `door.row` is the entered
    // kDoors row while it runs.
    DoorState door{};
    int s7Step = 0;
    std::size_t shrineMeshes = 0;
    int e2eStep = 0;
    int e2eFrame = 0;
    // --s8-selftest's progress (0 the plain, 1 the arena after R) and
    // --s8-e2e's (0 the shrine, 1 the plain, 2 the shrine again, 3 the arena).
    int s8Step = 0;
    // --s9-selftest's progress (0 the road, 1 the arena through the corridor door,
    // 2 the road through the arena door, 3 the arena after R) and --s9-e2e's (0 the
    // shrine, 1 the road, 2 the arena, 3 the shrine after the return); --s9-e2e
    // counts its frames in e2eFrame.
    int s9Step = 0;
    // S9 (D4): Agro's pose when the player last left the road by a door (S9-wired
    // runs only); the next road load places her there. R clears it.
    std::optional<HorsePose> horse;
};

// Opens the windowed SDL3 shell and runs it until the user quits (Escape or the
// window close button). With opts.selftest, runs exactly one frame, prints the
// `s1-status` line to stdout, and returns.
//
// Returns 0 on success, 1 on a runtime (SDL/GPU) failure, 2 on usage errors
// (including an --anim that names no clip of the loaded model).
int run_app(const AppOptions& opts);
