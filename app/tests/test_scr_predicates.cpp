// Gaius's predicates, actions and control resolution (S12 Task 5). The fixture is his
// real graph: tools/re/scr/graph_export.py runs at test time into the build directory
// from out/re/eboot.elf (the case fails, never skips, without it); nothing derived is in
// git. The judge-name case reads his sidecar from SOTC_LIB (the branch's view).
#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "json_reader.h"
#include "knight_fixture.h"
#include "objanim_events.h"
#include "scr_graph.h"
#include "scr_predicates.h"

namespace {

namespace fs = std::filesystem;
namespace k = scr_knight;

using namespace knight_fixture;

struct Knight {
    ScrRegistry reg = scr_registry_knight();
    ScrGraph graph;
    Knight() {
        std::string error;
        const bool ok = scr_graph_load(knight_json(), export_clips(knight_json()), reg, graph, error);
        INFO(error);
        REQUIRE(ok);
    }
    int cond(const std::string& key) const {
        for (size_t i = 0; i < graph.conditions.size(); ++i) {
            if (graph.conditions[i].key == key) return static_cast<int>(i);
        }
        FAIL("no condition " << key);
        return -1;
    }
    const ScrClause& clause(const std::string& key, size_t i) const {
        const ScrCondition& c = graph.conditions[cond(key)];
        REQUIRE(i < c.clauses.size());
        return c.clauses[i];
    }
    int clip(const char* name) const {
        const int i = scr_clip(graph, name);
        REQUIRE(i >= 0);
        return i;
    }
};

const Knight& knight() {
    static const Knight kn;
    return kn;
}

bool has(const std::vector<int>& controls, int bit) {
    return std::find(controls.begin(), controls.end(), bit) != controls.end();
}

// Gaius at the origin facing +z on flat ground, the player `ahead` m in front.
ScrWorld world_ahead(float ahead) {
    ScrWorld w;
    w.bossPosition = {0.0f, 0.0f, 0.0f};
    w.playerPosition = {0.0f, 0.0f, ahead};
    w.playerGroundPosition = w.playerPosition;
    w.groundAt = [](float, float, float& h) {
        h = 0.0f;
        return true;
    };
    scr_world_relate(w);
    return w;
}

std::vector<std::string> sidecar_judges() {
    const char* lib = std::getenv("SOTC_LIB");
    if (lib == nullptr || lib[0] == '\0') throw std::runtime_error("Set SOTC_LIB to the library view.");
    EventsFile file;
    std::string error;
    const std::string path =
        std::string(lib) + "/models/shadow/characters/colossus_03_knight/_cmn/colossus_03_knight.events.json";
    if (!events_load(path, file, error)) throw std::runtime_error(error);
    std::vector<std::string> names;
    for (const EventsVolume& v : file.judgeDefaults) names.push_back(v.name);
    return names;
}

}  // namespace

TEST_CASE("KNIGHT's graph loads with scr_registry_knight() and no unknown fn") {
    const Knight& kn = knight();
    CHECK(kn.graph.modes.size() == 13);
    CHECK(kn.graph.conditions.size() == 53);

    // Exactly his fns: the export's predicates minus `sub`, and his action kinds.
    std::set<uint32_t> preds, acts, regPreds, regActs;
    for (const ScrCondition& c : kn.graph.conditions) {
        for (const ScrClause& cl : c.clauses) {
            if (cl.fn != kScrSubFn) preds.insert(cl.fn);
        }
    }
    for (const ScrActionList& l : kn.graph.actions) {
        for (const ScrAction& a : l.records) acts.insert(a.fn);
    }
    for (const auto& [fn, f] : kn.reg.predicates) regPreds.insert(fn);
    for (const auto& [fn, f] : kn.reg.actions) regActs.insert(fn);
    CHECK(preds.size() == 23);
    CHECK(regPreds == preds);
    CHECK(acts.size() == 18);
    CHECK(regActs == acts);
    CHECK(kn.reg.predicates.count(kScrSubFn) == 0);
    CHECK(kn.reg.enterMode != nullptr);

    std::string error;
    CHECK_MESSAGE(scr_knight_check(kn.graph, sidecar_judges(), error), error);
}

