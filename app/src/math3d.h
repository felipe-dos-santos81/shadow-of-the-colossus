#pragma once

#include <cmath>

// Minimal 3D math for the viewer: column-major matrices (m[col * 4 + row]),
// right-handed, Y-up. The matrix helpers (mat4_*) arrive with the camera.

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
