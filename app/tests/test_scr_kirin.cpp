// Phaedra's registry, check and additive requests (S13 Task 14), on her real exports generated at
// test time into the build directory: tools/re/scr/graph_export.py KIRIN (out/re/eboot.elf) and
// tools/re/path/route_export.py on $SOTC_GAME_ROOT/cp3shadow/path/pathdata/kirin.path; her sidecar
// comes from SOTC_LIB (the branch's view). A case fails, never skips, without them; nothing
// derived is in git. The scenarios drive scr_boss_step with scripted ScrWorlds in the game's frame
// order (scr_kirin.h).
#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "additive.h"
#include "knight_fixture.h"
#include "objanim_events.h"
#include "route.h"
#include "scr_graph.h"
#include "scr_kirin.h"
#include "scr_library.h"
#include "scr_predicates.h"

namespace {

namespace fs = std::filesystem;
using knight_fixture::export_clips;
using knight_fixture::kRepoRoot;
using knight_fixture::read_file;

std::string generate(const std::string& name, const std::string& args) {
    const fs::path out = fs::path(SOTC_TEST_BUILD_DIR) / "fixtures" / name;
    fs::create_directories(out.parent_path());
    const std::string cmd = args + " '" + out.string() + "'";
    if (std::system(cmd.c_str()) != 0) throw std::runtime_error("fixture export failed: " + cmd);
    return read_file(out);
}

const std::string& kirin_json() {
    static const std::string json =
        generate("KIRIN.t14.graph.json",
                 "python3 -I '" + (kRepoRoot / "tools/re/scr/graph_export.py").string() + "' export KIRIN");
    return json;
}

const std::string& route_json() {
    static const std::string json = [] {
        const char* game = std::getenv("SOTC_GAME_ROOT");
        if (!game) throw std::runtime_error("Set SOTC_GAME_ROOT to the extracted game tree.");
        return generate("kirin.t14.route.json",
                        "python3 -I '" + (kRepoRoot / "tools/re/path/route_export.py").string() + "' '" +
                            std::string(game) + "/cp3shadow/path/pathdata/kirin.path'");
    }();
    return json;
}

const EventsFile& sidecar() {
    static const EventsFile file = [] {
        const char* lib = std::getenv("SOTC_LIB");
        if (lib == nullptr || lib[0] == '\0') throw std::runtime_error("Set SOTC_LIB to the library view.");
        EventsFile f;
        std::string error;
        const std::string path =
            std::string(lib) + "/models/shadow/characters/colossus_04_kirin/_cmn/c04_kirin.events.json";
        if (!events_load(path, f, error)) throw std::runtime_error(error);
        return f;
    }();
    return file;
}

std::vector<std::string> judge_names() {
    std::vector<std::string> names;
    for (const EventsVolume& v : sidecar().judgeDefaults) names.push_back(v.name);
    return names;
}

// Her graph loaded as the glue loads it (scr_kirin.h): load, the two binds, the check.
struct Kirin {
    ScrRegistry reg = scr_registry_kirin();
    ScrGraph graph;
    RouteGraph routes;
    Kirin() {
        std::string error;
        REQUIRE_MESSAGE(route_load(route_json(), routes, error), error);
        const bool ok = scr_graph_load(kirin_json(), export_clips(kirin_json()), reg, graph, error);
        INFO(error);
        REQUIRE(ok);
        REQUIRE_MESSAGE(scr_attack_attain(graph, sidecar().judgeDefaults, error), error);
        REQUIRE_MESSAGE(scr_move_back_bind(graph, sidecar().judgeDefaults, error), error);
        REQUIRE_MESSAGE(scr_kirin_check(graph, &routes, judge_names(), error), error);
    }
    int cond(const std::string& key) const {
        for (size_t i = 0; i < graph.conditions.size(); ++i) {
            if (graph.conditions[i].key == key) return static_cast<int>(i);
        }
        FAIL("no condition " << key);
        return -1;
    }
    const ScrClause& clause(const std::string& key, uint32_t fn) const {
        for (const ScrClause& c : graph.conditions[static_cast<size_t>(cond(key))].clauses) {
            if (c.fn == fn) return c;
        }
        throw std::runtime_error(key + ": no clause of that fn");
    }
    const ScrAction& action(const std::string& key, uint32_t fn) const {
        for (const ScrActionList& l : graph.actions) {
            if (l.key != key) continue;
            for (const ScrAction& a : l.records) {
                if (a.fn == fn) return a;
            }
        }
        throw std::runtime_error(key + ": no record of that fn");
    }
    int clip(const char* name) const {
        const int i = scr_clip(graph, name);
        REQUIRE(i >= 0);
        return i;
    }
};

const Kirin& kirin() {
    static const Kirin k;
    return k;
}

bool has(const std::vector<int>& v, int bit) { return std::find(v.begin(), v.end(), bit) != v.end(); }

// An orient whose quoted name is `name` ("DEFAULT:motion-orient-def[893] 'Neck_down_1'").
bool has(const std::vector<std::string>& orients, const char* name) {
    const std::string quoted = std::string("'") + name + "'";
    return std::any_of(orients.begin(), orients.end(), [&](const std::string& o) {
        return o.size() >= quoted.size() && o.compare(o.size() - quoted.size(), quoted.size(), quoted) == 0;
    });
}

const Vec3 kSpawn{1122.0f, 53.76f, 147.0f};  // data: her manifest entity (Task 12, R-Y)

// One run: her runtime, a scripted world (the boss at her spawn facing +z, KIRIN_DEFAULT) and an
// additive pool, stepped in the game's frame order.
struct Run {
    const Kirin& k;
    ScrRuntime rt;
    ScrWorld w;
    AddLayers layers;
    std::vector<AddEdge> edges;
    std::vector<float> seconds;
    std::vector<std::string> orients;
    ScrStepOut out;

