#pragma once

#include "app.h"

// The run's derived mode flags (S10 Task 5, design §13.8): one place for the
// expressions app.cpp/main.cpp used to spell inline. Pure. Split by when the
// inputs exist: RunFlags before the stage's manifest loads, SceneFlags after.
struct RunFlags {
    bool frozenSelftest = false;
    bool interactiveRun = false;
    bool deathWired = false;
    bool returnWired = false;
    bool pumpLoad = false;
    bool s8Wired = false;
    bool s9Wired = false;
    bool road = false;
    bool scriptedE2e = false;  // --s7-e2e, --s8-e2e or --s9-e2e
    bool audioStrict = false;  // --s5/--s6-selftest: the sound is the artifact
};

// The flags that read the loaded scene (the entity, its rig).
struct SceneFlags {
    bool audioWanted = false;
    bool valusBehavior = false;
};

// The selftests that never switch stages (the frozen regressions).
bool is_frozen_selftest(const AppOptions& opts);

RunFlags run_flags(const AppOptions& opts, StageId stage, bool followMode, bool returnAvailable);

SceneFlags scene_flags(const AppOptions& opts, StageId stage, bool followMode, bool hasEntity,
                       bool entityRigValid);
