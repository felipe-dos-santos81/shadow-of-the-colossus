#include "scr_library.h"

#include <cstdio>
#include <set>

#include "scr_cdef.h"

void scr_library_add(ScrRegistry&) {}  // S13 Task 5

void scr_slowleg(ScrRuntime&, const ScrWorld&, const Vec3&, bool, ScrStepOut&) {}  // S13 Task 5

std::string scr_registry_dump(const ScrRegistry& registry) {
    std::set<uint32_t> fns(registry.gaps.begin(), registry.gaps.end());
    fns.insert(kScrSubFn);  // scr_eval evaluates `sub` itself
    for (const auto& [fn, handler] : registry.predicates) fns.insert(fn);
    for (const auto& [fn, handler] : registry.actions) fns.insert(fn);
    std::string out;
    char line[48];
    for (const uint32_t fn : fns) {
        std::snprintf(line, sizeof line, "%x %s\n", fn, registry.gaps.count(fn) != 0 ? "gap" : "implemented");
        out += line;
    }
    for (const int kind : std::set<int>(registry.kinds.begin(), registry.kinds.end())) {
        out += "kind " + std::to_string(kind) + "\n";
    }
    for (const ScrCdefOp& op : kScrCdefOps) {
        std::snprintf(line, sizeof line, "op 0x%02x %s\n", op.op, op.gap ? "gap" : "implemented");
        out += line;
    }
    return out;
}