    explicit Run(const Kirin& kk) : k(kk), edges(scr_additive_edges(kk.graph)) {
        seconds.assign(k.graph.clipNames.size(), 1.0f);  // authored: every clip 1 s (the logic, not the timing)
        scr_runtime_start(k.graph, rt, true);  // the retry start: 2 -> 3, the cut-scene skipped (S12 ruling 8)
        w.routes = &k.routes;
        w.bossPosition = kSpawn;
        w.bossControlMode = 0;
        w.bossLayerPlaying = [this](int clip) { return additive_playing(layers, clip); };
        player(0.0f, 50.0f);
    }
    Run(const Run&) = delete;

    // The player `x` m along +x and `z` m along +z (her forward) from her root, on her ground.
    void player(float x, float z) {
        w.playerPosition = {kSpawn.x + x, kSpawn.y, kSpawn.z + z};
        w.playerGroundPosition = w.playerPosition;
    }
    void frame(float dt = 1.0f / 30.0f) {
        scr_world_relate(w);
        out = scr_boss_step(k.graph, k.reg, rt, w, dt);
        orients = scr_control_additive(k.graph, rt, out.controls, w);
        additive_step(layers, seconds, dt);
        for (const std::string& o : orients) additive_request(layers, edges, o);
        w.hit = false;  // one-frame events
        w.playerMotionStarted = false;
    }
    void run(float s) {
        for (int i = 0, n = static_cast<int>(std::lround(s * 30.0f)); i < n; ++i) frame();
    }
    // Steps until mode `m` (at most `limit` frames); the mode reached.
    int until(int m, int limit = 60) {
        for (int i = 0; i < limit && rt.current != m; ++i) frame();
        return rt.current;
    }
    // A layer plays `clip` over reference `ref` (-1 none).
    bool layer(int clip, int ref) const {
        for (const AddLayer& l : layers.slot) {
            if (l.state != AddLayer::Free && l.clip == clip && l.ref == ref) return true;
        }
        return false;
    }
};

// Sensed in front, then in mode 6 (Normal attack).
void to_attack(Run& r) {
    REQUIRE(r.until(3) == 3);
    r.player(0.0f, 50.0f);
    REQUIRE(r.until(6) == 6);
}

}  // namespace

