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

// The follow camera's pinned constants (S3 W1.4).
constexpr float kFollowMinDistance = 2.0f;       // wheel zoom floor (m)
constexpr float kFollowMaxDistance = 12.0f;      // wheel zoom ceiling (m)
constexpr float kFollowPitchLimitDeg = 60.0f;    // drag pitch clamp
constexpr float kFollowOffsetDecay = 1.5f;       // recenter rate while moving (s^-1)
constexpr float kFollowRate = 8.0f;              // smoothing rate toward the targets (s^-1)

// Wraps an angle into (-180, 180]: the shortest-path form of a yaw difference,
// and the representation camera_follow stores.
float wrap_deg(float degrees) {
    float wrapped = std::fmod(degrees, 360.0f);
    if (wrapped <= -180.0f) {
        wrapped += 360.0f;
    } else if (wrapped > 180.0f) {
        wrapped -= 360.0f;
    }
    return wrapped;
}

// Unit vector from the target to the eye for a yaw/pitch pair (degrees).
// Pitch is clamped here as well as in camera_orbit so that a hand-set pitch
// can never make the view direction parallel to the up vector.
Vec3 orbit_offset(float yawDeg, float pitchDeg) {
    const float yaw = yawDeg * kDegToRad;
    const float pitch = std::clamp(pitchDeg, -kPitchLimitDeg, kPitchLimitDeg) * kDegToRad;
    const float cos_pitch = std::cos(pitch);
    return Vec3{cos_pitch * std::sin(yaw), std::sin(pitch), cos_pitch * std::cos(yaw)};
}

bool finite_vec3(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}  // namespace

Vec3 camera_eye(const Camera& camera) {
    const Vec3 offset = orbit_offset(camera.yaw, camera.pitch);
    return Vec3{camera.target.x + offset.x * camera.distance,
                camera.target.y + offset.y * camera.distance,
                camera.target.z + offset.z * camera.distance};
}

