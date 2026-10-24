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

// The opcodes scr_cdef_eval implements: the 37 the 16 exports use (census16 §4; R2's 28 + 9).
// The loader refuses a record with any other opcode.
inline constexpr std::array<ScrCdefOp, 37> kScrCdefOps{{
    {0x02, true}, {0x04, false}, {0x27, false}, {0x28, false}, {0x36, false}, {0x38, false}, {0x3a, false},
    {0x3b, false}, {0x42, false}, {0x43, false}, {0x45, false}, {0x4a, true},  {0x4b, true},  {0x4c, false},
    {0x4d, true},  {0x54, false}, {0x56, false}, {0x59, false}, {0x5c, false}, {0x5d, false}, {0x64, false},
    {0x67, false}, {0x68, false}, {0x69, false}, {0x6c, false}, {0x6e, false}, {0x71, false}, {0x74, false},
    {0x76, false}, {0x78, false}, {0x79, false}, {0x7a, false}, {0x7c, false}, {0x7d, false}, {0x7e, false},
    {0x80, false}, {0x83, false},
}};

// Whether `op` is in kScrCdefOps.
constexpr bool scr_cdef_known(int op) {
    for (const ScrCdefOp& o : kScrCdefOps) {
        if (o.op == op) return true;
    }
    return false;
}

// Whether the control-def `handle` (a control-assign row's `when`) holds this frame, with the
// frame's requested control bits. A handle the export does not carry is false.
bool scr_cdef_eval(const ScrGraph& graph, std::string_view handle, const std::vector<int>& requested,
                   const ScrWorld& world, const ScrRuntime& runtime);