TEST_CASE("KIRIN's export loads strictly with scr_registry_kirin(); every fn and kind is registered") {
    const Kirin& k = kirin();
    CHECK(k.graph.modes.size() == 15);
    CHECK(k.graph.conditions.size() == 84);
    std::set<uint32_t> preds, acts;
    for (const ScrCondition& c : k.graph.conditions) {
        for (const ScrClause& cl : c.clauses) {
            if (cl.fn != kScrSubFn) preds.insert(cl.fn);
        }
    }
    for (const ScrActionList& l : k.graph.actions) {
        for (const ScrAction& a : l.records) acts.insert(a.fn);
    }
    for (const uint32_t fn : preds) CHECK_MESSAGE(k.reg.predicates.count(fn) == 1, std::hex << fn);
    for (const uint32_t fn : acts) CHECK_MESSAGE(k.reg.actions.count(fn) == 1, std::hex << fn);
    for (const ScrMode& m : k.graph.modes) CHECK(k.reg.kinds.count(m.locomotion.kind) == 1);
    CHECK(preds.size() == 30);  // data: the export's distinct predicate fns (sub aside) and action kinds
    CHECK(acts.size() == 23);
    CHECK(k.reg.enterMode != nullptr);
    CHECK(k.reg.locomotion != nullptr);

    // Her locals: four implemented, four labelled gaps (the rockfall spawners, the prox latch).
    for (const uint32_t fn : {0x6fdf70u, 0x6fdfd0u, 0x6fdf20u, 0x6fdf90u, 0x6fdfb0u}) CHECK(k.reg.gaps.count(fn) == 0);
    for (const uint32_t fn : {0x6fccf0u, 0x6fd5a0u, 0x6fc8a0u, 0x6fc8c0u}) CHECK(k.reg.gaps.count(fn) == 1);
    const std::string dump = scr_registry_dump(k.reg);
    CHECK(dump.find("6fdfd0 implemented\n") != std::string::npos);
    CHECK(dump.find("6fccf0 gap\n") != std::string::npos);
    CHECK(dump.find("833680 implemented\n") != std::string::npos);  // move_done (the routes)
    for (const char* kind : {"kind 2\n", "kind 6\n", "kind 8\n", "kind 9\n", "kind 10\n"}) {
        CHECK(dump.find(kind) != std::string::npos);
    }

    // The terrain-class words are 64-bit (0x7f << 48): the loader must not truncate a q-slot to 32 bits.
    const ScrClause& slope = k.clause("KIRIN:C95", 0x832bb0);
    CHECK(scr_arg(slope.args, "q28")->num == static_cast<double>(0x7f000000000000ull));
    CHECK(scr_arg(slope.args, "q30")->num == static_cast<double>(0x3000000000000ull));

    // Ruling R-AB: dprelate names route 1 (terrain 2), which kirin.path lacks; the check passes.
    CHECK(std::none_of(k.routes.routes.begin(), k.routes.routes.end(), [](const auto& r) { return r.id == 1; }));
    std::string error;
    CHECK_MESSAGE(scr_kirin_check(k.graph, &k.routes, judge_names(), error), error);
}

TEST_CASE("scr_kirin_check refuses: no routes, an unknown judge, an unbound ATTAIN or MOVE_BACK, a bad slot") {
    const Kirin& k = kirin();
    std::string error;
    CHECK_FALSE(scr_kirin_check(k.graph, nullptr, judge_names(), error));
    CHECK(error.find("route") != std::string::npos);

    std::vector<std::string> judges = judge_names();
    judges.erase(std::remove(judges.begin(), judges.end(), "KIRIN_BACK"), judges.end());
    CHECK_FALSE(scr_kirin_check(k.graph, &k.routes, judges, error));
    CHECK(error.find("KIRIN_BACK") != std::string::npos);

    ScrGraph g = k.graph;  // scr_attack_attain forgotten: a row's cylinder is unbound
    g.attackdata.begin()->second[0].attainR = 0.0f;
    CHECK_FALSE(scr_kirin_check(g, &k.routes, judge_names(), error));
    CHECK(error.find("ATTAIN") != std::string::npos);

    g = k.graph;  // scr_move_back_bind forgotten
    g.modes[6].locomotion.backRadius = 0.0f;
    CHECK_FALSE(scr_kirin_check(g, &k.routes, judge_names(), error));
    CHECK(error.find("MOVE_BACK") != std::string::npos);

    g = k.graph;  // time_in_mode_cycle's period must be > 0 (the binary divides by it)
    for (ScrClause& c : g.conditions[static_cast<size_t>(k.cond("KIRIN:C110"))].clauses) {
        for (ScrArg& a : c.args) {
            if (a.slot == "i10") a.num = 0.0;
        }
    }
    CHECK_FALSE(scr_kirin_check(g, &k.routes, judge_names(), error));
    CHECK(error.find("i10") != std::string::npos);

    g = k.graph;  // an additive row's control-def missing from the export
    g.controlDefs.erase("KIRIN_CONTROL:control-def[45] 'GIRAFFE_SWING_CONDITIONS_1'");
    CHECK_FALSE(scr_kirin_check(g, &k.routes, judge_names(), error));
    CHECK(error.find("GIRAFFE_SWING_CONDITIONS_1") != std::string::npos);
}