TEST_CASE("scr_knight_check fails loudly on a judge volume the sidecar does not have") {
    const Knight& kn = knight();
    std::vector<std::string> judges = sidecar_judges();
    judges.erase(std::remove(judges.begin(), judges.end(), "KNIGHT_WEAPON_ATK2_FIX"), judges.end());
    std::string error;
    CHECK_FALSE(scr_knight_check(kn.graph, judges, error));
    CHECK(error.find("KNIGHT_WEAPON_ATK2_FIX") != std::string::npos);
}

TEST_CASE("every predicate, one row of KNIGHT's export, evaluated both ways") {
    const Knight& kn = knight();
    struct Row {
        const char* cond;
        size_t clause;
        std::function<void(const ScrClause&, ScrWorld&, ScrRuntime&, bool)> make;  // make the row `want`
        bool twoWay = true;
    };
    const auto f = [](const ScrClause& c, const char* slot) { return static_cast<float>(scr_arg(c.args, slot)->num); };
    const auto clip = [](const ScrClause& c) { return scr_arg(c.args, "h50")->clip; };
    const std::vector<Row> rows = {
        {"KNIGHT:C5", 1, [](auto&, ScrWorld& w, auto&, bool want) { w.playerGroundAttr = want ? 0x800000u : 0u; }},
        {"ALWAYS:C32", 0, [](auto&, ScrWorld& w, auto&, bool want) { w.playerAttached = want; }},
        {"KNIGHT:C33", 0, [&](auto& c, ScrWorld& w, auto&, bool want) { w.bossMotion = want ? clip(c) : clip(c) + 1; }},
        {"ALWAYS:C120", 0, [](auto&, ScrWorld& w, auto&, bool want) {
             w.playerMotion = 604;  // PLAYER:ph_motion_def[604], the finger whistle
             w.playerMotionStarted = want;
         }},
        {"KNIGHT:C3", 0, [&](auto& c, ScrWorld& w, auto&, bool want) {
             w.bossPreviousMotion = clip(c);
             w.bossMotionSwitched = want;
         }},
        {"KNIGHT:C54", 0, [](auto&, ScrWorld& w, auto&, bool want) { w.bearingDeg = want ? -30.0f : 30.0f; }},
        {"ALWAYS:C95", 0, [](auto&, auto&, ScrRuntime& rt, bool want) { rt.sensed = want; }},
        {"KNIGHT:C60", 0, [&](auto& c, ScrWorld& w, auto&, bool want) {
             w.distanceHorizontal = f(c, "f1c") + (want ? 1.0f : -1.0f);
         }},
        {"KNIGHT:C0", 1, [](auto&, ScrWorld& w, auto&, bool want) { w.flags.set(417, want); }},
        {"KNIGHT:C54", 1, [&](auto& c, ScrWorld& w, auto&, bool want) {
             w.distanceHorizontal = f(c, "f1c") + (want ? -1.0f : 1.0f);
         }},
        {"ALWAYS:C1", 0, [](auto&, auto&, auto&, bool) {}, false},  // true: true only
        {"ALWAYS:C111", 0, [](auto&, auto&, ScrRuntime& rt, bool want) { rt.arrived = want; }},
        {"KNIGHT:C0", 0, [&](auto& c, auto&, ScrRuntime& rt, bool want) { rt.timer1 = f(c, "f1c") + (want ? 0.5f : -0.5f); }},
        {"ALWAYS:C28", 0, [](auto&, auto&, auto&, bool) {}, false},  // no arrows in the app: false only
        {"KNIGHT:C92", 0, [&](auto& c, ScrWorld& w, auto&, bool want) {
             w.bossMotion = clip(c);
             w.bossMotionSwitched = want;
         }},
        {"ALWAYS:C107", 0, [](auto&, auto&, ScrRuntime& rt, bool want) {
             rt.locoKind = 0;
             rt.hasGoal = !want;
         }},
        {"ALWAYS:C5", 0, [](auto&, ScrWorld& w, auto&, bool want) { w.hit = want; }},
        {"ALWAYS:C71", 0, [](auto&, ScrWorld& w, auto&, bool want) { w.playerControlMode = want ? 79 : 3; }},
        {"ALWAYS:C71", 1, [](auto&, ScrWorld& w, auto&, bool want) { w.playerMotion = want ? 258 : 257; }},
        {"KNIGHT:C123", 0, [&](auto& c, ScrWorld& w, auto&, bool want) { w.bossMotion = want ? clip(c) : -1; }},
        {"KNIGHT:C123", 1, [](auto&, auto&, ScrRuntime& rt, bool want) {
             rt.current = 9;
             rt.transitioned = want;
         }},
        {"KNIGHT:C18", 0, [](auto&, auto&, ScrRuntime& rt, bool want) { rt.sensed = !want; }},
        {"KNIGHT:C39", 0, [](auto&, ScrWorld& w, auto&, bool want) {
             w.groundAt = [want](float, float z, float& h) {
                 h = !want && z > 20.0f && z < 21.0f ? 2.0f : 0.0f;  // a 2 m wall across all three rays
                 return true;
             };
         }},
        {"KNIGHT:C94", 1, [](auto&, auto&, ScrRuntime& rt, bool want) { rt.bossBits = want ? 1u : 0u; }},
    };

    std::set<uint32_t> covered;
    for (const Row& row : rows) {
        const ScrClause& c = kn.clause(row.cond, row.clause);
        covered.insert(c.fn);
        const ScrPredicateFn pred = kn.reg.predicates.at(c.fn);
        for (const bool want : {true, false}) {
            if (!row.twoWay) continue;
            ScrWorld w;
            ScrRuntime rt;
            row.make(c, w, rt, want);
            INFO(row.cond << "[" << row.clause << "] " << c.pred << " want " << want);
            CHECK(pred(c, w, rt) == want);
        }
        if (!row.twoWay) {
            INFO(row.cond << " " << c.pred);
            CHECK(pred(c, ScrWorld{}, ScrRuntime{}) == (c.fn == 0x832600));
        }
    }
    std::set<uint32_t> registered;
    for (const auto& [fn, p] : kn.reg.predicates) registered.insert(fn);
    CHECK(covered == registered);

    // Without a player: sensed/dist_le/bearing false, not_sensed/dist_ge true (findings §2).
    ScrWorld none;
    none.playerPresent = false;
    ScrRuntime sensed;
    sensed.sensed = true;
    const auto eval = [&](const char* key, size_t i, const ScrRuntime& rt) {
        const ScrClause& c = kn.clause(key, i);
        return kn.reg.predicates.at(c.fn)(c, none, rt);
    };
    CHECK_FALSE(eval("ALWAYS:C95", 0, sensed));
    CHECK(eval("KNIGHT:C18", 0, sensed));
    CHECK_FALSE(eval("KNIGHT:C54", 1, sensed));
    CHECK(eval("KNIGHT:C60", 0, sensed));
    CHECK_FALSE(eval("KNIGHT:C51", 0, sensed));
}

