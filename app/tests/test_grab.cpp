// Climb bands and the grab solver (S4.1 Task 2), plus the collision-capsule
// identity that lets a grab exclude the limb it holds. The solver is exercised
// against hand-built joint lookups, so this file needs no asset library.
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
#include "math3d.h"
#include "scene.h"

namespace {

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

// A rig joint whose bind origin is `position` and whose pose is the identity:
// with an identity palette, posed_joint_world_position answers `position`.
JointCpu joint_at(std::string name, Vec3 position) {
    JointCpu joint;
    joint.name = std::move(name);
    joint.parent = -1;
    joint.inverseBind = mat4_from_trs(Vec3{-position.x, -position.y, -position.z},
                                      Quat{0.0f, 0.0f, 0.0f, 1.0f},
                                      Vec3{1.0f, 1.0f, 1.0f});
    joint.restT = position;
    joint.restR = Quat{0.0f, 0.0f, 0.0f, 1.0f};
    joint.restS = Vec3{1.0f, 1.0f, 1.0f};
    return joint;
}

// Two rigs, the second carrying every bone the Valus capsule table names. Rig 1
// is used so the identity test can tell a filled `rig` from the -1 default. An
// empty rig 0 also proves the proxy does not assume the player's slot.
Scene capsule_scene(bool include_left_shin) {
    Scene scene;
    scene.rigs.emplace_back();

    RigCpu& rig = scene.rigs.emplace_back();
    const std::pair<const char*, Vec3> bones[] = {
        {"Bone_MOMO_L", Vec3{0.0f, 4.0f, 0.0f}},
        {"Bone_SUNE_L", Vec3{0.0f, 3.0f, 0.0f}},
        {"Bone_TOE_L", Vec3{0.0f, 1.0f, 0.0f}},
        {"Bone_TUMASAKI_L", Vec3{0.0f, 0.0f, 0.0f}},
        {"Bone_MOMO_R", Vec3{1.0f, 4.0f, 0.0f}},
        {"Bone_SUNE_R", Vec3{1.0f, 3.0f, 0.0f}},
        {"Bone_TOE_R", Vec3{1.0f, 1.0f, 0.0f}},
        {"Bone_TUMASAKI_R", Vec3{1.0f, 0.0f, 0.0f}},
        {"Bone_HARA", Vec3{2.0f, 2.0f, 0.0f}},
        {"Bone_MUNE", Vec3{2.0f, 3.0f, 0.0f}},
        {"Bone_KUBI", Vec3{2.0f, 4.0f, 0.0f}},
    };
    for (const std::pair<const char*, Vec3>& bone : bones) {
        if (!include_left_shin && std::string_view(bone.first) == "Bone_SUNE_L") {
            continue;  // drops table segments 0 and 1, keeping a gap in the indices
        }
        rig.skin.joints.push_back(joint_at(bone.first, bone.second));
    }
    return scene;
}

std::vector<Mat4> identity_palette(const Scene& scene, std::size_t rigIndex) {
    return std::vector<Mat4>(scene.rigs[rigIndex].skin.joints.size(), mat4_identity());
}

}  // namespace

TEST_CASE("climb bands form a connected chain") {
    for (int i = 0; i < static_cast<int>(ClimbBandId::Count); ++i) {
        const ClimbBand& b = climb_band(static_cast<ClimbBandId>(i));
        if (b.up != ClimbBandId::None) {
            REQUIRE(climb_band(b.up).down == static_cast<ClimbBandId>(i));
        }
    }
}

TEST_CASE("every climb band has a distinct stable name") {
    const char* expected[] = {"left_foot", "left_shin",  "left_thigh",     "waist", "chest",
                              "neck",      "head",       "right_shoulder", "right_arm"};
    std::vector<std::string> names;
    for (int i = 0; i < static_cast<int>(ClimbBandId::Count); ++i) {
        const char* name = climb_band_name(static_cast<ClimbBandId>(i));
        REQUIRE(name != nullptr);
        CHECK(std::string(name) == expected[i]);
        REQUIRE(name[0] != '\0');
        names.emplace_back(name);
    }
    std::sort(names.begin(), names.end());
    CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
    CHECK(std::string(climb_band_name(ClimbBandId::Count)) == "none");
}

