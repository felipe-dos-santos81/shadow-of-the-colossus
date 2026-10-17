#include "camera.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kMinDistance = 0.5f;
constexpr float kMaxDistance = 500.0f;
constexpr float kPitchLimitDeg = 89.0f;
constexpr float kOrbitDegreesPerPixel = 0.4f;  // drag sensitivity
constexpr float kZoomPerWheelStep = 0.15f;     // distance decays by exp(-0.15) per notch
constexpr float kFramePitchDeg = 20.0f;        // camera_frame's framing angle

// Unit vector from the target to the eye for a yaw/pitch pair (degrees).
// Pitch is clamped here as well as in camera_orbit so that a hand-set pitch
// can never make the view direction parallel to the up vector.
Vec3 orbit_offset(float yawDeg, float pitchDeg) {
    const float yaw = yawDeg * kDegToRad;
    const float pitch = std::clamp(pitchDeg, -kPitchLimitDeg, kPitchLimitDeg) * kDegToRad;
    const float cos_pitch = std::cos(pitch);
    return Vec3{cos_pitch * std::sin(yaw), std::sin(pitch), cos_pitch * std::cos(yaw)};
}

}  // namespace

Mat4 camera_view(const Camera& camera) {
    const Vec3 offset = orbit_offset(camera.yaw, camera.pitch);
    const Vec3 eye{camera.target.x + offset.x * camera.distance,
                   camera.target.y + offset.y * camera.distance,
                   camera.target.z + offset.z * camera.distance};
    return mat4_look_at(eye, camera.target, Vec3{0.0f, 1.0f, 0.0f});
}

Mat4 camera_proj(const Camera& camera, float aspect) {
    if (!(aspect > 0.0f)) {
        aspect = 1.0f;  // a zero-size drawable must not produce infinities
    }
    // A hand-set zero distance (the orbit and the fit clamp to [0.5, 500]) must
    // not make the depth range divide by zero.
    const float distance = std::max(camera.distance, 1e-3f);

    // Both planes scale with the orbit distance so the viewer works at any
    // scene scale (never absolute planes). The far plane is deliberately 100 x
    // the distance: the framed model reaches up to `distance + radius` in front
    // of the eye, and a far plane that tracked the distance with a small
    // multiplier cuts a hard depth hole through that far side as soon as the
    // user zooms in (a 2.6 x multiplier clipped from ≈0.36 x the framed
    // distance onward — e.g. the far side of a framed unit cube at distance 0.9
    // sits at 2.63 while 2.6 x 0.9 is only 2.34). 100 x keeps float32 depth
    // precision ample at scene depth (a constant 1:5000 near:far ratio).
    const float near_plane = std::max(0.02f * distance, 1e-3f);
    const float far_plane = 100.0f * distance;
    return mat4_perspective(camera.fovYDeg, aspect, near_plane, far_plane);
}

void camera_orbit(Camera& camera, float dxPixels, float dyPixels) {
    camera.yaw += dxPixels * kOrbitDegreesPerPixel;
    camera.pitch = std::clamp(camera.pitch + dyPixels * kOrbitDegreesPerPixel, -kPitchLimitDeg,
                              kPitchLimitDeg);
}

void camera_zoom(Camera& camera, float wheelDelta) {
    const float scaled = camera.distance * std::exp(-kZoomPerWheelStep * wheelDelta);
    camera.distance = std::clamp(scaled, kMinDistance, kMaxDistance);
}

void camera_frame(Camera& camera, Vec3 boundsMin, Vec3 boundsMax) {
    const Vec3 center{(boundsMin.x + boundsMax.x) * 0.5f, (boundsMin.y + boundsMax.y) * 0.5f,
                      (boundsMin.z + boundsMax.z) * 0.5f};
    const Vec3 diagonal{boundsMax.x - boundsMin.x, boundsMax.y - boundsMin.y,
                        boundsMax.z - boundsMin.z};
    float radius = 0.5f * std::sqrt(vec3_dot(diagonal, diagonal));
    if (!(radius > 0.0f)) {
        radius = 1.0f;  // empty or degenerate bounds: frame something finite
    }

    camera.target = center;
    // The yaw (the viewing direction) is kept: only the fit changes, so
    // pressing F after orbiting reframes without resetting the view.
    camera.distance = std::clamp(radius / std::tan(0.5f * camera.fovYDeg * kDegToRad),
                                 kMinDistance, kMaxDistance);
    camera.pitch = kFramePitchDeg;
}
