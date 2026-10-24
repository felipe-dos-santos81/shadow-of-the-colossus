#pragma once

#include <bitset>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "math3d.h"
#include "route.h"

// A colossus's own behaviour, run from the game's mode graph (S12 Task 4, design
// §13). The exporter (tools/re/scr/graph_export.py) writes the eboot's
// scr_data_manager as `sotc-scr-graph/0` JSON; scr_graph_load parses it strictly
// and scr_step walks it one frame at a time. Pure: no SDL, no GPU, no clock; the
// caller fills a ScrWorld per frame and plays the controls the step requests.
//
// The contracts are the recon's (binary, .superpowers/sdd/2026-10-23-s12-recon/
// graph/findings.md): the walker FUN_0082c080 is §1 (steps 6-10, 11 and 13 run
// here), the condition evaluator FUN_0082d9f0 is §2, the JSON is §5. The boss's
// predicates and action kinds are not here: a ScrRegistry (filled per boss, Task 5)
// supplies them, and loading fails on any fn it does not know (never a silent skip).

// The game flags game_flag/set_flag index (S13 Task 14: KIRIN's 587/588, POSEIDON's 619). Guess: the
// game's flag array is not decoded; 1024 holds every index the 16 exports use.
inline constexpr size_t kScrFlags = 1024;

// The `sub` predicate (data 0x832610): the evaluator itself evaluates the
// condition its h50 slot names, so no registry needs to carry it.
inline constexpr uint32_t kScrSubFn = 0x832610;

// One argument slot of a clause or an action ("f1c", "h50", "i10", "q28", ...: the
// export names a slot by its record offset). The loader resolves what it can.
struct ScrArg {
    std::string slot;
    bool null = false;   // a null handle
    double num = 0.0;    // a number, a bool, or a "0x..." hex word (64-bit; exact while it spans <= 53 bits)
    std::string text;    // an unresolved handle ("PLAYER:ph_motion_def[604]") or a ref's name
    int clip = -1;       // {motion, name, clip} -> index in the loader's clipNames
    int mode = -1;       // {mode, name} -> mode id
    int ctrl = -1;       // {ctrl, bit, index} -> the control's request bit (0..63)
    int cond = -1;       // "TABLE:Cn" -> index in ScrGraph::conditions
    int actions = -1;    // "TABLE:Fn" -> index in ScrGraph::actions
};

struct ScrClause {
    std::string pred;  // the exporter's name for fn (informational)
    uint32_t fn = 0;   // the predicate's eboot VA: the registry key
    int expect = 1;    // the clause holds when (u8)result == expect (1 must hold, 0 NOT)
    std::vector<ScrArg> args;
};

// The arg in `slot`, or nullptr.
const ScrArg* scr_arg(const std::vector<ScrArg>& args, const char* slot);

// The combiner comes from the condition's first record only (§2): 1 AND, 2 OR,
// anything else is false.
enum class ScrOp { False, And, Or };

struct ScrCondition {
    std::string key;    // "KNIGHT:C41"
    std::string label;
    ScrOp op = ScrOp::And;
    std::vector<ScrClause> clauses;
};

struct ScrAction {
    std::string act;   // the exporter's name for fn (informational)
    uint32_t fn = 0;   // the action kind's eboot VA: the registry key
    std::vector<ScrArg> args;
};

struct ScrActionList {
    std::string key;   // "KNIGHT:F16"
    std::vector<ScrAction> records;
};

struct ScrTransition {
    int cond = -1;     // -1: a null condition, which is true
    int to = -1;
    float minSinceTransition = 0.0f;  // +0x20: a minimum on timer 2
};

