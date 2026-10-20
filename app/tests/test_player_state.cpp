// The player combat state machine (S4.1 Task 3): grab, climb, hold-to-charge
// strike, stamina, falling, catch and stagger. Frames are hand-built joint
// lookups and heightmaps, so this file needs no asset library.
#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "collision.h"
#include "grab.h"
#include "heightmap.h"
#include "math3d.h"
#include "player.h"
#include "player_state.h"
#include "scene.h"

namespace {

constexpr float kStep = 1.0f / 60.0f;

// A JointLookup over a fixed list; names not in the list answer false, exactly
// like posed_joint_world_position for a joint the rig does not have.
JointLookup lookup_of(std::vector<std::pair<std::string, Vec3>> joints) {
    return [joints = std::move(joints)](std::string_view name, Vec3& out) {
        for (const std::pair<std::string, Vec3>& joint : joints) {
            if (std::string_view(joint.first) == name) {
                out = joint.second;
                return true;
            }
        }
        return false;
    };
}

// A flat heightmap at `height` over [-halfExtent, halfExtent]^2, one node per
// metre. The symmetric bounds keep the player away from the S3 edge inset.
Heightmap flat_heightmap(float halfExtent = 10.0f, float height = 0.0f) {
    Heightmap heightmap;
    const int nodes = static_cast<int>(halfExtent) * 2 + 1;
    heightmap.cellSize = 1.0f;
    heightmap.width = nodes;
    heightmap.height = nodes;
    heightmap.heights.assign(static_cast<std::size_t>(nodes) * static_cast<std::size_t>(nodes),
                             height);
    heightmap.boundsMin = Vec3{-halfExtent, 0.0f, -halfExtent};
    heightmap.boundsMax = Vec3{halfExtent, 0.0f, halfExtent};
    return heightmap;
}

// The same grid with no valid node: walking or falling over it never has ground.
Heightmap hole_heightmap(float halfExtent = 10.0f) {
    Heightmap heightmap = flat_heightmap(halfExtent);
    std::fill(heightmap.heights.begin(), heightmap.heights.end(),
              std::numeric_limits<float>::quiet_NaN());
    return heightmap;
}

PlayerFrame frame_of(const Heightmap& ground, JointLookup joints,
                     std::vector<Capsule> capsules = {}) {
    PlayerFrame frame;
    frame.heightmap = &ground;
    frame.jointWorld = std::move(joints);
    frame.entityCapsules = std::move(capsules);
    return frame;
}

// The brief's foot fixture: band 0 spans y 0 -> 2 at x = 0.
PlayerFrame foot_frame(const Heightmap& ground) {
    return frame_of(ground, lookup_of({{"Bone_TUMASAKI_L", Vec3{0.0f, 0.0f, 0.0f}},
                                       {"Bone_TOE_L", Vec3{0.0f, 2.0f, 0.0f}}}));
}

// Head band (weakpoint 0) spans y 8 -> 9 at x = 0. `joints_ok` lets a test
// drop the skeleton mid-grip to force a technical grip break.
PlayerFrame head_frame(const Heightmap& ground, bool& joints_ok) {
    return frame_of(ground, [&joints_ok](std::string_view name, Vec3& out) {
        if (!joints_ok) {
            return false;
        }
        if (name == "Bone_KUBI") {
            out = Vec3{0.0f, 8.0f, 0.0f};
            return true;
        }
        if (name == "Bone_KAO") {
            out = Vec3{0.0f, 9.0f, 0.0f};
            return true;
        }
        return false;
    });
}

// Waist (y 0 -> 1, up = Chest) then Chest (y 1 -> 2), both radius 2.0, so a
// climb can cross a band boundary.
PlayerFrame waist_chest_frame(const Heightmap& ground) {
    return frame_of(ground, lookup_of({{"Bone_KOSI", Vec3{0.0f, 0.0f, 0.0f}},
                                       {"Bone_HARA", Vec3{0.0f, 1.0f, 0.0f}},
                                       {"Bone_MUNE", Vec3{0.0f, 2.0f, 0.0f}}}));
}

}  // namespace

