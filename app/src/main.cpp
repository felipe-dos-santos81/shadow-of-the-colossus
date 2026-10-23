#include "app.h"
#include "run_flags.h"

#include <SDL3/SDL.h>

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <system_error>

namespace {

void print_usage(FILE* out) {
    std::fprintf(out,
                 "usage: sotc-viewer [options]\n"
                 "\n"
                 "  --library DIR     converted asset library (default: $SOTC_LIB)\n"
                 "  --model REL       model to load, relative to the library\n"
                 "  --manifest PATH   compose the scene from a scene manifest; --model\n"
                 "                    is then the player's glb (W/A/S/D walk, Shift runs;\n"
                 "                    the clip follows: stand/walk/run)\n"
                 "  --anim NAME|IDX   clip to play, by name or 0-based index (default: the\n"
                 "                    model's first clip; procedural-wave only when a\n"
                 "                    skinned model carries no clips); in arena mode it is\n"
                 "                    the manual override of stand/walk/run selection\n"
                 "  --anim-time SEC   pause playback at SEC seconds (deterministic captures;\n"
                 "                    also pins the clip in arena mode)\n"
                 "  --entity-time SEC pin every manifest entity's clip to SEC seconds\n"
                 "                    (clamped into the clip; deterministic captures)\n"
                 "  --camera-target player|entity  initial framing for captures (entity\n"
                 "                    frames the entity's chest; requires --manifest)\n"
                 "  --screenshot PNG  write a PNG after --frames frames, then exit\n"
                 "  --frames N        frames the --screenshot path renders first (default 3)\n"
                 "  --width N         window width in points (default 1280)\n"
                 "  --height N        window height in points (default 720)\n"
                 "  --selftest        render one frame, print s1-status, exit\n"
                 "  --s3-selftest     compose --manifest, walk 240 fixed frames, print\n"
                 "                    s3-status, exit\n"
                 "  --s4-selftest     compose the encounter --manifest, pin the entity at\n"
                 "                    t=0, walk the player into its collision proxy, print\n"
                 "                    s4-status, exit\n"
                 "  --s41-selftest    compose the encounter --manifest, then grab, climb,\n"
                 "                    strike, catch, miss, force a stamina fall and land\n"
                 "                    the killing blow through fixed 1/60 s steps; print\n"
                 "                    s41-status, exit\n"
                 "  --s41-stills DIR  with --s41-selftest: write grab, mid-climb, strike,\n"
                 "                    reaction and defeat stills (plus controls) to DIR\n"
                 "  --s42-selftest    compose the encounter --manifest, then run the\n"
                 "                    weakpoint, gate, arm-route, stomp, club, death,\n"
                 "                    reset and stamina timeline through fixed 1/60 s\n"
                 "                    steps; print s42-status, exit\n"
                 "  --s42-stills DIR  with --s42-selftest: write the stomp telegraph\n"
                 "                    and impact, club, hurt, death and arm-weakpoint\n"
                 "                    stills to DIR\n"
                 "  --s5-selftest     run the same timeline with the curated audio on;\n"
                 "                    print s5-event/s5-status and capture the mix\n"
                 "                    (requires --audio-mixdown)\n"
                 "  --audio-mixdown PATH  the --s5-selftest WAV capture destination\n"
                 "  --s6-selftest     compose the encounter --manifest, hear a club\n"
                 "                    swing, drive a scripted lethal event, run the\n"
                 "                    death sequence (tendrils, corpse, cinematic\n"
                 "                    camera), print s6-status, soft-reset with R,\n"
                 "                    print s6-reset, exit\n"
                 "  --s6-stills DIR   with --s6-selftest: write the motion, camera,\n"
                 "                    tendrils and corpse stills to DIR\n"
                 "  --stage arena|shrine|plain  the first stage (default arena; the\n"
                 "                    shrine and the plain start with control; R\n"
                 "                    returns to the arena)\n"
                 "  --shrine-manifest PATH  the shrine's manifest (default\n"
                 "                    out/re/stage/shrine.manifest)\n"
                 "  --plain-manifest PATH  the plain's manifest (default\n"
                 "                    out/re/stage/plain.manifest)\n"
                 "  --road-manifest PATH  the road's manifest: the plain stage in\n"
                 "                    interactive play (default out/re/stage/road.manifest)\n"
                 "  --no-return       keep S6's end state: no return to the shrine\n"
                 "  --s7-selftest     seed Valus dead, run the return to the shrine, the\n"
                 "                    wake and the shrine probes, R back and reload;\n"
                 "                    print the s7-* lines, exit\n"
                 "  --s7-stills DIR   with --s7-selftest: write the fade, wake, altar,\n"
                 "                    idols and hall stills to DIR\n"
                 "  --s7-e2e          drive the interactive loop at fixed 1/60 s with\n"
                 "                    scripted input: one lethal hit, the death, the\n"
                 "                    return, the shrine load, the wake, Free, then R\n"
                 "                    back to the arena; print the s7e2e-* lines, exit\n"
                 "  --s8-selftest     on the plain: whistle, mount, kick through the\n"
                 "                    gaits, steer, stop, back up, dismount, the cliff\n"
                 "                    edge, R mounted; print the s8-* lines, exit\n"
                 "  --s8-stills DIR   with --s8-selftest: write the mount, gallop,\n"
                 "                    whistle and plain stills to DIR\n"
                 "  --s8-e2e          with --stage shrine: drive the interactive loop at\n"
                 "                    fixed 1/60 s with scripted input through the\n"
                 "                    shrine door, the plain, the whistle, the ride and\n"
                 "                    back, then R; print the s8e2e-* lines, exit\n"
                 "  --s9-selftest     on the road: raise the sword (the fan, the lock,\n"
                 "                    the shade, mounted at a stand), the entrance\n"
                 "                    stairs, the corridor and arena doors, Agro left\n"
                 "                    behind, R raised; print the s9-* lines, exit\n"
                 "  --s9-stills DIR   with --s9-selftest: write the fan, locked,\n"
                 "                    entrance and arena_door stills to DIR\n"
                 "  --s9-e2e          with --stage shrine: drive the interactive loop at\n"
                 "                    fixed 1/60 s with scripted input: the shrine door,\n"
                 "                    the ride to the stairs, the lock, up the stairs,\n"
                 "                    the corridor door, the lethal hit, the return;\n"
                 "                    print the s9e2e-* lines, exit\n");
}

bool parse_int(const char* text, int& out) {
    const char* const end = text + std::strlen(text);
    const auto [ptr, ec] = std::from_chars(text, end, out);
    return ec == std::errc() && ptr == end;
}

// Strict seconds parse for --anim-time: the whole string must be one finite
// number. std::strtof alone would accept trailing garbage and the "nan"/"inf"
// spellings, and a non-finite time has no meaning for playback.
bool parse_seconds(const char* text, float& out) {
    char* end = nullptr;
    const float value = std::strtof(text, &end);
    if (end == text || *end != '\0' || !std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

const char* library_from_env() {
    const char* value = std::getenv("SOTC_LIB");
    return (value != nullptr && value[0] != '\0') ? value : nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    AppOptions opts;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--selftest") {
            opts.selftest = true;
            continue;
        }
        if (arg == "--s3-selftest") {
            opts.s3Selftest = true;
            continue;
        }
        if (arg == "--s4-selftest") {
            opts.s4Selftest = true;
            continue;
        }
        if (arg == "--s41-selftest") {
            opts.s41Selftest = true;
            continue;
        }
        if (arg == "--s42-selftest") {
            opts.s42Selftest = true;
            continue;
        }
        if (arg == "--s5-selftest") {
            opts.s5Selftest = true;
            continue;
        }
        if (arg == "--s6-selftest") {
            opts.s6Selftest = true;
            continue;
        }
        if (arg == "--s7-selftest") {
            opts.s7Selftest = true;
            continue;
        }
        if (arg == "--s7-e2e") {
            opts.s7E2e = true;
            continue;
        }
        if (arg == "--s8-selftest") {
            opts.s8Selftest = true;
            continue;
        }
        if (arg == "--s8-e2e") {
            opts.s8E2e = true;
            continue;
        }
        if (arg == "--s9-selftest") {
            opts.s9Selftest = true;
            continue;
        }
        if (arg == "--s9-e2e") {
            opts.s9E2e = true;
            continue;
        }
        if (arg == "--s11-selftest") {
            opts.s11Selftest = true;
            continue;
        }
        if (arg == "--s11-e2e") {
            opts.s11E2e = true;
            continue;
        }
        if (arg == "--no-return") {
            opts.noReturn = true;
            continue;
        }

        const bool wants_value = arg == "--library" || arg == "--model" || arg == "--screenshot" ||
                                 arg == "--frames" || arg == "--width" || arg == "--height" ||
                                 arg == "--anim" || arg == "--anim-time" || arg == "--manifest" ||
                                 arg == "--entity-time" || arg == "--camera-target" ||
                                 arg == "--s41-stills" || arg == "--s42-stills" ||
                                 arg == "--s6-stills" || arg == "--audio-mixdown" ||
                                 arg == "--stage" || arg == "--shrine-manifest" ||
                                 arg == "--s7-stills" || arg == "--plain-manifest" ||
                                 arg == "--s8-stills" || arg == "--road-manifest" ||
                                 arg == "--s9-stills" || arg == "--s11-stills";
        if (!wants_value) {
            std::fprintf(stderr, "sotc-viewer: unknown argument: %s\n", arg.c_str());
            print_usage(stderr);
            return 2;
        }
        if (i + 1 >= argc) {
            std::fprintf(stderr, "sotc-viewer: %s requires a value\n", arg.c_str());
            print_usage(stderr);
            return 2;
        }
        const char* value = argv[++i];

        if (arg == "--library") {
            opts.libraryDir = value;
        } else if (arg == "--model") {
            opts.modelRelPath = value;
        } else if (arg == "--manifest") {
            opts.manifestPath = value;
        } else if (arg == "--screenshot") {
            opts.screenshotPath = value;
        } else if (arg == "--anim") {
            opts.animClip = value;
        } else if (arg == "--anim-time" || arg == "--entity-time") {
            float seconds = 0.0f;
            if (!parse_seconds(value, seconds)) {
                std::fprintf(stderr, "sotc-viewer: %s expects a finite number of seconds, got: %s\n",
                             arg.c_str(), value);
                print_usage(stderr);
                return 2;
            }
            if (arg == "--anim-time") {
                opts.animTime = seconds;
                opts.animTimeSet = true;
            } else {
                opts.entityTime = seconds;
                opts.entityTimeSet = true;
            }
        } else if (arg == "--camera-target") {
            if (std::strcmp(value, "player") != 0 && std::strcmp(value, "entity") != 0) {
                std::fprintf(stderr,
                             "sotc-viewer: --camera-target expects player or entity, got: %s\n",
                             value);
                print_usage(stderr);
                return 2;
            }
            opts.cameraTarget = value;
        } else if (arg == "--s41-stills") {
            opts.s41StillsDir = value;
        } else if (arg == "--s42-stills") {
            opts.s42StillsDir = value;
        } else if (arg == "--s6-stills") {
            opts.s6StillsPath = value;
        } else if (arg == "--audio-mixdown") {
            opts.audioMixdownPath = value;
        } else if (arg == "--stage") {
            if (std::strcmp(value, "arena") == 0) {
                opts.stage = StageId::Arena;
            } else if (std::strcmp(value, "shrine") == 0) {
                opts.stage = StageId::Shrine;
            } else if (std::strcmp(value, "plain") == 0) {
                opts.stage = StageId::Plain;
            } else {
                std::fprintf(stderr,
                             "sotc-viewer: --stage expects arena, shrine or plain, got: %s\n",
                             value);
                print_usage(stderr);
                return 2;
            }
        } else if (arg == "--shrine-manifest") {
            opts.shrineManifestPath = value;
        } else if (arg == "--plain-manifest") {
            opts.plainManifestPath = value;
        } else if (arg == "--s7-stills") {
            opts.s7StillsPath = value;
        } else if (arg == "--s8-stills") {
            opts.s8StillsPath = value;
        } else if (arg == "--road-manifest") {
            opts.roadManifestPath = value;
        } else if (arg == "--s9-stills") {
            opts.s9StillsPath = value;
        } else if (arg == "--s11-stills") {
            opts.s11StillsPath = value;
        } else {
            int parsed = 0;
            if (!parse_int(value, parsed) || parsed < 1) {
                std::fprintf(stderr, "sotc-viewer: %s expects a positive integer, got: %s\n",
                             arg.c_str(), value);
                print_usage(stderr);
                return 2;
            }
            if (arg == "--frames") {
                opts.frames = parsed;
            } else if (arg == "--width") {
                opts.width = parsed;
            } else {
                opts.height = parsed;
            }
        }
    }

    if (opts.libraryDir.empty()) {
        if (const char* env = library_from_env(); env != nullptr) {
            opts.libraryDir = env;
        }
    }
    if (opts.libraryDir.empty()) {
        std::fprintf(stderr, "sotc-viewer: no asset library: pass --library DIR or set SOTC_LIB\n");
        return 2;
    }

    // --s6-selftest composes the encounter manifest with the player's glb, so
    // it names both requirements itself (the generic --manifest rule below
    // would otherwise answer for --model with a manifest-generic message).
    if (opts.s6Selftest && opts.manifestPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --s6-selftest needs --manifest\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s6Selftest && opts.modelRelPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --s6-selftest needs --model (the player glb)\n");
        print_usage(stderr);
        return 2;
    }

    // --manifest composes the scene, so --model is the player's glb (required);
    // --s3-selftest only makes sense for a composed scene.
    if (!opts.manifestPath.empty() && opts.modelRelPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --manifest needs --model (the player glb)\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s3Selftest && opts.manifestPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --s3-selftest needs --manifest\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s4Selftest && opts.manifestPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --s4-selftest needs --manifest\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s41Selftest && opts.manifestPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --s41-selftest needs --manifest\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s42Selftest && opts.manifestPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --s42-selftest needs --manifest\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s5Selftest && opts.manifestPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --s5-selftest needs --manifest\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s5Selftest &&
        (opts.selftest || opts.s3Selftest || opts.s4Selftest || opts.s41Selftest ||
         opts.s42Selftest || opts.s6Selftest)) {
        std::fprintf(stderr,
                     "sotc-viewer: --s5-selftest cannot be combined with another selftest; "
                     "pass one\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s6Selftest &&
        (opts.selftest || opts.s3Selftest || opts.s4Selftest || opts.s41Selftest ||
         opts.s42Selftest || opts.s5Selftest)) {
        std::fprintf(stderr,
                     "sotc-viewer: --s6-selftest cannot be combined with another selftest; "
                     "pass one\n");
        print_usage(stderr);
        return 2;
    }
    // S7: the selftest needs the arena encounter and the player glb, runs
    // alone and starts in the arena; the shrine stage needs the arena's
    // --manifest too (R returns there) and is an interactive start.
    const bool frozen_selftest = is_frozen_selftest(opts);
    if (opts.s7Selftest && opts.manifestPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --s7-selftest needs --manifest\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s7Selftest && opts.modelRelPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --s7-selftest needs --model (the player glb)\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s7Selftest && frozen_selftest) {
        std::fprintf(stderr,
                     "sotc-viewer: --s7-selftest cannot be combined with another selftest; "
                     "pass one\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s7E2e && (opts.manifestPath.empty() || opts.modelRelPath.empty())) {
        std::fprintf(stderr, "sotc-viewer: --s7-e2e needs --manifest and --model\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s7E2e && (frozen_selftest || opts.s7Selftest || opts.s8E2e ||
                       opts.stage != StageId::Arena || opts.noReturn ||
                       !opts.screenshotPath.empty() || opts.entityTimeSet)) {
        std::fprintf(stderr,
                     "sotc-viewer: --s7-e2e runs alone: no selftest, capture, --entity-time, "
                     "--stage shrine|plain or --no-return\n");
        print_usage(stderr);
        return 2;
    }
    // S8 (D17): the selftest starts on the plain and runs alone; the e2e starts
    // in the shrine (--stage shrine) and is otherwise an interactive run.
    if ((opts.s8Selftest || opts.s8E2e) &&
        (opts.manifestPath.empty() || opts.modelRelPath.empty())) {
        std::fprintf(stderr, "sotc-viewer: --s8-selftest/--s8-e2e need --manifest and --model\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s8Selftest && (opts.selftest || opts.s3Selftest || opts.s4Selftest ||
                            opts.s41Selftest || opts.s42Selftest || opts.s5Selftest ||
                            opts.s6Selftest || opts.s7Selftest || opts.s7E2e || opts.s8E2e)) {
        std::fprintf(stderr,
                     "sotc-viewer: --s8-selftest cannot be combined with another selftest; "
                     "pass one\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s8E2e && (frozen_selftest || opts.s7Selftest || opts.stage != StageId::Shrine ||
                       opts.noReturn || !opts.screenshotPath.empty() || opts.entityTimeSet)) {
        std::fprintf(stderr,
                     "sotc-viewer: --s8-e2e runs alone and starts in the shrine: --stage shrine, "
                     "no selftest, capture, --entity-time or --no-return\n");
        print_usage(stderr);
        return 2;
    }
    // S9: the selftest starts on the road and runs alone; the e2e is an
    // interactive run that starts in the shrine (--stage shrine) with no
    // selftest, capture or --no-return.
    if ((opts.s9Selftest || opts.s9E2e) &&
        (opts.manifestPath.empty() || opts.modelRelPath.empty())) {
        std::fprintf(stderr, "sotc-viewer: --s9-selftest/--s9-e2e need --manifest and --model\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s9Selftest && (opts.selftest || opts.s3Selftest || opts.s4Selftest ||
                            opts.s41Selftest || opts.s42Selftest || opts.s5Selftest ||
                            opts.s6Selftest || opts.s7Selftest || opts.s7E2e || opts.s8Selftest ||
                            opts.s8E2e || opts.s9E2e)) {
        std::fprintf(stderr,
                     "sotc-viewer: --s9-selftest cannot be combined with another selftest; "
                     "pass one\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s9E2e && (frozen_selftest || opts.s7Selftest || opts.s7E2e || opts.s8E2e ||
                       opts.stage != StageId::Shrine || opts.noReturn ||
                       !opts.screenshotPath.empty() || opts.entityTimeSet)) {
        std::fprintf(stderr,
                     "sotc-viewer: --s9-e2e runs alone and starts in the shrine: --stage shrine, "
                     "no selftest, other e2e, capture, --entity-time or --no-return\n");
        print_usage(stderr);
        return 2;
    }
    if (!opts.s9StillsPath.empty() && !opts.s9Selftest) {
        std::fprintf(stderr, "sotc-viewer: --s9-stills needs --s9-selftest\n");
        print_usage(stderr);
        return 2;
    }
    if (!opts.s8StillsPath.empty() && !opts.s8Selftest) {
        std::fprintf(stderr, "sotc-viewer: --s8-stills needs --s8-selftest\n");
        print_usage(stderr);
        return 2;
    }
    if (!opts.s7StillsPath.empty() && !opts.s7Selftest) {
        std::fprintf(stderr, "sotc-viewer: --s7-stills needs --s7-selftest\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.stage != StageId::Arena &&
        (opts.manifestPath.empty() || frozen_selftest || opts.s7Selftest ||
         !opts.screenshotPath.empty() || opts.entityTimeSet)) {
        std::fprintf(stderr,
                     "sotc-viewer: --stage %s is an interactive start: it needs --manifest "
                     "(the arena R returns to) and no selftest or capture\n",
                     opts.stage == StageId::Shrine ? "shrine" : "plain");
        print_usage(stderr);
        return 2;
    }
    if (!opts.s6StillsPath.empty() && !opts.s6Selftest) {
        std::fprintf(stderr, "sotc-viewer: --s6-stills needs --s6-selftest\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.s5Selftest && opts.audioMixdownPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --s5-selftest needs --audio-mixdown PATH\n");
        print_usage(stderr);
        return 2;
    }
    if (!opts.audioMixdownPath.empty() && !opts.s5Selftest) {
        std::fprintf(stderr, "sotc-viewer: --audio-mixdown needs --s5-selftest\n");
        print_usage(stderr);
        return 2;
    }
    if (!opts.s41StillsDir.empty() && !opts.s41Selftest) {
        std::fprintf(stderr, "sotc-viewer: --s41-stills needs --s41-selftest\n");
        print_usage(stderr);
        return 2;
    }
    if (!opts.s42StillsDir.empty() && !opts.s42Selftest) {
        std::fprintf(stderr, "sotc-viewer: --s42-stills needs --s42-selftest\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.entityTimeSet && opts.manifestPath.empty()) {
        std::fprintf(stderr, "sotc-viewer: --entity-time needs --manifest (there is no entity)\n");
        print_usage(stderr);
        return 2;
    }
    if (opts.cameraTarget == "entity" && opts.manifestPath.empty()) {
        std::fprintf(stderr,
                     "sotc-viewer: --camera-target entity needs --manifest (there is no entity)\n");
        print_usage(stderr);
        return 2;
    }

    // S11 (Task 10): Quadratus's selftest and e2e run alone in the --manifest
    // arena (arena_02). These checks come last, so every earlier refusal's
    // output is unchanged (the frozen goldens hold them, usage text included).
    if ((opts.s11Selftest || opts.s11E2e) &&
        (opts.manifestPath.empty() || opts.modelRelPath.empty())) {
        std::fprintf(stderr, "sotc-viewer: --s11-selftest/--s11-e2e need --manifest and --model\n");
        return 2;
    }
    if (opts.s11Selftest &&
        (opts.selftest || opts.s3Selftest || opts.s4Selftest || opts.s41Selftest ||
         opts.s42Selftest || opts.s5Selftest || opts.s6Selftest || opts.s7Selftest ||
         opts.s7E2e || opts.s8Selftest || opts.s8E2e || opts.s9Selftest || opts.s9E2e ||
         opts.s11E2e || !opts.screenshotPath.empty() || opts.entityTimeSet ||
         opts.stage != StageId::Arena)) {
        std::fprintf(stderr,
                     "sotc-viewer: --s11-selftest cannot be combined with another selftest, "
                     "e2e, capture or --stage\n");
        return 2;
    }
    if (opts.s11E2e && (frozen_selftest || opts.s7Selftest || opts.s7E2e || opts.s8E2e ||
                        opts.s9E2e || opts.stage != StageId::Arena ||
                        !opts.screenshotPath.empty() || opts.entityTimeSet)) {
        std::fprintf(stderr,
                     "sotc-viewer: --s11-e2e runs alone in the --manifest arena: no selftest, "
                     "other e2e, capture, --entity-time or --stage\n");
        return 2;
    }
    if (!opts.s11StillsPath.empty() && !opts.s11Selftest) {
        std::fprintf(stderr, "sotc-viewer: --s11-stills needs --s11-selftest\n");
        return 2;
    }

    return run_app(opts);
}
