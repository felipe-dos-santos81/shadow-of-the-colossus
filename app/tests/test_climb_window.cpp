// S12 Task 7 (design D7/D8, §13.4): band enable windows, the slope gate on
// walkable bands, the blade's low-end grab entry and its walk pace, and what a
// band disabling under the player does (fall, or be carried onto `carryTo`).
// Synthetic bands and hand-built joint lookups: no asset library; Gaius's own
// band table is the next task's.
#include <doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "grab.h"
#include "heightmap.h"
#include "math3d.h"
#include "player.h"
#include "player_state.h"

namespace {

constexpr float kStep = 1.0f / 60.0f;
constexpr const char* kSlamClip = "knight_a_atk1_3";

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

Heightmap flat_heightmap(float halfExtent = 20.0f) {
    Heightmap heightmap;
    const int nodes = static_cast<int>(halfExtent) * 2 + 1;
    heightmap.cellSize = 1.0f;
    heightmap.width = nodes;
    heightmap.height = nodes;
    heightmap.heights.assign(static_cast<std::size_t>(nodes) * static_cast<std::size_t>(nodes),
                             0.0f);
    heightmap.boundsMin = Vec3{-halfExtent, 0.0f, -halfExtent};
    heightmap.boundsMax = Vec3{halfExtent, 0.0f, halfExtent};
    return heightmap;
}

float distance(Vec3 a, Vec3 b) {
    const Vec3 d{a.x - b.x, a.y - b.y, a.z - b.z};
    return std::sqrt(vec3_dot(d, d));
}

// A blade (band 0: TIP -> HILT, walked, enabled in the slam window [9, 75),
// grabbed only at its low end, 5 m/s, the upper 40 % carried onto band 1) and a
// forearm (band 1: HILT -> ELBOW, lying back along the blade's upper part).
inline constexpr std::array<ClimbBand, 2> kBladeBands{{
    {.boneA = "TIP", .boneB = "HILT", .radius = 0.5f, .up = 1, .down = -1, .weakpoint = -1,
     .horizontal = true, .name = "blade", .enableClip = kSlamClip, .enableFrom = 9,
     .enableTo = 75, .walkSpeed = 5.0f, .carryFraction = 0.4f, .carryTo = 1,
     .grabEntryLowEnd = true},
    {.boneA = "HILT", .boneB = "ELBOW", .radius = 0.5f, .up = -1, .down = 0, .weakpoint = -1,
     .horizontal = true, .name = "forearm"},
}};

// The flat blade: y 2, z 0 -> 10; the forearm from the hilt back to z 4, rising
// 0.4 m, so its top passes 0.2 m over the blade's top at 70 %.
PlayerFrame blade_frame(const Heightmap& ground, int frameIndex, Vec3 tip = {0.0f, 2.0f, 0.0f},
                        Vec3 hilt = {0.0f, 2.0f, 10.0f}, Vec3 elbow = {0.0f, 2.4f, 4.0f}) {
    PlayerFrame frame;
    frame.heightmap = &ground;
    frame.jointWorld = lookup_of({{"TIP", tip}, {"HILT", hilt}, {"ELBOW", elbow}});
    frame.bands = kBladeBands;
    frame.entityClip = kSlamClip;
    frame.entityFrame = frameIndex;
    return frame;
}

// Grabs the blade at its low end (feet just short of the tip) in the window.
void grab_blade(PlayerState& player, PlayerCombat& combat, const PlayerFrame& frame) {
    player.position = Vec3{0.0f, 0.0f, -0.6f};
    PlayerInput input{};
    input.grab = true;
    player_step(player, combat, input, frame, CombatTuning{}, kStep);
    REQUIRE(combat.band == 0);
}

}  // namespace

TEST_CASE("band_enabled: a window band is enabled in [enableFrom, enableTo) of its clip only") {
    const ClimbBand& blade = kBladeBands[0];
    CHECK_FALSE(band_enabled(blade, kSlamClip, 8));
    CHECK(band_enabled(blade, kSlamClip, 9));
    CHECK(band_enabled(blade, kSlamClip, 74));
    CHECK_FALSE(band_enabled(blade, kSlamClip, 75));  // PULLSWD fires at 75.0
    CHECK_FALSE(band_enabled(blade, "knight_a_atk1_2", 20));
    CHECK_FALSE(band_enabled(blade, "", 20));

    // The defaults: no clip = always enabled (every S4/S11 band).
    const ClimbBand& forearm = kBladeBands[1];
    CHECK(band_enabled(forearm, "", 0));
    CHECK(band_enabled(forearm, kSlamClip, 1000));

    // enableTo < 0: open to the clip's end.
    ClimbBand open = blade;
    open.enableTo = -1;
    CHECK(band_enabled(open, kSlamClip, 5000));
    CHECK_FALSE(band_enabled(open, kSlamClip, 8));
}

