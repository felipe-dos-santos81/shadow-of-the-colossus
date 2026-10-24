#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

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
    std::vector<char> loops;     // per graph clip: the root series wraps (knight_root.h)
    int standClip = -1;
    int standControlMode = -1;   // the stand's else edge's control-mode (the retry start's)
    KnightMotion motion;
    float yawDeg = 0.0f;         // his facing (the entity yaw), without a playing turn's heading
    Vec3 boundCentre{};          // the clamp on his root and his goal (D9)
    float boundRadius = 0.0f;
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

// --- The scene's side ---------------------------------------------------------

struct KnightFight {
    KnightCore core;
    EventsFile events;
    std::vector<int> playIndex;   // graph clip -> the entity playback's clip (-1 none)
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
    // The run's record (the selftest's lines).
    float maxBoundDistance = 0.0f;  // his root's farthest from the bound centre (s12-bound)
    std::vector<std::string> judgeFired;  // judge volumes that held the player this frame
    KnightThink think;
    int deathMode = -1;           // the mode the graph entered at HP 0
    int deathGraphClip = -1;      // the clip its controls chose there (the death sequence plays desc's)
};

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
// pose for the judge volumes, his ground), the think, his travel over the ground
// clamped into the bound, the entity's clip and time. Defeated, the death sequence owns
// the clip: the graph enters its death mode once and he travels the death clip's root.
KnightThink knight_fight_step(KnightFight& fight, Encounter& encounter, Playback& entity,
                              const Heightmap& ground, const PlayerState& player,
                              const PlayerCombat& combat, float dt);

// After update_all_poses: the entity's palette re-posed with the switch's blend (the
// caller uploads it), and the joints' world matrices cached for the events and the
// next frame's judge volumes.
void knight_fight_pose(KnightFight& fight, const Scene& scene, std::size_t entityRig,
                       const Playback& entity, float dt, std::vector<Mat4>& palette);

// After the player's step: the clip's events (fired effects start their spawned
// damage), his damage rows and the spawned rows against the player (the highest
// amount lands: player_take_damage, heavy on kHeavyReaction; §13.5 holds back every row
// from an attached player but the throw-off, which releases him next frame). Returns
// the rows that landed this frame.
std::vector<DamageHit> knight_fight_events(KnightFight& fight, PlayerState& player,
                                           PlayerCombat& combat, const CombatTuning& tuning,
                                           float dt, StepEvents* events);

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
