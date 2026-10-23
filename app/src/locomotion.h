#pragma once

#include <functional>

#include "heightmap.h"
#include "math3d.h"

// Entity locomotion (S11 Task 8, design D9): a pure, headless step that moves an
// entity's ground origin along its yaw and keeps it on the ground. Generic: the
// speeds and turn rate are data (a colossus's table measures them from its walk,
// back and turn clips' root motion); nothing here knows a colossus. The rig's
// height above its origin (Quadratus's hip 14.53 m) is the caller's offset.
//
// Conventions are player.h's: yaw 0 faces +Z, yaw 90 faces +X, so forward is
// (sin yaw, 0, cos yaw). A positive turn turns right — yaw decreases — as in
// ride.h.

struct LocomotionTuning {
    float walkSpeed = 0.0f;    // m/s at drive +1 (the walk clip's root distance / its length)
    float backSpeed = 0.0f;    // m/s at drive -1
    float turnRateDeg = 0.0f;  // deg/s at turn +-1 (a turn clip's heading change / its length)
};

struct EntityMotion {
    Vec3 position{};      // the ground origin, world space
    float yawDeg = 0.0f;  // wrapped into [-180, 180]
};

// The ground under a move. Answers true and writes the floor height at `to`
// when the move `from -> to` is allowed; false refuses it.
using GroundQuery = std::function<bool(Vec3 from, Vec3 to, float& floorY)>;

// A GroundQuery over `heightmap` (kept by reference: it must outlive the
// query). It refuses a target outside the map's XZ bounds or one
// heightmap_walkable refuses, and floors at heightmap_floor(to, kFloorStep).
GroundQuery heightmap_ground(const Heightmap& heightmap);

enum class LocomotionResult { Still, Moved, Refused };

// One step. `drive` in [-1, 1] (clamped) scales walkSpeed forward or backSpeed
// backward; `turn` in [-1, 1] (clamped) scales turnRateDeg. The turn applies
// first, then the move along the new heading. A move the ground refuses — a
// false answer, a non-finite floor, or an empty query — leaves the position
// untouched (Refused); the turn still applies. An accepted move sets the
// position's Y to the floor (Moved). A non-finite or non-positive `dt` changes
// nothing (Still), and a non-finite drive or turn counts as 0. The caller
// clamps frame spikes; the ground only sees the step's two endpoints.
LocomotionResult locomotion_step(EntityMotion& motion, float drive, float turn,
                                 const LocomotionTuning& tuning, const GroundQuery& ground,
                                 float dt);
