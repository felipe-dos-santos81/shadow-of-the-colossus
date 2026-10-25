// The colossus mode-graph interpreter (S12 Task 4): the walker (recon graph/findings.md
// §1), the condition evaluator (§2) and the strict loader. Every graph here is
// synthetic, written in the exporter's shape (`sotc-scr-graph/0`: fn as "0x..." hex,
// control_modes keyed by index, clip names as lower-case stems) — no game data.
#include <doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "scr_graph.h"
#include "scr_library.h"
#include "scr_predicates.h"

namespace {

// Test predicates/actions, keyed by the real VAs of the predicates they stand in for.
constexpr uint32_t kTrueFn = 0x832600;     // true
constexpr uint32_t kFlagFn = 0x8346e0;     // game_flag: flags[i10]
constexpr uint32_t kRequestFn = 0x900000;  // a test action: request its h60 control

bool pred_true(const ScrClause&, const ScrWorld&, const ScrRuntime&) { return true; }

bool pred_flag(const ScrClause& clause, const ScrWorld& world, const ScrRuntime&) {
    const ScrArg* flag = scr_arg(clause.args, "i10");
    return flag != nullptr && world.flags.test(static_cast<size_t>(flag->num));
}

void act_request(const ScrAction& action, ScrCtx& ctx) {
    scr_request(ctx.out, scr_arg(action.args, "h60")->ctrl);
}

ScrRegistry test_registry() {
    ScrRegistry reg;
    reg.predicates[kTrueFn] = pred_true;
    reg.predicates[kFlagFn] = pred_flag;
    reg.actions[kRequestFn] = act_request;
    reg.kinds = {-1};  // the synthetic modes' locomotion kind
    return reg;
}

const std::vector<std::string> kClips = {"knight_a_wait", "knight_a_search", "knight_a_exit"};
constexpr int kClipWait = 0;
constexpr int kClipSearch = 1;
constexpr int kClipExit = 2;

std::string ctrl(int bit) {
    return R"({"ctrl": "C)" + std::to_string(bit) + R"(", "bit": )" + std::to_string(bit) +
           R"(, "index": )" + std::to_string(100 + bit) + "}";
}

std::string clip(const std::string& name) {
    return R"({"motion": "knight[0]", "name": "M", "clip": ")" + name + R"("})";
}

constexpr const char* kNoEnter = R"({"actions": null, "ctrl": null, "wait_motion": null})";
constexpr const char* kNoUpdate = R"({"actions": null, "ctrl": null})";
constexpr const char* kNoExit = R"({"actions": null, "ctrl_or_demo": null, "wait_motion": null})";

std::string mode(int id, const std::string& transitions, const std::string& enter = kNoEnter,
                 const std::string& update = kNoUpdate, const std::string& exit = kNoExit) {
    return R"({"id": )" + std::to_string(id) + R"(, "name": "m)" + std::to_string(id) +
           R"(", "locomotion": {"row": "T:scr_slowstatus[0]", "kind": -1, "p0c": 0, "attack": null,
              "attack_trigger": null, "attain": [0, 0, 0, 0, 12, 60, 80],
              "perception": [10, 100, 200, 60, 45, -55, 5, 0.5, 2, 10], "gaze": "T:scr_gazestatus[0]"},
            "enter": )" + enter + R"(, "update": )" + update + R"(, "exit": )" + exit +
           R"(, "transitions": [)" + transitions + "]}";
}

std::string row(const std::string& when, int to, float minSince = -1.0f) {
    std::string r = R"({"when": )" + when + R"(, "to": )" + std::to_string(to);
    if (minSince >= 0.0f) r += R"(, "min_since_transition": )" + std::to_string(minSince);
    return r + "}";
}

// T:C0 always true; T:C1..C3 game_flag 1..3.
const std::string kConditions = R"({
  "T:C0": {"label": "always", "op": "and",
           "clauses": [{"pred": "true", "fn": "0x832600", "expect": true, "args": {}}]},
  "T:C1": {"label": "flag1", "op": "and",
           "clauses": [{"pred": "game_flag", "fn": "0x8346e0", "expect": true, "args": {"i10": 1}}]},
  "T:C2": {"label": "flag2", "op": "and",
           "clauses": [{"pred": "game_flag", "fn": "0x8346e0", "expect": true, "args": {"i10": 2}}]},
  "T:C3": {"label": "flag3", "op": "and",
           "clauses": [{"pred": "game_flag", "fn": "0x8346e0", "expect": true, "args": {"i10": 3}}]}
})";

