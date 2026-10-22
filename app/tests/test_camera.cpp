// Camera and matrix-math tests: the pinned camera behaviors from the S1 plan
// (the look-at convention, bounds fitting, zoom/orbit clamps) plus the rules
// the shared mat4_* helpers must follow (R3). Float expectations use doctest's
// Approx unless the value is structurally exact.
#include <doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "app.h"
#include "camera.h"
#include "heightmap.h"
#include "math3d.h"
#include "player.h"

namespace {

// matrix * (point, 1) as a clip-space (x, y, z, w).
std::array<float, 4> clip_space(const Mat4& matrix, Vec3 point) {
    return {matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12],
            matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13],
            matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14],
            matrix.m[3] * point.x + matrix.m[7] * point.y + matrix.m[11] * point.z + matrix.m[15]};
}

}  // namespace

TEST_CASE("look-at with eye at +Z maps the origin to -Z") {
    const Mat4 view =
        mat4_look_at(Vec3{0.0f, 0.0f, 5.0f}, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    CHECK(view.m[14] == doctest::Approx(-5.0f));
    CHECK(view.m[12] == doctest::Approx(0.0f));
    CHECK(view.m[13] == doctest::Approx(0.0f));
    CHECK(view.m[15] == 1.0f);
}

TEST_CASE("mat4_mul applies its right operand first") {
    Mat4 scale = mat4_identity();
    scale.m[0] = scale.m[5] = scale.m[10] = 2.0f;
    Mat4 translate = mat4_identity();
    translate.m[12] = 1.0f;
    translate.m[13] = 2.0f;
    translate.m[14] = 3.0f;

    // (scale * translate) * p == 2 * (p + t): the translation is scaled too.
    const Mat4 product = mat4_mul(scale, translate);
    CHECK(product.m[12] == doctest::Approx(2.0f));
    CHECK(product.m[13] == doctest::Approx(4.0f));
    CHECK(product.m[14] == doctest::Approx(6.0f));
}

TEST_CASE("perspective maps its depth range to [0, 1]") {
    const Mat4 proj = mat4_perspective(60.0f, 1.0f, 0.1f, 100.0f);
    CHECK(proj.m[11] == -1.0f);  // w_clip = -z_view: only points in front survive
    const auto ndc_z = [&proj](float z_view) {
        return (proj.m[10] * z_view + proj.m[14]) / (proj.m[11] * z_view);
    };
    CHECK(ndc_z(-0.1f) == doctest::Approx(0.0f));    // the near plane
    CHECK(ndc_z(-100.0f) == doctest::Approx(1.0f));  // the far plane
}

TEST_CASE("camera_proj divides the focal length by the aspect") {
    Camera camera{};
    camera.distance = 5.0f;
    const Mat4 square = camera_proj(camera, 1.0f);
    const Mat4 wide = camera_proj(camera, 2.0f);
    CHECK(square.m[0] == doctest::Approx(square.m[5]));
    CHECK(wide.m[0] == doctest::Approx(wide.m[5] / 2.0f));
    CHECK(square.m[5] > 0.0f);
}

TEST_CASE("camera_frame fits a unit cube") {
    Camera camera{};
    camera_frame(camera, Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f});
    CHECK(camera.target.x == 0.0f);
    CHECK(camera.target.y == 0.0f);
    CHECK(camera.target.z == 0.0f);
    CHECK(camera.distance > 2.5f);
    CHECK(camera.distance < 3.5f);  // sqrt(3) / tan(30°) ≈ 3.0
    CHECK(camera.pitch > 15.0f);    // the plan's framing angle: ~20°
    CHECK(camera.pitch < 25.0f);
    CHECK(camera.fovYDeg == 60.0f);
}

TEST_CASE("camera_frame keeps an off-center box inside the frustum") {
    Camera camera{};
    const Vec3 bounds_min{9.0f, -3.2f, 2.2f};
    const Vec3 bounds_max{11.0f, -0.8f, 3.8f};
    camera_frame(camera, bounds_min, bounds_max);
    CHECK(camera.target.x == doctest::Approx(10.0f));
    CHECK(camera.target.y == doctest::Approx(-2.0f));
    CHECK(camera.target.z == doctest::Approx(3.0f));

    const Mat4 view_proj = mat4_mul(camera_proj(camera, 16.0f / 9.0f), camera_view(camera));
    for (int corner = 0; corner < 8; ++corner) {
        const Vec3 point{(corner & 1) != 0 ? bounds_max.x : bounds_min.x,
                         (corner & 2) != 0 ? bounds_max.y : bounds_min.y,
                         (corner & 4) != 0 ? bounds_max.z : bounds_min.z};
        const std::array<float, 4> clip = clip_space(view_proj, point);
        CHECK(clip[3] > 0.0f);
        CHECK(std::abs(clip[0]) <= clip[3] * 1.001f);
        CHECK(std::abs(clip[1]) <= clip[3] * 1.001f);
        CHECK(clip[2] >= 0.0f);          // Metal's [0, 1] depth range
        CHECK(clip[2] <= clip[3] * 1.001f);
    }
}

