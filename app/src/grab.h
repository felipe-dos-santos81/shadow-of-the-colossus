#pragma once

#include <functional>
#include <span>
#include <string_view>

#include "encounters/valus_desc.h"
#include "math3d.h"

// Climbing bands for the colossus grab (S4.1 Task 2; S11 Task 7 moved the band
// table and its types into the encounter description, encounter_desc.h).
//
// A band is one capsule-shaped strip of the body between two posed skeleton
// joints; its `up`/`down`/`branch` links and its `weakpoint` are indices into
// the encounter's tables, -1 (`ClimbBandId::None`) for none. The functions
// below take the encounter's band table. The overloads without one read
// Valus's (`encounters::kValus.bands`) and are for Valus only — the unit tests
// and the frozen selftests. Runtime code passes the encounter's table
// (`PlayerFrame::bands`, `EncounterDesc`); an int id converts silently, so the
// type will not catch a Valus overload called on another colossus.

// The band record for `id` in `bands`. An out-of-range id (None included)
// answers a stable empty band rather than indexing out of bounds.
const ClimbBand& climb_band(std::span<const ClimbBand> bands, int id);
const ClimbBand& climb_band(ClimbBandId id);

// The band's stable lowercase status token (its table `name`), and `"none"`
// for an out-of-range id. Never null.
const char* climb_band_name(std::span<const ClimbBand> bands, int id);
const char* climb_band_name(ClimbBandId id);

// The material shell of weakpoint `weakpoint` in `weakpoints` — Valus's 0 is
// "weakpoint_03" (the forehead), 1 is "weakpoint_04" (the right arm). Any other
// id answers nullptr.
const char* weakpoint_material_name(std::span<const WeakpointDesc> weakpoints, int weakpoint);
const char* weakpoint_material_name(int weakpoint);

// Resolves joint names to posed world positions. The next task adapts
// posed_joint_world_position to this signature; returning false means the
// joint is absent (another rig, or a malformed skeleton).
using JointLookup = std::function<bool(std::string_view, Vec3&)>;

// The band's radius at parameter `t` (clamped to [0, 1]): `radius` at boneA,
// `radiusB` at boneB, linear between (S11 taper, Quadratus's back); a band with
// no taper (radiusB < 0, every Valus band) answers `radius` everywhere.
float band_radius_at(const ClimbBand& band, float t);

// The band's world-space segment. Returns false, writing nothing, when either
// joint is missing, a position is not finite, or the segment has zero length.
bool climb_band_segment(const ClimbBand& band, const JointLookup& lookup, Vec3& a, Vec3& b);

// The point on the climbing surface at parameter `t` (clamped to [0, 1] along
// the segment): the axis point plus the horizontal radial direction from
// `hint` scaled by `band_radius_at(band, t) + kPlayerCapsuleRadius`. `normal` is that
// unit radial (horizontal, +X when `hint` sits on the axis). Returns false and
// writes nothing for missing joints, non-finite input, a degenerate segment,
// or a negative/non-finite radius.
bool climb_surface_point(const ClimbBand& band, const JointLookup& lookup, float t, Vec3 hint,
                         Vec3& point, Vec3& normal);

// The band of `bands` whose surface is nearest the player capsule (feet at
// `playerPos`, kPlayerCapsuleHeight tall, kPlayerCapsuleRadius around the axis)
// when that surface distance is at most `maxSurfaceDistance`. Returns
// ClimbBandId::None when no band is within reach, when the lookup is empty,
// or when the input is invalid — never an out-of-range index. Ties keep the
// earlier band in table order.
//
// S12: a band `band_enabled` rejects for (`playingClip`, `frame`) — the
// entity's playing clip and frame — is skipped, and a `grabEntryLowEnd` band is
// measured to its low end only (a ball of the band's radius there), so it is
// reached at that end and nowhere else. The defaults (no clip) skip only
// window bands; no S4/S11 band has one.
ClimbBandId nearest_climb_band(std::span<const ClimbBand> bands, const JointLookup& lookup,
                               Vec3 playerPos, float maxSurfaceDistance,
                               std::string_view playingClip = {}, int frame = 0);
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
// offset `across` (clamped to +-r * kSurfaceWalkMaxAngleDeg, r the radius at
// t): the axis point plus r along `normal`, the unit surface normal there. Returns
// false and writes nothing for missing joints, a degenerate segment, a band
// parallel to `up` (no top), a non-positive radius, or non-finite input.
bool surface_walk_point(const ClimbBand& band, const JointLookup& lookup, float t, float across,
                        Vec3 up, Vec3& point, Vec3& normal);

// One surface-walk step: `along` and `lateral` (each clamped to [-1, 1]) move
// `t` by along * speed * dt / segmentLength (clamped to [0, 1]; the chain
// hand-over at an end is the caller's) and `across` by lateral * speed * dt
// (clamped as above). A non-finite or non-positive dt, or a non-finite input,
// moves nothing. Returns false, changing nothing, when the band's segment
// cannot be formed (the caller's grip break) or the radius is not positive and
// finite. Whether a band is walked at all is band_walkable's slope gate.
bool surface_walk_step(const ClimbBand& band, const JointLookup& lookup, float along,
                       float lateral, float speed, float dt, float& t, float& across);

// The attach point for any band: a walkable band's surface_walk_point (up is
// world +Y; `hint` unused), every other band's climb_surface_point (`across`
// unused) — the existing climb, bit for bit.
bool climb_attach_point(const ClimbBand& band, const JointLookup& lookup, float t, float across,
                        Vec3 hint, Vec3& point, Vec3& normal);

// --- S12 band windows, the slope gate, the low end (design D7/D8, §13.4) ------

// Whether `band` exists while the entity plays `playingClip` at `frame`: always
// for a band with no `enableClip`; else only for that clip at a frame in
// [enableFrom, enableTo) (enableTo < 0: no end). See ClimbBand.
bool band_enabled(const ClimbBand& band, std::string_view playingClip, int frame);

// D8 (authored, the heightmap's kWallSteepness): a `horizontal` band is walked
// only while its top normal's y >= cos of this; steeper, it is hung and climbed
// like any other band.
inline constexpr float kWalkableBandMaxSlopeDeg = 60.0f;

// The disable hand-over's reach (authored: Plan Review Focus 2's continuity
// bound): the player is carried onto `carryTo` only when its attach point lies
// within this of him; farther, he falls rather than snap.
inline constexpr float kCarryReachMeters = 0.5f;

// `horizontal` and its top (the up hint +Y made perpendicular to the posed
// segment) passes the slope gate. False for a band whose segment cannot be
// formed.
bool band_walkable(const ClimbBand& band, const JointLookup& lookup);

// The parameter of the band's low end: 1 when boneB sits strictly lower than
// boneA, else 0 — a tie, or a band that cannot be resolved, is boneA. A table
// lists a low-end band low end -> high end (the blade: tip -> hilt), so its
// `carryFraction` measures the upper part.
float band_low_end_t(const ClimbBand& band, const JointLookup& lookup);