std::string root(int deathMode = -1) {
    const std::string death = deathMode < 0 ? "null"
                                            : R"({"mode": )" + std::to_string(deathMode) + R"(, "name": "d"})";
    return R"({"start_mode": {"mode": 0, "name": "m0"}, "retry_start_mode": {"mode": 0, "name": "m0"},
               "retry_ctrl": )" + ctrl(43) + R"(, "death_mode": )" + death + R"(,
               "death_control_mode": "T:control-mode[10]", "callback_actions": null,
               "every_frame_actions": null, "anger": null})";
}

std::string graph_json(const std::vector<std::string>& modes, const std::string& conditions = kConditions,
                       const std::string& actions = "{}", const std::string& rootJson = root(),
                       const std::string& controlModes = "{}", const std::string& motionGraph = "[]") {
    std::string m;
    for (const std::string& s : modes) m += (m.empty() ? "" : ",") + s;
    return R"({"format": "sotc-scr-graph/0", "boss": "T", "source": "test", "root": )" + rootJson +
           R"(, "modes": [)" + m + R"(], "conditions": )" + conditions + R"(, "actions": )" + actions +
           R"(, "control_modes": )" + controlModes + R"(, "motion_graph": )" + motionGraph + "}";
}

ScrGraph load(const std::string& json, const ScrRegistry& reg) {
    ScrGraph graph;
    std::string error;
    const bool ok = scr_graph_load(json, kClips, reg, graph, error);
    INFO(error);
    REQUIRE(ok);
    return graph;
}

int cond_index(const ScrGraph& graph, const std::string& key) {
    for (size_t i = 0; i < graph.conditions.size(); ++i) {
        if (graph.conditions[i].key == key) return static_cast<int>(i);
    }
    FAIL("no condition " << key);
    return -1;
}

bool has(const ScrStepOut& out, int bit) {
    return std::find(out.controls.begin(), out.controls.end(), bit) != out.controls.end();
}

// One machine: a graph, its registry, a runtime started on the root's start mode.
struct Machine {
    ScrRegistry reg = test_registry();
    ScrGraph graph;
    ScrRuntime rt;
    ScrWorld world;
    explicit Machine(const std::string& json) : graph(load(json, reg)) { scr_runtime_start(graph, rt, false); }
    ScrStepOut step(float dt = 0.1f) { return scr_step(graph, reg, rt, world, dt); }
};

}  // namespace

TEST_CASE("first match wins") {
    Machine m(graph_json({mode(0, row(R"("T:C1")", 1) + "," + row(R"("T:C0")", 2) + "," + row(R"("T:C0")", 3)),
                          mode(1, ""), mode(2, ""), mode(3, "")}));
    ScrStepOut out = m.step();  // the start: pending 0, no current -> enter 0, no walk
    CHECK(out.mode == 0);
    CHECK(out.modeChanged);
    out = m.step();  // C1 false, then the first of the two true rows
    CHECK(out.mode == 2);
    CHECK(m.rt.previous == 0);

    Machine first(graph_json({mode(0, row(R"("T:C1")", 1) + "," + row(R"("T:C0")", 2)), mode(1, ""), mode(2, "")}));
    first.world.flags.set(1);
    first.step();
    CHECK(first.step().mode == 1);
}

TEST_CASE("the +0x20 minimum on timer 2 holds a row back") {
    Machine m(graph_json({mode(0, row(R"("T:C0")", 1, 0.5f)), mode(1, "")}));
    CHECK(m.step(0.2f).mode == 0);  // enter: timer 2 = 0, then += dt
    CHECK(m.rt.timer2 == doctest::Approx(0.2f));
    CHECK(m.step(0.2f).mode == 0);  // 0.5 > 0.2: held although the condition is true
    CHECK(m.step(0.2f).mode == 0);  // 0.5 > 0.4
    const ScrStepOut out = m.step(0.2f);  // 0.5 <= 0.6: fires
    CHECK(out.mode == 1);
    CHECK(out.modeChanged);
}