// The mode's scr_slowstatus as exported; Task 5's locomotion reads it.
struct ScrLocomotion {
    std::string row;
    int kind = -1;
    int p0c = 0;
    std::string attack;         // attackdata handle, "" when null
    std::string attackTrigger;  // "" when null
    std::vector<float> attain;
    std::vector<float> perception;  // scr_relposparam, the 10 floats (§2)
    std::string gaze;
    // S13 (docs/notes/s13-schemas.md; optional keys, absent -> these defaults).
    bool refPlayer = false;  // +04: the route GP nearest the player (else the boss)
    bool track = false;      // +08: tracking
    std::string judge;       // +78 handle, "" when null
    float p60 = 0.0f, p64 = 0.0f, p68 = 0.0f, p6c = 0.0f, p70 = 0.0f, p74 = 0.0f, p98 = 0.0f, p9c = 0.0f;
    // The `judge` volume's MOVE_BACK sector (radius, half width in degrees), not exported: bound from the
    // events sidecar (scr_move_back_bind, S13 Task 14). 0: unbound, the library's Gaius sector.
    float backRadius = 0.0f, backHalfDeg = 0.0f;
};

struct ScrMode {
    int id = -1;
    std::string name;
    ScrLocomotion locomotion;
    int enterActions = -1;     // +0x40
    int enterCtrl = -1;        // +0x50, a request bit
    int enterWaitClip = -1;    // +0x60
    int updateActions = -1;    // +0x20
    int updateCtrl = -1;       // +0x30
    int exitActions = -1;      // +0x70
    int exitCtrl = -1;         // +0x80 when a script-ctrl-def
    std::string exitDemo;      // +0x80 when a scr_demo_def (FUN_00836520, not decoded)
    int exitWaitClip = -1;     // +0x90
    std::vector<ScrTransition> transitions;  // in order; the sentinel is not exported
};

struct ScrRoot {
    int startMode = -1;
    int retryStartMode = -1;
    int retryCtrl = -1;
    int deathMode = -1;
    int deathControlMode = -1;  // the boss's motion-layer control-mode index; -1 none
    int callbackActions = -1;
    int everyFrameActions = -1;
    std::vector<float> anger;   // scr_angry_status {decay, a, b, flag}; empty when null
    int kind = -1;              // S13: the root kind (+0x00, guess: census16 Open); -1 when absent
};

struct ScrAssign {
    std::string layer;   // "base" / "additive"
    std::string when;    // the control-def handle (R2 decodes its records)
    int ctrl = -1;       // request bit, -1 when the row has no ctrl
    std::string ctrlName;
    std::string orient;
    int nextControlMode = -1;
};

struct ScrControlMode {
    std::string name;
    std::vector<ScrAssign> assign;
};

struct ScrMotionEdge {
    std::string table;
    int from = -1;  // clip index, -1 when the export has null
    std::string orient;
    int to = -1;
    float p30 = 0.0f;
    float p34 = 0.0f;
    // S13 (optional): the reference clip (motion-def +0x98; an additive edge's frame-0 reference),
    // the speed flag (+0x60 bit 1) and the blend-out frames (+0x218).
    int ref = -1;  // clip index, -1 when null or absent
    bool speed = false;
    float blendOut = 0.0f;
    int fromRef = -2;  // the `from` motion-def's +0x98 clip (S13 Task 14): -1 null, -2 absent (any)
};

// One control-def record (R2: the 0x68-byte records up to opcode 1, docs/notes/s12-recon.md).
struct ScrCdefRecord {
    int op = 0;                     // +00
    float min = 0.0f, max = 0.0f;   // +04 / +10
    ScrArg handle;                  // +38, resolved as an argument (slot "handle"): ctrl bit, clip, mode or text
    uint64_t mask = 0, value = 0;   // +48 / +50
    int expect = 1;                 // +58: the record holds when (result == expect)
};

// One scr_dprelate row (data 0x22d37e0 for KIRIN): the player's ground word masked equals
// `value` and `cond` holds -> `route`.
struct ScrDprelateRow {
    uint64_t mask = 0, value = 0;
    std::string object;  // "" when null
    int cond = -1;       // index in ScrGraph::conditions, -1 null
    int route = -1;
};