TEST_CASE("holding grab attaches to the nearest band") {
    PlayerState player{};
    player.position = Vec3{0.0f, 0.0f, 0.0f};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    Heightmap ground = flat_heightmap();
    PlayerFrame frame = foot_frame(ground);
    CombatTuning tuning{};

    StepEvents e = player_step(player, combat, input, frame, tuning, 1.0f / 60.0f);

    REQUIRE(combat.mode == PlayerMode::Grabbing);
    REQUIRE(e.grabbed);
    REQUIRE(combat.band == ClimbBandId::LeftFoot);
    CHECK(combat.weakpoint == -1);
    CHECK(combat.bandT == doctest::Approx(0.0f));
    CHECK(std::isfinite(player.position.x));
    CHECK(std::isfinite(player.position.y));
    CHECK(std::isfinite(player.position.z));
}

TEST_CASE("a grab needs a band inside grabRange") {
    Heightmap ground = flat_heightmap();

    // Surface distance is x - 0.35 - 1.7; 2.8 gives 0.75 m (inside 0.9), 3.0
    // gives 0.95 m (outside).
    PlayerState out_of_reach{};
    out_of_reach.position = Vec3{3.0f, 0.0f, 0.0f};
    PlayerCombat combat_out{};
    PlayerInput input{};
    input.grab = true;
    PlayerFrame frame = foot_frame(ground);
    StepEvents missed = player_step(out_of_reach, combat_out, input, frame, CombatTuning{},
                                    kStep);
    CHECK(combat_out.mode == PlayerMode::Grounded);
    CHECK_FALSE(missed.grabbed);

    PlayerState in_reach{};
    in_reach.position = Vec3{2.8f, 0.0f, 0.0f};
    PlayerCombat combat_in{};
    StepEvents caught = player_step(in_reach, combat_in, input, frame, CombatTuning{}, kStep);
    CHECK(combat_in.mode == PlayerMode::Grabbing);
    CHECK(caught.grabbed);
}

TEST_CASE("a zero-stamina player cannot grab") {
    Heightmap ground = flat_heightmap();
    PlayerFrame frame = foot_frame(ground);
    PlayerState player{};
    PlayerCombat combat{};
    combat.stamina = 0.0f;
    combat.regenDelay = 1.0f;  // hold the pool at zero for this step
    PlayerInput input{};
    input.grab = true;

    StepEvents e = player_step(player, combat, input, frame, CombatTuning{}, kStep);

    CHECK(combat.mode == PlayerMode::Grounded);
    CHECK_FALSE(e.grabbed);
    CHECK(combat.stamina == 0.0f);
}

TEST_CASE("grab is held: releasing it drops the player") {
    Heightmap ground = flat_heightmap();
    PlayerFrame frame = foot_frame(ground);
    PlayerState player{};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    player_step(player, combat, input, frame, CombatTuning{}, kStep);
    REQUIRE(combat.mode == PlayerMode::Grabbing);

    input.grab = false;
    StepEvents e = player_step(player, combat, input, frame, CombatTuning{}, kStep);

    CHECK(combat.mode == PlayerMode::Falling);
    CHECK_FALSE(e.gripBroke);  // a deliberate release is not a grip break
    CHECK(combat.catchSecondsLeft == 0.0f);
}

TEST_CASE("stamina zero forces a fall once, not every frame") {
    Heightmap ground = flat_heightmap();
    PlayerFrame frame = foot_frame(ground);
    PlayerState player{};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    player_step(player, combat, input, frame, CombatTuning{}, kStep);
    REQUIRE(combat.mode == PlayerMode::Grabbing);

    combat.stamina = 0.0f;
    StepEvents broke = player_step(player, combat, input, frame, CombatTuning{}, kStep);
    REQUIRE(broke.gripBroke);
    REQUIRE(combat.mode == PlayerMode::Falling);
    CHECK(combat.stamina == 0.0f);
    CHECK(combat.catchSecondsLeft == doctest::Approx(0.6f));
    CHECK(combat.band == ClimbBandId::Count);
    CHECK(combat.weakpoint == -1);

    // The fall starts at the ground here, so the next step lands.
    StepEvents landed = player_step(player, combat, input, frame, CombatTuning{}, kStep);
    REQUIRE(landed.landed);
    REQUIRE(combat.mode == PlayerMode::Staggered);
    CHECK_FALSE(landed.gripBroke);

    int breaks = 0;
    for (int i = 0; i < 120; ++i) {
        StepEvents e = player_step(player, combat, input, frame, CombatTuning{}, kStep);
        breaks += e.gripBroke ? 1 : 0;
    }
    CHECK(breaks == 0);
    CHECK(combat.mode == PlayerMode::Grounded);
}