TEST_CASE("timer 1 survives a self-transition, timer 2 resets") {
    Machine m(graph_json({mode(0, row(R"("T:C1")", 0) + "," + row(R"("T:C2")", 1)), mode(1, "")}));
    m.step(0.25f);
    m.step(0.25f);
    m.step(0.25f);
    CHECK(m.rt.timer1 == doctest::Approx(0.75f));
    CHECK(m.rt.timer2 == doctest::Approx(0.75f));

    m.world.flags.set(1);
    const ScrStepOut self = m.step(0.25f);  // 0 -> 0: a re-entry
    CHECK(self.modeChanged);
    CHECK(self.mode == 0);
    CHECK(m.rt.transitioned);
    CHECK(m.rt.previous == 0);
    CHECK(m.rt.timer1 == doctest::Approx(1.0f));  // kept, then += dt
    CHECK(m.rt.timer2 == doctest::Approx(0.25f));  // reset, then += dt

    m.world.flags.reset(1);
    m.world.flags.set(2);
    CHECK(m.step(0.25f).mode == 1);  // a real change resets both
    CHECK(m.rt.timer1 == doctest::Approx(0.25f));
    CHECK(m.rt.timer2 == doctest::Approx(0.25f));
}

TEST_CASE("null condition is true") {
    Machine m(graph_json({mode(0, row("null", 1)), mode(1, "")}));
    m.step();
    CHECK(m.step().mode == 1);
    CHECK(scr_eval(m.graph, m.reg, -1, m.rt, m.world));
}

TEST_CASE("only the first record's combiner counts (AND then OR record -> AND)") {
    // The export carries the head record's combiner as `op` (the later records' +0
    // is never read): an AND over a matching then a failing clause is false, where an
    // OR reading of the second record would make it true. Any other combiner is false
    // even when every clause matches; `sub` evaluates the condition its h50 names.
    const std::string conditions = R"({
      "T:C0": {"label": "always", "op": "and",
               "clauses": [{"pred": "true", "fn": "0x832600", "expect": true, "args": {}}]},
      "T:C4": {"label": "and-then-or", "op": "and",
               "clauses": [{"pred": "true", "fn": "0x832600", "expect": true, "args": {}},
                           {"pred": "game_flag", "fn": "0x8346e0", "expect": true, "args": {"i10": 1}}]},
      "T:C5": {"label": "or", "op": "or",
               "clauses": [{"pred": "game_flag", "fn": "0x8346e0", "expect": true, "args": {"i10": 1}},
                           {"pred": "true", "fn": "0x832600", "expect": true, "args": {}}]},
      "T:C6": {"label": "other", "op": "false",
               "clauses": [{"pred": "true", "fn": "0x832600", "expect": true, "args": {}}]},
      "T:C7": {"label": "empty and", "op": "and", "clauses": []},
      "T:C8": {"label": "empty or", "op": "or", "clauses": []},
      "T:C9": {"label": "sub", "op": "and",
               "clauses": [{"pred": "sub", "fn": "0x832610", "expect": true, "args": {"h50": "T:C4"}}]}
    })";
    Machine m(graph_json({mode(0, "")}, conditions));
    auto eval = [&](const char* key) { return scr_eval(m.graph, m.reg, cond_index(m.graph, key), m.rt, m.world); };
    CHECK_FALSE(eval("T:C4"));
    CHECK(eval("T:C5"));
    CHECK_FALSE(eval("T:C6"));
    CHECK(eval("T:C7"));
    CHECK_FALSE(eval("T:C8"));
    CHECK_FALSE(eval("T:C9"));
    m.world.flags.set(1);
    CHECK(eval("T:C4"));
    CHECK(eval("T:C9"));
}

TEST_CASE("expect 0 negates a clause") {
    const std::string conditions = R"({
      "T:C0": {"label": "not true", "op": "and",
               "clauses": [{"pred": "true", "fn": "0x832600", "expect": false, "args": {}}]},
      "T:C1": {"label": "not flag1", "op": "and",
               "clauses": [{"pred": "game_flag", "fn": "0x8346e0", "expect": false, "args": {"i10": 1}}]},
      "T:C2": {"label": "not sub", "op": "and",
               "clauses": [{"pred": "sub", "fn": "0x832610", "expect": false, "args": {"h50": "T:C1"}}]}
    })";
    Machine m(graph_json({mode(0, "")}, conditions));
    auto eval = [&](const char* key) { return scr_eval(m.graph, m.reg, cond_index(m.graph, key), m.rt, m.world); };
    CHECK_FALSE(eval("T:C0"));
    CHECK(eval("T:C1"));
    CHECK_FALSE(eval("T:C2"));
    m.world.flags.set(1);
    CHECK_FALSE(eval("T:C1"));
    CHECK(eval("T:C2"));
}

