// The shared predicate/action library (S13 Task 5). The fixtures are real exports:
// tools/re/scr/graph_export.py runs at test time into the build directory from out/re/eboot.elf
// (a case fails, never skips, without it); KNIGHT's sidecar comes from SOTC_LIB. Every D5 row is
// one exported clause or action record (ruling R-F: whichever of the 16 exports uses it).
#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <tuple>
#include <stdexcept>
#include <string>
#include <vector>

#include "json_reader.h"
#include "knight_fixture.h"
#include "objanim_events.h"
#include "scr_graph.h"
#include "scr_library.h"
#include "scr_predicates.h"

namespace {

namespace fs = std::filesystem;
using namespace knight_fixture;

bool p_any(const ScrClause&, const ScrWorld&, const ScrRuntime&) { return false; }
void a_any(const ScrAction&, ScrCtx&) {}

// `boss`'s export, generated once per run into the build directory.
const std::string& export_json(const std::string& boss) {
    static std::map<std::string, std::string> cache;
    auto it = cache.find(boss);
    if (it != cache.end()) return it->second;
    const fs::path out = fs::path(SOTC_TEST_BUILD_DIR) / "fixtures" / (boss + ".lib.graph.json");
    fs::create_directories(out.parent_path());
    const std::string cmd = "python3 -I '" + (kRepoRoot / "tools/re/scr/graph_export.py").string() + "' export " +
                            boss + " '" + out.string() + "'";
    if (std::system(cmd.c_str()) != 0) throw std::runtime_error("graph_export.py failed (out/re/eboot.elf?): " + cmd);
    return cache.emplace(boss, read_file(out)).first->second;
}

// Accepts every fn and kind the export names: the cases call the library's handlers directly.
ScrRegistry permissive(const std::string& json) {
    ScrRegistry r;
    std::function<void(const Json&)> walk = [&](const Json& v) {
        if (v.kind == Json::Object) {
            for (const auto& [key, child] : v.object) {
                if (key == "fn" && child.kind == Json::String) {
                    const uint32_t fn = static_cast<uint32_t>(std::strtoul(child.string.c_str() + 2, nullptr, 16));
                    r.predicates[fn] = p_any;
                    r.actions[fn] = a_any;
                }
                walk(child);
            }
        } else if (v.kind == Json::Array) {
            for (const Json& child : v.array) walk(child);
        }
    };
    walk(json_parse(json));
    for (int k = -1; k <= 10; ++k) r.kinds.insert(k);
    return r;
}

struct Boss {
    ScrRegistry reg;
    ScrGraph graph;
};

const Boss& boss(const std::string& name) {
    static std::map<std::string, Boss> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    const std::string& json = export_json(name);
    Boss b;
    b.reg = permissive(json);
    std::string error;
    const bool ok = scr_graph_load(json, export_clips(json), b.reg, b.graph, error);
    INFO(name << ": " << error);
    REQUIRE(ok);
    return cache.emplace(name, std::move(b)).first->second;
}

const ScrRegistry& library() {
    static const ScrRegistry r = [] {
        ScrRegistry reg;
        scr_library_add(reg);
        return reg;
    }();
    return r;
}

// The first clause of condition `key` in `name`'s export calling `fn`.
const ScrClause& clause(const std::string& name, const std::string& key, uint32_t fn) {
    for (const ScrCondition& c : boss(name).graph.conditions) {
        if (c.key != key) continue;
        for (const ScrClause& cl : c.clauses) {
            if (cl.fn == fn) return cl;
        }
    }
    throw std::runtime_error(name + " " + key + ": no clause of that fn");
}

// The first record of action list `key` in `name`'s export calling `fn`.
const ScrAction& action(const std::string& name, const std::string& key, uint32_t fn) {
    for (const ScrActionList& l : boss(name).graph.actions) {
        if (l.key != key) continue;
        for (const ScrAction& a : l.records) {
            if (a.fn == fn) return a;
        }
    }
    throw std::runtime_error(name + " " + key + ": no record of that fn");
}

bool has(const std::vector<int>& controls, int bit) {
    for (const int c : controls) {
        if (c == bit) return true;
    }
    return false;
}

// Runs `a` with the library's handler on `rt`/`w`; returns the requests.
std::vector<int> run(const std::string& name, const ScrAction& a, ScrRuntime& rt, const ScrWorld& w) {
    ScrStepOut out;
    ScrCtx ctx{boss(name).graph, library(), rt, w, out};
    library().actions.at(a.fn)(a, ctx);
    return out.controls;
}

}  // namespace

