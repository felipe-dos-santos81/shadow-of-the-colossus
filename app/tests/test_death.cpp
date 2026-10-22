// Valus's death sequence (S6 Task 2, design §8.1, ruling D3). Every case here
// is pure: no scene, no window and no clock, just the phase walk, the pinned
// timings, the lock/corpse windows, the roots and the reset. The runtime clip
// duration is 93 / 15.08056640625 = 6.166877 s (the last key, not the 94-frame
// count), and the builder's calls add the 0.4-3.0 s tendril burst and the
// 8.0 s cinematic end.
#include <doctest.h>

#include <cmath>
#include <limits>

#include "death.h"

namespace {

// The state a defeat produces: Motion at t = total = 0, locked, two roots.
// Starting from here keeps the boundary cases independent of the first step,
// which deliberately does not advance the clock.
DeathState begun() { return step_death({}, 0.0f, true); }

}  // namespace

TEST_CASE("the motion boundary is the runtime clip's 6.166877 s (R8)") {
    // The retired 6.233 s (94 frames) is 66 ms late; the recon note's
    // 6.166933 is a typo. The exact quotient is 6.16687727 s, so 1e-6 accepts
    // the constant and the quotient while rejecting both wrong values.
    CHECK(kMotionSeconds == doctest::Approx(6.166877f).epsilon(1e-6));
    CHECK(kMotionSeconds == doctest::Approx(93.0 / 15.08056640625).epsilon(1e-6));
    CHECK(kMotionSeconds != doctest::Approx(6.166933f).epsilon(1e-6));
    CHECK(kMotionSeconds != doctest::Approx(6.233f).epsilon(1e-6));
}

TEST_CASE("the death machine walks Idle to Motion to Collapse to Done") {
    DeathState s = step_death({}, 1.0f / 60.0f, false);
    CHECK(s.phase == DeathPhase::Idle);
    CHECK_FALSE(s.inputLocked);
    CHECK_FALSE(s.corpseVisible);
    CHECK_FALSE(death_locks_input(s));
    CHECK_FALSE(death_corpse_visible(s));

    s = step_death({}, 1.0f / 60.0f, true);
    CHECK(s.phase == DeathPhase::Motion);
    CHECK(s.inputLocked);
    CHECK_FALSE(s.corpseVisible);
    CHECK(death_locks_input(s));
    CHECK_FALSE(death_corpse_visible(s));
    CHECK(s.roots == 2);

    s = step_death(s, 6.3f, true);
    CHECK(s.phase == DeathPhase::Collapse);
    CHECK(s.inputLocked);
    CHECK(s.corpseVisible);
    CHECK(death_corpse_visible(s));
    CHECK(death_locks_input(s));

    s = step_death(s, 1.8f, true);
    CHECK(s.phase == DeathPhase::Done);
    CHECK_FALSE(s.inputLocked);
    CHECK(s.corpseVisible);
    CHECK_FALSE(death_locks_input(s));
    CHECK(death_corpse_visible(s));
}

TEST_CASE("the tendril window starts at 0.4 s and ends at 3.0 s") {
    DeathState s = begun();
    CHECK(s.total == 0.0f);
    CHECK_FALSE(death_tendrils_active(s));

    s = step_death(s, 0.39f, true);  // total 0.39: just before the burst
    CHECK(s.total == doctest::Approx(0.39f));
    CHECK(s.phase == DeathPhase::Motion);
    CHECK_FALSE(death_tendrils_active(s));

    s = step_death(s, 0.02f, true);  // total 0.41: the burst is running
    CHECK(s.total == doctest::Approx(0.41f));
    CHECK(s.phase == DeathPhase::Motion);
    CHECK(death_tendrils_active(s));
    CHECK(s.inputLocked);
    CHECK_FALSE(s.corpseVisible);

    s = step_death(s, 2.58f, true);  // total 2.99: still running
    CHECK(s.total == doctest::Approx(2.99f));
    CHECK(s.phase == DeathPhase::Motion);
    CHECK(death_tendrils_active(s));

    s = step_death(s, 0.02f, true);  // total 3.01: the burst has ended
    CHECK(s.total == doctest::Approx(3.01f));
    CHECK(s.phase == DeathPhase::Motion);
    CHECK_FALSE(death_tendrils_active(s));
}