TEST_CASE("player_bearing_in follows R5: a player 30 deg to his left satisfies the left-range row") {
    const Knight& kn = knight();
    ScrRuntime rt;
    ScrWorld w;
    w.bossPosition = {0.0f, 0.0f, 0.0f};
    w.bossYawDeg = 0.0f;
    // 30 deg to his left = toward glb +x (the model's left; the app bearing's +).
    w.playerPosition = {10.0f * std::sin(30.0f * kDegToRad), 0.0f, 10.0f * std::cos(30.0f * kDegToRad)};
    scr_world_relate(w);
    CHECK(w.bearingDeg == doctest::Approx(30.0f));
    CHECK(scr_eval(kn.graph, kn.reg, kn.cond("KNIGHT:C57"), rt, w));   // left foot: game [-60, 0]
    CHECK_FALSE(scr_eval(kn.graph, kn.reg, kn.cond("KNIGHT:C54"), rt, w));  // right foot: game [0, 60]

    // bearing_positive_is_right (R5): the player at glb (-10, 0, 10) is on his right.
    w.playerPosition = {-10.0f, 0.0f, 10.0f};
    scr_world_relate(w);
    CHECK(w.bearingDeg == doctest::Approx(-45.0f));
    CHECK(scr_eval(kn.graph, kn.reg, kn.cond("KNIGHT:C54"), rt, w));
    CHECK_FALSE(scr_eval(kn.graph, kn.reg, kn.cond("KNIGHT:C57"), rt, w));

    // His yaw turns the frame: facing +x (yaw 90), his left is -z.
    w.bossYawDeg = 90.0f;
    w.playerPosition = {0.0f, 0.0f, -10.0f};
    scr_world_relate(w);
    CHECK(w.bearingDeg == doctest::Approx(90.0f));
}

