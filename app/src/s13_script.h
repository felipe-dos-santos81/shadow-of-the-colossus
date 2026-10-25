#pragma once

// The --s13 script (S13 Task 16): one scripted player for --s13-selftest and --s13-e2e.
// Both drive the production frame (frame_step) from Phaedra's arena start: walk out to her
// platform until she stomps, run off her line, run into the west stair tunnel and whistle
// until she routes to its inner GP and peeps, leave by the north-west tunnel, grab her tail in
// a `watch_*` window, climb tail -> back -> withers -> neck, strike the neck trigger twice (neck
// down 1, then 2), climb the neck to the head, hold on through a shake, strike the head sigil
// to her death, watch it to Done (the corpse stays), then R. The script only picks keys (and,
// like the S8/S9/S11/S12 scripts, turns the camera to the heading it walks); it reads the
// game state the way a player watches it. The selftest adds the stills and the perf run; the
// e2e runs the real loop.

#include "interactive.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct S13Script {
    const char* prefix = "s13";  // the lines' prefix: "s13" or "s13e2e"
    int phase = 0;
    int phasesDone = 0;
    float phaseSeconds = 0.0f;
    float seconds = 0.0f;  // fixed-step seconds since the start
    int frames = 0;
    int waypoint = 0;      // the walk's next waypoint in this phase
    bool whistle = false;  // this frame's whistle key (the e2e passes it as the S8 key)
    // The strike routine (align, charge, release, regrab).
    int step = 0;
    float stepSeconds = 0.0f;
    float lastDistance = -1.0f;
    int axis = 0;
    float along = 1.0f;
    float lateral = 1.0f;
    int flips = 0;
    int strikes = 0;
    int necks = 0;      // neck trigger strikes landed
    int neckLines = 0;  // neck-down stages seen (s13-neck lines)
    // The once-only lines and their memory.
    int lastMode = -1;
    int lastRoute = -2;
    int lastGp = -2;
    int lastBand = -2;
    int lastDeathPhase = -1;
    bool sawStomp = false;
    int stompRows = 0;          // the stomp's damage rows that overlapped the player during the dodge
    std::string stompClip;
    float stompOff = 0.0f;      // the player's widest distance off her facing line during the stomp
    float stompNear = 1e9f;     // ... and the nearest to her root
    bool peeped = false;
    uint64_t peepGround = 0;  // her ground word when she peeped
    bool leftRuins = false;   // her root's ground word left the ruins' after the peep (s13-ground_handoff)
    bool arrivedGp = false;   // the picked GP's arrival line printed
    // The first shake on the hung neck (s13-shake_hung).
    bool hungActive = false, hungDone = false;
    std::string hungClip;
    float hungStart = 0.0f, hungEnd = 0.0f, hungFrom = 0.0f, hungDrain = 0.0f;
    float arrivedAt = -1.0f;
    std::string entryClip;  // the tail grab's clip and frame
    int entryFrame = -1;
    std::vector<std::string> route;  // the bands climbed, in order
    bool sawShake = false;
    bool shookLast = false;
    float shakeLow = 100.0f;
    std::string shakeClip;
    float shakeStart = 0.0f;  // stamina when it began
    float shakeDrain = 0.0f;  // the strongest drain (kirin_fight_shake) during the shake
    float sinceShake = -1.0f;
    Vec3 diedAt{};
    Vec3 staging{};   // where it waits for the tail's quiet frames
    int retries = 0;  // windows it gave up on
    // A stalled walk's sidestep.
    Vec3 progressFrom{};
    float progressSeconds = 0.0f;
    float detourSeconds = 0.0f;
    float detourSide = 1.0f;
    bool grabPressed = false;
    // Requests for the caller this frame: R, and the selftest's still.
    bool wantR = false;
    std::string still;
    bool stillSide = false;  // the selftest frames the still from her side (not the climb camera)
};

// This frame's held keys (before frame_step) and `whistle`; may turn the camera.
PlayerInput s13_input(S13Script& script, StageCtx& ctx);

// After frame_step: the script's checks and lines. An exit code ends the run: 0 once the
// reset is proven, 1 (with a message) on a failed step.
std::optional<int> s13_after(S13Script& script, StageCtx& ctx);

// The opening lines (both runs). False when the manifest's encounter is not Phaedra or her
// fight is unwired.
bool s13_start(S13Script& script, StageCtx& ctx, double loadMs);
