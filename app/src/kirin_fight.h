#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "additive.h"
#include "encounters/kirin.h"
#include "heightmap.h"
#include "knight_fight.h"
#include "route.h"
#include "terrain.h"

// Phaedra's fight at runtime (S13 Task 15, design D7–D12, §13.2): glue on the shared step.
// Her exported graph (scr_registry_kirin) runs through S12's base-motion layer (KnightCore,
// her kirin_root.h tables plugged in) and S12's scene side (KnightFight: the sidecar, the
// rig, the pose, the events); this file adds what she has and Gaius has not: the terrain
// words under the player and under her root, her nav graph (routes, a clear test over her
// ground), her ground (the boss layer, else the layered heightmap), the additive layers
// (requested from her graph, composed into her final palette), the neck trigger, the rockfall
// feed and the death's root drop. Every number is encounters/kirin.h's or the data's.
//
// Two layers again: KirinState + kirin_think are pure (no Scene) — a unit test drives them on
// the arena's heightmap, terrain and routes; KirinFight adds the scene's side.

// What her pure layer keeps beside the shared core.
struct KirinState {
    RouteGraph routes;            // kirin.route.json, every height snapped to her ground
    TerrainMap terrain;           // arena_04.terrain.json
    AddLayers layers;             // the additive pool
    std::vector<AddEdge> addEdges;  // KIRIN_ADD (scr_additive_edges)
    std::vector<std::string> orients;  // this frame's additive orients (requested in order)
};

// Loads her graph (scr_graph_load with scr_registry_kirin, scr_attack_attain and
// scr_move_back_bind on `judgeDefaults`, then scr_kirin_check against `judgeNames`), her
// routes and terrain, `seconds` per clip, her rootless clips (kirin_root's kNoRoot and
// kUnusable: never played raw) and the stand, and snaps every route height to her ground on
// `ground`. False with `why` naming the culprit.
bool kirin_core_load(KnightCore& core, KirinState& k, const std::string& graphJson,
                     const std::string& routeJson, const std::string& terrainJson,
                     const std::vector<std::string>& clipNames, const std::vector<float>& seconds,
                     const std::vector<EventsVolume>& judgeDefaults,
                     const std::vector<std::string>& judgeNames, const std::string& standClip,
                     const Heightmap& ground, std::string& why);

// The opening state: the core's (the retry start 2 -> 3, the stand; the runtime's RouteState
// replaced, so closed links reopen) and an empty layer pool.
void kirin_core_reset(KnightCore& core, KirinState& k, float yawDeg);

// A terrain hit as the game's mask word `class << 48 | owner` (D8); 0 when nothing answers.
uint64_t kirin_attr_word(const TerrainHit& hit);

// Her ground under `p` (§13.2): the boss-ground layer's height where it answers (at or below
// p.y + 2 m), else the layered heightmap's floor within kGroundStep of p.y; NaN when neither.
float kirin_ground(const KirinState& k, const Heightmap& ground, Vec3 p);

// Whether she can walk straight from `from` to `to` over her ground: sampled every
// kWalkSample m, each sample's ground within kGroundStep of the last (the move rule). False
// with `stop` the last walkable sample.
bool kirin_walkable(const KirinState& k, const Heightmap& ground, Vec3 from, Vec3 to, Vec3& stop);

// One frame without the scene. The caller fills the world's player side (position, ground
// attribute, attachment, whistle, hit, hp); this fills hers — her root, her ground word, the
// routes, the layer query, the ground and the clear test — runs the shared think, the
// additive layers (step -> request, none at dt 0) and moves `root` by the think's travel over
// her ground (a refused step holds her). `yawDeg` is the core's.
KnightThink kirin_think(KnightCore& core, KirinState& k, ScrWorld& world, const Heightmap& ground,
                        Vec3& root, float dt);

// --- The scene's side ---------------------------------------------------------

struct KirinFight {
    KnightFight base;   // the shared scene side: core, sidecar, rig, pose, events (S12)
    KirinState k;
    std::array<float, encounters::kirin_detail::W::Count> spent{};  // damage per strike target
    int struckKind = 0;     // the landed strike's hit kind (next frame's world.hitKind)
    float baseLift = 0.0f;  // the entity's grounding lift (the death's root drop adds to it)
    int deathLoopClip = -1;  // kDeathLoopClip in the graph: held after the death clip
    float rockfallFed = 0.0f;  // seconds fed to her rockfall timer this run (the selftest's line)
};

// Loads <libraryDir>/behaviour/KIRIN.graph.json, her routes and terrain, the entity glb's
// sidecar, maps the graph's clips onto the entity playback, resolves the rig's joints, and
// checks the manifest's placement and player start against kArena. False with `why`.
bool kirin_fight_load(KirinFight& fight, const Scene& scene, std::size_t entityRig,
                      const Playback& entity, const Heightmap& ground, const std::string& libraryDir,
                      const std::string& entityGlb, const std::string& standClip, Vec3 placement,
                      Vec3 playerStart, std::string& why);

// The opening state (the stage's start and R): her placement and facing, the graph from the
// retry start, the stand, the layers empty, the targets whole, no events in flight.
void kirin_fight_reset(KirinFight& fight, Encounter& encounter, Playback& entity);

// One frame before the poses: the world (the player's terrain word, the S8 whistle press,
// last frame's strike, the judge volumes on last frame's pose), kirin_think, the entity's
// clip and time. Defeated, the death sequence owns the clip: the graph enters its death mode
// (5) once, and the root drops kDeathRootY over the death clip.
KnightThink kirin_fight_step(KirinFight& fight, Encounter& encounter, Playback& entity,
                             const Heightmap& ground, const PlayerState& player,
                             const PlayerCombat& combat, bool whistleHeld, float dt);

// After update_all_poses: her palette with the switch's blend and her additive layers (the
// caller uploads it; the bands and the targets are posed from it).
void kirin_fight_pose(KirinFight& fight, const Scene& scene, std::size_t entityRig,
                      const Playback& entity, float dt, std::vector<Mat4>& palette);

// After the player's step: the sidecar's damage on the player (none on a rider, §13.5), the
// rockfall feed from the fired ground-destruction spawns. Returns the rows that landed.
std::vector<DamageHit> kirin_fight_events(KirinFight& fight, PlayerState& player,
                                          PlayerCombat& combat, const CombatTuning& tuning,
                                          float dt, StepEvents* events);

// A strike on target `weakpoint`: the head sigil takes at most what is left of its pool (HP 0
// defeats her); the neck trigger (no pool) takes no HP. Either is next frame's hit for the
// graph (its region, kind kTriggerHitKind). Dead, nothing counts.
KnightHit kirin_fight_hit(KirinFight& fight, Encounter& encounter, Playback& entity,
                          const EntityClips& clips, int weakpoint, float damage);

// The shake this frame. ponytail: no drain table for her refuse clips yet (Gaius's rule needs
// her bones' angular speeds measured) — the shake-off plays, it drains nothing (labelled gap).
ShakeOff kirin_fight_shake(const KirinFight& fight);
