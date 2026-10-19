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
