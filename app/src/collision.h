#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "encounters/valus_desc.h"
#include "heightmap.h"
#include "math3d.h"
#include "player.h"
#include "scene.h"

// Entity collision for the arena player (S4.0 Task 5).
//
// The player is a vertical capsule pinned by the design: radius 0.35 m, height
// 1.7 m, feet at `PlayerState::position.y`. Entities contribute capsules built
// from their *posed* skeleton (the per-rig palette), so the proxy follows the
// animation and the manifest placement/lift. Resolution is horizontal only:
// each overlapping capsule pushes the player out along the shortest horizontal
// exit, two iterations handle overlapping capsules, and the vertical position
// is deliberately never touched by the resolver itself.

struct Capsule {
    Vec3 a, b;  // the segment's endpoints, world space
    float r;    // radius (metres)
    // Identity of the body segment that produced this capsule: the rig index
    // and the capsule's index in that rig's table (EncounterDesc::capsules order).
    // -1 means hand-built/unknown. The grab solver uses these to exclude the
    // limb the player already holds; they are trailing fields so the original
    // aggregate initialiser `Capsule{a, b, r}` still compiles.
    int rig = -1;
    int segment = -1;
};

inline constexpr float kPlayerCapsuleRadius = 0.35f;
inline constexpr float kPlayerCapsuleHeight = 1.7f;

// The distance from `point` to the player's vertical capsule *axis* at `feet`:
// the segment [feet, feet + kPlayerCapsuleHeight]. The closest point keeps the
// horizontal offset and only clamps y, so a point beside the player is as far
// away as it stands — not merely as high as it sits. The weakpoint strike gate
// (`strike_reaches_shell`) and Valus's attack damage gate
// (`apply_valus_impact`) both measure reach with this one function, so the two
// gates cannot drift apart. A non-finite input answers a non-finite distance;
// both callers already reject that.
float player_capsule_distance(Vec3 feet, Vec3 point);

// Pushes `player` out of every capsule that overlaps its vertical span, in two
// iterations. A capsule is skipped when its y extent (expanded by its radius)
// does not overlap [position.y, position.y + kPlayerCapsuleHeight] — walking
// under a raised limb is allowed. The push is along the shortest horizontal
// exit from the capsule's XZ-projected axis; a player exactly on that axis (or
// inside a point-shaped capsule) exits perpendicular to the axis, or along +X
// for a zero-length axis. Y is left exactly as it was.
void resolve_player_collision(PlayerState& player, const std::vector<Capsule>& capsules);

// The same resolution, then the ground is re-sampled at the resolved XZ
// position and written to `position.y` when the sample is finite (the ruling's
// order: resolve horizontally, then reapply the heightmap ground sample).
void resolve_player_collision(PlayerState& player, const std::vector<Capsule>& capsules,
                              const Heightmap& heightmap);

// Builds the entity's collision proxy from rig `rigIndex`'s posed palette, one
// capsule per `capsules` row in table order (S11: the encounter's table; the
// default is Valus's). Valus's: the two leg chains and the torso chain, in this
// pinned order
//
//   left  MOMO_L -> SUNE_L -> TOE_L -> TUMASAKI_L   (thigh, shin, foot)
//   right MOMO_R -> SUNE_R -> TOE_R -> TUMASAKI_R
//   torso HARA -> MUNE -> KUBI                      (waist, chest, neck)
//
// Radii (metres): thigh 1.7, shin 1.6, foot 1.7, torso 2.0. They are measured
// from the posed Valus mesh around each segment (dominant-weight vertices:
// thigh p99 1.61 / max 2.00, shin p99 1.49 / max 1.56, foot p99 1.68 / max
// 1.75; the torso proxy is unreachable from the arena floor). The player's own
// 0.35 m radius is added at resolution, so the effective centreline clearance
// is 2.05 / 1.95 / 2.05 m — the body silhouette cannot be entered. Capsules
// whose named bones are absent (another rig, or a malformed skeleton) are
// skipped; the default Valus rig yields 8. The world-space conversion uses the
// rig's mesh model transform, so the manifest placement and `apply_rig_lift`
// are included.
std::vector<Capsule> entity_collision_capsules(
    const Scene& scene, std::size_t rigIndex, const std::vector<Mat4>& palette,
    std::span<const CapsuleDesc> capsules);