TEST_CASE("zooming in keeps the framed scene inside the depth range") {
    // camera_frame derives the distance from the bounds' half-diagonal, so the
    // model lives inside the sphere of that radius r around the target: its far
    // side sits `distance + r` in front of the eye. A far plane that only
    // tracks the distance shrinks along with the zoom and cuts a hard depth
    // hole through the far side — with the old 2.6 x distance, this fixture's
    // far side (0.9 + sqrt(3) = 2.63) was past far = 2.34.
    Camera camera{};
    camera_frame(camera, Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f});  // framed: 3.0
    camera.distance = 0.9f;

    const Mat4 proj = camera_proj(camera, 16.0f / 9.0f);
    const auto ndc_z = [&proj](float z_view) {
        return (proj.m[10] * z_view + proj.m[14]) / (proj.m[11] * z_view);
    };

    // The whole bounding sphere — hence every vertex of the model — must be in
    // front of the far plane.
    const float radius = std::sqrt(3.0f);
    CHECK(ndc_z(-(camera.distance + radius)) <= 1.0f);

    // The cube's own corners: the six in front of the eye sit inside the
    // [near, far] range; the two behind the eye are legitimately clipped by the
    // near plane.
    const Mat4 view = camera_view(camera);
    int corners_in_front = 0;
    for (int corner = 0; corner < 8; ++corner) {
        const Vec3 point{(corner & 1) != 0 ? 1.0f : -1.0f, (corner & 2) != 0 ? 1.0f : -1.0f,
                         (corner & 4) != 0 ? 1.0f : -1.0f};
        const float z_view = clip_space(view, point)[2];
        if (z_view >= 0.0f) {
            continue;
        }
        ++corners_in_front;
        CHECK(ndc_z(z_view) >= 0.0f);
        CHECK(ndc_z(z_view) <= 1.0f);
    }
    CHECK(corners_in_front == 6);  // the loop is not silently vacuous
}

