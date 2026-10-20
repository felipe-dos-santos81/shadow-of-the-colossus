// Player controller and follow-camera tests (S3 arena plan, W1.4). Fixture
// heightmaps are built in code, no asset library needed; the composed-scene
// case at the end uses the real library like test_asset_io.cpp.
//
// The movement basis is pinned here, in code: `cameraYawDeg` is the camera's
// look-direction yaw — the direction the camera faces, projected on the
// ground, measured from +Z toward +X (yaw 0 = looking along +Z). Forward is
// (sin(yaw), 0, cos(yaw)), so at camera yaw 0 forward input moves +Z. The
// follow camera's own orbit yaw (the eye-offset direction) is the opposite,
// which is why camera_follow targets `playerYawDeg + yawOffsetDeg + 180`.
#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "asset_io.h"
#include "camera.h"
#include "collision.h"
#include "heightmap.h"
#include "manifest.h"
#include "math3d.h"
#include "player.h"
#include "scene.h"

namespace {

// A flat heightmap at y = 0 over [0, size]^2, one node per metre.
Heightmap flat_heightmap(float size = 10.0f) {
    Heightmap heightmap;
    heightmap.cellSize = 1.0f;
    heightmap.width = static_cast<int>(size) + 1;
    heightmap.height = static_cast<int>(size) + 1;
    heightmap.heights.assign(
        static_cast<std::size_t>(heightmap.width) * static_cast<std::size_t>(heightmap.height),
        0.0f);
    heightmap.boundsMin = Vec3{0.0f, 0.0f, 0.0f};
    heightmap.boundsMax = Vec3{size, 0.0f, size};
    return heightmap;
}

// A ramp heightmap over [0, 10]^2 whose height equals z (node (i, j) holds j),
// so a sample at (x, ., z) reads z exactly.
Heightmap ramp_heightmap() {
    Heightmap heightmap = flat_heightmap();
    for (int j = 0; j < heightmap.height; ++j) {
        for (int i = 0; i < heightmap.width; ++i) {
            heightmap.heights[static_cast<std::size_t>(j) * heightmap.width + i] =
                static_cast<float>(j);
        }
    }
    return heightmap;
}

bool is_finite(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// The highest valid node in a heightmap (NaN nodes are skipped).
float max_height(const Heightmap& heightmap) {
    float highest = -std::numeric_limits<float>::infinity();
    for (const float h : heightmap.heights) {
        if (std::isfinite(h) && h > highest) {
            highest = h;
        }
    }
    return highest;
}

// The view-space z of a point through the view matrix: negative means the
// point is in front of the camera (the view looks down its own -Z).
float view_z(const Mat4& view, Vec3 point) {
    return view.m[2] * point.x + view.m[6] * point.y + view.m[10] * point.z + view.m[14];
}

// The shortest signed difference a - b in degrees, for comparing yaw angles
// that may be stored in any 360-degree representation (-90 == 270).
float angle_delta(float a, float b) {
    float delta = std::fmod(a - b + 180.0f, 360.0f);
    if (delta < 0.0f) {
        delta += 360.0f;
    }
    return delta - 180.0f;
}

// Drives camera_follow to its steady state (no drag, no wheel, no movement).
void converge_camera(Camera& camera, CameraFollow& follow, Vec3 playerPos, float playerYawDeg) {
    for (int i = 0; i < 600; ++i) {
        camera_follow(camera, follow, playerPos, playerYawDeg, 0.0f, 0.0f, 0.0f,
                      /*moving=*/false, 1.0f / 60.0f);
    }
}

// The converted-asset library lives outside the repo, so the environment must
// name it; tests fail with this message rather than skipping silently.
std::filesystem::path test_library() {
    const char* value = std::getenv("SOTC_LIB");
    if (value == nullptr || value[0] == '\0') {
        throw std::runtime_error("Set SOTC_LIB to the converted asset library.");
    }
    std::filesystem::path library = value;
    if (library.is_relative()) {
        const std::filesystem::path repo_root =
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        library = repo_root / library;
    }
    return library;
}

ManifestInstance placed_at(const std::string& glbRelPath, Vec3 position) {
    ManifestInstance instance;
    instance.glbRelPath = glbRelPath;
    instance.position = position;
    instance.rotation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
    instance.scale = Vec3{1.0f, 1.0f, 1.0f};
    return instance;
}

}  // namespace

// --- Movement ---------------------------------------------------------------

TEST_CASE("forward input at camera yaw 0 walks +Z at walk speed") {
    const Heightmap heightmap = flat_heightmap();
    PlayerState state{Vec3{5.0f, 0.0f, 5.0f}, 0.0f};

    PlayerInput input;
    input.forward = true;
    player_update(state, input, 0.05f, heightmap, /*cameraYawDeg=*/0.0f);

    CHECK(state.position.x == doctest::Approx(5.0f));
    CHECK(state.position.z == doctest::Approx(5.0f + 2.2f * 0.05f));
    CHECK(state.position.y == doctest::Approx(0.0f));
    CHECK(state.yawDeg == doctest::Approx(0.0f));
}

TEST_CASE("run speed replaces walk speed") {
    const Heightmap heightmap = flat_heightmap();
    PlayerState state{Vec3{5.0f, 0.0f, 5.0f}, 0.0f};

    PlayerInput input;
    input.forward = true;
    input.run = true;
    player_update(state, input, 0.05f, heightmap, 0.0f);

    CHECK(state.position.z == doctest::Approx(5.0f + 5.0f * 0.05f));
}

TEST_CASE("a long frame advances by at most 5 m/s x 0.05 s") {
    const Heightmap heightmap = flat_heightmap();
    PlayerState state{Vec3{5.0f, 0.0f, 5.0f}, 0.0f};

    PlayerInput input;
    input.forward = true;
    input.run = true;
    player_update(state, input, /*dtSeconds=*/1.0f, heightmap, 0.0f);

    // dt clamps to 0.05 s: 5 m/s x 0.05 s = 0.25 m, not 5 m.
    CHECK(state.position.z == doctest::Approx(5.0f + 0.25f));
}

TEST_CASE("movement is camera-relative") {
    const Heightmap heightmap = flat_heightmap();

    // Camera yaw 90: the camera faces +X, so forward moves +X.
    {
        PlayerState state{Vec3{5.0f, 0.0f, 5.0f}, 0.0f};
        PlayerInput input;
        input.forward = true;
        player_update(state, input, 0.05f, heightmap, 90.0f);
        CHECK(state.position.x == doctest::Approx(5.0f + 2.2f * 0.05f));
        CHECK(state.position.z == doctest::Approx(5.0f));
        CHECK(state.yawDeg == doctest::Approx(27.0f));  // turns toward +X at 540 deg/s
    }

    // At camera yaw 0 the camera faces +Z, so its right is -X: D strafes -X
    // and A strafes +X.
    {
        PlayerState state{Vec3{5.0f, 0.0f, 5.0f}, 0.0f};
        PlayerInput input;
        input.right = true;
        player_update(state, input, 0.05f, heightmap, 0.0f);
        CHECK(state.position.x == doctest::Approx(5.0f - 2.2f * 0.05f));
    }
    {
        PlayerState state{Vec3{5.0f, 0.0f, 5.0f}, 0.0f};
        PlayerInput input;
        input.left = true;
        player_update(state, input, 0.05f, heightmap, 0.0f);
        CHECK(state.position.x == doctest::Approx(5.0f + 2.2f * 0.05f));
    }
    {
        PlayerState state{Vec3{5.0f, 0.0f, 5.0f}, 0.0f};
        PlayerInput input;
        input.back = true;
        player_update(state, input, 0.05f, heightmap, 0.0f);
        CHECK(state.position.z == doctest::Approx(5.0f - 2.2f * 0.05f));
    }
}

TEST_CASE("opposite inputs cancel and no input keeps the position") {
    const Heightmap heightmap = flat_heightmap();
    PlayerState state{Vec3{5.0f, 0.0f, 5.0f}, 42.0f};

    PlayerInput cancel;
    cancel.forward = true;
    cancel.back = true;
    player_update(state, cancel, 0.05f, heightmap, 0.0f);
    CHECK(state.position.x == doctest::Approx(5.0f));
    CHECK(state.position.z == doctest::Approx(5.0f));
    CHECK(state.yawDeg == doctest::Approx(42.0f));  // no direction: no turn

    PlayerInput none;
    player_update(state, none, 0.05f, heightmap, 90.0f);
    CHECK(state.position.z == doctest::Approx(5.0f));
    CHECK(state.yawDeg == doctest::Approx(42.0f));
}

TEST_CASE("yaw turns toward the movement direction at 540 deg/s") {
    const Heightmap heightmap = flat_heightmap();

    // Camera yaw 90 targets yaw 90; one 0.05 s frame turns 27 deg.
    {
        PlayerState state{Vec3{5.0f, 0.0f, 5.0f}, 0.0f};
        PlayerInput input;
        input.forward = true;
        player_update(state, input, 0.05f, heightmap, 90.0f);
        CHECK(state.yawDeg == doctest::Approx(27.0f));
    }

    // The shortest path wins: 350 -> 10 is +20 deg, not -340.
    {
        PlayerState state{Vec3{5.0f, 0.0f, 5.0f}, 350.0f};
        PlayerInput input;
        input.forward = true;
        player_update(state, input, 0.05f, heightmap, 10.0f);
        CHECK(state.yawDeg == doctest::Approx(10.0f));
    }

    // A small dt only turns part of the way.
    {
        PlayerState state{Vec3{5.0f, 0.0f, 5.0f}, 0.0f};
        PlayerInput input;
        input.forward = true;
        player_update(state, input, 1.0f / 60.0f, heightmap, 90.0f);
        CHECK(state.yawDeg == doctest::Approx(540.0f / 60.0f));
    }
}

TEST_CASE("the XZ clamp stops at the bounds inset by 0.5 m") {
    const Heightmap heightmap = flat_heightmap();  // bounds [0, 10]^2

    // Starting 0.4 m from the +Z edge, forward (camera yaw 0) stops at 9.5.
    {
        PlayerState state{Vec3{5.0f, 0.0f, 9.6f}, 0.0f};
        PlayerInput input;
        input.forward = true;
        input.run = true;
        player_update(state, input, 0.05f, heightmap, 0.0f);
        CHECK(state.position.z == doctest::Approx(9.5f));

        player_update(state, input, 0.05f, heightmap, 0.0f);  // and stays there
        CHECK(state.position.z == doctest::Approx(9.5f));
    }

    // The -X edge: strafe right (screen right is -X at camera yaw 0) clamps at
    // x = 0.5, and the corner clamp applies on both axes at once.
    {
        PlayerState state{Vec3{0.4f, 0.0f, 0.4f}, 0.0f};
        PlayerInput input;
        input.right = true;
        input.back = true;
        player_update(state, input, 0.05f, heightmap, 0.0f);
        CHECK(state.position.x == doctest::Approx(0.5f));
        CHECK(state.position.z == doctest::Approx(0.5f));
    }
}

TEST_CASE("position.y follows the heightmap") {
    const Heightmap heightmap = ramp_heightmap();  // height == z

    PlayerState state{Vec3{5.0f, 0.0f, 3.0f}, 0.0f};
    PlayerInput input;
    input.forward = true;
    input.run = true;
    player_update(state, input, 0.05f, heightmap, 0.0f);

    CHECK(state.position.z == doctest::Approx(3.25f));
    CHECK(state.position.y == doctest::Approx(3.25f));  // the ramp's height at z

    // Walking back down the ramp lowers y with it.
    PlayerInput back;
    back.back = true;
    back.run = true;
    player_update(state, back, 0.05f, heightmap, 0.0f);
    CHECK(state.position.y == doctest::Approx(3.0f));
}

TEST_CASE("player_update never produces NaN") {
    const Heightmap heightmap = flat_heightmap();

    // dt = 0: a no-op, and no NaN anywhere.
    {
        PlayerState state{Vec3{5.0f, 1.0f, 5.0f}, 0.0f};
        PlayerInput input;
        input.forward = true;
        input.run = true;
        player_update(state, input, 0.0f, heightmap, 0.0f);
        CHECK(is_finite(state.position));
        CHECK(state.position.z == doctest::Approx(5.0f));
        CHECK(std::isfinite(state.yawDeg));
    }

    // A NaN dt is treated as 0.
    {
        PlayerState state{Vec3{5.0f, 0.0f, 5.0f}, 0.0f};
        PlayerInput input;
        input.forward = true;
        player_update(state, input, std::nanf(""), heightmap, 0.0f);
        CHECK(is_finite(state.position));
        CHECK(state.position.z == doctest::Approx(5.0f));
    }

    // A heightmap with no grid at all (no ground): y is kept, XZ unclamped,
    // and a NaN input position does not propagate.
    {
        const Heightmap none{};
        PlayerState state{Vec3{std::nanf(""), 2.0f, 5.0f}, 0.0f};
        PlayerInput input;
        input.forward = true;
        player_update(state, input, 0.05f, none, 0.0f);
        CHECK(is_finite(state.position));
        CHECK(state.position.y == doctest::Approx(2.0f));
    }
}

// --- Follow camera ----------------------------------------------------------

TEST_CASE("camera_follow keeps its offsets when not moving") {
    Camera camera{};
    CameraFollow follow;
    follow.yawOffsetDeg = 30.0f;
    follow.pitchOffsetDeg = 10.0f;

    camera_follow(camera, follow, Vec3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f,
                  /*moving=*/false, 1.0f);

    CHECK(follow.yawOffsetDeg == doctest::Approx(30.0f));
    CHECK(follow.pitchOffsetDeg == doctest::Approx(10.0f));
}

TEST_CASE("camera_follow decays its offsets when moving") {
    Camera camera{};
    CameraFollow follow;
    follow.yawOffsetDeg = 30.0f;
    follow.pitchOffsetDeg = 10.0f;

    camera_follow(camera, follow, Vec3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f,
                  /*moving=*/true, 1.0f);

    // exp(-1.5 s^-1 x 1 s) = 0.2231.
    CHECK(follow.yawOffsetDeg == doctest::Approx(30.0f * std::exp(-1.5f)));
    CHECK(follow.pitchOffsetDeg == doctest::Approx(10.0f * std::exp(-1.5f)));
}

TEST_CASE("drag changes the offsets by the pixel deltas times the sensitivity") {
    Camera camera{};
    CameraFollow follow;

    camera_follow(camera, follow, Vec3{0.0f, 0.0f, 0.0f}, 0.0f, /*dx=*/100.0f, /*dy=*/-50.0f,
                  0.0f, /*moving=*/false, 1.0f / 60.0f);

    // The orbit camera's own drag sensitivity: 0.4 deg per pixel.
    CHECK(follow.yawOffsetDeg == doctest::Approx(40.0f));
    CHECK(follow.pitchOffsetDeg == doctest::Approx(-20.0f));
}

TEST_CASE("the pitch offset is clamped to +-60 deg") {
    Camera camera{};
    CameraFollow follow;

    camera_follow(camera, follow, Vec3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, /*dy=*/10000.0f, 0.0f,
                  /*moving=*/false, 1.0f / 60.0f);
    CHECK(follow.pitchOffsetDeg == doctest::Approx(60.0f));

    camera_follow(camera, follow, Vec3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, /*dy=*/-10000.0f, 0.0f,
                  /*moving=*/false, 1.0f / 60.0f);
    CHECK(follow.pitchOffsetDeg == doctest::Approx(-60.0f));
}

TEST_CASE("wheel zooms the distance within [2, 12]") {
    Camera camera{};
    CameraFollow follow;
    CHECK(follow.distance == doctest::Approx(4.5f));  // the pinned start

    camera_follow(camera, follow, Vec3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, /*wheel=*/100.0f,
                  /*moving=*/false, 1.0f / 60.0f);
    CHECK(follow.distance == doctest::Approx(2.0f));

    camera_follow(camera, follow, Vec3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, /*wheel=*/-100.0f,
                  /*moving=*/false, 1.0f / 60.0f);
    CHECK(follow.distance == doctest::Approx(12.0f));

    // One notch in from 4.5: multiplicative, like camera_zoom.
    CameraFollow fresh;
    camera_follow(camera, fresh, Vec3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, /*wheel=*/1.0f,
                  /*moving=*/false, 1.0f / 60.0f);
    CHECK(fresh.distance == doctest::Approx(4.5f * std::exp(-0.15f)));
}

TEST_CASE("camera_follow targets the head point and converges behind the player") {
    Camera camera{};
    CameraFollow follow;
    const Vec3 player{1.0f, 2.0f, 3.0f};

    camera_follow(camera, follow, player, 0.0f, 0.0f, 0.0f, 0.0f, /*moving=*/false, 1.0f / 60.0f);
    CHECK(camera.target.x == doctest::Approx(1.0f));
    CHECK(camera.target.y == doctest::Approx(2.0f + 1.4f));
    CHECK(camera.target.z == doctest::Approx(3.0f));

    converge_camera(camera, follow, player, 0.0f);

    // Game-style chase: at player yaw 0 (facing +Z) the camera's orbit yaw
    // converges to 180, so the eye sits behind the player and looks along +Z.
    CHECK(angle_delta(camera.yaw, 180.0f) == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(camera.pitch == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(camera.distance == doctest::Approx(4.5f).epsilon(1e-3));

    const Mat4 view = camera_view(camera);
    CHECK(view_z(view, Vec3{player.x, player.y, player.z + 10.0f}) < 0.0f);  // in front
    CHECK(view_z(view, Vec3{player.x, player.y, player.z - 10.0f}) > 0.0f);  // behind the eye

    // The chase follows the player's yaw: at yaw 90 (facing +X) the eye swings
    // to the -X side.
    converge_camera(camera, follow, player, 90.0f);
    CHECK(angle_delta(camera.yaw, 270.0f) == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(view_z(camera_view(camera), Vec3{player.x + 10.0f, player.y, player.z}) < 0.0f);
    CHECK(view_z(camera_view(camera), Vec3{player.x - 10.0f, player.y, player.z}) > 0.0f);
}

TEST_CASE("camera_follow smooths toward its target and reports the blend") {
    Camera camera{};
    CameraFollow follow;

    camera_follow(camera, follow, Vec3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f,
                  /*moving=*/false, 0.5f);
    CHECK(follow.followBlend == doctest::Approx(1.0f - std::exp(-8.0f * 0.5f)));

    // One 0.5 s step lands the yaw most of the way from 0 to 180, not all of it.
    CHECK(camera.yaw > 100.0f);
    CHECK(camera.yaw < 180.0f);

    // A zero-dt call changes nothing at all.
    const float yaw = camera.yaw;
    const float distance = camera.distance;
    camera_follow(camera, follow, Vec3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f,
                  /*moving=*/true, 0.0f);
    CHECK(camera.yaw == yaw);
    CHECK(camera.distance == distance);
}

TEST_CASE("player_model_matrix places and turns the player's meshes") {
    PlayerState state{Vec3{1.0f, 2.0f, 3.0f}, 0.0f};
    Mat4 model = player_model_matrix(state);
    CHECK(model.m[12] == doctest::Approx(1.0f));
    CHECK(model.m[13] == doctest::Approx(2.0f));
    CHECK(model.m[14] == doctest::Approx(3.0f));

    // yaw 90: the model's +Z (its authored facing) maps to the movement
    // direction (sin 90, 0, cos 90) = +X.
    state.yawDeg = 90.0f;
    model = player_model_matrix(state);
    const Vec3 facing{model.m[8], model.m[9], model.m[10]};  // the +Z column
    CHECK(facing.x == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(facing.y == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(facing.z == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("player_model_matrix lifts the model by its origin-to-feet offset") {
    PlayerState state{Vec3{1.0f, 2.0f, 3.0f}, 0.0f};

    // The default draws the model at the position exactly.
    CHECK(player_model_matrix(state).m[13] == doctest::Approx(2.0f));

    // The lift raises the whole model; x, z and the rotation are untouched.
    const Mat4 lifted = player_model_matrix(state, 0.76f);
    CHECK(lifted.m[13] == doctest::Approx(2.76f));
    CHECK(lifted.m[12] == doctest::Approx(1.0f));
    CHECK(lifted.m[14] == doctest::Approx(3.0f));
    CHECK(lifted.m[0] == doctest::Approx(1.0f));
}

TEST_CASE("rig_ground_offset reads the rig's meshes' lowest vertex") {
    Vertex low{};
    low.py = -5.0f;
    Vertex foot{};
    foot.py = -0.76f;
    Vertex hip{};
    hip.py = 0.2f;

    Scene scene;
    MeshCpu instance;
    instance.vertices = {low};  // an instance far below the player
    MeshCpu player;
    player.vertices = {hip, foot};
    scene.meshes = {instance, player};

    // Only the rig's own meshes count: the instance's -5 does not leak in.
    const RigCpu player_rig_record{/*skin=*/{}, /*animations=*/{}, /*meshBegin=*/1,
                                   /*meshEnd=*/2, /*dynamic=*/true};
    CHECK(rig_ground_offset(scene, player_rig_record) == doctest::Approx(0.76f));
    // Nothing below y = 0, an empty range, and a past-the-end range need no
    // lift.
    const RigCpu empty_rig{/*skin=*/{}, /*animations=*/{}, 2, 2, false};
    CHECK(rig_ground_offset(scene, empty_rig) == doctest::Approx(0.0f));
    const RigCpu past_end{/*skin=*/{}, /*animations=*/{}, 9, 12, false};
    CHECK(rig_ground_offset(scene, past_end) == doctest::Approx(0.0f));
    // The mesh range primitive itself is half-open and clamped to the scene.
    CHECK(mesh_ground_offset(scene, 0, 1) == doctest::Approx(5.0f));
    CHECK(mesh_ground_offset(scene, 2, 9) == doctest::Approx(0.0f));
}

// --- The carried W1.3 fix: the heightmap excludes rigged meshes --------------
//
// S4 replaced the S3 "first meshCount meshes" split with the rig records: the
// heightmap rasterizes `mesh.rig < 0` (static instances) only, so neither the
// player's body nor a placed entity can become the ground.

TEST_CASE("the heightmap holds the static ground, not the player's body") {
    SceneManifest manifest{};
    // A plank at the origin gives the composed scene ground under the player.
    manifest.instances.push_back(
        placed_at("models/objects/props/_cmn/obj_plank_01.glb", Vec3{0.0f, 0.0f, 0.0f}));

    std::size_t player_mesh_begin = 0;
    const Scene scene = compose_scene(test_library(), manifest,
                                      "models/shadow/characters/wander/_cmn/wander.glb",
                                      &player_mesh_begin);

    // The player's meshes are appended after the manifest's one static mesh,
    // and rig 0's range starts there.
    REQUIRE(player_mesh_begin == 1);
    REQUIRE(scene.meshes.size() > player_mesh_begin);
    const RigCpu& rig = player_rig(scene);
    REQUIRE(rig.meshBegin == player_mesh_begin);
    REQUIRE(rig.meshEnd == scene.meshes.size());
    for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
        CHECK(scene.meshes[i].rig == (i < player_mesh_begin ? -1 : 0));
    }

    // A 0.1 m grid so the player's thin body (its XZ footprint is ~1.2 m wide
    // but only ~0.2 m deep, and the model is only 1.26 m tall) would actually
    // contain grid nodes: at 1 m cells the body can fall between nodes
    // entirely, which is exactly the phantom the rig filter removes.
    const Heightmap ground_only = build_heightmap(scene, 0.1f);

    // The highest node is the plank's top, not the player's body (which
    // reaches ~1.26 m above its origin).
    const float max_ground = max_height(ground_only);
    CHECK(std::isfinite(max_ground));
    CHECK(max_ground < 0.2f);  // the plank is a few cm thick

    // The grid keeps the scene's own bounds: the walkable area is unchanged.
    CHECK(ground_only.boundsMin.x == doctest::Approx(scene.boundsMin.x));
    CHECK(ground_only.boundsMax.z == doctest::Approx(scene.boundsMax.z));

    // The ground under the player's spawn is finite and low: the app can stand
    // the player on it without the phantom lift.
    const float ground_at_origin = heightmap_sample(ground_only, Vec3{0.0f, 0.0f, 0.0f});
    CHECK(std::isfinite(ground_at_origin));
    CHECK(ground_at_origin < 0.5f);

    // The player model's own grounding offset is its origin-to-feet lift:
    // wander's lowest vertex sits ~0.76 m below the origin (the origin is at
    // the hips), so the app lifts the meshes by that much to stand on the
    // ground instead of sinking into it.
    const Scene wander =
        load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    CHECK(rig_ground_offset(scene, rig) == doctest::Approx(-wander.boundsMin.y));
    CHECK(rig_ground_offset(scene, rig) > 0.5f);
}

// --- Entity collision (S4 Task 5) ------------------------------------------
//
// The player capsule is pinned: radius 0.35 m, height 1.7 m, feet at
// position.y. Resolution is horizontal only (2 iterations), then the ground
// sample is reapplied. The entity capsules come from a posed skeleton (see
// test_anim.cpp for the real-Valus integration).

namespace {

// A flat heightmap at height `y` over [0, size]^2.
Heightmap flat_heightmap_at(float y, float size = 10.0f) {
    Heightmap heightmap = flat_heightmap(size);
    for (float& height : heightmap.heights) {
        height = y;
    }
    heightmap.boundsMin.y = y;
    heightmap.boundsMax.y = y;
    return heightmap;
}

// The distance from `p` to the capsule's XZ axis segment (its horizontal
// silhouette's spine) — the quantity the resolver pushes to r + 0.35.
float capsule_xz_distance(const Capsule& capsule, Vec3 p) {
    const float dx = capsule.b.x - capsule.a.x;
    const float dz = capsule.b.z - capsule.a.z;
    const float length_squared = dx * dx + dz * dz;
    float t = 0.0f;
    if (length_squared > 0.0f) {
        t = ((p.x - capsule.a.x) * dx + (p.z - capsule.a.z) * dz) / length_squared;
        t = std::clamp(t, 0.0f, 1.0f);
    }
    const float axis_x = capsule.a.x + dx * t;
    const float axis_z = capsule.a.z + dz * t;
    return std::sqrt((p.x - axis_x) * (p.x - axis_x) + (p.z - axis_z) * (p.z - axis_z));
}

}  // namespace

TEST_CASE("a player overlapping a capsule is pushed out along the shortest horizontal axis") {
    const Capsule capsule{{0.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, /*r=*/1.0f};
    PlayerState player{Vec3{0.2f, 0.0f, 0.0f}, 0.0f};

    resolve_player_collision(player, {capsule});

    // The shortest exit from a vertical capsule is its own radius plus the
    // player's: 1.0 + 0.35, along the player's side (+X).
    CHECK(player.position.x == doctest::Approx(1.35f));
    CHECK(player.position.z == doctest::Approx(0.0f));
    CHECK(player.position.y == doctest::Approx(0.0f));  // no vertical change
}

TEST_CASE("a player inside a tilted capsule exits perpendicular to its axis") {
    // The capsule runs along +Z; the player sits exactly on the axis. The
    // shortest exit is the horizontal perpendicular (+X by the pinned choice),
    // not an arbitrary corner.
    const Capsule capsule{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 4.0f}, /*r=*/0.8f};
    PlayerState player{Vec3{0.0f, 0.0f, 2.0f}, 0.0f};

    resolve_player_collision(player, {capsule});

    CHECK(capsule_xz_distance(capsule, player.position) == doctest::Approx(0.8f + 0.35f));
    CHECK(player.position.x > 0.0f);
    CHECK(player.position.z == doctest::Approx(2.0f));
}

TEST_CASE("two overlapping capsules are both resolved") {
    // Two overlapping vertical proxies (their silhouettes overlap): the player
    // starts inside both. Iterating twice must leave it outside each one.
    const Capsule left{{0.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, /*r=*/1.0f};
    const Capsule right{{0.8f, 0.0f, 0.0f}, {0.8f, 2.0f, 0.0f}, /*r=*/1.0f};
    PlayerState player{Vec3{0.4f, 0.0f, 0.0f}, 0.0f};

    resolve_player_collision(player, {left, right});

    CHECK(capsule_xz_distance(left, player.position) >= 1.0f + 0.35f - 1e-4f);
    CHECK(capsule_xz_distance(right, player.position) >= 1.0f + 0.35f - 1e-4f);
}

TEST_CASE("a player far from every capsule is untouched") {
    const Capsule capsule{{5.0f, 0.0f, 5.0f}, {5.0f, 2.0f, 5.0f}, /*r=*/1.0f};
    PlayerState player{Vec3{0.0f, 0.0f, 0.0f}, 12.0f};

    resolve_player_collision(player, {capsule});

    CHECK(player.position.x == doctest::Approx(0.0f));
    CHECK(player.position.y == doctest::Approx(0.0f));
    CHECK(player.position.z == doctest::Approx(0.0f));
    CHECK(player.yawDeg == doctest::Approx(12.0f));
}

TEST_CASE("a capsule above the player's head does not push") {
    // The player capsule reaches y = 1.7; a capsule whose lowest point is far
    // above cannot be entered by walking under it.
    const Capsule high{{0.0f, 10.0f, 0.0f}, {0.0f, 12.0f, 0.0f}, /*r=*/0.5f};
    PlayerState player{Vec3{0.0f, 0.0f, 0.0f}, 0.0f};

    resolve_player_collision(player, {high});

    CHECK(player.position.x == doctest::Approx(0.0f));
    CHECK(player.position.z == doctest::Approx(0.0f));
}

TEST_CASE("the resolved position stays on the heightmap") {
    // The ramp's height equals z, so pushing the player in X must leave y on
    // the ramp's surface at the resolved z.
    const Heightmap ramp = ramp_heightmap();  // height == z
    const Capsule capsule{{5.0f, 0.0f, 4.0f}, {5.0f, 2.0f, 4.0f}, /*r=*/1.0f};
    PlayerState player{Vec3{5.1f, 0.0f, 4.0f}, 0.0f};

    resolve_player_collision(player, {capsule}, ramp);

    CHECK(player.position.x == doctest::Approx(6.35f));
    CHECK(player.position.z == doctest::Approx(4.0f));
    CHECK(player.position.y == doctest::Approx(4.0f));  // reapplied ground sample
}

TEST_CASE("collision keeps the feet on a flat heightmap at any elevation") {
    const Heightmap ground = flat_heightmap_at(/*y=*/3.0f);
    const Capsule capsule{{5.0f, 0.0f, 5.0f}, {5.0f, 2.0f, 5.0f}, /*r=*/1.0f};
    PlayerState player{Vec3{5.0f, 3.0f, 5.0f}, 0.0f};

    resolve_player_collision(player, {capsule}, ground);

    CHECK(player.position.y == doctest::Approx(3.0f));
    CHECK(capsule_xz_distance(capsule, player.position) == doctest::Approx(1.35f));
}
