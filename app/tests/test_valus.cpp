// Valus's attack decision (S4.2 Task 7, ruling R8). Every case here is pure:
// no scene, no window and no clock, just the selection matrix, the pinned
// impact windows, the cooldowns, the interrupt rule and the yaw slew.
#include <doctest.h>

#include <cmath>
#include <limits>

#include "math3d.h"
#include "valus.h"

namespace {

// A grounded player 10 m in front of an entity at the origin facing +Z: the
// club's baseline. The feet sit far to the sides so no stomp competes.
ValusAttackInput club_baseline() {
    ValusAttackInput input;
    input.playerGrounded = true;
    input.playerAttached = false;
    input.defeated = false;
    input.cooldownSeconds = 0.0f;
    input.entityPos = Vec3{0.0f, 0.0f, 0.0f};
    input.entityYawDeg = 0.0f;  // +Z
    input.playerPos = Vec3{0.0f, 0.0f, 10.0f};
    input.footL = Vec3{-20.0f, 0.0f, 0.0f};
    input.footR = Vec3{20.0f, 0.0f, 0.0f};
    input.stompLeftNext = true;
    return input;
}

// A grounded player next to the left foot, well inside stomp range.
ValusAttackInput stomp_baseline() {
    ValusAttackInput input = club_baseline();
    input.footL = Vec3{-2.0f, 0.0f, 2.0f};
    input.footR = Vec3{2.0f, 0.0f, 2.0f};
    input.playerPos = Vec3{-2.0f, 0.0f, 3.0f};
    return input;
}

}  // namespace

TEST_CASE("the club is the baseline attack for a grounded target in front") {
    CHECK(select_valus_attack(club_baseline()) == ValusAttack::Club);
}

TEST_CASE("no attack without a live, grounded, unattached target") {
    SUBCASE("a non-grounded player is not a target") {
        ValusAttackInput input = club_baseline();
        input.playerGrounded = false;
        CHECK(select_valus_attack(input) == ValusAttack::None);
    }
    SUBCASE("an attached player is not a target") {
        ValusAttackInput input = club_baseline();
        input.playerAttached = true;
        CHECK(select_valus_attack(input) == ValusAttack::None);
    }
    SUBCASE("a defeated Valus never attacks") {
        ValusAttackInput input = club_baseline();
        input.defeated = true;
        CHECK(select_valus_attack(input) == ValusAttack::None);
    }
    SUBCASE("an active cooldown blocks both attacks") {
        ValusAttackInput input = club_baseline();
        input.cooldownSeconds = 0.5f;
        CHECK(select_valus_attack(input) == ValusAttack::None);
    }
    SUBCASE("a zero or negative cooldown is ready") {
        ValusAttackInput input = club_baseline();
        input.cooldownSeconds = 0.0f;
        CHECK(select_valus_attack(input) == ValusAttack::Club);
        input.cooldownSeconds = -1.0f;
        CHECK(select_valus_attack(input) == ValusAttack::Club);
    }
    SUBCASE("a non-finite cooldown never reads as ready") {
        ValusAttackInput input = club_baseline();
        input.cooldownSeconds = std::numeric_limits<float>::quiet_NaN();
        CHECK(select_valus_attack(input) == ValusAttack::None);
    }
}

TEST_CASE("a foot in range picks the stomp side") {
    ValusAttackInput input = stomp_baseline();
    CHECK(select_valus_attack(input) == ValusAttack::StompL);

    input.footL = Vec3{-2.0f, 0.0f, -3.0f};  // 7.2 m away: out of range
    input.playerPos = Vec3{2.0f, 0.0f, 3.0f};
    CHECK(select_valus_attack(input) == ValusAttack::StompR);

    // 4.5 m exactly is inside (the rule is "within", inclusive at the edge).
    input.footL = Vec3{-2.0f, 0.0f, 2.0f};
    input.footR = Vec3{2.0f, 0.0f, 2.0f};
    input.playerPos = Vec3{-2.0f, 0.0f, 6.5f};
    CHECK(select_valus_attack(input) == ValusAttack::StompL);

    // 4.6 m is outside: the same player is now a club target (6.9 m, in front).
    input.playerPos = Vec3{-2.0f, 0.0f, 6.6f};
    CHECK(select_valus_attack(input) == ValusAttack::Club);
}

