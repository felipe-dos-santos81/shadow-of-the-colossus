// Quaternion and TRS math for the skeletal animation path (task A1), and the
// channel sampling / pose evaluation built on them (task A2). Float
// expectations use doctest's Approx unless the value is structurally exact.
#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

#include "anim.h"
#include "asset_io.h"
#include "math3d.h"

TEST_CASE("quat_to_mat4 rotates a point 90 degrees about Z") {
    const Quat q = {0.0f, 0.0f, std::sin(kPi / 4.0f), std::cos(kPi / 4.0f)};
    const Mat4 m = quat_to_mat4(q);
    // column-major: transform (0,1,0) by hand -> (-1,0,0)
    const float x = m.m[4] * 1.0f;   // m[col=1][row=0]
    const float y = m.m[5] * 1.0f;
    CHECK(x == doctest::Approx(-1.0f).epsilon(1e-4));
    CHECK(y == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("quat_slerp halfway between identity and 90 degrees Z is 45 degrees") {
    const Quat a = {0.0f, 0.0f, 0.0f, 1.0f};
    const Quat b = {0.0f, 0.0f, std::sin(kPi / 4.0f), std::cos(kPi / 4.0f)};
    const Quat h = quat_slerp(a, b, 0.5f);
    CHECK(h.z == doctest::Approx(std::sin(kPi / 8.0f)).epsilon(1e-4));
    CHECK(h.w == doctest::Approx(std::cos(kPi / 8.0f)).epsilon(1e-4));
}

TEST_CASE("quat_slerp takes the short way around a negated endpoint") {
    // -b is the same rotation as b: interpolating towards it must not take the
    // 270 degree detour through the far side of the sphere.
    const Quat a = {0.0f, 0.0f, 0.0f, 1.0f};
    const Quat b = {0.0f, 0.0f, -std::sin(kPi / 4.0f), -std::cos(kPi / 4.0f)};
    const Quat h = quat_slerp(a, b, 0.5f);
    CHECK(h.z == doctest::Approx(std::sin(kPi / 8.0f)).epsilon(1e-4));
    CHECK(h.w == doctest::Approx(std::cos(kPi / 8.0f)).epsilon(1e-4));
}

TEST_CASE("mat4_from_trs round-trips through decomposition values") {
    // T(1,2,3), R = 90 degrees about Z, S = (2,2,2): the rotation's columns are
    // scaled by S and the translation lands in column 3.
    const Quat r = {0.0f, 0.0f, std::sin(kPi / 4.0f), std::cos(kPi / 4.0f)};
    const Mat4 m = mat4_from_trs(Vec3{1.0f, 2.0f, 3.0f}, r, Vec3{2.0f, 2.0f, 2.0f});
    CHECK(m.m[12] == doctest::Approx(1.0f));
    CHECK(m.m[13] == doctest::Approx(2.0f));
    CHECK(m.m[14] == doctest::Approx(3.0f));
    CHECK(m.m[0] == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(m.m[1] == doctest::Approx(2.0f).epsilon(1e-4));
    CHECK(m.m[4] == doctest::Approx(-2.0f).epsilon(1e-4));
    CHECK(m.m[5] == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("quat_mul applies its right operand first") {
    const Quat about_z = {0.0f, 0.0f, std::sin(kPi / 4.0f), std::cos(kPi / 4.0f)};
    const Quat about_x = {std::sin(kPi / 4.0f), 0.0f, 0.0f, std::cos(kPi / 4.0f)};
    // R(about_x * about_z) = R(about_x) R(about_z): the Z rotation leaves
    // (0,0,1) alone, then the X rotation maps it to (0,-1,0) — column 2.
    const Mat4 m = quat_to_mat4(quat_mul(about_x, about_z));
    CHECK(m.m[8] == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(m.m[9] == doctest::Approx(-1.0f).epsilon(1e-4));
    CHECK(m.m[10] == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("quat_normalize scales to unit length and never returns NaN") {
    const Quat scaled = quat_normalize(Quat{0.0f, 0.0f, 3.0f, 4.0f});
    CHECK(scaled.z == doctest::Approx(0.6f).epsilon(1e-5));
    CHECK(scaled.w == doctest::Approx(0.8f).epsilon(1e-5));

    const Quat zero = quat_normalize(Quat{0.0f, 0.0f, 0.0f, 0.0f});
    CHECK(zero.x == 0.0f);
    CHECK(zero.y == 0.0f);
    CHECK(zero.z == 0.0f);
    CHECK(zero.w == 1.0f);
}

// --- Channel sampling and pose evaluation (task A2) ---------------------------

namespace {

// Same contract as test_asset_io.cpp's helper (kept local to this file): the
// library path comes from the environment and the tests fail loudly rather
// than skipping when it is unset. A relative value is the documented
// `<repo>/out/library` form, resolved against the repo root — ctest runs the
// tests from `app/build`, not the repo root.
std::filesystem::path test_library() {
    const char* value = std::getenv("SOTC_LIB");
    if (value == nullptr || value[0] == '\0') {
        throw std::runtime_error("Set SOTC_LIB to the converted asset library.");
    }
    std::filesystem::path library = value;
    if (library.is_relative()) {
        // __FILE__ is <repo>/app/tests/test_anim.cpp (CMake passes absolute paths).
        const std::filesystem::path repo_root =
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        library = repo_root / library;
    }
    return library;
}

Mat4 translation_matrix(Vec3 t) {
    return mat4_from_trs(t, Quat{0.0f, 0.0f, 0.0f, 1.0f}, Vec3{1.0f, 1.0f, 1.0f});
}

JointCpu joint(std::string name, int parent, Vec3 restT, Mat4 inverseBind) {
    JointCpu out;
    out.name = std::move(name);
    out.parent = parent;
    out.inverseBind = inverseBind;
    out.restT = restT;
    out.restR = {0.0f, 0.0f, 0.0f, 1.0f};
    out.restS = {1.0f, 1.0f, 1.0f};
    return out;
}

// The same two-joint fixture test_asset_io.cpp loads from its inline glTF:
// JointA (root) at (0,1,0) with inverse bind T(0,-1,0), JointB (child) one more
// unit up with inverse bind T(0,-2,0), and the one-second "turn" clip rotating
// JointA 90 degrees about Z. Built here as the DTOs that loader produces, so
// the pose math is pinned against hand-derived values; the glb-load path is
// exercised by the wander case below.
Scene turn_fixture_scene() {
    Scene scene;
    scene.skin.joints = {
        joint("JointA", -1, {0.0f, 1.0f, 0.0f}, translation_matrix({0.0f, -1.0f, 0.0f})),
        joint("JointB", 0, {0.0f, 1.0f, 0.0f}, translation_matrix({0.0f, -2.0f, 0.0f})),
    };
    return scene;
}

AnimationChannelCpu turn_channel(int interpolation) {
    AnimationChannelCpu channel;
    channel.joint = 0;
    channel.path = 1;  // rotation
    channel.interpolation = interpolation;
    channel.times = {0.0f, 1.0f};
    channel.values = {0.0f, 0.0f, 0.0f, 1.0f,
                      0.0f, 0.0f, std::sin(kPi / 4.0f), std::cos(kPi / 4.0f)};  // 90 degrees about Z
    return channel;
}

AnimationCpu turn_clip(int interpolation) {
    AnimationCpu clip;
    clip.name = "turn";
    clip.duration = 1.0f;
    clip.channels.push_back(turn_channel(interpolation));
    return clip;
}

// Whether every element of `m` is within `epsilon` of the identity matrix.
bool is_identity(const Mat4& m, float epsilon) {
    for (int i = 0; i < 16; ++i) {
        const float expected = (i % 5 == 0) ? 1.0f : 0.0f;
        if (std::abs(m.m[i] - expected) > epsilon) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("sampling LINEAR at the midpoint interpolates rotations") {
    const AnimationChannelCpu channel = turn_channel(0);
    float out[4] = {};
    animation_sample(channel, 0.5f, out);
    // Halfway from identity to 90 degrees about Z is 45 degrees about Z.
    CHECK(out[0] == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(out[1] == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(out[2] == doctest::Approx(std::sin(kPi / 8.0f)).epsilon(1e-4));
    CHECK(out[3] == doctest::Approx(std::cos(kPi / 8.0f)).epsilon(1e-4));
}

TEST_CASE("sampling LINEAR lerps translations componentwise") {
    AnimationChannelCpu channel;
    channel.joint = 0;
    channel.path = 0;  // translation
    channel.interpolation = 0;
    channel.times = {0.0f, 1.0f};
    channel.values = {0.0f, 0.0f, 0.0f, 2.0f, 4.0f, 6.0f};

    float out[3] = {};
    animation_sample(channel, 0.25f, out);
    CHECK(out[0] == doctest::Approx(0.5f).epsilon(1e-5));
    CHECK(out[1] == doctest::Approx(1.0f).epsilon(1e-5));
    CHECK(out[2] == doctest::Approx(1.5f).epsilon(1e-5));
}

TEST_CASE("STEP holds until the next key") {
    const AnimationChannelCpu channel = turn_channel(1);
    float out[4] = {};
    animation_sample(channel, 0.99f, out);
    CHECK(out[0] == 0.0f);
    CHECK(out[1] == 0.0f);
    CHECK(out[2] == 0.0f);
    CHECK(out[3] == 1.0f);  // still key 0's value

    // At the next key's own time the hold ends: the value is that key's.
    animation_sample(channel, 1.0f, out);
    CHECK(out[2] == std::sin(kPi / 4.0f));
    CHECK(out[3] == std::cos(kPi / 4.0f));
}

TEST_CASE("sampling clamps outside the key range") {
    const AnimationChannelCpu channel = turn_channel(0);
    float out[4] = {};
    animation_sample(channel, -1.0f, out);  // before the first key -> key 0
    CHECK(out[0] == 0.0f);
    CHECK(out[1] == 0.0f);
    CHECK(out[2] == 0.0f);
    CHECK(out[3] == 1.0f);

    animation_sample(channel, 99.0f, out);  // after the last key -> last key
    CHECK(out[2] == std::sin(kPi / 4.0f));
    CHECK(out[3] == std::cos(kPi / 4.0f));
}

TEST_CASE("loop time wraps and handles zero duration") {
    CHECK(animation_loop_time(1.5f, 1.0f) == doctest::Approx(0.5f));
    CHECK(animation_loop_time(3.0f, 2.0f) == doctest::Approx(1.0f));
    CHECK(animation_loop_time(3.0f, 0.0f) == doctest::Approx(0.0f));
}

TEST_CASE("the fixture pose at t=0 is the bind pose") {
    const Scene scene = turn_fixture_scene();
    const std::vector<Mat4> palette = animation_pose(scene, turn_clip(0), 0.0f);
    REQUIRE(palette.size() == 2);
    for (const Mat4& m : palette)
        for (int i = 0; i < 16; ++i)
            CHECK(m.m[i] == doctest::Approx(i % 5 == 0 ? 1.0f : 0.0f).epsilon(1e-4));
}

TEST_CASE("the fixture pose at t=1 rotates the bound vertex around JointA") {
    const Scene scene = turn_fixture_scene();
    const std::vector<Mat4> palette = animation_pose(scene, turn_clip(0), 1.0f);
    REQUIRE(palette.size() == 2);

    // Palette 0 = world(JointA) * inverseBind = T(0,1,0) * Rz(90) * T(0,-1,0):
    // it maps the fixture vertex (0,2,0) to (-1,1,0).
    const Mat4& p = palette[0];
    CHECK(p.m[4] * 2.0f + p.m[12] == doctest::Approx(-1.0f).epsilon(1e-4));
    CHECK(p.m[5] * 2.0f + p.m[13] == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(p.m[6] * 2.0f + p.m[14] == doctest::Approx(0.0f).epsilon(1e-4));

    // Palette 1 rides JointA's rotation through the parent chain: JointB's world
    // is T(0,1,0) * Rz(90) * T(0,1,0), and the same vertex again lands at
    // (-1,1,0) — a wrong (rest-only) parent composition would leave it at
    // (0,2,0).
    const Mat4& q = palette[1];
    CHECK(q.m[4] * 2.0f + q.m[12] == doctest::Approx(-1.0f).epsilon(1e-4));
    CHECK(q.m[5] * 2.0f + q.m[13] == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(q.m[6] * 2.0f + q.m[14] == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("wander's rest palette is identity for all 348 joints") {
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    AnimationCpu empty;  // no channels
    const std::vector<Mat4> palette = animation_pose(scene, empty, 0.0f);
    REQUIRE(palette.size() == 348);
    for (const Mat4& m : palette)
        for (int i = 0; i < 16; ++i)
            CHECK(m.m[i] == doctest::Approx(i % 5 == 0 ? 1.0f : 0.0f).epsilon(1e-3));
}

TEST_CASE("the procedural wave starts at rest and moves later") {
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    const AnimationCpu wave = make_procedural_wave(scene);

    // The declared shape: a 2 s loop, one rotation channel per animated joint
    // (the first 12 of wander's 348), LINEAR keys at 0/1/2 s.
    CHECK(wave.name == "procedural-wave");
    CHECK(wave.duration == doctest::Approx(2.0f));
    REQUIRE(wave.channels.size() == 12);
    for (const AnimationChannelCpu& channel : wave.channels) {
        CHECK(channel.path == 1);
        CHECK(channel.interpolation == 0);
        const std::vector<float> expected_times = {0.0f, 1.0f, 2.0f};
        CHECK(channel.times == expected_times);
        CHECK(channel.values.size() == 12);
    }

    // At t=0 every key holds the joint's rest rotation: the palette is the bind
    // pose (the converter guarantees inverseBind = inverse(rest world)).
    const std::vector<Mat4> at_rest = animation_pose(scene, wave, 0.0f);
    REQUIRE(at_rest.size() == 348);
    for (const Mat4& m : at_rest)
        for (int i = 0; i < 16; ++i)
            CHECK(m.m[i] == doctest::Approx(i % 5 == 0 ? 1.0f : 0.0f).epsilon(1e-3));

    // Halfway through the first key span the tilt is 7.5 degrees about X: at
    // least one joint's palette has moved off the bind pose.
    const std::vector<Mat4> mid = animation_pose(scene, wave, 0.5f);
    REQUIRE(mid.size() == 348);
    CHECK(!std::all_of(mid.begin(), mid.end(),
                       [](const Mat4& m) { return is_identity(m, 1e-3f); }));
}