TEST_CASE("sensed latches after relposparam[7] s in sight and releases after [6] s out of sight") {
    const Knight& kn = knight();
    ScrRuntime rt;
    rt.relpos = kn.graph.modes[3].locomotion.perception;  // {10, 100, 200, 60, 45, -55, 5, 0.5, 2, 10}
    REQUIRE(rt.relpos.size() == 10);
    const float sight = rt.relpos[7], forget = rt.relpos[6];
    CHECK(sight == doctest::Approx(0.5f));
    CHECK(forget == doctest::Approx(5.0f));

    ScrWorld w = world_ahead(50.0f);  // in sight (inside 200 m, 0 deg off his facing), not near (> 10 m)
    const float dt = 0.05f;
    float t = 0.0f;
    while (!rt.sensed && t < 2.0f) {
        scr_perceive(rt, w, dt);
        t += dt;
    }
    CHECK(rt.sensed);
    CHECK(t == doctest::Approx(sight).epsilon(0.15));

    w = world_ahead(-50.0f);  // behind him: outside the 60 deg half-FOV, beyond the 10 m near radius
    t = 0.0f;
    while (rt.sensed && t < 10.0f) {
        scr_perceive(rt, w, dt);
        t += dt;
    }
    CHECK_FALSE(rt.sensed);
    CHECK(t == doctest::Approx(forget).epsilon(0.02));

    // A blocked sight ray is out of sight; the near radius senses without sight after [8] s.
    ScrRuntime near;
    near.relpos = rt.relpos;
    w = world_ahead(-5.0f);
    t = 0.0f;
    while (!near.sensed && t < 5.0f) {
        scr_perceive(near, w, dt);
        t += dt;
    }
    CHECK(t == doctest::Approx(near.relpos[8]).epsilon(0.05));

    ScrRuntime blocked;
    blocked.relpos = rt.relpos;
    w = world_ahead(50.0f);
    w.ray = [](const Vec3&, const Vec3&, Vec3& hit) {
        hit = {0.0f, 0.0f, 20.0f};
        return true;
    };
    for (int i = 0; i < 40; ++i) scr_perceive(blocked, w, dt);
    CHECK_FALSE(blocked.sensed);
}

TEST_CASE("swing_space_clear: a 2 m wall 20 m ahead blocks the centre ray; a 30 m drop does not") {
    ScrWorld w = world_ahead(30.0f);
    w.bossPosition = {0.0f, 47.657f, 0.0f};  // his root ground on the tower top
    w.groundAt = [](float x, float z, float& h) {
        h = 47.657f + (std::fabs(x) < 1.0f && z > 19.5f && z < 20.5f ? 2.0f : 0.0f);
        return true;
    };
    CHECK_FALSE(scr_swing_space_clear(w, 35.0f, 20.0f, 1.0f));

    w.groundAt = [](float, float z, float& h) {
        h = z > 20.0f ? 47.657f - 30.0f : 47.657f;
        return true;
    };
    CHECK(scr_swing_space_clear(w, 35.0f, 20.0f, 1.0f));

    // No ground at all past the edge is a drop too; a 0.9 m rise is still clear.
    w.groundAt = [](float, float z, float& h) {
        h = 47.657f;
        return z < 20.0f;
    };
    CHECK(scr_swing_space_clear(w, 35.0f, 20.0f, 1.0f));
    w.groundAt = [](float, float, float& h) {
        h = 47.657f + 0.9f;
        return true;
    };
    CHECK(scr_swing_space_clear(w, 35.0f, 20.0f, 1.0f));

    // A wall only on his right blocks the right ray (glb -x is his right).
    w.bossYawDeg = 0.0f;
    w.groundAt = [](float x, float, float& h) {
        h = 47.657f + (x < -15.0f ? 3.0f : 0.0f);
        return true;
    };
    CHECK_FALSE(scr_swing_space_clear(w, 35.0f, 20.0f, 1.0f));
}