TEST_CASE("stamina drains at the mode rate") {
    Heightmap ground = flat_heightmap();
    PlayerFrame frame = foot_frame(ground);
    PlayerState player{};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    player_step(player, combat, input, frame, CombatTuning{}, kStep);
    REQUIRE(combat.mode == PlayerMode::Grabbing);

    for (int i = 0; i < 60; ++i) {
        player_step(player, combat, input, frame, CombatTuning{}, kStep);
    }
    CHECK(combat.mode == PlayerMode::Grabbing);
    CHECK(combat.stamina == doctest::Approx(97.5f));  // 2.5 / s for 1 s

    input.forward = true;
    for (int i = 0; i < 60; ++i) {
        player_step(player, combat, input, frame, CombatTuning{}, kStep);
    }
    CHECK(combat.mode == PlayerMode::Climbing);
    CHECK(combat.stamina == doctest::Approx(93.5f));  // 4.0 / s for 1 s

    input.forward = false;
    input.strike = true;
    for (int i = 0; i < 60; ++i) {
        player_step(player, combat, input, frame, CombatTuning{}, kStep);
    }
    CHECK(combat.mode == PlayerMode::Striking);
    CHECK(combat.stamina == doctest::Approx(85.5f));  // 8.0 / s for 1 s
}

TEST_CASE("a full-charge strike deals damage once and shakes the player off") {
    bool joints_ok = true;
    Heightmap high = flat_heightmap(10.0f, 8.0f);
    Heightmap ground = flat_heightmap();
    PlayerState player{};
    player.position = Vec3{0.0f, 8.0f, 0.0f};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    CombatTuning tuning{};

    StepEvents e =
        player_step(player, combat, input, head_frame(high, joints_ok), tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Grabbing);
    REQUIRE(combat.band == ClimbBandId::Head);
    CHECK(combat.weakpoint == 0);

    input.strike = true;
    int held_damage = 0;
    for (int i = 0; i < 60; ++i) {  // hold a second: past the 0.9 s full charge
        e = player_step(player, combat, input, head_frame(high, joints_ok), tuning, kStep);
        held_damage += e.damageDealt;
    }
    CHECK(combat.mode == PlayerMode::Striking);
    CHECK(held_damage == 0);  // a plunge only lands on release
    CHECK(combat.charge == doctest::Approx(tuning.chargeSeconds));

    input.strike = false;
    e = player_step(player, combat, input, head_frame(high, joints_ok), tuning, kStep);
    CHECK(e.damageDealt == 34);
    CHECK(e.gripBroke);
    CHECK(combat.strikeHit);                    // the swing landed
    CHECK(combat.mode == PlayerMode::Falling);  // R7: the hit shakes him off
    CHECK(combat.catchSecondsLeft == doctest::Approx(tuning.catchSeconds));
    CHECK(combat.band == ClimbBandId::Count);
    CHECK(combat.weakpoint == -1);

    // A fresh grab press inside the window catches the nearest band.
    input.grab = false;
    player_step(player, combat, input, head_frame(ground, joints_ok), tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Falling);
    input.grab = true;
    StepEvents caught =
        player_step(player, combat, input, head_frame(ground, joints_ok), tuning, kStep);
    CHECK(caught.grabbed);
    CHECK(caught.damageDealt == 0);
    CHECK(caught.gripBroke == false);
    CHECK(combat.mode == PlayerMode::Grabbing);
    CHECK(combat.band == ClimbBandId::Head);

    // The swing ended on release: no second damage event follows.
    int extra_damage = 0;
    for (int i = 0; i < 60; ++i) {
        e = player_step(player, combat, input, head_frame(ground, joints_ok), tuning, kStep);
        extra_damage += e.damageDealt;
    }
    CHECK(extra_damage == 0);
}

