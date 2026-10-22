#pragma once

// Valus's death sequence (S6 Task 2, design §8.1, ruling D3): a pure,
// headless-testable phase machine, so the sequence's clock, input lock and
// corpse window do not live inside the app loop. `app.cpp` owns the encounter,
// the clip playback, the camera and the rendering; everything that answers a
// question about the death's timeline answers it here.
//
//     Idle --defeat--> Motion --kMotionSeconds--> Collapse --8.0 s--> Done
//                        \_ tendril burst 0.4-3.0 s _/    (corpse stays)
//
// The death clip is `minotaur_a_death_front`. Its runtime duration is the
// maximum input time, 6.166877 s (93 / 15.08056640625), not the 94-frame
// figure (6.2332 s): the last key sits one frame interval before the count.
// `kMotionSeconds` pins that duration, and the one-shot clip the app starts on
// the defeat must not outlive it.
//
// `DeathPhase::Tendrils` is part of the interface but the machine never enters
// it: the burst *overlaps* Motion (it is an effect window, not a playback
// phase), so `phase` stays Motion while `death_tendrils_active` answers the
// window. Idle is the only phase that reads `defeated`; the sequence is
// one-way until `reset_death`.
//
// The machine is pure: no I/O, no SDL and no clock. It advances `total` and
// derives the phase from it at the pinned boundaries, so a long frame cannot
// strand the sequence in an early phase and the result does not depend on how
// `dt` is sliced.

enum class DeathPhase { Idle, Motion, Tendrils, Collapse, Done };

struct DeathState {
    DeathPhase phase = DeathPhase::Idle;
    float t = 0.0f;      // seconds in the current phase
    float total = 0.0f;  // seconds since the defeat
    bool inputLocked = false;
    bool corpseVisible = false;
    int roots = 0;  // tendril roots: 2 from the defeat until the reset
};

// The pinned timeline (design §8.1 and §9). The motion duration is the clip's;
// the tendril start/end and the cinematic end are the builder's calls. Exported
// so the app and the tests read the same numbers instead of restating them.
inline constexpr float kDeathTendrilStart = 0.4f;
inline constexpr float kDeathTendrilEnd = 3.0f;
inline constexpr float kMotionSeconds = 6.166877f;  // 93 / 15.08056640625
inline constexpr float kDeathEnd = 8.0f;
inline constexpr int kDeathRoots = 2;

// One frame. In Idle, a true `defeated` starts the sequence (phase Motion,
// t = total = 0, input locked, two roots); a false one leaves the state
// untouched. Once running, `dt` advances `total` (a negative or non-finite
// `dt` advances nothing) and the phase follows `total`: Motion below
// `kMotionSeconds`, Collapse below `kDeathEnd`, Done from `kDeathEnd` on.
// `t` is the time inside the current phase, so a long frame's overshoot is
// kept. `defeated` is not re-read after the transition; only `reset_death`
// returns to Idle.
DeathState step_death(DeathState state, float dt, bool defeated);

// Idle, unlocked, no corpse, no roots.
DeathState reset_death();

// Phase-derived, so a state's flags cannot disagree with its phase: Motion and
// Collapse lock input (Idle and Done do not); Collapse and Done show the
// corpse; the tendrils are active only during the death motion and while
// 0.4 s <= total <= 3.0 s. `step_death` mirrors all three into the struct's
// fields for the app's telemetry.
bool death_locks_input(const DeathState& state);
bool death_corpse_visible(const DeathState& state);
bool death_tendrils_active(const DeathState& state);
