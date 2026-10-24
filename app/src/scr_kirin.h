#pragma once

#include <string>
#include <vector>

#include "scr_graph.h"

// Phaedra's (`KIRIN`) registry and load-time check (S13 Task 14): the shared library, the
// routes and her boss-local fns (census16 findings, headline 7). Pure.

// Her registry: scr_library_add + scr_route_add + her locals. S13 Task 14.
ScrRegistry scr_registry_kirin();

// The checks the strict loader cannot make (as scr_knight_check), the routes included. False
// with `error` naming the culprit. S13 Task 14.
bool scr_kirin_check(const ScrGraph& graph, const RouteGraph* routes, const std::vector<std::string>& judgeNames,
                     std::string& error);
