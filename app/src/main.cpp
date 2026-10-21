#include "app.h"

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
                 "  --audio-mixdown PATH  the --s5-selftest WAV capture destination\n");
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

        const bool wants_value = arg == "--library" || arg == "--model" || arg == "--screenshot" ||
                                 arg == "--frames" || arg == "--width" || arg == "--height" ||
                                 arg == "--anim" || arg == "--anim-time" || arg == "--manifest" ||
                                 arg == "--entity-time" || arg == "--camera-target" ||
                                 arg == "--s41-stills" || arg == "--s42-stills" ||
                                 arg == "--audio-mixdown";
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
        } else if (arg == "--audio-mixdown") {
            opts.audioMixdownPath = value;
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
    if (opts.s5Selftest && opts.s42Selftest) {
        std::fprintf(stderr,
                     "sotc-viewer: --s5-selftest and --s42-selftest run the same timeline; "
                     "pass one\n");
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

    return run_app(opts);
}