TEST_CASE("the tendril window includes both of its edges") {
    DeathState s = step_death(begun(), kDeathTendrilStart, true);
    CHECK(s.total == kDeathTendrilStart);
    CHECK(death_tendrils_active(s));

    s = step_death(begun(), kDeathTendrilEnd, true);
    CHECK(s.total == kDeathTendrilEnd);
    CHECK(death_tendrils_active(s));

    s = step_death(s, 0.0001f, true);
    CHECK_FALSE(death_tendrils_active(s));
}

TEST_CASE("the lock holds through the motion and collapse and releases in Done") {
    DeathState s = step_death(begun(), kMotionSeconds - 0.001f, true);
    CHECK(s.phase == DeathPhase::Motion);
    CHECK(s.inputLocked);
    CHECK_FALSE(s.corpseVisible);
    CHECK(death_locks_input(s));
    CHECK_FALSE(death_corpse_visible(s));

    s = step_death(begun(), kMotionSeconds, true);
    CHECK(s.phase == DeathPhase::Collapse);
    CHECK(s.inputLocked);
    CHECK(s.corpseVisible);
    CHECK(death_locks_input(s));
    CHECK(death_corpse_visible(s));

    s = step_death(begun(), kDeathEnd - 0.001f, true);
    CHECK(s.phase == DeathPhase::Collapse);
    CHECK(s.inputLocked);
    CHECK(s.corpseVisible);
    CHECK(death_locks_input(s));
    CHECK(death_corpse_visible(s));

    s = step_death(begun(), kDeathEnd, true);
    CHECK(s.phase == DeathPhase::Done);
    CHECK_FALSE(s.inputLocked);
    CHECK(s.corpseVisible);
    CHECK_FALSE(death_locks_input(s));
    CHECK(death_corpse_visible(s));
}

TEST_CASE("the corpse stays and the lock stays off once Done") {
    DeathState s = step_death(begun(), kDeathEnd, true);
    CHECK(s.phase == DeathPhase::Done);
    CHECK(s.total == kDeathEnd);
    CHECK(s.t == 0.0f);

    s = step_death(s, 1.0f, true);
    CHECK(s.phase == DeathPhase::Done);
    CHECK_FALSE(s.inputLocked);
    CHECK(s.corpseVisible);
    CHECK(s.total == kDeathEnd);  // capped: the terminal state does not drift
    CHECK(s.t == 0.0f);

    // Only the reset returns to Idle; a cleared defeat does not rewind.
    s = step_death(s, 1.0f, false);
    CHECK(s.phase == DeathPhase::Done);
    CHECK_FALSE(s.inputLocked);
    CHECK(s.corpseVisible);
    CHECK(s.total == kDeathEnd);
    CHECK(s.t == 0.0f);
}

TEST_CASE("the defeat plants two tendril roots and only the reset clears them") {
    CHECK(reset_death().roots == 0);

    DeathState s = begun();
    CHECK(s.roots == 2);
    s = step_death(s, 4.0f, true);
    CHECK(s.roots == 2);
    s = step_death(s, 5.0f, true);  // total 9: Done
    CHECK(s.phase == DeathPhase::Done);
    CHECK(s.roots == 2);

    CHECK(reset_death().roots == 0);
}

TEST_CASE("reset_death restores Idle and unlocks") {
    DeathState s = step_death(begun(), 9.0f, true);
    CHECK(s.phase == DeathPhase::Done);

    s = reset_death();
    CHECK(s.phase == DeathPhase::Idle);
    CHECK(s.t == 0.0f);
    CHECK(s.total == 0.0f);
    CHECK_FALSE(s.inputLocked);
    CHECK_FALSE(s.corpseVisible);
    CHECK(s.roots == 0);
    CHECK_FALSE(death_locks_input(s));
    CHECK_FALSE(death_corpse_visible(s));
    CHECK_FALSE(death_tendrils_active(s));

    // A reset encounter steps as if it had never been defeated.
    s = step_death(s, 1.0f, false);
    CHECK(s.phase == DeathPhase::Idle);
    CHECK(s.total == 0.0f);
}

