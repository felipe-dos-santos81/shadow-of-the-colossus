#pragma once

#include "app.h"

// The run's derived mode flags (S10 Task 5, design §13.8): one place for the
// expressions app.cpp/main.cpp used to spell inline. Pure.
struct RunFlags {
    bool frozenSelftest = false;
    bool interactiveRun = false;
    bool deathWired = false;
    bool audioWanted = false;
    bool valusBehavior = false;
    bool returnWired = false;
    bool pumpLoad = false;
    bool s8Wired = false;
    bool s9Wired = false;
    bool road = false;
};

RunFlags run_flags(const AppOptions& opts, StageId stage, bool followMode, bool hasEntity,
                   bool entityRigValid, bool returnAvailable);
