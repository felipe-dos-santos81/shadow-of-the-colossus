#include "manifest.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

// Every parse failure names the file and the line, e.g.
// `out/re/stage/valus.manifest:3: unknown directive 'foo'`.
[[noreturn]] void fail(const std::filesystem::path& path, int line, const std::string& reason) {
    throw std::runtime_error(path.string() + ":" + std::to_string(line) + ": " + reason);
}

// Splits a line into whitespace-separated tokens. A `#` starts a comment that
// runs to the end of the line, so a comment-only line yields no tokens and is
// skipped like a blank line. `\r` counts as whitespace, so CRLF files parse.
std::vector<std::string> split_tokens(const std::string& line) {
    std::vector<std::string> tokens;
    std::string token;
    for (const char character : line) {
        if (character == '#') {
            break;
        }
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            if (!token.empty()) {
                tokens.push_back(std::move(token));
                token.clear();
            }
        } else {
            token.push_back(character);
        }
    }
    if (!token.empty()) {
        tokens.push_back(std::move(token));
    }
    return tokens;
}

// Strict float: the whole token must be consumed and the value finite, so
// "nan", "inf", "1.5x" and "1e999" are all refused.
bool parse_float_token(const std::string& token, float& out) {
    const char* const begin = token.c_str();
    char* end = nullptr;
    const float value = std::strtof(begin, &end);
    if (end != begin + token.size() || !std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

// The directives' numeric fields, in schema order (for error messages). An
// entity places with the same ten numbers as an instance, then names its clip.
constexpr const char* kPlacementFields[] = {"px", "py", "pz", "qx", "qy",
                                            "qz", "qw", "sx", "sy", "sz"};
constexpr const char* kSpawnFields[] = {"px", "py", "pz", "yawDeg"};

// Parses the placement directives' shared ten-number tail starting at
// `tokens[first]`, or fails naming the directive and field.
void parse_placement_tokens(const std::filesystem::path& path, int line,
                            const std::vector<std::string>& tokens, std::size_t first,
                            const std::string& directive, ManifestInstance& out) {
    float values[10] = {};
    for (int i = 0; i < 10; ++i) {
        if (!parse_float_token(tokens[first + static_cast<std::size_t>(i)], values[i])) {
            fail(path, line, directive + ": invalid number '" +
                                 tokens[first + static_cast<std::size_t>(i)] + "' for " +
                                 kPlacementFields[i]);
        }
    }
    out.position = Vec3{values[0], values[1], values[2]};
    out.rotation = Quat{values[3], values[4], values[5], values[6]};
    out.scale = Vec3{values[7], values[8], values[9]};
}

}  // namespace

SceneManifest parse_manifest(const std::filesystem::path& path) {
    // A missing path and a directory both open-or-not ambiguously: reading a
    // directory succeeds and yields no lines, so guard on the file type first
    // and never return an empty manifest for one.
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(path, file_error)) {
        throw std::runtime_error(path.string() + ": cannot open manifest file");
    }
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error(path.string() + ": cannot open manifest file");
    }

    SceneManifest manifest{};
    manifest.spawnPosition = Vec3{0.0f, 0.0f, 0.0f};
    manifest.spawnYawDeg = 0.0f;
    manifest.hasSpawn = false;

    std::string line;
    int line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        const std::vector<std::string> tokens = split_tokens(line);
        if (tokens.empty()) {
            continue;  // a blank line or a comment
        }

        const std::string& directive = tokens[0];
        if (directive == "instance") {
            if (tokens.size() != 12) {
                fail(path, line_number, "instance expects 12 tokens (glb path + 10 numbers), got " +
                                            std::to_string(tokens.size()));
            }
            ManifestInstance instance{};
            instance.glbRelPath = tokens[1];
            parse_placement_tokens(path, line_number, tokens, 2, "instance", instance);
            manifest.instances.push_back(std::move(instance));
        } else if (directive == "entity") {
            if (tokens.size() != 13) {
                fail(path, line_number,
                     "entity expects 13 tokens (glb path + 10 numbers + clip), got " +
                         std::to_string(tokens.size()));
            }
            ManifestEntity entity{};
            entity.placement.glbRelPath = tokens[1];
            parse_placement_tokens(path, line_number, tokens, 2, "entity", entity.placement);
            entity.clip = tokens[12];
            manifest.entities.push_back(std::move(entity));
        } else if (directive == "spawn") {
            if (tokens.size() != 5) {
                fail(path, line_number, "spawn expects 5 tokens (3 position + yaw), got " +
                                            std::to_string(tokens.size()));
            }
            float values[4] = {};
            for (int i = 0; i < 4; ++i) {
                if (!parse_float_token(tokens[1 + i], values[i])) {
                    fail(path, line_number, "spawn: invalid number '" + tokens[1 + i] +
                                                "' for " + kSpawnFields[i]);
                }
            }
            manifest.spawnPosition = Vec3{values[0], values[1], values[2]};
            manifest.spawnYawDeg = values[3];
            manifest.hasSpawn = true;
        } else {
            fail(path, line_number, "unknown directive '" + directive + "'");
        }
    }
    if (in.bad()) {
        throw std::runtime_error(path.string() + ": read error");
    }
    return manifest;
}