Mat4 camera_view(const Camera& camera) {
    return mat4_look_at(camera_eye(camera), camera.target, Vec3{0.0f, 1.0f, 0.0f});
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

float camera_look_yaw(const Camera& camera) {
    const float yaw = std::isfinite(camera.yaw) ? camera.yaw : 0.0f;
    return wrap_deg(yaw - 180.0f);
}

void camera_follow(Camera& camera, CameraFollow& follow, Vec3 playerPos, float playerYawDeg,
                   float dragDxPixels, float dragDyPixels, float wheelDelta, bool moving,
                   float dtSeconds) {
    const float dt = std::max(std::isfinite(dtSeconds) ? dtSeconds : 0.0f, 0.0f);

    // Drag turns the view (the orbit camera's own sensitivity). The pitch
    // offset is clamped; the yaw offset is free to wrap.
    if (std::isfinite(dragDxPixels)) {
        follow.yawOffsetDeg += dragDxPixels * kOrbitDegreesPerPixel;
    }
    if (std::isfinite(dragDyPixels)) {
        follow.pitchOffsetDeg += dragDyPixels * kOrbitDegreesPerPixel;
    }
    follow.pitchOffsetDeg =
        std::clamp(follow.pitchOffsetDeg, -kFollowPitchLimitDeg, kFollowPitchLimitDeg);

    // Moving recenters the camera behind the player: the offsets decay to 0.
    if (moving) {
        const float decay = std::exp(-kFollowOffsetDecay * dt);
        follow.yawOffsetDeg *= decay;
        follow.pitchOffsetDeg *= decay;
    }

    // The wheel zooms multiplicatively and stays inside the pinned range.
    if (std::isfinite(wheelDelta) && wheelDelta != 0.0f) {
        follow.distance = std::clamp(follow.distance * std::exp(-kZoomPerWheelStep * wheelDelta),
                                     kFollowMinDistance, kFollowMaxDistance);
    }
    follow.distance = std::clamp(follow.distance, kFollowMinDistance, kFollowMaxDistance);

    // The head point is the orbit target, set exactly (no smoothing on it).
    if (std::isfinite(playerPos.x) && std::isfinite(playerPos.y) && std::isfinite(playerPos.z)) {
        camera.target = Vec3{playerPos.x, playerPos.y + kFollowHeadHeight, playerPos.z};
    }

    // The look yaw targets the player's facing plus the drag offset; the orbit
    // yaw (eye-offset direction) is its opposite.
    const float player_yaw = std::isfinite(playerYawDeg) ? playerYawDeg : 0.0f;
    const float target_yaw = wrap_deg(player_yaw + follow.yawOffsetDeg) + 180.0f;
    const float target_pitch = std::clamp(follow.pitchOffsetDeg, -kFollowPitchLimitDeg,
                                          kFollowPitchLimitDeg);

    // Exponential smoothing toward the targets; `blend` is this call's factor.
    const float blend = 1.0f - std::exp(-kFollowRate * dt);
    follow.followBlend = blend;

    const float current_yaw = std::isfinite(camera.yaw) ? camera.yaw : target_yaw;
    camera.yaw = wrap_deg(current_yaw + wrap_deg(target_yaw - current_yaw) * blend);

    const float current_pitch = std::isfinite(camera.pitch) ? camera.pitch : target_pitch;
    camera.pitch = std::clamp(current_pitch + (target_pitch - current_pitch) * blend,
                              -kPitchLimitDeg, kPitchLimitDeg);

    const float current_distance = std::isfinite(camera.distance) ? camera.distance : follow.distance;
    camera.distance = current_distance + (follow.distance - current_distance) * blend;
}

void camera_target_enter(Camera& camera, const CameraFollow& follow, CameraTarget& state,
                         Vec3 target) {
    state.savedYawOffsetDeg = follow.yawOffsetDeg;
    state.savedPitchOffsetDeg = follow.pitchOffsetDeg;
    state.savedDistance = follow.distance;
    state.saved = true;
    state.returning = false;
    state.returnSeconds = 0.0f;

    state.active = true;
    state.distance = std::clamp(state.distance, kTargetDistanceMin, kTargetDistanceMax);
    const bool finite_target =
        std::isfinite(target.x) && std::isfinite(target.y) && std::isfinite(target.z);
    state.target = finite_target ? target : camera.target;

    camera.target = state.target;
    camera.distance = state.distance;
    camera.pitch = kTargetDefaultPitchDeg;
    // The yaw is kept: the view swings to the entity rather than resetting.
}

void camera_target_exit(CameraFollow& follow, CameraTarget& state) {
    if (!state.active) {
        return;
    }
    state.active = false;
    if (state.saved) {
        follow.yawOffsetDeg = state.savedYawOffsetDeg;
        follow.pitchOffsetDeg = state.savedPitchOffsetDeg;
        follow.distance = state.savedDistance;
        state.saved = false;
    }
    // The camera is left where the orbit had it; camera_target_return blends the
    // target back to the player without the 40 m jump a bare camera_follow
    // would make.
    state.returning = true;
    state.returnSeconds = 0.0f;
}

bool camera_target_return(Camera& camera, CameraFollow& follow, CameraTarget& state,
                          Vec3 playerPos, float playerYawDeg, float dragDxPixels,
                          float dragDyPixels, float wheelDelta, bool moving, float dtSeconds) {
    if (!state.returning) {
        return false;
    }
    const float dt = std::max(std::isfinite(dtSeconds) ? dtSeconds : 0.0f, 0.0f);

    const Vec3 from = camera.target;
    camera_follow(camera, follow, playerPos, playerYawDeg, dragDxPixels, dragDyPixels,
                  wheelDelta, moving, dt);
    const Vec3 to = camera.target;

    const float blend = 1.0f - std::exp(-kFollowRate * dt);
    camera.target = Vec3{from.x + (to.x - from.x) * blend, from.y + (to.y - from.y) * blend,
                         from.z + (to.z - from.z) * blend};
    state.returnSeconds += dt;

    const float dx = to.x - camera.target.x;
    const float dy = to.y - camera.target.y;
    const float dz = to.z - camera.target.z;
    const float remaining = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (state.returnSeconds >= 1.0f || remaining < 0.01f) {
        camera.target = to;  // land exactly on the follow target
        camera.distance = follow.distance;
        state.returning = false;
        state.returnSeconds = 0.0f;
    }
    return state.returning;
}

void camera_target_toggle(Camera& camera, CameraFollow& follow, CameraTarget& state,
                          Vec3 target) {
    if (state.active) {
        camera_target_exit(follow, state);
    } else {
        camera_target_enter(camera, follow, state, target);
    }
}

void camera_target_update(Camera& camera, CameraTarget& state, Vec3 target, float dragDxPixels,
                          float dragDyPixels, float wheelDelta) {
    if (!state.active) {
        return;
    }
    if (std::isfinite(dragDxPixels)) {
        camera.yaw += dragDxPixels * kOrbitDegreesPerPixel;
    }
    if (std::isfinite(dragDyPixels)) {
        camera.pitch += dragDyPixels * kOrbitDegreesPerPixel;
    }
    camera.pitch = std::clamp(camera.pitch, -kTargetPitchLimitDeg, kTargetPitchLimitDeg);

    if (std::isfinite(wheelDelta) && wheelDelta != 0.0f) {
        state.distance = std::clamp(
            state.distance * std::exp(-kZoomPerWheelStep * wheelDelta), kTargetDistanceMin,
            kTargetDistanceMax);
    }
    state.distance = std::clamp(state.distance, kTargetDistanceMin, kTargetDistanceMax);
    camera.distance = state.distance;

    if (std::isfinite(target.x) && std::isfinite(target.y) && std::isfinite(target.z)) {
        state.target = target;
        camera.target = target;
    }
}

void camera_climb_enter(Camera& camera, CameraClimb& state, Vec3 attachPoint,
                        Vec3 surfaceNormal) {
    if (finite_vec3(attachPoint)) {
        camera.target = attachPoint;
    }

    // The orbit's eye offset points along (sin yaw, 0, cos yaw) horizontally,
    // so yaw = atan2(n.x, n.z) puts the eye on the normal's side — outward. A
    // zero-length or non-finite normal has no direction to seed from; keep the
    // current yaw rather than write a NaN.
    const float normal_length_squared = vec3_dot(surfaceNormal, surfaceNormal);
    if (finite_vec3(surfaceNormal) && normal_length_squared > 0.0f) {
        camera.yaw = std::atan2(surfaceNormal.x, surfaceNormal.z) / kDegToRad;
    } else if (!std::isfinite(camera.yaw)) {
        camera.yaw = 0.0f;
    }

    state.active = true;
    state.distance = kClimbDefaultDistance;
    camera.distance = state.distance;
    camera.pitch = kClimbDefaultPitchDeg;
}

void camera_climb_update(Camera& camera, CameraClimb& state, Vec3 attachPoint, float dragDxPixels,
                         float dragDyPixels, float wheelDelta, float dtSeconds) {
    if (!state.active) {
        return;
    }
    // The orbit responds to input samples, not to elapsed time: dt is not
    // integrated. A non-finite frame time is a broken sample, so drop the whole
    // call rather than feed a hand-driven orbit from an untrustworthy frame.
    if (!std::isfinite(dtSeconds)) {
        return;
    }

    if (finite_vec3(attachPoint)) {
        camera.target = attachPoint;
    }

    // A hand-set non-finite orbit would poison the clamps below; reset it to the
    // seeded values instead of propagating NaN.
    if (!std::isfinite(camera.yaw)) {
        camera.yaw = 0.0f;
    }
    if (!std::isfinite(camera.pitch)) {
        camera.pitch = kClimbDefaultPitchDeg;
    }
    if (!std::isfinite(state.distance)) {
        state.distance = kClimbDefaultDistance;
    }

    if (std::isfinite(dragDxPixels)) {
        camera.yaw += dragDxPixels * kOrbitDegreesPerPixel;
    }
    if (std::isfinite(dragDyPixels)) {
        camera.pitch += dragDyPixels * kOrbitDegreesPerPixel;
    }
    camera.pitch = std::clamp(camera.pitch, -kClimbPitchLimitDeg, kClimbPitchLimitDeg);

    if (std::isfinite(wheelDelta) && wheelDelta != 0.0f) {
        state.distance =
            std::clamp(state.distance * std::exp(-kZoomPerWheelStep * wheelDelta),
                       kClimbDistanceMin, kClimbDistanceMax);
    }
    state.distance = std::clamp(state.distance, kClimbDistanceMin, kClimbDistanceMax);
    camera.distance = state.distance;
}

void camera_climb_exit(Camera&, CameraClimb& state) {
    state.active = false;
    // The orbit (target, yaw, pitch, distance) is deliberately left untouched:
    // the follow camera eases back from where the user left it, with no snap.
}

// --- Death cinematic (S6 Task 5) ---------------------------------------------

namespace {

// The smoothstep ease: 0 at 0, 1 at 1, zero slope at both ends. A clamped
// input keeps a hand-driven segment time inside its own segment.
float death_ease(float u) {
    u = std::clamp(u, 0.0f, 1.0f);
    return u * u * (3.0f - 2.0f * u);
}

// Component-wise interpolation between two key poses. Yaw is lerped raw (the
// keys are base.yaw + a fixed offset, so there is no wrap ambiguity).
Camera death_blend(const Camera& from, const Camera& to, float u) {
    Camera out;
    out.target = Vec3{from.target.x + (to.target.x - from.target.x) * u,
                      from.target.y + (to.target.y - from.target.y) * u,
                      from.target.z + (to.target.z - from.target.z) * u};
    out.yaw = from.yaw + (to.yaw - from.yaw) * u;
    out.pitch = from.pitch + (to.pitch - from.pitch) * u;
    out.distance = from.distance + (to.distance - from.distance) * u;
    out.fovYDeg = from.fovYDeg + (to.fovYDeg - from.fovYDeg) * u;
    return out;
}

}  // namespace

Camera camera_death_pose(const Camera& base, const Vec3& valus, const Vec3& player, float t) {
    if (!std::isfinite(t) || !finite_vec3(valus) || !finite_vec3(player)) {
        return base;  // a broken sample: never write NaN into the camera
    }

    // The five key poses. The four cinematic keys share the base's fov and its
    // viewing side, offset by the fixed yaw offsets; the follow key re-anchors
    // the base on the player's head so it is a live follow pose.
    Camera pull = base;
    pull.target = Vec3{valus.x, valus.y + kDeathPullTargetHeight, valus.z};
    pull.distance = kDeathPullDistance;
    pull.yaw = base.yaw;
    pull.pitch = kDeathPullPitchDeg;

    Camera low = base;
    low.target = Vec3{valus.x, valus.y + kDeathLowTargetHeight, valus.z};
    low.distance = kDeathLowDistance;
    low.yaw = base.yaw + kDeathLowYawOffsetDeg;
    low.pitch = kDeathLowPitchDeg;

    Camera wide = base;
    wide.target = Vec3{valus.x, valus.y + kDeathWideTargetHeight, valus.z};
    wide.distance = kDeathWideDistance;
    wide.yaw = base.yaw + kDeathWideYawOffsetDeg;
    wide.pitch = kDeathWidePitchDeg;

    Camera follow = base;
    follow.target = Vec3{player.x, player.y + kFollowHeadHeight, player.z};

    // The segment boundaries return the key pose exactly (a segment's u is 0 at
    // its start and 1 at its end), so the test's five keys are exact values.
    if (t <= 0.0f) {
        return base;
    }
    if (t < kDeathPullSeconds) {
        return death_blend(base, pull, death_ease(t / kDeathPullSeconds));
    }
    if (t < kDeathLowSeconds) {
        return death_blend(
            pull, low,
            death_ease((t - kDeathPullSeconds) / (kDeathLowSeconds - kDeathPullSeconds)));
    }
    if (t < kDeathWideSeconds) {
        return death_blend(
            low, wide,
            death_ease((t - kDeathLowSeconds) / (kDeathWideSeconds - kDeathLowSeconds)));
    }
    if (t < kDeathFollowSeconds) {
        return death_blend(
            wide, follow,
            death_ease((t - kDeathWideSeconds) / (kDeathFollowSeconds - kDeathWideSeconds)));
    }
    return follow;
}

Camera camera_wake_pose(Vec3 spawn, float yawDeg, float t, float duration) {
    // An orbit key whose eye sits `back` metres along -yaw (negative = in
    // front) and `eyeUp` above the spawn, looking at `targetUp` above it.
    const auto key = [&](float back, float eyeUp, float targetUp) {
        Camera c{};
        c.target = Vec3{spawn.x, spawn.y + targetUp, spawn.z};
        const float horizontal = std::fabs(back);
        c.distance = std::sqrt(horizontal * horizontal + (eyeUp - targetUp) * (eyeUp - targetUp));
        c.pitch = std::atan2(eyeUp - targetUp, horizontal) / kDegToRad;
        c.yaw = back < 0.0f ? yawDeg : yawDeg + 180.0f;
        return c;
    };
    const Camera front = key(-kWakeFrontDistance, kWakeFrontEyeHeight, kWakeLyingTargetHeight);
    const Camera behind = key(kWakeBackDistance, kWakeBackEyeHeight, kFollowHeadHeight);
    if (!std::isfinite(t)) {
        return front;
    }
    if (!(duration > 0.0f) || t >= duration) {
        return behind;
    }
    return death_blend(front, behind, death_ease(t / duration));
}