namespace {

// Gaius from the retry start (mode 3) at the stand, the player 30 m ahead on the arena
// ground; the judge stub says the player is inside every WEAPON volume (the slam's
// and the stab's) and no LEG volume. Steps until an attack control is requested.
struct Fight {
    const Knight& kn = knight();
    ScrRuntime rt;
    ScrWorld w = world_ahead(30.0f);
    std::vector<int> attack;  // the first frame's requested attack bits
    std::vector<int> modes;

    explicit Fight(bool wall) {
        w.playerGroundAttr = 0x800000u;  // the arena ground
        w.bossMotion = kn.clip("knight_a_stand");
        w.bossControlMode = 1;
        w.playerInJudge = [](std::string_view name) { return name.find("WEAPON") != std::string_view::npos; };
        if (wall) {
            w.groundAt = [](float, float z, float& h) {
                h = z > 20.0f && z < 21.0f ? 2.0f : 0.0f;
                return true;
            };
        }
        scr_runtime_start(kn.graph, rt, true);
        for (int i = 0; i < 40 && attack.empty(); ++i) {
            const ScrStepOut out = scr_knight_step(kn.graph, kn.reg, rt, w, 0.1f);
            modes.push_back(out.mode);
            for (const int bit : {k::kCtrlSlam, k::kCtrlStab, k::kCtrlLeftFoot, k::kCtrlRightFoot}) {
                if (has(out.controls, bit)) attack.push_back(bit);
            }
        }
    }
};

}  // namespace

TEST_CASE("the slam/stab choice: all three rays clear -> slam control requested") {
    const Fight clear(false);
    CHECK(clear.modes.front() == 3);  // the retry start mode (§13.8)
    CHECK(std::find(clear.modes.begin(), clear.modes.end(), 9) != clear.modes.end());  // Normal_Attack
    CHECK(clear.attack == std::vector<int>{k::kCtrlSlam});
    CHECK(clear.rt.attackRow == 2);
    // The slam's control resolves to its clip from the stand.
    ScrRuntime rt = clear.rt;
    CHECK(scr_control_clip(clear.kn.graph, rt, clear.attack, clear.w) == clear.kn.clip("knight_a_atk1_start"));
    CHECK(rt.controlMode == 6);  // KNIGHT_ATTACK

    const Fight walled(true);  // a 2 m wall 20 m ahead: the slam is impossible -> the stab
    CHECK(std::find(walled.modes.begin(), walled.modes.end(), 10) != walled.modes.end());
    CHECK(std::find(walled.modes.begin(), walled.modes.end(), 9) == walled.modes.end());
    CHECK(walled.attack == std::vector<int>{k::kCtrlStab});
    CHECK(walled.rt.attackRow == 6);
}

TEST_CASE("the attack runner: FIX first, then ATK, then the default row; the cooldown holds the issue") {
    const Knight& kn = knight();
    ScrRuntime rt;
    ScrWorld w = world_ahead(10.0f);
    w.bossMotion = kn.clip("knight_a_stand");
    std::set<std::string> inside = {"KNIGHT_LEG_R_ATK", "KNIGHT_WEAPON_ATK_FIX", "KNIGHT_WEAPON_ATK"};
    w.playerInJudge = [&](std::string_view name) { return inside.count(std::string(name)) > 0; };
    w.playerGroundAttr = 0x800000u;  // the battle area: mode 9's quit row stays false
    scr_runtime_start(kn.graph, rt, true);
    rt.sensed = true;
    // Arm mode 9 the way the walker does (the mode-entry hook), and sit in it.
    ScrStepOut sink;
    ScrCtx ctx{kn.graph, kn.reg, rt, w, sink};
    kn.reg.enterMode(9, ctx);
    rt.current = rt.pending = 9;
    CHECK(rt.attackTable == 0);
    CHECK(rt.locoKind == 10);

    // (a) the slam's FIX holds the player (row 2): its ATK holds too -> the slam.
    ScrStepOut out = scr_knight_step(kn.graph, kn.reg, rt, w, 0.1f);
    CHECK(rt.attackRow == 2);
    CHECK(has(out.controls, k::kCtrlSlam));

    // The slam motion ends: the cooldown (10 s) holds the issue unless arrived (+f8).
    w.bossPreviousMotion = kn.clip("knight_a_atk1_start");
    w.bossMotionSwitched = true;
    out = scr_knight_step(kn.graph, kn.reg, rt, w, 0.1f);
    CHECK(rt.cooldown == doctest::Approx(10.0f).epsilon(0.02));
    CHECK_FALSE(has(out.controls, k::kCtrlSlam));
    w.bossMotionSwitched = false;
    inside.insert("KNIGHT_WEAPON_ATK_ATTAIN");  // arrived: the slam row issues during its cooldown
    out = scr_knight_step(kn.graph, kn.reg, rt, w, 0.1f);
    CHECK(has(out.controls, k::kCtrlSlam));

    // (b) no FIX holds after the re-select period: the first row whose ATK holds (R stomp).
    inside = {"KNIGHT_LEG_R_ATK"};
    rt.cooldown = 0.0f;
    rt.timeInRow = 20.0f;
    out = scr_knight_step(kn.graph, kn.reg, rt, w, 0.1f);
    CHECK(rt.attackRow == 1);
    CHECK(has(out.controls, k::kCtrlRightFoot));

    // (c) nothing holds: the default row (the slam), nothing issued.
    inside.clear();
    rt.timeInRow = 20.0f;
    out = scr_knight_step(kn.graph, kn.reg, rt, w, 0.1f);
    CHECK(rt.attackRow == 2);
    for (const int bit : {k::kCtrlSlam, k::kCtrlStab, k::kCtrlLeftFoot, k::kCtrlRightFoot}) {
        CHECK_FALSE(has(out.controls, bit));
    }
}

