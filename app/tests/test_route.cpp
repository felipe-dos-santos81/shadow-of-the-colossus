// S13 Task 8: the route follower (kinds 2-9), the nav-graph walk and the route fns, on
// Phaedra's real exports generated at test time into the build directory:
// tools/re/path/route_export.py on $SOTC_GAME_ROOT/cp3shadow/path/pathdata/kirin.path and
// tools/re/scr/graph_export.py KIRIN (out/re/eboot.elf). A case fails, never skips, without
// them; nothing derived is in git.
#include <doctest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "json_reader.h"
#include "knight_fixture.h"
#include "route.h"
#include "scr_graph.h"

namespace {

namespace fs = std::filesystem;
using knight_fixture::kRepoRoot;
using knight_fixture::read_file;

std::string generate(const std::string& name, const std::string& args) {
    const fs::path out = fs::path(SOTC_TEST_BUILD_DIR) / "fixtures" / name;
    fs::create_directories(out.parent_path());
    const std::string cmd = args + " '" + out.string() + "'";
    if (std::system(cmd.c_str()) != 0) throw std::runtime_error("fixture export failed: " + cmd);
    return read_file(out);
}

const std::string& kirin_route_json() {
    static const std::string json = [] {
        const char* game = std::getenv("SOTC_GAME_ROOT");
        if (!game) throw std::runtime_error("Set SOTC_GAME_ROOT to the extracted game tree.");
        return generate("kirin.route.json", "python3 -I '" + (kRepoRoot / "tools/re/path/route_export.py").string() +
                                                "' '" + std::string(game) + "/cp3shadow/path/pathdata/kirin.path'");
    }();
    return json;
}

const std::string& kirin_graph_json() {
    static const std::string json = generate(
        "KIRIN.graph.json", "python3 -I '" + (kRepoRoot / "tools/re/scr/graph_export.py").string() + "' export KIRIN");
    return json;
}

RouteGraph kirin() {
    RouteGraph g;
    std::string err;
    if (!route_load(kirin_route_json(), g, err)) throw std::runtime_error(err);
    return g;
}

float dist(Vec3 a, Vec3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }
bool same(Vec3 a, Vec3 b) { return dist(a, b) < 1e-4f; }
Vec3 lerp(Vec3 a, Vec3 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

const std::function<bool(Vec3, Vec3, Vec3&)> kNoHit = [](Vec3, Vec3, Vec3&) { return false; };
const std::function<bool(Vec3, Vec3)> kClear = [](Vec3, Vec3) { return true; };

struct Tick {
    Vec3 goal{0, 0, 0}, face{0, 0, 0};
    bool hasGoal = false;
};
Tick tick(RouteState& st, const RouteGraph& g, Vec3 boss, Vec3 player, bool arrived,
          const std::function<bool(Vec3, Vec3, Vec3&)>& sweep = kNoHit) {
    Tick t;
    route_tick(st, g, boss, player, arrived, sweep, t.goal, t.hasGoal, t.face);
    return t;
}

// The link joining nodes a and b, -1 when none.
int link_between(const RouteGraph& g, int a, int b) {
    for (size_t i = 0; i < g.links.size(); ++i) {
        const auto& l = g.links[i];
        if ((l.a == a && l.b == b) || (l.a == b && l.b == a)) return static_cast<int>(i);
    }
    return -1;
}
int closed_links(const std::vector<int>& flags) {
    int n = 0;
    for (const int f : flags) n += (f & 4) != 0;
    return n;
}

// The walk's waypoints as node indices (each is a node position exactly).
std::vector<int> as_nodes(const RouteGraph& g, const std::vector<Vec3>& path) {
    std::vector<int> out;
    for (const Vec3& p : path) {
        int found = -1;
        for (size_t n = 0; n < g.nodes.size(); ++n) {
            if (same(g.nodes[n].pos, p)) found = static_cast<int>(n);
        }
        REQUIRE(found >= 0);
        out.push_back(found);
    }
    return out;
}

}  // namespace

TEST_CASE("route: the loader is strict and reads the kirin export") {
    RouteGraph g;
    std::string err;
    CHECK_FALSE(route_load("{\"format\":\"nope\"}", g, err));
    CHECK(err.find("sotc-route/0") != std::string::npos);
    CHECK_FALSE(route_load("not json", g, err));
    CHECK_FALSE(route_load(R"({"format":"sotc-route/0","nodes":[],"links":[{"i":0,"a":0,"b":1,"flags":1,"tags":[-1,-1],"length":1}],"routes":[],"gps":[],"points":[]})",
                           g, err));
    CHECK(err.find("link 0") != std::string::npos);
    g = kirin();
    CHECK(g.nodes.size() == 52);
    CHECK(g.links.size() == 62);
    CHECK(g.routes.size() == 5);
    CHECK(g.gps.size() == 11);
    CHECK(g.points.size() == 17);
}

TEST_CASE("route: kind 9 on route 2 ping-pongs between the GP's two PP points") {
    const RouteGraph g = kirin();
    RouteState st;
    route_enter(st, 9, true, true);  // mode 9: ref player, tracking (data)
    st.route = 2;
    const Vec3 player = g.gps[9].pos;
    const Vec3 boss{940.0f, 53.0f, 60.0f};
    Tick t = tick(st, g, boss, player, false);
    CHECK(st.gp == 9);
    REQUIRE((st.pp == 0 || st.pp == 2));
    CHECK(t.hasGoal);
    CHECK(same(t.goal, g.points[st.pp].pos));
    CHECK_FALSE(st.donePP);
    int prev = st.pp;
    for (int i = 0; i < 5; ++i) {
        t = tick(st, g, boss, player, true);
        CHECK(st.donePP);
        CHECK(st.pp != prev);
        CHECK((st.pp == 0 || st.pp == 2));
        CHECK(same(t.goal, g.points[st.pp].pos));
        prev = st.pp;
        t = tick(st, g, boss, player, false);  // walking: the level drops, the PP holds
        CHECK_FALSE(st.donePP);
        CHECK(st.pp == prev);
    }
    CHECK(st.ppCount == 5);
}

TEST_CASE("route: kind 8 with refPlayer=false picks the GP nearest the boss and sweeps AP->GP") {
    const RouteGraph g = kirin();
    const Vec3 boss{1122.0f, 59.0f, 147.0f};
    const Vec3 player = g.gps[1].pos;  // on the outer GP: the player must not decide
    RouteState st;
    route_enter(st, 8, false, true);  // mode 10 (data)
    st.route = 4;
    Tick t = tick(st, g, boss, player, false);
    CHECK(st.gp == 0);
    CHECK(st.ap == 9);  // GP 0's only AP point
    CHECK(t.hasGoal);
    CHECK(same(t.goal, g.gps[0].pos));  // no hit: the GP
    const Vec3 ap = g.points[9].pos, gp = g.gps[0].pos;
    const float len = std::hypot(gp.x - ap.x, gp.z - ap.z);
    CHECK(t.face.x - t.goal.x == doctest::Approx((gp.x - ap.x) / len).epsilon(1e-4));
    CHECK(t.face.z - t.goal.z == doctest::Approx((gp.z - ap.z) / len).epsilon(1e-4));
    CHECK_FALSE(st.doneGP);
    tick(st, g, boss, player, true);
    CHECK(st.doneGP);

    // A wall halfway: the goal stops 0.1 m short of the hit, back toward the AP.
    const Vec3 mid = lerp(ap, gp, 0.5f);
    RouteState hit;
    route_enter(hit, 8, false, true);
    hit.route = 4;
    t = tick(hit, g, boss, player, false, [&](Vec3, Vec3, Vec3& h) {
        h = mid;
        return true;
    });
    CHECK(dist(t.goal, mid) == doctest::Approx(0.1f).epsilon(1e-3));
    CHECK(dist(t.goal, ap) < dist(mid, ap));
}

TEST_CASE("route: kind 2 walks to the AP, arrives facing the GP, doneAP a level") {
    const RouteGraph g = kirin();
    const Vec3 player = g.gps[8].pos;
    const Vec3 boss{900.0f, 53.0f, 120.0f};
    RouteState st;
    route_enter(st, 2, true, true);  // mode 7 (data)
    st.route = 2;
    Tick t = tick(st, g, boss, player, false);
    CHECK(st.gp == 8);
    REQUIRE(st.ap >= 0);
    CHECK(g.points[st.ap].gp == 8);
    CHECK(g.points[st.ap].ap);
    for (const int p : g.gps[8].points) {  // the AP nearest the boss
        if (g.points[p].ap) CHECK(dist(g.points[st.ap].pos, boss) <= dist(g.points[p].pos, boss));
    }
    CHECK(same(t.goal, g.points[st.ap].pos));
    t = tick(st, g, boss, player, false);
    CHECK(same(t.face, g.gps[8].pos));  // angle 180 (data): always facing
    for (int i = 0; i < 3; ++i) {
        tick(st, g, boss, player, true);
        CHECK(st.doneAP);
    }
    tick(st, g, boss, player, false);
    CHECK_FALSE(st.doneAP);
}

TEST_CASE("route: player_gp_changed and the GP update keep 0.5 m of hysteresis") {
    RouteGraph g = kirin();
    ScrRegistry reg;
    scr_route_add(reg);
    const ScrClause clause{};
    ScrRuntime rt;
    ScrWorld w;
    w.routes = &g;
    rt.route.route = 2;
    rt.route.refPlayer = true;
    const auto changed = reg.predicates.at(0x8336f0);
    w.playerPosition = g.gps[8].pos;
    CHECK(changed(clause, w, rt));  // no current GP: true (binary)

    const Vec3 a = g.gps[8].pos, b = g.gps[9].pos;
    const Vec3 dir = vec3_normalize({b.x - a.x, b.y - a.y, b.z - a.z});
    const Vec3 mid = lerp(a, b, 0.5f);
    const Vec3 near{mid.x + dir.x * 0.1f, mid.y + dir.y * 0.1f, mid.z + dir.z * 0.1f};  // GP 9 0.2 m nearer
    const Vec3 far{mid.x + dir.x * 0.5f, mid.y + dir.y * 0.5f, mid.z + dir.z * 0.5f};   // 1.0 m nearer
    rt.route.gp = 8;
    rt.route.gpRoute = 2;
    w.playerPosition = near;
    CHECK_FALSE(changed(clause, w, rt));
    w.playerPosition = far;
    CHECK(changed(clause, w, rt));

    // The follower's GP update (tracking) keeps GP 8 inside the hysteresis, moves past it.
    RouteState st;
    route_enter(st, 6, true, true);
    st.route = 2;
    tick(st, g, {930, 53, 70}, a, false);
    REQUIRE(st.gp == 8);
    route_enter(st, 6, true, true);
    tick(st, g, {930, 53, 70}, near, false);
    CHECK(st.gp == 8);
    route_enter(st, 6, true, true);
    tick(st, g, {930, 53, 70}, far, false);
    CHECK(st.gp == 9);
}

TEST_CASE("route: closing tag 1 closes 27 links and the walk avoids them") {
    const RouteGraph g = kirin();
    RouteState st;
    const Vec3 from = g.nodes[12].pos, to = g.nodes[29].pos;  // the plain -> the hill
    const std::vector<int> before = as_nodes(g, route_walk(g, st.linkFlags, from, to, 1, kClear));
    REQUIRE(before.size() >= 2);
    CHECK(before.front() == 12);
    CHECK(before.back() == 29);
    bool usedTagged = false;
    for (size_t i = 1; i < before.size(); ++i) {
        const auto& l = g.links[link_between(g, before[i - 1], before[i])];
        usedTagged = usedTagged || l.tags[0] == 1 || l.tags[1] == 1;
    }
    CHECK(usedTagged);

    route_link_flags(st, g, 1, true);
    CHECK(closed_links(st.linkFlags) == 27);
    for (const auto& l : g.links) CHECK((l.flags & 4) == 0);  // the graph itself is never written
    const std::vector<int> after = as_nodes(g, route_walk(g, st.linkFlags, from, to, 1, kClear));
    REQUIRE_FALSE(after.empty());
    for (size_t i = 1; i < after.size(); ++i) {
        const int l = link_between(g, after[i - 1], after[i]);
        REQUIRE(l >= 0);
        CHECK((st.linkFlags[l] & 4) == 0);
    }
    CHECK(after.back() != 29);

    route_link_flags(st, g, 1, false);
    CHECK(closed_links(st.linkFlags) == 0);

    // Ruling R-AC: the live flags are runtime state, so a reset (a fresh RouteState) reopens them.
    route_link_flags(st, g, 1, true);
    REQUIRE(closed_links(st.linkFlags) == 27);
    st = RouteState{};
    CHECK(as_nodes(g, route_walk(g, st.linkFlags, from, to, 1, kClear)) == before);
}

TEST_CASE("route: route_by_dprelate and the link actions run from Phaedra's graph") {
    const RouteGraph routes = kirin();
    const std::string& json = kirin_graph_json();
    // Every fn the export names as a no-op (her registry is Task 14's), then the route fns.
    ScrRegistry reg;
    std::set<uint32_t> fns;
    std::function<void(const Json&)> walk = [&](const Json& v) {
        if (v.kind == Json::Object) {
            if (const Json* fn = v.get("fn"); fn && fn->kind == Json::String) {
                const uint32_t va = static_cast<uint32_t>(std::stoul(fn->string, nullptr, 16));
                if (v.get("act")) reg.actions[va] = [](const ScrAction&, ScrCtx&) {};
                else reg.predicates[va] = [](const ScrClause&, const ScrWorld&, const ScrRuntime&) { return false; };
            }
            for (const auto& [k, c] : v.object) walk(c);
        } else if (v.kind == Json::Array) {
            for (const Json& c : v.array) walk(c);
        }
    };
    walk(json_parse(json));
    reg.kinds = {-1, 0, 1, 10};
    scr_route_add(reg);
    for (int k = 2; k <= 9; ++k) CHECK(reg.kinds.count(k) == 1);
    CHECK(reg.locomotion != nullptr);
    ScrGraph graph;
    std::string err;
    const bool ok = scr_graph_load(json, knight_fixture::export_clips(json), reg, graph, err);
    INFO(err);
    REQUIRE(ok);
    auto list = [&](const char* key) {
        for (size_t i = 0; i < graph.actions.size(); ++i) {
            if (graph.actions[i].key == key) return static_cast<int>(i);
        }
        FAIL("no action list " << key);
        return -1;
    };

    ScrRuntime rt;
    ScrWorld w;
    w.routes = &routes;
    ScrStepOut out;
    ScrCtx ctx{graph, reg, rt, w, out};
    w.playerGroundAttr = 7ull << 48;  // the player on terrain class 7 (a tunnel)
    scr_run(ctx, list("KIRIN:F15"));
    CHECK(rt.route.route == 4);
    w.playerGroundAttr = 4ull << 48;  // class 4: no row -> unchanged
    scr_run(ctx, list("KIRIN:F15"));
    CHECK(rt.route.route == 4);

    scr_run(ctx, list("KIRIN:F69"));  // route_link_flag_set(1): mode 3's entry
    CHECK(closed_links(rt.route.linkFlags) == 27);
    scr_run(ctx, list("KIRIN:F67"));  // route_link_flag_clear(1)
    CHECK(closed_links(rt.route.linkFlags) == 0);

    // Ruling R-AB: terrain class 2 names route 1, which kirin.path lacks: the route id is set
    // (binary), the follower finds no GP and keeps the goal it had; nothing crashes.
    w.playerPosition = routes.gps[1].pos;
    w.bossPosition = {1122.0f, 59.0f, 147.0f};
    route_enter(rt.route, 2, true, true);
    Vec3 target{};
    REQUIRE(route_target(rt, w, target));  // route 4: a goal
    const Vec3 before = rt.route.goal;
    w.playerGroundAttr = 2ull << 48;
    scr_run(ctx, list("KIRIN:F15"));
    CHECK(rt.route.route == 1);
    route_enter(rt.route, 2, true, true);
    route_target(rt, w, target);
    CHECK(rt.route.gp == -1);
    CHECK(rt.route.hasGoal);
    CHECK(same(rt.route.goal, before));
    route_target(rt, w, target);  // the per-frame step with no GP
    CHECK(same(rt.route.goal, before));
    CHECK_FALSE(rt.route.doneAP);
}

TEST_CASE("route: kind 6 (mode 8) keeps mode 7's GP and faces on through it") {
    const RouteGraph g = kirin();
    const Vec3 boss{900.0f, 53.0f, 120.0f};
    RouteState st;
    route_enter(st, 2, true, true);  // mode 7: picks GP 8
    st.route = 2;
    tick(st, g, boss, g.gps[8].pos, false);
    REQUIRE(st.gp == 8);
    route_enter(st, 6, true, false);  // mode 8: ref player, tracking off (data)
    const Tick t = tick(st, g, boss, g.gps[9].pos, false);  // the player moved: no GP update
    CHECK(st.gp == 8);
    CHECK(t.hasGoal);
    CHECK(same(t.goal, g.gps[8].pos));  // offset 0
    const Vec3 gp = g.gps[8].pos;
    const float len = std::hypot(boss.x - gp.x, boss.z - gp.z);
    CHECK(t.face.x - t.goal.x == doctest::Approx(-(boss.x - gp.x) / len).epsilon(1e-4));
    CHECK(t.face.z - t.goal.z == doctest::Approx(-(boss.z - gp.z) / len).epsilon(1e-4));
    CHECK(t.face.y == doctest::Approx(t.goal.y));
    CHECK_FALSE(st.doneGP);
    tick(st, g, boss, g.gps[9].pos, true);
    CHECK(st.doneGP);
}

TEST_CASE("route: a mode entry clears the done levels (no stale doneAP into mode 7)") {
    const RouteGraph g = kirin();
    ScrRegistry reg;
    scr_route_add(reg);
    const auto moveDoneAp = reg.predicates.at(0x833660);
    const ScrClause clause{};
    ScrWorld w;
    ScrRuntime rt;
    const Vec3 boss{900.0f, 53.0f, 120.0f};
    route_enter(rt.route, 2, true, true);
    rt.route.route = 2;
    tick(rt.route, g, boss, g.gps[8].pos, false);
    tick(rt.route, g, boss, g.gps[8].pos, true);
    REQUIRE(rt.route.doneAP);
    route_enter(rt.route, 0, false, false);  // mode 4 (kind 0): the follower does not tick
    CHECK_FALSE(moveDoneAp(clause, w, rt));
    route_enter(rt.route, 2, true, true);    // mode 7 again
    CHECK_FALSE(moveDoneAp(clause, w, rt));
    tick(rt.route, g, boss, g.gps[8].pos, false);
    CHECK_FALSE(moveDoneAp(clause, w, rt));
}

TEST_CASE("route: with every link closed kind 5 still goes straight to the GP (Review Focus 4)") {
    RouteGraph g = kirin();
    for (auto& l : g.links) l.flags |= 4;
    const Vec3 boss{1122.0f, 59.0f, 147.0f};
    RouteState st;
    route_enter(st, 5, false, true);
    st.route = 4;
    Tick t = tick(st, g, boss, g.gps[1].pos, false);
    REQUIRE(st.gp == 0);
    CHECK(t.hasGoal);
    CHECK(same(t.goal, g.gps[0].pos));
    CHECK_FALSE(st.doneGP);
    tick(st, g, boss, g.gps[1].pos, false);
    CHECK_FALSE(st.doneGP);
    tick(st, g, boss, g.gps[1].pos, true);
    CHECK(st.doneGP);
    CHECK(route_walk(g, st.linkFlags, boss, g.gps[0].pos, 1, kClear).empty());

    // The hook hands the gait the GP itself.
    ScrRuntime rt;
    ScrWorld w;
    w.routes = &g;
    w.bossPosition = boss;
    rt.arriveRadius = 1.5f;  // mode 10's attain +0x10 (data)
    route_enter(rt.route, 5, false, true);
    rt.route.route = 4;
    Vec3 target{};
    REQUIRE(route_target(rt, w, target));
    CHECK(same(target, g.gps[0].pos));
    CHECK_FALSE(rt.arrived);
}

TEST_CASE("route: the locomotion hook hands the gait the next waypoint, then the face point") {
    RouteGraph g = kirin();
    ScrRegistry reg;
    scr_route_add(reg);
    ScrRuntime rt;
    ScrWorld w;
    w.routes = &g;
    w.playerPosition = g.gps[8].pos;
    w.bossPosition = {900.0f, 53.0f, 120.0f};
    rt.arriveRadius = 6.0f;  // modes 7-9's attain +0x10 (data)
    route_enter(rt.route, 2, true, true);
    rt.route.route = 2;
    Vec3 target{};
    REQUIRE(route_target(rt, w, target));
    const Vec3 ap = g.points[rt.route.ap].pos;
    CHECK(same(rt.goal, ap));
    CHECK(rt.hasGoal);
    CHECK_FALSE(rt.arrived);
    CHECK(same(target, ap));  // nothing blocks: straight at the goal

    // A wall on every segment but those touching a node: the walk's waypoints lead.
    w.ray = [&](const Vec3& from, const Vec3& to, Vec3& h) {
        h = to;
        for (const auto& n : g.nodes) {
            if (same(n.pos, from) || same(n.pos, to)) return false;
        }
        return true;
    };
    REQUIRE(route_target(rt, w, target));
    bool onNode = false;
    for (const auto& n : g.nodes) onNode = onNode || same(n.pos, target);
    CHECK(onNode);
    w.ray = nullptr;

    w.bossPosition = {ap.x + 1.0f, ap.y, ap.z};  // inside the 6 m
    REQUIRE(route_target(rt, w, target));
    CHECK(rt.arrived);
    CHECK(same(target, g.gps[8].pos));  // arrived: face the GP
    REQUIRE(route_target(rt, w, target));
    CHECK(rt.route.doneAP);
    const ScrClause clause{};
    CHECK(reg.predicates.at(0x833660)(clause, w, rt));   // move_done_ap
    CHECK_FALSE(reg.predicates.at(0x833680)(clause, w, rt));  // move_done
}
