#include "run_flags.h"

RunFlags run_flags(const AppOptions& opts, StageId stage, bool followMode, bool hasEntity,
                   bool entityRigValid, bool returnAvailable) {
    // The scripted timelines that pin the entity and own every step.
    const bool pinnedTimeline = opts.s3Selftest || opts.s4Selftest || opts.s41Selftest ||
                                opts.s42Selftest || opts.s5Selftest;
    const bool capture = opts.entityTimeSet || !opts.screenshotPath.empty();
    RunFlags f;
    // S7: the selftests that never switch stages (s7 is the one that does).
    f.frozenSelftest = opts.selftest || pinnedTimeline || opts.s6Selftest || opts.s8Selftest ||
                       opts.s9Selftest;
    // Interactive play: no selftest, no capture (the e2e runs are interactive).
    f.interactiveRun = !f.frozenSelftest && !opts.s7Selftest && !capture;
    // S6: the death sequence — interactive play and the s6/s7 selftests; never
    // a frozen timeline or a capture.
    f.deathWired = opts.s6Selftest || (!opts.selftest && !pinnedTimeline && !opts.s8Selftest &&
                                       !opts.s9Selftest && !capture);
    // S5/S8 D15: sound where there is an entity (or the plain).
    f.audioWanted = followMode && (hasEntity || stage == StageId::Plain) &&
                    (opts.s5Selftest || opts.s6Selftest ||
                     (!opts.selftest && !pinnedTimeline && !opts.entityTimeSet));
    // Ruling R8: Valus's autonomous fight.
    f.valusBehavior =
        followMode && hasEntity && entityRigValid && !pinnedTimeline && !opts.entityTimeSet;
    // S7: the return to the shrine (R16 clears returnAvailable).
    f.returnWired = followMode && returnAvailable &&
                    (opts.s7Selftest || (f.interactiveRun && !opts.noReturn));
    // S7: only the S7+ runs pump events during a load.
    f.pumpLoad = opts.s7Selftest || opts.s8Selftest || opts.s9Selftest || f.interactiveRun;
    // S8: the plain's doors and ride; S9: the road, the corridor and arena doors.
    f.s8Wired = (f.interactiveRun && !opts.s7E2e) || opts.s8Selftest || opts.s9Selftest;
    f.s9Wired = (f.interactiveRun && !opts.s7E2e && !opts.s8E2e) || opts.s9Selftest || opts.s9E2e;
    f.road = f.s9Wired && stage == StageId::Plain;
    return f;
}