// One scr_attackdata row (0x100 B; census16 findings "Attackdata export"). Handles that name
// nothing the app resolves stay text ("" when null).
struct ScrAttackRow {
    std::string label;
    std::string chain;              // +08
    float chainPct = 0.0f;          // +18
    std::string ctrlModes;          // +20
    bool isDefault = false;         // +30
    float reselectS = 0.0f;         // +34
    int sensing = 0;                // +38
    int cond = -1;                  // +40 condition index
    std::string fix, atk, altAtk;   // +50 / +60 / +70 judge names
    int altCond = -1;               // +80 condition index
    std::string attain;             // +90 judge name
    int issue = -1, issue2 = -1;    // +a0 / +b0 request bits
    int motion = -1;                // +c0 clip index
    std::string aimBone, aimVolume; // +d0 / +e0 (the foot-IK aim: a gap, spec §13.4)
    float f0 = 0.0f;                // +f0
    float cooldownS = 0.0f;         // +f4
    bool issueArrived = false;      // +f8
    // The `attain` volume's cylinder in his frame (glb x, z; radius), not exported: bound from the
    // events sidecar at load (scr_attack_attain, S13 Task 5). 0 until bound.
    float attainX = 0.0f, attainZ = 0.0f, attainR = 0.0f;
};

struct ScrGraph {
    std::string boss;
    ScrRoot root;
    std::vector<ScrMode> modes;  // modes[i].id == i
    std::vector<ScrCondition> conditions;
    std::vector<ScrActionList> actions;
    std::vector<ScrControlMode> controlModes;  // indexed by control-mode number
    std::vector<ScrMotionEdge> motionGraph;
    std::vector<std::string> clipNames;  // the loader's clipNames: ScrArg::clip etc. index these
    // S13 (optional keys, absent -> empty): keyed by the handle text exactly as exported (a
    // control-assign row's `when`, an action's dprelate handle, a slowstatus `attack`).
    std::unordered_map<std::string, std::vector<ScrCdefRecord>> controlDefs;
    std::unordered_map<std::string, std::vector<ScrDprelateRow>> dprelate;
    std::unordered_map<std::string, std::vector<ScrAttackRow>> attackdata;
};

// Everything the predicates read this frame. App conventions (metres, degrees, a
// bearing + = the boss's left, recon R5). Task 5 adds fields as its predicates
// need them; the query callbacks are std::function so the app binds its scene.
struct ScrWorld {
    bool playerPresent = true;
    Vec3 playerPosition{0.0f, 0.0f, 0.0f};
    Vec3 playerGroundPosition{0.0f, 0.0f, 0.0f};
    bool playerAttached = false;
    int playerAttachedRegion = 0;   // contact attr & 0x3f0
    bool playerRiding = false;      // S13: on a horse (player_riding 832e30)
    int playerMotion = -1;          // PLAYER:ph_motion_def index
    bool playerMotionStarted = false;
    int playerControlMode = -1;     // PLAYER_CONTROL_MODE:control-mode index

    bool hit = false;               // a landed hit this frame
    int hitRegion = 0;              // hit attr & 0x3f0
    int hitKind = -1;

    int bossMotion = -1;            // clip index (sub+0x260)
    int bossPreviousMotion = -1;    // sub+0x268
    bool bossMotionSwitched = false;
    int bossControlMode = -1;       // sub+0x9c8

    float distanceHorizontal = 0.0f;  // W+0x4b20
    float distance3d = 0.0f;          // W+0x4b24
    float bearingDeg = 0.0f;          // to the player, + = his left
    float hp = 1.0f;
    float hpMax = 1.0f;             // S13: hp_pct_* read 100·hp/hpMax

    std::bitset<kScrFlags> flags;