TEST_CASE("entry gate: entered stays 0 until the boss motion equals the mode's +0x60") {
    const std::string enter = R"({"actions": null, "ctrl": )" + ctrl(7) + R"(, "wait_motion": )" +
                              clip("knight_a_search") + "}";
    const std::string update = R"({"actions": null, "ctrl": )" + ctrl(9) + "}";
    Machine m(graph_json({mode(0, row(R"("T:C0")", 1)), mode(1, row(R"("T:C0")", 2), enter, update), mode(2, "")}));
    m.world.bossMotion = kClipWait;
    m.step();
    ScrStepOut out = m.step();  // the switch into 1 requests its enter control and closes the gate
    CHECK(out.mode == 1);
    CHECK(has(out, 7));
    CHECK_FALSE(m.rt.entered);
    CHECK_FALSE(has(out, 9));  // no update while not entered

    for (int i = 0; i < 3; ++i) {
        out = m.step();  // the motion is not the search: re-request, no walk (C0 would leave)
        CHECK(out.mode == 1);
        CHECK_FALSE(out.modeChanged);
        CHECK(out.controls == std::vector<int>{7});
        CHECK_FALSE(m.rt.entered);
    }

    m.world.bossMotion = kClipSearch;
    out = m.step();  // the gate opens and the walk runs in the same frame
    CHECK(m.rt.entered);
    CHECK(out.mode == 2);
}

TEST_CASE("entry gate: once entered, the update control runs and the enter control stops") {
    const std::string enter = R"({"actions": null, "ctrl": )" + ctrl(7) + R"(, "wait_motion": )" +
                              clip("knight_a_search") + "}";
    const std::string update = R"({"actions": null, "ctrl": )" + ctrl(9) + "}";
    Machine m(graph_json({mode(0, row(R"("T:C0")", 1)), mode(1, row(R"("T:C2")", 0), enter, update)}));
    m.step();
    m.step();
    m.world.bossMotion = kClipSearch;
    const ScrStepOut out = m.step();
    CHECK(m.rt.entered);
    CHECK(out.mode == 1);
    CHECK(out.controls == std::vector<int>{9});
}

TEST_CASE("exit gate defers the switch until the old mode's +0x90 motion plays, re-requesting its exit control") {
    const std::string actions = R"({"T:F0": [{"act": "request", "fn": "0x900000", "args": {"h60": )" + ctrl(12) +
                                "}}]}";
    const std::string exit = R"({"actions": "T:F0", "ctrl_or_demo": )" + ctrl(11) + R"(, "wait_motion": )" +
                             clip("knight_a_exit") + "}";
    const std::string enter1 = R"({"actions": null, "ctrl": )" + ctrl(13) + R"(, "wait_motion": null})";
    const std::string update1 = R"({"actions": null, "ctrl": )" + ctrl(14) + "}";
    Machine m(graph_json({mode(0, row(R"("T:C1")", 1), kNoEnter, kNoUpdate, exit), mode(1, "", enter1, update1)},
                         kConditions, actions));
    m.world.bossMotion = kClipWait;
    m.step();
    m.world.flags.set(1);
    for (int i = 0; i < 3; ++i) {
        const ScrStepOut out = m.step();
        CHECK(out.mode == 0);
        CHECK_FALSE(out.modeChanged);
        CHECK(m.rt.exitDeferred);
        CHECK(m.rt.pending == 1);
        CHECK(has(out, 11));         // the exit control, every deferred frame
        CHECK_FALSE(has(out, 12));   // the exit actions wait for the gate
        CHECK_FALSE(has(out, 13));   // no enter yet
        CHECK(has(out, 14));         // the update control comes from the pending mode
    }
    m.world.bossMotion = kClipExit;
    const ScrStepOut out = m.step();
    CHECK(out.mode == 1);
    CHECK(out.modeChanged);
    CHECK_FALSE(m.rt.exitDeferred);
    CHECK(out.controls == std::vector<int>{11, 12, 13, 14});  // exit ctrl, exit actions, enter, update
}

