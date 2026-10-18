#pragma once

#include <string>

// One viewer run's parameters, filled by the argument parser in main.cpp.
struct AppOptions {
    std::string libraryDir;      // converted asset library (--library, or $SOTC_LIB)
    std::string modelRelPath;    // model to load, relative to libraryDir (--model)
    std::string screenshotPath;  // PNG destination; empty = no screenshot (--screenshot)
    int frames = 3;              // frames the --screenshot path renders first (--frames)
    bool selftest = false;       // --selftest: render one frame, print s1-status, exit
    int width = 1280;            // window width in points (--width)
    int height = 720;            // window height in points (--height)
    std::string animClip;        // clip to play, by name or 0-based index (--anim)
    float animTime = 0.0f;       // fixed playback time in seconds (--anim-time)
    bool animTimeSet = false;    // --anim-time was given: start paused at animTime
};

// Opens the windowed SDL3 shell and runs it until the user quits (Escape or the
// window close button). With opts.selftest, runs exactly one frame, prints the
// `s1-status` line to stdout, and returns.
//
// Returns 0 on success, 1 on a runtime (SDL/GPU) failure, 2 on usage errors
// (including an --anim that names no clip of the loaded model).
int run_app(const AppOptions& opts);
