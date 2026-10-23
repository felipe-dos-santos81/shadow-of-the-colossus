#pragma once

#include "math3d.h"

// The door fade machine (S8 Task 6, design D14): a pure, headless-testable clock
// for "walk through a door, fade out, swap the stage, fade in". `app.cpp` owns the
// stage switch and the fade quad; everything that answers a question about the
// sequence's timeline answers it here. No I/O, no SDL, no clock.
//
//   Idle --entered--> FadeOut --1.0 s--> Load --loadDone--> FadeIn --1.5 s--> Idle
//
// `fade` is 0 (clear) in Idle, ramps 0 -> 1 across FadeOut, is 1 through Load and
// ramps 1 -> 0 across FadeIn. Input is locked from the entering frame until
// FadeIn ends. `row` is the entered door (the app's door table row, app.h kDoors —
// its destination and arrival): set on entry, held through the sequence, -1 again
// in Idle.
//
// Load is the only phase left by something other than the clock: it waits for
// `loadDone`, however long the app takes, and one long frame crosses FadeOut but
// never Load. The frame that enters Load is the one `door_wants_load` answers true
// for, so the app switches stage exactly once. The frames that enter FadeOut (from
// `entered`) and FadeIn (from `loadDone`) do not spend their `dt`: a stage-load
// hitch must not eat the first fade-in seconds. `entered` is read only in Idle,
// `loadDone` only in Load. A negative or non-finite `dt` advances nothing.

enum class DoorPhase { Idle, FadeOut, Load, FadeIn };

struct DoorState {
    DoorPhase phase = DoorPhase::Idle;
    float t = 0.0f;     // seconds in the phase
    float fade = 0.0f;  // 0 clear .. 1 opaque
    bool inputLocked = false;
    int row = -1;
};

constexpr float kDoorFadeOut = 1.0f;  // seconds
constexpr float kDoorFadeIn = 1.5f;

DoorState step_door(DoorState s, float dt, bool entered, int row, bool loadDone);

// True on the step that enters Load (and only that one).
bool door_wants_load(const DoorState& before, const DoorState& after);

// True only when the segment's start is outside the axis-aligned box [mn, mx] and
// its end is inside. Bounds are inclusive. A spawn inside a box never fires.
bool box_entered(Vec3 prev, Vec3 now, Vec3 mn, Vec3 mx);

// Idle, unlocked, clear, no row (the `R` soft reset, from any phase).
DoorState reset_door();

// "Idle", "FadeOut", "Load", "FadeIn", for telemetry.
const char* door_phase_name(DoorPhase phase);