    // Gaius (S12 Task 5, scr_predicates.h). His root on his ground and his facing in
    // the app convention (forward = (sin yaw, 0, cos yaw); scr_world_relate derives
    // the distances and the bearing above from these).
    Vec3 bossPosition{0.0f, 0.0f, 0.0f};
    float bossYawDeg = 0.0f;
    bool bossOnGround = true;          // R2: PLAYER_LANDING / NO_PLAYER_SCAFFOLDING control-defs
    // S13 D8: the ground attribute under the player (on foot) and under the boss's root, as the
    // game's mask words `class << 48 | owner` (clb findings §3). Gaius's glue sets the player's to
    // owner 0x800000 on the arena ground (the S12 §13.3 stand-in, authored).
    uint64_t playerGroundAttr = 0;
    uint64_t bossGroundAttr = 0;
    const RouteGraph* routes = nullptr;  // S13: the boss's route graph; null without one
    // S13: true when clip `clip` plays in any of the boss's layers (op 0x56 / boss_motion_in_layers);
    // null: the base motion only.
    // Deviation (labelled, S13 Task 14): by clip, where the game tests motion-def identity (FUN_006357f0); two defs
    // of one clip (KIRIN's neck dummy kirin[65] and swing start kirin[66]) read alike.
    std::function<bool(int clip)> bossLayerPlaying;
    // True when the player's point is inside the judge volume `name` (ruling R-B: the
    // app binds events_player_in_judge). Null: never inside.
    std::function<bool(std::string_view name)> playerInJudge;

    // The ground height under (x, z); false when there is none.
    std::function<bool(float x, float z, float& height)> groundAt;
    // A segment from -> to against the scene; true and the hit point on a hit.
    std::function<bool(const Vec3& from, const Vec3& to, Vec3& hitPoint)> ray;
};

// The walker's state (W+0x4b34..+0x4b5c), then the rest of the boss work block the
// boss's own predicates and actions keep (Task 5, scr_predicates.h).
struct ScrRuntime {
    int current = -1, pending = -1, previous = -1;
    bool entered = true, transitioned = false;
    float timer1 = 0.0f;  // time in mode: survives a self-transition
    float timer2 = 0.0f;  // since the last transition
    bool exitDeferred = false;  // a switch waits on the old mode's exit motion
    float dt = 0.0f;            // this frame's time (DAT_0325de90) for actions that time themselves

    // Perception (FUN_0083a6f0): the mode's scr_relposparam (W+0x4b00, 10 floats; empty
    // before the first switch) and the `sensed` latch W+0x4aec.
    std::vector<float> relpos;
    bool sensed = false;
    bool seenNow = false, nearNow = false;  // S13: this frame's raw seen W+0x4af0 / near W+0x4af1
    float seenFor = 0.0f, nearFor = 0.0f, unsensedFor = 0.0f;
    Vec3 lastKnown{0.0f, 0.0f, 0.0f};  // W+0x4ac0

    // Locomotion: the kind W+0x4730, the point goal W+0x54 and arrival W+0x3d6.
    int locoKind = -1;
    int moveParam = 0;         // W+0x3dc (slowstatus +0c / move_param)
    float arriveRadius = 0.0f;
    bool hasGoal = false, arrived = false;
    Vec3 goal{0.0f, 0.0f, 0.0f};
    float steerDeg = 0.0f;     // this frame's turn toward the goal (+ = left), for the caller's walk
    bool aimAtPlayer = false;  // look_if_motion held this frame

    // The attack runner (FUN_0085ab40): armed table W+0x4b78 (S13: the mode whose slowstatus
    // `attack` handle armed it, a graph.attackdata key; -1 none), row W+0x4b80 (an index into
    // that table's rows), time in row +0x4b8c, since attack +0x4b90, cooldown +0x4b94.
    int attackTable = -1, attackRow = -1;
    float timeInRow = 0.0f, timeSinceAttack = 0.0f, cooldown = 0.0f;