TEST_CASE("forced death at HP <= 0 skips the walk and enters the root death mode once") {
    const std::string exit = R"({"actions": null, "ctrl_or_demo": null, "wait_motion": )" + clip("knight_a_exit") + "}";
    Machine m(graph_json({mode(0, row(R"("T:C0")", 1), kNoEnter, kNoUpdate, exit), mode(1, ""), mode(2, "")},
                         kConditions, "{}", root(2)));
    CHECK(m.graph.root.deathMode == 2);
    CHECK(m.graph.root.deathControlMode == 10);
    m.world.bossMotion = kClipWait;
    m.step();

    m.world.hp = 0.0f;
    m.world.bossControlMode = 3;  // HP 0 but not in the death control-mode: the walk runs
    ScrStepOut out = m.step();
    CHECK_FALSE(out.deathForced);
    CHECK(m.rt.exitDeferred);
    CHECK(m.rt.pending == 1);

    m.world.bossControlMode = 10;
    out = m.step();  // forced: pending = death, no walk, and the exit gate does not hold it
    CHECK(out.deathForced);
    CHECK(out.modeChanged);
    CHECK(out.mode == 2);
    CHECK(m.rt.entered);

    out = m.step();  // already in the death mode: not forced again
    CHECK_FALSE(out.deathForced);
    CHECK_FALSE(out.modeChanged);
    CHECK(out.mode == 2);
}

TEST_CASE("scr_graph_load rejects an unknown predicate fn and an unknown action kind, naming them") {
    const ScrRegistry reg = test_registry();
    ScrGraph graph;
    std::string error;

    const std::string unknownPred = R"({
      "T:C0": {"label": "sensed", "op": "and",
               "clauses": [{"pred": "sensed", "fn": "0x833880", "expect": true, "args": {}}]}
    })";
    CHECK_FALSE(scr_graph_load(graph_json({mode(0, "")}, unknownPred), kClips, reg, graph, error));
    CHECK(error.find("0x833880") != std::string::npos);
    CHECK(error.find("T:C0") != std::string::npos);

    error.clear();
    const std::string unknownAct = R"({"T:F8": [{"act": "if_run", "fn": "0x82f5b0", "args": {}}]})";
    CHECK_FALSE(scr_graph_load(graph_json({mode(0, "")}, kConditions, unknownAct), kClips, reg, graph, error));
    CHECK(error.find("0x82f5b0") != std::string::npos);
    CHECK(error.find("T:F8") != std::string::npos);

    // An unreferenced condition is still checked: nothing is skipped.
    error.clear();
    CHECK_FALSE(scr_graph_load(graph_json({mode(0, "")}, unknownPred), kClips, ScrRegistry{}, graph, error));
    CHECK_FALSE(error.empty());
    // A dangling condition reference is an error, not a null (true) condition.
    error.clear();
    CHECK_FALSE(scr_graph_load(graph_json({mode(0, row(R"("T:C99")", 0))}), kClips, reg, graph, error));
    CHECK(error.find("T:C99") != std::string::npos);
}

TEST_CASE("a clip name missing from clipNames is a load error") {
    const ScrRegistry reg = test_registry();
    ScrGraph graph;
    std::string error;
    const std::string enter = R"({"actions": null, "ctrl": )" + ctrl(7) + R"(, "wait_motion": )" +
                              clip("knight_a_missing") + "}";
    CHECK_FALSE(scr_graph_load(graph_json({mode(0, "", enter)}), kClips, reg, graph, error));
    CHECK(error.find("knight_a_missing") != std::string::npos);

    error.clear();
    const std::string edges =
        R"([{"table": "T", "from": "knight_a_wait", "orient": "o", "to": "knight_a_gone", "p30": 0, "p34": 0}])";
    CHECK_FALSE(scr_graph_load(graph_json({mode(0, "")}, kConditions, "{}", root(), "{}", edges), kClips, reg, graph,
                               error));
    CHECK(error.find("knight_a_gone") != std::string::npos);
}

