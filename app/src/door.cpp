#include "door.h"

#include <cmath>

namespace {

// A stalled or NaN frame must not poison `t` or `fade`; a negative dt never
// rewinds the clock (the same idiom as transition.cpp).
float non_negative_dt(float dt) {
    return (std::isfinite(dt) && dt > 0.0f) ? dt : 0.0f;
}

float unit(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

bool inside(Vec3 p, Vec3 mn, Vec3 mx) {
    return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y && p.z >= mn.z &&
           p.z <= mx.z;
}

}  // namespace

DoorState step_door(DoorState s, float dt, bool entered, int target, bool loadDone) {
    switch (s.phase) {
        case DoorPhase::Idle:
            if (entered) {
                s = DoorState{DoorPhase::FadeOut, 0.0f, 0.0f, true, target};
            }
            return s;
        case DoorPhase::FadeOut:
            s.t += non_negative_dt(dt);
            if (s.t < kDoorFadeOut) {
                s.fade = unit(s.t / kDoorFadeOut);
            } else {
                s.phase = DoorPhase::Load;  // never cross Load
                s.t = 0.0f;
                s.fade = 1.0f;
            }
            return s;
        case DoorPhase::Load:
            if (loadDone) {
                s.phase = DoorPhase::FadeIn;
                s.t = 0.0f;
                s.fade = 1.0f;
            }
            return s;
        case DoorPhase::FadeIn:
            s.t += non_negative_dt(dt);
            if (s.t < kDoorFadeIn) {
                s.fade = unit(1.0f - s.t / kDoorFadeIn);
                return s;
            }
            return reset_door();
    }
    return s;
}

bool door_wants_load(const DoorState& before, const DoorState& after) {
    return before.phase != DoorPhase::Load && after.phase == DoorPhase::Load;
}

bool box_entered(Vec3 prev, Vec3 now, Vec3 mn, Vec3 mx) {
    return !inside(prev, mn, mx) && inside(now, mn, mx);
}

DoorState reset_door() { return DoorState{}; }

const char* door_phase_name(DoorPhase phase) {
    switch (phase) {
        case DoorPhase::Idle: return "Idle";
        case DoorPhase::FadeOut: return "FadeOut";
        case DoorPhase::Load: return "Load";
        case DoorPhase::FadeIn: return "FadeIn";
    }
    return "?";
}