TEST_CASE("camera_view maps the target to -distance on the view axis") {
    Camera camera{};
    camera.target = Vec3{3.0f, 1.5f, -2.0f};
    camera.distance = 7.0f;
    camera.yaw = 35.0f;
    camera.pitch = -12.0f;
    const std::array<float, 4> clip = clip_space(camera_view(camera), camera.target);
    CHECK(clip[0] == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(clip[1] == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(clip[2] == doctest::Approx(-camera.distance).epsilon(1e-4));
    CHECK(clip[3] == 1.0f);
}

TEST_CASE("zoom clamps") {
    Camera camera{};
    camera.distance = 3.0f;
    camera_zoom(camera, -100.0f);  // scroll far out
    CHECK(camera.distance == 500.0f);
    camera_zoom(camera, 100.0f);  // scroll far in
    CHECK(camera.distance == 0.5f);
    camera_zoom(camera, 1.0f);  // a step inward from the floor stays on the floor
    CHECK(camera.distance == 0.5f);
}

TEST_CASE("camera_orbit clamps pitch and turns yaw") {
    Camera camera{};
    camera_orbit(camera, 100.0f, 100000.0f);
    CHECK(camera.pitch <= 89.0f);
    CHECK(camera.pitch > 0.0f);
    CHECK(camera.yaw > 0.0f);
    camera_orbit(camera, 0.0f, -1000000.0f);
    CHECK(camera.pitch >= -89.0f);
    CHECK(camera.pitch < 0.0f);
}

// --- Entity target framing (S4 Task 5) --------------------------------------
//
// T is a camera-only mode: the orbit intersects the entity's chest instead of
// trailing the player. The distance clamp is widened to [22, 30] m (the builder
// value for the 17.7 m body), pitch to ±80°, and T-off restores the follow
// offsets while leaving the camera in place so camera_follow eases back.

namespace {

Heightmap flat_ground() {
    Heightmap heightmap;
    heightmap.cellSize = 1.0f;
    heightmap.width = 11;
    heightmap.height = 11;
    heightmap.heights.assign(11 * 11, 0.0f);
    heightmap.boundsMin = Vec3{0.0f, 0.0f, 0.0f};
    heightmap.boundsMax = Vec3{10.0f, 0.0f, 10.0f};
    return heightmap;
}

}  // namespace

TEST_CASE("the T toggle is inert without an entity chest") {
    // The app's guard (app.h): target framing needs a composed scene and a
    // resolved entity chest. The S3 arena manifest has no entity, so T must do
    // nothing rather than park the camera 26 m from the unset zero target.
    Camera camera{};
    camera.yaw = 30.0f;
    camera.distance = 4.5f;
    camera.target = Vec3{1.0f, 2.0f, 3.0f};
    CameraFollow follow;
    CameraTarget target;

    if (entity_target_key_allowed(/*followMode=*/true, /*entityChestResolved=*/false)) {
        camera_target_toggle(camera, follow, target, Vec3{0.0f, 0.0f, 0.0f});
    }
    CHECK_FALSE(target.active);
    CHECK(camera.distance == doctest::Approx(4.5f));
    CHECK(camera.target.x == doctest::Approx(1.0f));
    CHECK(camera.target.y == doctest::Approx(2.0f));
    CHECK(camera.target.z == doctest::Approx(3.0f));

    // A single-model run (no follow mode) is inert too.
    CHECK_FALSE(entity_target_key_allowed(/*followMode=*/false, /*chest=*/true));
    // With both preconditions the key enters the mode.
    CHECK(entity_target_key_allowed(/*followMode=*/true, /*chest=*/true));
    if (entity_target_key_allowed(true, true)) {
        camera_target_toggle(camera, follow, target, Vec3{0.0f, 0.0f, 0.0f});
    }
    CHECK(target.active);
}

TEST_CASE("the target camera's look yaw is the active camera's movement basis") {
    // The orbit stores the eye-offset direction; the movement basis is its
    // opposite. This is the same rule in follow and target mode.
    Camera camera{};
    camera.yaw = 30.0f;
    CHECK(camera_look_yaw(camera) == doctest::Approx(-150.0f));

    CameraFollow follow;
    CameraTarget target;
    camera_target_toggle(camera, follow, target, Vec3{0.0f, 5.0f, 0.0f});
    camera_target_update(camera, target, Vec3{0.0f, 5.0f, 0.0f}, /*dx=*/100.0f, 0.0f, 0.0f);

    // 30 + 40 = 70: the active camera looks along 70 - 180 = -110.
    CHECK(camera.yaw == doctest::Approx(70.0f));
    CHECK(camera_look_yaw(camera) == doctest::Approx(-110.0f));

    // The controller moves along that camera's look direction: forward at
    // yaw -110 is (sin(-110), 0, cos(-110)).
    PlayerState player{Vec3{5.0f, 0.0f, 5.0f}, 0.0f};
    PlayerInput forward;
    forward.forward = true;
    player_update(player, forward, 0.05f, flat_ground(), camera_look_yaw(camera));
    const float expected_x = 5.0f + std::sin(-110.0f * kDegToRad) * 2.2f * 0.05f;
    const float expected_z = 5.0f + std::cos(-110.0f * kDegToRad) * 2.2f * 0.05f;
    CHECK(player.position.x == doctest::Approx(expected_x));
    CHECK(player.position.z == doctest::Approx(expected_z));
}

TEST_CASE("camera_target_toggle enters target framing and restores the follow state") {
    const Vec3 chest{382.4f, 39.3f, 370.5f};
    Camera camera{};
    camera.yaw = 30.0f;
    camera.pitch = 5.0f;
    camera.distance = 4.5f;
    CameraFollow follow;
    follow.yawOffsetDeg = 18.0f;
    follow.pitchOffsetDeg = -6.0f;
    follow.distance = 7.0f;
    CameraTarget target;

    CHECK_FALSE(target.active);
    camera_target_toggle(camera, follow, target, chest);
    CHECK(target.active);
    CHECK(camera.target.x == doctest::Approx(chest.x));
    CHECK(camera.target.y == doctest::Approx(chest.y));
    CHECK(camera.target.z == doctest::Approx(chest.z));
    CHECK(camera.distance == doctest::Approx(26.0f));  // the builder value, in [22, 30]
    CHECK(camera.pitch == doctest::Approx(10.0f));     // within the widened clamp
    CHECK(camera.yaw == doctest::Approx(30.0f));       // the view swings, not spins

    // Drag orbits the chest; pitch is clamped wider than the follow camera's 60.
    camera_target_update(camera, target, chest, /*dx=*/100.0f, /*dy=*/0.0f, 0.0f);
    CHECK(camera.yaw == doctest::Approx(70.0f));
    camera_target_update(camera, target, chest, 0.0f, 10000.0f, 0.0f);
    CHECK(camera.pitch == doctest::Approx(kTargetPitchLimitDeg));
    camera_target_update(camera, target, chest, 0.0f, -10000.0f, 0.0f);
    CHECK(camera.pitch == doctest::Approx(-kTargetPitchLimitDeg));

    // The wheel stays inside the widened [22, 30] range.
    camera_target_update(camera, target, chest, 0.0f, 0.0f, 1000.0f);
    CHECK(camera.distance == doctest::Approx(kTargetDistanceMin));
    camera_target_update(camera, target, chest, 0.0f, 0.0f, -1000.0f);
    CHECK(camera.distance == doctest::Approx(kTargetDistanceMax));

    const float yaw_before = camera.yaw;
    const float pitch_before = camera.pitch;
    const float distance_before = camera.distance;
    camera_target_toggle(camera, follow, target, chest);
    CHECK_FALSE(target.active);
    CHECK(follow.yawOffsetDeg == doctest::Approx(18.0f));
    CHECK(follow.pitchOffsetDeg == doctest::Approx(-6.0f));
    CHECK(follow.distance == doctest::Approx(7.0f));
    // No snap: the camera is left where the target orbit had it; the next
    // camera_follow call eases toward the restored follow state.
    CHECK(camera.yaw == doctest::Approx(yaw_before));
    CHECK(camera.pitch == doctest::Approx(pitch_before));
    CHECK(camera.distance == doctest::Approx(distance_before));
}

TEST_CASE("T-off eases the target back to the player instead of snapping") {
    const Vec3 chest{382.4f, 39.3f, 370.5f};
    const Vec3 player{10.0f, 0.0f, 10.0f};
    const Vec3 head{player.x, player.y + 1.4f, player.z};
    const auto distance = [](Vec3 a, Vec3 b) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };

    Camera camera{};
    camera.yaw = 30.0f;
    camera.pitch = 5.0f;
    camera.distance = 4.5f;
    CameraFollow follow;
    CameraTarget target;
    camera_target_toggle(camera, follow, target, chest);
    camera_target_toggle(camera, follow, target, chest);
    CHECK_FALSE(target.active);
    CHECK(target.returning);

    const float start = distance(camera.target, head);
    CHECK(start > 20.0f);  // the entity's chest is far from the player
    bool returning =
        camera_target_return(camera, follow, target, player, 0.0f, 0.0f, 0.0f, 0.0f,
                             /*moving=*/false, 1.0f / 60.0f);
    CHECK(returning);
    const float after_one_frame = distance(camera.target, head);
    CHECK(after_one_frame < start);
    CHECK(after_one_frame > 0.5f * start);  // no one-frame jump

    for (int frame = 0; frame < 180 && returning; ++frame) {
        returning = camera_target_return(camera, follow, target, player, 0.0f, 0.0f, 0.0f, 0.0f,
                                         /*moving=*/false, 1.0f / 60.0f);
    }
    CHECK_FALSE(returning);
    CHECK(camera.target.x == doctest::Approx(head.x));
    CHECK(camera.target.y == doctest::Approx(head.y));
    CHECK(camera.target.z == doctest::Approx(head.z));
    CHECK(camera.distance == doctest::Approx(follow.distance));
}

TEST_CASE("target framing fits a 17.7 m subject") {
    const Vec3 chest{382.4f, 39.3f, 370.5f};
    Camera camera{};
    camera.yaw = 0.0f;
    CameraFollow follow;
    CameraTarget target;
    camera_target_toggle(camera, follow, target, chest);

    // The vertical half-FOV at 26 m reaches 26 * tan(30°) = 15.0 m. The real
    // Valus body at t=0 spans y 28.297..45.09 world with its chest at 39.294:
    // -11.00 / +5.80 m around the orbit point, inside that 15 m half-height.
    const float body_bottom = -11.0f;
    const float body_top = 5.8f;
    CHECK(camera.distance * std::tan(0.5f * camera.fovYDeg * kDegToRad) >
          std::max(-body_bottom, body_top));

    // The real check: the body's bottom and top (chest-relative offsets taken
    // from the posed Valus bounds) project inside the frustum — in front,
    // within NDC x/y, and inside [0, 1] depth.
    const Mat4 view_proj = mat4_mul(camera_proj(camera, 16.0f / 9.0f), camera_view(camera));
    for (const float dy : {body_bottom, body_top}) {
        const std::array<float, 4> clip =
            clip_space(view_proj, Vec3{chest.x, chest.y + dy, chest.z});
        CHECK(clip[3] > 0.0f);
        CHECK(std::abs(clip[0]) <= clip[3] * 1.001f);
        CHECK(std::abs(clip[1]) <= clip[3] * 1.001f);
        CHECK(clip[2] >= 0.0f);
        CHECK(clip[2] <= clip[3] * 1.001f);
    }
}

// --- Climb camera (S4.1 Task 4) ---------------------------------------------
//
// While the player grips the colossus the orbit is pinned to the attach point
// instead of trailing the player. Grabbing seeds the eye *outward* along the
// surface normal (yaw = atan2(n.x, n.z), pitch 10°, distance 6 m), so the view
// looks back into the body. Drag orbits at the orbit camera's 0.4°/px with
// pitch clamped to ±45°; the wheel zooms multiplicatively inside [5, 8] m.
// Leaving the mode only clears `active` — the orbit never snaps.

TEST_CASE("climb camera seeds outward and clamps distance") {
    Camera cam{};
    CameraClimb climb{};
    camera_climb_enter(cam, climb, Vec3{0, 10, 0}, Vec3{1, 0, 0});
    REQUIRE(climb.active);
    REQUIRE(cam.distance == doctest::Approx(6.0f));
    REQUIRE(cam.target.x == doctest::Approx(0.0f));
    REQUIRE(cam.target.y == doctest::Approx(10.0f));
    for (int i = 0; i < 100; ++i)
        camera_climb_update(cam, climb, cam.target, 0, 0, 10, 1.0f / 60.0f);
    REQUIRE(cam.distance <= 8.0f + 1e-4f);
    REQUIRE(cam.distance >= 5.0f - 1e-4f);
}

TEST_CASE("climb camera seeds the orbit outward along the surface normal") {
    Camera camera{};
    camera.yaw = 123.0f;  // discarded: the grab seed wins
    camera.pitch = -40.0f;
    camera.distance = 26.0f;
    CameraClimb climb{};
    const Vec3 attach{3.0f, 12.5f, -4.0f};

    camera_climb_enter(camera, climb, attach, Vec3{1.0f, 0.0f, 0.0f});
    CHECK(climb.active);
    CHECK(camera.target.x == doctest::Approx(attach.x));
    CHECK(camera.target.y == doctest::Approx(attach.y));
    CHECK(camera.target.z == doctest::Approx(attach.z));
    CHECK(camera.yaw == doctest::Approx(90.0f));  // atan2(1, 0)
    CHECK(camera.pitch == doctest::Approx(10.0f));
    CHECK(camera.distance == doctest::Approx(6.0f));
    CHECK(climb.distance == doctest::Approx(6.0f));
    // At yaw 90 the eye-offset direction is +X: the normal's side, outward.
    CHECK(std::sin(camera.yaw * kDegToRad) == doctest::Approx(1.0f));

    // A +Z normal is the orbit's yaw-0 side; a diagonal normal seeds at 45°.
    camera_climb_enter(camera, climb, attach, Vec3{0.0f, 0.0f, 2.0f});
    CHECK(camera.yaw == doctest::Approx(0.0f));
    camera_climb_enter(camera, climb, attach, Vec3{1.0f, 0.0f, 1.0f});
    CHECK(camera.yaw == doctest::Approx(45.0f));
    // Only the normal's direction matters, not its length.
    camera_climb_enter(camera, climb, attach, Vec3{4.0f, 0.0f, 4.0f});
    CHECK(camera.yaw == doctest::Approx(45.0f));
    // Entering resets the orbit to the 6 m seed even if a climb already zoomed.
    camera.distance = 7.5f;
    camera_climb_enter(camera, climb, attach, Vec3{0.0f, 0.0f, 1.0f});
    CHECK(camera.distance == doctest::Approx(6.0f));
    CHECK(climb.distance == doctest::Approx(6.0f));
}

TEST_CASE("climb camera orbits the attach point at the orbit sensitivity") {
    Camera camera{};
    CameraClimb climb{};
    camera_climb_enter(camera, climb, Vec3{0.0f, 10.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f});

    // The target follows the attach point every frame.
    const Vec3 moved{1.5f, 11.25f, -2.0f};
    camera_climb_update(camera, climb, moved, 100.0f, 0.0f, 0.0f, 1.0f / 60.0f);
    CHECK(camera.target.x == doctest::Approx(moved.x));
    CHECK(camera.target.y == doctest::Approx(moved.y));
    CHECK(camera.target.z == doctest::Approx(moved.z));
    // 100 px at the orbit camera's 0.4°/px.
    CHECK(camera.yaw == doctest::Approx(40.0f));
    CHECK(camera.pitch == doctest::Approx(10.0f));

    camera_climb_update(camera, climb, moved, 0.0f, 10000.0f, 0.0f, 1.0f / 60.0f);
    CHECK(camera.pitch == doctest::Approx(kClimbPitchLimitDeg));
    camera_climb_update(camera, climb, moved, 0.0f, -10000.0f, 0.0f, 1.0f / 60.0f);
    CHECK(camera.pitch == doctest::Approx(-kClimbPitchLimitDeg));
    // A small drag still moves the clamped pitch.
    camera_climb_update(camera, climb, moved, 0.0f, 25.0f, 0.0f, 1.0f / 60.0f);
    CHECK(camera.pitch == doctest::Approx(-45.0f + 25.0f * 0.4f));
}

TEST_CASE("climb camera wheel is multiplicative inside [5, 8] m") {
    Camera camera{};
    CameraClimb climb{};
    camera_climb_enter(camera, climb, Vec3{0.0f, 10.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f});

    // One notch in: 6 * exp(-0.15), the orbit camera's multiplicative zoom.
    camera_climb_update(camera, climb, camera.target, 0.0f, 0.0f, 1.0f, 1.0f / 60.0f);
    CHECK(camera.distance == doctest::Approx(6.0f * std::exp(-0.15f)));
    CHECK(climb.distance == doctest::Approx(camera.distance));
    camera_climb_update(camera, climb, camera.target, 0.0f, 0.0f, -1.0f, 1.0f / 60.0f);
    CHECK(camera.distance == doctest::Approx(6.0f));

    camera_climb_update(camera, climb, camera.target, 0.0f, 0.0f, 1000.0f, 1.0f / 60.0f);
    CHECK(camera.distance == doctest::Approx(kClimbDistanceMin));
    camera_climb_update(camera, climb, camera.target, 0.0f, 0.0f, -1000.0f, 1.0f / 60.0f);
    CHECK(camera.distance == doctest::Approx(kClimbDistanceMax));
}

TEST_CASE("leaving the climb camera never moves the orbit") {
    Camera camera{};
    CameraClimb climb{};
    camera_climb_enter(camera, climb, Vec3{0.0f, 10.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f});
    camera_climb_update(camera, climb, Vec3{0.0f, 10.0f, 0.0f}, 55.0f, -20.0f, 2.0f,
                        1.0f / 60.0f);
    const float yaw = camera.yaw;
    const float pitch = camera.pitch;
    const float distance = camera.distance;
    const Vec3 target = camera.target;

    camera_climb_exit(camera, climb);
    CHECK_FALSE(climb.active);
    CHECK(camera.yaw == doctest::Approx(yaw));
    CHECK(camera.pitch == doctest::Approx(pitch));
    CHECK(camera.distance == doctest::Approx(distance));
    CHECK(camera.target.x == doctest::Approx(target.x));
    CHECK(camera.target.y == doctest::Approx(target.y));
    CHECK(camera.target.z == doctest::Approx(target.z));

    // An update after exit is a no-op too.
    camera_climb_update(camera, climb, Vec3{9.0f, 9.0f, 9.0f}, 100.0f, 100.0f, 10.0f,
                        1.0f / 60.0f);
    CHECK(camera.yaw == doctest::Approx(yaw));
    CHECK(camera.pitch == doctest::Approx(pitch));
    CHECK(camera.distance == doctest::Approx(distance));
    CHECK(camera.target.x == doctest::Approx(target.x));
    CHECK(camera.target.y == doctest::Approx(target.y));
    CHECK(camera.target.z == doctest::Approx(target.z));
}

TEST_CASE("climb camera ignores non-finite input") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    Camera camera{};
    CameraClimb climb{};
    camera_climb_enter(camera, climb, Vec3{0.0f, 10.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f});
    const float yaw = camera.yaw;
    const float pitch = camera.pitch;
    const float distance = camera.distance;

    camera_climb_update(camera, climb, camera.target, nan, inf, nan, 1.0f / 60.0f);
    CHECK(camera.yaw == doctest::Approx(yaw));
    CHECK(camera.pitch == doctest::Approx(pitch));
    CHECK(camera.distance == doctest::Approx(distance));

    // A non-finite frame time is a broken sample: the whole call is ignored.
    camera_climb_update(camera, climb, camera.target, 100.0f, 100.0f, 10.0f, nan);
    CHECK(camera.yaw == doctest::Approx(yaw));
    CHECK(camera.pitch == doctest::Approx(pitch));
    CHECK(camera.distance == doctest::Approx(distance));

    // A non-finite attach point keeps the last finite target.
    const Vec3 last = camera.target;
    camera_climb_update(camera, climb, Vec3{nan, inf, nan}, 0.0f, 0.0f, 0.0f, 1.0f / 60.0f);
    CHECK(camera.target.x == doctest::Approx(last.x));
    CHECK(camera.target.y == doctest::Approx(last.y));
    CHECK(camera.target.z == doctest::Approx(last.z));

    CHECK(std::isfinite(camera.yaw));
    CHECK(std::isfinite(camera.pitch));
    CHECK(std::isfinite(camera.distance));
    CHECK(std::isfinite(camera.target.x));
    CHECK(std::isfinite(camera.target.y));
    CHECK(std::isfinite(camera.target.z));
}

