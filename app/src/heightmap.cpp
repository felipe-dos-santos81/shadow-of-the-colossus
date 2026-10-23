#include "heightmap.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
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

// ---- the layered ground ----------------------------------------------------

constexpr float kLayerMerge = 0.05f;

bool is_layered(const Heightmap& hm) {
    return hm.maxLayers > 0 && hm.width > 0 && hm.height > 0 &&
           hm.layers.size() >= static_cast<std::size_t>(hm.width) * hm.height * hm.maxLayers &&
           hm.blocked.size() >= hm.layers.size();
}

std::size_t layer_base(const Heightmap& hm, int i, int j) {
    return (static_cast<std::size_t>(j) * hm.width + i) * hm.maxLayers;
}

// Adds `y` to one node's ascending, NaN-padded list: within kLayerMerge of an
// existing layer it merges to the higher; a full list drops its lowest.
// Returns false when the list was full (a layer was lost: the node saturated).
bool insert_layer(float* node, int capacity, float y) {
    int count = 0;
    while (count < capacity && !std::isnan(node[count])) {
        if (std::fabs(node[count] - y) <= kLayerMerge) {
            node[count] = std::max(node[count], y);
            return true;
        }
        ++count;
    }
    int at = 0;
    while (at < count && node[at] < y) {
        ++at;
    }
    if (count == capacity) {
        if (at == 0) {
            return false;  // lower than everything kept
        }
        for (int k = 0; k + 1 < at; ++k) {
            node[k] = node[k + 1];  // drop the lowest
        }
        node[at - 1] = y;
        return false;
    }
    for (int k = count; k > at; --k) {
        node[k] = node[k - 1];
    }
    node[at] = y;
    return true;
}

// The layer index a body at height `y` stands on at one node: the highest layer
// at or below y + step, else the lowest; -1 when the node has no layer.
int select_layer(const Heightmap& hm, int i, int j, float y, float step) {
    const float* node = &hm.layers[layer_base(hm, i, j)];
    int best = -1;
    for (int k = 0; k < hm.maxLayers && !std::isnan(node[k]); ++k) {
        if (best < 0 || node[k] <= y + step) {
            best = k;
        }
    }
    return best;
}

float layer_height(const Heightmap& hm, int i, int j, float y, float step) {
    const int k = select_layer(hm, i, j, y, step);
    return k < 0 ? std::numeric_limits<float>::quiet_NaN() : hm.layers[layer_base(hm, i, j) + k];
}

// The node nearest `p` (clamped into the grid).
void nearest_node(const Heightmap& hm, Vec3 p, int& i, int& j) {
    const float x = clamp_coordinate(p.x, hm.boundsMin.x, hm.boundsMax.x);
    const float z = clamp_coordinate(p.z, hm.boundsMin.z, hm.boundsMax.z);
    i = clamp_index(std::floor((x - hm.boundsMin.x) / hm.cellSize + 0.5f), hm.width);
    j = clamp_index(std::floor((z - hm.boundsMin.z) / hm.cellSize + 0.5f), hm.height);
}

// Flags, on the nodes within one cell of (x, z), every layer whose body band
// overlaps the wall's [y_lo, y_hi].
void flag_walls_near(Heightmap& hm, float x, float z, float y_lo, float y_hi) {
    const float u = (x - hm.boundsMin.x) / hm.cellSize;
    const float v = (z - hm.boundsMin.z) / hm.cellSize;
    const int i0 = clamp_index(std::ceil(u - 1.0f), hm.width);
    const int i1 = clamp_index(std::floor(u + 1.0f), hm.width);
    const int j0 = clamp_index(std::ceil(v - 1.0f), hm.height);
    const int j1 = clamp_index(std::floor(v + 1.0f), hm.height);
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            const std::size_t base = layer_base(hm, i, j);
            for (int k = 0; k < hm.maxLayers && !std::isnan(hm.layers[base + k]); ++k) {
                const float y = hm.layers[base + k];
                if (y_hi >= y + kBodyBandLow && y_lo <= y + kBodyBandHigh) {
                    // Non-zero = blocked; the value is higher the nearer the wall
                    // (100 on it, falling to 1 a cell away), which is what lets
                    // heightmap_walkable tell a step out of the strip from one in.
                    const float du = static_cast<float>(i) - u;
                    const float dv = static_cast<float>(j) - v;
                    const int depth = 100 - static_cast<int>(100.0f * std::sqrt(du * du + dv * dv));
                    hm.blocked[base + k] = std::max<uint8_t>(
                        hm.blocked[base + k], static_cast<uint8_t>(std::max(1, depth)));
                }
            }
        }
    }
}

}  // namespace