TEST_CASE("her locals: the 30 s / 5 s mode cycle and the rockfall timer's state; the gaps do nothing") {
    const Kirin& k = kirin();
    const ScrClause& cycle = k.clause("KIRIN:C110", 0x6fdfd0);  // {i10 30, i14 5}
    const auto cyc = k.reg.predicates.at(0x6fdfd0);
    ScrRuntime rt;
    const ScrWorld w;
    for (const auto& [t, want] : {std::pair{0.0f, true}, {4.99f, true}, {5.0f, false}, {29.9f, false}, {30.0f, true},
                                  {64.5f, true}, {65.0f, false}}) {
        rt.timer1 = t;
        CHECK_MESSAGE(cyc(cycle, w, rt) == want, t);
    }

    const ScrClause& running = k.clause("KIRIN:C209", 0x6fdf70);
    ScrStepOut out;
    ScrCtx ctx{k.graph, k.reg, rt, w, out};
    auto act = [&](const char* list, uint32_t fn) { k.reg.actions.at(fn)(k.action(list, fn), ctx); };
    CHECK_FALSE(k.reg.predicates.at(0x6fdf70)(running, w, rt));
    rt.bossTimer[0] = 1.0f;
    CHECK(k.reg.predicates.at(0x6fdf70)(running, w, rt));
    rt.dt = 0.4f;
    act("KIRIN:F82", 0x6fdf20);  // tick
    CHECK(rt.bossTimer[0] == doctest::Approx(0.6f));
    rt.dt = 1.0f;
    act("KIRIN:F82", 0x6fdf20);  // floored at 0
    CHECK(rt.bossTimer[0] == 0.0f);
    CHECK_FALSE(k.reg.predicates.at(0x6fdf70)(running, w, rt));
    act("KIRIN:F75", 0x6fdf90);  // arm
    CHECK((rt.bossBits & 1u) != 0);
    act("KIRIN:F77", 0x6fdfb0);  // disarm
    CHECK((rt.bossBits & 1u) == 0);

    const ScrRuntime before = rt;
    for (const auto& [list, fn] : {std::pair{"KIRIN:F71", 0x6fccf0u}, {"KIRIN:F73", 0x6fd5a0u},
                                   {"KIRIN:F61", 0x6fc8a0u}, {"KIRIN:F63", 0x6fc8c0u}}) {
        act(list, fn);
    }
    CHECK(out.controls.empty());
    CHECK(rt.bossBits == before.bossBits);
    CHECK(rt.bossTimer[0] == before.bossTimer[0]);
}

TEST_CASE("patrol: mode 3 holds while she cannot sense the player, then sensed -> mode 6") {
    Run r(kirin());
    r.player(0.0f, -50.0f);  // behind her, beyond the 20 m near radius
    REQUIRE(r.until(3) == 3);
    CHECK(r.rt.locoKind == 1);
    CHECK(r.rt.route.kind == 1);  // enterMode ran scr_route_enter too
    r.run(1.0f);
    CHECK(r.rt.current == 3);
    r.player(0.0f, 50.0f);  // in front: seen, sensed after 0.5 s (perception [3])
    r.run(0.4f);
    CHECK(r.rt.current == 3);
    CHECK(r.until(6, 10) == 6);
    CHECK(r.rt.locoKind == 10);
}

TEST_CASE("the whistle: in a tunnel (class 6) within 70 m -> mode 10, move_done -> 11 or 12 by her ground's owner") {
    for (const auto& [owner, peep] : {std::pair{0x3c00000ull, 11}, {0x400000ull, 12}}) {
        Run r(kirin());
        r.player(0.0f, -50.0f);
        REQUIRE(r.until(3) == 3);
        r.w.bossGroundAttr = owner;  // R-O: (attr & 0x3c00000) == 0x400000; the road/hill 0x3c00000 is not
        r.w.playerGroundAttr = 6ull << 48;
        r.w.playerMotion = 604;  // a finger whistle (ALWAYS:C120)
        r.w.playerMotionStarted = true;
        r.frame();
        REQUIRE(r.rt.current == 10);
        CHECK(r.rt.route.route == 3);  // route_by_dprelate: class 6 -> route 3
        r.frame();
        REQUIRE(r.rt.route.hasGoal);
        CHECK(r.rt.current == 10);
        r.w.bossPosition = r.rt.route.goal;  // she arrives at the GP
        r.frame();
        CHECK_MESSAGE(r.rt.current == peep, owner);
    }
}

