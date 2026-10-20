#pragma once

#include <cstddef>

#include "heightmap.h"
#include "math3d.h"

// The player controller (S3 arena plan, W1.4): ground-following movement on the
// heightmap, no wall or prop collision (spec §1). All units are metres and
// seconds; the camera's yaw is in degrees.

// One frame's key state, read from the keyboard.
struct PlayerInput {
    bool forward = false;
    bool back = false;
    bool left = false;
    bool right = false;
    bool run = false;  // Shift: run speed instead of walk speed
    bool grab = false;    // E: held to grip a climb band
    bool strike = false;  // J: held to charge, released to plunge
};

// The player's world position and facing yaw (degrees).
struct PlayerState {
    Vec3 position{0.0f, 0.0f, 0.0f};
    float yawDeg = 0.0f;
};

// Advances the player one frame.
//
// `cameraYawDeg` is the camera's look-direction yaw: the direction the camera
// faces, projected on the ground, measured from +Z toward +X — so yaw 0 looks
// along +Z and yaw 90 along +X. The movement basis is the pinned convention:
//
//     forward = (sin(cameraYaw), 0, cos(cameraYaw))
//     right   = forward x +Y = (-cos(cameraYaw), 0, sin(cameraYaw))
//
// and the inputs combine as `forward - back` on the forward axis and
// `right - left` on the right axis. At camera yaw 0, W moves +Z and D (screen
// right) moves -X. The combined direction is normalized, so diagonals do not
// move faster.
//
// Walk speed is 2.2 m/s, run (input.run) 5.0 m/s. The yaw turns toward the
// movement direction at 540 deg/s (shortest path; no turn when there is no
// movement input). `dtSeconds` is clamped into [0, 0.05] so a frame-time spike
// cannot teleport the player past the edge clamp. The XZ position is clamped
// into the heightmap's bounds inset by 0.5 m, then `position.y` is set to
// heightmap_sample() at the clamped XZ. A non-finite sample (no ground) keeps
// the previous y; an empty heightmap (no grid at all) clamps nothing and keeps
// y. The update never writes NaN into the state.
void player_update(PlayerState& state, const PlayerInput& input, float dtSeconds,
                   const Heightmap& heightmap, float cameraYawDeg);

// The world transform the player's meshes are drawn with: translation to
// `state.position` and a rotation about +Y by `state.yawDeg`. The rotation maps
// the model's authored +Z (the glTF character facing) onto the movement
// direction (sin(yaw), 0, cos(yaw)), so the character faces where it walks.
//
// `modelGroundOffsetY` lifts the model by its own origin-to-lowest-vertex
// offset: the controller's position is the ground contact point (position.y =
// heightmap_sample), but a converted glb's origin often sits at the character's
// hips — wander's origin is 0.76 m above its feet — so drawing at the raw
// position sinks the character into the ground. Pass 0 to draw the model's
// origin exactly at the position.
Mat4 player_model_matrix(const PlayerState& state, float modelGroundOffsetY = 0.0f);

// The lift `player_model_matrix` needs for a rig's meshes: the negated lowest
// vertex y over the half-open mesh range [meshBegin, meshEnd) (a rigged glb's
// meshes carry an identity model in a composed scene, so their vertex y's are
// local). 0 when every vertex is at or above y = 0, and 0 for an empty or
// inverted range; a range past the end is clamped.
float mesh_ground_offset(const Scene& scene, std::size_t meshBegin, std::size_t meshEnd);

// The same lift for one rig record: mesh_ground_offset over the rig's own mesh
// range. Used for the player's rig today; an entity's rig (which the design
// grounds the same way) takes the same call.
float rig_ground_offset(const Scene& scene, const RigCpu& rig);
