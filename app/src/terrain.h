#pragma once

#include <cstdint>
#include <string>

#include "math3d.h"

// Terrain classes and owners under a point (S13 design D8 and ruling §13.2, Task 10): the
// `sotc-terrain/0` export (docs/notes/s13-schemas.md; glb frame, game-mask values). Pure.

// A loaded `sotc-terrain/0` document (Task 10).
struct TerrainMap {};

struct TerrainHit {
    bool found;
    int cls;
    uint32_t owner;
    float y;
};

// Parses `json` into `out`; false with `error` naming the culprit. S13 Task 10.
bool terrain_load(const std::string& json, TerrainMap& out, std::string& error);

// The highest exported stage triangle whose surface under p lies in [p.y − 1.0, p.y + 0.5] m;
// else not found, class 0. S13 Task 10.
TerrainHit terrain_at(const TerrainMap& map, Vec3 p);

// The highest boss-ground triangle at or below p.y + 2.0 m; `y` its height. S13 Task 10.
TerrainHit terrain_boss_ground(const TerrainMap& map, Vec3 p);
