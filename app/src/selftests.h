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
SelftestResult s4_pin_entity(StageCtx& ctx);
SelftestResult run_s4(StageCtx& ctx, RunState& run);
SelftestResult run_s6(StageCtx& ctx, RunState& run);
SelftestResult run_s7(StageCtx& ctx, RunState& run);
SelftestResult run_s41(StageCtx& ctx, RunState& run);
SelftestResult run_s42(StageCtx& ctx, RunState& run);
SelftestResult run_s8(StageCtx& ctx, RunState& run);
SelftestResult run_s9(StageCtx& ctx, RunState& run);
SelftestResult run_s11(StageCtx& ctx, RunState& run);
SelftestResult run_s12(StageCtx& ctx, RunState& run);

// The scripted paths in run_app's order (design §13.9's list, explicitly): the
// first that ends the run or switches the stage wins; a path that falls through
// hands on to the next, and none = the interactive loop. (The s4 pin runs in
// stage_setup.)
inline SelftestResult run_selftests(StageCtx& ctx, RunState& run) {
    const AppOptions& opts = run.opts;
    if (opts.s41Selftest) {
        const SelftestResult result = run_s41(ctx, run);
        if (result.exitCode || result.next) {
            return result;
        }
    }
    if (opts.s42Selftest || opts.s5Selftest) {
        const SelftestResult result = run_s42(ctx, run);
        if (result.exitCode || result.next) {
            return result;
        }
    }
    if (opts.s7Selftest) {
        const SelftestResult result = run_s7(ctx, run);
        if (result.exitCode || result.next) {
            return result;
        }
    }
    if (opts.s8Selftest) {
        const SelftestResult result = run_s8(ctx, run);
        if (result.exitCode || result.next) {
            return result;
        }
    }
    if (opts.s9Selftest) {
        const SelftestResult result = run_s9(ctx, run);
        if (result.exitCode || result.next) {
            return result;
        }
    }
    if (opts.s11Selftest) {
        const SelftestResult result = run_s11(ctx, run);
        if (result.exitCode || result.next) {
            return result;
        }
    }
    if (opts.s12Selftest) {
        const SelftestResult result = run_s12(ctx, run);
        if (result.exitCode || result.next) {
            return result;
        }
    }
    if (opts.s6Selftest) {
        const SelftestResult result = run_s6(ctx, run);
        if (result.exitCode || result.next) {
            return result;
        }
    }
    if (opts.s4Selftest) {
        const SelftestResult result = run_s4(ctx, run);
        if (result.exitCode || result.next) {
            return result;
        }
    }
    if (opts.s3Selftest) {
        const SelftestResult result = run_s3(ctx, run);
        if (result.exitCode || result.next) {
            return result;
        }
    }
    if (opts.selftest) {
        const SelftestResult result = run_selftest(ctx, run);
        if (result.exitCode || result.next) {
            return result;
        }
    }
    if (!opts.screenshotPath.empty()) {
        const SelftestResult result = run_screenshot(ctx, run);
        if (result.exitCode || result.next) {
            return result;
        }
    }
    return {};
}
