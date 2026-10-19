#include "player.h"

#include <algorithm>
#include <cmath>

namespace {

// The S3 plan's pinned movement constants.
constexpr float kWalkSpeed = 2.2f;          // m/s
constexpr float kRunSpeed = 5.0f;           // m/s, while Shift is held
constexpr float kTurnDegPerSecond = 540.0f; // yaw follows the movement direction
constexpr float kMaxStepSeconds = 0.05f;    // a frame spike cannot teleport
constexpr float kBoundsInset = 0.5f;        // walkable area: bounds inset by this

// Wraps an angle into [-180, 180): 350 + 20 reads as 10, never 370.
float wrap_deg(float degrees) {
    float wrapped = std::fmod(degrees + 180.0f, 360.0f);
    if (wrapped < 0.0f) {
        wrapped += 360.0f;
    }
    return wrapped - 180.0f;
}

// Clamps one coordinate into [boundMin + inset, boundMax - inset]. Bounds too
// small for the inset (a degenerate, zero-extent grid) collapse to their
// middle; a NaN coordinate lands on the low edge; a non-finite bound pair
// leaves a finite value alone (never writes NaN).
float clamp_inset(float value, float bound_min, float bound_max) {
    const float lo = bound_min + kBoundsInset;
    const float hi = bound_max - kBoundsInset;
    if (!(hi > lo)) {
        const float middle = (bound_min + bound_max) * 0.5f;
        if (std::isfinite(middle)) {
            return middle;
        }
        return std::isfinite(value) ? value : 0.0f;
    }
    if (!(value >= lo)) {
        return lo;
    }
    return value > hi ? hi : value;
}

}  // namespace

void player_update(PlayerState& state, const PlayerInput& input, float dtSeconds,
                   const Heightmap& heightmap, float cameraYawDeg) {
    const float dt = std::clamp(std::isfinite(dtSeconds) ? dtSeconds : 0.0f, 0.0f, kMaxStepSeconds);

    // The pinned camera-relative basis (see player.h): forward is the camera's
    // look direction, right is forward x +Y.
    const float camera_yaw = std::isfinite(cameraYawDeg) ? cameraYawDeg : 0.0f;
    const float sin_yaw = std::sin(camera_yaw * kDegToRad);
    const float cos_yaw = std::cos(camera_yaw * kDegToRad);
    const float forward_axis = (input.forward ? 1.0f : 0.0f) - (input.back ? 1.0f : 0.0f);
    const float right_axis = (input.right ? 1.0f : 0.0f) - (input.left ? 1.0f : 0.0f);

    Vec3 direction{sin_yaw * forward_axis - cos_yaw * right_axis, 0.0f,
                   cos_yaw * forward_axis + sin_yaw * right_axis};
    const float length_squared = vec3_dot(direction, direction);

    if (length_squared > 0.0f) {
        // Normalize so a diagonal (two keys) does not move faster than one.
        const float inv_length = 1.0f / std::sqrt(length_squared);
        direction.x *= inv_length;
        direction.z *= inv_length;

        const float speed = input.run ? kRunSpeed : kWalkSpeed;
        state.position.x += direction.x * speed * dt;
        state.position.z += direction.z * speed * dt;

        // Turn toward the movement direction at the pinned rate, shortest path.
        const float target = std::atan2(direction.x, direction.z) / kDegToRad;
        const float current = std::isfinite(state.yawDeg) ? state.yawDeg : 0.0f;
        const float step = kTurnDegPerSecond * dt;
        state.yawDeg = wrap_deg(current + std::clamp(wrap_deg(target - current), -step, step));
    }

    // A heightmap with no grid at all (no ground anywhere) clamps nothing and
    // keeps y; one with a grid insets the XZ position and follows the ground.
    const bool has_grid = heightmap.width > 0 && heightmap.height > 0 &&
                          !heightmap.heights.empty();
    if (has_grid) {
        state.position.x =
            clamp_inset(state.position.x, heightmap.boundsMin.x, heightmap.boundsMax.x);
        state.position.z =
            clamp_inset(state.position.z, heightmap.boundsMin.z, heightmap.boundsMax.z);

        const float ground = heightmap_sample(heightmap, state.position);
        if (std::isfinite(ground)) {
            state.position.y = ground;
        } else if (!std::isfinite(state.position.y)) {
            state.position.y = 0.0f;  // a hole under a NaN y: settle somewhere finite
        }
    } else if (!std::isfinite(state.position.y)) {
        state.position.y = 0.0f;
    }

    // The XZ clamp can leave a non-finite coordinate only when the heightmap's
    // bounds are non-finite; the guard above covers y, and clamp_inset covers x
    // and z. Belt and braces for a hand-built grid:
    if (!std::isfinite(state.position.x)) {
        state.position.x = heightmap.boundsMin.x;
    }
    if (!std::isfinite(state.position.z)) {
        state.position.z = heightmap.boundsMin.z;
    }
}

Mat4 player_model_matrix(const PlayerState& state, float modelGroundOffsetY) {
    const float yaw = std::isfinite(state.yawDeg) ? state.yawDeg : 0.0f;
    const float half_angle = 0.5f * yaw * kDegToRad;
    // Rotation about +Y: R_y(yaw) maps +Z to (sin(yaw), 0, cos(yaw)) — the
    // movement direction the yaw itself names.
    const Quat rotation{0.0f, std::sin(half_angle), 0.0f, std::cos(half_angle)};
    const float lift = std::isfinite(modelGroundOffsetY) ? modelGroundOffsetY : 0.0f;
    const Vec3 position{state.position.x, state.position.y + lift, state.position.z};
    return mat4_from_trs(position, rotation, Vec3{1.0f, 1.0f, 1.0f});
}

float player_ground_offset(const Scene& scene, std::size_t playerMeshBegin) {
    float lowest = 0.0f;  // every vertex at or above 0: no lift needed
    for (std::size_t i = playerMeshBegin; i < scene.meshes.size(); ++i) {
        for (const Vertex& vertex : scene.meshes[i].vertices) {
            if (std::isfinite(vertex.py) && vertex.py < lowest) {
                lowest = vertex.py;
            }
        }
    }
    return -lowest;
}
