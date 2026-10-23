#pragma once

// The scripted runs, one function per selftest (S10 Task 6c, design §13.9).
// Each takes the stage it runs in and the run around it; what it returns tells
// the stage loop to end the run (`exitCode`) or to load another stage
// (`next`; the selftest has already released the scene). Neither = the path
// falls through to the ones after it, as the inline blocks did.

#include "stage_ctx.h"

#include <optional>

struct SelftestResult {
    std::optional<int> exitCode;
    std::optional<StageId> next;

    SelftestResult() = default;
    // `return 0;`, `return fail(...)`: the bodies keep the run_app returns they
    // were written with.
    SelftestResult(int code) : exitCode(code) {}  // NOLINT(google-explicit-constructor)
    static SelftestResult next_stage(StageId stage) {
        SelftestResult result;
        result.next = stage;
        return result;
    }
};

// --selftest (S1): one frame and the s1-status line. --screenshot: the capture run.
SelftestResult run_selftest(StageCtx& ctx, RunState& run);
SelftestResult run_screenshot(StageCtx& ctx, RunState& run);
SelftestResult run_s3(StageCtx& ctx, RunState& run);
