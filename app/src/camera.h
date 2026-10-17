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
