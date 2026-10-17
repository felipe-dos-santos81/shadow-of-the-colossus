// Camera and matrix-math tests: the pinned camera behaviors from the S1 plan
// (the look-at convention, bounds fitting, zoom/orbit clamps) plus the rules
// the shared mat4_* helpers must follow (R3). Float expectations use doctest's
// Approx unless the value is structurally exact.
#include <doctest.h>

#include <array>
#include <cmath>

#include "camera.h"
#include "math3d.h"

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
