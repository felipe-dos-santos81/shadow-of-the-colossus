#include "heightmap.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace {

// A point through a column-major matrix (w = 1) — the same transform the loader
// bakes a scene's world-space bounds with.
Vec3 transform_point(const Mat4& matrix, Vec3 point) {
    return Vec3{
        matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12],
        matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13],
        matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14]};
}

bool is_finite(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// The 2D cross product in XZ: positive when c lies left of a -> b. Its
// magnitude is twice the triangle's XZ area, which is the rasterization's
// degenerate case (a vertical wall projects onto a line).
float cross_xz(Vec3 a, Vec3 b, Vec3 c) {
    return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
}

// One axis' node count: ceil(extent / cellSize) + 1, so the last node reaches
// (or passes) the far bound. A degenerate or non-finite extent collapses to a
// single node instead of a bad index.
int axis_nodes(float extent, float cellSize) {
    if (!std::isfinite(extent) || extent <= 0.0f) {
        return 1;
    }
    const float nodes = std::ceil(extent / cellSize) + 1.0f;
    if (!std::isfinite(nodes) || nodes < 1.0f) {
        return 1;
    }
    return static_cast<int>(nodes);
}

// Clamp one coordinate into [lo, hi]; NaN lands on lo instead of propagating
// into the index arithmetic.
float clamp_coordinate(float value, float lo, float hi) {
    if (!(value >= lo)) {
        return lo;
    }
    return value > hi ? hi : value;
}

// The squared XZ distance from `point` to grid node (i, j).
float node_distance_squared(const Heightmap& heightmap, int i, int j, float x, float z) {
    const float dx = heightmap.boundsMin.x + static_cast<float>(i) * heightmap.cellSize - x;
    const float dz = heightmap.boundsMin.z + static_cast<float>(j) * heightmap.cellSize - z;
    return dx * dx + dz * dz;
}

// An index clamped into [0, nodes - 1] before its int cast: a far-away — or
// NaN-bounds — value must not reach an undefined float-to-int conversion.
int clamp_index(float index, int nodes) {
    if (!(index >= 0.0f)) {
        return 0;
    }
    const float upper = static_cast<float>(nodes - 1);
    return static_cast<int>(index > upper ? upper : index);
}

// The grid node a coordinate's floor (`begin`) or ceil (`end`) lands on.
int node_index_begin(float coordinate, float bound_min, float cellSize, int nodes) {
    return clamp_index(std::floor((coordinate - bound_min) / cellSize), nodes);
}

int node_index_end(float coordinate, float bound_min, float cellSize, int nodes) {
    return clamp_index(std::ceil((coordinate - bound_min) / cellSize), nodes);
}

}  // namespace

Heightmap build_heightmap(const Scene& scene, float cellSize, std::size_t meshCount) {
    if (!(cellSize > 0.0f) || !std::isfinite(cellSize)) {
        throw std::runtime_error("heightmap cell size must be positive and finite");
    }
    const std::size_t rasterized = std::min(meshCount, scene.meshes.size());
    if (rasterized == 0) {
        throw std::runtime_error("no meshes");
    }

    Heightmap heightmap;
    heightmap.cellSize = cellSize;
    heightmap.boundsMin = scene.boundsMin;
    heightmap.boundsMax = scene.boundsMax;
    heightmap.width = axis_nodes(heightmap.boundsMax.x - heightmap.boundsMin.x, cellSize);
    heightmap.height = axis_nodes(heightmap.boundsMax.z - heightmap.boundsMin.z, cellSize);
    heightmap.heights.assign(
        static_cast<std::size_t>(heightmap.width) * static_cast<std::size_t>(heightmap.height),
        std::numeric_limits<float>::quiet_NaN());

    for (std::size_t mesh_index = 0; mesh_index < rasterized; ++mesh_index) {
        const MeshCpu& mesh = scene.meshes[mesh_index];
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const std::size_t i0 = mesh.indices[t];
            const std::size_t i1 = mesh.indices[t + 1];
            const std::size_t i2 = mesh.indices[t + 2];
            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() ||
                i2 >= mesh.vertices.size()) {
                continue;  // a malformed index would read out of bounds
            }
            const Vec3 p0 = transform_point(
                mesh.model, Vec3{mesh.vertices[i0].px, mesh.vertices[i0].py, mesh.vertices[i0].pz});
            const Vec3 p1 = transform_point(
                mesh.model, Vec3{mesh.vertices[i1].px, mesh.vertices[i1].py, mesh.vertices[i1].pz});
            const Vec3 p2 = transform_point(
                mesh.model, Vec3{mesh.vertices[i2].px, mesh.vertices[i2].py, mesh.vertices[i2].pz});
            if (!is_finite(p0) || !is_finite(p1) || !is_finite(p2)) {
                continue;  // a non-finite vertex would poison the index arithmetic
            }

            const float area = cross_xz(p0, p1, p2);
            if (area == 0.0f) {
                continue;  // a vertical wall: no XZ footprint, no ground height
            }

            // The node range the triangle's XZ bounding box can touch.
            const float min_x = std::min({p0.x, p1.x, p2.x});
            const float max_x = std::max({p0.x, p1.x, p2.x});
            const float min_z = std::min({p0.z, p1.z, p2.z});
            const float max_z = std::max({p0.z, p1.z, p2.z});
            const int i_min =
                node_index_begin(min_x, heightmap.boundsMin.x, cellSize, heightmap.width);
            const int i_max = node_index_end(max_x, heightmap.boundsMin.x, cellSize, heightmap.width);
            const int j_min =
                node_index_begin(min_z, heightmap.boundsMin.z, cellSize, heightmap.height);
            const int j_max =
                node_index_end(max_z, heightmap.boundsMin.z, cellSize, heightmap.height);

            for (int j = j_min; j <= j_max; ++j) {
                for (int i = i_min; i <= i_max; ++i) {
                    const Vec3 node{heightmap.boundsMin.x + static_cast<float>(i) * cellSize, 0.0f,
                                    heightmap.boundsMin.z + static_cast<float>(j) * cellSize};

                    // Barycentric coverage, edges included: a node on a shared
                    // edge is claimed by every triangle that touches it, which
                    // the max-Y rule resolves.
                    const float e0 = cross_xz(p1, p2, node);
                    const float e1 = cross_xz(p2, p0, node);
                    const float e2 = cross_xz(p0, p1, node);
                    const bool inside = area > 0.0f ? (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f)
                                                    : (e0 <= 0.0f && e1 <= 0.0f && e2 <= 0.0f);
                    if (!inside) {
                        continue;
                    }

                    // λ0 = e0/area is p0's barycentric weight, and so on: the
                    // sub-triangle opposite a vertex is the one the other two
                    // edges form.
                    const float y = (e0 * p0.y + e1 * p1.y + e2 * p2.y) / area;
                    float& cell =
                        heightmap.heights[static_cast<std::size_t>(j) * heightmap.width + i];
                    if (std::isnan(cell) || y > cell) {
                        cell = y;
                    }
                }
            }
        }
    }
    return heightmap;
}

