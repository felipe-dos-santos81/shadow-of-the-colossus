#pragma once

#include <string>
#include <vector>

#include "additive.h"
#include "scr_graph.h"

// Phaedra's (`KIRIN`) registry and load-time check (S13 Task 14): the shared library, the
// routes and her boss-local fns (census16 findings, headline 7), and the additive layer's
// requests. Pure.
//
// Loading her: scr_graph_load with scr_registry_kirin(), then scr_attack_attain and
// scr_move_back_bind (the sidecar's judge defaults), then scr_kirin_check (it refuses a graph
// either bind left unbound).
//
// A frame (the game's order, binary FUN_00631cd0): scr_boss_step -> scr_control_additive on its
// controls (the control-defs see the layers before they step) -> additive_step ->
// additive_request for each orient, in order. The add rows are level-triggered (binary
// FUN_006329b0 queues every firing row each frame through FUN_00634f80, no edge test); a
// from-row tells two motion-defs of one clip apart by AddEdge::fromRef.

// Her registry: scr_library_add + scr_route_add + her locals; enterMode is
// scr_library_enter_mode then scr_route_enter. S13 Task 14.
ScrRegistry scr_registry_kirin();

// The checks the strict loader cannot make (as scr_knight_check), the routes included: every
// control-assign row's control-def is exported, every attackdata table and row motion exists,
// every judge volume (attackdata FIX/ATK/ATTAIN, a slowstatus judge) is in `judgeNames`, every
// row's ATTAIN cylinder and every judge's MOVE_BACK sector is bound, every argument slot the
// handlers read is present, and a mode of kind 2-9 has a route graph. A dprelate route id the
// route graph lacks is not refused (ruling R-AB: it resolves to "no route" at run time). False
// with `error` naming the culprit. S13 Task 14.
bool scr_kirin_check(const ScrGraph& graph, const RouteGraph* routes, const std::vector<std::string>& judgeNames,
                     std::string& error);

// The motion-graph edges of the table whose name ends `_ADD`, in table order, for
// additive_request. S13 Task 14.
std::vector<AddEdge> scr_additive_edges(const ScrGraph& graph);

// The orients of the additive control-assign rows of the boss's control-mode
// (world.bossControlMode) that fire this frame (a requested bit or the row's control-def), in
// table order, at most one per layer slot (binary FUN_00634f80 caps the queue at the layer
// count). S13 Task 14.
std::vector<std::string> scr_control_additive(const ScrGraph& graph, ScrRuntime& runtime, const std::vector<int>& ctrls,
                                              const ScrWorld& world);