TEST_CASE("locomotion: kind 0 walks to its point goal, turns in place past 30 deg, brakes on arrival") {
    const Knight& kn = knight();
    ScrRuntime rt;
    ScrWorld w = world_ahead(80.0f);  // the snapshot point is the player's ground position
    w.bossMotion = kn.clip("knight_a_stand");
    scr_runtime_start(kn.graph, rt, true);
    ScrStepOut sink;
    ScrCtx ctx{kn.graph, kn.reg, rt, w, sink};
    kn.reg.enterMode(4, ctx);  // lose_sight: kind 0, arrival radius attain[4] = 30 m
    scr_run(ctx, kn.graph.modes[4].enterActions);  // ALWAYS:F8: goal_player, route_replan, goal_player_ground
    rt.current = rt.pending = 4;
    CHECK(rt.locoKind == 0);
    CHECK(rt.hasGoal);
    CHECK(rt.goal.z == doctest::Approx(80.0f));

    ScrStepOut out = scr_knight_step(kn.graph, kn.reg, rt, w, 0.1f);
    CHECK(has(out.controls, k::kCtrlAccel));
    CHECK_FALSE(has(out.controls, k::kCtrlTurnLeft));

    w.bossYawDeg = -60.0f;  // the goal is now 60 deg to his left: a stop-turn left, steering left
    out = scr_knight_step(kn.graph, kn.reg, rt, w, 0.1f);
    CHECK(has(out.controls, k::kCtrlStopTurnLeft));
    CHECK(has(out.controls, k::kCtrlTurnLeft));
    CHECK_FALSE(has(out.controls, k::kCtrlAccel));
    CHECK(rt.steerDeg == doctest::Approx(60.0f));

    w.bossYawDeg = 0.0f;
    w.bossPosition = {0.0f, 0.0f, 55.0f};  // 25 m from the goal: inside 30 m
    out = scr_knight_step(kn.graph, kn.reg, rt, w, 0.1f);
    CHECK(rt.arrived);
    CHECK(has(out.controls, k::kCtrlBrake));
    CHECK_FALSE(has(out.controls, k::kCtrlAccel));

    // Kind 1 (route_patrol, the retry start mode): no goal, so he stands (R1).
    kn.reg.enterMode(3, ctx);
    rt.current = rt.pending = 3;
    out = scr_knight_step(kn.graph, kn.reg, rt, w, 0.1f);
    for (const int bit : {k::kCtrlAccel, k::kCtrlBrake, k::kCtrlStopTurnLeft, k::kCtrlStopTurnRight}) {
        CHECK_FALSE(has(out.controls, bit));
    }
}

