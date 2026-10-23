// The door fade machine (S8 Task 6, design D14). Pure: the phase walk, the fade
// ramps, the input lock, the single Load request, the box crossing and the reset.
#include <doctest.h>

#include <cmath>
#include <cstring>
#include <limits>

#include "door.h"

namespace {

DoorState fading_out() { return step_door({}, 0.0f, true, 3, false); }

// First step in Load (fade 1, locked).
DoorState loading() { return step_door(fading_out(), kDoorFadeOut, false, 0, false); }

DoorState fading_in() { return step_door(loading(), 0.5f, false, 0, true); }

}  // namespace

TEST_CASE("Idle stays Idle without entered, whatever else is passed") {
    DoorState s = step_door({}, 5.0f, false, 7, true);
    CHECK(s.phase == DoorPhase::Idle);
    CHECK_FALSE(s.inputLocked);
    CHECK(s.fade == 0.0f);
    CHECK(s.t == 0.0f);
    CHECK(s.target == -1);
}

TEST_CASE("entered starts FadeOut, locked, clear, with the target, not spending dt") {
    DoorState s = step_door({}, 0.5f, true, 3, false);
    CHECK(s.phase == DoorPhase::FadeOut);
    CHECK(s.inputLocked);
    CHECK(s.fade == 0.0f);
    CHECK(s.t == 0.0f);
    CHECK(s.target == 3);
    // entered and target are not re-read afterwards.
    s = step_door(s, 0.25f, true, 9, false);
    CHECK(s.target == 3);
    CHECK(s.t == doctest::Approx(0.25f));
}

TEST_CASE("FadeOut ramps the fade 0 to 1 over 1.0 s, locked") {
    DoorState s = fading_out();
    s = step_door(s, 0.25f, false, 0, false);
    CHECK(s.phase == DoorPhase::FadeOut);
    CHECK(s.fade == doctest::Approx(0.25f));
    CHECK(s.inputLocked);
    s = step_door(s, 0.5f, false, 0, false);
    CHECK(s.phase == DoorPhase::FadeOut);
    CHECK(s.fade == doctest::Approx(0.75f));
    CHECK(s.inputLocked);
}

TEST_CASE("FadeOut ends in Load at fade 1, requesting the load exactly once") {
    DoorState before = step_door(fading_out(), 0.99f, false, 0, false);
    REQUIRE(before.phase == DoorPhase::FadeOut);
    CHECK_FALSE(door_wants_load(fading_out(), before));
    DoorState after = step_door(before, 0.01f, false, 0, false);
    CHECK(after.phase == DoorPhase::Load);
    CHECK(after.fade == 1.0f);
    CHECK(after.inputLocked);
    CHECK(after.target == 3);
    CHECK(door_wants_load(before, after));
    DoorState next = step_door(after, 100.0f, false, 0, false);
    CHECK(next.phase == DoorPhase::Load);
    CHECK(next.fade == 1.0f);
    CHECK_FALSE(door_wants_load(after, next));
}

TEST_CASE("one long frame crosses FadeOut but stops at Load, which waits for loadDone") {
    DoorState s = step_door(fading_out(), 1000.0f, false, 0, true);
    CHECK(s.phase == DoorPhase::Load);
    for (int i = 0; i < 10; ++i) s = step_door(s, 10.0f, false, 0, false);
    CHECK(s.phase == DoorPhase::Load);
    CHECK(s.fade == 1.0f);
    s = step_door(s, 10.0f, false, 0, true);
    CHECK(s.phase == DoorPhase::FadeIn);
}

TEST_CASE("loadDone starts FadeIn at fade 1 without spending dt; it ramps 1 to 0 over 1.5 s") {
    DoorState s = step_door(loading(), 0.5f, false, 0, true);
    CHECK(s.phase == DoorPhase::FadeIn);
    CHECK(s.fade == 1.0f);
    CHECK(s.t == 0.0f);
    CHECK(s.inputLocked);
    s = step_door(s, 0.75f, false, 0, true);
    CHECK(s.phase == DoorPhase::FadeIn);
    CHECK(s.fade == doctest::Approx(0.5f));
    CHECK(s.inputLocked);
    s = step_door(s, 0.74f, false, 0, true);
    CHECK(s.phase == DoorPhase::FadeIn);
    CHECK(s.fade > 0.0f);
}

