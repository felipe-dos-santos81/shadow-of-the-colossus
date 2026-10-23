#include "sword.h"

#include <algorithm>
#include <cmath>

namespace {

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

}  // namespace

SwordBeam sword_beam(Vec3 tip, Vec3 aim, Vec3 target, float raise) {
    SwordBeam b{};
    b.axis = vec3_normalize(aim);
    const Vec3 to{target.x - tip.x, target.y - tip.y, target.z - tip.z};
    b.dot = vec3_dot(vec3_normalize(to), b.axis);
    b.k = clamp01(90.0f * (1.0f - b.dot) / 180.0f);
    b.leanDeg = b.k * kLeanMaxDeg;
    b.widthTarget = clamp01((b.dot - 0.6f) / 0.4f) * kMaxLineWidth;
    b.locked = b.k < kLockK;
    b.rayLength = clamp01(raise) * kBeamRayPerRaise;

    const Vec3 up{0.0f, 1.0f, 0.0f};
    const Vec3 ref = std::fabs(vec3_dot(b.axis, up)) > 0.999f ? Vec3{1.0f, 0.0f, 0.0f} : up;
    const Vec3 u = vec3_normalize(vec3_cross(b.axis, ref));
    const Vec3 v = vec3_cross(u, b.axis);
    const float lean = b.leanDeg * kDegToRad;
    const float c = std::cos(lean), s = std::sin(lean);
    for (int i = 0; i < 8; ++i) {
        const float th = static_cast<float>(i) * 45.0f * kDegToRad;
        const float cv = s * std::cos(th), cu = s * std::sin(th);
        b.lines[i] = Vec3{c * b.axis.x + cv * v.x + cu * u.x, c * b.axis.y + cv * v.y + cu * u.y,
                          c * b.axis.z + cv * v.z + cu * u.z};
    }
    return b;
}

SwordState step_sword(SwordState s, float dt, const SwordInput& in, const SwordBeam& beam,
                      bool sunlit, float hitMetres) {
    if (!(std::isfinite(dt) && dt >= 0.0f)) return s;

    const bool held = in.held && in.allowed;
    const SwordClips& c = s.mounted ? kSwordMounted : kSwordFoot;

    // Transitions on the input, then the clock (a transition frame also advances).
    if (s.phase == SwordPhase::Lowered && held) {
        s.phase = SwordPhase::Raising;
        s.mounted = in.mounted;
        s.clip = (s.mounted ? kSwordMounted : kSwordFoot).start;
        s.clipTime = 0.0f;
    } else if ((s.phase == SwordPhase::Raising || s.phase == SwordPhase::Raised) && !held) {
        s.phase = SwordPhase::Lowering;
        s.raiseFrom = s.raise;
        s.clip = c.end;
        s.clipTime = 0.0f;
    }

    const SwordClips& k = s.mounted ? kSwordMounted : kSwordFoot;
    s.clipTime += s.phase == SwordPhase::Lowered ? 0.0f : dt;
    if (s.phase == SwordPhase::Raising && s.clipTime >= k.startS) {
        s.phase = SwordPhase::Raised;
        s.clip = k.loop;
        s.clipTime -= k.startS;
    }
    if (s.phase == SwordPhase::Lowering && s.clipTime >= k.endS) {
        s = SwordState{.light = s.light, .width = s.width, .sparkCooldown = s.sparkCooldown,
                       .sparks = s.sparks};
    }
    switch (s.phase) {
        case SwordPhase::Raising: s.raise = clamp01(s.clipTime / k.startS); break;
        case SwordPhase::Raised:
            s.raise = 1.0f;
            s.clipTime = std::fmod(s.clipTime, k.loopS);
            break;
        case SwordPhase::Lowering: s.raise = s.raiseFrom * (1.0f - clamp01(s.clipTime / k.endS)); break;
        case SwordPhase::Lowered: s.raise = 0.0f; break;
    }

    // Light moves toward the sun (or the shade) at a fixed rate.
    const float step = kLightRatePerSecond * dt;
    const float goal = sunlit ? 1.0f : 0.0f;
    s.light = clamp01(s.light + std::clamp(goal - s.light, -step, step));

    // Width eases toward the beam's target, independent of the step size.
    s.width += (beam.widthTarget - s.width) * (1.0f - std::pow(1.0f - kWidthStepFraction, 30.0f * dt));

    // A locked hit with the cooldown spent is a spark.
    s.sparkCooldown = std::max(0.0f, s.sparkCooldown - dt);
    if (s.raise > 0.0f && beam.locked && std::isfinite(hitMetres) && s.sparkCooldown <= 0.0f) {
        ++s.sparks;
        s.sparkCooldown = kSparkCooldown;
    }
    return s;
}

float sword_intensity(const SwordState& s) { return s.raise * s.light; }

bool sword_locks_movement(const SwordState& s) { return !s.mounted && s.phase != SwordPhase::Lowered; }

SwordState reset_sword() { return SwordState{}; }

const char* sword_phase_name(SwordPhase p) {
    switch (p) {
        case SwordPhase::Lowered: return "Lowered";
        case SwordPhase::Raising: return "Raising";
        case SwordPhase::Raised: return "Raised";
        case SwordPhase::Lowering: return "Lowering";
    }
    return "?";
}