TEST_CASE("climb band table matches the pinned S4 measurements") {
    // The brief pins these values; a later retune must update the table and
    // this expectation together.
    // R10 lifts the neck/head radii out of the mesh: the head band must clear
    // the forehead shell (~1.73 m from Bone_KAO in z), the neck must not clip.
    const float expected_radius[] = {1.7f, 1.6f, 1.7f, 2.0f, 2.0f, 1.6f, 1.75f, 1.5f, 1.2f};
    const ClimbBandId expected_up[] = {
        ClimbBandId::LeftShin, ClimbBandId::LeftThigh, ClimbBandId::Waist,
        ClimbBandId::Chest,    ClimbBandId::Neck,      ClimbBandId::Head,
        ClimbBandId::None,    ClimbBandId::RightArm,  ClimbBandId::None};
    const ClimbBandId expected_down[] = {
        ClimbBandId::None,     ClimbBandId::LeftFoot,      ClimbBandId::LeftShin,
        ClimbBandId::LeftThigh, ClimbBandId::Waist,         ClimbBandId::Chest,
        ClimbBandId::Neck,      ClimbBandId::Chest,         ClimbBandId::RightShoulder};
    const int expected_weakpoint[] = {-1, -1, -1, -1, -1, -1, 0, -1, 1};
    // The one branch in the table: W plus any lateral at the top of the chest.
    const ClimbBandId expected_branch[] = {
        ClimbBandId::None, ClimbBandId::None,         ClimbBandId::None,
        ClimbBandId::None, ClimbBandId::RightShoulder, ClimbBandId::None,
        ClimbBandId::None, ClimbBandId::None,         ClimbBandId::None};
    for (int i = 0; i < static_cast<int>(ClimbBandId::Count); ++i) {
        const ClimbBand& band = climb_band(static_cast<ClimbBandId>(i));
        CHECK(band.radius == doctest::Approx(expected_radius[i]));
        CHECK(band.up == expected_up[i]);
        CHECK(band.down == expected_down[i]);
        CHECK(band.weakpoint == expected_weakpoint[i]);
        CHECK(band.branch == expected_branch[i]);
    }
}

TEST_CASE("only the chest branches, and its branch rejoins the chain") {
    for (int i = 0; i < static_cast<int>(ClimbBandId::Count); ++i) {
        const ClimbBandId id = static_cast<ClimbBandId>(i);
        const ClimbBand& band = climb_band(id);
        if (id == ClimbBandId::Chest) {
            CHECK(band.branch == ClimbBandId::RightShoulder);
        } else {
            CHECK(band.branch == ClimbBandId::None);
        }
    }

    const ClimbBandId branch = climb_band(ClimbBandId::Chest).branch;
    REQUIRE(branch == ClimbBandId::RightShoulder);
    CHECK(climb_band(branch).down == ClimbBandId::Chest);
    CHECK(climb_band(branch).up == ClimbBandId::RightArm);

    // The trailing default keeps a hand-written six-field band branchless.
    const ClimbBand legacy{"A", "B", 1.0f, ClimbBandId::None, ClimbBandId::None, -1};
    CHECK(legacy.branch == ClimbBandId::None);
}

