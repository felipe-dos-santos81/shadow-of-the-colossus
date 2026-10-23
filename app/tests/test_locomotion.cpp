// Entity locomotion (S11 Task 8, design D9): an entity walks along its yaw at a
// clip-measured speed, turns at a rate, follows the ground and refuses the moves
// the ground refuses. Quadratus's measured numbers are the test data only; the
// module carries none of them.
#include <doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>

#include "heightmap.h"
#include "locomotion.h"
#include "math3d.h"

namespace {

constexpr float kStep = 1.0f / 60.0f;

// The Quadratus recon (s11-quadratus-recon/recon.md §5): `walk` 2.133 s with a
// +20 m root, `turn_l/r` 45° over 2.333 s, `back` 2.0 s with -10 m.
LocomotionTuning quadratus_tuning() {
    LocomotionTuning tuning;
    tuning.walkSpeed = 20.0f / 2.1333333f;  // 9.375 m/s
    tuning.backSpeed = 10.0f / 2.0f;        // 5 m/s
    tuning.turnRateDeg = 45.0f / 2.3333333f;
    return tuning;
}

const GroundQuery kFlat = [](Vec3, Vec3, float& floorY) {
    floorY = 0.0f;
    return true;
};

// Steps `seconds` of whole kStep frames.
LocomotionResult run(EntityMotion& motion, float drive, float turn, const LocomotionTuning& tuning,
                     const GroundQuery& ground, float seconds) {
    LocomotionResult last = LocomotionResult::Still;
    const int frames = static_cast<int>(std::lround(seconds / kStep));
    for (int i = 0; i < frames; ++i) {
        last = locomotion_step(motion, drive, turn, tuning, ground, kStep);
    }
    return last;
}

// A flat unlayered grid at `height` over [-half, half]^2, one node per metre.
Heightmap flat_heightmap(float half, float height) {
    Heightmap heightmap;
    const int nodes = static_cast<int>(half) * 2 + 1;
    heightmap.cellSize = 1.0f;
    heightmap.width = nodes;
    heightmap.height = nodes;
    heightmap.heights.assign(static_cast<std::size_t>(nodes) * static_cast<std::size_t>(nodes),
                             height);
    heightmap.boundsMin = Vec3{-half, 0.0f, -half};
    heightmap.boundsMax = Vec3{half, 0.0f, half};
    return heightmap;
}

}  // namespace

