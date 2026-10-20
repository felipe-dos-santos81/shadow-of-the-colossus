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
#include "collision.h"
#include "heightmap.h"
#include "manifest.h"
#include "math3d.h"
#include "player.h"
#include "player_state.h"

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

// A scene whose player rig (rig 0) holds `joints`: what load_scene produces for
// a skinned model (and what the single-rig pose functions read).
Scene scene_with_player_joints(std::vector<JointCpu> joints) {
    Scene scene;
    RigCpu rig;
    rig.dynamic = true;
    rig.skin.joints = std::move(joints);
    scene.rigs.push_back(std::move(rig));
    return scene;
}

// The same two-joint fixture test_asset_io.cpp loads from its inline glTF:
// JointA (root) at (0,1,0) with inverse bind T(0,-1,0), JointB (child) one more
// unit up with inverse bind T(0,-2,0), and the one-second "turn" clip rotating
// JointA 90 degrees about Z. Built here as the DTOs that loader produces, so
// the pose math is pinned against hand-derived values; the glb-load path is
// exercised by the wander case below.
Scene turn_fixture_scene() {
    return scene_with_player_joints({
        joint("JointA", -1, {0.0f, 1.0f, 0.0f}, translation_matrix({0.0f, -1.0f, 0.0f})),
        joint("JointB", 0, {0.0f, 1.0f, 0.0f}, translation_matrix({0.0f, -2.0f, 0.0f})),
    });
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

// A rig of `joint_count` joints, all at the origin with identity rest TRS and
// identity inverse binds: the palette at rest is the identity matrix for every
// joint, so only a channel can move one.
RigCpu rig_of_identity_joints(std::size_t joint_count, const std::string& name_prefix) {
    RigCpu rig;
    rig.skin.joints.resize(joint_count);
    for (std::size_t i = 0; i < joint_count; ++i) {
        JointCpu& joint = rig.skin.joints[i];
        joint.name = name_prefix + std::to_string(i);
        joint.parent = -1;
        joint.inverseBind = mat4_identity();
        joint.restT = Vec3{0.0f, 0.0f, 0.0f};
        joint.restR = {0.0f, 0.0f, 0.0f, 1.0f};
        joint.restS = Vec3{1.0f, 1.0f, 1.0f};
    }
    return rig;
}

// One scene, two rigs — the shape of the T3 review finding: a small player rig
// (0) and a much larger entity rig (1) whose joint indices do not fit the
// player's palette.
Scene scene_with_two_rigs(std::size_t player_joints, std::size_t entity_joints) {
    Scene scene;
    RigCpu player = rig_of_identity_joints(player_joints, "player_j");
    player.dynamic = true;
    scene.rigs.push_back(std::move(player));
    scene.rigs.push_back(rig_of_identity_joints(entity_joints, "entity_j"));
    return scene;
}

// The one-second turn clip retargeted to any joint of any rig.
AnimationCpu turn_clip_on(std::string name, int joint_index) {
    AnimationCpu clip;
    clip.name = std::move(name);
    clip.duration = 1.0f;
    AnimationChannelCpu channel = turn_channel(0);
    channel.joint = joint_index;
    clip.channels.push_back(std::move(channel));
    return clip;
}

// A vertex bound to one joint: JOINTS_0 = {joint, 0, 0, 0}, WEIGHTS_0 = {w, 0, 0, 0}.
Vertex vertex_on_joint(int joint_index, float weight) {
    Vertex vertex{};
    vertex.j[0] = static_cast<uint16_t>(joint_index);
    vertex.w[0] = weight;
    return vertex;
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
    const std::vector<Mat4> at_nan = animation_pose(scene, 0, turn_clip(0), std::nanf(""));
    const std::vector<Mat4> at_zero = animation_pose(scene, 0, turn_clip(0), 0.0f);
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
    const std::vector<Mat4> palette = animation_pose(scene, 0, turn_clip(0), 0.0f);
    REQUIRE(palette.size() == 2);
    for (const Mat4& m : palette)
        for (int i = 0; i < 16; ++i)
            CHECK(m.m[i] == doctest::Approx(i % 5 == 0 ? 1.0f : 0.0f).epsilon(1e-4));
}

TEST_CASE("the fixture pose at t=1 rotates the bound vertex around JointA") {
    const Scene scene = turn_fixture_scene();
    const std::vector<Mat4> palette = animation_pose(scene, 0, turn_clip(0), 1.0f);
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
    const std::vector<Mat4> palette = animation_pose(scene, scene.playerRig, empty, 0.0f);
    REQUIRE(palette.size() == 348);
    for (const Mat4& m : palette)
        for (int i = 0; i < 16; ++i)
            CHECK(m.m[i] == doctest::Approx(i % 5 == 0 ? 1.0f : 0.0f).epsilon(1e-3));
}

TEST_CASE("posed_bounds applies the skin palette and the root lift") {
    // One joint at the origin bound to two vertices at y -1 and +1. A root
    // translation channel lifts the palette by 0.5, so the posed bounds move
    // with it; a scene with no skin keeps its bind bounds.
    Scene scene = scene_with_player_joints(
        {joint("Bone_root", -1, {0.0f, 0.0f, 0.0f}, mat4_identity())});
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
    posed_bounds(scene, animation_pose(scene, 0, lift, 0.0f), lo, hi);
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

TEST_CASE("posed_bounds applies the mesh model transform and passes zero-weight vertices through") {
    // The composited-scene contract: skinning happens in glb space, then the mesh's own model
    // transform places it in the world. A vertex with no weights (an unskinned primitive inside
    // a skinned scene) stays at its bind position, exactly as the shader's zero-weight path does.
    Scene scene = scene_with_player_joints(
        {joint("Bone_root", -1, {0.0f, 0.0f, 0.0f}, mat4_identity())});

    MeshCpu mesh;
    Vertex weighted{};
    weighted.j[0] = 0;
    weighted.w[0] = 1.0f;
    Vertex unweighted{};
    unweighted.px = 1.0f;
    unweighted.pz = -1.0f;
    unweighted.j[0] = 0;  // a valid joint index, but zero weight: pass-through
    unweighted.w[0] = 0.0f;
    mesh.vertices = {weighted, unweighted};
    mesh.model = translation_matrix({0.0f, 2.0f, 0.0f});
    scene.meshes = {mesh};

    // A palette that lifts the weighted vertex by 3 m: (0,0,0) -> (0,3,0) -> model (0,5,0).
    const std::vector<Mat4> palette{translation_matrix({0.0f, 3.0f, 0.0f})};
    Vec3 lo{};
    Vec3 hi{};
    posed_bounds(scene, palette, lo, hi);

    CHECK(lo.x == doctest::Approx(0.0f));
    CHECK(lo.y == doctest::Approx(2.0f));   // the zero-weight vertex at its bind y, then model +2
    CHECK(lo.z == doctest::Approx(-1.0f));
    CHECK(hi.x == doctest::Approx(1.0f));
    CHECK(hi.y == doctest::Approx(5.0f));
    CHECK(hi.z == doctest::Approx(0.0f));
}

TEST_CASE("the procedural wave starts at rest and moves later") {
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    const AnimationCpu wave = make_procedural_wave(scene, 0);

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
    const std::vector<Mat4> at_rest = animation_pose(scene, 0, wave, 0.0f);
    REQUIRE(at_rest.size() == 348);
    for (const Mat4& m : at_rest)
        for (int i = 0; i < 16; ++i)
            CHECK(m.m[i] == doctest::Approx(i % 5 == 0 ? 1.0f : 0.0f).epsilon(1e-3));

    // Halfway through the first key span the tilt is 7.5 degrees about X: at
    // least one joint's palette has moved off the bind pose.
    const std::vector<Mat4> mid = animation_pose(scene, 0, wave, 0.5f);
    REQUIRE(mid.size() == 348);
    CHECK(!std::all_of(mid.begin(), mid.end(),
                       [](const Mat4& m) { return is_identity(m, 1e-3f); }));
}

TEST_CASE("the procedural wave animates every joint of a rig smaller than the cap") {
    // The other half of the declared `min(12, joints)` contract: a rig with
    // fewer joints than the cap gets one channel per joint, not the cap
    // (wander's case above pins the cap with 348 joints).
    const Scene scene = turn_fixture_scene();  // 2 joints
    const AnimationCpu wave = make_procedural_wave(scene, 0);
    REQUIRE(player_rig(scene).skin.joints.size() == 2);
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
    with_clip.rigs[0].animations.push_back(turn_clip(0));
    Playback real;
    build_playback(with_clip, 0, real);
    REQUIRE(real.clips.size() == 1);
    CHECK(real.clips[0].name == "turn");
    CHECK(!real.has_procedural);
    CHECK(real.index == 0);
    CHECK(real.playing);
    CHECK(real.time == 0.0f);

    // An empty-animation scene keeps the procedural wave as its single clip.
    const Scene without_clips = turn_fixture_scene();
    Playback fallback;
    build_playback(without_clips, 0, fallback);
    REQUIRE(fallback.clips.size() == 1);
    CHECK(fallback.clips[0].name == "procedural-wave");
    CHECK(fallback.has_procedural);
    CHECK(fallback.index == 0);
    CHECK(fallback.playing);
}

TEST_CASE("build_playback keeps every real clip in scene order") {
    Scene scene = turn_fixture_scene();
    scene.rigs[0].animations.push_back(turn_clip(0));
    AnimationCpu second = turn_clip(0);
    second.name = "second";
    scene.rigs[0].animations.push_back(std::move(second));

    Playback playback;
    build_playback(scene, 0, playback);
    REQUIRE(playback.clips.size() == 2);
    CHECK(playback.clips[0].name == "turn");
    CHECK(playback.clips[1].name == "second");
    CHECK(!playback.has_procedural);
}

TEST_CASE("build_playback leaves a model with no skin and no clips silent") {
    Scene scene;  // no joints, no clips
    Playback playback;
    build_playback(scene, 0, playback);
    CHECK(playback.clips.empty());
    CHECK(!playback.has_procedural);
    CHECK(playback.index == -1);
    CHECK(!playback.playing);
}

// --- Combat clip selection (S4.1 task 6) -------------------------------------

TEST_CASE("select_mode_clip_index maps every combat mode by name") {
    const Playback demo = playback_of({"player_a_stand", "player_a_walk", "player_a_run",
                                       "player_a_grip_loop", "player_a_grip_move_f1",
                                       "player_a_grip_float_u_hi_attack",
                                       "player_a_damage_m_fly2"});
    REQUIRE(demo.clips.size() == 7);
    const PlayerInput still;

    PlayerCombat combat;
    combat.mode = PlayerMode::Grounded;
    CHECK(select_mode_clip_index(demo, combat, still) == 0);
    combat.mode = PlayerMode::Grabbing;
    CHECK(select_mode_clip_index(demo, combat, still) == 3);
    combat.mode = PlayerMode::Climbing;
    CHECK(select_mode_clip_index(demo, combat, still) == 4);
    combat.mode = PlayerMode::Striking;
    CHECK(select_mode_clip_index(demo, combat, still) == 5);
    combat.mode = PlayerMode::Falling;
    CHECK(select_mode_clip_index(demo, combat, still) == 6);
    combat.mode = PlayerMode::Staggered;
    CHECK(select_mode_clip_index(demo, combat, still) == 6);
}

TEST_CASE("select_mode_clip_index never falls back to the first clip") {
    const Playback locomotion = playback_of({"player_a_stand", "player_a_walk"});
    const PlayerInput still;

    for (const PlayerMode mode : {PlayerMode::Grabbing, PlayerMode::Climbing,
                                  PlayerMode::Striking, PlayerMode::Falling,
                                  PlayerMode::Staggered}) {
        PlayerCombat combat;
        combat.mode = mode;
        CHECK(select_mode_clip_index(locomotion, combat, still) == -1);
    }

    // Grounded still delegates to the locomotion selector, which keeps its own
    // documented fallback chain.
    PlayerCombat grounded;
    CHECK(select_mode_clip_index(locomotion, grounded, still) == 0);

    // No clips at all: nothing to play, in every mode.
    PlayerCombat combat;
    combat.mode = PlayerMode::Grabbing;
    CHECK(select_mode_clip_index(Playback{}, combat, still) == -1);
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
    // player rig's [1, 2) range may contribute to the lift.
    Scene scene = scene_with_player_joints({
        joint("Bone_KAO", -1, {0.0f, 0.0f, 0.0f}, mat4_identity()),
        joint("Bone_root", 0, {0.0f, 0.0f, 0.0f}, mat4_identity()),
    });
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
    scene.rigs[0].meshBegin = 1;
    scene.rigs[0].meshEnd = 2;
    const RigCpu& rig = player_rig(scene);

    // The clip the converter writes for a root series: translation on Bone_root.
    const AnimationCpu root_translation = one_channel_clip("root-motion", /*joint=*/1, /*path=*/0);
    CHECK(clip_has_root_translation(scene, 0, root_translation));
    CHECK(effective_player_lift(scene, root_translation, rig.meshBegin, rig.meshEnd) == 0.0f);

    // A rotation on Bone_root is not the root series; the S3 lift stands the
    // model on the ground: the player meshes' lowest vertex is 0.7596 below.
    const AnimationCpu root_rotation = one_channel_clip("wave", /*joint=*/1, /*path=*/1);
    CHECK_FALSE(clip_has_root_translation(scene, 0, root_rotation));
    CHECK(rig_ground_offset(scene, rig) == doctest::Approx(0.7596f));
    CHECK(effective_player_lift(scene, root_rotation, rig.meshBegin, rig.meshEnd) ==
          doctest::Approx(0.7596f));

    // A translation on another joint, an empty clip, and a channel whose joint
    // is outside the skin carry no root series either.
    CHECK_FALSE(
        clip_has_root_translation(scene, 0, one_channel_clip("other", /*joint=*/0, /*path=*/0)));
    CHECK_FALSE(clip_has_root_translation(scene, 0, AnimationCpu{}));
    CHECK_FALSE(
        clip_has_root_translation(scene, 0, one_channel_clip("stray", /*joint=*/99, /*path=*/0)));
}

TEST_CASE("wander's baked clips carry the root series and skip the S3 lift") {
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    const RigCpu& rig = player_rig(scene);
    REQUIRE(rig.animations.size() == 11);

    // The lift the S3 arena applies to a root-less model: Wander's origin sits
    // 0.7596 m above his feet.
    const float offset = rig_ground_offset(scene, rig);
    CHECK(offset == doctest::Approx(0.7596f).epsilon(1e-4));

    for (const AnimationCpu& clip : rig.animations) {
        CAPTURE(clip.name);
        CHECK(clip_has_root_translation(scene, 0, clip));
        CHECK(effective_player_lift(scene, clip, rig.meshBegin, rig.meshEnd) == 0.0f);
    }

    // A channel-less clip (the rest pose) carries no root series: the lift
    // applies as before.
    const AnimationCpu rest{};
    CHECK_FALSE(clip_has_root_translation(scene, 0, rest));
    CHECK(effective_player_lift(scene, rest, rig.meshBegin, rig.meshEnd) ==
          doctest::Approx(offset));
}

// --- Per-rig palettes, playback and grounding (S4 task 4) --------------------

TEST_CASE("two playbacks from one scene pose their own rigs and keep the palettes apart") {
    Scene scene = scene_with_two_rigs(/*player_joints=*/2, /*entity_joints=*/238);
    scene.rigs[0].animations.push_back(turn_clip_on("player_turn", /*joint=*/0));
    scene.rigs[1].animations.push_back(turn_clip_on("entity_turn", /*joint=*/237));

    // One Playback per rig: each is filled from the rig it was asked for.
    Playback player;
    Playback entity;
    build_playback(scene, 0, player);
    build_playback(scene, 1, entity);
    REQUIRE(player.clips.size() == 1);
    REQUIRE(entity.clips.size() == 1);
    CHECK(player.clips[0].name == "player_turn");
    CHECK(entity.clips[0].name == "entity_turn");
    CHECK(player.index == 0);
    CHECK(entity.index == 0);
    CHECK(player.playing);
    CHECK(entity.playing);
    CHECK_FALSE(player.has_procedural);
    CHECK_FALSE(entity.has_procedural);

    // Each pose is sized by its own rig and moves only its own joints: the
    // player's turn moves palette[0], the entity's moves palette[237].
    const std::vector<Mat4> player_pose = animation_pose(scene, 0, player.clips[0], 1.0f);
    const std::vector<Mat4> entity_pose = animation_pose(scene, 1, entity.clips[0], 1.0f);
    REQUIRE(player_pose.size() == 2);
    REQUIRE(entity_pose.size() == 238);
    CHECK_FALSE(is_identity(player_pose[0], 1e-4f));
    CHECK(is_identity(player_pose[1], 1e-4f));
    CHECK(is_identity(entity_pose[0], 1e-4f));
    CHECK_FALSE(is_identity(entity_pose[237], 1e-4f));

    // The entity's channel targets joint 237 — far outside the player's 2-joint
    // skin. Posing the entity's clip on the player rig leaves it at rest: a
    // channel of another rig is ignored, never an out-of-range read.
    const std::vector<Mat4> player_at_entity_clip =
        animation_pose(scene, 0, entity.clips[0], 1.0f);
    REQUIRE(player_at_entity_clip.size() == 2);
    for (const Mat4& m : player_at_entity_clip) {
        CHECK(is_identity(m, 1e-4f));
    }

    // The same clip on the rig that owns it: joint 237 is off its bind pose.
    const std::vector<Mat4> entity_at_own_clip = animation_pose(scene, 1, entity.clips[0], 0.0f);
    REQUIRE(entity_at_own_clip.size() == 238);
    CHECK(is_identity(entity_at_own_clip[237], 1e-4f));  // key 0 is the rest pose
}

TEST_CASE("every mesh's palette slot is its own rig's palette (a small player cannot hold the entity's indices)") {
    Scene scene = scene_with_two_rigs(/*player_joints=*/2, /*entity_joints=*/238);

    // One mesh per slot: a static instance (rig -1), the player's, and the
    // entity's, whose vertex carries the entity's largest joint index.
    MeshCpu static_mesh;
    static_mesh.vertices = {vertex_on_joint(/*joint=*/0, /*weight=*/0.0f)};
    MeshCpu player_mesh;
    player_mesh.rig = 0;
    player_mesh.vertices = {vertex_on_joint(/*joint=*/1, /*weight=*/1.0f)};
    MeshCpu entity_mesh;
    entity_mesh.rig = 1;
    entity_mesh.vertices = {vertex_on_joint(/*joint=*/237, /*weight=*/1.0f)};
    scene.meshes = {static_mesh, player_mesh, entity_mesh};
    scene.rigs[0].meshBegin = 1;
    scene.rigs[0].meshEnd = 2;
    scene.rigs[1].meshBegin = 2;
    scene.rigs[1].meshEnd = 3;

    // The slot each mesh draws with: its own rig; a static mesh keeps slot 0
    // (its zero weights make the palette read a pass-through).
    CHECK(mesh_palette_slot(scene, scene.meshes[0]) == 0);
    CHECK(mesh_palette_slot(scene, scene.meshes[1]) == 0);
    CHECK(mesh_palette_slot(scene, scene.meshes[2]) == 1);

    // The invariant the per-rig palette buffers hold: the palette a mesh draws
    // with is sized by that mesh's own rig, and every joint index the mesh's
    // vertices carry stays inside it. The old single buffer — sized from the
    // 2-joint player — would let the entity read palette[237] out of bounds.
    const AnimationCpu rest{};  // no channels: the palette is the bind pose
    for (const MeshCpu& mesh : scene.meshes) {
        const std::size_t slot = mesh_palette_slot(scene, mesh);
        REQUIRE(slot < scene.rigs.size());
        const std::size_t palette_size = animation_pose(scene, slot, rest, 0.0f).size();
        CHECK(palette_size == scene.rigs[slot].skin.joints.size());
        for (const Vertex& vertex : mesh.vertices) {
            for (int k = 0; k < 4; ++k) {
                CHECK(vertex.j[k] < palette_size);
            }
        }
    }

    // The decisive numbers: the entity's index does not fit the player's rig.
    CHECK(scene.rigs[0].skin.joints.size() == 2);
    CHECK(scene.meshes[2].vertices[0].j[0] == 237);
}

TEST_CASE("effective_rig_lift reads the requested rig's own clip and mesh range") {
    // The player's rig 0 names its root Bone_root at joint 1; the entity's rig 1
    // has its own Bone_root. The entity's meshes sit far below the player's, so
    // a lift taken from the wrong rig or range is a visibly wrong number.
    Scene scene = scene_with_player_joints({
        joint("Bone_KAO", -1, {0.0f, 0.0f, 0.0f}, mat4_identity()),
        joint("Bone_root", 0, {0.0f, 0.0f, 0.0f}, mat4_identity()),
    });
    RigCpu entity;
    entity.skin.joints = {joint("Bone_root", -1, {0.0f, 0.0f, 0.0f}, mat4_identity())};
    scene.rigs.push_back(std::move(entity));

    MeshCpu entity_mesh;
    Vertex feet{};
    feet.py = -8.739f;  // the colossus' feet, in the entity's local space
    entity_mesh.vertices = {feet};
    MeshCpu player_mesh;
    Vertex player_feet{};
    player_feet.py = -0.7596f;  // wander's origin-to-feet offset
    player_mesh.vertices = {player_feet};
    scene.meshes = {entity_mesh, player_mesh};
    scene.rigs[1].meshBegin = 0;
    scene.rigs[1].meshEnd = 1;
    scene.rigs[0].meshBegin = 1;
    scene.rigs[0].meshEnd = 2;

    const AnimationCpu rootless{};  // no channels: the origin-to-feet lift applies
    const AnimationCpu entity_root =
        one_channel_clip("entity-root", /*joint=*/0, /*path=*/0);
    const AnimationCpu player_root =
        one_channel_clip("player-root", /*joint=*/1, /*path=*/0);
    const AnimationCpu not_the_root =
        one_channel_clip("not-the-root", /*joint=*/0, /*path=*/0);

    // The entity: 0 with its own root series, else its own 8.739 m bind lift.
    CHECK(clip_has_root_translation(scene, 1, entity_root));
    CHECK(effective_rig_lift(scene, 1, entity_root) == 0.0f);
    CHECK(effective_rig_lift(scene, 1, rootless) == doctest::Approx(8.739f));

    // The player: the same rule against the player's joints and mesh range —
    // and the entity's root channel (joint 0 = Bone_KAO there) is not it.
    CHECK(clip_has_root_translation(scene, 0, player_root));
    CHECK_FALSE(clip_has_root_translation(scene, 0, entity_root));
    CHECK(effective_rig_lift(scene, 0, player_root) == 0.0f);
    CHECK(effective_rig_lift(scene, 0, rootless) == doctest::Approx(0.7596f));
    CHECK(effective_rig_lift(scene, 0, not_the_root) == doctest::Approx(0.7596f));

    // The player wrapper is the same rule over the player's joints and the
    // caller's mesh range.
    CHECK(effective_player_lift(scene, rootless, 1, 2) ==
          effective_rig_lift(scene, 0, rootless));
    CHECK(effective_player_lift(scene, player_root, 1, 2) == 0.0f);

    // An index that is no rig of this scene: no joints, no range — 0, and the
    // root query is false, never a crash.
    CHECK(effective_rig_lift(scene, 7, rootless) == 0.0f);
    CHECK_FALSE(clip_has_root_translation(scene, 7, entity_root));
}

TEST_CASE("animation_clamp_time pins a captured time into [0, duration]") {
    CHECK(animation_clamp_time(0.5f, 2.0f) == doctest::Approx(0.5f));
    CHECK(animation_clamp_time(2.0f, 2.0f) == doctest::Approx(2.0f));  // the end frame stays
    CHECK(animation_clamp_time(-1.0f, 2.0f) == 0.0f);                  // before the start
    CHECK(animation_clamp_time(99.0f, 1.6667f) == doctest::Approx(1.6667f));  // past the end
    CHECK(animation_clamp_time(1.0f, 0.0f) == 0.0f);   // an empty clip
    CHECK(animation_clamp_time(1.0f, -3.0f) == 0.0f);  // a malformed duration
    CHECK(animation_clamp_time(std::nanf(""), 2.0f) == 0.0f);  // never NaN out
}

TEST_CASE("a pinned playback samples its clamped time; the loop wraps") {
    // The normal loop: unwrapped seconds wrap into [0, duration).
    Playback loop;
    loop.time = 2.5f;
    CHECK(playback_time(loop, 2.0f) == doctest::Approx(0.5f));
    loop.time = -0.5f;
    CHECK(playback_time(loop, 2.0f) == doctest::Approx(1.5f));

    // --entity-time pins the clock: the clamp holds, so a time past the clip
    // samples its last key instead of wrapping back to the start.
    Playback pinned;
    pinned.pinned = true;
    pinned.time = 99.0f;
    CHECK(playback_time(pinned, 2.0f) == doctest::Approx(2.0f));
    pinned.time = -1.0f;
    CHECK(playback_time(pinned, 2.0f) == 0.0f);
    pinned.time = 0.75f;
    CHECK(playback_time(pinned, 2.0f) == doctest::Approx(0.75f));
    // An empty clip has only t = 0 in either mode.
    CHECK(playback_time(pinned, 0.0f) == 0.0f);
    CHECK(playback_time(loop, 0.0f) == 0.0f);
}

TEST_CASE("apply_rig_lift raises exactly the rig's meshes, in its own space") {
    Scene scene = scene_with_two_rigs(/*player_joints=*/1, /*entity_joints=*/1);

    MeshCpu instance;  // static
    MeshCpu player;    // rig 0
    player.rig = 0;
    MeshCpu entity;    // rig 1: the placement, scaled by 2
    entity.rig = 1;
    entity.model =
        mat4_from_trs(Vec3{382.0f, 28.369f, 370.0f}, Quat{0.0f, 0.0f, 0.0f, 1.0f},
                      Vec3{2.0f, 2.0f, 2.0f});
    scene.meshes = {instance, player, entity};
    scene.rigs[0].meshBegin = 1;
    scene.rigs[0].meshEnd = 2;
    scene.rigs[1].meshBegin = 2;
    scene.rigs[1].meshEnd = 3;

    // The lift is a local-space measurement of the mesh's vertices, so it
    // composes after the placement: a scaled entity scales its own
    // origin-to-feet offset too (y += 2 * 4.5), and the feet stay on the
    // manifest's ground sample.
    apply_rig_lift(scene, 1, 4.5f);
    CHECK(scene.meshes[2].model.m[12] == doctest::Approx(382.0f));
    CHECK(scene.meshes[2].model.m[13] == doctest::Approx(28.369f + 9.0f));
    CHECK(scene.meshes[2].model.m[14] == doctest::Approx(370.0f));

    // Only the lifted rig moves: the player's meshes and the static instance
    // keep their model matrices.
    for (int k = 0; k < 16; ++k) {
        const float identity_element = (k % 5 == 0) ? 1.0f : 0.0f;
        CHECK(scene.meshes[0].model.m[k] == identity_element);
        CHECK(scene.meshes[1].model.m[k] == identity_element);
    }

    // A zero lift leaves the placement alone; an index that is no rig touches
    // nothing at all.
    apply_rig_lift(scene, 1, 0.0f);
    CHECK(scene.meshes[2].model.m[13] == doctest::Approx(28.369f + 9.0f));
    apply_rig_lift(scene, 7, 100.0f);
    CHECK(scene.meshes[2].model.m[13] == doctest::Approx(28.369f + 9.0f));
}

TEST_CASE("build_playback reads the rig it is asked for") {
    // A two-rig scene where only the entity has clips: the player's playback
    // must not pick them up (and keeps its procedural fallback), and the
    // entity's must not fall back to the player's wave.
    Scene scene = scene_with_two_rigs(/*player_joints=*/2, /*entity_joints=*/4);
    scene.rigs[1].animations.push_back(turn_clip_on("entity_turn", /*joint=*/3));

    Playback player;
    Playback entity;
    build_playback(scene, 0, player);
    build_playback(scene, 1, entity);

    REQUIRE(player.clips.size() == 1);
    CHECK(player.clips[0].name == "procedural-wave");
    CHECK(player.has_procedural);
    CHECK(player.clips[0].channels.size() == 2);  // one per player joint, not the entity's

    REQUIRE(entity.clips.size() == 1);
    CHECK(entity.clips[0].name == "entity_turn");
    CHECK_FALSE(entity.has_procedural);
    CHECK(clip_index_named(entity, "entity_turn") == 0);
    CHECK(clip_index_named(entity, "player_a_stand") == -1);

    // A model with no rig at all stays silent in both slots.
    const Scene empty{};
    Playback silent;
    build_playback(empty, 0, silent);
    CHECK(silent.clips.empty());
    CHECK(silent.index == -1);
    CHECK_FALSE(silent.playing);
}

TEST_CASE("an empty-range rig uses identity, not the next rig's placement") {
    // A rigged glb that contributed no geometry keeps an empty mesh range
    // (scene.h). Rig 0 here owns no meshes at all; rig 1 owns mesh 0 and is
    // placed at (10, 2, -3). The empty-range rig's joints must stay in its own
    // (identity) space — without the range check they were carried by the next
    // rig's first mesh — while rig 1's joints take its own placement.
    Scene scene = scene_with_player_joints({
        joint("Bone_MUNE", -1, {0.0f, 1.0f, 0.0f}, translation_matrix({0.0f, -1.0f, 0.0f})),
    });
    RigCpu entity;
    entity.skin.joints = {
        joint("Bone_MUNE", -1, {0.0f, 5.0f, 0.0f}, translation_matrix({0.0f, -5.0f, 0.0f}))};
    scene.rigs.push_back(std::move(entity));

    MeshCpu placed;
    placed.model = translation_matrix({10.0f, 2.0f, -3.0f});
    scene.meshes.push_back(placed);

    REQUIRE(scene.rigs[0].meshBegin == 0);
    REQUIRE(scene.rigs[0].meshEnd == 0);  // the empty range
    REQUIRE(scene.meshes.size() == 1);    // ... but the next rig has a mesh
    scene.rigs[1].meshBegin = 0;
    scene.rigs[1].meshEnd = 1;

    const std::vector<Mat4> empty_palette = animation_pose(scene, 0, AnimationCpu{}, 0.0f);
    REQUIRE(empty_palette.size() == 1);
    Vec3 empty_rig{};
    REQUIRE(posed_joint_world_position(scene, 0, empty_palette, "Bone_MUNE", empty_rig));
    CHECK(empty_rig.x == doctest::Approx(0.0f));
    CHECK(empty_rig.y == doctest::Approx(1.0f));
    CHECK(empty_rig.z == doctest::Approx(0.0f));

    const std::vector<Mat4> placed_palette = animation_pose(scene, 1, AnimationCpu{}, 0.0f);
    REQUIRE(placed_palette.size() == 1);
    Vec3 placed_joint{};
    REQUIRE(posed_joint_world_position(scene, 1, placed_palette, "Bone_MUNE", placed_joint));
    CHECK(placed_joint.x == doctest::Approx(10.0f));
    CHECK(placed_joint.y == doctest::Approx(7.0f));
    CHECK(placed_joint.z == doctest::Approx(-3.0f));
}

// --- The real Valus pose drives the collision proxy (S4 Task 5) ---------------

namespace {

// A flat heightmap of `y` over [minX, maxX] x [minZ, maxZ], one node per metre.
Heightmap flat_ground_over(float min_x, float min_z, float max_x, float max_z, float y) {
    Heightmap heightmap;
    heightmap.cellSize = 1.0f;
    heightmap.width = static_cast<int>(max_x - min_x) + 1;
    heightmap.height = static_cast<int>(max_z - min_z) + 1;
    heightmap.heights.assign(static_cast<std::size_t>(heightmap.width) *
                                 static_cast<std::size_t>(heightmap.height),
                             y);
    heightmap.boundsMin = Vec3{min_x, y, min_z};
    heightmap.boundsMax = Vec3{max_x, y, max_z};
    return heightmap;
}

// Distance from `p` to the capsule's XZ axis segment (its horizontal spine).
float capsule_xz_distance(const Capsule& capsule, Vec3 p) {
    const float dx = capsule.b.x - capsule.a.x;
    const float dz = capsule.b.z - capsule.a.z;
    const float length_squared = dx * dx + dz * dz;
    float t = 0.0f;
    if (length_squared > 0.0f) {
        t = std::clamp(((p.x - capsule.a.x) * dx + (p.z - capsule.a.z) * dz) / length_squared,
                       0.0f, 1.0f);
    }
    const float axis_x = capsule.a.x + dx * t;
    const float axis_z = capsule.a.z + dz * t;
    return std::sqrt((p.x - axis_x) * (p.x - axis_x) + (p.z - axis_z) * (p.z - axis_z));
}

}  // namespace

TEST_CASE("Valus' posed palette drives the capsule proxy and each leg pushes the player out") {
    // The RE-4 builder pick, exactly as the manifest places it (docs/notes/
    // re4-placement.md §8): ground 28.369, yaw -130.76°.
    SceneManifest manifest{};
    ManifestEntity entity;
    entity.placement.glbRelPath =
        "models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb";
    entity.placement.position = Vec3{382.0f, 28.369f, 370.0f};
    entity.placement.rotation = Quat{0.0f, -0.909104f, 0.0f, 0.416570f};
    entity.placement.scale = Vec3{1.0f, 1.0f, 1.0f};
    entity.clip = "minotaur_a_stand";
    manifest.entities.push_back(entity);

    Scene scene = compose_scene(test_library(), manifest,
                                "models/shadow/characters/wander/_cmn/wander.glb");
    REQUIRE(scene.rigs.size() == 2);
    REQUIRE(scene.rigs[1].skin.joints.size() == 238);

    Playback playback;
    build_playback(scene, 1, playback);
    const int index = clip_index_named(playback, "minotaur_a_stand");
    REQUIRE(index >= 0);
    const AnimationCpu& clip = playback.clips[static_cast<std::size_t>(index)];

    // The stand clip carries the root series, so the origin-to-feet lift is 0:
    // the palette alone already stands Valus on the manifest's ground point.
    CHECK(clip_has_root_translation(scene, 1, clip));
    const float lift = effective_rig_lift(scene, 1, clip);
    CHECK(lift == 0.0f);
    apply_rig_lift(scene, 1, lift);

    const std::vector<Mat4> palette = animation_pose(scene, 1, clip, 0.0f);
    REQUIRE(palette.size() == 238);

    const std::vector<Capsule> capsules = entity_collision_capsules(scene, 1, palette);
    REQUIRE(capsules.size() == 8);  // three per leg + two torso segments

    // The collision input *is* the palette's bone world positions: the first
    // capsule is the left thigh, MOMO_L -> SUNE_L, at the placed world point.
    Vec3 momo{};
    Vec3 sune{};
    Vec3 chest{};
    REQUIRE(posed_joint_world_position(scene, 1, palette, "Bone_MOMO_L", momo));
    REQUIRE(posed_joint_world_position(scene, 1, palette, "Bone_SUNE_L", sune));
    REQUIRE(posed_joint_world_position(scene, 1, palette, "Bone_MUNE", chest));
    CHECK(capsules[0].a.x == doctest::Approx(momo.x));
    CHECK(capsules[0].a.y == doctest::Approx(momo.y));
    CHECK(capsules[0].a.z == doctest::Approx(momo.z));
    CHECK(capsules[0].b.x == doctest::Approx(sune.x));
    CHECK(capsules[0].b.y == doctest::Approx(sune.y));
    CHECK(capsules[0].b.z == doctest::Approx(sune.z));
    CHECK(momo.y == doctest::Approx(35.771f).epsilon(0.01));
    CHECK(chest.y == doctest::Approx(39.294f).epsilon(0.01));
    CHECK(chest.x == doctest::Approx(382.409f).epsilon(0.01));
    CHECK(chest.z == doctest::Approx(370.465f).epsilon(0.01));

    // Each leg capsule, resolved alone, pushes a player placed on its axis to
    // the capsule's radius + the player's 0.35 m. The ground height puts the
    // player's 1.7 m body across the capsule's mid-height, so the vertical
    // overlap is real for every segment (thigh, shin, foot).
    for (std::size_t i = 0; i < 6; ++i) {
        const Capsule& capsule = capsules[i];
        const Vec3 mid{(capsule.a.x + capsule.b.x) * 0.5f,
                       (capsule.a.y + capsule.b.y) * 0.5f,
                       (capsule.a.z + capsule.b.z) * 0.5f};
        const float ground_y = mid.y - 0.5f * kPlayerCapsuleHeight;
        const Heightmap ground = flat_ground_over(360.0f, 350.0f, 400.0f, 390.0f, ground_y);
        PlayerState player{mid, 0.0f};
        const std::vector<Capsule> one{capsule};

        resolve_player_collision(player, one, ground);

        CHECK(capsule_xz_distance(capsule, player.position) ==
              doctest::Approx(capsule.r + kPlayerCapsuleRadius).epsilon(1e-3));
        CHECK(player.position.y == doctest::Approx(ground_y));
    }

    // On the arena's real ground, with the full proxy (both legs), a player
    // approaching from the outside and starting just inside a foot's
    // silhouette is pushed clear of that foot and stays on the ground. (A
    // player starting exactly between the two feet can remain between their
    // overlapping exclusion zones: the ruling caps the resolver at two
    // iterations, and no tunneling guarantee is made past the frame step.)
    const Heightmap arena_ground = flat_ground_over(360.0f, 350.0f, 400.0f, 390.0f, 28.369f);
    for (const std::size_t foot : {std::size_t{2}, std::size_t{5}}) {
        const Capsule& capsule = capsules[foot];
        const Vec3 mid{(capsule.a.x + capsule.b.x) * 0.5f, 0.0f,
                       (capsule.a.z + capsule.b.z) * 0.5f};
        // Outward from the body's centre (382, 370): the side a walking player
        // reaches first. 0.5 m is still inside the 1.7 m foot radius.
        const float outward_x = mid.x - 382.0f;
        const float outward_z = mid.z - 370.0f;
        const float outward_length = std::sqrt(outward_x * outward_x + outward_z * outward_z);
        const Vec3 start{mid.x + outward_x / outward_length * 0.5f, 28.369f,
                         mid.z + outward_z / outward_length * 0.5f};
        PlayerState player{start, 0.0f};

        resolve_player_collision(player, capsules, arena_ground);

        CHECK(capsule_xz_distance(capsule, player.position) >=
              capsule.r + kPlayerCapsuleRadius - 1e-3f);
        CHECK(player.position.y == doctest::Approx(28.369f));
    }

    // Known limitation accepted by the controller: with the plan's two
    // iterations, a player *placed* exactly between the two feet remains
    // inside the union of their overlapping exclusion zones (each clearance is
    // 2.05 m, the feet are ~2.7 m apart). Walking cannot reach this point —
    // the zones overlap — so it is a spawn/teleport edge; Task 6 must keep the
    // spawn outside the proxy. A future resolver change that clears this point
    // must fail this test visibly and update it (the resolver algorithm is not
    // to be changed to "fix" it under the current ruling).
    const Capsule& left_foot = capsules[2];
    const Capsule& right_foot = capsules[5];
    const Vec3 left_mid{(left_foot.a.x + left_foot.b.x) * 0.5f, 0.0f,
                        (left_foot.a.z + left_foot.b.z) * 0.5f};
    const Vec3 right_mid{(right_foot.a.x + right_foot.b.x) * 0.5f, 0.0f,
                         (right_foot.a.z + right_foot.b.z) * 0.5f};
    PlayerState between_player{
        Vec3{(left_mid.x + right_mid.x) * 0.5f, 28.369f,
             (left_mid.z + right_mid.z) * 0.5f},
        0.0f};

    resolve_player_collision(between_player, capsules, arena_ground);

    const float distance_to_left = capsule_xz_distance(left_foot, between_player.position);
    const float distance_to_right = capsule_xz_distance(right_foot, between_player.position);
    CHECK(std::min(distance_to_left, distance_to_right) <
          kPlayerCapsuleRadius + left_foot.r - 1e-3f);
    CHECK(between_player.position.y == doctest::Approx(28.369f));
}
