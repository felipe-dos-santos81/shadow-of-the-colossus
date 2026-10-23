// The sword module (S9 Task 2, design §6/D5/D6): pure. The beam's curves at
// their edges, the width and light steps, the phase walk, the sparks, the
// movement lock and the reset. No scene, no window, no clock.
#include <doctest.h>

#include <cmath>
#include <cstring>
#include <limits>

#include "sword.h"

namespace {

constexpr float kEps = 1e-5f;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

// A beam whose aim is +X and whose target sits at angle acos(d) from it.
SwordBeam beam_at_dot(float d, float raise = 1.0f) {
    const Vec3 aim{1.0f, 0.0f, 0.0f};
    const Vec3 target{d, std::sqrt(1.0f - d * d), 0.0f};
    return sword_beam(Vec3{0, 0, 0}, aim, target, raise);
}

float angle_deg(Vec3 a, Vec3 b) {
    return std::acos(std::fmin(1.0f, std::fmax(-1.0f, vec3_dot(a, b)))) / kDegToRad;
}

// Held on foot, sunlit, no beam of interest.
SwordInput held_foot() { return SwordInput{true, false, true}; }
SwordInput released() { return SwordInput{false, false, true}; }
const SwordBeam kIdleBeam = beam_at_dot(0.0f);

}  // namespace

TEST_CASE("curves at the edges") {
    struct Row { float dot, k, lean, width; };
    const Row rows[] = {{1.0f, 0.0f, 0.0f, 1.35f},   {0.8f, 0.1f, 7.5f, 0.675f},
                        {0.6f, 0.2f, 15.0f, 0.0f},   {-1.0f, 1.0f, 75.0f, 0.0f}};
    for (const Row& r : rows) {
        const SwordBeam b = beam_at_dot(r.dot);
        CHECK(b.dot == doctest::Approx(r.dot).epsilon(kEps));
        CHECK(b.k == doctest::Approx(r.k).epsilon(kEps));
        CHECK(b.leanDeg == doctest::Approx(r.lean).epsilon(kEps));
        CHECK(b.widthTarget == doctest::Approx(r.width).epsilon(kEps));
    }
    CHECK(beam_at_dot(1.0f).locked);
    CHECK_FALSE(beam_at_dot(0.8f).locked);
    CHECK_FALSE(beam_at_dot(-1.0f).locked);
}

TEST_CASE("the lock boundary: 0.979 free, 0.981 and 0.99 locked") {
    // 0.98 sits on the float boundary of the strict k < 0.01 and is not tested.
    CHECK_FALSE(beam_at_dot(0.979f).locked);
    CHECK(beam_at_dot(0.981f).locked);
    CHECK(beam_at_dot(0.99f).locked);
}

TEST_CASE("the ray is raise * 150 m") {
    CHECK(beam_at_dot(1.0f, 0.5f).rayLength == doctest::Approx(75.0f));
    CHECK(beam_at_dot(1.0f, 1.0f).rayLength == doctest::Approx(150.0f));
    CHECK(beam_at_dot(1.0f, 0.0f).rayLength == doctest::Approx(0.0f));
}

TEST_CASE("the eight lines fan around the axis at the lean") {
    const SwordBeam b = beam_at_dot(0.6f);  // lean 15 degrees
    CHECK(b.axis.x == doctest::Approx(1.0f));
    for (const Vec3& l : b.lines) {
        CHECK(vec3_dot(l, l) == doctest::Approx(1.0f).epsilon(1e-4));
        CHECK(angle_deg(l, b.axis) == doctest::Approx(b.leanDeg).epsilon(1e-4));
    }
    // Adjacent radial components (the part orthogonal to the axis) are 45 degrees apart.
    for (int i = 0; i < 8; ++i) {
        const Vec3 a = b.lines[i], c = b.lines[(i + 1) % 8];
        const float da = vec3_dot(a, b.axis), dc = vec3_dot(c, b.axis);
        const Vec3 ra = vec3_normalize({a.x - da * b.axis.x, a.y - da * b.axis.y, a.z - da * b.axis.z});
        const Vec3 rc = vec3_normalize({c.x - dc * b.axis.x, c.y - dc * b.axis.y, c.z - dc * b.axis.z});
        CHECK(angle_deg(ra, rc) == doctest::Approx(45.0f).epsilon(1e-3));
    }
}

