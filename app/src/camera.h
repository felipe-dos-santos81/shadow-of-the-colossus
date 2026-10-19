#pragma once

#include "math3d.h"

// Orbit camera: the eye sits `distance` from `target` at the given yaw and
// pitch angles (degrees), and camera_view/camera_proj produce the column-major,
// right-handed, Y-up matrices the shader consumes. Pitch is limited to ±89°
// so the view's up vector can never become parallel with the world's.
struct Camera {
    Vec3 target;
    float distance, yaw, pitch;
    float fovYDeg = 60.0f;
};

// World-to-view matrix: yaw orbits around world +Y (0° = the target's +Z
// side), pitch lifts the eye from the horizon, and the world's +Y is up.
Mat4 camera_view(const Camera&);

// View-to-clip matrix: right-handed, Metal's [0, 1] depth range. The near and
// far planes scale with the orbit distance.
Mat4 camera_proj(const Camera&, float aspect);

// Mouse drag in pixels: dx turns the yaw, dy changes the pitch (clamped to
// ±89°). No mouse capture is involved.
void camera_orbit(Camera&, float dxPixels, float dyPixels);

// Mouse wheel: one notch is 1.0. The distance scales multiplicatively and is
// clamped to [0.5, 500].
void camera_zoom(Camera&, float wheelDelta);

// Fits the bounds: target = their centre, distance fits the half-diagonal at
// the camera's vertical FOV, pitch ~20°. The yaw (the viewing direction) is
// kept.
void camera_frame(Camera&, Vec3 boundsMin, Vec3 boundsMax);

// The camera's look-direction yaw in degrees — the movement basis player_update
// takes. The orbit Camera stores the opposite direction (the eye sits at
// `yaw`, so the look direction is yaw - 180). This is the *active* camera: in
// entity-target mode the orbit is around the entity and the same rule holds,
// which is what makes the movement basis follow the framed view.
float camera_look_yaw(const Camera& camera);

// A game-style third-person follow camera (S3 arena plan, W1.4): the eye trails
// the player and looks where the player faces.
//
// The *look* yaw targets `playerYawDeg + yawOffsetDeg`; the orbit Camera above
// stores the eye-offset direction, which is the opposite of the look direction,
// so `camera.yaw` targets that value + 180° (at player yaw 0 the eye sits at
// the player's -Z side and looks along +Z — a chase view, pinned by a test).
// The orbit target is the head point, `playerPos + (0, 1.4, 0)`, set exactly;
// yaw, pitch and distance converge on their targets with exponential smoothing
// at 8 s^-1 (`followBlend` reports each call's factor, 1 - exp(-8 dt)).
//
// Drag adds to the offsets with the orbit camera's own 0.4°/pixel sensitivity;
// `pitchOffsetDeg` is clamped to ±60°. While `moving`, both offsets decay
// toward 0 at 1.5 s^-1 (the camera recenters behind the player). The wheel
// zooms the distance multiplicatively (one notch is exp(-0.15)), clamped to
// [2, 12] m; the default distance is 4.5 m.
//
// dtSeconds is clamped into [0, ...] (a zero or negative dt is a no-op), and
// non-finite drag/wheel values are ignored, so a hand-driven call never writes
// NaN into the camera.
struct CameraFollow {
    float yawOffsetDeg = 0.0f;
    float pitchOffsetDeg = 0.0f;
    float distance = 4.5f;
    float followBlend = 0.0f;  // internal: the last call's smoothing factor
};

void camera_follow(Camera&, CameraFollow&, Vec3 playerPos, float playerYawDeg,
                   float dragDxPixels, float dragDyPixels, float wheelDelta, bool moving,
                   float dtSeconds);

// --- Entity target framing (S4 Task 5) ---------------------------------------
//
// `T` toggles a camera-only framing mode: the orbit's target becomes the
// entity's chest joint (Bone_MUNE) instead of the player's head. Distance is
// widened to [22, 30] m — the builder value that fits the 17.7 m body — and
// pitch to ±80°; drag orbits the chest, the wheel zooms. The mode is camera
// only: the player controller keeps the same movement basis, now taken from the
// active (target) camera's yaw via camera_look_yaw(). T-off restores the saved
// follow offsets/distance and leaves the camera where the orbit had it, so the
// next camera_follow call eases back without a snap.

inline constexpr float kTargetDistanceMin = 22.0f;
inline constexpr float kTargetDistanceMax = 30.0f;
inline constexpr float kTargetDefaultDistance = 26.0f;
inline constexpr float kTargetDefaultPitchDeg = 10.0f;
inline constexpr float kTargetPitchLimitDeg = 80.0f;

struct CameraTarget {
    bool active = false;
    Vec3 target{};              // the chest point this frame (world space)
    float distance = kTargetDefaultDistance;
    // The follow state saved when the mode was entered; restored on exit.
    float savedYawOffsetDeg = 0.0f;
    float savedPitchOffsetDeg = 0.0f;
    float savedDistance = 4.5f;
    bool saved = false;
    // T-off transition: while true the caller eases back with
    // camera_target_return instead of camera_follow (the saved offsets are
    // already restored). `returnSeconds` bounds the asymptote.
    bool returning = false;
    float returnSeconds = 0.0f;
};

// Enters target framing around `target`: saves the follow offsets and distance,
// sets the orbit target to the chest, restores the target distance (clamped to
// [22, 30]), pins the framing pitch to 10°, and keeps the current yaw so the
// view swings to the subject instead of spinning.
void camera_target_enter(Camera&, const CameraFollow&, CameraTarget&, Vec3 target);

// Leaves target framing: the saved follow offsets/distance are written back
// into `follow`. The camera is left exactly where the orbit had it; the app
// then calls camera_target_return once per frame until it reports the follow
// framing has been reached (no target jump, no orientation snap).
void camera_target_exit(Camera&, CameraFollow&, CameraTarget&);

// One frame of the T-off return. While `state.returning`, this runs a normal
// camera_follow step (the saved offsets, drag/wheel and offset decay all apply)
// but blends the orbit target from the entity's chest to the follow head point
// at the follow camera's own 8 s^-1 rate. The blend is bounded to one second so
// the asymptote cannot linger. Returns true while the return is in progress;
// on false the camera is exactly at the follow state and the caller resumes
// camera_follow normally.
bool camera_target_return(Camera&, CameraFollow&, CameraTarget&, Vec3 playerPos,
                          float playerYawDeg, float dragDxPixels, float dragDyPixels,
                          float wheelDelta, bool moving, float dtSeconds);

// `T`: enter when inactive, leave when active.
void camera_target_toggle(Camera&, CameraFollow&, CameraTarget&, Vec3 target);

// One frame in target mode (a no-op when inactive): drag orbits the chest at
// the orbit camera's 0.4°/pixel sensitivity (pitch clamped to ±80°), the wheel
// zooms multiplicatively within [22, 30] m, and the target follows `target`.
// Non-finite inputs are ignored, so a hand-driven call never writes NaN.
void camera_target_update(Camera&, CameraTarget&, Vec3 target, float dragDxPixels,
                          float dragDyPixels, float wheelDelta);
