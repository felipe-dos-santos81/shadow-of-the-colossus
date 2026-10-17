#pragma once

#include <cmath>

// Minimal 3D math for the viewer: column-major matrices (m[col * 4 + row]),
// right-handed, Y-up — the layout the shader's float4x4 consumes, and the
// angles passed to mat4_perspective are in degrees.

struct Vec3 {
    float x, y, z;
};

struct Mat4 {
    float m[16];
};

inline float vec3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Returns the zero vector for zero-length input (never NaN).
inline Vec3 vec3_normalize(Vec3 v) {
    const float length_squared = vec3_dot(v, v);
    if (length_squared <= 0.0f) {
        return Vec3{0.0f, 0.0f, 0.0f};
    }
    const float inv_length = 1.0f / std::sqrt(length_squared);
    return Vec3{v.x * inv_length, v.y * inv_length, v.z * inv_length};
}

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kDegToRad = kPi / 180.0f;

inline Mat4 mat4_identity() {
    Mat4 out{};
    out.m[0] = out.m[5] = out.m[10] = out.m[15] = 1.0f;
    return out;
}

// Column-major product: out[col * 4 + row] = sum_k a.m[k * 4 + row] * b.m[col * 4 + k].
inline Mat4 mat4_mul(const Mat4& a, const Mat4& b) {
    Mat4 out{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            out.m[col * 4 + row] = sum;
        }
    }
    return out;
}

// Right-handed look-at: the camera looks down its own -Z, so the origin lands
// at (0, 0, -distance) when the eye is `distance` from it. Degenerate inputs
// (a zero look direction, or one parallel to `up`) yield zero rows rather than
// NaNs — vec3_normalize returns the zero vector for zero-length input.
inline Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up) {
    const Vec3 forward =
        vec3_normalize(Vec3{center.x - eye.x, center.y - eye.y, center.z - eye.z});
    const Vec3 right = vec3_normalize(vec3_cross(forward, up));
    const Vec3 camera_up = vec3_cross(right, forward);

    Mat4 out{};
    out.m[0] = right.x;
    out.m[1] = camera_up.x;
    out.m[2] = -forward.x;
    out.m[4] = right.y;
    out.m[5] = camera_up.y;
    out.m[6] = -forward.y;
    out.m[8] = right.z;
    out.m[9] = camera_up.z;
    out.m[10] = -forward.z;
    out.m[12] = -vec3_dot(right, eye);
    out.m[13] = -vec3_dot(camera_up, eye);
    out.m[14] = vec3_dot(forward, eye);
    out.m[15] = 1.0f;
    return out;
}

// Right-handed perspective with Metal's [0, 1] depth range: the near plane
// maps to z_ndc = 0 and the far plane to 1, and w_clip = -z_view, so only
// geometry in front of the camera survives.
inline Mat4 mat4_perspective(float fovYDeg, float aspect, float nearPlane, float farPlane) {
    const float focal = 1.0f / std::tan(0.5f * fovYDeg * kDegToRad);
    Mat4 out{};
    out.m[0] = focal / aspect;
    out.m[5] = focal;
    out.m[10] = farPlane / (nearPlane - farPlane);
    out.m[11] = -1.0f;
    out.m[14] = nearPlane * farPlane / (nearPlane - farPlane);
    return out;
}