    uint32_t bossBits = 0;                // the boss-local work word (Gaius: W'+0x4c80)
    float bossTimer[2] = {0.0f, 0.0f};    // W'+0x4c84, +0x4c88 (Gaius's floats; Phaedra's +0x4c88 is a byte: bossBits)
    std::bitset<kScrFlags> flags;         // flags the graph sets (set_flag)
    int controlMode = -1;                 // the control-mode scr_control_clip moved to
    int controlEdge = -1;                 // the motionGraph edge it took (p30/p34: its blend data)
    float anger = 0.0f;                   // S13: the anger model's value (root.anger; FUN_0082aef0)
    RouteState route;                     // S13: the route follower (route.h)
};

struct ScrStepOut {
    std::vector<int> controls;  // request bits, in request order, each once
    bool modeChanged = false;   // a switch ran this frame (a self-transition re-enters)
    int mode = -1;              // the current mode after the step
    bool deathForced = false;
};

struct ScrRegistry;

// What an action handler may touch: it reads the world and the graph, evaluates
// conditions, runs action lists (if_run) and requests controls (if_ctrl).
struct ScrCtx {
    const ScrGraph& graph;
    const ScrRegistry& registry;
    ScrRuntime& runtime;
    const ScrWorld& world;
    ScrStepOut& out;
};

using ScrPredicateFn = bool (*)(const ScrClause&, const ScrWorld&, const ScrRuntime&);
using ScrActionFn = void (*)(const ScrAction&, ScrCtx&);

// fn VA -> handler. Empty here; Task 5 fills it per boss.
struct ScrRegistry {
    std::unordered_map<uint32_t, ScrPredicateFn> predicates;
    std::unordered_map<uint32_t, ScrActionFn> actions;
    // Applies mode `mode`'s slowstatus at a switch (FUN_0082c080 after the exit
    // actions, before the enter actions; never on a deferred switch). rt.current is
    // still the old mode. Null: nothing to apply.
    void (*enterMode)(int mode, ScrCtx& ctx) = nullptr;
    // S13 D4: fns registered as labelled no-ops (also in predicates/actions); the coverage
    // report counts them apart from the implemented ones.
    std::unordered_set<uint32_t> gaps;
    // The locomotion kinds this registry runs; the loader refuses a mode of any other kind.
    std::unordered_set<int> kinds;
    // The step for kinds the library does not run itself (Task 8 installs it for 2-9). Null: none.
    void (*locomotion)(int kind, ScrCtx& ctx, float dt) = nullptr;
};

// Parses `json` into `out`. Fails (false, `error` naming the culprit) on a wrong
// format tag, a malformed document, an unknown predicate fn, action kind or control-def
// opcode (kScrCdefOps), a mode locomotion kind not in `registry.kinds`, a clip missing from `clipNames` (lower-case
// stems; edge refs and control-def handles included), or a dangling
// mode/condition/action reference.
bool scr_graph_load(const std::string& json, const std::vector<std::string>& clipNames,
                    const ScrRegistry& registry, ScrGraph& out, std::string& error);

// Resets `rt` and queues the root's start mode (the retry start mode on a retry):
// pending = start, current = none, entered = 1.
void scr_runtime_start(const ScrGraph& graph, ScrRuntime& rt, bool retry);

// The condition at `cond` (§2); -1 (a null handle) is true.
bool scr_eval(const ScrGraph& graph, const ScrRegistry& registry, int cond, const ScrRuntime& rt,
              const ScrWorld& world);

// Runs the action list at `actions` in record order (FUN_0082bfb0); -1 is a no-op.
void scr_run(ScrCtx& ctx, int actions);

// Adds `bit` to the frame's requests (the actor+0xbd0 word); -1 is a no-op.
void scr_request(ScrStepOut& out, int bit);

// One frame of the walker (§1). `registry` must be the one the graph was loaded
// with: the step looks every fn up unchecked (the load proved them present).
ScrStepOut scr_step(const ScrGraph& graph, const ScrRegistry& registry, ScrRuntime& rt,
                    const ScrWorld& world, float dt);
