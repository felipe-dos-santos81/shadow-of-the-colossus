#pragma once

#include <cstddef>
#include <vector>

#include "math3d.h"
#include "scene.h"

// A rasterized ground surface over the scene's XZ bounds: a regular grid of
// sample heights, one every `cellSize` metres. Grid node (i, j) sits at
//
//     (boundsMin.x + i * cellSize, ., boundsMin.z + j * cellSize)
//
// and its height lives at `heights[j * width + i]`. The grid carries
// ceil(extent / cellSize) + 1 nodes per axis, so node 0 is boundsMin and the
// last node reaches (or passes) boundsMax. A node no triangle covered holds
// NaN.
struct Heightmap {
    float cellSize;
    int width, height;
    std::vector<float> heights;
    Vec3 boundsMin, boundsMax;
};

// Rasterizes the scene's static meshes (`mesh.rig < 0` — the manifest's
// instances) into a heightmap of `cellSize` steps over the scene's XZ bounds,
// keeping each node's maximum Y. Vertices are taken in world space — each
// mesh's `model` matrix is applied, the same one the renderer draws with — so a
// composed scene's placed instances land where they stand. A triangle whose XZ
// footprint is a line (a vertical wall) covers no node and is skipped. Nodes no
// triangle covers stay NaN.
//
// Rigged meshes never rasterize: the player's own body (rig 0) must not become
// the ground it walks on, and a placed entity (rig 1..) must not raise it
// either. The grid's bounds are always the scene's own, rigged meshes included:
// the walkable area is unchanged.
//
// Throws std::runtime_error("no meshes") when the scene has no meshes at all,
// std::runtime_error("no static meshes") when every mesh is rigged (there is no
// ground to build), and std::runtime_error when `cellSize` is not positive and
// finite.
Heightmap build_heightmap(const Scene& scene, float cellSize);

// The ground height under `p`: `p` is clamped into the heightmap's XZ bounds
// first (a non-finite coordinate clamps to boundsMin, never a NaN index; Y is
// ignored). Where the four surrounding nodes are all valid, the result is
// their bilinear interpolation. Where any of them is NaN, the nearest valid
// node's value is used instead — the nearest node in the whole grid when the
// four are all empty — so a hole (or a spawn over one) falls back to the
// closest ground. Returns NaN only when no node is valid at all, and also for
// an empty heightmap (no grid: no ground), which never reads out of bounds.
float heightmap_sample(const Heightmap& heightmap, Vec3 p);

// True when `p`'s XZ lies inside the heightmap's bounds, edges included. Y is
// ignored: this is the walkable-area test.
bool heightmap_in_bounds(const Heightmap& heightmap, Vec3 p);