TEST_CASE("FadeIn ends back in Idle, unlocked, clear, without a target") {
    DoorState s = step_door(fading_in(), kDoorFadeIn, false, 0, true);
    CHECK(s.phase == DoorPhase::Idle);
    CHECK_FALSE(s.inputLocked);
    CHECK(s.fade == 0.0f);
    CHECK(s.t == 0.0f);
    CHECK(s.target == -1);
    // and a second door can start.
    s = step_door(s, 0.0f, true, 5, false);
    CHECK(s.phase == DoorPhase::FadeOut);
    CHECK(s.target == 5);
}

TEST_CASE("the door_wants_load is true only on the FadeOut to Load step across a whole run") {
    DoorState s = step_door({}, 0.0f, true, 1, false);
    int loads = 0;
    for (int i = 0; i < 1000; ++i) {
        DoorState n = step_door(s, 0.01f, false, 0, i >= 300);
        if (door_wants_load(s, n)) ++loads;
        s = n;
    }
    CHECK(loads == 1);
    CHECK(s.phase == DoorPhase::Idle);
}

TEST_CASE("a negative or non-finite dt advances nothing") {
    const float bad[] = {-1.0f, std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity()};
    for (DoorState base : {step_door(fading_out(), 0.25f, false, 0, false),
                           step_door(fading_in(), 0.25f, false, 0, true)}) {
        for (float dt : bad) {
            DoorState s = step_door(base, dt, false, 0, true);
            if (base.phase == DoorPhase::FadeOut || base.phase == DoorPhase::FadeIn) {
                CHECK(s.phase == base.phase);
                CHECK(s.t == base.t);
                CHECK(s.fade == base.fade);
            }
            CHECK(std::isfinite(s.fade));
        }
    }
}

TEST_CASE("reset from every phase equals the initial state") {
    const DoorState fresh{};
    const DoorState phases[] = {{}, fading_out(), loading(), fading_in()};
    bool seen[4] = {};
    for (const DoorState& p : phases) {
        seen[static_cast<int>(p.phase)] = true;
        (void)p;
        DoorState r = reset_door();
        CHECK(r.phase == fresh.phase);
        CHECK(r.t == fresh.t);
        CHECK(r.fade == fresh.fade);
        CHECK(r.inputLocked == fresh.inputLocked);
        CHECK(r.target == -1);
    }
    CHECK((seen[0] && seen[1] && seen[2] && seen[3]));
    // A reset machine is a working one.
    CHECK(step_door(reset_door(), 0.0f, true, 2, false).phase == DoorPhase::FadeOut);
}

TEST_CASE("box_entered: outside to inside only") {
    const Vec3 mn{0, 0, 0}, mx{2, 2, 2};
    const Vec3 out{5, 1, 1}, in{1, 1, 1}, in2{1.5f, 1, 1};
    CHECK(box_entered(out, in, mn, mx));
    CHECK_FALSE(box_entered(in, in2, mn, mx));  // starting inside (a spawn) never fires
    CHECK_FALSE(box_entered(in, in, mn, mx));
    CHECK_FALSE(box_entered(in, out, mn, mx));  // leaving
    CHECK_FALSE(box_entered(out, Vec3{6, 1, 1}, mn, mx));
    // each axis gates on its own
    CHECK_FALSE(box_entered(out, Vec3{1, 3, 1}, mn, mx));
    CHECK_FALSE(box_entered(out, Vec3{1, 1, -0.1f}, mn, mx));
    CHECK(box_entered(Vec3{1, 5, 1}, in, mn, mx));
    // inclusive bounds
    CHECK(box_entered(out, Vec3{2, 2, 2}, mn, mx));
    CHECK_FALSE(box_entered(Vec3{2, 2, 2}, in, mn, mx));
}

TEST_CASE("door_phase_name spells the enum") {
    CHECK(std::strcmp(door_phase_name(DoorPhase::Idle), "Idle") == 0);
    CHECK(std::strcmp(door_phase_name(DoorPhase::FadeOut), "FadeOut") == 0);
    CHECK(std::strcmp(door_phase_name(DoorPhase::Load), "Load") == 0);
    CHECK(std::strcmp(door_phase_name(DoorPhase::FadeIn), "FadeIn") == 0);
}