TEST_CASE("an entity walks along its yaw at the clip-measured speed") {
    EntityMotion motion;
    const LocomotionTuning tuning = quadratus_tuning();

    CHECK(run(motion, 1.0f, 0.0f, tuning, kFlat, 1.0f) == LocomotionResult::Moved);
    CHECK(motion.position.z == doctest::Approx(9.375f).epsilon(1e-4));
    CHECK(motion.position.x == doctest::Approx(0.0f));
    CHECK(motion.yawDeg == 0.0f);

    // One `walk` cycle covers its root distance.
    EntityMotion cycle;
    run(cycle, 1.0f, 0.0f, tuning, kFlat, 2.1333333f);
    CHECK(cycle.position.z == doctest::Approx(20.0f).epsilon(1e-2));

    // Yaw 90 faces +X (player.h's convention).
    EntityMotion east;
    east.yawDeg = 90.0f;
    run(east, 1.0f, 0.0f, tuning, kFlat, 1.0f);
    CHECK(east.position.x == doctest::Approx(9.375f).epsilon(1e-4));
    CHECK(east.position.z == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("an entity backs up at the back speed and drive is clamped") {
    const LocomotionTuning tuning = quadratus_tuning();
    EntityMotion back;
    run(back, -1.0f, 0.0f, tuning, kFlat, 2.0f);
    CHECK(back.position.z == doctest::Approx(-10.0f).epsilon(1e-4));

    EntityMotion half;
    run(half, 0.5f, 0.0f, tuning, kFlat, 1.0f);
    CHECK(half.position.z == doctest::Approx(9.375f * 0.5f).epsilon(1e-4));

    EntityMotion over;
    run(over, 7.0f, 0.0f, tuning, kFlat, 1.0f);
    CHECK(over.position.z == doctest::Approx(9.375f).epsilon(1e-4));

    EntityMotion idle;
    CHECK(run(idle, 0.0f, 0.0f, tuning, kFlat, 1.0f) == LocomotionResult::Still);
    CHECK(idle.position.z == 0.0f);
}

TEST_CASE("an entity turns 45 degrees per turn clip; positive turns right") {
    const LocomotionTuning tuning = quadratus_tuning();

    EntityMotion right;
    CHECK(run(right, 0.0f, 1.0f, tuning, kFlat, 2.3333333f) == LocomotionResult::Still);
    CHECK(right.yawDeg == doctest::Approx(-45.0f).epsilon(1e-2));
    CHECK(right.position.x == 0.0f);
    CHECK(right.position.z == 0.0f);

    EntityMotion left;
    run(left, 0.0f, -1.0f, tuning, kFlat, 2.3333333f);
    CHECK(left.yawDeg == doctest::Approx(45.0f).epsilon(1e-2));

    // Turning while walking moves along the turned heading: right of +Z is -X.
    EntityMotion arc;
    run(arc, 1.0f, 1.0f, tuning, kFlat, 1.0f);
    CHECK(arc.position.x < -0.1f);
    CHECK(arc.position.z > 9.0f);
}

TEST_CASE("an entity follows the ground the caller supplies") {
    const LocomotionTuning tuning = quadratus_tuning();
    const GroundQuery slope = [](Vec3, Vec3 to, float& floorY) {
        floorY = 0.5f * to.z;
        return true;
    };
    EntityMotion motion;
    motion.position.y = 99.0f;  // a stale height snaps to the floor on the first move
    run(motion, 1.0f, 0.0f, tuning, slope, 1.0f);
    CHECK(motion.position.y == doctest::Approx(0.5f * motion.position.z));

    // The heightmap adapter: the floor under the move's target.
    const Heightmap raised = flat_heightmap(20.0f, 3.0f);
    EntityMotion onMap;
    run(onMap, 1.0f, 0.0f, tuning, heightmap_ground(raised), 1.0f);
    CHECK(onMap.position.z == doctest::Approx(9.375f).epsilon(1e-4));
    CHECK(onMap.position.y == doctest::Approx(3.0f));
}

TEST_CASE("a refused step leaves the entity where it stood, turning still applies") {
    const LocomotionTuning tuning = quadratus_tuning();
    const GroundQuery fence = [](Vec3, Vec3 to, float& floorY) {
        floorY = 0.0f;
        return to.z <= 1.0f;
    };
    EntityMotion motion;
    run(motion, 1.0f, 0.0f, tuning, fence, 1.0f);
    const Vec3 stopped = motion.position;
    CHECK(stopped.z <= 1.0f);
    CHECK(stopped.z > 0.8f);

    CHECK(locomotion_step(motion, 1.0f, 1.0f, tuning, fence, kStep) == LocomotionResult::Refused);
    CHECK(motion.position.x == stopped.x);
    CHECK(motion.position.y == stopped.y);
    CHECK(motion.position.z == stopped.z);
    CHECK(motion.yawDeg < 0.0f);

    // A non-finite floor is a refusal too.
    const GroundQuery nanFloor = [](Vec3, Vec3, float& floorY) {
        floorY = std::numeric_limits<float>::quiet_NaN();
        return true;
    };
    EntityMotion nanMotion;
    CHECK(locomotion_step(nanMotion, 1.0f, 0.0f, tuning, nanFloor, kStep) ==
          LocomotionResult::Refused);
    CHECK(nanMotion.position.z == 0.0f);

    // No ground at all refuses every move.
    EntityMotion none;
    CHECK(locomotion_step(none, 1.0f, 0.0f, tuning, GroundQuery{}, kStep) ==
          LocomotionResult::Refused);

    // The heightmap refuses leaving its bounds.
    const Heightmap small = flat_heightmap(2.0f, 0.0f);
    EntityMotion edge;
    edge.position = Vec3{0.0f, 0.0f, 1.95f};
    CHECK(locomotion_step(edge, 1.0f, 0.0f, tuning, heightmap_ground(small), kStep) ==
          LocomotionResult::Refused);
    CHECK(edge.position.z == 1.95f);
}

TEST_CASE("a non-finite or negative dt, drive or turn changes nothing") {
    const LocomotionTuning tuning = quadratus_tuning();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (float dt : {nan, inf, -inf, -kStep, 0.0f}) {
        EntityMotion motion;
        motion.yawDeg = 10.0f;
        CHECK(locomotion_step(motion, 1.0f, 1.0f, tuning, kFlat, dt) == LocomotionResult::Still);
        CHECK(motion.position.x == 0.0f);
        CHECK(motion.position.y == 0.0f);
        CHECK(motion.position.z == 0.0f);
        CHECK(motion.yawDeg == 10.0f);
    }
    EntityMotion motion;
    CHECK(locomotion_step(motion, nan, nan, tuning, kFlat, kStep) == LocomotionResult::Still);
    CHECK(motion.position.z == 0.0f);
    CHECK(motion.yawDeg == 0.0f);
}
