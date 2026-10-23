#pragma once

// The interactive loop (S10 Task 6d): the keyboard, the gamepad and the window
// drive the stage; --s7-e2e, --s8-e2e and --s9-e2e run the same loop with a
// scripted input source.

#include "selftests.h"

SelftestResult run_interactive(StageCtx& ctx, RunState& run);
