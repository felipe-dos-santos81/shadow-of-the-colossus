#pragma once

// The --s11 script (S11 Task 10): one scripted player for --s11-selftest and
// --s11-e2e. Both drive the production frame (frame_step) from Quadratus's
// arena spawn: walk at him, dodge his stomp, stab a rear hoof in its lift
// window, grab and climb the fur while he kneels, strike the back sigil from the
// rump, surface-walk his back, hold through a shake, climb the neck to the head,
// strike the head sigil to his death, watch the death to Done, then R. The
// script only picks keys (and, like the S8/S9 e2e scripts, turns the camera to
// the heading it walks); it reads the game state the way a player watches it.
// The selftest adds the stills and the perf run; the e2e runs the real loop.

#include "interactive.h"

#include <optional>
#include <string>

struct S11Script {
    const char* prefix = "s11";  // the lines' prefix: "s11" or "s11e2e"
    int phase = 0;
    float phaseSeconds = 0.0f;
    float seconds = 0.0f;  // fixed-step seconds since the start
    int frames = 0;
    // The strike routine's step (align, charge, release, regrab) and its clocks.
    int step = 0;
    float stepSeconds = 0.0f;
    float lastDistance = -1.0f;
    int axis = 0;          // the descent's axis: 0 along the band, 1 around/across it
    float along = 1.0f;    // its signs
    float lateral = 1.0f;
    int flips = 0;
    bool resting = false;  // standing on his back to regain grip
    int strikes = 0;
    int strikeBand = -1;  // the band the last swing was released from
    // What the lines report once.
    int lastBand = -2;
    int lastQuadState = -1;
    int lastDeathPhase = -1;
    int chain = -1;
    bool sawShake = false;
    float shakeLow = 100.0f;
    float diedAtYaw = 0.0f;
    Vec3 diedAt{};
    bool stabPressed = false;
    bool headStill = false;
    // A stalled walk's sidestep (walk_toward).
    Vec3 progressFrom{};
    float progressSeconds = 0.0f;
    float detourSeconds = 0.0f;
    float detourSide = 1.0f;
    int targetHoof = -1;
    // Requests for the caller this frame: R, and the selftest's still.
    bool wantR = false;
    std::string still;
};

// This frame's held keys (before frame_step). May turn the camera (a scripted
// look along the walk, as the S8/S9 e2e scripts do).
PlayerInput s11_input(S11Script& script, StageCtx& ctx);

// After frame_step: the script's checks and lines. An exit code ends the run:
// 0 once the reset is proven, 1 (with a message) on a failed step.
std::optional<int> s11_after(S11Script& script, StageCtx& ctx);

// The opening line (both runs): the stage, the encounter, the layered ground.
// False when the manifest's encounter is not Quadratus or his fight is unwired.
bool s11_start(S11Script& script, StageCtx& ctx, double loadMs);