TEST_CASE("the stomping foot alternates when both feet are in range") {
    ValusAttackInput input = stomp_baseline();
    input.footL = Vec3{-1.0f, 0.0f, 3.0f};
    input.footR = Vec3{1.0f, 0.0f, 3.0f};
    input.playerPos = Vec3{0.0f, 0.0f, 3.5f};  // ~1.12 m from either foot

    input.stompLeftNext = true;
    CHECK(select_valus_attack(input) == ValusAttack::StompL);
    input.stompLeftNext = false;
    CHECK(select_valus_attack(input) == ValusAttack::StompR);

    // A single reachable foot overrides the alternation seed.
    input.footL = Vec3{-5.0f, 0.0f, 3.0f};     // 6 m from the player: out of range
    input.playerPos = Vec3{1.0f, 0.0f, 3.5f};  // only the right foot is near
    input.stompLeftNext = true;
    CHECK(select_valus_attack(input) == ValusAttack::StompR);
}

TEST_CASE("the club needs both range and facing") {
    ValusAttackInput input = club_baseline();

    input.playerPos = Vec3{0.0f, 0.0f, 14.0f};
    CHECK(select_valus_attack(input) == ValusAttack::Club);  // exactly 14 m

    input.playerPos = Vec3{0.0f, 0.0f, 14.1f};
    CHECK(select_valus_attack(input) == ValusAttack::None);

    // Yaw 90 faces +X: the player behind the facing axis is out of the club.
    input = club_baseline();
    input.entityYawDeg = 90.0f;
    input.playerPos = Vec3{0.0f, 0.0f, 10.0f};
    CHECK(select_valus_attack(input) == ValusAttack::None);
    input.playerPos = Vec3{10.0f, 0.0f, 0.0f};
    CHECK(select_valus_attack(input) == ValusAttack::Club);

    // The facing threshold is dot > 0.2, not >=: 0.19 is out, 0.21 is in.
    input = club_baseline();
    input.playerPos = Vec3{13.746f, 0.0f, 2.66f};  // 14 m, dot = 0.19
    CHECK(select_valus_attack(input) == ValusAttack::None);
    input.playerPos = Vec3{13.687f, 0.0f, 2.94f};  // 14 m, dot = 0.21
    CHECK(select_valus_attack(input) == ValusAttack::Club);
}

TEST_CASE("a stomp wins over the club when the player is at a foot") {
    ValusAttackInput input = stomp_baseline();
    input.playerPos = Vec3{-2.0f, 0.0f, 3.0f};  // 1 m from the left foot, in front
    CHECK(select_valus_attack(input) == ValusAttack::StompL);
}

TEST_CASE("impact windows are the pinned ranges of the clip") {
    SUBCASE("a stomp fires in [0.55, 0.70]") {
        CHECK_FALSE(attack_impact_due(ValusAttack::StompL, 0.54f, false));
        CHECK(attack_impact_due(ValusAttack::StompL, 0.55f, false));
        CHECK(attack_impact_due(ValusAttack::StompL, 0.62f, false));
        CHECK(attack_impact_due(ValusAttack::StompL, 0.70f, false));
        CHECK_FALSE(attack_impact_due(ValusAttack::StompL, 0.71f, false));
    }
    SUBCASE("a club fires in [0.45, 0.65]") {
        CHECK_FALSE(attack_impact_due(ValusAttack::Club, 0.44f, false));
        CHECK(attack_impact_due(ValusAttack::Club, 0.45f, false));
        CHECK(attack_impact_due(ValusAttack::Club, 0.55f, false));
        CHECK(attack_impact_due(ValusAttack::Club, 0.65f, false));
        CHECK_FALSE(attack_impact_due(ValusAttack::Club, 0.66f, false));
    }
    SUBCASE("both stomp sides share one window") {
        CHECK(attack_impact_due(ValusAttack::StompR, 0.60f, false));
        CHECK_FALSE(attack_impact_due(ValusAttack::StompR, 0.40f, false));
    }
}

TEST_CASE("an impact fires once and never for None or a bad time") {
    CHECK_FALSE(attack_impact_due(ValusAttack::StompL, 0.60f, true));
    CHECK_FALSE(attack_impact_due(ValusAttack::Club, 0.55f, true));
    CHECK_FALSE(attack_impact_due(ValusAttack::None, 0.60f, false));
    CHECK_FALSE(attack_impact_due(ValusAttack::StompL, -0.01f, false));
    CHECK_FALSE(attack_impact_due(
        ValusAttack::StompL, std::numeric_limits<float>::quiet_NaN(), false));
    CHECK_FALSE(attack_impact_due(
        ValusAttack::Club, std::numeric_limits<float>::infinity(), false));
}

