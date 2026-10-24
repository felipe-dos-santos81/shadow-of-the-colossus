#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "additive.h"
#include "anim.h"
#include "app.h"
#include "encounter_rt.h"
#include "encounters/knight.h"
#include "heightmap.h"
#include "objanim_events.h"
#include "player_state.h"
#include "scr_graph.h"
#include "scr_predicates.h"

// Gaius's fight at runtime (S12 Task 9, design D3–D7, D9, D12, §13.8): glue, not a fight
// file. It fills a ScrWorld, runs his exported graph (scr_knight_step), turns the
// requested controls into clips through the exported motion graph (scr_control_clip and
// the else edges), moves him by his measured root (knight_root.h) over the ground within
// his bound, and applies the events sidecar (his damage on the player, the spawned
// footstep damage, the judge volumes the attack runner reads). Every number is
// encounters/knight.h's (labelled) or the data's; nothing here is keyed on his identity
// beyond that table.
//
// Two layers: KnightCore is pure (no Scene) — the graph, its runtime and the motion
// layer — so a unit test drives it from a scripted ScrWorld; KnightFight adds the
// scene's side (the playback, the pose, the events, the sigils).

// The boss's base motion layer: the clip the graph's controls led to and its clock.
struct KnightMotion {
    int clip = -1;            // graph clip index (ScrGraph::clipNames)
    float time = 0.0f;        // seconds into it
    int previous = -1;        // the clip before the last switch
    bool switched = false;    // a switch ran last frame (the world's bossMotionSwitched)
    int controlMode = -1;     // the boss's control-mode (the world's bossControlMode)
    int instance = 0;         // bumps on every start or loop wrap (the events' bookkeeping)
    float blendSeconds = 0.0f;   // the last switch's blend (edge p34 / kBlendFps)
    float blendElapsed = -1.0f;  // s into it; -1 = no blend running
    bool held = false;           // the clip holds its last frame (a rootless target refused)
};

struct KnightCore {
    ScrRegistry registry;
    ScrGraph graph;
    ScrRuntime rt;
    std::vector<float> seconds;  // per graph clip: its length (s)
    std::vector<char> rootless;  // per graph clip: no root series (never played raw)
    int standClip = -1;
    int standControlMode = -1;   // the stand's else edge's control-mode (the retry start's)
    KnightMotion motion;
    float yawDeg = 0.0f;         // his facing (the entity yaw), without a playing turn's heading
    Vec3 boundCentre{};          // the clamp on his root and his goal (D9)
    float boundRadius = 0.0f;
    // S13: the boss's root tables, so Phaedra's glue (kirin_fight) runs this motion layer on
    // hers. Null: knight_root.h's. `turnAt` is a turn clip's heading at t (0 for any other clip).
    Vec3 (*rootAt)(std::string_view clip, float t) = nullptr;
    float (*turnAt)(std::string_view clip, float t) = nullptr;
    float blendFps = encounters::knight_detail::kBlendFps;  // an edge's p34 frames per second
};

// What one think did (the selftest's lines read it).
struct KnightThink {
    ScrStepOut step;
    int modeBefore = -1;
    int clipBefore = -1, clipAfter = -1;
    int edge = -1;           // the motion-graph edge taken this frame (-1 none)
    bool viaElse = false;    // the clip ended with no request: its else edge
    bool turnCut = false;    // a playing turn cut (its turn bits stopped)
    bool wrapped = false;    // a loop wrapped
    bool refused = false;    // a rootless target refused (the clip holds)
    Vec3 travel{};           // his root's world x/z travel this frame (y 0)
    float yawAddedDeg = 0.0f;  // a turn's yaw, added at its end or cut
};

// Loads the graph (scr_graph_load with scr_registry_knight, then scr_knight_check),
// `seconds` per name of `clipNames` (the glb's), the rootless/loop flags from
// knight_root.h and the stand. False with `why` naming the culprit.
bool knight_core_load(KnightCore& core, const std::string& graphJson,
                      const std::vector<std::string>& clipNames, const std::vector<float>& seconds,
                      const std::vector<std::string>& judgeNames, const std::string& standClip,
                      std::string& why);

// The opening state (§13.8): the graph from its retry start mode (3), the stand at t 0
// in the stand's control-mode, facing `yawDeg`.
void knight_core_reset(KnightCore& core, float yawDeg);

// `p` clamped into the disc of `radius` about `centre` (x/z; y kept): Review Focus 5.
Vec3 knight_clamp_bound(Vec3 p, Vec3 centre, float radius);

// One frame: fills the world's boss-motion fields (bossMotion/Previous/Switched,
// bossControlMode, bossYawDeg with a playing turn's heading) and its relations, runs
// scr_knight_step, advances the clip, and resolves the requests: a control's edge now
// (p30 >= 0) or at the clip's end (p30 -1), the else edge at an end with no request
// (else_orient_def[N] returns to control-mode N), a playing turn cut once its turn bits stop,
// a rootless target refused (the clip holds its last frame). His goal is clamped into
// the bound before and after the step. The root travel and a turn's yaw are returned
// (the caller moves him); `yawDeg` takes the yaw.
KnightThink knight_think(KnightCore& core, ScrWorld& world, float dt);