TEST_CASE("climb camera survives a degenerate surface normal") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Camera camera{};
    camera.yaw = 25.0f;
    camera.pitch = 3.0f;
    camera.distance = 7.0f;
    CameraClimb climb{};

    // No direction to seed from: the current yaw is kept, never NaN.
    camera_climb_enter(camera, climb, Vec3{0.0f, 10.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f});
    CHECK(climb.active);
    CHECK(camera.yaw == doctest::Approx(25.0f));
    CHECK(camera.pitch == doctest::Approx(10.0f));
    CHECK(camera.distance == doctest::Approx(6.0f));

    // A non-finite normal is discarded the same way.
    camera_climb_enter(camera, climb, Vec3{0.0f, 10.0f, 0.0f}, Vec3{nan, 0.0f, 0.0f});
    CHECK(camera.yaw == doctest::Approx(25.0f));
    CHECK(std::isfinite(camera.yaw));

    // A non-finite attach point keeps the last target instead of poisoning it.
    const Vec3 last = camera.target;
    camera_climb_enter(camera, climb, Vec3{nan, nan, nan}, Vec3{1.0f, 0.0f, 0.0f});
    CHECK(camera.target.x == doctest::Approx(last.x));
    CHECK(camera.target.y == doctest::Approx(last.y));
    CHECK(camera.target.z == doctest::Approx(last.z));
    CHECK(camera.yaw == doctest::Approx(90.0f));  // the valid normal still seeds
}