TEST_CASE("an early release is a weak hit") {
    bool joints_ok = true;
    Heightmap high = flat_heightmap(10.0f, 8.0f);
    PlayerFrame frame = head_frame(high, joints_ok);
    PlayerState player{};
    player.position = Vec3{0.0f, 8.0f, 0.0f};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    CombatTuning tuning{};
    player_step(player, combat, input, frame, tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Grabbing);

    input.strike = true;
    player_step(player, combat, input, frame, tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Striking);
    input.strike = false;
    StepEvents e = player_step(player, combat, input, frame, tuning, kStep);

    CHECK(e.damageDealt == 20);
    CHECK(e.gripBroke);
    CHECK(combat.mode == PlayerMode::Falling);
    CHECK(combat.catchSecondsLeft == doctest::Approx(tuning.catchSeconds));
    CHECK(combat.charge < tuning.fullChargeFraction * tuning.chargeSeconds);
}

TEST_CASE("a grip break mid-charge cancels the strike") {
    bool joints_ok = true;
    Heightmap high = flat_heightmap(10.0f, 8.0f);
    PlayerFrame frame = head_frame(high, joints_ok);
    PlayerState player{};
    player.position = Vec3{0.0f, 8.0f, 0.0f};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    CombatTuning tuning{};
    player_step(player, combat, input, frame, tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Grabbing);

    input.strike = true;
    for (int i = 0; i < 5; ++i) {
        player_step(player, combat, input, frame, tuning, kStep);
    }
    REQUIRE(combat.mode == PlayerMode::Striking);
    REQUIRE(combat.charge > 0.0f);

    combat.stamina = 0.01f;  // this step drains more than is left
    StepEvents broke = player_step(player, combat, input, frame, tuning, kStep);
    CHECK(broke.gripBroke);
    CHECK(broke.damageDealt == 0);
    CHECK(combat.mode == PlayerMode::Falling);
    CHECK_FALSE(combat.strikeHit);

    input.strike = false;
    for (int i = 0; i < 30; ++i) {
        StepEvents e = player_step(player, combat, input, frame, tuning, kStep);
        CHECK(e.damageDealt == 0);
    }
}

TEST_CASE("a fresh grab press catches inside the catch window") {
    bool joints_ok = true;
    Heightmap high = flat_heightmap(10.0f, 8.0f);
    Heightmap ground = flat_heightmap();
    PlayerState player{};
    player.position = Vec3{0.0f, 8.0f, 0.0f};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    CombatTuning tuning{};

    StepEvents attached =
        player_step(player, combat, input, head_frame(high, joints_ok), tuning, kStep);
    REQUIRE(attached.grabbed);
    REQUIRE(combat.mode == PlayerMode::Grabbing);

    // Drop the skeleton: the surface point can no longer be recomputed, so the
    // grip breaks with stamina intact.
    joints_ok = false;
    StepEvents broke =
        player_step(player, combat, input, head_frame(high, joints_ok), tuning, kStep);
    REQUIRE(broke.gripBroke);
    REQUIRE(combat.mode == PlayerMode::Falling);
    CHECK(combat.catchSecondsLeft == doctest::Approx(0.6f));

    joints_ok = true;
    input.grab = false;  // release before the catch press: a fresh edge
    player_step(player, combat, input, head_frame(ground, joints_ok), tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Falling);

    input.grab = true;
    StepEvents caught =
        player_step(player, combat, input, head_frame(ground, joints_ok), tuning, kStep);
    CHECK(caught.grabbed);
    CHECK(combat.mode == PlayerMode::Grabbing);
    CHECK(combat.band == ClimbBandId::Head);
    CHECK(combat.catchSecondsLeft == 0.0f);
    CHECK(combat.weakpoint == 0);
}

