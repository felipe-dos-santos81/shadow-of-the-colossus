#include "death.h"

#include <cmath>

namespace {

// A stalled or NaN frame must not poison `total`: the app writes it into the
// camera and the status line, and one NaN would stick there for the rest of
// the sequence. A negative dt never rewinds the clock.
float non_negative_dt(float dt) {
    return (std::isfinite(dt) && dt > 0.0f) ? dt : 0.0f;
}

}  // namespace

DeathState step_death(DeathState state, float dt, bool defeated, const EncounterDesc& desc) {
    const float motion = desc.death.motionSeconds;
    const float end = desc.death.endSeconds;
    if (state.phase == DeathPhase::Idle) {
        if (!defeated) {
            return state;
        }
        // A fresh sequence: drop any stale time/roots from a previous death.
        state = DeathState{};
        state.phase = DeathPhase::Motion;
        state.inputLocked = true;
        state.roots = desc.death.roots;
        return state;
    }

    state.total += non_negative_dt(dt);

    // Ordered by the later boundary first: one long frame may skip a phase.
    // Done is terminal and capped, so the claim "the corpse stays" is also a
    // claim that the state stops moving: `total`/`t` would otherwise grow (and
    // lose float precision) for the rest of the session.
    if (state.total >= end) {
        state.phase = DeathPhase::Done;
        state.total = end;
        state.t = 0.0f;
        state.inputLocked = false;
        state.corpseVisible = true;
    } else if (state.total >= motion) {
        state.phase = DeathPhase::Collapse;
        state.t = state.total - motion;
        state.inputLocked = true;
        state.corpseVisible = true;
    } else {
        state.phase = DeathPhase::Motion;
        state.t = state.total;
        state.inputLocked = true;
        state.corpseVisible = false;
    }
    return state;
}

DeathState reset_death() { return DeathState{}; }

bool death_locks_input(const DeathState& state) {
    return state.phase == DeathPhase::Motion || state.phase == DeathPhase::Tendrils ||
           state.phase == DeathPhase::Collapse;
}

bool death_corpse_visible(const DeathState& state) {
    return state.phase == DeathPhase::Collapse || state.phase == DeathPhase::Done;
}

bool death_tendrils_active(const DeathState& state, const EncounterDesc& desc) {
    if (state.phase != DeathPhase::Motion && state.phase != DeathPhase::Tendrils) {
        return false;
    }
    return state.total >= desc.death.tendrilStartSeconds &&
           state.total <= desc.death.tendrilEndSeconds;
}
