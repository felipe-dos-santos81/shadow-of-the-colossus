#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "math3d.h"

// A colossus's routes (S13 design D7): the `.path` route graph exported as `sotc-route/0`
// (docs/notes/s13-schemas.md; positions in the glb frame), the route follower's kinds 2-9
// and the nav-graph walk. Pure: no SDL, no GPU. S13 Task 1 declares the contract; Task 8
// fills it (path findings: .superpowers/sdd/2026-10-23-s13-recon/path/findings.md).
//
// No scr_graph.h here: scr_graph.h includes this header (ScrRuntime holds a RouteState).

struct ScrRegistry;
struct ScrCtx;

// A loaded `sotc-route/0` document (Task 8): the nav graph (nodes, links) and the route tree
// (routes -> GPs -> AP/PP points); indices are the export's, checked in range at load.
struct RouteGraph {
    struct Node {
        Vec3 pos;
        int cls = 0;  // +18 walker class bits (1 class 0, 2 class 1)
        int tag = 0;
        std::vector<int> links;
    };
    struct Link {
        int a = 0, b = 0;
        int flags = 0;  // the live flags (+08): 0x4 closed, 0x1/0x2 walker class
        int tags[2] = {-1, -1};
        float length = 0.0f;
    };
    struct Route {
        int id = 0;
        Vec3 pos;
        std::vector<int> gps;
    };
    struct Gp {
        Vec3 pos;
        std::vector<int> points;
    };
    struct Point {
        Vec3 pos;
        bool ap = false, pp = false;
        float angle = 0.0f;  // degrees (kind 2's facing cone)
        int gp = -1;         // the owning GP
    };
    std::vector<Node> nodes;
    std::vector<Link> links;
    std::vector<Route> routes;
    std::vector<Gp> gps;
    std::vector<Point> points;
};

// The follower's state (binary: path findings §4, W+0x4730..+0x4774).
struct RouteState {
    int kind = -1;          // +4730 the locomotion kind (2..9)
    int route = -1;         // the route id (route_by_dprelate)
    int gp = -1, ap = -1, pp = -1;
    int ppCount = 0;        // +4774 PP steps
    bool refPlayer = false; // +4748: GP nearest the player (1) or the boss (0)
    bool track = false;     // +474d tracking
    bool replan = false;
    bool doneGP = false, doneAP = false, donePP = false;  // +4770/+4771/+4772, levels
    float offset = 0.0f;    // +4750 (only FUN_0082f5f0 sets it; not in KIRIN)
    uint32_t rng = 1;       // kind 9's LFSR state (guess: the game's DAT_02f11404 runs globally)
    // The follower's own bookkeeping (binary FUN_00859ad0): the kind/route of the last tick
    // (+4734, +4740), the route the GP was picked on (+4760), and the goal it set (W+0x54,
    // the facing W+0xb0; `face` is the goal itself when the kind sets no facing).
    int prevKind = -1, prevRoute = -1, gpRoute = -1;
    bool hasGoal = false;
    Vec3 goal{0.0f, 0.0f, 0.0f}, face{0.0f, 0.0f, 0.0f};
};

// Parses `json` into `out`; false with `error` naming the culprit. S13 Task 8.
bool route_load(const std::string& json, RouteGraph& out, std::string& error);

// The follower's mode entry: the kind and the slowstatus +04/+08. S13 Task 8.
void route_enter(RouteState& st, int kind, bool refPlayer, bool track);

// One frame of kinds 2-9 (FUN_00859ad0): the goal (and whether there is one) and the point to
// face on arrival (the goal itself when the kind sets no facing). `arrived`: at the goal last
// frame; `sweep`: kind 8's AP->GP collision test. S13 Task 8.
void route_tick(RouteState& st, const RouteGraph& graph, Vec3 boss, Vec3 player, bool arrived,
                const std::function<bool(Vec3 from, Vec3 to, Vec3& hit)>& sweep, Vec3& goal, bool& hasGoal,
                Vec3& face);

// The nav-graph walk from `from` to `to` over open, class-allowed links; empty when none (the
// caller goes straight). S13 Task 8.
std::vector<Vec3> route_walk(const RouteGraph& graph, Vec3 from, Vec3 to, int classMask,
                             const std::function<bool(Vec3, Vec3)>& clear);

// Closes (sets bit 0x4) or opens every link tagged `tag`. S13 Task 8.
void route_link_flags(RouteGraph& graph, int tag, bool close);

// Registers the route predicates/actions, kinds 2-9 and the registry's locomotion hook. S13 Task 8.
void scr_route_add(ScrRegistry& registry);

// route_enter from mode `mode`'s slowstatus, for an enterMode hook. S13 Task 8.
void scr_route_enter(int mode, ScrCtx& ctx);

struct ScrRuntime;
struct ScrWorld;

// The locomotion hook without the gait: route_tick on `world.routes` (the boss, the player,
// arrival within runtime.arriveRadius of the route goal), runtime.goal/hasGoal/arrived from
// it, and `target`: once arrived the face point, else the farthest of the nav walk's
// waypoints (then the goal) with a clear segment from the boss. False: no goal. S13 Task 8.
bool route_target(ScrRuntime& runtime, const ScrWorld& world, Vec3& target);