TEST_CASE("a zero-stamina catch does not re-attach") {
    bool joints_ok = true;
    Heightmap high = flat_heightmap(10.0f, 8.0f);
    Heightmap ground = flat_heightmap();
    PlayerState player{};
    player.position = Vec3{0.0f, 8.0f, 0.0f};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    CombatTuning tuning{};

    player_step(player, combat, input, head_frame(high, joints_ok), tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Grabbing);

    combat.stamina = 0.0f;
    StepEvents broke =
        player_step(player, combat, input, head_frame(high, joints_ok), tuning, kStep);
    REQUIRE(broke.gripBroke);
    REQUIRE(combat.mode == PlayerMode::Falling);
    REQUIRE(combat.catchSecondsLeft == doctest::Approx(tuning.catchSeconds));

    input.grab = false;
    player_step(player, combat, input, head_frame(ground, joints_ok), tuning, kStep);
    const float window = combat.catchSecondsLeft;
    REQUIRE(window > 0.0f);

    input.grab = true;  // fresh press, but the pool is empty
    StepEvents missed =
        player_step(player, combat, input, head_frame(ground, joints_ok), tuning, kStep);
    CHECK_FALSE(missed.grabbed);
    CHECK_FALSE(missed.gripBroke);
    CHECK(combat.mode == PlayerMode::Falling);
    CHECK(combat.stamina == 0.0f);
    CHECK(combat.catchSecondsLeft <= window);  // no new 0.6 s window was opened
    CHECK(combat.band == ClimbBandId::Count);
}

TEST_CASE("a missed catch means the ground") {
    bool joints_ok = true;
    Heightmap high = flat_heightmap(10.0f, 8.0f);
    Heightmap ground = flat_heightmap();
    PlayerState player{};
    player.position = Vec3{0.0f, 8.0f, 0.0f};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    CombatTuning tuning{};
    player_step(player, combat, input, head_frame(high, joints_ok), tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Grabbing);

    joints_ok = false;
    StepEvents broke =
        player_step(player, combat, input, head_frame(high, joints_ok), tuning, kStep);
    REQUIRE(broke.gripBroke);

    joints_ok = true;
    input.grab = false;
    int landings = 0;
    for (int i = 0; i < 200; ++i) {
        StepEvents e = player_step(player, combat, input, head_frame(ground, joints_ok), tuning,
                                   kStep);
        landings += e.landed ? 1 : 0;
    }
    CHECK(landings == 1);
    CHECK(player.position.y == doctest::Approx(0.0f));
    CHECK(combat.mode == PlayerMode::Grounded);
}

TEST_CASE("falling integrates gravity, keeps XZ and lands once") {
    bool joints_ok = true;
    Heightmap high = flat_heightmap(10.0f, 8.0f);
    Heightmap ground = flat_heightmap();
    PlayerState player{};
    player.position = Vec3{0.0f, 8.0f, 0.0f};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    CombatTuning tuning{};
    player_step(player, combat, input, head_frame(high, joints_ok), tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Grabbing);
    const float release_x = player.position.x;
    const float release_z = player.position.z;
    const float release_y = player.position.y;

    input.grab = false;  // a deliberate release falls with no catch window
    player_step(player, combat, input, head_frame(ground, joints_ok), tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Falling);

    int landings = 0;
    int frames = 0;
    while (combat.mode == PlayerMode::Falling && frames < 300) {
        StepEvents e =
            player_step(player, combat, input, head_frame(ground, joints_ok), tuning, kStep);
        landings += e.landed ? 1 : 0;
        ++frames;
    }
    CHECK(landings == 1);
    CHECK(frames < 100);  // sqrt(2 * 8 / 9.8) * 60 ~= 77 frames
    CHECK(player.position.y == doctest::Approx(0.0f));
    CHECK(player.position.x == doctest::Approx(release_x));
    CHECK(player.position.z == doctest::Approx(release_z));
    CHECK(player.position.y < release_y);
    CHECK(combat.mode == PlayerMode::Staggered);
}

TEST_CASE("a non-finite ground keeps the fall finite") {
    bool joints_ok = true;
    Heightmap high = flat_heightmap(10.0f, 8.0f);
    Heightmap hole = hole_heightmap();
    PlayerState player{};
    player.position = Vec3{0.0f, 8.0f, 0.0f};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    CombatTuning tuning{};
    player_step(player, combat, input, head_frame(high, joints_ok), tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Grabbing);

    combat.stamina = 0.0f;
    StepEvents broke =
        player_step(player, combat, input, head_frame(hole, joints_ok), tuning, kStep);
    REQUIRE(broke.gripBroke);
    REQUIRE(combat.mode == PlayerMode::Falling);

    for (int i = 0; i < 120; ++i) {
        StepEvents e =
            player_step(player, combat, input, head_frame(hole, joints_ok), tuning, kStep);
        CHECK_FALSE(e.landed);
        REQUIRE(std::isfinite(player.position.y));
    }
    CHECK(combat.mode == PlayerMode::Falling);
    CHECK(player.position.y < 8.0f);
}

