#pragma once

#include <string>

#include "scr_graph.h"

// The shared predicate/action library every colossus's registry starts from (S13 design D5,
// Task 5): the 0x82…/0x83… handlers, the anger model, the locomotion kinds −1/0/1/10, the
// attackdata runner on exported rows. Pure: no SDL, no GPU.

// Registers the shared handlers and kinds into `registry`. S13 Task 5.
void scr_library_add(ScrRegistry& registry);

// S12's slowleg gait toward `goal` (R1: steer bits 48/49, accel 50, backward 52, stop-turn 55/56,
// brake 57). S13 Task 5 moves its body here.
void scr_slowleg(ScrRuntime& runtime, const ScrWorld& world, const Vec3& goal, bool arrived, ScrStepOut& out);

// What `registry` can run, one line each (S13 design D13, the coverage report's input): every
// fn `<hex> implemented|gap` (sorted; the evaluator's own `sub` 832610 always), every locomotion
// kind `kind <n>` (sorted), every evaluator opcode `op 0x<hh> implemented|gap` (kScrCdefOps).
std::string scr_registry_dump(const ScrRegistry& registry);
