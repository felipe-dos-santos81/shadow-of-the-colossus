#include "app.h"

#include "audio_seam.h"
#include "capture.h"
#include "interactive.h"
#include "renderer.h"
#include "selftests.h"
#include "stage_ctx.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <exception>
#include <optional>

// run_app (S10 Task 6): the options' checks, the SDL shell, and the stage loop —
// stage_setup composes a stage, run_selftests runs a scripted path, and
// run_interactive plays it. The paths live in selftest_*.cpp and interactive.cpp.
int run_app(const AppOptions& opts) {
    if (opts.libraryDir.empty()) {
        std::fprintf(stderr, "sotc-viewer: no asset library: pass --library DIR or set SOTC_LIB\n");
        return 2;
    }
    if (!opts.screenshotPath.empty() && opts.modelRelPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --screenshot needs --model: there is nothing to capture\n");
        return 2;
    }
    // Manifest mode: --model names the player's glb (required); --s3-selftest
    // only makes sense for a composed scene.
    if (!opts.manifestPath.empty() && opts.modelRelPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --manifest needs --model (the player glb)\n");
        return 2;
    }
    if (opts.s3Selftest && opts.manifestPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --s3-selftest needs --manifest\n");
        return 2;
    }
    // --entity-time pins an entity's clip; entities only exist in a composed
    // manifest scene, so the flag without one is a usage error, not a no-op.
    if (opts.entityTimeSet && opts.manifestPath.empty()) {
        std::fprintf(stderr,
                     "sotc-viewer: --entity-time needs --manifest (there is no entity to pin)\n");
        return 2;
    }
    // --camera-target entity needs a composed scene with an entity to frame.
    if (opts.cameraTarget != "player" && opts.cameraTarget != "entity") {
        std::fprintf(stderr, "sotc-viewer: --camera-target expects player or entity, got: %s\n",
                     opts.cameraTarget.c_str());
        return 2;
    }
    if (opts.cameraTarget == "entity" && opts.manifestPath.empty()) {
        std::fprintf(stderr,
                     "sotc-viewer: --camera-target entity needs --manifest (there is no entity)\n");
        return 2;
    }

    // Cursor policy, part 1: SDL must never auto-capture the mouse when the
    // window gains focus. Set the hint before SDL_Init so the video subsystem
    // starts with it in place.
    if (!SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0")) {
        std::fprintf(stderr, "sotc-viewer: could not set %s=0\n", SDL_HINT_MOUSE_AUTO_CAPTURE);
        return 1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "sotc-viewer: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    Shell shell;

    shell.win = SDL_CreateWindow("sotc-viewer", opts.width, opts.height,
                                 SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (shell.win == nullptr) {
        std::fprintf(stderr, "sotc-viewer: SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }

    // Cursor policy, part 2: the cursor is never captured — state both
    // guarantees explicitly, and fail if the platform refuses to honor them.
    if (!SDL_SetWindowRelativeMouseMode(shell.win, false) ||
        !SDL_SetWindowMouseGrab(shell.win, false)) {
        std::fprintf(stderr, "sotc-viewer: cursor policy: %s\n", SDL_GetError());
        return 1;
    }

    // MSL sources are compiled by SDL at runtime: the build machine has no
    // Xcode Metal toolchain, so no metallib is produced here (plan ruling R2).
    shell.device =
        SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, /*debug=*/false, /*name=*/nullptr);
    if (shell.device == nullptr) {
        std::fprintf(stderr, "sotc-viewer: SDL_CreateGPUDevice: %s\n", SDL_GetError());
        return 1;
    }

    if (!SDL_ClaimWindowForGPUDevice(shell.device, shell.win)) {
        std::fprintf(stderr, "sotc-viewer: SDL_ClaimWindowForGPUDevice: %s\n", SDL_GetError());
        return 1;
    }
    shell.window_claimed = true;

    if (!SDL_SetGPUSwapchainParameters(shell.device, shell.win,
                                       SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC)) {
        std::fprintf(stderr, "sotc-viewer: SDL_SetGPUSwapchainParameters: %s\n", SDL_GetError());
        return 1;
    }

    try {
        // Constructing the renderer compiles the MSL shader (loudly failing
        // when mesh.metal is missing or does not compile).
        Renderer renderer(shell.device, shell.win);
        std::printf("s1-gpu format=%s\n",
                    texture_format_name(SDL_GetGPUSwapchainTextureFormat(shell.device, shell.win)));
        // The S5 audio engine lives for the run; each stage decides what it loads.
        AudioSeam audio;

        RunState run{opts, shell, renderer, audio};
        // --s8-selftest starts on the plain (D17); --s8-e2e passes --stage shrine.
        // --s9-selftest starts on the plain too (the road).
        run.stage = (opts.s8Selftest || opts.s9Selftest) ? StageId::Plain : opts.stage;
        if (run.stage != StageId::Arena) {
            // --stage shrine|plain starts with control (D11): the return is over.
            run.carry.ret.phase = ReturnPhase::Free;
        }
        StageId& stage = run.stage;
        for (;;) {
            StageCtx ctx(run);
            if (const std::optional<int> exitCode = stage_setup(run, ctx)) {
                return *exitCode;
            }

            // The scripted paths (selftests.h): each ends the run or names the
            // next stage; none of them asked = the interactive loop below.
            const SelftestResult scripted = run_selftests(ctx, run);
            if (scripted.exitCode) {
                return *scripted.exitCode;
            }
            if (scripted.next) {
                stage = *scripted.next;
                continue;
            }
            const SelftestResult played = run_interactive(ctx, run);
            if (played.exitCode) {
                return *played.exitCode;
            }
            // run_interactive's contract: an exit code or the next stage.
            if (!played.next) {
                std::fprintf(stderr, "sotc-viewer: internal error: the interactive loop "
                                     "ended with neither an exit code nor a next stage\n");
                return 1;
            }
            stage = *played.next;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "sotc-viewer: %s\n", error.what());
        return 1;
    }
}