TEST_CASE("a frame-time spike cannot skip a band") {
    Heightmap ground = flat_heightmap();
    PlayerFrame frame = waist_chest_frame(ground);
    PlayerState player{};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    CombatTuning tuning{};
    StepEvents e = player_step(player, combat, input, frame, tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Grabbing);
    REQUIRE(combat.band == ClimbBandId::Waist);

    // 1.2 m/s up a 1 m band is 0.06 of t per clamped step; from 0.99 that
    // crosses exactly one boundary into Chest, never two.
    combat.bandT = 0.99f;
    input.forward = true;
    e = player_step(player, combat, input, frame, tuning, /*dtSeconds=*/0.25f);
    CHECK(combat.mode == PlayerMode::Climbing);
    CHECK(combat.band == ClimbBandId::Chest);
    CHECK(combat.bandT == doctest::Approx(0.0f));

    // From mid-band the step advance is bounded by climbSpeed * 0.05 / length.
    combat.bandT = 0.5f;
    e = player_step(player, combat, input, frame, tuning, /*dtSeconds=*/0.25f);
    CHECK(combat.bandT == doctest::Approx(0.56f).epsilon(1e-4));

    // Walking back down enters Chest's down band at its top end.
    combat.bandT = 0.01f;
    input.forward = false;
    input.back = true;
    e = player_step(player, combat, input, frame, tuning, /*dtSeconds=*/0.25f);
    CHECK(combat.band == ClimbBandId::Waist);
    CHECK(combat.bandT == doctest::Approx(1.0f));
}

TEST_CASE("attached modes write the surface point and skip the ground and capsules") {
    bool joints_ok = true;
    Heightmap high = flat_heightmap(10.0f, 8.0f);
    Heightmap raised = flat_heightmap(10.0f, 50.0f);
    PlayerState player{};
    player.position = Vec3{0.0f, 8.0f, 0.0f};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    CombatTuning tuning{};
    player_step(player, combat, input, head_frame(high, joints_ok), tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Grabbing);

    // The Head band at t = 0 sits at (1.2 + 0.35, 8, 0). A 50 m ground and an
    // overlapping capsule must not move the climber: neither runs while attached.
    PlayerFrame raised_frame = head_frame(raised, joints_ok);
    raised_frame.entityCapsules.push_back(
        Capsule{Vec3{1.55f, 8.0f, 0.0f}, Vec3{1.55f, 9.0f, 0.0f}, 0.5f});
    StepEvents e = player_step(player, combat, input, raised_frame, tuning, kStep);

    CHECK(combat.mode == PlayerMode::Grabbing);
    CHECK(player.position.x == doctest::Approx(1.55f).epsilon(1e-4));
    CHECK(player.position.y == doctest::Approx(8.0f).epsilon(1e-4));
    CHECK(player.position.z == doctest::Approx(0.0f).epsilon(1e-4));
    // The +X radial faces the player back towards the body: -X, yaw -90.
    CHECK(player.yawDeg == doctest::Approx(-90.0f).epsilon(1e-4));
    CHECK_FALSE(e.landed);
}

TEST_CASE("the attach point follows this frame's joints") {
    Vec3 head_a{0.0f, 8.0f, 0.0f};
    Vec3 head_b{0.0f, 9.0f, 0.0f};
    const JointLookup lookup = [&head_a, &head_b](std::string_view name, Vec3& out) {
        if (name == "Bone_KUBI") {
            out = head_a;
            return true;
        }
        if (name == "Bone_KAO") {
            out = head_b;
            return true;
        }
        return false;
    };
    Heightmap high = flat_heightmap(10.0f, 8.0f);
    PlayerFrame frame = frame_of(high, lookup);
    PlayerState player{};
    player.position = Vec3{0.0f, 8.0f, 0.0f};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    CombatTuning tuning{};
    player_step(player, combat, input, frame, tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Grabbing);
    const float attached_x = player.position.x;

    head_a.x += 1.0f;
    head_b.x += 1.0f;
    player_step(player, combat, input, frame, tuning, kStep);
    CHECK(player.position.x == doctest::Approx(attached_x + 1.0f).epsilon(1e-4));
}

