#pragma once

#include <array>
#include <string_view>
#include <vector>

#include "scr_graph.h"

// The generic control-def evaluator (S13 design D5, Task 4): the AND of (result == expect) over
// a control-def's exported records (R2, docs/notes/s12-recon.md; graph.controlDefs). Pure.

// One opcode the evaluator knows; `gap` marks a labelled no-op (reads state the app does not
// model: false), which the registry dump and the coverage report list apart.
struct ScrCdefOp {
    int op;
    bool gap;
};

// The opcodes scr_cdef_eval implements. Empty until S13 Task 4 fills it (37 opcodes).
inline constexpr std::array<ScrCdefOp, 0> kScrCdefOps{};

// Whether the control-def `handle` (a control-assign row's `when`) holds this frame, with the
// frame's requested control bits. S13 Task 4.
bool scr_cdef_eval(const ScrGraph& graph, std::string_view handle, const std::vector<int>& requested,
                   const ScrWorld& world, const ScrRuntime& runtime);