Heightmap build_heightmap(const Scene& scene, float cellSize, bool layered) {
    if (!(cellSize > 0.0f) || !std::isfinite(cellSize)) {
        throw std::runtime_error("heightmap cell size must be positive and finite");
    }
    if (scene.meshes.empty()) {
        throw std::runtime_error("no meshes");
    }
    // Only static meshes are ground: the player and the entities are rigged and
    // must never rasterize into the surface they stand on.
    std::size_t static_count = 0;
    for (const MeshCpu& mesh : scene.meshes) {
        if (mesh.rig < 0) {
            ++static_count;
        }
    }
    if (static_count == 0) {
        throw std::runtime_error("no static meshes");
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

    if (layered) {
        heightmap.maxLayers = kHeightmapLayers;
        heightmap.layers.assign(heightmap.heights.size() * kHeightmapLayers,
                                std::numeric_limits<float>::quiet_NaN());
        heightmap.blocked.assign(heightmap.layers.size(), 0);
    }

    // Calls `visit(p0, p1, p2)` for each static triangle, in world space.
    const auto for_each_triangle = [&](auto&& visit) {
        for (std::size_t mesh_index = 0; mesh_index < scene.meshes.size(); ++mesh_index) {
            const MeshCpu& mesh = scene.meshes[mesh_index];
            if (mesh.rig >= 0) {
                continue;  // a rigged mesh (player or entity): never ground
            }
            for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
                const std::size_t i0 = mesh.indices[t];
                const std::size_t i1 = mesh.indices[t + 1];
                const std::size_t i2 = mesh.indices[t + 2];
                if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() ||
                    i2 >= mesh.vertices.size()) {
                    continue;  // a malformed index would read out of bounds
                }
                const Vec3 p0 = transform_point(
                    mesh.model,
                    Vec3{mesh.vertices[i0].px, mesh.vertices[i0].py, mesh.vertices[i0].pz});
                const Vec3 p1 = transform_point(
                    mesh.model,
                    Vec3{mesh.vertices[i1].px, mesh.vertices[i1].py, mesh.vertices[i1].pz});
                const Vec3 p2 = transform_point(
                    mesh.model,
                    Vec3{mesh.vertices[i2].px, mesh.vertices[i2].py, mesh.vertices[i2].pz});
                if (!is_finite(p0) || !is_finite(p1) || !is_finite(p2)) {
                    continue;  // a non-finite vertex would poison the index arithmetic
                }
                visit(p0, p1, p2);
            }
        }
    };

    // |normal.y| of a triangle (normal normalized); 1 for a flat floor, 0 for a
    // wall. A zero-area triangle has no normal and counts as neither.
    const auto steepness = [](Vec3 a, Vec3 b, Vec3 c) {
        const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z};
        const Vec3 v{c.x - a.x, c.y - a.y, c.z - a.z};
        const Vec3 n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
        const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        return length > 0.0f ? std::fabs(n.y) / length : -1.0f;
    };

    bool saturated = false;  // the saturation warning prints once per build
    for_each_triangle([&](Vec3 p0, Vec3 p1, Vec3 p2) {
        const float area = cross_xz(p0, p1, p2);
        if (area == 0.0f) {
            return;  // a vertical wall: no XZ footprint, no ground height
        }
        const bool floor_triangle = layered && steepness(p0, p1, p2) >= kWallSteepness;
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
                if (floor_triangle &&
                    !insert_layer(&heightmap.layers[layer_base(heightmap, i, j)],
                                  heightmap.maxLayers, y) &&
                    !saturated) {
                    saturated = true;
                    std::fprintf(stderr,
                                 "sotc-viewer: a heightmap node holds more than %d floor layers "
                                 "at (%.2f, %.2f); the lowest are dropped\n",
                                 kHeightmapLayers, node.x, node.z);
                }
            }
        }
    });

    if (layered) {
        // Walls, once every floor layer is known: march each steep triangle's
        // outline at half-cell steps and flag the layers its height reaches.
        for_each_triangle([&](Vec3 p0, Vec3 p1, Vec3 p2) {
            const float s = steepness(p0, p1, p2);
            if (!(s >= 0.0f && s < kWallSteepness)) {
                return;
            }
            const float y_lo = std::min({p0.y, p1.y, p2.y});
            const float y_hi = std::max({p0.y, p1.y, p2.y});
            const Vec3 corners[3] = {p0, p1, p2};
            for (int e = 0; e < 3; ++e) {
                const Vec3 a = corners[e];
                const Vec3 b = corners[(e + 1) % 3];
                const float length = std::hypot(b.x - a.x, b.z - a.z);
                const int steps = std::max(1, static_cast<int>(std::ceil(length / (cellSize * 0.5f))));
                for (int n = 0; n <= steps; ++n) {
                    const float f = static_cast<float>(n) / static_cast<float>(steps);
                    flag_walls_near(heightmap, a.x + (b.x - a.x) * f, a.z + (b.z - a.z) * f, y_lo,
                                    y_hi);
                }
            }
        });
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

float heightmap_floor(const Heightmap& heightmap, Vec3 p, float step) {
    if (!is_layered(heightmap)) {
        return heightmap_sample(heightmap, p);
    }

    const float x = clamp_coordinate(p.x, heightmap.boundsMin.x, heightmap.boundsMax.x);
    const float z = clamp_coordinate(p.z, heightmap.boundsMin.z, heightmap.boundsMax.z);
    const float u = (x - heightmap.boundsMin.x) / heightmap.cellSize;
    const float v = (z - heightmap.boundsMin.z) / heightmap.cellSize;
    const int i0 = clamp_index(std::floor(u), heightmap.width);
    const int j0 = clamp_index(std::floor(v), heightmap.height);
    const int i1 = std::min(i0 + 1, heightmap.width - 1);
    const int j1 = std::min(j0 + 1, heightmap.height - 1);

    const float h00 = layer_height(heightmap, i0, j0, p.y, step);
    const float h10 = layer_height(heightmap, i1, j0, p.y, step);
    const float h01 = layer_height(heightmap, i0, j1, p.y, step);
    const float h11 = layer_height(heightmap, i1, j1, p.y, step);
    if (!std::isnan(h00) && !std::isnan(h10) && !std::isnan(h01) && !std::isnan(h11)) {
        const float tx = u - static_cast<float>(i0);
        const float tz = v - static_cast<float>(j0);
        return (1.0f - tx) * (1.0f - tz) * h00 + tx * (1.0f - tz) * h10 +
               (1.0f - tx) * tz * h01 + tx * tz * h11;
    }

    // Any empty node: the nearest node holding a layer, the four first and then
    // the whole grid (a spawn over a hole), as heightmap_sample does.
    float best = std::numeric_limits<float>::quiet_NaN();
    float best_distance = std::numeric_limits<float>::infinity();
    const auto consider = [&](int i, int j) {
        const float height = layer_height(heightmap, i, j, p.y, step);
        if (std::isnan(height)) {
            return;
        }
        const float distance = node_distance_squared(heightmap, i, j, x, z);
        if (distance < best_distance) {
            best_distance = distance;
            best = height;
        }
    };
    for (const int j : {j0, j1}) {
        for (const int i : {i0, i1}) {
            consider(i, j);
        }
    }
    if (!std::isnan(best)) {
        return best;
    }
    for (int j = 0; j < heightmap.height; ++j) {
        for (int i = 0; i < heightmap.width; ++i) {
            consider(i, j);
        }
    }
    return best;
}

// refuseHoles: the target's nearest node must hold ground (read straight from
// `heights`, before any floor query — heightmap_sample scans the whole grid at a
// hole). When the move crosses into a different node, that node's height may not
// rise above the source node's by more than kHoleRiseMax nor drop below it by more
// than kHoleDropMax. The two stored node heights are compared, never the
// interpolated floors: a cliff between adjacent nodes would read as a ramp across
// the cell and let frame-sized steps through. A move inside one node crosses no
// boundary and is allowed; a source over a hole may leave. The layered rules, when
// the map has them, still apply after this.
static bool hole_move_allowed(const Heightmap& hm, Vec3 from, Vec3 to) {
    if (hm.width <= 0 || hm.height <= 0 ||
        hm.heights.size() < static_cast<std::size_t>(hm.width) * hm.height) {
        return true;  // no grid: nothing to refuse
    }
    int si = 0, sj = 0, ti = 0, tj = 0;
    nearest_node(hm, from, si, sj);
    nearest_node(hm, to, ti, tj);
    const float target = hm.heights[static_cast<std::size_t>(tj) * hm.width + ti];
    if (std::isnan(target)) {
        return false;
    }
    if (si == ti && sj == tj) {
        return true;
    }
    const float source = hm.heights[static_cast<std::size_t>(sj) * hm.width + si];
    if (std::isnan(source)) {
        return true;
    }
    const float rise = target - source;
    return !(rise > kHoleRiseMax) && !(-rise > kHoleDropMax);
}

bool heightmap_walkable(const Heightmap& heightmap, Vec3 from, Vec3 to) {
    if (heightmap.refuseHoles && !hole_move_allowed(heightmap, from, to)) {
        return false;
    }
    if (!is_layered(heightmap)) {
        return true;
    }
    int si = 0, sj = 0, ti = 0, tj = 0;
    nearest_node(heightmap, from, si, sj);
    nearest_node(heightmap, to, ti, tj);
    const int src = select_layer(heightmap, si, sj, from.y, kFloorStep);
    const int dst = select_layer(heightmap, ti, tj, from.y, kFloorStep);
    if (dst < 0) {
        return src < 0;  // no floor ahead: only a body already over a hole may move
    }
    const std::size_t dst_at = layer_base(heightmap, ti, tj) + dst;
    // A body already inside a wall's strip (a spawn, a landing or a push put it
    // there) must be able to leave: a blocked target is refused unless the
    // source is blocked too and the target is no nearer the wall than it.
    const std::size_t src_at = src < 0 ? 0 : layer_base(heightmap, si, sj) + src;
    if (heightmap.blocked[dst_at] != 0 &&
        (src < 0 || heightmap.blocked[dst_at] > heightmap.blocked[src_at])) {
        return false;
    }
    if (src < 0) {
        return true;
    }
    const float rise = heightmap.layers[dst_at] - heightmap.layers[src_at];
    // A hair of slack: a 0.6 m stair is a step, not a wall.
    return rise <= kFloorStep + 1e-4f && -rise <= kFloorDrop + 1e-4f;
}
