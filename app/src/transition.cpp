#include "transition.h"

#include <cmath>

namespace {

// A stalled or NaN frame must not poison `t` or `fade`; a negative dt never
// rewinds the clock (the same idiom as death.cpp).
float non_negative_dt(float dt) {
    return (std::isfinite(dt) && dt > 0.0f) ? dt : 0.0f;
}

float unit(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// Enters `phase` at t = 0 with its fixed fade and lock.
void enter(ReturnState& s, ReturnPhase phase, float fade, bool locked) {
    s.phase = phase;
    s.t = 0.0f;
    s.fade = fade;
    s.inputLocked = locked;
}

}  // namespace

ReturnState step_return(ReturnState s, float dt, const ReturnTimings& tm, bool deathDone,
                        bool loadDone) {
    if (s.phase == ReturnPhase::Idle) {
        if (deathDone) {
            enter(s, ReturnPhase::Hold, 0.0f, false);
        }
        return s;
    }
    if (s.phase == ReturnPhase::Load) {
        if (loadDone) {
            enter(s, ReturnPhase::FadeIn, 1.0f, true);
        }
        return s;
    }

    // Timed phases: `rem` is the time left to spend. Each pass either absorbs it
    // inside the phase (and returns) or moves to the next phase with the
    // overshoot, so the result does not depend on how `dt` is sliced. A duration
    // that is zero, negative or NaN is never "longer than the time left" and is
    // crossed at once.
    float rem = non_negative_dt(dt);
    for (;;) {
        switch (s.phase) {
            case ReturnPhase::Hold:
                if (s.t + rem < tm.hold) {
                    s.t += rem;
                    return s;
                }
                rem = s.t + rem - tm.hold;
                enter(s, ReturnPhase::FadeOut, 0.0f, true);
                break;
            case ReturnPhase::FadeOut:
                if (s.t + rem < tm.fadeOut) {
                    s.t += rem;
                    s.fade = unit(s.t / tm.fadeOut);
                    return s;
                }
                enter(s, ReturnPhase::Load, 1.0f, true);  // never cross Load
                return s;
            case ReturnPhase::FadeIn:
                if (s.t + rem < tm.fadeIn) {
                    s.t += rem;
                    s.fade = unit(1.0f - s.t / tm.fadeIn);
                    return s;
                }
                rem = s.t + rem - tm.fadeIn;
                enter(s, ReturnPhase::Wake, 0.0f, true);
                break;
            case ReturnPhase::Wake:
                if (s.t + rem < tm.wake) {
                    s.t += rem;
                    return s;
                }
                enter(s, ReturnPhase::Free, 0.0f, false);
                return s;
            default:  // Free (terminal); Idle and Load returned above
                return s;
        }
    }
}

ReturnState reset_return() { return ReturnState{}; }

bool return_wants_load(const ReturnState& before, const ReturnState& after) {
    return before.phase != ReturnPhase::Load && after.phase == ReturnPhase::Load;
}

const char* return_phase_name(ReturnPhase phase) {
    switch (phase) {
        case ReturnPhase::Idle: return "Idle";
        case ReturnPhase::Hold: return "Hold";
        case ReturnPhase::FadeOut: return "FadeOut";
        case ReturnPhase::Load: return "Load";
        case ReturnPhase::FadeIn: return "FadeIn";
        case ReturnPhase::Wake: return "Wake";
        case ReturnPhase::Free: return "Free";
    }
    return "?";
}