TEST_CASE("the export's shape loads: hex fns, control_modes keyed by index, null motion-graph ends") {
    const std::string controlModes = R"({
      "1": {"name": "WALK", "assign": [{"layer": "base", "when": "K:control-def[3] 'X'", "ctrl": null,
                                        "orient": "o", "next_control_mode": 0}]},
      "0": {"name": "STANDBY", "assign": [{"layer": "base", "when": "K:control-def[1] 'Y'", "ctrl": )" +
                                     ctrl(0) + R"(, "orient": "start_up", "next_control_mode": 1}]}
    })";
    const std::string edges =
        R"([{"table": "T", "from": "knight_a_wait", "orient": "o", "to": "knight_a_search", "p30": -1.0, "p34": 0.5},
            {"table": "T", "from": null, "orient": "o", "to": "knight_a_wait", "p30": 0, "p34": 0}])";
    const ScrGraph g = load(graph_json({mode(0, "")}, kConditions, "{}", root(), controlModes, edges), test_registry());
    REQUIRE(g.controlModes.size() == 2);
    CHECK(g.controlModes[0].name == "STANDBY");
    CHECK(g.controlModes[0].assign[0].ctrl == 0);
    CHECK(g.controlModes[1].assign[0].ctrl == -1);
    CHECK(g.controlModes[1].assign[0].nextControlMode == 0);
    REQUIRE(g.motionGraph.size() == 2);
    CHECK(g.motionGraph[0].from == kClipWait);
    CHECK(g.motionGraph[0].to == kClipSearch);
    CHECK(g.motionGraph[0].p30 == doctest::Approx(-1.0f));
    CHECK(g.motionGraph[1].from == -1);
    CHECK(g.conditions[cond_index(g, "T:C1")].clauses[0].fn == kFlagFn);
    CHECK(g.root.retryCtrl == 43);
    CHECK(g.modes[0].locomotion.perception.size() == 10);
}

namespace {

std::vector<std::pair<int, int>> gEntered;  // (mode, rt.current) per enterMode call

void enter_mode_hook(int mode, ScrCtx& ctx) {
    gEntered.emplace_back(mode, ctx.runtime.current);
    scr_request(ctx.out, 40);
}

}  // namespace

TEST_CASE("the mode-entry hook runs after the exit actions, before the enter actions, never on a deferred switch") {
    const std::string actions = R"({"T:F0": [{"act": "request", "fn": "0x900000", "args": {"h60": )" + ctrl(12) +
                                R"(}}], "T:F1": [{"act": "request", "fn": "0x900000", "args": {"h60": )" + ctrl(15) +
                                "}}]}";
    const std::string exit = R"({"actions": "T:F1", "ctrl_or_demo": null, "wait_motion": )" + clip("knight_a_exit") +
                             "}";
    const std::string enter1 = R"({"actions": "T:F0", "ctrl": null, "wait_motion": null})";
    Machine m(graph_json({mode(0, row(R"("T:C1")", 1), kNoEnter, kNoUpdate, exit), mode(1, "", enter1)}, kConditions,
                         actions));
    m.reg.enterMode = enter_mode_hook;
    gEntered.clear();
    m.world.bossMotion = kClipWait;
    CHECK(m.step().controls == std::vector<int>{40});  // the start enters mode 0 through the hook too
    REQUIRE(gEntered.size() == 1);
    CHECK(gEntered[0] == std::pair{0, -1});

    m.world.flags.set(1);
    const ScrStepOut held = m.step();  // deferred: no hook
    CHECK(m.rt.exitDeferred);
    CHECK_FALSE(has(held, 40));
    CHECK(gEntered.size() == 1);

    m.world.bossMotion = kClipExit;
    const ScrStepOut out = m.step();
    CHECK(out.mode == 1);
    CHECK(out.controls == std::vector<int>{15, 40, 12});  // exit actions, hook, enter actions
    REQUIRE(gEntered.size() == 2);
    CHECK(gEntered[1] == std::pair{1, 0});  // the old mode is still current
}

TEST_CASE("a mode block missing a key is a load error (an explicit null is fine)") {
    const ScrRegistry reg = test_registry();
    ScrGraph graph;
    std::string error;
    CHECK_FALSE(scr_graph_load(graph_json({mode(0, "", R"({"actions": null, "wait_motion": null})")}), kClips, reg,
                               graph, error));
    CHECK(error.find("\"ctrl\"") != std::string::npos);
    error.clear();
    const std::string noCtrl = R"({"actions": null, "wait_motion": null})";
    CHECK_FALSE(scr_graph_load(graph_json({mode(0, "", kNoEnter, kNoUpdate, noCtrl)}), kClips, reg, graph, error));
    CHECK(error.find("ctrl_or_demo") != std::string::npos);
}

// --- S13 contracts: the optional `/0` additions (docs/notes/s13-schemas.md) ----------