// The clip's name, "" for -1.
const char* knight_clip_name(const KnightCore& core, int clip);

// The else edge out of `clip` (its first; -1 none) and the control-mode it returns to (S13:
// Phaedra's load finds her stand's control-mode with it).
int knight_else_edge(const ScrGraph& graph, int clip, int& controlMode);

// Ruling R-N: the player's motion fields of the world. A whistle press this frame starts
// kWhistleMotion (ALWAYS:C120, the finger whistle); a Dead player is in
// kPlayerDeathControlMode (ALWAYS:C71). Otherwise no motion and no control-mode.
void knight_world_player(ScrWorld& world, bool whistlePressed, bool dead);

// A per-frame table at clip time `t` (frame = t · fps), linear between frames, clamped (the
// root tables' sampling; S13: Phaedra's tables too).
template <typename F>
auto knight_sample(int frames, float fps, float t, F at) {
    const float f = std::clamp(std::isfinite(t) ? t * fps : 0.0f, 0.0f, static_cast<float>(frames - 1));
    const int i = std::min(static_cast<int>(f), frames - 1);
    const int j = std::min(i + 1, frames - 1);
    const float u = f - static_cast<float>(i);
    return at(i) * (1.0f - u) + at(j) * u;
}

// --- The scene's side ---------------------------------------------------------

struct KnightFight {
    KnightCore core;
    EventsFile events;
    std::vector<int> eventsClip;  // graph clip -> events clip (-1 none)
    std::vector<std::string> jointNames;  // the rig's joints, in skin order
    std::vector<std::string> optBones;    // OptBone_* in glb joint order
    std::vector<Mat4> joints, jointsPrev;  // this frame's / last frame's joint world matrices
    Mat4 model = mat4_identity(), modelPrev = mat4_identity();  // his mesh model then
    bool havePose = false, havePrev = false;
    int hipJoint = -1;
    EventsClipState evNow, evPrev;
    EventsSpawnRunner spawns;
    RigBlend blend;
    int deathClip = -1;           // desc.death.clip in the graph
    Vec3 basePosition{};          // the manifest placement: R returns here
    float baseYawDeg = 0.0f;
    std::array<float, encounters::knight_detail::W::Count> spent{};  // damage dealt per sigil
    // The landed strike, for the next frame's world.hit (the graph's "I was attacked").
    bool struck = false;
    int struckRegion = 0;
    bool throwOff = false;        // a throw-off row landed: the next frame's ShakeOff releases
    bool whistleBefore = false;   // last frame's whistle key (R-N: a press is down now, up then)
    // The run's record (the selftest's lines).
    float maxBoundDistance = 0.0f;  // his root's farthest from the bound centre (s12-bound)
    float maxUnclampedDistance = 0.0f;  // ... before the clamp: where his travel would have taken him
    int clampedFrames = 0;              // frames the clamp moved him
    // This frame's damage rows that overlapped the player (his and the spawned), landed or
    // not: i-frames refuse a hit, they do not make a row miss (the dodge's evidence).
    std::vector<DamageHit> overlaps;
    std::vector<std::string> judgeFired;  // judge volumes that held the player this frame
    KnightThink think;
    int deathMode = -1;           // the mode the graph entered at HP 0
    int deathGraphClip = -1;      // the clip its controls chose there (the death sequence plays desc's)
};

// The sidecar against his rig, at load (finding 5/6: a bad row must not throw mid-frame,
// a sidecar without its map must not load silently harmless). Every clip row (damage,
// judge, effect, judge default) has a non-spawn parent whose `bone` is in `joints` and
// whose `optBone` indexes `optBones`; every spawned row's parent is the spawn or the
// entity; the file carries spawned objanims and at least one damage row with a def
// (tools/re/objanim's map reached the converter). False with `why` naming the row.
bool knight_events_check(const EventsFile& events, const std::vector<std::string>& joints,
                         const std::vector<std::string>& optBones, std::string& why);

// --- The scene side both colossi share (S13: Gaius's and Phaedra's loads and steps) ---

// The file at `path`; "" when it is missing.
std::string knight_read_text(const std::string& path);

// The manifest's placement (x, ground y, z) and player start (x, z) against the encounter
// table's (`spawn` is x, ground y, z). False with `why` naming `stage` and `table` to fix.
bool knight_check_arena(Vec3 placement, Vec3 playerStart, Vec3 spawn, float playerStartX, float playerStartZ,
                        const char* stage, const char* table, std::string& why);

// Before the core loads: the entity glb's sidecar into `fight.events`, the playback's clip
// names and lengths (the graph's clip indices are the playback's) and every judge name the
// sidecar carries. False with `why`.
bool knight_scene_sidecar(KnightFight& fight, const Playback& entity, const std::string& libraryDir,
                          const std::string& entityGlb, std::vector<std::string>& names,
                          std::vector<float>& seconds, std::vector<std::string>& judges, std::string& why);

