#include "behaviour.h"

#include <algorithm>
#include <cmath>

namespace {

bool state_in(const BehaviourDesc& desc, int state) {
    return state >= 0 && state < static_cast<int>(desc.states.size());
}

// A clip's usable length: 0 for -1, an index past the table, or a duration that
// is not positive and finite — such a clip never ends (and never starts an action).
float clip_seconds(std::span<const float> durations, int clip) {
    if (clip < 0 || clip >= static_cast<int>(durations.size())) {
        return 0.0f;
    }
    const float seconds = durations[static_cast<std::size_t>(clip)];
    return std::isfinite(seconds) && seconds > 0.0f ? seconds : 0.0f;
}

// The action a runtime plays, or nullptr when none (or its index left the slice).
const BehaviourAction* playing(const BehaviourDesc& desc, const BehaviourRuntime& rt) {
    const BehaviourStateDesc& state = desc.states[static_cast<std::size_t>(rt.state)];
    if (rt.action < 0 || rt.action >= state.actionCount) {
        return nullptr;
    }
    return &desc.actions[static_cast<std::size_t>(state.firstAction + rt.action)];
}

BehaviourRuntime enter(BehaviourRuntime rt, int state, float seconds) {
    rt.state = state;
    rt.stateSeconds = seconds;
    rt.action = kBehaviourNone;
    rt.actionSeconds = 0.0f;
    return rt;
}

}  // namespace

BehaviourRuntime step_behaviour(const BehaviourDesc& desc, BehaviourRuntime rt, float dt,
                                std::span<const float> clipDurations, int requestedAction) {
    if (!behaviour_valid(desc) || !state_in(desc, rt.state)) {
        return rt;
    }
    const bool advance = std::isfinite(dt) && dt > 0.0f;

    // 1. The cooldown. A non-finite one is spent: an infinite cooldown would
    // freeze the entity for good (advance_valus_attack keeps a +inf one).
    if (!std::isfinite(rt.cooldownSeconds)) {
        rt.cooldownSeconds = 0.0f;
    } else if (advance) {
        rt.cooldownSeconds = std::max(0.0f, rt.cooldownSeconds - dt);
    }

    // 2./3. The action playing at the frame's start, else the requested one.
    const BehaviourStateDesc& state = desc.states[static_cast<std::size_t>(rt.state)];
    if (rt.action != kBehaviourNone) {
        const BehaviourAction* action = playing(desc, rt);
        const float length = action != nullptr ? clip_seconds(clipDurations, action->clip) : 0.0f;
        if (advance) {
            rt.actionSeconds += dt;
        }
        if (length <= 0.0f) {
            rt.action = kBehaviourNone;  // nothing to play: dropped, no cooldown
            rt.actionSeconds = 0.0f;
        } else if (rt.actionSeconds >= length) {
            rt.cooldownSeconds = action->cooldownSeconds;
            rt.action = kBehaviourNone;
            rt.actionSeconds = 0.0f;
        }
    } else if (requestedAction >= 0 && requestedAction < state.actionCount &&
               rt.cooldownSeconds <= 0.0f) {
        const BehaviourAction& action =
            desc.actions[static_cast<std::size_t>(state.firstAction + requestedAction)];
        if (clip_seconds(clipDurations, action.clip) > 0.0f) {
            rt.action = requestedAction;
            rt.actionSeconds = 0.0f;
        }
    }

    // 4. The state clock and its transitions, overshoot carried.
    if (!advance) {
        return rt;
    }
    rt.stateSeconds += dt;
    // ponytail: 64 chained transitions per frame at most; every threshold is > 0,
    // so only a table of near-zero clips/timers could reach it.
    for (int chained = 0; chained < 64; ++chained) {
        const BehaviourStateDesc& s = desc.states[static_cast<std::size_t>(rt.state)];
        const float endAt = s.clipEndNext != kBehaviourNone ? clip_seconds(clipDurations, s.clip) : 0.0f;
        const float timerAt = s.timerNext != kBehaviourNone && std::isfinite(s.timerSeconds) &&
                                      s.timerSeconds > 0.0f
                                  ? s.timerSeconds
                                  : 0.0f;
        if (endAt > 0.0f && rt.stateSeconds >= endAt && (timerAt <= 0.0f || endAt <= timerAt)) {
            rt = enter(rt, s.clipEndNext, rt.stateSeconds - endAt);
        } else if (timerAt > 0.0f && rt.stateSeconds >= timerAt) {
            rt = enter(rt, s.timerNext, rt.stateSeconds - timerAt);
        } else {
            break;
        }
    }
    return rt;
}

BehaviourRuntime behaviour_enter(const BehaviourDesc& desc, BehaviourRuntime rt, int state) {
    if (!behaviour_valid(desc) || !state_in(desc, state)) {
        return rt;
    }
    return enter(rt, state, 0.0f);
}

WeakpointGate behaviour_gate(const BehaviourDesc& desc, int state, int weakpoint) {
    if (!behaviour_valid(desc) || !state_in(desc, state)) {
        return {};
    }
    for (const WeakpointGate& gate : desc.gates) {
        if (gate.weakpoint == weakpoint && (gate.state == kBehaviourNone || gate.state == state)) {
            return gate;
        }
    }
    return {};
}

BehaviourHit behaviour_hit(const BehaviourDesc& desc, BehaviourRuntime rt, int weakpoint) {
    BehaviourHit hit;
    hit.runtime = rt;
    if (weakpoint < 0 || !behaviour_valid(desc) || !state_in(desc, rt.state)) {
        return hit;
    }
    const WeakpointGate gate = behaviour_gate(desc, rt.state, weakpoint);
    hit.kind = gate.kind;
    if (gate.kind == GateKind::Damage) {
        if (const BehaviourAction* action = playing(desc, rt)) {
            hit.cancelledCooldownSeconds = action->cooldownSeconds;
        }
        hit.runtime.action = kBehaviourNone;
        hit.runtime.actionSeconds = 0.0f;
    } else if (gate.kind == GateKind::Transition) {
        hit.runtime = enter(rt, gate.target, 0.0f);
    }
    return hit;
}

int behaviour_clip(const BehaviourDesc& desc, const BehaviourRuntime& rt) {
    if (!behaviour_valid(desc) || !state_in(desc, rt.state)) {
        return kBehaviourNone;
    }
    const BehaviourAction* action = playing(desc, rt);
    return action != nullptr ? action->clip : desc.states[static_cast<std::size_t>(rt.state)].clip;
}

float behaviour_turn_rate(const BehaviourDesc& desc, const BehaviourRuntime& rt) {
    return state_in(desc, rt.state)
               ? desc.states[static_cast<std::size_t>(rt.state)].turnRateDegPerSecond
               : 0.0f;
}
