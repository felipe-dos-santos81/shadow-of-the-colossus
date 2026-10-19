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
    // --manifest: compose the scene from a scene manifest; modelRelPath is then
    // the player's glb (required), the ground heightmap is built from the
    // player-less part of the scene, and the interactive loop drives the player
    // controller with the follow camera.
    std::string manifestPath;
    // --s3-selftest: compose, spawn the player (manifest spawn or the bounds
    // centre), simulate 240 fixed 1/60 s frames of forward+run, render one
    // frame, print `s3-status ...`, and exit 0. Requires --manifest and --model.
    bool s3Selftest = false;
};

// Opens the windowed SDL3 shell and runs it until the user quits (Escape or the
// window close button). With opts.selftest, runs exactly one frame, prints the
// `s1-status` line to stdout, and returns.
//
// Returns 0 on success, 1 on a runtime (SDL/GPU) failure, 2 on usage errors
// (including an --anim that names no clip of the loaded model).
int run_app(const AppOptions& opts);
