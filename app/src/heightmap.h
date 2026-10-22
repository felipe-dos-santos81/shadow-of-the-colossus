#pragma once

#include <cstddef>
#include <cstdint>
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
//
// A *layered* map (S7: a building with floors, roofs and walls) also keeps, per
// node, up to `maxLayers` floor heights and a wall flag per floor. `heights`
// stays the node's maximum Y, so `heightmap_sample` answers the same thing on
// either kind of map; the arena is never layered.
inline constexpr int kHeightmapLayers = 8;
inline constexpr float kFloorStep = 0.6f;   // the most a move may step up
inline constexpr float kFloorDrop = 3.0f;   // the most a horizontal move may step down
// A wall blocks a floor layer when it overlaps [y + kBodyBandLow, y + kBodyBandHigh]
// (the body band above the layer); a triangle is a wall when |normal.y| is under
// kWallSteepness, a floor otherwise.
inline constexpr float kBodyBandLow = 0.6f;
inline constexpr float kBodyBandHigh = 1.8f;
inline constexpr float kWallSteepness = 0.5f;

struct Heightmap {
    float cellSize;
    int width, height;
    std::vector<float> heights;
    Vec3 boundsMin, boundsMax;
    int maxLayers = 0;                  // 0 = unlayered
    std::vector<float> layers;          // node-major, maxLayers per node, ascending, NaN-padded
    std::vector<uint8_t> blocked;       // same layout: 1 = a wall in that layer's body band
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
//
// With `layered`, the map also fills `layers` and `blocked` (kHeightmapLayers per
// node): every non-steep triangle (|normal.y| >= kWallSteepness) adds its node
// height to the node's ascending list (heights within 0.05 m merge, to the
// higher; when full the highest kHeightmapLayers are kept), and every steep
// triangle flags the layers whose body band it overlaps over the nodes within
// one cell of its XZ footprint's outline.
// ponytail: a steep slope's footprint interior wider than ~4 cells is not
// flagged; rasterize the interior if the shrine ever has one.
Heightmap build_heightmap(const Scene& scene, float cellSize, bool layered = false);

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

// The floor under `p` for a body whose feet are at `p.y`: per surrounding node
// the highest layer at or below `p.y + step` (the node's lowest layer when none
// is), blended and filled exactly as `heightmap_sample` does. On a map with
// `maxLayers == 0` this IS `heightmap_sample` (`p.y` and `step` are ignored).
float heightmap_floor(const Heightmap& heightmap, Vec3 p, float step);

// Whether a horizontal move `from` -> `to` is allowed: false when the target's
// floor layer (at the node nearest `to`) is flagged by a wall, rises more than
// kFloorStep above the source's floor, or lies more than kFloorDrop below it, or
// when the target has no floor at all while the source does. Always true on a
// map with `maxLayers == 0`.
bool heightmap_walkable(const Heightmap& heightmap, Vec3 from, Vec3 to);
