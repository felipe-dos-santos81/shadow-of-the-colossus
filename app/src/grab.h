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
// hit routing consumes). `branch` is the one lateral fork in the chain: the
// chest's is the right shoulder, reached by climbing forward with a lateral
// input at the band top; every other band leaves it `Count`.

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
    ClimbBandId branch = ClimbBandId::Count;  // W+lateral fork at the top; Count none
    bool horizontal = false;  // S11 surface walk: stood on and walked, not hung from
};

// The band record for `id`. `Count` (and any out-of-range cast) answers a
// stable empty band rather than indexing out of bounds.
const ClimbBand& climb_band(ClimbBandId id);

// The band's stable lowercase status token (`left_foot` .. `right_arm`), and
// `"none"` for `Count`. Never null.
const char* climb_band_name(ClimbBandId id);

// The material shell of weakpoint `weakpoint` — 0 is "weakpoint_03" (the
// forehead), 1 is "weakpoint_04" (the right arm). Any other id answers nullptr.
const char* weakpoint_material_name(int weakpoint);

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

// --- S11 surface walk (design D9) ---------------------------------------------
//
// A band flagged `horizontal` (Quadratus's back) is a floor, not a wall: the
// player stands on its top and walks along it (`t`, as a climb) and across it
// (`across`, metres of arc around the axis from the top, positive to the right
// of a walker facing boneA -> boneB). The band's frame is its bone axis and the
// `up` hint made perpendicular to it (Gram-Schmidt), so a band that tilts — the
// rear kneel turns the back into a slope — tilts its top normal with it.
// ponytail: JointLookup carries positions only, so the bone's roll is not seen;
// pass the posed bone's own up as `up` if the back ever rolls under the feet.

// The arc limit either side of the top; past it the surface is a wall (the
// heightmap's kWallSteepness is cos 60°).
inline constexpr float kSurfaceWalkMaxAngleDeg = 60.0f;

// The feet on the band's top at parameter `t` (clamped to [0, 1]) and arc
// offset `across` (clamped to +-radius * kSurfaceWalkMaxAngleDeg): the axis
// point plus `radius` along `normal`, the unit surface normal there. Returns
// false and writes nothing for missing joints, a degenerate segment, a band
// parallel to `up` (no top), a non-positive radius, or non-finite input.
bool surface_walk_point(const ClimbBand& band, const JointLookup& lookup, float t, float across,
                        Vec3 up, Vec3& point, Vec3& normal);

// One surface-walk step: `along` and `lateral` (each clamped to [-1, 1]) move
// `t` by along * speed * dt / segmentLength (clamped to [0, 1]; the chain
// hand-over at an end is the caller's) and `across` by lateral * speed * dt
// (clamped as above). A non-finite or non-positive dt, or a non-finite input,
// moves nothing. Returns false, changing nothing, when the band's segment
// cannot be formed (the caller's grip break).
bool surface_walk_step(const ClimbBand& band, const JointLookup& lookup, float along,
                       float lateral, float speed, float dt, float& t, float& across);

// The attach point for any band: a horizontal band's surface_walk_point (up is
// world +Y; `hint` unused), every other band's climb_surface_point (`across`
// unused) — the existing climb, bit for bit.
bool climb_attach_point(const ClimbBand& band, const JointLookup& lookup, float t, float across,
                        Vec3 hint, Vec3& point, Vec3& normal);