TEST_CASE("climb surface point sits outside the band radius") {
    JointLookup lookup = [](std::string_view name, Vec3& out) {
        if (name == "A") {
            out = Vec3{0, 0, 0};
            return true;
        }
        if (name == "B") {
            out = Vec3{0, 4, 0};
            return true;
        }
        return false;
    };
    const ClimbBand band{"A", "B", 1.5f, ClimbBandId::None, ClimbBandId::None, -1};
    Vec3 point, normal;
    REQUIRE(climb_surface_point(band, lookup, 0.5f, Vec3{3, 2, 0}, point, normal));
    REQUIRE(point.y == doctest::Approx(2.0f).epsilon(1e-4));
    REQUIRE((point.x * point.x + point.z * point.z) ==
            doctest::Approx((1.5f + kPlayerCapsuleRadius) * (1.5f + kPlayerCapsuleRadius))
                .epsilon(1e-3));

    // The hint (3, 2, 0) is horizontal +X from the axis point (0, 2, 0), so the
    // normal is the unit +X radial and the point sits along it.
    CHECK(normal.x == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(normal.y == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(normal.z == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(point.x == doctest::Approx(1.5f + kPlayerCapsuleRadius).epsilon(1e-4));
}

TEST_CASE("climb surface point falls back to +X when the hint is on the axis") {
    const JointLookup lookup =
        lookup_of({{"A", Vec3{0.0f, 0.0f, 0.0f}}, {"B", Vec3{0.0f, 4.0f, 0.0f}}});
    const ClimbBand band{"A", "B", 1.0f, ClimbBandId::None, ClimbBandId::None, -1};
    Vec3 point{}, normal{};
    REQUIRE(climb_surface_point(band, lookup, 0.25f, Vec3{0.0f, 1.0f, 0.0f}, point, normal));
    CHECK(point.x == doctest::Approx(1.0f + kPlayerCapsuleRadius).epsilon(1e-4));
    CHECK(point.y == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(point.z == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(normal.x == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(normal.y == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(normal.z == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("climb surface point rejects missing and degenerate input without writing") {
    const ClimbBand missing_band{"MISSING_A", "MISSING_B", 1.5f, ClimbBandId::None,
                                 ClimbBandId::None, -1};
    Vec3 point{7.0f, 8.0f, 9.0f};
    Vec3 normal{6.0f, 5.0f, 4.0f};
    const JointLookup missing = [](std::string_view, Vec3&) { return false; };
    CHECK_FALSE(
        climb_surface_point(missing_band, missing, 0.5f, Vec3{1.0f, 0.0f, 0.0f}, point, normal));
    CHECK(point.x == 7.0f);
    CHECK(point.y == 8.0f);
    CHECK(point.z == 9.0f);
    CHECK(normal.x == 6.0f);
    CHECK(normal.y == 5.0f);
    CHECK(normal.z == 4.0f);

    const JointLookup lookup =
        lookup_of({{"A", Vec3{0.0f, 0.0f, 0.0f}}, {"B", Vec3{0.0f, 4.0f, 0.0f}}});
    const ClimbBand band{"A", "B", 1.5f, ClimbBandId::None, ClimbBandId::None, -1};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK_FALSE(climb_surface_point(band, lookup, nan, Vec3{3, 2, 0}, point, normal));
    CHECK_FALSE(climb_surface_point(band, lookup, 0.5f, Vec3{nan, 2, 0}, point, normal));
    CHECK_FALSE(climb_surface_point(band, lookup, inf, Vec3{3, 2, 0}, point, normal));
    CHECK(point.x == 7.0f);
    CHECK(point.y == 8.0f);
    CHECK(point.z == 9.0f);
    CHECK(normal.x == 6.0f);
    CHECK(normal.y == 5.0f);
    CHECK(normal.z == 4.0f);

    // A zero-length segment is degenerate: it has no axis to place t on.
    const JointLookup point_joints =
        lookup_of({{"A", Vec3{1.0f, 2.0f, 3.0f}}, {"B", Vec3{1.0f, 2.0f, 3.0f}}});
    CHECK_FALSE(climb_surface_point(band, point_joints, 0.5f, Vec3{3, 2, 0}, point, normal));
    CHECK(point.x == 7.0f);
    CHECK(point.y == 8.0f);
    CHECK(point.z == 9.0f);
    CHECK(normal.x == 6.0f);
    CHECK(normal.y == 5.0f);
    CHECK(normal.z == 4.0f);
}

TEST_CASE("nearest climb band returns the closest band within reach") {
    // The left shin (radius 1.6) sits at x = 0, the chest (radius 2.0) at
    // x = 5; the player capsule stands at x = 2. Surface distances are
    // 2 - 1.6 - 0.35 = 0.05 and 3 - 2.0 - 0.35 = 0.65.
    const JointLookup lookup = lookup_of({{"Bone_TOE_L", Vec3{0.0f, 0.0f, 0.0f}},
                                          {"Bone_MOMO_L", Vec3{0.0f, 4.0f, 0.0f}},
                                          {"Bone_HARA", Vec3{5.0f, 0.0f, 0.0f}},
                                          {"Bone_MUNE", Vec3{5.0f, 4.0f, 0.0f}}});
    const Vec3 player{2.0f, 0.0f, 0.0f};
    CHECK(nearest_climb_band(lookup, player, 0.1f) == ClimbBandId::LeftShin);
    CHECK(nearest_climb_band(lookup, player, 0.04f) == ClimbBandId::None);
    CHECK(nearest_climb_band(lookup, Vec3{100.0f, 0.0f, 0.0f}, 0.5f) == ClimbBandId::None);
}

TEST_CASE("nearest climb band answers None when no band can be formed") {
    CHECK(nearest_climb_band(JointLookup{}, Vec3{0.0f, 0.0f, 0.0f}, 10.0f) ==
          ClimbBandId::None);
    const JointLookup missing = [](std::string_view, Vec3&) { return false; };
    CHECK(nearest_climb_band(missing, Vec3{0.0f, 0.0f, 0.0f}, 10.0f) == ClimbBandId::None);
}

TEST_CASE("capsule identity carries the rig and the table segment index") {
    // Existing three-field aggregate initialisers keep compiling and default
    // to "no identity".
    const Capsule legacy{Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 0.5f};
    CHECK(legacy.rig == -1);
    CHECK(legacy.segment == -1);

    const Scene scene = capsule_scene(true);
    REQUIRE(scene.rigs.size() == 2);
    const std::vector<Mat4> palette = identity_palette(scene, 1);
    const std::vector<Capsule> capsules = entity_collision_capsules(scene, 1, palette);
    REQUIRE(capsules.size() == 8);
    for (std::size_t i = 0; i < capsules.size(); ++i) {
        CHECK(capsules[i].rig == 1);
        CHECK(capsules[i].segment == static_cast<int>(i));
    }

    // With one bone absent, the first two table entries are skipped. The
    // remaining capsules must keep their table indices (2..7), not renumber.
    const Scene with_gap = capsule_scene(false);
    const std::vector<Mat4> gap_palette = identity_palette(with_gap, 1);
    const std::vector<Capsule> gap_capsules = entity_collision_capsules(with_gap, 1, gap_palette);
    REQUIRE(gap_capsules.size() == 6);
    const int expected_segments[] = {2, 3, 4, 5, 6, 7};
    for (std::size_t i = 0; i < gap_capsules.size(); ++i) {
        CHECK(gap_capsules[i].rig == 1);
        CHECK(gap_capsules[i].segment == expected_segments[i]);
    }
    CHECK(gap_capsules[0].a.y == doctest::Approx(1.0f));  // Bone_TOE_L, the left foot
    CHECK(gap_capsules[0].r == doctest::Approx(1.7f));
}

TEST_CASE("weakpoint ids map to their shell material names") {
    REQUIRE(weakpoint_material_name(0) != nullptr);
    CHECK(std::string(weakpoint_material_name(0)) == "weakpoint_03");
    REQUIRE(weakpoint_material_name(1) != nullptr);
    CHECK(std::string(weakpoint_material_name(1)) == "weakpoint_04");
    CHECK(weakpoint_material_name(2) == nullptr);
    CHECK(weakpoint_material_name(-1) == nullptr);
}

// --- S11 surface walk (design D9) -------------------------------------------
// A horizontal band is walked on its top instead of hung from: the feet sit on
// the surface at an arc offset `across` around the band's axis from its top.

namespace {

// A horizontal band along +Z at y 10 (A at z 0, B at z 10), radius 2.
ClimbBand back_band() {
    ClimbBand band{"A", "B", 2.0f, ClimbBandId::None, ClimbBandId::None, -1};
    band.horizontal = true;
    return band;
}

const JointLookup kBackJoints =
    lookup_of({{"A", Vec3{0.0f, 10.0f, 0.0f}}, {"B", Vec3{0.0f, 10.0f, 10.0f}}});

}  // namespace

TEST_CASE("a surface walk stands on the band's top, normal from its frame") {
    const ClimbBand band = back_band();
    Vec3 point{};
    Vec3 normal{};
    REQUIRE(surface_walk_point(band, kBackJoints, 0.5f, 0.0f, Vec3{0, 1, 0}, point, normal));
    CHECK(point.x == doctest::Approx(0.0f));
    CHECK(point.y == doctest::Approx(12.0f));  // the radius above the axis, no capsule offset
    CHECK(point.z == doctest::Approx(5.0f));
    CHECK(normal.x == doctest::Approx(0.0f));
    CHECK(normal.y == doctest::Approx(1.0f));
    CHECK(normal.z == doctest::Approx(0.0f));

    // Positive `across` is the right of a walker facing A -> B: +Z forward,
    // right = forward x up = -X (player.h). 30 degrees of arc at radius 2.
    const float arc30 = 2.0f * kPi / 6.0f;
    REQUIRE(surface_walk_point(band, kBackJoints, 0.5f, arc30, Vec3{0, 1, 0}, point, normal));
    CHECK(point.x == doctest::Approx(-1.0f));
    CHECK(point.y == doctest::Approx(10.0f + std::sqrt(3.0f)));
    CHECK(normal.x == doctest::Approx(-0.5f));
    CHECK(normal.y == doctest::Approx(std::sqrt(3.0f) / 2.0f));

    // The arc is clamped to the walkable top (kSurfaceWalkMaxAngleDeg).
    Vec3 clamped{};
    REQUIRE(surface_walk_point(band, kBackJoints, 0.5f, -100.0f, Vec3{0, 1, 0}, clamped, normal));
    CHECK(std::acos(normal.y) / kDegToRad == doctest::Approx(kSurfaceWalkMaxAngleDeg));
    CHECK(clamped.x > 0.0f);
}

TEST_CASE("a sloped band's top normal is perpendicular to the bone axis") {
    // The rear kneel tilts the back: A drops 5 m.
    const JointLookup kneel =
        lookup_of({{"A", Vec3{0.0f, 5.0f, 0.0f}}, {"B", Vec3{0.0f, 10.0f, 10.0f}}});
    Vec3 point{};
    Vec3 normal{};
    REQUIRE(surface_walk_point(back_band(), kneel, 0.0f, 0.0f, Vec3{0, 1, 0}, point, normal));
    const Vec3 axis = vec3_normalize(Vec3{0.0f, 5.0f, 10.0f});
    CHECK(vec3_dot(normal, axis) == doctest::Approx(0.0f).epsilon(1e-6));
    CHECK(normal.y > 0.8f);
    CHECK(normal.z < 0.0f);  // leaning back from the rise
    CHECK(point.y == doctest::Approx(5.0f + 2.0f * normal.y));
}

TEST_CASE("a surface walk rejects a vertical band, missing joints and bad input") {
    Vec3 point{7, 7, 7};
    Vec3 normal{7, 7, 7};
    const JointLookup upright =
        lookup_of({{"A", Vec3{0.0f, 0.0f, 0.0f}}, {"B", Vec3{0.0f, 10.0f, 0.0f}}});
    CHECK_FALSE(surface_walk_point(back_band(), upright, 0.5f, 0.0f, Vec3{0, 1, 0}, point, normal));
    const JointLookup missing = [](std::string_view, Vec3&) { return false; };
    CHECK_FALSE(surface_walk_point(back_band(), missing, 0.5f, 0.0f, Vec3{0, 1, 0}, point, normal));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(surface_walk_point(back_band(), kBackJoints, nan, 0.0f, Vec3{0, 1, 0}, point, normal));
    CHECK_FALSE(surface_walk_point(back_band(), kBackJoints, 0.5f, nan, Vec3{0, 1, 0}, point, normal));
    CHECK_FALSE(surface_walk_point(back_band(), kBackJoints, 0.5f, 0.0f, Vec3{0, 0, 0}, point, normal));
    ClimbBand thin = back_band();
    thin.radius = 0.0f;
    CHECK_FALSE(surface_walk_point(thin, kBackJoints, 0.5f, 0.0f, Vec3{0, 1, 0}, point, normal));
    CHECK(point.x == 7.0f);  // nothing written
    CHECK(normal.x == 7.0f);
}

TEST_CASE("a surface walk step moves along and across the band") {
    const ClimbBand band = back_band();
    float t = 0.5f;
    float across = 0.0f;
    // Along: 2 m/s for 1 s over a 10 m band is 0.2 of t.
    REQUIRE(surface_walk_step(band, kBackJoints, 1.0f, 0.0f, 2.0f, 1.0f, t, across));
    CHECK(t == doctest::Approx(0.7f));
    CHECK(across == 0.0f);
    REQUIRE(surface_walk_step(band, kBackJoints, -1.0f, 0.0f, 2.0f, 0.5f, t, across));
    CHECK(t == doctest::Approx(0.6f));
    // Across: metres of arc, clamped to the walkable top.
    REQUIRE(surface_walk_step(band, kBackJoints, 0.0f, 1.0f, 2.0f, 0.25f, t, across));
    CHECK(across == doctest::Approx(0.5f));
    REQUIRE(surface_walk_step(band, kBackJoints, 0.0f, -1.0f, 2.0f, 10.0f, t, across));
    CHECK(across == doctest::Approx(-2.0f * kSurfaceWalkMaxAngleDeg * kDegToRad));
    // The band's ends clamp t; the chain hand-over is the caller's.
    REQUIRE(surface_walk_step(band, kBackJoints, 1.0f, 0.0f, 2.0f, 100.0f, t, across));
    CHECK(t == 1.0f);

    // A non-finite or negative dt is no move; an unresolved band is false.
    const float before_t = t;
    const float before_across = across;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(surface_walk_step(band, kBackJoints, -1.0f, 1.0f, 2.0f, nan, t, across));
    CHECK(surface_walk_step(band, kBackJoints, -1.0f, 1.0f, 2.0f, -1.0f, t, across));
    CHECK(surface_walk_step(band, kBackJoints, nan, nan, 2.0f, 1.0f, t, across));
    CHECK(t == before_t);
    CHECK(across == before_across);
    const JointLookup missing = [](std::string_view, Vec3&) { return false; };
    CHECK_FALSE(surface_walk_step(band, missing, 1.0f, 0.0f, 2.0f, 1.0f, t, across));
}

TEST_CASE("climb_attach_point walks a horizontal band's top") {
    Vec3 point{};
    Vec3 normal{};
    Vec3 expected_point{};
    Vec3 expected_normal{};
    REQUIRE(climb_attach_point(back_band(), kBackJoints, 0.25f, 0.5f, Vec3{9, 0, 0}, point,
                               normal));
    REQUIRE(surface_walk_point(back_band(), kBackJoints, 0.25f, 0.5f, Vec3{0, 1, 0},
                               expected_point, expected_normal));
    CHECK(point.x == expected_point.x);
    CHECK(point.y == expected_point.y);
    CHECK(point.z == expected_point.z);
    CHECK(normal.y == expected_normal.y);
}

TEST_CASE("Valus's bands are all non-horizontal and attach exactly as before") {
    // A Valus-shaped skeleton: every bone the table names, at distinct points.
    const JointLookup valus = lookup_of({{"Bone_TUMASAKI_L", Vec3{-1.0f, 0.0f, 0.5f}},
                                         {"Bone_TOE_L", Vec3{-1.1f, 1.8f, 0.4f}},
                                         {"Bone_MOMO_L", Vec3{-1.3f, 5.0f, 0.2f}},
                                         {"Bone_KOSI", Vec3{0.0f, 7.5f, 0.0f}},
                                         {"Bone_HARA", Vec3{0.0f, 7.5f, 0.0f}},  // shared, as in the rig
                                         {"Bone_MUNE", Vec3{0.1f, 11.0f, 0.6f}},
                                         {"Bone_KUBI", Vec3{0.1f, 13.5f, 1.0f}},
                                         {"Bone_KAO", Vec3{0.1f, 15.2f, 1.6f}},
                                         {"Bone_KATA_R", Vec3{2.5f, 11.5f, 0.3f}},
                                         {"Bone_UDE_R", Vec3{4.0f, 9.0f, 0.9f}}});
    const float ts[] = {-0.5f, 0.0f, 0.3f, 0.5f, 1.0f, 1.5f};
    const Vec3 hints[] = {{5.0f, 3.0f, 0.0f}, {-4.0f, 9.0f, 2.0f}, {0.1f, 11.0f, 0.6f},
                          {0.0f, 0.0f, -6.0f}};
    for (int i = 0; i < static_cast<int>(ClimbBandId::Count); ++i) {
        const ClimbBand& band = climb_band(static_cast<ClimbBandId>(i));
        CAPTURE(i);
        CHECK_FALSE(band.horizontal);
        for (float t : ts) {
            for (Vec3 hint : hints) {
                Vec3 p0{}, n0{}, p1{}, n1{};
                const bool ok0 = climb_surface_point(band, valus, t, hint, p0, n0);
                const bool ok1 = climb_attach_point(band, valus, t, 3.0f, hint, p1, n1);
                CHECK(ok0 == ok1);
                CHECK(p0.x == p1.x);
                CHECK(p0.y == p1.y);
                CHECK(p0.z == p1.z);
                CHECK(n0.x == n1.x);
                CHECK(n0.y == n1.y);
                CHECK(n0.z == n1.z);
            }
        }
    }
    CHECK_FALSE(climb_band(ClimbBandId::Count).horizontal);
}

TEST_CASE("a surface walk step rejects a non-positive or NaN radius") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (float radius : {-2.0f, 0.0f, nan}) {
        ClimbBand band = back_band();
        band.radius = radius;
        float t = 0.5f;
        float across = 0.25f;
        CHECK_FALSE(surface_walk_step(band, kBackJoints, 1.0f, 1.0f, 1.0f, 0.1f, t, across));
        CHECK(t == 0.5f);
        CHECK(across == 0.25f);
    }
}
