#include "route.h"

#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>

#include "json_reader.h"
#include "scr_graph.h"
#include "scr_library.h"

// S13 Task 8. The follower is FUN_00859ad0 (binary, decompiled for this task; path findings
// §4): a tick clears the three done levels, then either re-enters (the kind, the route or
// the replan flag changed: the entry switch picks the GP and sets the goal) or runs the
// kind's per-frame step (arrival sets the level, kind 9 picks the next PP).

namespace {

constexpr float kGpHysteresis = 0.5f;   // data 0x128f238 / 0x128f250 (FUN_00859650 / FUN_0085a420)
constexpr float kSweepBack = 0.1f;      // data 0x128d5a4: FUN_008214f0 pulls a hit back toward the AP
constexpr float kNormEps = 1.1920929e-7f;  // data 0x128f244: below it a direction is zero
constexpr float kLfsrScale = 1.52590219e-05f;  // data 0x1024cdc (1/65535)
constexpr int kWalkerClassMask = 1;     // guess: KIRIN's walker class 0 (mask 1) is not settled (path findings §2)

using Graph = RouteGraph;

float dist3(Vec3 a, Vec3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

Vec3 norm3(Vec3 v) {
    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > kNormEps ? Vec3{v.x / l, v.y / l, v.z / l} : Vec3{0.0f, 0.0f, 0.0f};
}

// The plan direction from -> to (y 0), the binary's xz-normalised vectors.
Vec3 flat_dir(Vec3 from, Vec3 to) {
    const float dx = to.x - from.x, dz = to.z - from.z, l = std::hypot(dx, dz);
    return l > kNormEps ? Vec3{dx / l, 0.0f, dz / l} : Vec3{0.0f, 0.0f, 0.0f};
}

Vec3 add(Vec3 a, Vec3 b, float s = 1.0f) { return {a.x + b.x * s, a.y + b.y * s, a.z + b.z * s}; }

const Graph::Route* find_route(const Graph& g, int id) {
    for (const auto& r : g.routes) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

// FUN_005866c0 (binary): a 16-bit shift register, the result scaled to [0, 1].
float lfsr(uint32_t& x) {
    const uint32_t s = x & 0xffffu;
    x = (s & 1u) != 0 ? s >> 1 : (s >> 1) ^ 0xb400u;
    return static_cast<float>(x) * kLfsrScale;
}

// FUN_00859760: the route's GP nearest the player (refPlayer) or the boss, 3-D; -1 none.
int nearest_gp(const RouteState& st, const Graph& g, Vec3 boss, Vec3 player) {
    const Graph::Route* r = find_route(g, st.route);
    if (!r) return -1;
    const Vec3 ref = st.refPlayer ? player : boss;
    int best = -1;
    float bestD = std::numeric_limits<float>::max();
    for (const int gp : r->gps) {
        const float d = dist3(g.gps[gp].pos, ref);
        if (d < bestD) best = gp, bestD = d;
    }
    return best;
}

// FUN_0085a420: while tracking, the nearest GP replaces the current one on the same route
// only when it is more than 0.5 m nearer.
void update_gp(RouteState& st, const Graph& g, Vec3 boss, Vec3 player) {
    if (!st.track) return;
    const int n = nearest_gp(st, g, boss, player);
    if (st.gpRoute == st.route && st.gp >= 0 && n >= 0 && st.gp != n) {
        const Vec3 ref = st.refPlayer ? player : boss;
        if (dist3(ref, g.gps[st.gp].pos) - dist3(ref, g.gps[n].pos) <= kGpHysteresis) return;
    }
    st.gp = n;
    st.gpRoute = st.route;
}

// FUN_008598e0: the GP's AP point nearest the boss; -1 none.
int nearest_ap(const Graph& g, int gp, Vec3 boss) {
    if (gp < 0) return -1;
    int best = -1;
    float bestD = std::numeric_limits<float>::max();
    for (const int p : g.gps[gp].points) {
        const float d = dist3(g.points[p].pos, boss);
        if (d < bestD && g.points[p].ap) best = p, bestD = d;
    }
    return best;
}

// FUN_0085a6d0: a random PP point of the GP other than `exclude` (at most 20 considered);
// `exclude` when there is none, -1 without a GP.
int pick_pp(RouteState& st, const Graph& g, int gp, int exclude) {
    if (gp < 0) return -1;
    int pool[20];
    int n = 0;
    for (const int p : g.gps[gp].points) {
        if (g.points[p].pp && n < 20 && p != exclude) pool[n++] = p;
    }
    if (n == 0) return exclude;
    const int i = static_cast<int>(static_cast<float>(n) * lfsr(st.rng));
    return pool[i < n ? i : n - 1];
}

// The goal setters: FUN_0081cfc0 (a point), FUN_0081d0c0/FUN_0081d190 (a point and a facing
// direction), FUN_0081d260 (a point facing another).
void set_goal(RouteState& st, Vec3 p) {
    st.goal = st.face = p;
    st.hasGoal = true;
}
void set_goal_dir(RouteState& st, Vec3 p, Vec3 dir) {
    st.goal = p;
    st.face = add(p, dir);
    st.hasGoal = true;
}
void set_goal_face(RouteState& st, Vec3 p, Vec3 face) {
    st.goal = p;
    st.face = face;
    st.hasGoal = true;
}

// FUN_0085a560 (kind 7): the GP pushed `offset` toward the player, facing the player.
void goal_toward_player(RouteState& st, const Graph& g, Vec3 player) {
    if (st.gp < 0) return;
    const Vec3 gp = g.gps[st.gp].pos, d = flat_dir(gp, player);
    set_goal_dir(st, add(gp, d, st.offset), d);
}

void enter(RouteState& st, const Graph& g, Vec3 boss, Vec3 player,
           const std::function<bool(Vec3, Vec3, Vec3&)>& sweep) {
    switch (st.kind) {
        case 2: case 3: case 4:  // the GP's AP point nearest the boss
            update_gp(st, g, boss, player);
            st.ap = nearest_ap(g, st.gp, boss);
            if (st.ap >= 0) set_goal(st, g.points[st.ap].pos);
            return;
        case 5: case 8: {
            update_gp(st, g, boss, player);
            if (st.gp < 0) return;
            const Vec3 gp = g.gps[st.gp].pos;
            if (st.ap < 0 && st.kind == 8) st.ap = nearest_ap(g, st.gp, boss);
            if (st.ap < 0 || g.points[st.ap].gp != st.gp) {  // no AP of this GP: the GP itself
                set_goal(st, gp);
                return;
            }
            // The AP -> GP sweep (FUN_008214f0): a hit stops 0.1 m short of it, else the GP.
            const Vec3 ap = g.points[st.ap].pos;
            Vec3 target = gp, hit{};
            if (sweep && sweep(ap, gp, hit)) target = add(hit, norm3({ap.x - hit.x, ap.y - hit.y, ap.z - hit.z}), kSweepBack);
            if (st.kind == 5) set_goal(st, target);
            else set_goal_dir(st, target, flat_dir(ap, gp));
            return;
        }
        case 6: {  // the GP pulled `offset` toward the boss, facing on through it
            update_gp(st, g, boss, player);
            if (st.gp < 0) return;
            const Vec3 gp = g.gps[st.gp].pos, d = flat_dir(gp, boss);
            set_goal_dir(st, add(gp, d, st.offset), {-d.x, -d.y, -d.z});
            return;
        }
        case 7:
            update_gp(st, g, boss, player);
            goal_toward_player(st, g, player);
            return;
        case 9:
            st.ppCount = 0;
            update_gp(st, g, boss, player);
            st.pp = pick_pp(st, g, st.gp, -1);
            if (st.pp >= 0) set_goal(st, g.points[st.pp].pos);
            return;
        default: return;
    }
}

void step(RouteState& st, const Graph& g, Vec3 boss, Vec3 player, bool arrived) {
    switch (st.kind) {
        case 2: case 3: case 4: {
            if (st.ap < 0) return;
            if (arrived) {
                st.doneAP = true;
                return;
            }
            if (st.gp < 0) return;
            // ponytail: the binary decides the facing on the last nav leg only (W+0x36c == 1);
            // here every walking frame (same goal, the face only matters on arrival).
            const Graph::Point& ap = g.points[st.ap];
            const Vec3 gp = g.gps[st.gp].pos;
            if (st.kind == 2) {  // facing only when boss->AP->GP turns less than the point's angle
                const Vec3 d1 = norm3({ap.pos.x - boss.x, ap.pos.y - boss.y, ap.pos.z - boss.z});
                const Vec3 d2 = norm3({gp.x - ap.pos.x, gp.y - ap.pos.y, gp.z - ap.pos.z});
                if (vec3_dot(d1, d2) <= std::cos(ap.angle * kDegToRad)) {
                    set_goal(st, ap.pos);
                    return;
                }
            }
            set_goal_face(st, ap.pos, gp);
            return;
        }
        case 5: case 6: case 8:
            if (st.gp >= 0 && arrived) st.doneGP = true;
            return;
        case 7:
            if (st.gp < 0) return;
            if (arrived) st.doneGP = true;
            goal_toward_player(st, g, player);
            return;
        case 9:
            if (st.pp < 0 || !arrived) return;
            st.pp = pick_pp(st, g, st.gp, st.pp);
            if (st.pp >= 0) set_goal(st, g.points[st.pp].pos);
            st.donePP = true;
            ++st.ppCount;
            return;
        default: return;
    }
}

// --- the loader ---

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error(what); }

const Json& need(const Json& o, const char* key, const std::string& where) {
    const Json* v = o.kind == Json::Object ? o.get(key) : nullptr;
    if (!v) fail(where + ": missing \"" + key + "\"");
    return *v;
}
double number(const Json& v, const std::string& where) {
    if (v.kind != Json::Number) fail(where + ": expected a number");
    return v.number;
}
int integer(const Json& v, const std::string& where) {
    const double d = number(v, where);
    if (d != std::floor(d) || std::fabs(d) > 1e9) fail(where + ": expected an integer");
    return static_cast<int>(d);
}
int index(const Json& v, size_t size, const std::string& where) {
    const int i = integer(v, where);
    if (i < 0 || static_cast<size_t>(i) >= size) fail(where + ": index " + std::to_string(i) + " out of range");
    return i;
}
Vec3 position(const Json& v, const std::string& where) {
    if (v.kind != Json::Array || v.array.size() != 3) fail(where + ": expected [x, y, z]");
    return {static_cast<float>(number(v.array[0], where)), static_cast<float>(number(v.array[1], where)),
            static_cast<float>(number(v.array[2], where))};
}
const std::vector<Json>& array(const Json& v, const std::string& where) {
    if (v.kind != Json::Array) fail(where + ": expected an array");
    return v.array;
}
bool boolean(const Json& v, const std::string& where) {
    if (v.kind != Json::Bool) fail(where + ": expected a bool");
    return v.boolean;
}

void parse(const Json& doc, Graph& g) {
    const Json* format = doc.kind == Json::Object ? doc.get("format") : nullptr;
    if (!format || format->kind != Json::String || format->string != "sotc-route/0") fail("not a sotc-route/0 document");
    const auto& nodes = array(need(doc, "nodes", "route"), "nodes");
    const auto& links = array(need(doc, "links", "route"), "links");
    const auto& routes = array(need(doc, "routes", "route"), "routes");
    const auto& gps = array(need(doc, "gps", "route"), "gps");
    const auto& points = array(need(doc, "points", "route"), "points");
    for (size_t i = 0; i < nodes.size(); ++i) {
        const std::string at = "node " + std::to_string(i);
        Graph::Node n;
        n.pos = position(need(nodes[i], "pos", at), at);
        n.cls = integer(need(nodes[i], "class", at), at);
        n.tag = integer(need(nodes[i], "tag", at), at);
        for (const Json& l : array(need(nodes[i], "links", at), at)) n.links.push_back(index(l, links.size(), at));
        g.nodes.push_back(std::move(n));
    }
    for (size_t i = 0; i < links.size(); ++i) {
        const std::string at = "link " + std::to_string(i);
        Graph::Link l;
        l.a = index(need(links[i], "a", at), nodes.size(), at);
        l.b = index(need(links[i], "b", at), nodes.size(), at);
        l.flags = integer(need(links[i], "flags", at), at);
        const auto& tags = array(need(links[i], "tags", at), at);
        if (tags.size() != 2) fail(at + ": expected two tags");
        l.tags[0] = integer(tags[0], at);
        l.tags[1] = integer(tags[1], at);
        l.length = static_cast<float>(number(need(links[i], "length", at), at));
        g.links.push_back(l);
    }
    for (size_t i = 0; i < routes.size(); ++i) {
        const std::string at = "route " + std::to_string(i);
        Graph::Route r;
        r.id = integer(need(routes[i], "id", at), at);
        r.pos = position(need(routes[i], "pos", at), at);
        for (const Json& gp : array(need(routes[i], "gps", at), at)) r.gps.push_back(index(gp, gps.size(), at));
        g.routes.push_back(std::move(r));
    }
    for (size_t i = 0; i < gps.size(); ++i) {
        const std::string at = "gp " + std::to_string(i);
        Graph::Gp gp;
        gp.pos = position(need(gps[i], "pos", at), at);
        for (const Json& p : array(need(gps[i], "points", at), at)) gp.points.push_back(index(p, points.size(), at));
        g.gps.push_back(std::move(gp));
    }
    for (size_t i = 0; i < points.size(); ++i) {
        const std::string at = "point " + std::to_string(i);
        Graph::Point p;
        p.pos = position(need(points[i], "pos", at), at);
        p.ap = boolean(need(points[i], "ap", at), at);
        p.pp = boolean(need(points[i], "pp", at), at);
        p.angle = static_cast<float>(number(need(points[i], "angle", at), at));
        p.gp = index(need(points[i], "gp", at), gps.size(), at);
        g.points.push_back(p);
    }
}

// --- the registry's fns ---

RouteState& route_of(ScrCtx& ctx) { return ctx.runtime.route; }

// FUN_008595f0(W, 5) / (W, 2): the levels the follower set this frame.
bool p_move_done(const ScrClause&, const ScrWorld&, const ScrRuntime& rt) { return rt.route.doneGP; }
bool p_move_done_ap(const ScrClause&, const ScrWorld&, const ScrRuntime& rt) { return rt.route.doneAP; }

// FUN_00859650: no current GP -> true; else the nearest GP is more than 0.5 m nearer.
bool p_player_gp_changed(const ScrClause&, const ScrWorld& w, const ScrRuntime& rt) {
    const RouteState& st = rt.route;
    if (!w.routes || st.gp < 0) return true;
    const int n = nearest_gp(st, *w.routes, w.bossPosition, w.playerPosition);
    if (n < 0) return false;  // the binary dereferences a missing GP; no route here reads as unchanged
    const Vec3 ref = st.refPlayer ? w.playerPosition : w.bossPosition;
    return dist3(ref, w.routes->gps[st.gp].pos) - dist3(ref, w.routes->gps[n].pos) > kGpHysteresis;
}

// FUN_0082e3a0 / FUN_0082ddf0: the first dprelate row whose player-ground mask/value and
// condition hold names the route; no row -> unchanged. A mask of all ones matches anything
// (binary). Rows naming a contact object never match (none in KIRIN; the object test
// FUN_006fb7f0 is not decoded: a labelled gap).
void a_route_by_dprelate(const ScrAction& a, ScrCtx& ctx) {
    const ScrArg* h = scr_arg(a.args, "h30");
    if (!h) return;
    const auto it = ctx.graph.dprelate.find(h->text);
    if (it == ctx.graph.dprelate.end()) return;
    for (const ScrDprelateRow& row : it->second) {
        if (row.mask != ~0ull && (ctx.world.playerGroundAttr & row.mask) != row.value) continue;
        if (!row.object.empty()) continue;
        if (!scr_eval(ctx.graph, ctx.registry, row.cond, ctx.runtime, ctx.world)) continue;
        if (row.route != -1) route_of(ctx).route = row.route;
        return;
    }
}

// FUN_0082e290 / FUN_0082e2c0 (FUN_0081c300): bit 0x4 on every link tagged i08. The game
// writes the stage's shared graph (DAT_03290da0); the world holds the glue's own graph by a
// const pointer, so the action writes through it.
void link_flags(const ScrAction& a, ScrCtx& ctx, bool close) {
    const ScrArg* tag = scr_arg(a.args, "i08");
    if (!ctx.world.routes || !tag) return;
    route_link_flags(const_cast<RouteGraph&>(*ctx.world.routes), static_cast<int>(tag->num), close);
}
void a_link_set(const ScrAction& a, ScrCtx& ctx) { link_flags(a, ctx, true); }
void a_link_clear(const ScrAction& a, ScrCtx& ctx) { link_flags(a, ctx, false); }

void route_locomotion(int, ScrCtx& ctx, float) {
    Vec3 target{};
    if (route_target(ctx.runtime, ctx.world, target)) {
        scr_slowleg(ctx.runtime, ctx.world, target, ctx.runtime.arrived, ctx.out);
    }
}

}  // namespace

bool route_load(const std::string& json, RouteGraph& out, std::string& error) {
    try {
        RouteGraph g;
        parse(json_parse(json), g);
        out = std::move(g);
        return true;
    } catch (const std::exception& e) {
        error = std::string("route_load: ") + e.what();
        return false;
    }
}

void route_enter(RouteState& st, int kind, bool refPlayer, bool track) {
    st.kind = kind;
    st.refPlayer = refPlayer;
    st.track = track;
    st.replan = true;
    // The entry switch's resets for kinds -1/0/1/10 (binary): the follower never ticks those
    // kinds, so mode entry applies them.
    if (kind < 2 || kind > 9) st.ap = st.pp = -1;
}

void route_tick(RouteState& st, const RouteGraph& graph, Vec3 boss, Vec3 player, bool arrived,
                const std::function<bool(Vec3 from, Vec3 to, Vec3& hit)>& sweep, Vec3& goal, bool& hasGoal,
                Vec3& face) {
    st.doneGP = st.doneAP = st.donePP = false;
    if (st.kind != st.prevKind || st.route != st.prevRoute || st.replan) enter(st, graph, boss, player, sweep);
    else step(st, graph, boss, player, arrived);
    st.prevKind = st.kind;
    st.prevRoute = st.route;
    st.replan = false;
    goal = st.goal;
    face = st.face;
    hasGoal = st.hasGoal;
}

std::vector<Vec3> route_walk(const RouteGraph& graph, Vec3 from, Vec3 to, int classMask,
                             const std::function<bool(Vec3, Vec3)>& clear) {
    const int n = static_cast<int>(graph.nodes.size());
    auto open = [&](int li) {
        const auto& l = graph.links[li];
        return (l.flags & 4) == 0 && (l.flags & classMask) != 0;
    };
    auto usable = [&](int i) {
        if ((graph.nodes[i].cls & classMask) == 0) return false;
        for (const int li : graph.nodes[i].links) {
            if (open(li)) return true;
        }
        return false;
    };
    // FUN_00820940 / FUN_00820f50: the nearest usable node with a clear segment to (from) / from (to) it.
    auto nearest = [&](Vec3 p, bool toNode) {
        std::vector<bool> tried(n, false);
        while (true) {
            int best = -1;
            float bestD = std::numeric_limits<float>::max();
            for (int i = 0; i < n; ++i) {
                const float d = dist3(graph.nodes[i].pos, p);
                if (!tried[i] && d < bestD && usable(i)) best = i, bestD = d;
            }
            if (best < 0) return -1;
            tried[best] = true;
            const Vec3 q = graph.nodes[best].pos;
            if (!clear || (toNode ? clear(p, q) : clear(q, p))) return best;
        }
    };
    const int entry = nearest(from, true);
    const int exit = entry < 0 ? -1 : nearest(to, false);
    if (exit < 0) return {};
    // ponytail: the core search (FUN_0081ff60/FUN_00821840) is not decoded; an O(n²) Dijkstra on
    // the link lengths stands in (guess, path findings §2) — fine for 52 nodes.
    std::vector<float> cost(n, std::numeric_limits<float>::max());
    std::vector<int> prev(n, -1);
    std::vector<bool> done(n, false);
    cost[entry] = 0.0f;
    for (int u = entry; u >= 0 && u != exit;) {
        done[u] = true;
        for (const int li : graph.nodes[u].links) {
            if (!open(li)) continue;
            const auto& l = graph.links[li];
            const int v = l.a == u ? l.b : l.a;
            if (!done[v] && cost[u] + l.length < cost[v]) cost[v] = cost[u] + l.length, prev[v] = u;
        }
        u = -1;
        for (int i = 0; i < n; ++i) {
            if (!done[i] && cost[i] < std::numeric_limits<float>::max() && (u < 0 || cost[i] < cost[u])) u = i;
        }
    }
    if (exit != entry && prev[exit] < 0) return {};
    std::vector<Vec3> path;
    for (int i = exit; i >= 0; i = i == entry ? -1 : prev[i]) path.insert(path.begin(), graph.nodes[i].pos);
    return path;
}

void route_link_flags(RouteGraph& graph, int tag, bool close) {
    for (auto& l : graph.links) {
        if (l.tags[0] != tag && l.tags[1] != tag) continue;
        l.flags = close ? (l.flags | 4) : (l.flags & ~4);
    }
}

bool route_target(ScrRuntime& rt, const ScrWorld& w, Vec3& target) {
    if (!w.routes) return false;
    RouteState& st = rt.route;
    auto near = [&](Vec3 p) { return std::hypot(p.x - w.bossPosition.x, p.z - w.bossPosition.z) <= rt.arriveRadius; };
    auto clear = [&](Vec3 a, Vec3 b) {
        Vec3 hit{};
        return !w.ray || !w.ray(a, b, hit);
    };
    auto sweep = [&](Vec3 a, Vec3 b, Vec3& hit) { return w.ray && w.ray(a, b, hit); };
    // Arrival at the route goal (the game's W+0x3d6 with no waypoint left), as kind 0's test.
    const bool arrived = st.hasGoal && near(st.goal);
    Vec3 goal{}, face{};
    bool has = false;
    route_tick(st, *w.routes, w.bossPosition, w.playerPosition, arrived, sweep, goal, has, face);
    rt.hasGoal = has;
    rt.arrived = has && near(goal);
    if (!has) return false;
    rt.goal = goal;
    if (rt.arrived) {
        target = face;
        return true;
    }
    // authored: the walk's waypoints string-pulled from the boss (the farthest visible one,
    // the goal last), so a re-plan every frame never walks back to a node already passed.
    std::vector<Vec3> pts = route_walk(*w.routes, w.bossPosition, goal, kWalkerClassMask, clear);
    pts.push_back(goal);
    target = pts.front();
    for (size_t i = pts.size(); i-- > 0;) {
        if (clear(w.bossPosition, pts[i])) {
            target = pts[i];
            break;
        }
    }
    return true;
}

void scr_route_add(ScrRegistry& registry) {
    registry.predicates[0x833680] = p_move_done;
    registry.predicates[0x833660] = p_move_done_ap;
    registry.predicates[0x8336f0] = p_player_gp_changed;
    registry.actions[0x82e3a0] = a_route_by_dprelate;
    registry.actions[0x82e290] = a_link_set;
    registry.actions[0x82e2c0] = a_link_clear;
    for (int k = 2; k <= 9; ++k) registry.kinds.insert(k);
    registry.locomotion = route_locomotion;
}

void scr_route_enter(int mode, ScrCtx& ctx) {
    const ScrLocomotion& l = ctx.graph.modes[mode].locomotion;
    route_enter(ctx.runtime.route, l.kind, l.refPlayer, l.track);
}