TEST_CASE("k 0 collapses every line onto the axis") {
    const SwordBeam b = beam_at_dot(1.0f);
    for (const Vec3& l : b.lines) {
        CHECK(l.x == doctest::Approx(b.axis.x).epsilon(kEps));
        CHECK(l.y == doctest::Approx(b.axis.y).epsilon(1e-4));
        CHECK(l.z == doctest::Approx(b.axis.z).epsilon(1e-4));
    }
}

TEST_CASE("a vertical aim takes the X fallback and stays finite") {
    const SwordBeam b = sword_beam(Vec3{0, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0.6f, 0.8f}, 1.0f);
    for (const Vec3& l : b.lines) {
        CHECK(std::isfinite(l.x));
        CHECK(std::isfinite(l.y));
        CHECK(std::isfinite(l.z));
        CHECK(angle_deg(l, b.axis) == doctest::Approx(b.leanDeg).epsilon(1e-4));
    }
}

TEST_CASE("width smoothing is step-size independent") {
    SwordState s;
    s = step_sword(s, 1.0f / 30.0f, released(), beam_at_dot(1.0f), true, kNaN);
    CHECK(s.width == doctest::Approx(0.135f).epsilon(kEps));
    SwordState t;
    t = step_sword(t, 1.0f / 60.0f, released(), beam_at_dot(1.0f), true, kNaN);
    t = step_sword(t, 1.0f / 60.0f, released(), beam_at_dot(1.0f), true, kNaN);
    CHECK(t.width == doctest::Approx(0.135f).epsilon(kEps));
}

TEST_CASE("light fades at 9.0/s") {
    SwordState s;
    s.light = 1.0f;
    for (int i = 0; i < 6; ++i) s = step_sword(s, 1.0f / 60.0f, released(), kIdleBeam, false, kNaN);
    CHECK(s.light == doctest::Approx(0.1f).epsilon(1e-4));
    s = step_sword(s, 1.0f / 60.0f, released(), kIdleBeam, false, kNaN);
    CHECK(s.light == 0.0f);
    for (int i = 0; i < 10; ++i) s = step_sword(s, 1.0f / 60.0f, released(), kIdleBeam, false, kNaN);
    CHECK(s.light == 0.0f);
    // And back up, never past 1.
    for (int i = 0; i < 30; ++i) {
        s = step_sword(s, 1.0f / 60.0f, released(), kIdleBeam, true, kNaN);
        CHECK(s.light >= 0.0f);
        CHECK(s.light <= 1.0f);
    }
    CHECK(s.light == 1.0f);
}

namespace {
// Walks the held -> released cycle for one clip set.
void walk_phases(bool mounted, const SwordClips& c) {
    SwordInput in{true, mounted, true};
    SwordState s = step_sword({}, 0.0f, in, kIdleBeam, true, kNaN);
    CHECK(s.phase == SwordPhase::Raising);
    CHECK(std::strcmp(s.clip, c.start) == 0);
    CHECK(s.mounted == mounted);

    s = step_sword(s, c.startS / 2, in, kIdleBeam, true, kNaN);
    CHECK(s.phase == SwordPhase::Raising);
    CHECK(s.raise == doctest::Approx(0.5f).epsilon(1e-4));
    s = step_sword(s, c.startS / 2, in, kIdleBeam, true, kNaN);
    CHECK(s.phase == SwordPhase::Raised);
    CHECK(std::strcmp(s.clip, c.loop) == 0);
    CHECK(s.raise == doctest::Approx(1.0f));

    in.held = false;
    s = step_sword(s, 0.0f, in, kIdleBeam, true, kNaN);
    CHECK(s.phase == SwordPhase::Lowering);
    CHECK(std::strcmp(s.clip, c.end) == 0);
    s = step_sword(s, c.endS / 2, in, kIdleBeam, true, kNaN);
    CHECK(s.phase == SwordPhase::Lowering);
    CHECK(s.raise == doctest::Approx(0.5f).epsilon(1e-4));
    s = step_sword(s, c.endS / 2, in, kIdleBeam, true, kNaN);
    CHECK(s.phase == SwordPhase::Lowered);
    CHECK(s.raise == 0.0f);
    CHECK(s.clip == nullptr);
}
}  // namespace