TEST_CASE("knight_shake: the region picks the swing; the cooldown runs double on the head") {
    const Knight& kn = knight();
    ScrRuntime rt;
    ScrWorld w = world_ahead(0.0f);
    w.playerAttached = true;
    rt.dt = 1.0f / 30.0f;  // DAT_0325de90, the frame time the shake timers add
    ScrStepOut out;
    ScrCtx ctx{kn.graph, kn.reg, rt, w, out};
    const ScrAction& shake = kn.graph.actions[kn.graph.modes[12].updateActions].records[3];
    REQUIRE(shake.act == "knight_shake");
    const auto run = [&](int region) {
        out = ScrStepOut{};
        w.playerAttachedRegion = region;
        kn.reg.actions.at(shake.fn)(shake, ctx);
        return out.controls;
    };
    CHECK(run(0x10) == std::vector<int>{k::kCtrlRockHead});
    CHECK(run(0x50) == std::vector<int>{k::kCtrlRockBody});    // the belly sigil
    CHECK(run(0xa0) == std::vector<int>{k::kCtrlRockArmR});    // the sword
    CHECK(run(0x100) == std::vector<int>{k::kCtrlRockArmL});
    CHECK(run(0x300).empty());

    rt.bossBits |= 4;  // a refuse clip ended (knight_set4): 10 s cooldown, 5 s on the head
    float t = 0.0f;
    while ((rt.bossBits & 4) != 0 && t < 20.0f) {
        CHECK(run(0x10).empty());
        t += rt.dt;
    }
    CHECK(t == doctest::Approx(5.0f).epsilon(0.02));
}

TEST_CASE("control resolution: requested bits through the control-assign rows and the motion graph") {
    const Knight& kn = knight();
    // The hard-coded bits are the export's: each bit's base row carries the named orient.
    const auto orient_of = [&](int bit) {
        for (const ScrAssign& a : kn.graph.controlModes[1].assign) {
            if (a.layer == "base" && a.ctrl == bit) return a.orient;
        }
        return std::string();
    };
    CHECK(orient_of(k::kCtrlRockHead).find("'Head_swing'") != std::string::npos);
    CHECK(orient_of(k::kCtrlRockBody).find("'Body_swing'") != std::string::npos);
    CHECK(orient_of(k::kCtrlRockArmR).find("'Right_arm_swing'") != std::string::npos);
    CHECK(orient_of(k::kCtrlRockArmL).find("'Left_arm_swing'") != std::string::npos);
    CHECK(orient_of(k::kCtrlForcedStop).find("'Force_wait'") != std::string::npos);
    CHECK(orient_of(k::kCtrlAccel).find("'Move_(small)'") != std::string::npos);
    CHECK(orient_of(k::kCtrlStopTurnRight).find("'Right_rotation_Tachidomari'") != std::string::npos);
    CHECK(orient_of(k::kCtrlRightFoot).find("'Right_foot_crushed_tread'") != std::string::npos);
    CHECK(orient_of(k::kCtrlStab).find("'Short-range_attack_2'") != std::string::npos);

    ScrRuntime rt;
    ScrWorld w;
    w.bossMotion = kn.clip("knight_a_stand");
    w.bossControlMode = 1;
    CHECK(scr_control_clip(kn.graph, rt, k::kCtrlStopTurnRight, w) == kn.clip("knight_a_turn_r"));
    CHECK(rt.controlMode == 2);
    // The last firing row wins: accel's row precedes the shake rows, so the swing plays.
    CHECK(scr_control_clip(kn.graph, rt, std::vector<int>{k::kCtrlRockHead, k::kCtrlAccel}, w) ==
          kn.clip("knight_a_refuse_head"));
    // No request, no condition: nothing changes.
    rt.controlMode = -1;
    CHECK(scr_control_clip(kn.graph, rt, std::vector<int>{}, w) == -1);
    CHECK(rt.controlMode == -1);
    // Mortality (R2: HP <= 0, a condition-only row) through the any-motion death edge.
    w.hp = 0.0f;
    CHECK(scr_control_clip(kn.graph, rt, std::vector<int>{}, w) == kn.clip("knight_a_knight_death"));
    CHECK(rt.controlMode == 10);
}