TEST_CASE("staggered returns to the ground after staggerSeconds") {
    Heightmap ground = flat_heightmap();
    PlayerFrame frame = foot_frame(ground);
    PlayerState player{};
    PlayerCombat combat{};
    combat.mode = PlayerMode::Staggered;
    combat.modeSeconds = 0.0f;
    PlayerInput input{};
    CombatTuning tuning{};

    int frames = 0;
    while (combat.mode == PlayerMode::Staggered && frames < 300) {
        player_step(player, combat, input, frame, tuning, kStep);
        ++frames;
    }
    CHECK(combat.mode == PlayerMode::Grounded);
    CHECK(frames >= 72);  // 1.2 s at 1/60, plus at most one frame of rounding
    CHECK(frames <= 73);
    CHECK(combat.regenDelay == doctest::Approx(tuning.staminaRegenDelay));
}

TEST_CASE("stamina regenerates on the ground only after the delay") {
    Heightmap ground = flat_heightmap();
    PlayerFrame frame = foot_frame(ground);
    PlayerState player{};
    PlayerCombat combat{};
    combat.stamina = 50.0f;
    PlayerInput input{};
    CombatTuning tuning{};

    for (int i = 0; i < 60; ++i) {
        player_step(player, combat, input, frame, tuning, kStep);
    }
    CHECK(combat.stamina == doctest::Approx(75.0f));  // 25 / s for 1 s

    combat.stamina = 50.0f;
    combat.regenDelay = tuning.staminaRegenDelay;
    for (int i = 0; i < 60; ++i) {
        player_step(player, combat, input, frame, tuning, kStep);
    }
    CHECK(combat.stamina == doctest::Approx(50.0f));  // still inside the delay

    for (int i = 0; i < 180; ++i) {
        player_step(player, combat, input, frame, tuning, kStep);
    }
    CHECK(combat.stamina == doctest::Approx(100.0f));  // capped at the pool
}

TEST_CASE("a frame-time spike cannot overcharge") {
    bool joints_ok = true;
    Heightmap high = flat_heightmap(10.0f, 8.0f);
    PlayerFrame frame = head_frame(high, joints_ok);
    PlayerState player{};
    player.position = Vec3{0.0f, 8.0f, 0.0f};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    CombatTuning tuning{};
    player_step(player, combat, input, frame, tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Grabbing);

    input.strike = true;
    player_step(player, combat, input, frame, tuning, kStep);  // charge += 1/60
    const float before = combat.charge;
    player_step(player, combat, input, frame, tuning, /*dtSeconds=*/0.25f);
    CHECK(combat.charge == doctest::Approx(before + 0.05f).epsilon(1e-4));
}

TEST_CASE("grounded is player_update plus the capsule resolver") {
    Heightmap ground = flat_heightmap();
    // A slight ramp so the ground-follow half of player_update is exercised.
    for (int j = 0; j < ground.height; ++j) {
        for (int i = 0; i < ground.width; ++i) {
            ground.heights[static_cast<std::size_t>(j) * ground.width + i] =
                static_cast<float>(j) * 0.1f;
        }
    }
    const std::vector<Capsule> capsules = {
        Capsule{Vec3{2.0f, 0.0f, 0.0f}, Vec3{2.0f, 2.0f, 0.0f}, 1.0f}};

    PlayerState start{};
    start.position = Vec3{1.2f, 0.0f, 1.0f};
    start.yawDeg = 10.0f;
    PlayerInput input{};
    input.forward = true;
    input.run = true;
    const float dt = 0.031f;
    const float camera_yaw = 37.0f;

    PlayerState expected = start;
    player_update(expected, input, dt, ground, camera_yaw);
    resolve_player_collision(expected, capsules, ground);

    PlayerState actual = start;
    PlayerCombat combat{};
    PlayerFrame frame;
    frame.heightmap = &ground;
    frame.entityCapsules = capsules;
    frame.cameraYawDeg = camera_yaw;
    StepEvents e = player_step(actual, combat, input, frame, CombatTuning{}, dt);

    CHECK(combat.mode == PlayerMode::Grounded);
    CHECK_FALSE(e.grabbed);
    CHECK(actual.position.x == expected.position.x);
    CHECK(actual.position.y == expected.position.y);
    CHECK(actual.position.z == expected.position.z);
    CHECK(actual.yawDeg == expected.yawDeg);
}