TEST_CASE("the hill lure: the player on class 3 -> 7 (AP) -> 8 (GP) -> 9 (PP); a hit there -> 13 (threat)") {
    Run r(kirin());
    to_attack(r);
    r.w.playerGroundAttr = 3ull << 48;
    r.frame();
    REQUIRE(r.rt.current == 7);
    CHECK(r.rt.route.route == 2);  // class 3 -> route 2
    r.frame();
    REQUIRE(r.rt.route.hasGoal);
    CHECK(r.rt.route.kind == 2);
    r.w.bossPosition = r.rt.route.goal;
    r.frame();
    REQUIRE(r.rt.current == 8);
    r.frame();
    REQUIRE(r.rt.route.hasGoal);
    CHECK(r.rt.route.kind == 6);
    r.w.bossPosition = r.rt.route.goal;
    r.frame();
    CHECK(r.rt.current == 9);
    r.frame();
    CHECK(r.rt.route.kind == 9);
    CHECK(r.rt.route.pp >= 0);

    // Spec §9 (graph findings §3): a hit on the hill route -> 13, whose entry runs
    // route_link_flag_clear(1) (KIRIN:F67) and reopens the links mode 3's entry closed.
    const auto closed_tag1 = [&r] {
        int n = 0;
        for (size_t i = 0; i < r.k.routes.links.size(); ++i) {
            const RouteGraph::Link& l = r.k.routes.links[i];
            if ((l.tags[0] == 1 || l.tags[1] == 1) && (r.rt.route.linkFlags[i] & 4) != 0) ++n;
        }
        return n;
    };
    REQUIRE(r.rt.route.linkFlags.size() == r.k.routes.links.size());
    CHECK(closed_tag1() > 0);  // mode 3's entry closed them (route_link_flag_set(1))
    const ScrMode& threat = r.k.graph.modes[13];
    CHECK(threat.name == "Kirin_slope_intimidation_attack_mode");
    const ScrAction& clear = r.k.action("KIRIN:F67", 0x82e2c0);
    CHECK(scr_arg(clear.args, "i08")->num == 1.0);
    CHECK(r.k.graph.actions[static_cast<size_t>(threat.enterActions)].key == "KIRIN:F67");
    r.w.hit = true;
    r.frame();
    REQUIRE(r.rt.current == 13);
    CHECK(r.rt.locoKind == 10);
    CHECK(closed_tag1() == 0);
}

TEST_CASE("neck down: a hit on 0x20 requests Neck_down_1 (additive), a second Neck_down_2") {
    const Kirin& k = kirin();
    const int ref = k.clip("kirin_a_add_neck_refuse_start");
    Run r(k);
    to_attack(r);
    r.w.hit = true;
    r.w.hitRegion = 0x20;  // the neck trigger, a sword hit
    r.w.hitKind = 0;
    r.frame();
    CHECK(has(r.out.controls, 21));  // Giraffe_neck_lowered_by_one_first_addition
    CHECK(has(r.orients, "Neck_down_1_First_time"));
    CHECK(r.layer(k.clip("kirin_a_add_neck_down_1_start"), ref));
    r.frame();
    CHECK(r.rt.flags.test(587));     // KIRIN:C132 saw the layer play
    CHECK(has(r.out.controls, 15));  // the stage-1 hold
    CHECK(has(r.orients, "Neck_down_1"));
    r.run(0.5f);
    CHECK(has(r.orients, "Neck_down_1"));  // level: every frame while held

    r.w.hit = true;
    r.w.hitRegion = 0x20;
    r.w.hitKind = 0;
    r.frame();
    CHECK(has(r.out.controls, 20));  // Giraffe_neck_down_2_first_addition
    CHECK(has(r.orients, "Neck_down_2_first"));
    CHECK(r.layer(k.clip("kirin_a_add_neck_down_2_start"), ref));
    r.frame();
    CHECK(r.rt.flags.test(588));
    CHECK(has(r.out.controls, 16));
    CHECK(has(r.orients, "Neck_down_2"));
    CHECK_FALSE(has(r.orients, "Neck_down_1"));
    r.run(1.0f);  // the start clip ends: the from-row (p30 -1) shifts to the stage-2 loop
    CHECK(r.layer(k.clip("kirin_a_add_neck_down_2_loop"), ref));
}

