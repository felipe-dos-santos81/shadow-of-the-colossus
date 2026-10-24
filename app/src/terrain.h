#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math3d.h"

// Terrain classes and owners under a point (S13 design D8 and ruling §13.2, Task 10): the
// `sotc-terrain/0` export (docs/notes/s13-schemas.md; glb frame, game-mask values). Pure.

// A loaded `sotc-terrain/0` document (Task 10).
struct TerrainMap {
    struct Tri {
        float v[3][3];
        int cls;
        uint32_t owner;
    };
    // A 2-D bucket grid (kCell m cells) over one triangle list: cell -> triangle indices.
    struct Grid {
        float minX = 0.0f, minZ = 0.0f;
        int nx = 0, nz = 0;
        std::vector<std::vector<uint32_t>> cells;
    };
    static constexpr float kCell = 4.0f;  // authored: the brief's bucket size
    std::vector<Tri> stage, boss;
    Grid stageGrid, bossGrid;
};

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
