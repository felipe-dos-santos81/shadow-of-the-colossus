// S13 Task 6: the additive motion layer (additive recon §1/§3). Pure: synthetic one-joint rigs and
// clips built here, no game data.
#include <doctest.h>

#include "additive.h"
#include "encounter_rt.h"

#include <cmath>
#include <cstring>

namespace {

constexpr float kFps = 30.0f;  // the layer clock (binary FUN_00639520)

Quat about(Vec3 axis, float degrees) {
    const float h = degrees * 3.14159265358979f / 360.0f;
    return Quat{axis.x * std::sin(h), axis.y * std::sin(h), axis.z * std::sin(h), std::cos(h)};
}
Quat about_z(float degrees) { return about({0.0f, 0.0f, 1.0f}, degrees); }

// One joint at rest: T (0, 1, 0), R identity, S 1.
Scene one_joint_scene() {
    JointCpu j;
    j.name = "Bone_root";
    j.parent = -1;
    j.inverseBind = mat4_identity();
    j.restT = {0.0f, 1.0f, 0.0f};
    j.restR = {0.0f, 0.0f, 0.0f, 1.0f};
    j.restS = {1.0f, 1.0f, 1.0f};
    Scene scene;
    RigCpu rig;
    rig.skin.joints.push_back(j);
    scene.rigs.push_back(std::move(rig));
    return scene;
}

// A clip on joint 0: rotation keys about Z (degrees) and root x offsets, at 30 fps from frame 0.
AnimationCpu z_clip(const char* name, std::vector<float> degrees, std::vector<float> rootX) {
    AnimationCpu clip;
    clip.name = name;
    clip.duration = static_cast<float>(degrees.size() - 1) / kFps;
    AnimationChannelCpu r{0, 1, 0, {}, {}};
    AnimationChannelCpu t{0, 0, 0, {}, {}};
    for (std::size_t k = 0; k < degrees.size(); ++k) {
        const Quat q = about_z(degrees[k]);
        r.times.push_back(static_cast<float>(k) / kFps);
        r.values.insert(r.values.end(), {q.x, q.y, q.z, q.w});
        t.times.push_back(static_cast<float>(k) / kFps);
        t.values.insert(t.values.end(), {rootX[k], 1.0f, 0.0f});
    }
    clip.channels = {r, t};
    return clip;
}

std::vector<float> seconds_of(const std::vector<AnimationCpu>& clips) {
    std::vector<float> out;
    for (const AnimationCpu& c : clips) out.push_back(c.duration);
    return out;
}

LocalPose pose_of(Quat r, Vec3 t, Vec3 s = {1.0f, 1.0f, 1.0f}) {
    LocalPose p;
    p.r = {r};
    p.t = {t};
    p.s = {s};
    return p;
}

bool bit_equal(const LocalPose& a, const LocalPose& b) {
    return a.t.size() == b.t.size() && a.r.size() == b.r.size() && a.s.size() == b.s.size() &&
           std::memcmp(a.t.data(), b.t.data(), a.t.size() * sizeof(Vec3)) == 0 &&
           std::memcmp(a.r.data(), b.r.data(), a.r.size() * sizeof(Quat)) == 0 &&
           std::memcmp(a.s.data(), b.s.data(), a.s.size() * sizeof(Vec3)) == 0;
}

void check_quat(Quat got, Quat want) {
    const float sign = (got.x * want.x + got.y * want.y + got.z * want.z + got.w * want.w) < 0 ? -1.0f : 1.0f;
    CHECK(got.x * sign == doctest::Approx(want.x).epsilon(1e-5));
    CHECK(got.y * sign == doctest::Approx(want.y).epsilon(1e-5));
    CHECK(got.z * sign == doctest::Approx(want.z).epsilon(1e-5));
    CHECK(got.w * sign == doctest::Approx(want.w).epsilon(1e-5));
}

AddEdge always(const char* orient, int to, float p30, float p34) {
    return AddEdge{orient, -1, to, -1, p30, p34, false};
}
AddEdge from(const char* orient, int f, int to, float p30, float p34) {
    return AddEdge{orient, f, to, -1, p30, p34, false};
}

int slot_of(const AddLayers& layers, int clip) {
    for (std::size_t i = 0; i < layers.slot.size(); ++i) {
        if (layers.slot[i].state != AddLayer::Free && layers.slot[i].clip == clip) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

TEST_CASE("additive_apply: q = slerp(I, cur*ref^-1, w)*base, t += w*(cur_t - ref_t), scale untouched") {
    const Quat baseR = about({1.0f, 0.0f, 0.0f}, 90.0f);  // does not commute with a Z delta
    LocalPose base = pose_of(baseR, {0.5f, 2.0f, -1.0f}, {2.0f, 2.0f, 2.0f});
    const LocalPose ref = pose_of(about_z(10.0f), {0.0f, 1.0f, 0.0f}, {5.0f, 5.0f, 5.0f});
    const LocalPose cur = pose_of(about_z(40.0f), {0.3f, 1.0f, 0.2f}, {7.0f, 7.0f, 7.0f});

    LocalPose full = base;
    additive_apply(full, cur, ref, 1.0f);
    check_quat(full.r[0], quat_mul(about_z(30.0f), baseR));  // the delta goes on the left
    CHECK(full.t[0].x == doctest::Approx(0.8f));
    CHECK(full.t[0].y == doctest::Approx(2.0f));
    CHECK(full.t[0].z == doctest::Approx(-0.8f));
    CHECK(full.s[0].x == 2.0f);

    LocalPose half = base;
    additive_apply(half, cur, ref, 0.5f);
    check_quat(half.r[0], quat_mul(about_z(15.0f), baseR));
    CHECK(half.t[0].x == doctest::Approx(0.65f));
}

TEST_CASE("a flinch whose first and last frames equal the base returns the base exactly at its end") {
    const Scene scene = one_joint_scene();
    std::vector<AnimationCpu> clips = {z_clip("flinch", {10, 20, 30, 40, 50, 60, 50, 40, 30, 20, 10},
                                              {0, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f, 0})};
    const std::vector<AddEdge> edges = {always("Head_damage", 0, 5.0f, 10.0f)};
    const LocalPose base = pose_of(about({1.0f, 0.0f, 0.0f}, 25.0f), {0.5f, 2.0f, 0.25f});

    AddLayers layers;
    additive_request(layers, edges, "Head_damage");
    REQUIRE(additive_playing(layers, 0));
    CHECK(layers.slot[0].weight == 1.0f);  // a fresh Always start is full weight

    additive_step(layers, seconds_of(clips), 5.0f / kFps);  // frame 5: the peak, 50 deg over f0
    LocalPose mid = base;
    additive_compose(mid, scene, 0, clips, layers);
    check_quat(mid.r[0], quat_mul(about_z(50.0f), base.r[0]));
    CHECK(mid.t[0].x == doctest::Approx(1.0f));

    additive_step(layers, seconds_of(clips), 6.0f / kFps);  // past the end: clamped, Ended
    CHECK(layers.slot[0].state == AddLayer::Ended);
    CHECK(layers.slot[0].frame == doctest::Approx(10.0f));
    LocalPose end = base;
    additive_compose(end, scene, 0, clips, layers);
    CHECK(bit_equal(end, base));

    additive_step(layers, seconds_of(clips), 1.0f / kFps);  // the end cuts (blend-out 0)
    CHECK(layers.slot[0].state == AddLayer::Free);
    CHECK_FALSE(additive_playing(layers, 0));
}

TEST_CASE("a 1-frame loop with ref = its _start holds the start's end delta") {
    const Scene scene = one_joint_scene();
    std::vector<AnimationCpu> clips = {z_clip("neck_down_2_start", {0, 10, 20, 30}, {0, 0, 0, 0}),
                                       z_clip("neck_down_2_loop", {30}, {0})};
    const std::vector<AddEdge> edges = {
        AddEdge{"Neck_down_2", -1, 0, -1, 0.0f, 0.0f, true},
        AddEdge{"Neck_down_2", 0, 1, 0, -1.0f, 0.0f, true},  // start ended -> loop (ref: start)
        AddEdge{"Neck_down_2", 1, 1, 0, -1.0f, 0.0f, true},  // loop ended -> loop (Continue)
    };
    const LocalPose base = pose_of({0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f});
    const std::vector<float> secs = seconds_of(clips);

    AddLayers layers;
    additive_request(layers, edges, "Neck_down_2");
    REQUIRE(layers.slot[0].clip == 0);
    additive_request(layers, edges, "Neck_down_2");  // start not ended: blocked, nothing changes
    CHECK(layers.slot[0].clip == 0);
    additive_step(layers, secs, 4.0f / kFps);
    REQUIRE(layers.slot[0].state == AddLayer::Ended);
    additive_request(layers, edges, "Neck_down_2");  // Shift into the loop, same slot
    REQUIRE(layers.slot[0].clip == 1);
    CHECK(layers.slot[0].ref == 0);

    for (int frame = 0; frame < 3; ++frame) {
        LocalPose pose = base;
        additive_compose(pose, scene, 0, clips, layers);
        check_quat(pose.r[0], about_z(30.0f));  // loop f0 (30) over start f0 (0)
        additive_step(layers, secs, 1.0f / kFps);
        CHECK(layers.slot[0].state == AddLayer::Ended);  // a 0 s clip ends at once
        additive_request(layers, edges, "Neck_down_2");   // Continue: restart in place
        CHECK(layers.slot[0].state == AddLayer::Playing);
        CHECK(layers.slot[0].clip == 1);
    }

    // The reference rule: with no ref the loop is its own reference -> no delta.
    layers.slot[0].ref = -1;
    LocalPose own = base;
    additive_compose(own, scene, 0, clips, layers);
    CHECK(bit_equal(own, base));
}

TEST_CASE("Shift reuses the slot; a gated from-row blocks the Always rows") {
    std::vector<AnimationCpu> clips = {z_clip("A", {0, 5, 0, 5, 0}, {0, 0, 0, 0, 0}),
                                       z_clip("B", {0, 5, 0, 5, 0}, {0, 0, 0, 0, 0}),
                                       z_clip("C", {0, 5, 0, 5, 0}, {0, 0, 0, 0, 0}),
                                       z_clip("D", {0, 5, 0, 5, 0}, {0, 0, 0, 0, 0})};
    const std::vector<AddEdge> edges = {always("goC", 2, 5.0f, 10.0f), always("goA", 0, 5.0f, 10.0f),
                                        from("Y", 0, 1, 1.0f, 10.0f), always("Y", 3, 5.0f, 10.0f)};
    const std::vector<float> secs = seconds_of(clips);
    AddLayers layers;
    additive_request(layers, edges, "goC");
    additive_request(layers, edges, "goA");
    REQUIRE(slot_of(layers, 2) == 0);
    REQUIRE(slot_of(layers, 0) == 1);

    additive_request(layers, edges, "Y");  // A at frame 0, p30 1: gated, and D is not started
    CHECK(slot_of(layers, 0) == 1);
    CHECK_FALSE(additive_playing(layers, 3));

    additive_step(layers, secs, 2.0f / kFps);
    additive_request(layers, edges, "Y");  // A at frame 2 > 1: Shift into B in A's slot
    CHECK(slot_of(layers, 1) == 1);
    CHECK_FALSE(additive_playing(layers, 0));
    CHECK_FALSE(additive_playing(layers, 3));  // a from-row applied: no Always row
    CHECK(slot_of(layers, 2) == 0);
    CHECK(layers.slot[1].frame == 0.0f);
    CHECK(layers.slot[1].blendFrames == 10.0f);  // p34 10: ramp in
    CHECK(layers.slot[1].weight == 0.0f);
    CHECK(layers.slot[2].state == AddLayer::Free);
    additive_step(layers, secs, 2.0f / kFps);
    CHECK(layers.slot[1].weight == doctest::Approx(0.2f));
}

TEST_CASE("Always-Cancel: a re-hit at frame 3 (p30 5) is ignored, at frame 7 restarts with a 10-frame ramp") {
    std::vector<AnimationCpu> clips = {z_clip("flinch", std::vector<float>(31, 0.0f), std::vector<float>(31, 0.0f)),
                                       z_clip("search", std::vector<float>(31, 0.0f), std::vector<float>(31, 0.0f))};
    const std::vector<AddEdge> edges = {always("Head_damage", 0, 5.0f, 10.0f), always("search", 1, -1.0f, 0.0f)};
    const std::vector<float> secs = seconds_of(clips);
    AddLayers layers;
    additive_request(layers, edges, "Head_damage");
    additive_request(layers, edges, "search");

    additive_step(layers, secs, 3.0f / kFps);
    additive_request(layers, edges, "Head_damage");
    CHECK(layers.slot[0].frame == doctest::Approx(3.0f));  // ignored
    CHECK(layers.slot[0].weight == 1.0f);

    additive_step(layers, secs, 2.0f / kFps);  // exactly at p30: not yet (frame must exceed it)
    REQUIRE(layers.slot[0].frame == doctest::Approx(5.0f));
    layers.slot[0].frame = 5.0f;
    additive_request(layers, edges, "Head_damage");
    CHECK(layers.slot[0].frame == 5.0f);

    additive_step(layers, secs, 2.0f / kFps);
    additive_request(layers, edges, "Head_damage");
    CHECK(slot_of(layers, 0) == 0);  // restarted in its own slot
    CHECK(layers.slot[0].frame == 0.0f);
    CHECK(layers.slot[0].blendFrames == 10.0f);
    CHECK(layers.slot[0].weight == 0.0f);
    additive_step(layers, secs, 3.0f / kFps);
    CHECK(layers.slot[0].weight == doctest::Approx(0.3f));
    additive_step(layers, secs, 9.0f / kFps);
    CHECK(layers.slot[0].weight == 1.0f);

    // p30 < 0: never cancel-restarts.
    REQUIRE(layers.slot[1].frame > 10.0f);
    const float before = layers.slot[1].frame;
    additive_request(layers, edges, "search");
    CHECK(layers.slot[1].frame == before);
}

TEST_CASE("a fifth concurrent request is dropped (pool of 4)") {
    std::vector<AnimationCpu> clips;
    std::vector<AddEdge> edges;
    const char* names[5] = {"a", "b", "c", "d", "e"};
    for (int i = 0; i < 5; ++i) {
        clips.push_back(z_clip(names[i], std::vector<float>(3 + i, 0.0f), std::vector<float>(3 + i, 0.0f)));
        edges.push_back(always(names[i], i, 5.0f, 10.0f));
    }
    const std::vector<float> secs = seconds_of(clips);
    AddLayers layers;
    for (const char* n : names) additive_request(layers, edges, n);
    for (int i = 0; i < 4; ++i) CHECK(slot_of(layers, i) == i);
    CHECK_FALSE(additive_playing(layers, 4));  // dropped, not queued

    additive_step(layers, secs, 2.0f / kFps);  // "a" (2 frames) ends
    additive_step(layers, secs, 0.5f / kFps);  // ... and is cut
    REQUIRE(layers.slot[0].state == AddLayer::Free);
    additive_request(layers, edges, "e");
    CHECK(slot_of(layers, 4) == 0);  // the first free slot
}

TEST_CASE("w = 0 and an empty pool leave the pose bit-identical") {
    const LocalPose base = pose_of(about({0.3f, 0.4f, 0.866f}, 33.0f), {-0.0f, 1.5f, 0.25f});
    LocalPose w0 = base;
    additive_apply(w0, pose_of(about_z(40.0f), {1.0f, 2.0f, 3.0f}), pose_of(about_z(-20.0f), {0, 0, 0}), 0.0f);
    CHECK(bit_equal(w0, base));

    const Scene scene = one_joint_scene();
    std::vector<AnimationCpu> clips = {z_clip("walk", {0, 30, 60}, {0, 0.5f, 1.0f})};
    LocalPose empty = base;
    additive_compose(empty, scene, 0, clips, AddLayers{});
    CHECK(bit_equal(empty, base));

    // The plug-in: an empty pool and no pool give blended_palette's palette bit for bit.
    Playback playback;
    playback.clips = clips;
    playback.index = 0;
    RigBlend b1, b2;
    const AddLayers none;
    const std::vector<Mat4> plain = blended_palette(scene, 0, playback, b1, "walk", 0.04f, -1.0f);
    const std::vector<Mat4> layered = blended_palette(scene, 0, playback, b2, "walk", 0.04f, -1.0f, &none);
    REQUIRE(plain.size() == 1);
    CHECK(std::memcmp(plain.data(), layered.data(), sizeof(Mat4)) == 0);
}

TEST_CASE("blended_palette composes the layers and keeps them out of blend.shown") {
    const Scene scene = one_joint_scene();
    std::vector<AnimationCpu> clips = {z_clip("stand", {0, 0}, {0, 0}),
                                       z_clip("flinch", {0, 90, 0}, {0, 0, 0})};
    Playback playback;
    playback.clips = clips;
    playback.index = 0;
    AddLayers layers;
    additive_request(layers, {always("hit", 1, 5.0f, 10.0f)}, "hit");
    additive_step(layers, seconds_of(clips), 1.0f / kFps);
    RigBlend blend;
    const std::vector<Mat4> palette = blended_palette(scene, 0, playback, blend, "stand", 0.0f, -1.0f, &layers);
    REQUIRE(palette.size() == 1);
    // The joint turned 90 deg about Z: its local +X lands on +Y (column 0 of the rotation).
    CHECK(palette[0].m[0] == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(palette[0].m[1] == doctest::Approx(1.0f).epsilon(1e-5));
    check_quat(blend.shown.r[0], {0.0f, 0.0f, 0.0f, 1.0f});
}
