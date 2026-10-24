#include "terrain.h"

// S13 Task 1 stubs: Task 10 implements the lookup.

bool terrain_load(const std::string&, TerrainMap&, std::string& error) {
    error = "terrain_load: not implemented (S13 Task 10)";
    return false;
}

TerrainHit terrain_at(const TerrainMap&, Vec3) { return {false, 0, 0, 0.0f}; }  // S13 Task 10

TerrainHit terrain_boss_ground(const TerrainMap&, Vec3) { return {false, 0, 0, 0.0f}; }  // S13 Task 10
