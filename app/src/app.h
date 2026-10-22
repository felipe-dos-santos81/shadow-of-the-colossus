#pragma once

#include <string>

#include "transition.h"
#include "valus.h"

// The encounter's own state (ruling R5): Valus's health and whether he is down,
// the one reaction/death clip currently playing and how long it has run, and —
// since S4.2 — the attack in progress, its pinned clock and cooldown.
// `reactionClip` is an index into the entity rig's Playback (-1 = none), never a
// clip name: the app resolves the R3 names once at load.
inline constexpr float kEncounterMaxHealth = 100.0f;

struct Encounter {
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
    // see kValusClubSwingSeconds). Reset when the attack starts, is cancelled
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
    encounter.health = kEncounterMaxHealth;
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
};

// Whether the interactive `T` key may toggle entity target framing. The mode
// needs a composed scene (follow mode) *and* an entity chest that resolved this
// run. A manifest with no entity — the S3 arena is one — leaves T inert rather
// than parking the camera on an unset target.
inline bool entity_target_key_allowed(bool followMode, bool entityChestResolved) {
    return followMode && entityChestResolved;
}

// The stage loop (S7 D6): run_app composes one stage per iteration; the
// iteration ends with a request — quit, or release the scene and load `next`.
enum class StageId { Arena, Shrine };
struct StageRequest { bool quit = true; StageId next = StageId::Arena; };
struct StageCarry { ReturnState ret{}; int s7Step = 0; bool reloadedShrine = false; };  // what crosses an iteration

// Opens the windowed SDL3 shell and runs it until the user quits (Escape or the
// window close button). With opts.selftest, runs exactly one frame, prints the
// `s1-status` line to stdout, and returns.
//
// Returns 0 on success, 1 on a runtime (SDL/GPU) failure, 2 on usage errors
// (including an --anim that names no clip of the loaded model).
int run_app(const AppOptions& opts);