float heightmap_sample(const Heightmap& heightmap, Vec3 p) {
    // An empty heightmap (no grid at all — e.g. a scene with no ground) has no
    // ground to report; never index into an empty grid.
    if (heightmap.width <= 0 || heightmap.height <= 0 ||
        heightmap.heights.size() <
            static_cast<std::size_t>(heightmap.width) * static_cast<std::size_t>(heightmap.height)) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    const float x = clamp_coordinate(p.x, heightmap.boundsMin.x, heightmap.boundsMax.x);
    const float z = clamp_coordinate(p.z, heightmap.boundsMin.z, heightmap.boundsMax.z);

    const float u = (x - heightmap.boundsMin.x) / heightmap.cellSize;
    const float v = (z - heightmap.boundsMin.z) / heightmap.cellSize;
    const int i0 = clamp_index(std::floor(u), heightmap.width);
    const int j0 = clamp_index(std::floor(v), heightmap.height);
    const int i1 = std::min(i0 + 1, heightmap.width - 1);
    const int j1 = std::min(j0 + 1, heightmap.height - 1);

    const float h00 = heightmap.heights[static_cast<std::size_t>(j0) * heightmap.width + i0];
    const float h10 = heightmap.heights[static_cast<std::size_t>(j0) * heightmap.width + i1];
    const float h01 = heightmap.heights[static_cast<std::size_t>(j1) * heightmap.width + i0];
    const float h11 = heightmap.heights[static_cast<std::size_t>(j1) * heightmap.width + i1];

    if (!std::isnan(h00) && !std::isnan(h10) && !std::isnan(h01) && !std::isnan(h11)) {
        const float tx = u - static_cast<float>(i0);
        const float tz = v - static_cast<float>(j0);
        return (1.0f - tx) * (1.0f - tz) * h00 + tx * (1.0f - tz) * h10 +
               (1.0f - tx) * tz * h01 + tx * tz * h11;
    }

    // Any empty node: the nearest valid one among the four. A strict `<` keeps
    // the first of an exact tie (row-major scan order).
    float best = std::numeric_limits<float>::quiet_NaN();
    float best_distance = std::numeric_limits<float>::infinity();
    const int columns[2] = {i0, i1};
    const int rows[2] = {j0, j1};
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 2; ++c) {
            const float height =
                heightmap.heights[static_cast<std::size_t>(rows[r]) * heightmap.width + columns[c]];
            if (std::isnan(height)) {
                continue;
            }
            const float distance = node_distance_squared(heightmap, columns[c], rows[r], x, z);
            if (distance < best_distance) {
                best_distance = distance;
                best = height;
            }
        }
    }
    if (!std::isnan(best)) {
        return best;
    }

    // All four empty (a spawn over a hole): the nearest valid node anywhere in
    // the grid. The result is NaN only when no node is valid at all.
    for (int j = 0; j < heightmap.height; ++j) {
        for (int i = 0; i < heightmap.width; ++i) {
            const float height =
                heightmap.heights[static_cast<std::size_t>(j) * heightmap.width + i];
            if (std::isnan(height)) {
                continue;
            }
            const float distance = node_distance_squared(heightmap, i, j, x, z);
            if (distance < best_distance) {
                best_distance = distance;
                best = height;
            }
        }
    }
    return best;
}

bool heightmap_in_bounds(const Heightmap& heightmap, Vec3 p) {
    return p.x >= heightmap.boundsMin.x && p.x <= heightmap.boundsMax.x &&
           p.z >= heightmap.boundsMin.z && p.z <= heightmap.boundsMax.z;
}
