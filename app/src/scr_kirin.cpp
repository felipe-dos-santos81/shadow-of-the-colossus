#include "scr_kirin.h"

#include <algorithm>

#include "scr_cdef.h"
#include "scr_library.h"

// Sources (labels on each value): S13's recon `graph/findings.md` §2 (her locals, decompiled
// dec1/dec3) and `additive/findings.md` §1; the add-row queue is binary FUN_006329b0 /
// FUN_00634f80 (decompiled for this task: every firing row queues its orient each frame).

namespace {

// Her boss-local fns (eboot VAs; graph findings §2).
enum : uint32_t {
    kRockfallRunning = 0x6fdf70, kModeCycle = 0x6fdfd0,
    kRockfallTick = 0x6fdf20, kRockfallArm = 0x6fdf90, kRockfallDisarm = 0x6fdfb0,
    kRockfallCave = 0x6fccf0, kRockfallHill = 0x6fd5a0, kProxForce = 0x6fc8a0, kProxClear = 0x6fc8c0,
};

constexpr uint32_t kRockfallArmed = kScrKirinRockfallArmed;

float argf(const std::vector<ScrArg>& args, const char* slot) {
    const ScrArg* a = scr_arg(args, slot);
    return a == nullptr ? 0.0f : static_cast<float>(a->num);
}

// FUN_006fdf70: the rockfall timer runs (stomps feed it while armed; graph findings §2).
bool p_rockfall_running(const ScrClause&, const ScrWorld&, const ScrRuntime& rt) { return rt.bossTimer[0] > 0.0f; }

// FUN_006fdfd0: (int)timer1 % i10 < i14 (KIRIN {30, 5}: 5 s of every 30 s). i10 > 0: scr_kirin_check.
bool p_mode_cycle(const ScrClause& c, const ScrWorld&, const ScrRuntime& rt) {
    return static_cast<int>(rt.timer1) % static_cast<int>(argf(c.args, "i10")) < static_cast<int>(argf(c.args, "i14"));
}

void a_rockfall_tick(const ScrAction&, ScrCtx& ctx) {  // FUN_006fdf20: −dt, floored at 0
    ctx.runtime.bossTimer[0] = std::max(0.0f, ctx.runtime.bossTimer[0] - ctx.runtime.dt);
}
void a_rockfall_arm(const ScrAction&, ScrCtx& ctx) { ctx.runtime.bossBits |= kRockfallArmed; }      // FUN_006fdf90
void a_rockfall_disarm(const ScrAction&, ScrCtx& ctx) { ctx.runtime.bossBits &= ~kRockfallArmed; }  // FUN_006fdfb0

// Gaps (D4): the rock spawners need rock objects and their LWS animations (graph findings §5:
// "hard; fallback no-op"); the prox latch's reader is not settled and no fight effect was found.
void a_gap(const ScrAction&, ScrCtx&) {}

void enter_mode(int mode, ScrCtx& ctx) {
    scr_library_enter_mode(mode, ctx);
    scr_route_enter(mode, ctx);
}

// What a handler reads that `args` lacks ("" when complete).
std::string missing_args(uint32_t fn, const std::vector<ScrArg>& a) {
    if (fn == kModeCycle) {
        const ScrArg* period = scr_arg(a, "i10");
        return period != nullptr && period->num >= 1.0 && scr_arg(a, "i14") != nullptr ? "" : "i10 (> 0)/i14";
    }
    return scr_library_missing_args(fn, a);
}

// "KIRIN:scr_judge_volume_def[10] 'KIRIN_BACK'" -> "KIRIN_BACK"; a bare name stays.
std::string judge_name(const std::string& handle) {
    const size_t open = handle.find('\'');
    return open == std::string::npos ? handle : handle.substr(open + 1, handle.size() - open - 2);
}

}  // namespace

ScrRegistry scr_registry_kirin() {
    ScrRegistry r;
    scr_library_add(r);
    scr_route_add(r);
    r.predicates[kRockfallRunning] = p_rockfall_running;
    r.predicates[kModeCycle] = p_mode_cycle;
    r.actions[kRockfallTick] = a_rockfall_tick;
    r.actions[kRockfallArm] = a_rockfall_arm;
    r.actions[kRockfallDisarm] = a_rockfall_disarm;
    for (const uint32_t fn : {kRockfallCave, kRockfallHill, kProxForce, kProxClear}) {
        r.actions[fn] = a_gap;
        r.gaps.insert(fn);
    }
    r.enterMode = enter_mode;
    return r;
}

bool scr_kirin_check(const ScrGraph& graph, const RouteGraph* routes, const std::vector<std::string>& judgeNames,
                     std::string& error) {
    ScrCheckRules rules;
    rules.additiveRows = true;    // she plays the additive layer: its rows' control-defs are read too
    rules.optionalJudges = true;  // her exported rows name every volume; an empty one is never inside
    rules.boundAttain = true;     // scr_attack_attain runs before this check (scr_kirin.h)
    rules.missingArgs = missing_args;
    if (!scr_check_graph(graph, judgeNames, rules, error)) return false;
    for (const ScrMode& m : graph.modes) {
        const ScrLocomotion& l = m.locomotion;
        const std::string at = "mode " + std::to_string(m.id) + ": ";
        if (l.kind >= 2 && l.kind <= 9 && routes == nullptr) {
            error = at + "locomotion kind " + std::to_string(l.kind) + " needs a route graph";
            return false;
        }
        if (l.judge.empty()) continue;
        if (std::find(judgeNames.begin(), judgeNames.end(), judge_name(l.judge)) == judgeNames.end()) {
            error = at + "judge volume not in the sidecar: " + judge_name(l.judge);
            return false;
        }
        if (!(l.backRadius > 0.0f)) {
            error = at + "the MOVE_BACK sector of " + judge_name(l.judge) + " is unbound (scr_move_back_bind)";
            return false;
        }
    }
    return true;
}

std::vector<AddEdge> scr_additive_edges(const ScrGraph& graph) {
    static const std::string kAdd = "_ADD";
    std::vector<AddEdge> edges;
    for (const ScrMotionEdge& e : graph.motionGraph) {
        const bool add = e.table.size() >= kAdd.size() &&
                         e.table.compare(e.table.size() - kAdd.size(), kAdd.size(), kAdd) == 0;
        if (!add) continue;
        edges.push_back(AddEdge{e.orient, e.from, e.to, e.ref, e.p30, e.p34, e.speed, e.fromRef});
    }
    return edges;
}

std::vector<std::string> scr_control_additive(const ScrGraph& graph, ScrRuntime& runtime, const std::vector<int>& ctrls,
                                              const ScrWorld& world) {
    std::vector<std::string> orients;
    const int cm = world.bossControlMode;
    if (cm < 0 || cm >= static_cast<int>(graph.controlModes.size())) return orients;
    const size_t cap = AddLayers{}.slot.size();  // binary FUN_00634f80: the queue holds one per layer
    for (const ScrAssign& row : graph.controlModes[static_cast<size_t>(cm)].assign) {
        if (row.layer == "base" || orients.size() >= cap) continue;
        const bool bit = row.ctrl >= 0 && std::find(ctrls.begin(), ctrls.end(), row.ctrl) != ctrls.end();
        if (bit || scr_cdef_eval(graph, row.when, ctrls, world, runtime)) orients.push_back(row.orient);
    }
    return orients;
}
