#pragma once

#include "encounter_desc.h"

// Valus's death sequence (S6 Task 2, design §8.1, ruling D3): a pure,
// headless-testable phase machine, so the sequence's clock, input lock and
// corpse window do not live inside the app loop. `app.cpp` owns the encounter,
// the clip playback, the camera and the rendering; everything that answers a
// question about the death's timeline answers it here.
//
//     Idle --defeat--> Motion --motionSeconds--> Collapse --total 8.0 s--> Done
//                        \_ tendril burst 0.4-3.0 s _/     (corpse stays)
//
// Collapse is then 1.833 s long; the 8.0 s in the diagram is the defeat clock,
// not the collapse's own duration.
//
// The death clip is `minotaur_a_death_front`. Its runtime duration is the
// maximum input time, 6.166877 s (93 / 15.08056640625), not the 94-frame
// figure (6.2332 s): the last key sits one frame interval before the count.
// R8 in `.superpowers/sdd/2026-10-22-s6-the-fall/progress.md` retires the
// design's 6.233 s. The table's `death.motionSeconds` pins the duration.
//
// Task 5 guidance (the clip is driven from this machine's clock, not a second
// clock): compute `clipTime = std::min(death.total, desc.death.motionSeconds)` each
// frame, or skip the clip's own `dt` on the transition frame; the defeat frame
// deliberately starts `total` at 0 and does not accumulate that frame's `dt`.
// The glb sampler max (6.1668767929077 s) and the constant are the same float,
// but the exact quotient is 6.16687727 s, so compare a loaded clip's duration
// with at least a 1e-5 relative tolerance instead of equality. The recon note
// printed `6.166933 s` in an earlier revision; that was a typo and it is
// corrected — R8's `6.166877` is the pinned value.
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
    float t = 0.0f;      // seconds in the current phase (0 in Idle and Done)
    float total = 0.0f;  // seconds since the defeat, capped at death.endSeconds in Done
    bool inputLocked = false;
    bool corpseVisible = false;
    int roots = 0;  // tendril roots: death.roots from the defeat until the reset
};

// The timeline is the encounter's `death` bundle (S11 M-1): `motionSeconds`
// (Valus: 6.166877 s, 93 / 15.08056640625), the tendril window
// [`tendrilStartSeconds`, `tendrilEndSeconds`] (0.4-3.0 s), the cinematic's
// `endSeconds` (8.0 s) and the `roots` count (2). Every reader takes the
// encounter's description; nothing here names a colossus.

// One frame. In Idle, a true `defeated` starts the sequence (phase Motion,
// t = total = 0, input locked, `death.roots` roots); a false one leaves the
// state untouched. Once running, `dt` advances `total` (a negative or
// non-finite `dt` advances nothing) and the phase follows `total`: Motion below
// `death.motionSeconds`, Collapse below `death.endSeconds`, Done from there on.
// A running phase's `t` keeps a long frame's overshoot; Done instead clamps
// `total` to `death.endSeconds` and `t` to 0, so the terminal state is exactly
// stable and every further step returns it unchanged. `defeated` is not re-read
// after the transition; only `reset_death` returns to Idle.
DeathState step_death(DeathState state, float dt, bool defeated, const EncounterDesc& desc);

// Idle, unlocked, no corpse, no roots.
DeathState reset_death();

// Phase-derived, so a state's flags cannot disagree with its phase: Motion and
// Collapse lock input (Idle and Done do not); Collapse and Done show the
// corpse; the tendrils are active only during the death motion and while
// total is in `desc.death`'s tendril window. `step_death` mirrors the first two
// into the struct's fields for the app's telemetry.
bool death_locks_input(const DeathState& state);
bool death_corpse_visible(const DeathState& state);
bool death_tendrils_active(const DeathState& state, const EncounterDesc& desc);