TEST_CASE("each D5 predicate, one exported row, evaluated both ways") {
    struct Row {
        const char* boss;
        const char* cond;
        uint32_t fn;
        std::function<void(const ScrClause&, ScrWorld&, ScrRuntime&, bool)> make;  // make the row `want`
    };
    const auto f = [](const ScrClause& c, const char* slot) { return static_cast<float>(scr_arg(c.args, slot)->num); };
    const std::vector<Row> rows = {
        {"KIRIN", "KIRIN:C6", 0x82afd0, [&](auto& c, auto&, ScrRuntime& rt, bool want) {  // anger_ge 1
             rt.anger = f(c, "f1c") + (want ? 0.0f : -0.5f);
         }},
        {"MAMMOTH", "MAMMOTH:C39", 0x82b000, [&](auto& c, auto&, ScrRuntime& rt, bool want) {  // anger_le 30
             rt.anger = f(c, "f1c") + (want ? 0.0f : 0.5f);
         }},
        {"KIRIN", "ALWAYS:C7", 0x832660, [](auto&, ScrWorld& w, auto&, bool want) {  // hit_kind 0: {0,2,4,5}
             w.hit = true;
             w.hitKind = want ? 5 : 3;
         }},
        {"KIRIN", "ALWAYS:C7", 0x832660, [](auto&, ScrWorld& w, auto&, bool want) {  // no hit this frame
             w.hit = want;
             w.hitKind = 0;
         }},
        {"KIRIN", "ALWAYS:C16", 0x832660, [](auto&, ScrWorld& w, auto&, bool want) {  // hit_kind 2: == 2
             w.hit = true;
             w.hitKind = want ? 2 : 0;
         }},
        {"KIRIN", "KIRIN:C123", 0x832710, [](auto&, ScrWorld& w, auto&, bool want) {  // hit_region 0x3f0 == 0x20
             w.hit = true;
             w.hitRegion = want ? 0x20 : 0x10;
         }},
        {"KAME", "KAME:C45", 0x834730, [](auto&, ScrWorld& w, auto&, bool want) {  // hp_pct_ge 50
             w.hpMax = 160.0f;
             w.hp = want ? 80.0f : 79.0f;
         }},
        {"POSEIDON", "POSEIDON:C180", 0x834770, [](auto&, ScrWorld& w, auto&, bool want) {  // hp_pct_le 50
             w.hpMax = 160.0f;
             w.hp = want ? 80.0f : 81.0f;
         }},
        {"KIRIN", "KIRIN:C50", 0x833d80, [&](auto& c, auto&, ScrRuntime& rt, bool want) {  // timer 2 >= 35
             rt.timer2 = f(c, "f1c") + (want ? 0.5f : -0.5f);
             rt.timer1 = f(c, "f1c") + (want ? -0.5f : 0.5f);  // timer 1 is not read
         }},
        {"MINOTOUR_A", "ALWAYS:C103", 0x834700, [&](auto& c, auto&, ScrRuntime& rt, bool want) {  // >= 15 s
             rt.timeSinceAttack = f(c, "f1c") + (want ? 0.5f : -0.5f);
         }},
        {"YAMORI_B", "YAMORI_B:C105", 0x8350d0, [](auto& c, auto&, ScrRuntime& rt, bool want) {  // mode_is 3
             rt.current = scr_arg(c.args, "h50")->mode + (want ? 0 : 1);
         }},
        {"MINOTOUR_A", "MINOTOUR_A:C122", 0x8332f0, [](auto&, ScrWorld& w, auto&, bool want) {  // control-mode[6]
             w.bossControlMode = want ? 6 : 5;
         }},
        {"MINOTOUR_A", "MINOTOUR_A:C156", 0x8338e0, [](auto&, auto&, ScrRuntime& rt, bool want) {  // seen now
             rt.seenNow = want;
             rt.sensed = !want;  // the latch is not read
         }},
        {"MINOTOUR_A", "MINOTOUR_A:C156", 0x8338e0, [](auto&, auto&, ScrRuntime& rt, bool want) {  // near now
             rt.nearNow = want;
         }},
        {"MAMMOTH", "MAMMOTH:C43", 0x833a20, [](auto&, ScrWorld& w, auto&, bool want) {  // |bearing| <= 20
             w.bearingDeg = want ? -20.0f : 21.0f;
         }},
        {"YAMORI_B", "YAMORI_B:C281", 0x8339e0, [](auto&, ScrWorld& w, auto&, bool want) {  // 3-D distance <= 10
             w.distanceHorizontal = 5.0f;
             w.distance3d = want ? 10.0f : 10.5f;
         }},
        {"NARGA", "ALWAYS:C36", 0x832e30, [](auto&, ScrWorld& w, auto&, bool want) { w.playerRiding = want; }},
        {"KIRIN", "KIRIN:C213", 0x832f30, [](auto&, ScrWorld& w, auto&, bool want) {  // owner kind 0x400000
             w.bossGroundAttr = want ? (uint64_t{5} << 48 | 0x400000u) : 0x800000u;
         }},
        {"KNIGHT", "KNIGHT:C123", 0x833200, [](auto& c, ScrWorld& w, auto&, bool want) {  // the base motion
             w.bossMotion = want ? scr_arg(c.args, "h50")->clip : -1;
         }},
        {"KNIGHT", "KNIGHT:C123", 0x833200, [](auto& c, ScrWorld& w, auto&, bool want) {  // another layer
             const int clip = scr_arg(c.args, "h50")->clip;
             w.bossMotion = -1;
             w.bossLayerPlaying = [clip, want](int c2) { return want && c2 == clip; };
         }},
    };
    for (const Row& row : rows) {
        const ScrClause& c = clause(row.boss, row.cond, row.fn);
        const ScrPredicateFn pred = library().predicates.at(row.fn);
        for (const bool want : {true, false}) {
            ScrWorld w;
            ScrRuntime rt;
            row.make(c, w, rt, want);
            INFO(row.boss << " " << row.cond << " " << c.pred << " want " << want);
            CHECK(pred(c, w, rt) == want);
        }
    }

    // hit_kind's selector 1 ({1, 3}): no export uses it, so a copy of the 0 row with i10 = 1.
    ScrClause one = clause("KIRIN", "ALWAYS:C7", 0x832660);
    for (ScrArg& a : one.args) {
        if (a.slot == "i10") a.num = 1.0;
    }
    ScrWorld w;
    w.hit = true;
    for (const int kind : {1, 3}) {
        w.hitKind = kind;
        CHECK(library().predicates.at(0x832660)(one, w, ScrRuntime{}));
    }
    w.hitKind = 0;
    CHECK_FALSE(library().predicates.at(0x832660)(one, w, ScrRuntime{}));

    // Without a player: the bearing, 3-D distance and riding rows are false.
    ScrWorld none;
    none.playerPresent = false;
    none.playerRiding = true;
    for (const auto& [name, key, fn] : {std::tuple{"MAMMOTH", "MAMMOTH:C43", 0x833a20u},
                                        std::tuple{"YAMORI_B", "YAMORI_B:C281", 0x8339e0u},
                                        std::tuple{"NARGA", "ALWAYS:C36", 0x832e30u}}) {
        INFO(key);
        CHECK_FALSE(library().predicates.at(fn)(clause(name, key, fn), none, ScrRuntime{}));
    }

    // The labelled gaps: false whatever the world, and listed as gaps.
    for (const auto& [name, key, fn] :
         {std::tuple{"KIRIN", "ALWAYS:C137", 0x835290u}, std::tuple{"MINOTOUR_A", "MINOTOUR_A:C5", 0x834fb0u}}) {
        INFO(key);
        CHECK_FALSE(library().predicates.at(fn)(clause(name, key, fn), ScrWorld{}, ScrRuntime{}));
        CHECK(library().gaps.count(fn) == 1);
    }
}