TEST_CASE("phases: on foot") { walk_phases(false, kSwordFoot); }
TEST_CASE("phases: mounted") { walk_phases(true, kSwordMounted); }

TEST_CASE("a release mid-raise lowers from the reached raise") {
    SwordState s = step_sword({}, 0.0f, held_foot(), kIdleBeam, true, kNaN);
    s = step_sword(s, kSwordFoot.startS * 0.4f, held_foot(), kIdleBeam, true, kNaN);
    CHECK(s.raise == doctest::Approx(0.4f).epsilon(1e-4));
    s = step_sword(s, 0.0f, released(), kIdleBeam, true, kNaN);
    CHECK(s.phase == SwordPhase::Lowering);
    CHECK(s.raise == doctest::Approx(0.4f).epsilon(1e-4));
    s = step_sword(s, kSwordFoot.endS / 2, released(), kIdleBeam, true, kNaN);
    CHECK(s.raise == doctest::Approx(0.2f).epsilon(1e-4));
    s = step_sword(s, kSwordFoot.endS, released(), kIdleBeam, true, kNaN);
    CHECK(s.phase == SwordPhase::Lowered);
}

TEST_CASE("allowed = false never leaves Lowered, and lowers a raised sword") {
    SwordInput in{true, false, false};
    SwordState s;
    for (int i = 0; i < 90; ++i) s = step_sword(s, 1.0f / 30.0f, in, kIdleBeam, true, kNaN);
    CHECK(s.phase == SwordPhase::Lowered);
    CHECK(s.raise == 0.0f);

    s = step_sword({}, 0.0f, held_foot(), kIdleBeam, true, kNaN);
    s = step_sword(s, 5.0f, held_foot(), kIdleBeam, true, kNaN);
    REQUIRE(s.phase == SwordPhase::Raised);
    s = step_sword(s, 0.0f, in, kIdleBeam, true, kNaN);
    CHECK(s.phase == SwordPhase::Lowering);
}

TEST_CASE("sparks: 2.0 s locked with a hit gives 4, first at once, then every 0.5 s") {
    SwordState s = step_sword({}, 0.0f, held_foot(), kIdleBeam, true, kNaN);
    s = step_sword(s, 5.0f, held_foot(), kIdleBeam, true, kNaN);  // Raised
    REQUIRE(s.phase == SwordPhase::Raised);
    const SwordBeam locked = beam_at_dot(1.0f);
    REQUIRE(locked.locked);
    s = step_sword(s, 1.0f / 60.0f, held_foot(), locked, true, 42.0f);
    CHECK(s.sparks == 1);  // at once
    for (int i = 1; i < 120; ++i) s = step_sword(s, 1.0f / 60.0f, held_foot(), locked, true, 42.0f);
    CHECK(s.sparks == 4);
}

TEST_CASE("sparks: none unlocked or without a hit") {
    SwordState s = step_sword({}, 0.0f, held_foot(), kIdleBeam, true, kNaN);
    s = step_sword(s, 5.0f, held_foot(), kIdleBeam, true, kNaN);
    const SwordBeam locked = beam_at_dot(1.0f);
    const SwordBeam free_beam = beam_at_dot(0.8f);
    for (int i = 0; i < 120; ++i) {
        s = step_sword(s, 1.0f / 60.0f, held_foot(), locked, true, kNaN);       // no hit
        s = step_sword(s, 1.0f / 60.0f, held_foot(), free_beam, true, 42.0f);   // unlocked
    }
    CHECK(s.sparks == 0);
}