namespace {

// `json` with `extra` (", "-joined keys) added at the document's top level.
std::string with_keys(std::string json, const std::string& extra) {
    json.insert(json.rfind('}'), ", " + extra);
    return json;
}

// `s` with its one `from` replaced by `to`.
std::string replace_once(std::string s, const std::string& from, const std::string& to) {
    const size_t at = s.find(from);
    REQUIRE(at != std::string::npos);
    return s.replace(at, from.size(), to);
}

std::string load_error(const std::string& json, const ScrRegistry& reg) {
    ScrGraph graph;
    std::string error;
    CHECK_FALSE(scr_graph_load(json, kClips, reg, graph, error));
    return error;
}

}  // namespace

TEST_CASE("the S13 optional keys parse into the new fields") {
    const std::string loco = R"("gaze": "T:scr_gazestatus[0]", "ref_player": 1, "track": false,
        "judge": "T:judge[2]", "p60": 1.5, "p64": 2, "p68": 3, "p6c": 4, "p70": 5, "p74": 6, "p98": 7, "p9c": 8)";
    const std::string m0 = replace_once(mode(0, ""), R"("gaze": "T:scr_gazestatus[0]")", loco);
    const std::string edges =
        R"([{"table": "T_ADD", "from": null, "orient": "o", "to": "knight_a_search", "p30": 5, "p34": 10,
             "ref": "knight_a_wait", "speed": true, "blend_out": 0},
            {"table": "T", "from": "knight_a_wait", "orient": "o", "to": "knight_a_exit", "p30": 0, "p34": 0,
             "ref": null}])";
    const std::string rootJson = replace_once(root(), R"("anger": null)", R"("anger": null, "kind": 1)");
    const std::string json = with_keys(graph_json({m0}, kConditions, "{}", rootJson, "{}", edges), R"(
      "control_defs": {"T:control-def[3] 'X'": [
         {"op": 84, "min": -1, "max": -1, "handle": )" + clip("knight_a_exit") + R"(, "mask": "0x0", "value": "0x0", "expect": 1},
         {"op": 56, "min": 0, "max": 0, "handle": )" + ctrl(6) + R"(, "mask": "0xffffffffffffffff", "value": "0x3f0", "expect": false},
         {"op": 54, "min": 0, "max": 0, "handle": "T:control-mode[7] 'Y'", "mask": "0x0", "value": "0x0", "expect": 1},
         {"op": 118, "min": 0.5, "max": 60, "handle": null, "mask": "0x0", "value": "0x0", "expect": 0}]},
      "dprelate": {"T:scr_dprelate[0] 'Z'": [
         {"mask": "0x7f000000000000", "value": "0x3000000000000", "object": null, "cond": "T:C1", "route": 2}]},
      "attackdata": {"T_SWORD:scr_attackdata[0]": [
         {"label": "L", "chain": null, "chain_pct": 100, "ctrl_modes": null, "default": 1, "reselect_s": 15,
          "sensing": 0, "cond": "T:C2", "fix": "T_FIX", "atk": "T_ATK", "alt_atk": null, "attain": "T_ATTAIN",
          "alt_cond": null, "issue": )" + ctrl(4) + R"(, "issue2": null, "motion": )" + clip("knight_a_search") + R"(,
          "aim_bone": null, "aim_volume": null, "f0": 0, "cooldown_s": 10, "issue_arrived": true}]})");
    const ScrGraph g = load(json, test_registry());

    CHECK(g.root.kind == 1);
    const ScrLocomotion& l = g.modes[0].locomotion;
    CHECK(l.refPlayer);
    CHECK_FALSE(l.track);
    CHECK(l.judge == "T:judge[2]");
    CHECK(l.p60 == 1.5f);
    CHECK(l.p74 == 6.0f);
    CHECK(l.p98 == 7.0f);
    CHECK(l.p9c == 8.0f);

    REQUIRE(g.motionGraph.size() == 2);
    CHECK(g.motionGraph[0].ref == kClipWait);
    CHECK(g.motionGraph[0].speed);
    CHECK(g.motionGraph[0].blendOut == 0.0f);
    CHECK(g.motionGraph[1].ref == -1);

    REQUIRE(g.controlDefs.count("T:control-def[3] 'X'") == 1);
    const std::vector<ScrCdefRecord>& cd = g.controlDefs.at("T:control-def[3] 'X'");
    REQUIRE(cd.size() == 4);
    CHECK(cd[0].op == 0x54);
    CHECK(cd[0].handle.clip == kClipExit);
    CHECK(cd[0].expect == 1);
    CHECK(cd[1].op == 0x38);
    CHECK(cd[1].handle.ctrl == 6);
    CHECK(cd[1].mask == ~uint64_t{0});
    CHECK(cd[1].value == 0x3f0u);
    CHECK(cd[1].expect == 0);
    CHECK(cd[2].handle.text == "T:control-mode[7] 'Y'");
    CHECK(cd[3].handle.null);
    CHECK(cd[3].min == 0.5f);
    CHECK(cd[3].max == 60.0f);

    REQUIRE(g.dprelate.count("T:scr_dprelate[0] 'Z'") == 1);
    const ScrDprelateRow& d = g.dprelate.at("T:scr_dprelate[0] 'Z'").at(0);
    CHECK(d.mask == 0x7f000000000000u);
    CHECK(d.value == 0x3000000000000u);
    CHECK(d.object.empty());
    CHECK(d.cond == cond_index(g, "T:C1"));
    CHECK(d.route == 2);

    REQUIRE(g.attackdata.count("T_SWORD:scr_attackdata[0]") == 1);
    const ScrAttackRow& a = g.attackdata.at("T_SWORD:scr_attackdata[0]").at(0);
    CHECK(a.label == "L");
    CHECK(a.chainPct == 100.0f);
    CHECK(a.isDefault);
    CHECK(a.reselectS == 15.0f);
    CHECK(a.cond == cond_index(g, "T:C2"));
    CHECK(a.altCond == -1);
    CHECK(a.fix == "T_FIX");
    CHECK(a.atk == "T_ATK");
    CHECK(a.altAtk.empty());
    CHECK(a.attain == "T_ATTAIN");
    CHECK(a.issue == 4);
    CHECK(a.issue2 == -1);
    CHECK(a.motion == kClipSearch);
    CHECK(a.cooldownS == 10.0f);
    CHECK(a.issueArrived);
}