// After the core loads: the events clip of each graph clip, `deathClip`, the rig's joints and
// OptBones, the sidecar checked against them (knight_events_check) and `hipJoint`. False with `why`.
bool knight_scene_bind(KnightFight& fight, const Scene& scene, std::size_t entityRig, const char* deathClip,
                       const char* hipJoint, std::string& why);

// The player's side of a step's world: the position, the layered floor under it within
// `groundStep`, the attachment and its band's region (`bandRegion`), the S8 whistle press and
// the death (R-N), last frame's strike (consumed), the HP, and the judge volumes on last frame's
// pose (`fight.judgeFired` cleared first). The ground attribute is the caller's.
void knight_world_player_side(KnightFight& fight, ScrWorld& world, const Encounter& encounter,
                              const Heightmap& ground, float groundStep, std::span<const int> bandRegion,
                              const PlayerState& player, const PlayerCombat& combat, bool whistleHeld);

// D12, once at the defeat: the graph's death (`thinkAtZero` until the root's death mode, at most
// 3 thinks) and the motion onto the death clip at t 0. Returns the last think.
KnightThink knight_enter_death(KnightFight& fight, const std::function<KnightThink()>& thinkAtZero);

// Loads <libraryDir>/behaviour/KNIGHT.graph.json and the entity glb's sidecar, maps the
// graph's clips onto the entity playback, resolves the rig's joints, and checks the
// manifest's placement and player start against kArena (x/z, his ground). False with `why`.
bool knight_fight_load(KnightFight& fight, const Scene& scene, std::size_t entityRig,
                       const Playback& entity, const std::string& libraryDir,
                       const std::string& entityGlb, const std::string& standClip,
                       Vec3 placement, Vec3 playerStart, std::string& why);

// The opening state (the stage's start and R): his placement and facing, the graph from
// the retry start, the stand on `entity`, the sigils whole, no events in flight.
void knight_fight_reset(KnightFight& fight, Encounter& encounter, Playback& entity);

// One frame before the poses: the world (the player, the arena ground, last frame's
// pose for the judge volumes, his ground; `whistleHeld` the S8 whistle key, R-N), the
// think, his travel over the ground clamped into the bound, the entity's clip and time. Defeated, the death sequence owns
// the clip: the graph enters its death mode once and he travels the death clip's root.
KnightThink knight_fight_step(KnightFight& fight, Encounter& encounter, Playback& entity,
                              const Heightmap& ground, const PlayerState& player,
                              const PlayerCombat& combat, bool whistleHeld, float dt);

// The end of a step (S13: shared with Phaedra's): the entity's clip at the motion's clock
// (not while `defeated`: the death sequence owns it), the events' clip state (a new instance
// on every start or wrap), and `think` kept as the frame's.
void knight_fight_sync(KnightFight& fight, Playback& entity, bool defeated, const KnightThink& think);

// After update_all_poses: the entity's palette re-posed with the switch's blend (the
// caller uploads it), and the joints' world matrices cached for the events and the
// next frame's judge volumes. S13: `layers` (Phaedra's additive layers) composes onto it;
// null (Gaius) is the path as it was.
void knight_fight_pose(KnightFight& fight, const Scene& scene, std::size_t entityRig,
                       const Playback& entity, float dt, std::vector<Mat4>& palette,
                       const AddLayers* layers = nullptr);

// The events' views of this frame (the posed colossus, last frame's as `prev`; the player,
// attached or not) — S13: Phaedra's glue reads the judge volumes through them too.
PoseView knight_pose_view(const KnightFight& fight);
PlayerView knight_player_view(const PlayerState& player, const PlayerCombat& combat);

// After the player's step: the clip's events (fired effects start their spawned
// damage), his damage rows and the spawned rows against the player (the highest
// amount lands: player_take_damage, heavy on kHeavyReaction; §13.5 holds back every row
// from an attached player but the throw-off, which releases him next frame). Returns
// the rows that landed this frame.
// S13: the two reaction readings are parameters (Phaedra passes hers; a value no row
// carries disables the throw-off).
std::vector<DamageHit> knight_fight_events(KnightFight& fight, PlayerState& player,
                                           PlayerCombat& combat, const CombatTuning& tuning,
                                           float dt, StepEvents* events,
                                           float heavyReaction = encounters::knight_detail::kHeavyReaction,
                                           float throwOffReaction = encounters::knight_detail::kThrowOffReaction);

// A strike's damage on weakpoint `weakpoint` (the band's shell): at most what is left of
// its pool (kWeakpointPool); HP 0 defeats him (the death clip starts, the graph's death
// follows next frame). Dead, nothing counts.
struct KnightHit {
    int weakpoint = -1;
    float damage = 0.0f;
    bool spent = false;   // the sigil's pool was already empty
    bool killed = false;
};
KnightHit knight_fight_hit(KnightFight& fight, Encounter& encounter, Playback& entity,
                           const EntityClips& clips, int weakpoint, float damage);

// The shake this frame: kShakes' drain inside the playing refuse clip's window, and the
// throw-off's release.
ShakeOff knight_fight_shake(const KnightFight& fight);

// The graph's mode name ("Knight_route_patrol"), "none" for -1.
const char* knight_mode_name(const KnightFight& fight, int mode);