TEST_CASE("sword_locks_movement: on foot while up, never mounted, never Lowered") {
    SwordState s = step_sword({}, 0.0f, held_foot(), kIdleBeam, true, kNaN);
    CHECK(sword_locks_movement(s));  // Raising
    s = step_sword(s, 5.0f, held_foot(), kIdleBeam, true, kNaN);
    CHECK(sword_locks_movement(s));  // Raised
    s = step_sword(s, 0.0f, released(), kIdleBeam, true, kNaN);
    CHECK(sword_locks_movement(s));  // Lowering
    s = step_sword(s, 5.0f, released(), kIdleBeam, true, kNaN);
    CHECK_FALSE(sword_locks_movement(s));  // Lowered

    SwordInput m{true, true, true};
    SwordState ms = step_sword({}, 0.0f, m, kIdleBeam, true, kNaN);
    CHECK_FALSE(sword_locks_movement(ms));
    ms = step_sword(ms, 5.0f, m, kIdleBeam, true, kNaN);
    CHECK(ms.phase == SwordPhase::Raised);
    CHECK_FALSE(sword_locks_movement(ms));
}

TEST_CASE("intensity is raise * light") {
    SwordState s;
    s.raise = 0.5f;
    s.light = 0.5f;
    CHECK(sword_intensity(s) == doctest::Approx(0.25f));
}

TEST_CASE("reset from every phase") {
    SwordState s = step_sword({}, 0.0f, held_foot(), kIdleBeam, true, kNaN);
    for (int phase = 0; phase < 4; ++phase) {
        if (phase == 2) s = step_sword(s, 5.0f, held_foot(), kIdleBeam, true, kNaN);
        if (phase == 3) s = step_sword(s, 0.0f, released(), kIdleBeam, true, kNaN);
        s.sparks = 3;
        s.width = 1.0f;
        const SwordState r = reset_sword();
        CHECK(r.phase == SwordPhase::Lowered);
        CHECK(r.clip == nullptr);
        CHECK(r.raise == 0.0f);
        CHECK(r.light == 0.0f);
        CHECK(r.width == 0.0f);
        CHECK(r.sparks == 0);
        CHECK_FALSE(sword_locks_movement(r));
    }
    CHECK(s.phase == SwordPhase::Lowering);
}

TEST_CASE("a non-finite or negative dt advances nothing") {
    SwordState s = step_sword({}, 0.0f, held_foot(), kIdleBeam, true, kNaN);
    s = step_sword(s, 0.2f, held_foot(), kIdleBeam, true, kNaN);
    for (float bad : {-0.1f, kNaN, std::numeric_limits<float>::infinity()}) {
        const SwordState n = step_sword(s, bad, held_foot(), beam_at_dot(1.0f), false, 3.0f);
        CHECK(n.phase == s.phase);
        CHECK(n.clipTime == s.clipTime);
        CHECK(n.raise == s.raise);
        CHECK(n.light == s.light);
        CHECK(n.width == s.width);
        CHECK(n.sparks == s.sparks);
    }
}

TEST_CASE("phase names") {
    CHECK(std::strcmp(sword_phase_name(SwordPhase::Lowered), "Lowered") == 0);
    CHECK(std::strcmp(sword_phase_name(SwordPhase::Raising), "Raising") == 0);
    CHECK(std::strcmp(sword_phase_name(SwordPhase::Raised), "Raised") == 0);
    CHECK(std::strcmp(sword_phase_name(SwordPhase::Lowering), "Lowering") == 0);
}

TEST_CASE("the sun matches mesh.metal's key light") {
    const float n = std::sqrt(0.16f + 0.64f + 0.36f);
    CHECK(kSunDir.x == doctest::Approx(0.4f / n));
    CHECK(kSunDir.y == doctest::Approx(0.8f / n));
    CHECK(kSunDir.z == doctest::Approx(0.6f / n));
}
