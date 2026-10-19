// Quaternion and TRS math for the skeletal animation path (task A1), and the
// channel sampling / pose evaluation built on them (task A2). Float
// expectations use doctest's Approx unless the value is structurally exact.
#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "anim.h"
#include "asset_io.h"
#include "math3d.h"
#include "player.h"

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

// A one-channel clip on `joint`; `path` is 0 (translation) or 1 (rotation).
// The keys are valid either way, so the channel is sampleable — the root
// query must answer from the channel's joint and path alone.
AnimationCpu one_channel_clip(std::string name, int joint_index, int path) {
    AnimationCpu clip;
    clip.name = std::move(name);
    clip.duration = 1.0f;

    AnimationChannelCpu channel;
    channel.joint = joint_index;
    channel.path = path;
    channel.interpolation = 0;  // LINEAR
    channel.times = {0.0f, 1.0f};
    channel.values = (path == 1) ? std::vector<float>{0.0f, 0.0f, 0.0f, 1.0f,
                                                      0.0f, 0.0f, 0.0f, 1.0f}
                                 : std::vector<float>{0.0f, 0.0f, 0.0f,
                                                      0.0f, 0.0f, 0.0f};
    clip.channels.push_back(std::move(channel));
    return clip;
}

// A playback whose clip list is exactly `names`, in that order: selection is
// by clip name, so the order here must not leak into the answers.
Playback playback_of(std::initializer_list<const char*> names, bool procedural = false) {
    Playback playback;
    for (const char* name : names) {
        AnimationCpu clip;
        clip.name = name;
        clip.duration = 1.0f;
        playback.clips.push_back(std::move(clip));
    }
    playback.index = playback.clips.empty() ? -1 : 0;
    playback.has_procedural = procedural;
    return playback;
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

TEST_CASE("a non-finite time samples the clip start instead of reading out of bounds") {
    const AnimationChannelCpu channel = turn_channel(0);

    // NaN and ±inf compare false against both clamp branches in
    // animation_sample: without the guard the key search runs off the end of
    // times[] (ASan-confirmed in the A2 review). The guard pins them to t = 0,
    // so the first key's value comes out.
    const float non_finite[] = {std::nanf(""), std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity()};
    for (float t : non_finite) {
        float out[4] = {9.0f, 9.0f, 9.0f, 9.0f};
        animation_sample(channel, t, out);
        CHECK(out[0] == 0.0f);
        CHECK(out[1] == 0.0f);
        CHECK(out[2] == 0.0f);
        CHECK(out[3] == 1.0f);
    }

    // The pose path stays finite and equals the t = 0 pose element for element.
    const Scene scene = turn_fixture_scene();
    const std::vector<Mat4> at_nan = animation_pose(scene, turn_clip(0), std::nanf(""));
    const std::vector<Mat4> at_zero = animation_pose(scene, turn_clip(0), 0.0f);
    REQUIRE(at_nan.size() == at_zero.size());
    REQUIRE(!at_nan.empty());
    for (size_t i = 0; i < at_nan.size(); ++i) {
        for (int element = 0; element < 16; ++element) {
            CHECK(at_nan[i].m[element] == at_zero[i].m[element]);
        }
    }
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

TEST_CASE("posed_bounds applies the skin palette and the root lift") {
    // One joint at the origin bound to two vertices at y -1 and +1. A root
    // translation channel lifts the palette by 0.5, so the posed bounds move
    // with it; a scene with no skin keeps its bind bounds.
    Scene scene;
    scene.skin.joints = {joint("Bone_root", -1, {0.0f, 0.0f, 0.0f}, mat4_identity())};
    MeshCpu mesh;
    Vertex low{};
    low.px = -0.5f;
    low.py = -1.0f;
    low.pz = -0.25f;
    low.j[0] = 0;
    low.w[0] = 1.0f;
    Vertex high{};
    high.px = 0.5f;
    high.py = 1.0f;
    high.pz = 0.25f;
    high.j[0] = 0;
    high.w[0] = 1.0f;
    mesh.vertices = {low, high};
    scene.meshes = {mesh};
    scene.boundsMin = Vec3{-0.5f, -1.0f, -0.25f};
    scene.boundsMax = Vec3{0.5f, 1.0f, 0.25f};

    AnimationCpu lift;
    lift.name = "lift";
    lift.duration = 1.0f;
    AnimationChannelCpu root;
    root.joint = 0;
    root.path = 0;  // translation
    root.interpolation = 0;
    root.times = {0.0f};
    root.values = {0.0f, 0.5f, 0.0f};
    lift.channels.push_back(root);

    Vec3 lo{};
    Vec3 hi{};
    posed_bounds(scene, animation_pose(scene, lift, 0.0f), lo, hi);
    CHECK(lo.x == doctest::Approx(-0.5f));
    CHECK(lo.y == doctest::Approx(-0.5f));
    CHECK(lo.z == doctest::Approx(-0.25f));
    CHECK(hi.x == doctest::Approx(0.5f));
    CHECK(hi.y == doctest::Approx(1.5f));
    CHECK(hi.z == doctest::Approx(0.25f));

    // No skin: the same vertices report the bind bounds (weights ignored).
    Scene plain;
    plain.meshes = {mesh};
    plain.boundsMin = scene.boundsMin;
    plain.boundsMax = scene.boundsMax;
    const std::vector<Mat4> no_palette;
    posed_bounds(plain, no_palette, lo, hi);
    CHECK(lo.x == doctest::Approx(-0.5f));
    CHECK(lo.y == doctest::Approx(-1.0f));
    CHECK(lo.z == doctest::Approx(-0.25f));
    CHECK(hi.x == doctest::Approx(0.5f));
    CHECK(hi.y == doctest::Approx(1.0f));
    CHECK(hi.z == doctest::Approx(0.25f));
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

TEST_CASE("the procedural wave animates every joint of a rig smaller than the cap") {
    // The other half of the declared `min(12, joints)` contract: a rig with
    // fewer joints than the cap gets one channel per joint, not the cap
    // (wander's case above pins the cap with 348 joints).
    const Scene scene = turn_fixture_scene();  // 2 joints
    const AnimationCpu wave = make_procedural_wave(scene);
    REQUIRE(scene.skin.joints.size() == 2);
    REQUIRE(wave.channels.size() == 2);
    CHECK(wave.channels[0].joint == 0);
    CHECK(wave.channels[1].joint == 1);

    // Key 0 of each channel is that joint's rest rotation (identity here).
    for (const AnimationChannelCpu& channel : wave.channels) {
        CHECK(channel.values[0] == 0.0f);
        CHECK(channel.values[3] == 1.0f);
    }
}

// --- Clip-list assembly ------------------------------------------------------

TEST_CASE("build_playback prefers real clips and falls back to the procedural wave") {
    // A scene with one animation: the model's own clip is the only clip played
    // — the stand-in exists only for models the converter cannot animate.
    Scene with_clip = turn_fixture_scene();
    with_clip.animations.push_back(turn_clip(0));
    Playback real;
    build_playback(with_clip, real);
    REQUIRE(real.clips.size() == 1);
    CHECK(real.clips[0].name == "turn");
    CHECK(!real.has_procedural);
    CHECK(real.index == 0);
    CHECK(real.playing);
    CHECK(real.time == 0.0f);

    // An empty-animation scene keeps the procedural wave as its single clip.
    const Scene without_clips = turn_fixture_scene();
    Playback fallback;
    build_playback(without_clips, fallback);
    REQUIRE(fallback.clips.size() == 1);
    CHECK(fallback.clips[0].name == "procedural-wave");
    CHECK(fallback.has_procedural);
    CHECK(fallback.index == 0);
    CHECK(fallback.playing);
}

TEST_CASE("build_playback keeps every real clip in scene order") {
    Scene scene = turn_fixture_scene();
    scene.animations.push_back(turn_clip(0));
    AnimationCpu second = turn_clip(0);
    second.name = "second";
    scene.animations.push_back(std::move(second));

    Playback playback;
    build_playback(scene, playback);
    REQUIRE(playback.clips.size() == 2);
    CHECK(playback.clips[0].name == "turn");
    CHECK(playback.clips[1].name == "second");
    CHECK(!playback.has_procedural);
}

TEST_CASE("build_playback leaves a model with no skin and no clips silent") {
    Scene scene;  // no joints, no clips
    Playback playback;
    build_playback(scene, playback);
    CHECK(playback.clips.empty());
    CHECK(!playback.has_procedural);
    CHECK(playback.index == -1);
    CHECK(!playback.playing);
}

// --- Locomotion selection and grounding (task 6) -----------------------------

TEST_CASE("select_clip_index maps the controller state to the demo clips") {
    // Deliberately not scene order (run, stand, walk): the mapping is by clip
    // name, never by index. The expected indices are stand 1, walk 2, run 0.
    const Playback demo = playback_of({"player_a_run", "player_a_stand", "player_a_walk"});
    REQUIRE(demo.clips.size() == 3);

    // No input: standing still plays the stand loop, whether or not Shift is
    // held (run alone does not move the character).
    CHECK(select_clip_index(demo, PlayerInput{}) == 1);
    PlayerInput run_only;
    run_only.run = true;
    CHECK(select_clip_index(demo, run_only) == 1);

    // Each single direction walks; Shift turns any of them into the run loop.
    PlayerInput directions[4];
    directions[0].forward = true;
    directions[1].back = true;
    directions[2].left = true;
    directions[3].right = true;
    for (PlayerInput& input : directions) {
        CHECK(select_clip_index(demo, input) == 2);
        input.run = true;
        CHECK(select_clip_index(demo, input) == 0);
    }
}

TEST_CASE("select_clip_index falls back when the demo clips are missing") {
    PlayerInput still;
    PlayerInput walk;
    walk.forward = true;
    PlayerInput run;
    run.forward = true;
    run.run = true;

    // A run request with no run clip falls back to walk while moving.
    CHECK(select_clip_index(playback_of({"player_a_stand", "player_a_walk"}), run) == 1);

    // A walk request with no walk clip falls through to stand.
    CHECK(select_clip_index(playback_of({"player_a_run", "player_a_stand"}), walk) == 1);

    // With neither walk nor stand, the first real clip answers — scene order,
    // not a name (run is first here, and still is not a run request).
    CHECK(select_clip_index(playback_of({"player_a_run", "player_a_walk"}), still) == 0);

    // A clip list with no locomotion names at all (the horse's kick) keeps its
    // only real clip for every controller state.
    const Playback horse = playback_of({"horse_a_add_horse_kick"});
    CHECK(select_clip_index(horse, still) == 0);
    CHECK(select_clip_index(horse, walk) == 0);
    CHECK(select_clip_index(horse, run) == 0);

    // Nothing to play: -1, so the caller keeps its "no clip" state.
    CHECK(select_clip_index(playback_of({}), still) == -1);
}

TEST_CASE("select_clip_index keeps the procedural stand-in") {
    // A skinned model with no baked clips has exactly one clip, the built-in
    // wave; locomotion has nothing real to select, so every state keeps it.
    const Playback procedural = playback_of({"procedural-wave"}, /*procedural=*/true);
    CHECK(select_clip_index(procedural, PlayerInput{}) == 0);

    PlayerInput run;
    run.forward = true;
    run.run = true;
    CHECK(select_clip_index(procedural, run) == 0);
}

TEST_CASE("the ground lift is skipped when the clip carries the root series") {
    // Bone_root is joint 1, not joint 0: the query must be name-based. The
    // instance mesh at index 0 sits far below the player's meshes, so only the
    // [1, end) range may contribute to the lift.
    Scene scene;
    scene.skin.joints = {
        joint("Bone_KAO", -1, {0.0f, 0.0f, 0.0f}, mat4_identity()),
        joint("Bone_root", 0, {0.0f, 0.0f, 0.0f}, mat4_identity()),
    };
    MeshCpu instance;
    Vertex deep{};
    deep.py = -5.0f;
    instance.vertices = {deep};
    MeshCpu player;
    Vertex feet{};
    feet.py = -0.7596f;
    Vertex hip{};
    hip.py = 0.2f;
    player.vertices = {feet, hip};
    scene.meshes = {instance, player};

    // The clip the converter writes for a root series: translation on Bone_root.
    const AnimationCpu root_translation = one_channel_clip("root-motion", /*joint=*/1, /*path=*/0);
    CHECK(clip_has_root_translation(scene, root_translation));
    CHECK(effective_player_lift(scene, root_translation, /*playerMeshBegin=*/1) == 0.0f);

    // A rotation on Bone_root is not the root series; the S3 lift stands the
    // model on the ground: the player meshes' lowest vertex is 0.7596 below.
    const AnimationCpu root_rotation = one_channel_clip("wave", /*joint=*/1, /*path=*/1);
    CHECK_FALSE(clip_has_root_translation(scene, root_rotation));
    CHECK(player_ground_offset(scene, 1) == doctest::Approx(0.7596f));
    CHECK(effective_player_lift(scene, root_rotation, 1) == doctest::Approx(0.7596f));

    // A translation on another joint, an empty clip, and a channel whose joint
    // is outside the skin carry no root series either.
    CHECK_FALSE(clip_has_root_translation(scene, one_channel_clip("other", /*joint=*/0, /*path=*/0)));
    CHECK_FALSE(clip_has_root_translation(scene, AnimationCpu{}));
    CHECK_FALSE(clip_has_root_translation(scene, one_channel_clip("stray", /*joint=*/99, /*path=*/0)));
}

TEST_CASE("wander's baked clips carry the root series and skip the S3 lift") {
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    REQUIRE(scene.animations.size() == 3);

    // The lift the S3 arena applies to a root-less model: Wander's origin sits
    // 0.7596 m above his feet.
    const float offset = player_ground_offset(scene, 0);
    CHECK(offset == doctest::Approx(0.7596f).epsilon(1e-4));

    for (const AnimationCpu& clip : scene.animations) {
        CAPTURE(clip.name);
        CHECK(clip_has_root_translation(scene, clip));
        CHECK(effective_player_lift(scene, clip, 0) == 0.0f);
    }

    // A channel-less clip (the rest pose) carries no root series: the lift
    // applies as before.
    const AnimationCpu rest{};
    CHECK_FALSE(clip_has_root_translation(scene, rest));
    CHECK(effective_player_lift(scene, rest, 0) == doctest::Approx(offset));
}
