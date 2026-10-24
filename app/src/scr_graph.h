#pragma once

#include <bitset>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "math3d.h"

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

// The `sub` predicate (data 0x832610): the evaluator itself evaluates the
// condition its h50 slot names, so no registry needs to carry it.
inline constexpr uint32_t kScrSubFn = 0x832610;

// One argument slot of a clause or an action ("f1c", "h50", "i10", "q28", ...: the
// export names a slot by its record offset). The loader resolves what it can.
struct ScrArg {
    std::string slot;
    bool null = false;   // a null handle
    double num = 0.0;    // a number, a bool, or a "0x..." hex string
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
};

struct ScrGraph {
    std::string boss;
    ScrRoot root;
    std::vector<ScrMode> modes;  // modes[i].id == i
    std::vector<ScrCondition> conditions;
    std::vector<ScrActionList> actions;
    std::vector<ScrControlMode> controlModes;  // indexed by control-mode number
    std::vector<ScrMotionEdge> motionGraph;
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

    std::bitset<512> flags;

    // The ground height under (x, z); false when there is none.
    std::function<bool(float x, float z, float& height)> groundAt;
    // A segment from -> to against the scene; true and the hit point on a hit.
    std::function<bool(const Vec3& from, const Vec3& to, Vec3& hitPoint)> ray;
};

// The walker's state (W+0x4b34..+0x4b5c). Task 5 adds its own fields.
struct ScrRuntime {
    int current = -1, pending = -1, previous = -1;
    bool entered = true, transitioned = false;
    float timer1 = 0.0f;  // time in mode: survives a self-transition
    float timer2 = 0.0f;  // since the last transition
    bool exitDeferred = false;  // a switch waits on the old mode's exit motion
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
};

// Parses `json` into `out`. Fails (false, `error` naming the culprit) on a wrong
// format tag, a malformed document, an unknown predicate fn or action kind, a clip
// missing from `clipNames` (lower-case stems), or a dangling mode/condition/action
// reference.
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

// One frame of the walker (§1).
ScrStepOut scr_step(const ScrGraph& graph, const ScrRegistry& registry, ScrRuntime& rt,
                    const ScrWorld& world, float dt);