// --- Death cinematic (S6 Task 5) ---------------------------------------------
//
// The reconstructed death camera (design §8.3, D2): five key poses on the
// death clock, eased between. T0 is the follow pose the defeat interrupted;
// T0.4 the 12 m pull-back / 4 m up; T3.0 the low angle on the tendrils;
// T6.2 the corpse wide; T8.0 the follow pose again, re-anchored on the
// player's head. `valus` is the entity's placement point (the ground under
// him), so the keys are world-space relative to the Valus transform.

TEST_CASE("camera_death_pose keys the cinematic path") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Vec3 valus{382.0f, 28.369f, 370.0f};
    const Vec3 player{353.0f, 27.951f, 345.0f};
    // A base whose target is NOT the head point: T0 must return it untouched
    // and T8 must re-anchor the follow key on the player's head.
    Camera base{};
    base.target = Vec3{player.x, player.y + 3.0f, player.z};
    base.yaw = 49.0f;
    base.pitch = 6.0f;
    base.distance = 4.5f;

    // T0: the follow pose exactly.
    const Camera t0 = camera_death_pose(base, valus, player, 0.0f);
    CHECK(t0.target.x == doctest::Approx(base.target.x));
    CHECK(t0.target.y == doctest::Approx(base.target.y));
    CHECK(t0.target.z == doctest::Approx(base.target.z));
    CHECK(t0.yaw == doctest::Approx(base.yaw));
    CHECK(t0.pitch == doctest::Approx(base.pitch));
    CHECK(t0.distance == doctest::Approx(base.distance));
    CHECK(t0.fovYDeg == doctest::Approx(base.fovYDeg));

    // T0.4: the pull-back — the eye 12 m out and exactly 4 m above the entity
    // base, tilted up at the body's midpoint so the death is framed, not the
    // legs.
    const Camera pull = camera_death_pose(base, valus, player, kDeathPullSeconds);
    CHECK(pull.target.x == doctest::Approx(valus.x));
    CHECK(pull.target.y == doctest::Approx(valus.y + kDeathPullTargetHeight));
    CHECK(pull.target.z == doctest::Approx(valus.z));
    CHECK(pull.distance == doctest::Approx(kDeathPullDistance));
    CHECK(pull.pitch == doctest::Approx(kDeathPullPitchDeg));
    CHECK(pull.yaw == doctest::Approx(base.yaw));
    const float pull_eye =
        pull.target.y + pull.distance * std::sin(pull.pitch * kDegToRad);
    CHECK(pull_eye == doctest::Approx(valus.y + kDeathPullEyeHeight).epsilon(0.01));

    // T3.0: the low angle — below the tendril band, looking up at it.
    const Camera low = camera_death_pose(base, valus, player, kDeathLowSeconds);
    CHECK(low.target.x == doctest::Approx(valus.x));
    CHECK(low.target.y == doctest::Approx(valus.y + kDeathLowTargetHeight));
    CHECK(low.distance == doctest::Approx(kDeathLowDistance));
    CHECK(low.pitch == doctest::Approx(kDeathLowPitchDeg));
    CHECK(low.pitch < 0.0f);  // the eye below the target: a low angle
    CHECK(low.yaw == doctest::Approx(base.yaw + kDeathLowYawOffsetDeg));
    // The eye is low: target height + distance * sin(pitch) above the base.
    const float low_eye = low.target.y + low.distance * std::sin(low.pitch * kDegToRad);
    CHECK(low_eye > valus.y);
    CHECK(low_eye < valus.y + kDeathLowTargetHeight);

    // T6.2: the corpse wide — the whole fallen body in frame.
    const Camera wide = camera_death_pose(base, valus, player, kDeathWideSeconds);
    CHECK(wide.target.x == doctest::Approx(valus.x));
    CHECK(wide.target.y == doctest::Approx(valus.y + kDeathWideTargetHeight));
    CHECK(wide.distance == doctest::Approx(kDeathWideDistance));
    CHECK(wide.pitch == doctest::Approx(kDeathWidePitchDeg));
    CHECK(wide.yaw == doctest::Approx(base.yaw + kDeathWideYawOffsetDeg));

    // T8.0 and later: the follow pose again, re-anchored on the player's head.
    for (const float t : {kDeathFollowSeconds, 12.0f}) {
        CAPTURE(t);
        const Camera back = camera_death_pose(base, valus, player, t);
        CHECK(back.target.x == doctest::Approx(player.x));
        CHECK(back.target.y == doctest::Approx(player.y + kFollowHeadHeight));
        CHECK(back.target.z == doctest::Approx(player.z));
        CHECK(back.yaw == doctest::Approx(base.yaw));
        CHECK(back.pitch == doctest::Approx(base.pitch));
        CHECK(back.distance == doctest::Approx(base.distance));
    }

    // The segments ease: a mid-segment pose sits strictly between its keys.
    const Camera mid_pull = camera_death_pose(base, valus, player, 0.5f * kDeathPullSeconds);
    CHECK(mid_pull.distance > base.distance);
    CHECK(mid_pull.distance < kDeathPullDistance);
    const Camera mid_low = camera_death_pose(base, valus, player, 4.0f);
    CHECK(mid_low.distance > kDeathLowDistance);
    CHECK(mid_low.distance < kDeathWideDistance);

    // t <= 0 and a non-finite clock/point answer the base pose, never NaN.
    const Camera before = camera_death_pose(base, valus, player, -1.0f);
    CHECK(before.distance == doctest::Approx(base.distance));
    const Camera nan_time = camera_death_pose(base, valus, player, nan);
    CHECK(nan_time.target.y == doctest::Approx(base.target.y));
    const Camera nan_valus = camera_death_pose(base, Vec3{nan, nan, nan}, player, 3.0f);
    CHECK(nan_valus.distance == doctest::Approx(base.distance));
}

