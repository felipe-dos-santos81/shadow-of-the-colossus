#include "scr_predicates.h"

#include <algorithm>

#include "scr_cdef.h"
#include "scr_library.h"

namespace {

namespace k = scr_knight;

// His boss-local fns (eboot VAs; recon findings §3/§4, predicates.json).
enum : uint32_t {
    kKnightFlag0 = 0x6feae0,
    kKnightSet1 = 0x6fe580, kKnightSet2 = 0x6fe5a0, kKnightSet4 = 0x6fe5c0, kKnightShake = 0x6fe5e0,
};

// The player's contact attribute & 0x3f0 (scr_library's contact_attr: the boss region while
// attached, else the ground word under him).
uint32_t region(const ScrWorld& w) {
    const uint64_t attr = w.playerAttached ? static_cast<uint64_t>(w.playerAttachedRegion) : w.playerGroundAttr;
    return static_cast<uint32_t>(attr & 0x3f0u);
}

bool p_knight_flag0(const ScrClause&, const ScrWorld&, const ScrRuntime& rt) { return (rt.bossBits & 1u) != 0; }

// --- Actions (findings §4; behaviour.md (e) for the shake) ---

void a_knight_set1(const ScrAction&, ScrCtx& ctx) { ctx.runtime.bossBits |= 1u; }  // FUN_006fe580
void a_knight_set2(const ScrAction&, ScrCtx& ctx) {                                // FUN_006fe5a0
    if ((ctx.runtime.bossBits & 4u) == 0) ctx.runtime.bossBits |= 2u;
}
void a_knight_set4(const ScrAction&, ScrCtx& ctx) {  // FUN_006fe5c0
    if ((ctx.runtime.bossBits & 4u) == 0) ctx.runtime.bossBits |= 4u;
}

// FUN_006fe5e0(f10 cooldown, f14 stop hold, f18 walk limit) — binary (knight/dumps/k1-dec.txt):
// bit 2 (walking since a walk started) times a forced stop after f18 s, clearing bits
// 1|2 f14 s on once bit 1 (stopped) is set; bit 4 (a refuse clip ended) is a cooldown of
// f10 s, double rate on the head/neck, with no swing; otherwise the region picks a swing.
void a_knight_shake(const ScrAction& a, ScrCtx& ctx) {
    ScrRuntime& rt = ctx.runtime;
    const float cooldown = scr_argf(a.args, "f10"), hold = scr_argf(a.args, "f14"), limit = scr_argf(a.args, "f18");
    float& walk = rt.bossTimer[0];  // W'+0x4c84
    float& cool = rt.bossTimer[1];  // W'+0x4c88
    const uint32_t r = region(ctx.world);
    const bool head = r == 0x10 || r == 0x20;
    if ((rt.bossBits & 2u) != 0) {
        // Verbatim: with bits 1 and 2 set and walk < limit, walk gains dt twice this frame.
        if (walk < limit) walk += rt.dt;
        if (walk >= limit) scr_request(ctx.out, k::kCtrlForcedStop);
        if ((rt.bossBits & 1u) != 0 && (walk += rt.dt) >= hold + limit) {
            rt.bossBits ^= 3u;
            walk = 0.0f;
        }
    }
    if ((rt.bossBits & 4u) != 0) {
        cool += rt.dt;
        if (head) cool += rt.dt;
        if (cool >= cooldown) {
            rt.bossBits ^= 4u;
            cool = 0.0f;
        }
        return;
    }
    switch (r) {
        case 0x10: case 0x20: scr_request(ctx.out, k::kCtrlRockHead); break;
        case 0x160: case 0x170: case 0x180: case 0x190: case 0x1a0: case 0x1c0: case 0x1e0: case 0x1f0: case 0x50:
            scr_request(ctx.out, k::kCtrlRockBody);
            break;
        case 0x60: case 0xe0: case 0xf0: case 0xa0: case 0xb0: case 0x40: scr_request(ctx.out, k::kCtrlRockArmR); break;
        case 0x80: case 0x100: case 0x110: case 0xc0: case 0xd0: scr_request(ctx.out, k::kCtrlRockArmL); break;
        default: break;
    }
}

bool requested(const std::vector<int>& ctrls, int bit) {
    return std::find(ctrls.begin(), ctrls.end(), bit) != ctrls.end();
}

// His own fns' argument slots ("" when the record carries them).
std::string missing_args(uint32_t fn, const std::vector<ScrArg>& a) {
    if (fn == kKnightShake) {
        const auto has = [&](const char* s) { return scr_arg(a, s) != nullptr; };
        return has("f10") && has("f14") && has("f18") ? "" : "f10/f14/f18";
    }
    return scr_library_missing_args(fn, a);
}

}  // namespace