TEST_CASE("cooldowns are 4 s for a stomp and 5 s for the club") {
    CHECK(valus_attack_cooldown_seconds(ValusAttack::StompL) == doctest::Approx(4.0f));
    CHECK(valus_attack_cooldown_seconds(ValusAttack::StompR) == doctest::Approx(4.0f));
    CHECK(valus_attack_cooldown_seconds(ValusAttack::Club) == doctest::Approx(5.0f));
    CHECK(valus_attack_cooldown_seconds(ValusAttack::None) == doctest::Approx(0.0f));
}

TEST_CASE("only a landed strike cancels an in-progress attack") {
    CHECK(valus_attack_cancelled(/*landedStrike=*/true, ValusAttack::StompL));
    CHECK(valus_attack_cancelled(/*landedStrike=*/true, ValusAttack::StompR));
    CHECK(valus_attack_cancelled(/*landedStrike=*/true, ValusAttack::Club));
    // A whiff leaves the telegraph and its pending impact alone.
    CHECK_FALSE(valus_attack_cancelled(/*landedStrike=*/false, ValusAttack::Club));
    CHECK_FALSE(valus_attack_cancelled(/*landedStrike=*/false, ValusAttack::StompL));
    // Nothing to cancel.
    CHECK_FALSE(valus_attack_cancelled(/*landedStrike=*/true, ValusAttack::None));
}

TEST_CASE("the entity yaw comes from the manifest quaternion") {
    CHECK(entity_yaw_deg(Quat{0.0f, 0.0f, 0.0f, 1.0f}) == doctest::Approx(0.0f));
    const float half = 0.5f * 90.0f * kDegToRad;
    CHECK(entity_yaw_deg(Quat{0.0f, std::sin(half), 0.0f, std::cos(half)}) ==
          doctest::Approx(90.0f));

    // The real encounter placement: (0, -0.909104, 0, 0.416570) is -130.8 deg.
    const Quat manifest{0.0f, -0.909104f, 0.0f, 0.416570f};
    CHECK(entity_yaw_deg(manifest) == doctest::Approx(-130.8f).epsilon(0.01));
    // -q is the same rotation: the yaw must not take the 360 degree detour.
    CHECK(entity_yaw_deg(Quat{-0.0f, 0.909104f, 0.0f, -0.416570f}) ==
          doctest::Approx(-130.8f).epsilon(0.01));

    CHECK(entity_yaw_deg(Quat{0.0f, 0.0f, 0.0f, 0.0f}) == doctest::Approx(0.0f));
    CHECK(entity_yaw_deg(Quat{0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f}) ==
          doctest::Approx(0.0f));
}

TEST_CASE("the yaw slew turns at 30 deg/s on the shortest path") {
    const Vec3 entity{0.0f, 0.0f, 0.0f};
    const Vec3 player{10.0f, 0.0f, 0.0f};  // target yaw +90

    CHECK(valus_yaw_slew(0.0f, entity, player, 30.0f, 1.0f) == doctest::Approx(30.0f));
    CHECK(valus_yaw_slew(0.0f, entity, player, 30.0f, 10.0f) == doctest::Approx(90.0f));
    CHECK(valus_yaw_slew(80.0f, entity, player, 30.0f, 10.0f) == doctest::Approx(90.0f));

    // 170 -> -90 is +100 the short way; one second is +30 -> 200 wraps to -160.
    const Vec3 west{-10.0f, 0.0f, 0.0f};
    CHECK(valus_yaw_slew(170.0f, entity, west, 30.0f, 1.0f) == doctest::Approx(-160.0f));

    // No time, no direction, no rate: hold.
    CHECK(valus_yaw_slew(170.0f, entity, west, 30.0f, 0.0f) == doctest::Approx(170.0f));
    CHECK(valus_yaw_slew(170.0f, entity, entity, 30.0f, 1.0f) == doctest::Approx(170.0f));
    CHECK(valus_yaw_slew(170.0f, entity, west, 0.0f, 1.0f) == doctest::Approx(170.0f));
    CHECK(valus_yaw_slew(170.0f, entity, west, 30.0f,
                         std::numeric_limits<float>::quiet_NaN()) ==
          doctest::Approx(170.0f));
}
