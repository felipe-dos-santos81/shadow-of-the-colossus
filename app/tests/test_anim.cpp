// Quaternion and TRS math for the skeletal animation path (task A1); the
// channel-sampling and pose tests join this file in A2. Float expectations use
// doctest's Approx unless the value is structurally exact.
#include <doctest.h>

#include <cmath>

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