TEST_CASE("a hurt (additive) row requests nothing") {
    const Knight& kn = knight();
    ScrRuntime rt;
    ScrWorld w;
    w.bossMotion = kn.clip("knight_a_stand");
    w.bossControlMode = 1;
    w.hit = true;
    w.hitRegion = 0x10;  // the head: COMMON_BOSS_CONTROL[10] 'Head_damage' (additive)
    w.hitKind = 0;
    CHECK(scr_control_clip(kn.graph, rt, std::vector<int>{}, w) == -1);
    CHECK(rt.controlMode == -1);
    // The additive arm raises and the discovery (bits 13, 14, 39) resolve to nothing too.
    for (const int bit : {13, 14, 39}) CHECK(scr_control_clip(kn.graph, rt, bit, w) == -1);

    // And the graph's arm-raise if_ctrl, run while he is parented, plays no clip.
    ScrStepOut out;
    w.playerAttached = true;
    ScrCtx ctx{kn.graph, kn.reg, rt, w, out};
    const ScrActionList& f33 = kn.graph.actions[kn.graph.modes[12].updateActions];
    for (const ScrAction& a : f33.records) {
        if (a.act == "if_ctrl") kn.reg.actions.at(a.fn)(a, ctx);
    }
    CHECK(has(out.controls, 13));
    CHECK(scr_control_clip(kn.graph, rt, out.controls, w) == -1);
}

TEST_CASE("the gait through control resolution: stand -> turn (10 deg off) -> walk_start (within 3 deg)") {
    const Knight& kn = knight();
    ScrRuntime rt;
    ScrWorld w = world_ahead(80.0f);
    scr_runtime_start(kn.graph, rt, true);
    ScrStepOut sink;
    ScrCtx ctx{kn.graph, kn.reg, rt, w, sink};
    kn.reg.enterMode(4, ctx);  // kind 0 toward the player's snapshot, 80 m ahead
    scr_run(ctx, kn.graph.modes[4].enterActions);
    rt.current = rt.pending = 4;

    // 10 deg to his right (yaw +10): inside ±30 -> accel, past 3 -> Turn_right. In
    // KNIGHT_DEFAULT the Turn_right row follows accel's, so it wins: the stop-turn in place.
    w.bossYawDeg = 10.0f;
    w.bossMotion = kn.clip("knight_a_stand");
    w.bossControlMode = 1;
    ScrStepOut out = scr_knight_step(kn.graph, kn.reg, rt, w, 0.1f);
    CHECK(has(out.controls, k::kCtrlAccel));
    CHECK(has(out.controls, k::kCtrlTurnRight));
    CHECK(rt.steerDeg == doctest::Approx(-10.0f));
    CHECK(scr_control_clip(kn.graph, rt, out.controls, w) == kn.clip("knight_a_turn_r"));
    CHECK(rt.controlMode == 2);  // KNIGHT_TURN
    REQUIRE(rt.controlEdge >= 0);
    CHECK(kn.graph.motionGraph[rt.controlEdge].p30 == doctest::Approx(0.0f));
    CHECK(kn.graph.motionGraph[rt.controlEdge].p34 == doctest::Approx(10.0f));  // the blend, frames

    // Still 10 deg off while turn_r plays: the self-edge keeps the playing clip (-1, no restart).
    w.bossMotion = kn.clip("knight_a_turn_r");
    w.bossControlMode = 2;
    rt.controlEdge = -1;
    out = scr_knight_step(kn.graph, kn.reg, rt, w, 0.1f);
    CHECK(scr_control_clip(kn.graph, rt, out.controls, w) == -1);
    CHECK(rt.controlMode == 2);
    CHECK(rt.controlEdge == -1);

    // From turn_l with the goal within 3 deg: accel alone -> 'Move_(small)' -> walk_start, KNIGHT_WALK.
    w.bossYawDeg = 1.0f;
    w.bossMotion = kn.clip("knight_a_turn_l");
    out = scr_knight_step(kn.graph, kn.reg, rt, w, 0.1f);
    CHECK(has(out.controls, k::kCtrlAccel));
    CHECK_FALSE(has(out.controls, k::kCtrlTurnRight));
    CHECK_FALSE(has(out.controls, k::kCtrlTurnLeft));
    CHECK(scr_control_clip(kn.graph, rt, out.controls, w) == kn.clip("knight_a_walk_start"));
    CHECK(rt.controlMode == 3);
    REQUIRE(rt.controlEdge >= 0);
    CHECK(kn.graph.motionGraph[rt.controlEdge].p30 == doctest::Approx(-1.0f));  // out of a turn: at its end
}
