#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "math3d.h"

// One placed model from a scene manifest: the library-relative glb path and
// the TRS the composer bakes into every mesh it contributes. `rotation` is the
// glTF quaternion layout (x, y, z, w), like Quat itself.
struct ManifestInstance {
    std::string glbRelPath;
    Vec3 position;
    Quat rotation;
    Vec3 scale;
};

// A parsed scene manifest — the W1↔W2 interface documented in
// `docs/notes/scene-manifest.md`. `hasSpawn` is false when the file carries no
// `spawn` directive; `spawnPosition` is then the zero vector (and
// `spawnYawDeg` 0), and the caller picks its fallback.
struct SceneManifest {
    std::vector<ManifestInstance> instances;
    Vec3 spawnPosition;
    float spawnYawDeg;
    bool hasSpawn;
};

// Parses a scene manifest. The format is line-oriented: blank lines are
// skipped and a `#` starts a comment that runs to the end of the line.
// Directives (tokens separated by whitespace):
//   instance <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz>
//   spawn <px> <py> <pz> <yawDeg>
// Every number must be a strict, full-string finite float: the whole token has
// to parse and be finite ("nan", "inf", "1.5x" and "1e999" are refused).
//
// Throws std::runtime_error formatted `<file>:<line>: <reason>` for a
// malformed line (wrong token count, invalid number, unknown directive), and
// `<file>: <reason>` when the file cannot be read as a manifest.
SceneManifest parse_manifest(const std::filesystem::path& path);
