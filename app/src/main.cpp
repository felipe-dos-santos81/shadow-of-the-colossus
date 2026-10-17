#include "app.h"

#include <SDL3/SDL.h>

#include <charconv>
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
                 "  --screenshot PNG  write a PNG after --frames frames, then exit\n"
                 "  --frames N        frames the --screenshot path renders first (default 3)\n"
                 "  --width N         window width in points (default 1280)\n"
                 "  --height N        window height in points (default 720)\n"
                 "  --selftest        render one frame, print s1-status, exit\n");
}

bool parse_int(const char* text, int& out) {
    const char* const end = text + std::strlen(text);
    const auto [ptr, ec] = std::from_chars(text, end, out);
    return ec == std::errc() && ptr == end;
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

        const bool wants_value = arg == "--library" || arg == "--model" || arg == "--screenshot" ||
                                 arg == "--frames" || arg == "--width" || arg == "--height";
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
        } else if (arg == "--screenshot") {
            opts.screenshotPath = value;
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

    return run_app(opts);
}
