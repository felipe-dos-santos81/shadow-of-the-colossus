#pragma once

// The --s12 script (S12 Task 9): one scripted player for --s12-selftest and --s12-e2e.
// Both drive the production frame (frame_step) from Gaius's arena spawn: walk at him,
// dodge his slam, grab the stuck blade's low end in its window and walk it, ride it up
// onto the forearm, climb the upper arm, shoulder and neck to the head, strike the head
// sigil, hold on through a shake, climb down to the belly sigil, strike it to his death,
// watch the death to Done (the corpse stays), then R. The script only picks keys (and,
// like the S8/S9/S11 scripts, turns the camera to the heading it walks); it reads the
// game state the way a player watches it. The selftest adds the stills and the perf run;
// the e2e runs the real loop.

#include "interactive.h"

#include <optional>
#include <string>
#include <vector>

struct S12Script {
    const char* prefix = "s12";  // the lines' prefix: "s12" or "s12e2e"
    int phase = 0;
    int phasesDone = 0;
    float phaseSeconds = 0.0f;
    float seconds = 0.0f;  // fixed-step seconds since the start
    int frames = 0;
    // The strike routine (align, charge, release, regrab) and its descent.
    int step = 0;
    float stepSeconds = 0.0f;
    float lastDistance = -1.0f;
    int axis = 0;
    float along = 1.0f;
    float lateral = 1.0f;
    int flips = 0;
    int strikes = 0;
    int strikeBand = -1;
    // The once-only lines and their memory.
    int lastMode = -1;
    int lastBand = -2;
    int lastDeathPhase = -1;
    int lastClip = -1;
    bool judged = false;
    bool hitPrinted = false;
    int slams = 0;
    int swordRows = 0;  // the slam's blade rows that overlapped the player during the dodge
    std::string swordFirst;  // the first one: row, clip, frame, the player's offsets
    int throwOffs = 0;  // KNIGHT_PLAYER_OUT landings (D12)
    float dodgeSide = 1.0f;
    // The blade: the window it was grabbed in and what its close did.
    int grabFrame = -1;
    int closeFrame = -1;
    float closeT = -1.0f;
    bool bladeDone = false;
    std::vector<std::string> route;  // the bands climbed, in order
    bool sawShake = false;
    bool shakeDone = false;
    float shakeLow = 100.0f;
    std::string shakeClip;
    bool headStill = false;
    Vec3 diedAt{};
    // A stalled walk's sidestep.
    Vec3 progressFrom{};
    float progressSeconds = 0.0f;
    float detourSeconds = 0.0f;
    float detourSide = 1.0f;
    bool grabPressed = false;
    float sinceShake = -1.0f;   // s since his last shake ended (-1 none yet)
    bool shookLast = false;
    // Requests for the caller this frame: R, and the selftest's still.
    bool wantR = false;
    std::string still;
};

// This frame's held keys (before frame_step); may turn the camera.
PlayerInput s12_input(S12Script& script, StageCtx& ctx);

// After frame_step: the script's checks and lines. An exit code ends the run: 0 once the
// reset is proven, 1 (with a message) on a failed step.
std::optional<int> s12_after(S12Script& script, StageCtx& ctx);

// The opening line (both runs). False when the manifest's encounter is not Gaius or his
// fight is unwired.
bool s12_start(S12Script& script, StageCtx& ctx, double loadMs);