TEST_CASE("a strike from a climb shakes the player off too") {
    Heightmap ground = flat_heightmap();
    PlayerFrame frame = waist_chest_frame(ground);
    PlayerState player{};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    CombatTuning tuning{};
    player_step(player, combat, input, frame, tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Grabbing);

    input.forward = true;
    player_step(player, combat, input, frame, tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Climbing);

    input.strike = true;
    player_step(player, combat, input, frame, tuning, kStep);
    REQUIRE(combat.mode == PlayerMode::Striking);
    input.strike = false;
    StepEvents hit = player_step(player, combat, input, frame, tuning, kStep);

    CHECK(hit.damageDealt == 20);  // a tap, not a full charge
    CHECK(hit.gripBroke);
    CHECK(combat.mode == PlayerMode::Falling);
    CHECK(combat.catchSecondsLeft == doctest::Approx(tuning.catchSeconds));
}

TEST_CASE("the combat tuning pins the design values") {
    const CombatTuning tuning{};
    CHECK(tuning.grabRange == doctest::Approx(0.9f));
    CHECK(tuning.climbSpeed == doctest::Approx(1.2f));
    CHECK(tuning.staminaHang == doctest::Approx(2.5f));
    CHECK(tuning.staminaClimb == doctest::Approx(4.0f));
    CHECK(tuning.staminaStrike == doctest::Approx(8.0f));
    CHECK(tuning.staminaRegen == doctest::Approx(25.0f));
    CHECK(tuning.staminaRegenDelay == doctest::Approx(1.0f));
    CHECK(tuning.chargeSeconds == doctest::Approx(0.9f));
    CHECK(tuning.fullChargeFraction == doctest::Approx(0.75f));
    CHECK(tuning.catchSeconds == doctest::Approx(0.6f));
    CHECK(tuning.catchRange == doctest::Approx(2.5f));
    CHECK(tuning.staggerSeconds == doctest::Approx(1.2f));
    CHECK(tuning.damageWeak == doctest::Approx(20.0f));
    CHECK(tuning.damageFull == doctest::Approx(34.0f));
}

TEST_CASE("the right arm band carries weakpoint 1") {
    Heightmap raised = flat_heightmap(10.0f, 4.0f);
    PlayerFrame frame = frame_of(raised,
                                 lookup_of({{"Bone_KATA_R", Vec3{0.0f, 4.0f, 0.0f}},
                                            {"Bone_UDE_R", Vec3{0.0f, 6.0f, 0.0f}}}));
    PlayerState player{};
    PlayerCombat combat{};
    PlayerInput input{};
    input.grab = true;
    StepEvents e = player_step(player, combat, input, frame, CombatTuning{}, kStep);

    REQUIRE(e.grabbed);
    REQUIRE(combat.mode == PlayerMode::Grabbing);
    CHECK(combat.band == ClimbBandId::RightArm);
    CHECK(combat.weakpoint == 1);
}

TEST_CASE("player_mode_name names every mode for the status line") {
    CHECK(std::string(player_mode_name(PlayerMode::Grounded)) == "grounded");
    CHECK(std::string(player_mode_name(PlayerMode::Grabbing)) == "grabbing");
    CHECK(std::string(player_mode_name(PlayerMode::Climbing)) == "climbing");
    CHECK(std::string(player_mode_name(PlayerMode::Striking)) == "striking");
    CHECK(std::string(player_mode_name(PlayerMode::Falling)) == "falling");
    CHECK(std::string(player_mode_name(PlayerMode::Staggered)) == "staggered");
}