TEST_CASE("the head shake: attached on 0x10 -> mode 14, Swing fires after the 5 s brake and reaches refuse_loop") {
    const Kirin& k = kirin();
    const int start = k.clip("kirin_a_add_neck_refuse_start"), loop = k.clip("kirin_a_add_neck_refuse_loop");
    Run r(k);
    to_attack(r);
    r.w.playerAttached = true;
    r.w.playerAttachedRegion = 0x10;
    REQUIRE(r.until(14, 5) == 14);
    r.run(4.8f);  // KIRIN:C110 brakes the first 5 s of every 30 s; the brake bars the swing
    CHECK(has(r.out.controls, 57));
    CHECK_FALSE(has(r.orients, "Swing"));
    r.run(0.3f);
    CHECK_FALSE(has(r.out.controls, 57));
    CHECK(has(r.orients, "Swing"));
    // The Always row started the neck dummy (no reference); the dummy's from-row shifts into the swing start
    // (reference: itself), whose own from-row waits for its end (p30 -1) and shifts into the loop.
    CHECK(r.layer(start, start));
    r.run(1.2f);  // the start (1 s, authored) has ended: its from-row shifted into the loop
    CHECK(has(r.orients, "Swing"));
    CHECK(r.layer(loop, start));
    r.frame();
    CHECK(has(r.orients, "Head_swing"));  // GIRAFFE_HEAD_SHAKING_CONDITIONS: the loop plays

    // Without the from motion-def's identity the dummy's Continue row would restart the start forever.
    Run blind(k);
    for (AddEdge& e : blind.edges) e.fromRef = -2;
    to_attack(blind);
    blind.w.playerAttached = true;
    blind.w.playerAttachedRegion = 0x10;
    REQUIRE(blind.until(14, 5) == 14);
    blind.run(7.0f);
    CHECK_FALSE(blind.layer(loop, start));
}

TEST_CASE("death: HP 0 -> control-mode 11 (BOSS_MORTALITY) -> mode 5") {
    Run r(kirin());
    to_attack(r);
    r.w.hp = 0.0f;
    r.frame();
    scr_control_clip(r.k.graph, r.rt, r.out.controls, r.w);
    CHECK(r.rt.controlMode == 11);
    r.w.bossControlMode = r.rt.controlMode;
    r.frame();
    CHECK(r.rt.current == 5);
}

TEST_CASE("scr_additive_edges: KIRIN_ADD's 74 edges with the elf's refs and the from-rows' motion-def refs") {
    const Kirin& k = kirin();
    const std::vector<AddEdge> edges = scr_additive_edges(k.graph);
    CHECK(edges.size() == 74);
    const int start = k.clip("kirin_a_add_neck_refuse_start");
    for (const AddEdge& e : edges) {
        if (e.to == k.clip("kirin_a_add_neck_down_2_loop")) CHECK(e.ref == start);  // R-S
    }
    std::vector<int> swing;
    for (const AddEdge& e : edges) {
        if (e.from == start && e.orient.find("'Swing'") != std::string::npos) swing.push_back(e.fromRef);
    }
    CHECK(swing == std::vector<int>{-1, start});  // the dummy's rows, then the swing start's
}

TEST_CASE("MOVE_BACK: her KIRIN_BACK sector (20 m, 80°) from the sidecar, not Gaius's 75 m") {
    const Kirin& k = kirin();
    CHECK(k.graph.modes[6].locomotion.backRadius == doctest::Approx(20.0f));
    CHECK(k.graph.modes[6].locomotion.backHalfDeg == doctest::Approx(40.0f).epsilon(1e-3));
    // A volume not yawed 180° (or not on the skelton) is not read as a MOVE_BACK sector.
    for (const bool yawed : {false, true}) {
        std::vector<EventsVolume> judges = sidecar().judgeDefaults;
        for (EventsVolume& v : judges) {
            if (v.name != "KIRIN_BACK") continue;
            if (yawed) v.parent.kind = EventsParent::Bone;
            else v.keys[0].quat = Quat{0.0f, 0.0f, 0.0f, 1.0f};
        }
        ScrGraph g = k.graph;
        std::string error;
        CHECK_FALSE(scr_move_back_bind(g, judges, error));
        CHECK(error.find("KIRIN_BACK") != std::string::npos);
    }
    Run r(k);
    to_attack(r);
    r.player(0.0f, -5.0f);  // the default row's attain point lands ~15 m behind her: inside 20 m
    r.frame();
    CHECK(has(r.out.controls, kScrCtrlBackward));
    r.player(0.0f, -30.0f);  // ~40 m behind: outside her 20 m (inside Gaius's 75 m)
    r.frame();
    CHECK_FALSE(has(r.out.controls, kScrCtrlBackward));
}