TEST_CASE("each D5 action, one exported record") {
    ScrWorld w;
    ScrRuntime rt;

    // anger_set 30 (KIRIN on any hit), anger_add -50 (MAMMOTH after an angry attack).
    run("KIRIN", action("KIRIN", "KIRIN:F10", 0x8313d0), rt, w);
    CHECK(rt.anger == doctest::Approx(30.0f));
    rt.anger = 80.0f;
    run("MAMMOTH", action("MAMMOTH", "MAMMOTH:F28", 0x831400), rt, w);
    CHECK(rt.anger == doctest::Approx(30.0f));

    // goal_lastseen (f10 0): the last-known point, not the live player.
    rt.lastKnown = {3.0f, 1.0f, 4.0f};
    rt.arrived = true;
    w.playerPosition = {50.0f, 0.0f, 50.0f};
    run("MAMMOTH", action("MAMMOTH", "ALWAYS:F4", 0x82e500), rt, w);
    CHECK(rt.hasGoal);
    CHECK_FALSE(rt.arrived);
    CHECK(rt.goal.x == doctest::Approx(3.0f));
    CHECK(rt.goal.y == doctest::Approx(1.0f));
    CHECK(rt.goal.z == doctest::Approx(4.0f));

    // turn_to_player (f10 30): game bearing (R5: minus the app's) past +30 -> Stop_right_turn,
    // past -30 -> Stop_turn_left, inside -> nothing.
    const ScrAction& turn = action("MAMMOTH", "MAMMOTH:F20", 0x82fae0);
    w.bearingDeg = -40.0f;  // 40 deg to his right
    CHECK(run("MAMMOTH", turn, rt, w) == std::vector<int>{kScrCtrlStopTurnRight});
    w.bearingDeg = 40.0f;
    CHECK(run("MAMMOTH", turn, rt, w) == std::vector<int>{kScrCtrlStopTurnLeft});
    w.bearingDeg = 20.0f;
    CHECK(run("MAMMOTH", turn, rt, w).empty());
}