TEST_CASE("a document without the S13 keys loads exactly as before") {
    const ScrGraph g = load(graph_json({mode(0, "")}), test_registry());
    CHECK(g.controlDefs.empty());
    CHECK(g.dprelate.empty());
    CHECK(g.attackdata.empty());
    CHECK(g.root.kind == -1);
    CHECK_FALSE(g.modes[0].locomotion.refPlayer);
    CHECK_FALSE(g.modes[0].locomotion.track);
    CHECK(g.modes[0].locomotion.judge.empty());
    CHECK(g.modes[0].locomotion.p60 == 0.0f);
}

TEST_CASE("an edge ref, a control-def clip and a mode kind outside the registry are load errors naming them") {
    const ScrRegistry reg = test_registry();
    const std::string edges =
        R"([{"table": "T_ADD", "from": null, "orient": "o", "to": "knight_a_wait", "p30": 0, "p34": 0,
             "ref": "knight_a_noref"}])";
    CHECK(load_error(graph_json({mode(0, "")}, kConditions, "{}", root(), "{}", edges), reg).find("knight_a_noref") !=
          std::string::npos);

    const std::string cdefs = R"("control_defs": {"T:control-def[1]": [{"op": 84, "min": 0, "max": 0, "handle": )" +
                              clip("knight_a_nocdef") + R"(, "mask": "0x0", "value": "0x0", "expect": 1}]})";
    CHECK(load_error(with_keys(graph_json({mode(0, "")}), cdefs), reg).find("knight_a_nocdef") != std::string::npos);

    const std::string kind9 = replace_once(mode(0, ""), R"("kind": -1)", R"("kind": 9)");
    const std::string error = load_error(graph_json({kind9}), reg);
    CHECK(error.find("mode 0") != std::string::npos);
    CHECK(error.find("kind 9") != std::string::npos);
}

TEST_CASE("scr_registry_dump lists fns, kinds and opcodes") {
    const std::string knight = scr_registry_dump(scr_registry_knight());
    for (const char* line : {"kind -1\n", "kind 0\n", "kind 1\n", "kind 10\n", "832600 implemented\n",
                             "832610 implemented\n", "6fe5e0 implemented\n"}) {
        CHECK_MESSAGE(knight.find(line) != std::string::npos, line);
    }
    ScrRegistry reg = test_registry();
    reg.gaps.insert(kTrueFn);
    const std::string dump = scr_registry_dump(reg);
    CHECK(dump.find("832600 gap\n") != std::string::npos);
    CHECK(dump.find("8346e0 implemented\n") != std::string::npos);
    CHECK(dump.find("900000 implemented\n") != std::string::npos);
}