// Frustum culling (S7 Task 3). The eye sits at the origin looking down -Z.
namespace {
Mat4 cull_view_proj() {
    return mat4_mul(mat4_perspective(60.0f, 16.0f / 9.0f, 0.1f, 100.0f),
                    mat4_look_at(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -1.0f},
                                 Vec3{0.0f, 1.0f, 0.0f}));
}
Mat4 translation(float x, float y, float z) {
    Mat4 out = mat4_identity();
    out.m[12] = x;
    out.m[13] = y;
    out.m[14] = z;
    return out;
}
}  // namespace

TEST_CASE("test_renderer_cull: a box in front is kept") {
    CHECK_FALSE(aabb_outside_frustum(cull_view_proj(), Vec3{-1, -1, -11}, Vec3{1, 1, -9}));
}

TEST_CASE("test_renderer_cull: a box behind the eye is culled") {
    CHECK(aabb_outside_frustum(cull_view_proj(), Vec3{-1, -1, 9}, Vec3{1, 1, 11}));
}

TEST_CASE("test_renderer_cull: a box straddling the near plane is kept") {
    CHECK_FALSE(aabb_outside_frustum(cull_view_proj(), Vec3{-1, -1, -5}, Vec3{1, 1, 5}));
}

TEST_CASE("test_renderer_cull: a box far to the side or beyond the far plane is culled") {
    CHECK(aabb_outside_frustum(cull_view_proj(), Vec3{199, -1, -11}, Vec3{201, 1, -9}));
    CHECK(aabb_outside_frustum(cull_view_proj(), Vec3{-1, 199, -11}, Vec3{1, 201, -9}));
    CHECK(aabb_outside_frustum(cull_view_proj(), Vec3{-1, -1, -201}, Vec3{1, 1, -199}));
}

TEST_CASE("test_renderer_cull: the model matrix moves the box into view") {
    const Vec3 mn{199, -1, -11};
    const Vec3 mx{201, 1, -9};
    const Mat4 viewProj = cull_view_proj();
    CHECK(aabb_outside_frustum(mat4_mul(viewProj, mat4_identity()), mn, mx));
    CHECK_FALSE(aabb_outside_frustum(mat4_mul(viewProj, translation(-200.0f, 0.0f, 0.0f)), mn, mx));
}