TEST_CASE("the anger model: KIRIN's {decay 1, a 0, b 0}, a kind-1 hit after anger_set 30 reads 30, 29 after 1 s") {
    const Boss& b = boss("KIRIN");
    REQUIRE(b.graph.root.anger.size() == 4);
    CHECK(b.graph.root.anger[0] == doctest::Approx(1.0f));
    CHECK(b.graph.root.anger[1] == doctest::Approx(0.0f));
    CHECK(b.graph.root.anger[2] == doctest::Approx(0.0f));
    ScrRuntime rt;  // no mode: only the library's steps 2-5 and the every-frame list (permissive) run
    ScrWorld w;
    run("KIRIN", action("KIRIN", "KIRIN:F10", 0x8313d0), rt, w);
    w.hit = true;
    w.hitKind = 1;
    scr_boss_step(b.graph, b.reg, rt, w, 0.1f);
    CHECK(rt.anger == doctest::Approx(30.0f));
    w.hit = false;
    for (int i = 0; i < 10; ++i) scr_boss_step(b.graph, b.reg, rt, w, 0.1f);
    CHECK(rt.anger == doctest::Approx(29.0f));

    // Clamped to [0, 100]; a null root.anger runs no model.
    rt.anger = 0.5f;
    scr_boss_step(b.graph, b.reg, rt, w, 1.0f);
    CHECK(rt.anger == 0.0f);
    ScrGraph withA = b.graph;
    withA.root.anger = {1.0f, 80.0f, 0.0f, 0.0f};  // a hand-built row: a = 80
    rt.anger = 50.0f;
    w.hit = true;
    w.hitKind = 4;
    scr_boss_step(withA, b.reg, rt, w, 0.1f);
    CHECK(rt.anger == 100.0f);
    withA.root.anger.clear();
    rt.anger = 50.0f;
    w.hit = false;
    scr_boss_step(withA, b.reg, rt, w, 1.0f);
    CHECK(rt.anger == 50.0f);
}

