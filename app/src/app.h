#pragma once

#include <string>

// The encounter's own state (ruling R5): Valus's health and whether he is down,
// plus the one reaction/death clip currently playing and how long it has run.
// `reactionClip` is an index into the entity rig's Playback (-1 = none), never a
// clip name: the app resolves the R3 names once at load.
struct Encounter {
    float health = 100.0f;
    bool defeated = false;
    int reactionClip = -1;
    float reactionSeconds = 0.0f;
};

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
    // --s41-stills DIR: with --s41-selftest, write one PNG per fight phase
    // (grab, mid-climb, strike, reaction) into DIR, plus the defeat still and
    // its two pixel controls under DIR/control/. The acceptance script creates
    // both DIR and DIR/control/; an absent one is a write failure, never a
    // silent skip.
    std::string s41StillsDir;
    // --camera-target player|entity: the initial framing for captures. `player`
    // is the normal follow camera; `entity` starts in the chest-framing target
    // mode (requires a manifest whose scene has an entity). Interactive runs
    // can still toggle the mode with T.
    std::string cameraTarget = "player";
};

// Whether the interactive `T` key may toggle entity target framing. The mode
// needs a composed scene (follow mode) *and* an entity chest that resolved this
// run. A manifest with no entity — the S3 arena is one — leaves T inert rather
// than parking the camera on an unset target.
inline bool entity_target_key_allowed(bool followMode, bool entityChestResolved) {
    return followMode && entityChestResolved;
}

// Opens the windowed SDL3 shell and runs it until the user quits (Escape or the
// window close button). With opts.selftest, runs exactly one frame, prints the
// `s1-status` line to stdout, and returns.
//
// Returns 0 on success, 1 on a runtime (SDL/GPU) failure, 2 on usage errors
// (including an --anim that names no clip of the loaded model).
int run_app(const AppOptions& opts);
