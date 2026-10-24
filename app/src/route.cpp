#include "route.h"

// S13 Task 1 stubs: Task 8 implements the route module.

bool route_load(const std::string&, RouteGraph&, std::string& error) {
    error = "route_load: not implemented (S13 Task 8)";
    return false;
}

void route_enter(RouteState&, int, bool, bool) {}  // S13 Task 8

void route_tick(RouteState&, const RouteGraph&, Vec3, Vec3, bool, const std::function<bool(Vec3, Vec3, Vec3&)>&, Vec3&,
                bool& hasGoal, Vec3&) {
    hasGoal = false;  // S13 Task 8
}

std::vector<Vec3> route_walk(const RouteGraph&, Vec3, Vec3, int, const std::function<bool(Vec3, Vec3)>&) {
    return {};  // S13 Task 8
}

void route_link_flags(RouteGraph&, int, bool) {}  // S13 Task 8

void scr_route_add(ScrRegistry&) {}  // S13 Task 8

void scr_route_enter(int, ScrCtx&) {}  // S13 Task 8