TEST_CASE("KNIGHT's exported attackdata plus the sidecar's ATTAIN cylinders equal S12's kAttack") {
    // S12's hand-transcribed kAttack (data 0x2232a98), copied here before its deletion: rows 0..2
    // of each table (its terminators rows 3 and 7 are not exported, R-I).
    struct S12Row {
        const char *fix, *atk, *attain;
        int issue;
        const char* motion;
        float reselect, cooldownS;
        bool issueArrived, isDefault;
        float attainX, attainZ, attainR;
    };
    const S12Row legL{"KNIGHT_LEG_L_ATK_FIX", "KNIGHT_LEG_L_ATK", "KNIGHT_LEG_L_ATK_ATTAIN", 3,
                      "knight_a_atk_leg_l_start", 5.0f, 15.0f, false, false, 3.0f, 7.0f, 2.0f};
    const S12Row legR{"KNIGHT_LEG_R_ATK_FIX", "KNIGHT_LEG_R_ATK", "KNIGHT_LEG_R_ATK_ATTAIN", 2,
                      "knight_a_atk_leg_r_start", 5.0f, 15.0f, false, false, -3.0f, 7.0f, 2.0f};
    const std::map<std::string, std::vector<S12Row>> s12 = {
        {"KNIGHT_SWORD:scr_attackdata[0]",
         {legL, legR,
          {"KNIGHT_WEAPON_ATK_FIX", "KNIGHT_WEAPON_ATK", "KNIGHT_WEAPON_ATK_ATTAIN", 4, "knight_a_atk1_start", 15.0f,
           10.0f, true, true, 0.0f, 30.0f, 5.0f}}},
        {"KNIGHT_SWORD:scr_attackdata[4]",
         {legL, legR,
          {"KNIGHT_WEAPON_ATK2_FIX", "KNIGHT_WEAPON_ATK2", "KNIGHT_WEAPON_ATK2_ATTAIN", 5, "knight_a_atk2_start",
           15.0f, 8.0f, true, true, 0.0f, 16.0f, 4.0f}}},
    };

    const ScrRegistry reg = scr_registry_knight();
    ScrGraph g;
    std::string error;
    REQUIRE_MESSAGE(scr_graph_load(knight_json(), export_clips(knight_json()), reg, g, error), error);
    const char* lib = std::getenv("SOTC_LIB");
    if (lib == nullptr || lib[0] == '\0') throw std::runtime_error("Set SOTC_LIB to the library view.");
    EventsFile events;
    REQUIRE_MESSAGE(events_load(std::string(lib) + "/models/shadow/characters/colossus_03_knight/_cmn/"
                                                   "colossus_03_knight.events.json",
                                events, error),
                    error);
    REQUIRE_MESSAGE(scr_attack_attain(g, events.judgeDefaults, error), error);

    CHECK(g.attackdata.size() == s12.size());
    CHECK(g.modes[9].locomotion.attack == "KNIGHT_SWORD:scr_attackdata[0]");
    CHECK(g.modes[10].locomotion.attack == "KNIGHT_SWORD:scr_attackdata[4]");
    for (const auto& [key, want] : s12) {
        REQUIRE(g.attackdata.count(key) == 1);
        const std::vector<ScrAttackRow>& rows = g.attackdata.at(key);
        REQUIRE(rows.size() == want.size());
        for (size_t i = 0; i < rows.size(); ++i) {
            const ScrAttackRow& r = rows[i];
            const S12Row& s = want[i];
            INFO(key << " row " << i);
            CHECK(r.fix == s.fix);
            CHECK(r.atk == s.atk);
            CHECK(r.attain == s.attain);
            CHECK(r.issue == s.issue);
            REQUIRE(r.motion >= 0);
            CHECK(g.clipNames[static_cast<size_t>(r.motion)] == s.motion);
            CHECK(r.reselectS == s.reselect);
            CHECK(r.cooldownS == s.cooldownS);
            CHECK(r.issueArrived == s.issueArrived);
            CHECK(r.isDefault == s.isDefault);
            CHECK(r.attainX == s.attainX);
            CHECK(r.attainZ == s.attainZ);
            CHECK(r.attainR == s.attainR);
            // S12's every-row facts: sensing 0, condition null, no control-mode list, 100 %, no chain.
            CHECK(r.sensing == 0);
            CHECK(r.cond == -1);
            CHECK(r.ctrlModes.empty());
            CHECK(r.chainPct == 100.0f);
            CHECK(r.chain.empty());
        }
    }

    // An `attain` the sidecar does not carry is refused, naming it.
    std::vector<EventsVolume> missing;
    for (const EventsVolume& v : events.judgeDefaults) {
        if (v.name != "KNIGHT_WEAPON_ATK2_ATTAIN") missing.push_back(v);
    }
    CHECK_FALSE(scr_attack_attain(g, missing, error));
    CHECK(error.find("KNIGHT_WEAPON_ATK2_ATTAIN") != std::string::npos);
}

