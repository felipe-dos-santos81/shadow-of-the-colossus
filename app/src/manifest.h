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

// One placed, animated entity from a scene manifest: a placement (the same
// glb + TRS shape as an instance's) plus the exact name of the clip its own rig
// loops. Unlike an `instance`, an entity keeps its skeleton and clips.
struct ManifestEntity {
    ManifestInstance placement;
    std::string clip;
};

// One placed horse (Agro): a placement like an entity's, no clip. The composer
// keeps its skeleton and clips as its own rig (`Scene::horseRig`) but it is
// never an encounter entity; the app drives it.
struct ManifestHorse {
    ManifestInstance placement;
};

// One placed, posed static prop: a skinned glb sampled once at `seconds` of
// `clip` (clamped into the clip, like a pinned clock) and CPU-skinned at compose
// time into static meshes — no rig, no playback. Mono's lying pose is one.
struct ManifestPosed {
    std::string glb;
    Vec3 position;
    Quat rotation;
    Vec3 scale;
    std::string clip;
    float seconds;
};

// A parsed scene manifest — the W1↔W2 interface documented in
// `docs/notes/scene-manifest.md`. `hasSpawn` is false when the file carries no
// `spawn` directive; `spawnPosition` is then the zero vector (and
// `spawnYawDeg` 0), and the caller picks its fallback.
struct SceneManifest {
    std::vector<ManifestInstance> instances;
    std::vector<ManifestEntity> entities;
    std::vector<ManifestPosed> posed;
    // Drawn but never ground (`MeshCpu::ground` false, outside the scene bounds).
    std::vector<ManifestInstance> backdrops;
    std::vector<ManifestHorse> horses;
    Vec3 spawnPosition;
    float spawnYawDeg;
    bool hasSpawn;
};

// Parses a scene manifest. The format is line-oriented: blank lines are
// skipped and a `#` starts a comment that runs to the end of the line.
// Directives (tokens separated by whitespace):
//   instance <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz>
//   entity <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz> <clipName>
//   posed <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz> <clipName> <seconds>
//   backdrop <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz>
//   horse <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz>
//   spawn <px> <py> <pz> <yawDeg>
// Every number must be a strict, full-string finite float: the whole token has
// to parse and be finite ("nan", "inf", "1.5x" and "1e999" are refused). The
// entity's clip is a non-numeric token: it names the clip exactly, and the
// loader is what decides whether the model carries it. A posed `seconds` is a
// finite float >= 0.
//
// Throws std::runtime_error formatted `<file>:<line>: <reason>` for a
// malformed line (wrong token count, invalid number, unknown directive), and
// `<file>: <reason>` when the file cannot be read as a manifest.
SceneManifest parse_manifest(const std::filesystem::path& path);
