#pragma once

// The return-to-shrine phase machine (S7 Task 4, design §8.4): a pure,
// headless-testable clock for "Valus is dead, walk back to the shrine". `app.cpp`
// owns the stage switch, the shrine scene and the fade quad's drawing; everything
// that answers a question about the sequence's timeline answers it here.
//
//   Idle --deathDone--> Hold --hold--> FadeOut --fadeOut--> Load
//        Load --loadDone--> FadeIn --fadeIn--> Wake --wake--> Free
//
// The fade is 0 (clear) in Idle, Hold, Wake and Free, ramps 0 -> 1 across FadeOut,
// is 1 through Load and ramps 1 -> 0 across FadeIn. Input is locked from FadeOut
// until Wake ends; Hold is unlocked (the player still owns the arena) and Free is
// unlocked (the player owns the shrine).
//
// Load is the only phase the machine does not leave by the clock: it waits for
// `loadDone`, however long the app takes, and one long frame can cross Hold and
// FadeOut but never Load. The frame that enters Load is the one `return_wants_load`
// answers true for, so the app asks for the stage switch exactly once. The frames
// that enter Hold (from `deathDone`) and FadeIn (from `loadDone`) do not spend
// their `dt`: a death-end or a stage-load hitch must not eat the first hold or
// fade-in seconds.
//
// `wake` is data, not a constant: it defaults to 0 (Wake passes straight to Free
// in the same step) and the app sets it from the shrine's authored values.
//
// The machine is pure: no I/O, no SDL and no clock. Free is terminal; only
// `reset_return` goes back to Idle (the `R` soft reset, from any phase).

enum class ReturnPhase { Idle, Hold, FadeOut, Load, FadeIn, Wake, Free };

struct ReturnState {
    ReturnPhase phase = ReturnPhase::Idle;
    float t = 0.0f;     // seconds in the phase
    float fade = 0.0f;  // 0 clear .. 1 opaque
    bool inputLocked = false;
};

struct ReturnTimings {
    float hold = 2.0f, fadeOut = 1.0f, fadeIn = 1.5f, wake = 0.0f;
};

// One frame. `deathDone` is read only in Idle and `loadDone` only in Load. A
// negative or non-finite `dt` advances nothing; a long one carries its overshoot
// across the timed phases (see above) and stops at Load. `fade` is always in
// [0, 1] and finite.
ReturnState step_return(ReturnState s, float dt, const ReturnTimings& tm, bool deathDone,
                        bool loadDone);

// Idle, unlocked, clear.
ReturnState reset_return();

// True on the step that enters Load (and only that one).
bool return_wants_load(const ReturnState& before, const ReturnState& after);

// "Idle", "Hold", "FadeOut", "Load", "FadeIn", "Wake", "Free", for telemetry.
const char* return_phase_name(ReturnPhase phase);
