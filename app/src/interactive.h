#pragma once

// The interactive loop (S10 Task 6d): the keyboard, the gamepad and the window
// drive the stage; --s7-e2e, --s8-e2e and --s9-e2e run the same loop with a
// scripted input source. frame_step is one manifest-mode frame of it.

#include "selftests.h"

#include <functional>
#include <optional>

// One frame's input: the live keyboard/gamepad or an e2e script's, decided
// before the frame, plus the hooks --s7-e2e checks the frame with.
struct FrameInput {
    float dt = 0.0f;
    float dragDx = 0.0f;  // the mouse drag and the right stick (pixels)
    float dragDy = 0.0f;
    float wheel = 0.0f;
    bool climbRecenter = false;  // F while gripping
    bool live = true;            // the stage still runs (no quit, R or exit before the frame)
    PlayerInput input;           // the held movement/grab/strike keys
    RideKeys rideKeys;           // S8: the plain's held ride keys
    bool swordHeld = false;      // S9: C / the left shoulder
    // An e2e script's one lethal hit (landed as a strike lands); a hit that does
    // not defeat Valus ends the run with lethalMissed()'s exit code.
    bool lethalHit = false;
    std::function<int()> lethalMissed;
    // --s9-e2e: the scripted look, after the follow camera, before the sword aims.
    std::optional<Vec3> lookAlong;
    // --s7-e2e: after the death and return machines step (it watches them and
    // picks its input), after the input lock (its checks; an exit code ends
    // the run), and in a wake frame (the clip it saw).
    std::function<void(PlayerInput&)> afterMachines;
    std::function<std::optional<int>(bool inputLocked, bool moving)> afterLock;
    std::function<void()> onWake;
};

// What the frame left: the run's exit code (a script's check failed), the
// stage request (the return's or a door's Load), the sword frame's readings
// (when the sword is wired), and this frame's tendril and sword light.
struct FrameResult {
    std::optional<int> exitCode;
    std::optional<StageRequest> request;
    std::optional<SwordProbe> probe;
    EffectFrame effect;
    SwordFxFrame sword;
};

FrameResult frame_step(StageCtx& ctx, const FrameInput& in);

SelftestResult run_interactive(StageCtx& ctx, RunState& run);
