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

// Quaternion: (x, y, z) vector part, w scalar part — the same layout as a glTF
// rotation accessor. Rotations are right-handed, matching the matrices above.
struct Quat {
    float x, y, z, w;
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

// True when the box [mn, mx] (in the space `mvp` maps from) lies entirely
// outside one clip plane — left/right/bottom/top (|x|, |y| <= w) or near/far
// (0 <= z <= w, Metal's depth range). Conservative: a box outside no single
// plane is kept, and a box straddling the near plane is never culled for it.
inline bool aabb_outside_frustum(const Mat4& mvp, Vec3 mn, Vec3 mx) {
    bool all_outside[6] = {true, true, true, true, true, true};
    for (int corner = 0; corner < 8; ++corner) {
        const float x = (corner & 1) ? mx.x : mn.x;
        const float y = (corner & 2) ? mx.y : mn.y;
        const float z = (corner & 4) ? mx.z : mn.z;
        float c[4];
        for (int row = 0; row < 4; ++row) {
            c[row] = mvp.m[row] * x + mvp.m[4 + row] * y + mvp.m[8 + row] * z + mvp.m[12 + row];
        }
        all_outside[0] = all_outside[0] && c[0] < -c[3];
        all_outside[1] = all_outside[1] && c[0] > c[3];
        all_outside[2] = all_outside[2] && c[1] < -c[3];
        all_outside[3] = all_outside[3] && c[1] > c[3];
        all_outside[4] = all_outside[4] && c[2] < 0.0f;
        all_outside[5] = all_outside[5] && c[2] > c[3];
    }
    for (bool outside : all_outside) {
        if (outside) {
            return true;
        }
    }
    return false;
}

// Returns the identity (0, 0, 0, 1) for zero-length input (never NaN), like
// vec3_normalize returns the zero vector.
inline Quat quat_normalize(Quat q) {
    const float length_squared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (length_squared <= 0.0f) {
        return Quat{0.0f, 0.0f, 0.0f, 1.0f};
    }
    const float inv_length = 1.0f / std::sqrt(length_squared);
    return Quat{q.x * inv_length, q.y * inv_length, q.z * inv_length, q.w * inv_length};
}

// Hamilton product: rotation a applied after b (R(a * b) = R(a) R(b)).
inline Quat quat_mul(Quat a, Quat b) {
    return Quat{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// Shortest-path spherical interpolation; t in [0, 1]. Near-parallel inputs take
// a normalized lerp: the sin(theta) form divides by zero there, and the two
// paths are within float precision of each other.
inline Quat quat_slerp(Quat a, Quat b, float t) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) {
        // Opposite hemispheres describe the same rotation: negate to take the
        // short way around.
        b = Quat{-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }
    if (dot > 0.9995f) {
        return quat_normalize(Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                   a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    }
    const float theta = std::acos(dot);
    const float sin_theta = std::sin(theta);
    const float scale_a = std::sin((1.0f - t) * theta) / sin_theta;
    const float scale_b = std::sin(t * theta) / sin_theta;
    return Quat{a.x * scale_a + b.x * scale_b, a.y * scale_a + b.y * scale_b,
                a.z * scale_a + b.z * scale_b, a.w * scale_a + b.w * scale_b};
}

// Column-major rotation matrix; q is normalized first, so a quaternion that
// drifted off the unit sphere still yields a rotation.
inline Mat4 quat_to_mat4(Quat q) {
    q = quat_normalize(q);
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    Mat4 out{};
    out.m[0] = 1.0f - 2.0f * (yy + zz);
    out.m[1] = 2.0f * (xy + wz);
    out.m[2] = 2.0f * (xz - wy);
    out.m[4] = 2.0f * (xy - wz);
    out.m[5] = 1.0f - 2.0f * (xx + zz);
    out.m[6] = 2.0f * (yz + wx);
    out.m[8] = 2.0f * (xz + wy);
    out.m[9] = 2.0f * (yz - wx);
    out.m[10] = 1.0f - 2.0f * (xx + yy);
    out.m[15] = 1.0f;
    return out;
}

// T * R * S: the rotation's basis columns scaled by s, the translation in
// column 3 — the local transform of a glTF node or skin joint.
inline Mat4 mat4_from_trs(Vec3 t, Quat r, Vec3 s) {
    Mat4 out = quat_to_mat4(r);
    for (int row = 0; row < 3; ++row) {
        out.m[row] *= s.x;
        out.m[4 + row] *= s.y;
        out.m[8 + row] *= s.z;
    }
    out.m[12] = t.x;
    out.m[13] = t.y;
    out.m[14] = t.z;
    return out;
}