TEST_CASE("scr_slowleg: walks at a goal ahead, faces a point once arrived, holds still on its own goal") {
    ScrWorld w;  // at the origin facing +z
    ScrRuntime rt;
    ScrStepOut out;
    scr_slowleg(rt, w, {0.0f, 0.0f, 40.0f}, false, out);
    CHECK(out.controls == std::vector<int>{kScrCtrlAccel});

    // Arrived, a face point 90 deg to his left: a stop-turn left; within 3 deg: the brake.
    out = {};
    scr_slowleg(rt, w, {10.0f, 0.0f, 0.0f}, true, out);
    CHECK(out.controls == std::vector<int>{kScrCtrlStopTurnLeft});
    out = {};
    scr_slowleg(rt, w, {0.1f, 0.0f, 10.0f}, true, out);
    CHECK(out.controls == std::vector<int>{kScrCtrlBrake});

    // Arrived and handed the goal itself (Task 8: the kind sets no facing): no turn, whatever its
    // bearing, and none under his root.
    rt.goal = {-3.0f, 0.0f, -1.0f};
    for (const Vec3 goal : {rt.goal, Vec3{0.0f, 0.0f, 0.0f}, Vec3{1e-4f, 0.0f, -1e-4f}}) {
        out = {};
        scr_slowleg(rt, w, goal, true, out);
        CHECK(out.controls == std::vector<int>{kScrCtrlBrake});
        CHECK(rt.steerDeg == 0.0f);
    }
}

TEST_CASE("scr_boss_step hands a kind the library does not run to registry.locomotion") {
    const Boss& b = boss("KIRIN");
    ScrRegistry reg = b.reg;
    static int seen = -100;
    reg.locomotion = [](int kind, ScrCtx& ctx, float) {
        seen = kind;
        scr_request(ctx.out, 7);
    };
    ScrRuntime rt;
    rt.locoKind = 5;
    const ScrStepOut out = scr_boss_step(b.graph, reg, rt, ScrWorld{}, 0.1f);
    CHECK(seen == 5);
    CHECK(has(out.controls, 7));

    seen = -100;
    rt.locoKind = 1;  // the library's own: the hook is not called
    scr_boss_step(b.graph, reg, rt, ScrWorld{}, 0.1f);
    CHECK(seen == -100);
}

TEST_CASE("the registry dump lists the library's gaps") {
    const std::string dump = scr_registry_dump(scr_registry_knight());
    for (const char* line : {"835290 gap\n", "834fb0 gap\n", "832660 implemented\n", "82afd0 implemented\n",
                             "6fe5e0 implemented\n", "kind 10\n"}) {
        CHECK_MESSAGE(dump.find(line) != std::string::npos, line);
    }
}
