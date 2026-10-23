#pragma once

#include <cstddef>
#include <span>

// The entity behaviour machine (S11 Task 8, design D9 and §13.10): a pure,
// data-driven state machine for a colossus. It owns no entity: the tables say
// which states exist, which clips each plays, what ends a state, and what a
// weakpoint hit does in each state; `app.cpp` (after the S10 split) owns the
// playback, the damage amount, the reactions and the selection of an attack.
// No I/O, no SDL, no clock, no bull or minotaur names: a colossus's data lives
// in its own table (`quadratus.h`, the EncounterDesc), not here.
//
// Every table reference is an `int` index, `kBehaviourNone` (-1) = none, so a
// table is a plain constexpr aggregate and `behaviour_valid` can static_assert
// it.
//
//   state --base clip ends (clipEndNext)--> state'     (the earlier of the two
//   state --timerSeconds elapse (timerNext)--> state'   thresholds fires; a tie
//   state --gate: weakpoint hit, Transition--> state'   goes to the clip end)
//
// A state's clip set is its base clip plus an optional slice of `actions` (an
// attack each: its clip and the cooldown it sets when the clip ends). The
// caller chooses an action (Valus: `select_valus_attack`) and passes its index
// within the state's slice to `step_behaviour`; the machine starts it, runs it
// to its clip end and sets the cooldown — the same sequence as S4.2's
// `advance_valus_attack`, which `test_behaviour.cpp` proves frame by frame.

inline constexpr int kBehaviourNone = -1;

// What a weakpoint hit does in a state.
enum class GateKind { Ignore, Damage, Transition };

// One gate row. `state` -1 matches every state; rows are searched in order and
// the first match wins, so put state-specific rows before the wildcards. A hit
// on a weakpoint with no matching row is Ignore (no damage, no transition).
struct WeakpointGate {
    int state = kBehaviourNone;
    int weakpoint = kBehaviourNone;
    GateKind kind = GateKind::Ignore;
    int target = kBehaviourNone;  // the state a Transition enters
};

// An attack within a state: its clip and the cooldown its clip end sets.
struct BehaviourAction {
    int clip = kBehaviourNone;
    float cooldownSeconds = 0.0f;
};

struct BehaviourStateDesc {
    int clip = kBehaviourNone;         // the base clip; -1 none
    int clipEndNext = kBehaviourNone;  // -1: the base clip loops and holds the state
    float timerSeconds = 0.0f;         // > 0 arms the timer
    int timerNext = kBehaviourNone;
    float turnRateDegPerSecond = 0.0f;  // the most the entity may turn per second here
    int firstAction = 0;                // the state's slice of BehaviourDesc::actions
    int actionCount = 0;
};

struct BehaviourDesc {
    std::span<const BehaviourStateDesc> states;
    std::span<const BehaviourAction> actions;
    std::span<const WeakpointGate> gates;
};

// The per-entity runtime. Plain data: the app may read every field and may set
// `cooldownSeconds` (Valus's cooldown deferred past a reaction) itself.
struct BehaviourRuntime {
    int state = 0;
    float stateSeconds = 0.0f;  // time in the state (the base clip's time)
    int action = kBehaviourNone;  // index within the state's action slice
    float actionSeconds = 0.0f;
    float cooldownSeconds = 0.0f;
};

// Whether every index in the table is -1 or in range: the transitions' and the
// gates' targets name states, the gates' states name states (or -1), the action
// slices lie inside `actions`, a Transition gate has a target. Clips are not
// checked: they index the entity's clip table, which the table does not hold.
constexpr bool behaviour_valid(const BehaviourDesc& desc) {
    const int states = static_cast<int>(desc.states.size());
    const int actions = static_cast<int>(desc.actions.size());
    const auto state_or_none = [states](int i) { return i == kBehaviourNone || (i >= 0 && i < states); };
    const auto state_index = [states](int i) { return i >= 0 && i < states; };
    if (states == 0) {
        return false;
    }
    for (const BehaviourStateDesc& s : desc.states) {
        if (!state_or_none(s.clipEndNext) || !state_or_none(s.timerNext)) {
            return false;
        }
        if (s.firstAction < 0 || s.actionCount < 0 || s.firstAction + s.actionCount > actions) {
            return false;
        }
    }
    for (const WeakpointGate& g : desc.gates) {
        if (!state_or_none(g.state) || g.weakpoint < 0) {
            return false;
        }
        if (g.kind == GateKind::Transition && !state_index(g.target)) {
            return false;
        }
    }
    return true;
}

// One frame. `clipDurations` is the entity's clip table (seconds, by clip
// index); a clip that is -1, out of that table, or not positive and finite
// never ends. In order, mirroring `advance_valus_attack`:
//   1. the cooldown ticks down by dt (to 0; a non-finite cooldown becomes 0);
//   2. an action playing at the frame's start advances, and at its clip end
//      sets its cooldown and returns to the base clip — no new action that
//      frame;
//   3. otherwise `requestedAction` (an index within the state's slice, or -1)
//      starts at t = 0 when the cooldown is spent and its clip has a length;
//   4. the state clock advances and fires clip-end/timer transitions, carrying
//      the overshoot into the next state, so the result does not depend on how
//      dt is sliced. Entering a state drops any action (the cooldown stays).
// A negative or non-finite dt advances nothing. A runtime whose state is out of
// range, or an invalid table, is returned unchanged.
BehaviourRuntime step_behaviour(const BehaviourDesc& desc, BehaviourRuntime rt, float dt,
                                std::span<const float> clipDurations,
                                int requestedAction = kBehaviourNone);

// What one weakpoint hit did.
struct BehaviourHit {
    GateKind kind = GateKind::Ignore;
    BehaviourRuntime runtime;            // after the hit
    float cancelledCooldownSeconds = 0;  // a Damage hit cancels the action; its cooldown,
                                         // for the app to start after the reaction
};

// A weakpoint hit in the runtime's state. Damage: the app applies its damage;
// an action in progress is cancelled (S4.2: a landed strike cancels the attack
// and its pending impact) and its cooldown is returned, not set. Transition:
// the target state is entered at t = 0 and any action is dropped. Ignore: the
// runtime is unchanged. An out-of-range state, a negative weakpoint, an invalid
// table or a Transition row with a bad target all answer Ignore.
BehaviourHit behaviour_hit(const BehaviourDesc& desc, BehaviourRuntime rt, int weakpoint);

// The gate a weakpoint hit meets in `state` (the first matching row), or an
// Ignore row when none matches.
WeakpointGate behaviour_gate(const BehaviourDesc& desc, int state, int weakpoint);

// The clip the entity plays now: the action's clip while one plays, else the
// state's base clip; -1 for an out-of-range state.
int behaviour_clip(const BehaviourDesc& desc, const BehaviourRuntime& rt);

// The state's turn rate (deg/s), 0 for an out-of-range state.
float behaviour_turn_rate(const BehaviourDesc& desc, const BehaviourRuntime& rt);
