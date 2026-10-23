// The run flags (S10 Task 5, design §13.8). Exhaustive: every combination of the
// boolean inputs the derived run-mode flags read (13 selftest/e2e flags,
// entityTimeSet, screenshotPath empty/non-empty, noReturn, followMode,
// hasEntity, entityRigValid, returnAvailable = 2^20) times the three stages,
// each field compared against the inline expression it replaced.
#include <doctest.h>

#include <cstdint>

#include "app.h"
#include "run_flags.h"

namespace {

// The oracle: the expressions verbatim from main 3481458. The only rewrites are
// the two scene facts the app reads off its loaded state:
// `!manifest.entities.empty()` -> hasEntity and `entityRig < playbacks.size()`
// -> entityRigValid.
struct Oracle {
    RunFlags run;
    SceneFlags scene;
    bool notScriptedE2e;  // app.cpp:8205, the negated spelling
};

Oracle oracle(const AppOptions& opts, StageId here, bool follow_mode, bool hasEntity,
                bool entityRigValid, bool return_available) {
    // main.cpp:382
    const bool frozen_selftest = opts.selftest || opts.s3Selftest || opts.s4Selftest ||
                                 opts.s41Selftest || opts.s42Selftest || opts.s5Selftest ||
                                 opts.s6Selftest || opts.s8Selftest || opts.s9Selftest;
    // app.cpp:2496
    const bool death_wired =
        opts.s6Selftest ||
        (!opts.selftest && !opts.s3Selftest && !opts.s4Selftest && !opts.s41Selftest &&
         !opts.s42Selftest && !opts.s5Selftest && !opts.s8Selftest &&
         !opts.s9Selftest && !opts.entityTimeSet && opts.screenshotPath.empty());
    // app.cpp:2505
    const bool interactive_run =
        !opts.selftest && !opts.s3Selftest && !opts.s4Selftest && !opts.s41Selftest &&
        !opts.s42Selftest && !opts.s5Selftest && !opts.s6Selftest && !opts.s7Selftest &&
        !opts.s8Selftest && !opts.s9Selftest && !opts.entityTimeSet &&
        opts.screenshotPath.empty();
    // app.cpp:2513
    const bool s9_wired = (interactive_run && !opts.s7E2e && !opts.s8E2e) ||
                          opts.s9Selftest || opts.s9E2e;
    // app.cpp:2516
    const bool road = s9_wired && here == StageId::Plain;
    // app.cpp:2525
    bool return_wired = follow_mode && return_available &&
                        (opts.s7Selftest || (interactive_run && !opts.noReturn));
    // app.cpp:2555
    const bool pump_load =
        opts.s7Selftest || opts.s8Selftest || opts.s9Selftest || interactive_run;
    // app.cpp:2951
    const bool audio_wanted =
        follow_mode && (hasEntity || here == StageId::Plain) &&
        (opts.s5Selftest || opts.s6Selftest ||
         (!opts.selftest && !opts.s3Selftest && !opts.s4Selftest && !opts.s41Selftest &&
          !opts.s42Selftest && !opts.entityTimeSet));
    // app.cpp:3002
    const bool valus_behavior = follow_mode && hasEntity &&
                                entityRigValid && !opts.s3Selftest &&
                                !opts.s4Selftest && !opts.s41Selftest &&
                                !opts.s42Selftest && !opts.s5Selftest &&
                                !opts.entityTimeSet;
    // app.cpp:5101
    const bool s8_wired =
        (interactive_run && !opts.s7E2e) || opts.s8Selftest || opts.s9Selftest;
    Oracle o{};
    RunFlags& f = o.run;
    f.frozenSelftest = frozen_selftest;
    f.interactiveRun = interactive_run;
    f.deathWired = death_wired;
    f.returnWired = return_wired;
    f.pumpLoad = pump_load;
    f.s8Wired = s8_wired;
    f.s9Wired = s9_wired;
    f.road = road;
    // app.cpp:8100, 8200
    f.scriptedE2e = (opts.s7E2e || opts.s8E2e || opts.s9E2e);
    // app.cpp:8205
    o.notScriptedE2e = !opts.s7E2e && !opts.s8E2e && !opts.s9E2e;
    // app.cpp:2960, 2964, 2972
    f.audioStrict = opts.s5Selftest || opts.s6Selftest;
    o.scene.audioWanted = audio_wanted;
    o.scene.valusBehavior = valus_behavior;
    return o;
}

}  // namespace

TEST_CASE("run_flags reproduces every derived run flag over all inputs") {
    constexpr int kBits = 20;
    std::uint64_t cases = 0;
    for (const StageId stage : {StageId::Arena, StageId::Shrine, StageId::Plain}) {
        for (std::uint32_t m = 0; m < (1u << kBits); ++m) {
            const auto bit = [m](int i) { return ((m >> i) & 1u) != 0; };
            AppOptions o;
            o.selftest = bit(0);
            o.s3Selftest = bit(1);
            o.s4Selftest = bit(2);
            o.s41Selftest = bit(3);
            o.s42Selftest = bit(4);
            o.s5Selftest = bit(5);
            o.s6Selftest = bit(6);
            o.s7Selftest = bit(7);
            o.s7E2e = bit(8);
            o.s8Selftest = bit(9);
            o.s8E2e = bit(10);
            o.s9Selftest = bit(11);
            o.s9E2e = bit(12);
            o.entityTimeSet = bit(13);
            o.screenshotPath = bit(14) ? "shot.png" : "";
            o.noReturn = bit(15);
            const bool follow = bit(16), hasEntity = bit(17), rigValid = bit(18),
                       retAvail = bit(19);
            const Oracle want = oracle(o, stage, follow, hasEntity, rigValid, retAvail);
            const RunFlags got = run_flags(o, stage, follow, retAvail);
            const SceneFlags scene = scene_flags(o, stage, follow, hasEntity, rigValid);
            const RunFlags& w = want.run;
            const bool same = got.frozenSelftest == w.frozenSelftest &&
                              is_frozen_selftest(o) == w.frozenSelftest &&
                              got.interactiveRun == w.interactiveRun &&
                              got.deathWired == w.deathWired &&
                              got.returnWired == w.returnWired && got.pumpLoad == w.pumpLoad &&
                              got.s8Wired == w.s8Wired && got.s9Wired == w.s9Wired &&
                              got.road == w.road && got.scriptedE2e == w.scriptedE2e &&
                              !got.scriptedE2e == want.notScriptedE2e &&
                              got.audioStrict == w.audioStrict &&
                              scene.audioWanted == want.scene.audioWanted &&
                              scene.valusBehavior == want.scene.valusBehavior;
            if (!same) {
                INFO("stage=" << static_cast<int>(stage) << " mask=" << m);
                FAIL("run_flags diverges from the oracle");
            }
            ++cases;
        }
    }
    CHECK(cases == 3u * (1u << kBits));  // 3,145,728
}