ScrRegistry scr_registry_knight() {
    ScrRegistry r;
    scr_library_add(r);
    r.predicates[kKnightFlag0] = p_knight_flag0;
    r.actions[kKnightShake] = a_knight_shake;
    r.actions[kKnightSet1] = a_knight_set1;
    r.actions[kKnightSet2] = a_knight_set2;
    r.actions[kKnightSet4] = a_knight_set4;
    r.enterMode = scr_library_enter_mode;
    return r;
}

bool scr_knight_check(const ScrGraph& graph, const std::vector<std::string>& judgeNames, std::string& error) {
    ScrCheckRules rules;  // base rows only, every FIX/ATK/ATTAIN named; his glue binds ATTAIN after the check
    rules.missingArgs = missing_args;
    return scr_check_graph(graph, judgeNames, rules, error);
}

int scr_clip(const ScrGraph& graph, const std::string& name) {
    const auto it = std::find(graph.clipNames.begin(), graph.clipNames.end(), name);
    return it == graph.clipNames.end() ? -1 : static_cast<int>(it - graph.clipNames.begin());
}

ScrStepOut scr_knight_step(const ScrGraph& graph, const ScrRegistry& registry, ScrRuntime& rt,
                           const ScrWorld& world, float dt) {
    return scr_boss_step(graph, registry, rt, world, dt);
}

int scr_control_clip(const ScrGraph& graph, ScrRuntime& rt, const std::vector<int>& ctrls, const ScrWorld& world) {
    const int cm = world.bossControlMode;
    if (cm < 0 || cm >= static_cast<int>(graph.controlModes.size())) return -1;
    const ScrAssign* fired = nullptr;
    for (const ScrAssign& row : graph.controlModes[cm].assign) {
        if (row.layer != "base") continue;  // §13.7: no additive layer in S12
        const bool cond = scr_cdef_eval(graph, row.when, ctrls, world, rt);  // scr_knight_check: all exported
        if ((row.ctrl >= 0 && requested(ctrls, row.ctrl)) || cond) fired = &row;  // the last firing row wins
    }
    if (fired == nullptr) return -1;
    // The edge from the current motion; else an any-motion edge (null `from`: the always
    // table — fall, death; the additive table's null-`from` edges are reached only by the
    // dropped additive rows' orients).
    int to = -1, edge = -1;
    for (size_t i = 0; i < graph.motionGraph.size(); ++i) {
        const ScrMotionEdge& e = graph.motionGraph[i];
        if (e.orient != fired->orient) continue;
        if (e.from == world.bossMotion) {
            to = e.to;
            edge = static_cast<int>(i);
            break;
        }
        if (e.from < 0 && to < 0) {
            to = e.to;
            edge = static_cast<int>(i);
        }
    }
    // No edge for that orient from here, or a self-edge (turn_r + Right_rotation_Tachidomari
    // -> turn_r): the playing clip keeps playing.
    if (to < 0 || to == world.bossMotion) return -1;
    rt.controlMode = fired->nextControlMode;
    rt.controlEdge = edge;
    return to;
}

int scr_control_clip(const ScrGraph& graph, ScrRuntime& rt, int ctrl, const ScrWorld& world) {
    return scr_control_clip(graph, rt, std::vector<int>{ctrl}, world);
}
