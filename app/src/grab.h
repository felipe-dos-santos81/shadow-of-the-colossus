#pragma once

#include <functional>
#include <string_view>

#include "math3d.h"

// Climbing bands for the colossus grab (S4.1 Task 2).
//
// A band is one capsule-shaped strip of the body between two posed skeleton
// joints. The chain runs from the left foot up the left leg and torso to the
// head, then down the right arm; `up`/`down` are the band above and below in
// that chain, and `ClimbBandId::Count` terminates either end. `weakpoint` is
// -1 for an ordinary band, 0 for the forehead and 1 for the right arm — the
// two weakpoint identifiers the fight design pins (the values the later task's
// hit routing consumes).

enum class ClimbBandId {
    LeftFoot,
    LeftShin,
    LeftThigh,
    Waist,
    Chest,
    Neck,
    Head,
    RightShoulder,
    RightArm,
    Count,
};

struct ClimbBand {
    const char* boneA;  // segment start, a joint name in the colossus rig
    const char* boneB;  // segment end
    float radius;       // body radius around the segment (metres)
    ClimbBandId up;     // next band towards the head; Count at the top
    ClimbBandId down;   // next band towards the feet; Count at the bottom
    int weakpoint;      // -1 none, 0 forehead, 1 right arm
};

// The band record for `id`. `Count` (and any out-of-range cast) answers a
// stable empty band rather than indexing out of bounds.
const ClimbBand& climb_band(ClimbBandId id);

// The band's stable lowercase status token (`left_foot` .. `right_arm`), and
// `"none"` for `Count`. Never null.
const char* climb_band_name(ClimbBandId id);

// Resolves joint names to posed world positions. The next task adapts
// posed_joint_world_position to this signature; returning false means the
// joint is absent (another rig, or a malformed skeleton).
using JointLookup = std::function<bool(std::string_view, Vec3&)>;

// The band's world-space segment. Returns false, writing nothing, when either
// joint is missing, a position is not finite, or the segment has zero length.
bool climb_band_segment(const ClimbBand& band, const JointLookup& lookup, Vec3& a, Vec3& b);

// The point on the climbing surface at parameter `t` (clamped to [0, 1] along
// the segment): the axis point plus the horizontal radial direction from
// `hint` scaled by `band.radius + kPlayerCapsuleRadius`. `normal` is that
// unit radial (horizontal, +X when `hint` sits on the axis). Returns false and
// writes nothing for missing joints, non-finite input, a degenerate segment,
// or a negative/non-finite radius.
bool climb_surface_point(const ClimbBand& band, const JointLookup& lookup, float t, Vec3 hint,
                         Vec3& point, Vec3& normal);

// The band whose surface is nearest the player capsule (feet at `playerPos`,
// kPlayerCapsuleHeight tall, kPlayerCapsuleRadius around the axis) when that
// surface distance is at most `maxSurfaceDistance`. Returns
// ClimbBandId::Count when no band is within reach, when the lookup is empty,
// or when the input is invalid — never an out-of-range index. Ties keep the
// earlier band in table order.
ClimbBandId nearest_climb_band(const JointLookup& lookup, Vec3 playerPos,
                               float maxSurfaceDistance);
