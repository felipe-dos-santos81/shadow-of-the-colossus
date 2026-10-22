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
void camera_target_exit(CameraFollow&, CameraTarget&);

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

// --- Climb camera (S4.1 Task 4) ---------------------------------------------
//
// While the player grips the colossus the orbit is pinned to the attach point
// on the body instead of trailing the player. Entering seeds the eye *outward*
// along the surface normal — yaw = atan2(normal.x, normal.z) in degrees, so the
// eye sits on the normal's side and the view looks back into the body — with a
// 10° elevation and a 6 m distance. Drag orbits at the orbit camera's
// 0.4°/pixel sensitivity with pitch clamped to ±45°; the wheel zooms
// multiplicatively within [5, 8] m. Leaving the mode only clears `active`: the
// orbit stays exactly where the user left it, so the follow camera can ease
// back without a snap.
//
// The orbit is input-driven (per drag pixel and per wheel notch), so dtSeconds
// is not integrated; a non-finite frame time is a broken sample and the whole
// call is ignored. Non-finite drag/wheel values are ignored channel by channel,
// and a non-finite attach point or normal is discarded, so a hand-driven call
// never writes NaN.
inline constexpr float kClimbDistanceMin = 5.0f;
inline constexpr float kClimbDistanceMax = 8.0f;
inline constexpr float kClimbDefaultDistance = 6.0f;
inline constexpr float kClimbDefaultPitchDeg = 10.0f;
inline constexpr float kClimbPitchLimitDeg = 45.0f;

struct CameraClimb {
    bool active = false;
    float distance = kClimbDefaultDistance;
};

// Enters the climb orbit around `attachPoint`, seeded outward along
// `surfaceNormal` (its length does not matter; a zero or non-finite normal
// keeps the current yaw). A non-finite attach point keeps the current target.
void camera_climb_enter(Camera&, CameraClimb&, Vec3 attachPoint, Vec3 surfaceNormal);

// One frame of the climb orbit (a no-op when inactive): the target follows the
// attach point, drag orbits at 0.4°/pixel with pitch clamped to ±45°, and the
// wheel zooms `distance` multiplicatively inside [5, 8] m.
void camera_climb_update(Camera&, CameraClimb&, Vec3 attachPoint, float dragDx,
                         float dragDy, float wheelDelta, float dtSeconds);

// Leaves the climb orbit: `active = false` and nothing else — the orbit is left
// exactly where it is (no snap).
void camera_climb_exit(Camera&, CameraClimb&);

// --- Death cinematic (S6 Task 5) ---------------------------------------------
//
// The reconstructed death camera (design §8.3, ruling D2: the shipped
// `camera_minoa_death_front` clips are track type 36 and are not played). The
// path is keyed on the death clock `t` (seconds since the defeat) and eases
// between five key poses, all orbit cameras around a point above the entity's
// placement point `valus`:
//
//   t = 0        the follow pose (`base`): the pose the defeat interrupted
//   0.4 s        the pull-back: the eye 12 m out and 4 m above the entity
//                base, tilted up at the body's midpoint (9 m) — "a 12 m
//                pull-back / 4 m up", framing the death, not the legs
//   3.0 s        the low angle: the target 10 m up (the tendril band), 8 m out,
//                pitched -40° so the eye sits low and looks up at the burst
//   6.2 s        the corpse wide: the target 2 m up, 24 m out, pitched 12°
//   8.0 s        the follow pose again, re-anchored on the player's head
//
// The keys after T0 orbit the base's own viewing side by fixed yaw offsets, so
// the cinematic turns around Valus without depending on where the player was
// facing. The app captures the camera pose on the first locked frame and passes
// it as `base` for the whole sequence (the follow pose in the ordinary defeat;
// the climb/target pose if that owned the view when the death started), so the
// path cannot feed back on itself. The player's input is locked while it runs
// (design D7) and the cursor is never captured (A5).

// The follow camera's head point above the player (camera_follow's target);
// camera_death_pose re-anchors its return key on the same point.
inline constexpr float kFollowHeadHeight = 1.4f;

inline constexpr float kDeathPullSeconds = 0.4f;   // the pull-back key
inline constexpr float kDeathLowSeconds = 3.0f;    // the low-angle key
inline constexpr float kDeathWideSeconds = 6.2f;   // the corpse-wide key
inline constexpr float kDeathFollowSeconds = 8.0f; // the follow key
inline constexpr float kDeathPullDistance = 12.0f;      // the 12 m pull-back
inline constexpr float kDeathPullEyeHeight = 4.0f;      // the eye 4 m above the entity base
inline constexpr float kDeathPullTargetHeight = 9.0f;   // the body's midpoint
// The pitch that puts the eye at kDeathPullEyeHeight: -asin((9 - 4) / 12).
inline constexpr float kDeathPullPitchDeg = -24.6243f;
inline constexpr float kDeathLowTargetHeight = 10.0f;  // the tendril band
inline constexpr float kDeathLowDistance = 8.0f;
inline constexpr float kDeathLowPitchDeg = -40.0f;     // below the target: a low angle
inline constexpr float kDeathLowYawOffsetDeg = 25.0f;
inline constexpr float kDeathWideTargetHeight = 2.0f;  // the corpse on the ground
inline constexpr float kDeathWideDistance = 24.0f;
inline constexpr float kDeathWidePitchDeg = 12.0f;
inline constexpr float kDeathWideYawOffsetDeg = 45.0f;

// The pose at death-clock time `t`. `t <= 0` answers `base` exactly, and so
// does a non-finite `t` or point (a broken sample never writes NaN). The
// segments ease with a smoothstep, so the pose at each key time is exactly that
// key and the path is continuous. `valus` is the entity's placement point;
// `player` anchors the T8 follow key.
Camera camera_death_pose(const Camera& base, const Vec3& valus, const Vec3& player, float t);