TEST_CASE("E at the low end grabs the blade; E at mid-blade does not") {
    Heightmap ground = flat_heightmap();
    // A tilted blade: the tip 1 m up, the hilt 4 m up, 10 m along z; the
    // forearm out of reach above the hilt.
    const Vec3 tip{0.0f, 1.0f, 0.0f};
    const Vec3 hilt{0.0f, 4.0f, 10.0f};
    const Vec3 elbow{0.0f, 12.0f, 14.0f};
    const PlayerFrame frame = blade_frame(ground, 20, tip, hilt, elbow);

    PlayerState low{};
    PlayerCombat lowCombat{};
    low.position = Vec3{0.0f, 0.0f, -0.6f};
    PlayerInput input{};
    input.grab = true;
    const StepEvents grabbed = player_step(low, lowCombat, input, frame, CombatTuning{}, kStep);
    CHECK(grabbed.grabbed);
    CHECK(lowCombat.band == 0);
    CHECK(lowCombat.bandT == 0.0f);  // on the low end itself

    // Mid-blade: the segment is within grab range of the capsule ...
    const Vec3 mid{0.0f, 0.0f, 5.0f};
    ClimbBand plain = kBladeBands[0];
    plain.grabEntryLowEnd = false;
    const std::array<ClimbBand, 1> plainBands{plain};
    CHECK(nearest_climb_band(plainBands, frame.jointWorld, mid, CombatTuning{}.grabRange,
                             kSlamClip, 20) == 0);
    // ... but the blade is entered only at its low end.
    PlayerState player{};
    PlayerCombat combat{};
    player.position = mid;
    const StepEvents none = player_step(player, combat, input, frame, CombatTuning{}, kStep);
    CHECK_FALSE(none.grabbed);
    CHECK(combat.mode == PlayerMode::Grounded);
    CHECK(combat.band == ClimbBandId::None);

    // Outside the window the low end grabs nothing either.
    PlayerState late{};
    PlayerCombat lateCombat{};
    late.position = Vec3{0.0f, 0.0f, -0.6f};
    player_step(late, lateCombat, input, blade_frame(ground, 75, tip, hilt, elbow), CombatTuning{},
                kStep);
    CHECK(lateCombat.band == ClimbBandId::None);
}

TEST_CASE("the blade walk speed is 5.0 m/s") {
    Heightmap ground = flat_heightmap();
    const PlayerFrame frame = blade_frame(ground, 20);
    PlayerState player{};
    PlayerCombat combat{};
    grab_blade(player, combat, frame);
    const float z0 = player.position.z;
    PlayerInput input{};
    input.grab = true;
    input.forward = true;
    for (int i = 0; i < 60; ++i) {  // 1 s
        player_step(player, combat, input, frame, CombatTuning{}, kStep);
    }
    CHECK(combat.band == 0);
    CHECK(combat.bandT == doctest::Approx(0.5f).epsilon(0.01));
    CHECK(player.position.z - z0 == doctest::Approx(5.0f).epsilon(0.01));
    CHECK(player.position.y == doctest::Approx(2.5f));  // standing on its top
    CHECK(CombatTuning{}.surfaceWalkSpeed == 2.2f);     // the default pace is untouched
}

TEST_CASE("disable at 30 % -> Falling, no band") {
    Heightmap ground = flat_heightmap();
    PlayerState player{};
    PlayerCombat combat{};
    grab_blade(player, combat, blade_frame(ground, 20));
    combat.bandT = 0.3f;
    PlayerInput input{};
    input.grab = true;
    player_step(player, combat, input, blade_frame(ground, 74), CombatTuning{}, kStep);
    REQUIRE(combat.band == 0);

    const StepEvents events =
        player_step(player, combat, input, blade_frame(ground, 75), CombatTuning{}, kStep);
    CHECK(combat.mode == PlayerMode::Falling);
    CHECK(combat.band == ClimbBandId::None);
    CHECK(events.gripBroke);
    // Nothing to stand on: the next step falls ...
    const float y = player.position.y;
    input.grab = false;
    player_step(player, combat, input, blade_frame(ground, 76), CombatTuning{}, kStep);
    CHECK(combat.mode == PlayerMode::Falling);
    CHECK(player.position.y < y);
    // ... and the catch press cannot take the disabled blade, the nearest band.
    input.grab = true;
    player_step(player, combat, input, blade_frame(ground, 77), CombatTuning{}, kStep);
    CHECK(combat.band != 0);
}