TEST_CASE("death_tendrils_active is false in Idle and Done") {
    DeathState idle;
    idle.total = 1.0f;  // inside the window's numbers, but not in the motion
    CHECK_FALSE(death_tendrils_active(idle));

    DeathState done = step_death(begun(), kDeathEnd, true);
    CHECK(done.phase == DeathPhase::Done);
    CHECK_FALSE(death_tendrils_active(done));

    done.total = 1.0f;
    CHECK_FALSE(death_tendrils_active(done));
}

TEST_CASE("total accumulates across steps and t tracks the current phase") {
    DeathState s = step_death({}, 0.5f, true);
    CHECK(s.total == 0.0f);  // the defeat frame starts the clock at zero
    CHECK(s.t == 0.0f);

    s = step_death(s, 0.25f, true);
    CHECK(s.total == doctest::Approx(0.25f));
    CHECK(s.t == doctest::Approx(0.25f));

    s = step_death(s, 0.5f, true);
    CHECK(s.total == doctest::Approx(0.75f));
    CHECK(s.t == doctest::Approx(0.75f));

    s = step_death(s, 1.0f, true);
    CHECK(s.total == doctest::Approx(1.75f));
    CHECK(s.phase == DeathPhase::Motion);
    CHECK(s.t == doctest::Approx(1.75f));

    s = step_death(s, 5.0f, true);  // total 6.75: the collapse has begun
    CHECK(s.total == doctest::Approx(6.75f));
    CHECK(s.phase == DeathPhase::Collapse);
    CHECK(s.t == doctest::Approx(6.75f - kMotionSeconds));

    s = step_death(s, 2.0f, true);  // total 8.75: Done caps the clock
    CHECK(s.total == kDeathEnd);
    CHECK(s.phase == DeathPhase::Done);
    CHECK(s.t == 0.0f);
}

TEST_CASE("a long frame still ends in Done instead of stranding the sequence") {
    DeathState s = step_death(begun(), 100.0f, true);
    CHECK(s.phase == DeathPhase::Done);
    CHECK_FALSE(death_locks_input(s));
    CHECK(death_corpse_visible(s));
    CHECK(s.total == kDeathEnd);
    CHECK(s.t == 0.0f);
    CHECK(s.inputLocked == false);
    CHECK(s.corpseVisible);
}

TEST_CASE("Done stays exactly put over a long idle") {
    DeathState s = step_death(begun(), 9.0f, true);
    CHECK(s.phase == DeathPhase::Done);
    CHECK(s.total == kDeathEnd);
    CHECK(s.t == 0.0f);

    const DeathState done = s;
    for (int frame = 0; frame < 3600; ++frame) {
        s = step_death(s, 1.0f / 60.0f, true);
        REQUIRE(s.phase == DeathPhase::Done);
        REQUIRE(s.total == done.total);
        REQUIRE(s.t == done.t);
        REQUIRE(s.inputLocked == done.inputLocked);
        REQUIRE(s.corpseVisible == done.corpseVisible);
        REQUIRE(s.roots == done.roots);
    }
    CHECK(s.roots == 2);
    CHECK_FALSE(death_locks_input(s));
    CHECK(death_corpse_visible(s));
}

TEST_CASE("a non-finite or negative dt does not move the clock") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    const DeathState s = step_death(begun(), 1.0f, true);
    CHECK(s.total == doctest::Approx(1.0f));

    DeathState held = step_death(s, nan, true);
    CHECK(held.phase == DeathPhase::Motion);
    CHECK(held.total == doctest::Approx(1.0f));

    held = step_death(s, -1.0f, true);
    CHECK(held.phase == DeathPhase::Motion);
    CHECK(held.total == doctest::Approx(1.0f));

    held = step_death(s, infinity, true);
    CHECK(held.phase == DeathPhase::Motion);
    CHECK(held.total == doctest::Approx(1.0f));
}

TEST_CASE("without a defeat the machine never leaves Idle") {
    DeathState s;
    for (int frame = 0; frame < 600; ++frame) {
        s = step_death(s, 1.0f / 60.0f, false);
    }
    CHECK(s.phase == DeathPhase::Idle);
    CHECK(s.total == 0.0f);
    CHECK(s.t == 0.0f);
    CHECK(s.roots == 0);
    CHECK_FALSE(death_locks_input(s));
    CHECK_FALSE(death_corpse_visible(s));
}