TEST_CASE("a horizontal band at 65 deg is hung, at 55 deg walked") {
    Heightmap ground = flat_heightmap();
    const auto slope_frame = [&](float deg) {
        PlayerFrame frame;
        frame.heightmap = &ground;
        const float r = deg * kDegToRad;
        frame.jointWorld = lookup_of({{"TIP", {0.0f, 0.0f, 0.0f}},
                                      {"HILT", {0.0f, 10.0f * std::sin(r), 10.0f * std::cos(r)}},
                                      {"ELBOW", {0.0f, 20.0f, 20.0f}}});
        frame.bands = kBladeBands;
        frame.entityClip = kSlamClip;
        frame.entityFrame = 20;
        return frame;
    };
    CHECK(kWalkableBandMaxSlopeDeg == 60.0f);
    CHECK(band_walkable(kBladeBands[0], slope_frame(55.0f).jointWorld));
    CHECK_FALSE(band_walkable(kBladeBands[0], slope_frame(65.0f).jointWorld));
    CHECK(band_walkable(kBladeBands[1], slope_frame(55.0f).jointWorld));  // the 40 deg forearm

    // In the state machine: walked = standing (stamina regenerates, on the top);
    // hung = gripping (stamina drains, beside the axis).
    for (const float deg : {55.0f, 65.0f}) {
        CAPTURE(deg);
        const PlayerFrame frame = slope_frame(deg);
        PlayerState player{};
        PlayerCombat combat{};
        grab_blade(player, combat, frame);
        combat.stamina = 50.0f;
        PlayerInput input{};
        input.grab = true;
        for (int i = 0; i < 60; ++i) {
            player_step(player, combat, input, frame, CombatTuning{}, kStep);
        }
        REQUIRE(combat.band == 0);
        if (deg < 60.0f) {
            CHECK(combat.stamina == doctest::Approx(75.0f).epsilon(0.01));
            CHECK(player.position.y > 0.0f);  // on the top: above the low end's axis
        } else {
            CHECK(combat.stamina == doctest::Approx(47.5f).epsilon(0.01));
            CHECK(player.position.y == doctest::Approx(0.0f).epsilon(0.01));  // beside it
        }
    }
}

// --- Ruling R-J: high on the blade, the player rides it as it lifts ----------

namespace {

// Holds the blade at `t` in the window (frame 74), then lets it close (75).
void hold_through_close(PlayerState& player, PlayerCombat& combat, const Heightmap& ground,
                        float t, Vec3& before, StepEvents& closing) {
    grab_blade(player, combat, blade_frame(ground, 20));
    combat.bandT = t;
    PlayerInput input{};
    input.grab = true;
    player_step(player, combat, input, blade_frame(ground, 74), CombatTuning{}, kStep);
    REQUIRE(combat.band == 0);
    before = player.position;
    closing = player_step(player, combat, input, blade_frame(ground, 75), CombatTuning{}, kStep);
}

}  // namespace

TEST_CASE("closes at 70 % -> still on the blade, continuous, riding it as the bones move") {
    Heightmap ground = flat_heightmap();
    PlayerState player{};
    PlayerCombat combat{};
    Vec3 before{};
    StepEvents closing{};
    hold_through_close(player, combat, ground, 0.7f, before, closing);
    CHECK(combat.band == 0);
    CHECK(combat.mode != PlayerMode::Falling);
    CHECK_FALSE(closing.gripBroke);
    CHECK(combat.bandT == doctest::Approx(0.7f));
    CHECK(distance(player.position, before) <= 0.5f);

    // The sword lifts 1 m after the window: the held player goes with it.
    PlayerInput input{};
    input.grab = true;
    const Vec3 onBlade = player.position;
    player_step(player, combat, input,
                blade_frame(ground, 80, {0.0f, 3.0f, 0.0f}, {0.0f, 3.0f, 10.0f}), CombatTuning{},
                kStep);
    CHECK(combat.band == 0);
    CHECK(player.position.y == doctest::Approx(onBlade.y + 1.0f));
    CHECK(player.position.z == doctest::Approx(onBlade.z));

    // Letting go of E still drops him.
    input.grab = false;
    player_step(player, combat, input, blade_frame(ground, 81), CombatTuning{}, kStep);
    CHECK(combat.mode == PlayerMode::Falling);
    CHECK(combat.band == ClimbBandId::None);
}

TEST_CASE("from the blade held past its window, its upper end links onto carryTo") {
    // `up` cleared: the hand-over past the window is carryTo's.
    std::array<ClimbBand, 2> bands = kBladeBands;
    bands[0].up = ClimbBandId::None;
    Heightmap ground = flat_heightmap();
    const auto frame_at = [&](int frameIndex) {
        PlayerFrame frame = blade_frame(ground, frameIndex);
        frame.bands = bands;
        return frame;
    };
    PlayerState player{};
    PlayerCombat combat{};
    grab_blade(player, combat, frame_at(20));
    combat.bandT = 0.7f;
    PlayerInput input{};
    input.grab = true;
    player_step(player, combat, input, frame_at(75), CombatTuning{}, kStep);
    REQUIRE(combat.band == 0);
    input.forward = true;
    for (int i = 0; i < 120 && combat.band == 0; ++i) {  // 3 m at 5 m/s
        player_step(player, combat, input, frame_at(80), CombatTuning{}, kStep);
    }
    CHECK(combat.band == 1);
    CHECK(combat.mode != PlayerMode::Falling);
}

TEST_CASE("a fresh grab on the closed blade fails") {
    Heightmap ground = flat_heightmap();
    PlayerState player{};
    PlayerCombat combat{};
    player.position = Vec3{0.0f, 0.0f, -0.6f};  // at the low end, as grab_blade
    PlayerInput input{};
    input.grab = true;
    const StepEvents events =
        player_step(player, combat, input, blade_frame(ground, 80), CombatTuning{}, kStep);
    CHECK_FALSE(events.grabbed);
    CHECK(combat.band == ClimbBandId::None);
    CHECK(combat.mode == PlayerMode::Grounded);
}
