// Heightmap tests: the W1.3 rasterizer (S3 arena plan) — fixture scenes built
// in code, no asset library needed. The scenes mirror what the loader
// produces: one mesh, indices 0..n-1, and world-space bounds.
#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "heightmap.h"
#include "math3d.h"
#include "scene.h"

namespace {

Vertex vertex_at(Vec3 position) {
    Vertex vertex{};
    vertex.px = position.x;
    vertex.py = position.y;
    vertex.pz = position.z;
    vertex.ny = 1.0f;    // the loader's absent-NORMAL fallback
    vertex.w[0] = 1.0f;  // full weight on joint 0
    return vertex;
}

// A point through a column-major matrix, w = 1 — the same transform the loader
// bakes a scene's world-space bounds with.
Vec3 world_point(const Mat4& matrix, Vec3 point) {
    return Vec3{
        matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12],
        matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13],
        matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14]};
}

// A scene with one mesh holding `positions` (one triangle per vertex triple,
// indices 0..n-1) under `model`, carrying the world-space vertex bounds a
// loaded scene would carry.
Scene scene_from_triangles(const std::vector<Vec3>& positions,
                           const Mat4& model = mat4_identity()) {
    Scene scene;
    MeshCpu mesh;
    mesh.vertices.reserve(positions.size());
    mesh.indices.resize(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        mesh.vertices.push_back(vertex_at(positions[i]));
        mesh.indices[i] = static_cast<uint32_t>(i);
    }
    mesh.model = model;
    scene.meshes.push_back(std::move(mesh));

    for (std::size_t i = 0; i < positions.size(); ++i) {
        const Vec3 world = world_point(model, positions[i]);
        if (i == 0) {
            scene.boundsMin = world;
            scene.boundsMax = world;
            continue;
        }
        scene.boundsMin.x = std::min(scene.boundsMin.x, world.x);
        scene.boundsMin.y = std::min(scene.boundsMin.y, world.y);
        scene.boundsMin.z = std::min(scene.boundsMin.z, world.z);
        scene.boundsMax.x = std::max(scene.boundsMax.x, world.x);
        scene.boundsMax.y = std::max(scene.boundsMax.y, world.y);
        scene.boundsMax.z = std::max(scene.boundsMax.z, world.z);
    }
    return scene;
}

}  // namespace

TEST_CASE("a flat triangle rasterizes to its plane height") {
    // One triangle at y = 3 whose XZ footprint is the lower-left half of
    // [0, 10]^2: nodes with x + z <= 10 are covered, the rest stay empty.
    const Scene scene = scene_from_triangles({{0, 3, 0}, {10, 3, 0}, {0, 3, 10}});
    const Heightmap hm = build_heightmap(scene, 1.0f);

    CHECK(hm.cellSize == doctest::Approx(1.0f));
    CHECK(hm.width == 11);  // ceil(10 / 1) + 1
    CHECK(hm.height == 11);
    CHECK(hm.heights.size() == 121);
    CHECK(hm.boundsMin.x == doctest::Approx(0.0f));
    CHECK(hm.boundsMin.y == doctest::Approx(3.0f));
    CHECK(hm.boundsMin.z == doctest::Approx(0.0f));
    CHECK(hm.boundsMax.x == doctest::Approx(10.0f));
    CHECK(hm.boundsMax.y == doctest::Approx(3.0f));
    CHECK(hm.boundsMax.z == doctest::Approx(10.0f));

    // Inside samples: the plane is flat, so every covered node reads 3.
    CHECK(heightmap_sample(hm, {1, 0, 1}) == doctest::Approx(3.0f));
    CHECK(heightmap_sample(hm, {5, 0, 5}) == doctest::Approx(3.0f));  // on the hypotenuse
    CHECK(heightmap_sample(hm, {2.5f, 0, 3.25f}) == doctest::Approx(3.0f));  // between nodes

    // in_bounds is XZ, edges included; Y is ignored.
    CHECK(heightmap_in_bounds(hm, {1, 0, 1}));
    CHECK(heightmap_in_bounds(hm, {0, 0, 0}));
    CHECK(heightmap_in_bounds(hm, {10, 0, 10}));
    CHECK(heightmap_in_bounds(hm, {5, -100, 5}));
    CHECK_FALSE(heightmap_in_bounds(hm, {-0.01f, 0, 5}));
    CHECK_FALSE(heightmap_in_bounds(hm, {5, 0, 10.01f}));

    // A node outside the triangle (x + z > 10) stays empty.
    CHECK(std::isnan(hm.heights[10 * hm.width + 10]));
}

TEST_CASE("a two-triangle ramp interpolates between grid heights") {
    // A quad over [0, 10]^2 split into two triangles, with y = z: node (i, j)
    // holds j, so a sample halfway between two rows is halfway between them.
    const Scene scene = scene_from_triangles({{0, 0, 0}, {10, 0, 0}, {10, 10, 10},
                                              {0, 0, 0}, {10, 10, 10}, {0, 10, 10}});
    const Heightmap hm = build_heightmap(scene, 1.0f);

    // On a node: the plane's own height.
    CHECK(heightmap_sample(hm, {5, 0, 5}) == doctest::Approx(5.0f));
    CHECK(heightmap_sample(hm, {2, 0, 7}) == doctest::Approx(7.0f));

    // Between nodes: bilinear, the midpoint of the two heights.
    CHECK(heightmap_sample(hm, {5, 0, 5.5f}) == doctest::Approx(5.5f));
    CHECK(heightmap_sample(hm, {2.5f, 0, 7.25f}) == doctest::Approx(7.25f));
}

TEST_CASE("samples outside the bounds clamp to the edge value") {
    // The full quad at y = 3 over [0, 10]^2: every edge node is covered, so a
    // clamped sample reads the edge it lands on.
    const Scene scene = scene_from_triangles({{0, 3, 0}, {10, 3, 0}, {10, 3, 10},
                                              {0, 3, 0}, {10, 3, 10}, {0, 3, 10}});
    const Heightmap hm = build_heightmap(scene, 1.0f);

    CHECK(heightmap_sample(hm, {-20, 0, 5}) == doctest::Approx(3.0f));   // x below boundsMin
    CHECK(heightmap_sample(hm, {25, 0, 5}) == doctest::Approx(3.0f));    // x above boundsMax
    CHECK(heightmap_sample(hm, {5, 0, -30}) == doctest::Approx(3.0f));   // z below boundsMin
    CHECK(heightmap_sample(hm, {5, 0, 30}) == doctest::Approx(3.0f));    // z above boundsMax
    CHECK(heightmap_sample(hm, {99, 0, -99}) == doctest::Approx(3.0f));  // both

    // The clamped sample equals the edge node's own value, and the point is
    // still reported out of bounds.
    CHECK(heightmap_sample(hm, {25, 0, 5}) == doctest::Approx(heightmap_sample(hm, {10, 0, 5})));
    CHECK_FALSE(heightmap_in_bounds(hm, {25, 0, 5}));

    // A non-finite coordinate clamps to boundsMin instead of poisoning the
    // index arithmetic (never NaN when the grid has ground).
    CHECK(heightmap_sample(hm, {std::numeric_limits<float>::quiet_NaN(), 0, 5}) ==
          doctest::Approx(3.0f));
}

TEST_CASE("a hole between triangles samples the nearest valid cell") {
    // Two quads with a gap: [0, 4] x [0, 10] at y = 1 and [8, 10] x [0, 10] at
    // y = 2. Nodes with x in (4, 8) are covered by nothing.
    const Scene scene = scene_from_triangles({{0, 1, 0}, {4, 1, 0}, {4, 1, 10},
                                              {0, 1, 0}, {4, 1, 10}, {0, 1, 10},
                                              {8, 2, 0}, {10, 2, 0}, {10, 2, 10},
                                              {8, 2, 0}, {10, 2, 10}, {8, 2, 10}});
    const Heightmap hm = build_heightmap(scene, 1.0f);

    // The scene's own Y extent is kept verbatim (it is not used for sampling).
    CHECK(hm.boundsMin.y == doctest::Approx(1.0f));
    CHECK(hm.boundsMax.y == doctest::Approx(2.0f));

    // The gap's nodes are empty: x in {5, 6, 7} is covered by nothing.
    CHECK(std::isnan(hm.heights[5 * hm.width + 5]));
    CHECK(std::isnan(hm.heights[5 * hm.width + 6]));
    CHECK(std::isnan(hm.heights[5 * hm.width + 7]));

    // Over the hole the sample takes the nearest valid node: just inside the
    // gap that is the left quad's edge (y = 1), just past the middle it is the
    // right quad's (y = 2) — including where all four surrounding nodes are
    // empty and the fallback has to scan further.
    CHECK(heightmap_sample(hm, {4.8f, 0, 5}) == doctest::Approx(1.0f));  // 2x2 has one valid
    CHECK(heightmap_sample(hm, {7.2f, 0, 5}) == doctest::Approx(2.0f));  // 2x2 has one valid
    CHECK(heightmap_sample(hm, {5.2f, 0, 5}) == doctest::Approx(1.0f));  // 2x2 all empty
    CHECK(heightmap_sample(hm, {6.8f, 0, 5}) == doctest::Approx(2.0f));  // 2x2 all empty
}

TEST_CASE("triangles rasterize through their mesh model matrices") {
    // A composed scene's meshes carry the instance's baked transform: the same
    // local triangle placed by T(1, 2, 3) * S(2, 2, 2) sits at y = 8 over world
    // XZ [1, 21] x [3, 23], and the heightmap must rasterize it there — not in
    // its local coordinates.
    const Mat4 model =
        mat4_from_trs(Vec3{1.0f, 2.0f, 3.0f}, Quat{0.0f, 0.0f, 0.0f, 1.0f}, Vec3{2.0f, 2.0f, 2.0f});
    const Scene scene =
        scene_from_triangles({{0, 3, 0}, {10, 3, 0}, {0, 3, 10}}, model);
    const Heightmap hm = build_heightmap(scene, 1.0f);

    CHECK(hm.boundsMin.x == doctest::Approx(1.0f));
    CHECK(hm.boundsMin.y == doctest::Approx(8.0f));
    CHECK(hm.boundsMin.z == doctest::Approx(3.0f));
    CHECK(hm.boundsMax.x == doctest::Approx(21.0f));
    CHECK(hm.boundsMax.z == doctest::Approx(23.0f));
    CHECK(hm.width == 21);  // ceil(20 / 1) + 1

    CHECK(heightmap_sample(hm, {3, 0, 5}) == doctest::Approx(8.0f));  // local (1, 1): covered
    CHECK(heightmap_sample(hm, {1, 0, 3}) == doctest::Approx(8.0f));  // the placed corner vertex
}

TEST_CASE("an empty scene throws") {
    const Scene empty{};
    CHECK_THROWS_AS(build_heightmap(empty, 1.0f), std::runtime_error);
    CHECK_THROWS_WITH_AS(build_heightmap(empty, 1.0f), "no meshes", std::runtime_error);
}

TEST_CASE("a non-positive or non-finite cell size throws") {
    const Scene scene = scene_from_triangles({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}});
    CHECK_THROWS_AS(build_heightmap(scene, 0.0f), std::runtime_error);
    CHECK_THROWS_AS(build_heightmap(scene, -1.0f), std::runtime_error);
    CHECK_THROWS_AS(build_heightmap(scene, std::numeric_limits<float>::quiet_NaN()),
                    std::runtime_error);
    CHECK_THROWS_AS(build_heightmap(scene, std::numeric_limits<float>::infinity()),
                    std::runtime_error);
}

TEST_CASE("a grid with no covered node samples NaN") {
    // A vertical wall: its XZ footprint is a line, so no node is covered and
    // the whole grid stays empty — the sample has nothing to fall back to.
    const Scene scene = scene_from_triangles({{0, 0, 0}, {0, 10, 0}, {0, 0, 10}});
    const Heightmap hm = build_heightmap(scene, 1.0f);

    CHECK(hm.width == 1);    // zero X extent
    CHECK(hm.height == 11);  // ceil(10 / 1) + 1
    CHECK(hm.heights.size() == 11);
    for (const float h : hm.heights) {
        CHECK(std::isnan(h));
    }
    CHECK(std::isnan(heightmap_sample(hm, {0, 0, 5})));
}

TEST_CASE("build_heightmap rasterizes only the first meshCount meshes") {
    // The W1.4 player-less build: mesh 0 is the ground plane at y = 3 (the
    // lower-left half of [0, 10]^2), mesh 1 is a stand-in "player" plane at
    // y = 5 covering the origin. A composed scene appends the player last, so
    // the app passes its mesh index to keep the player out of the ground.
    Scene scene = scene_from_triangles({{0, 3, 0}, {10, 3, 0}, {0, 3, 10}});
    MeshCpu player;
    player.vertices = {vertex_at({0, 5, 0}), vertex_at({2, 5, 0}), vertex_at({0, 5, 2})};
    player.indices = {0, 1, 2};
    scene.meshes.push_back(std::move(player));

    const Heightmap all = build_heightmap(scene, 1.0f);
    const Heightmap ground_only = build_heightmap(scene, 1.0f, /*meshCount=*/1);

    // With every mesh the player's plane wins the max-Y at the origin.
    CHECK(heightmap_sample(all, {0.5f, 0, 0.5f}) == doctest::Approx(5.0f));
    // With meshCount = 1 the ground plane is what remains.
    CHECK(heightmap_sample(ground_only, {0.5f, 0, 0.5f}) == doctest::Approx(3.0f));

    // The bounds stay the scene's own (the walkable area is unchanged), and a
    // meshCount past the end is simply "all meshes".
    CHECK(ground_only.boundsMin.x == doctest::Approx(all.boundsMin.x));
    CHECK(ground_only.boundsMax.x == doctest::Approx(all.boundsMax.x));
    CHECK(heightmap_sample(build_heightmap(scene, 1.0f, 99), {0.5f, 0, 0.5f}) ==
          doctest::Approx(5.0f));

    // meshCount 0 leaves nothing to rasterize: the same error as no meshes.
    CHECK_THROWS_WITH_AS(build_heightmap(scene, 1.0f, 0), "no meshes", std::runtime_error);
}

TEST_CASE("heightmap_sample refuses an empty grid instead of reading out of bounds") {
    // A default-constructed Heightmap has no grid at all (the app's "no ground"
    // state): the sample is NaN, never an index into an empty vector.
    const Heightmap empty{};
    CHECK(std::isnan(heightmap_sample(empty, {0, 0, 0})));
    CHECK(std::isnan(heightmap_sample(empty, {5, 1, -3})));

    // A grid whose heights vector is shorter than width * height is refused the
    // same way (it could not be indexed safely either).
    Heightmap short_grid{};
    short_grid.cellSize = 1.0f;
    short_grid.width = 4;
    short_grid.height = 4;
    short_grid.heights.assign(3, 0.0f);
    short_grid.boundsMin = Vec3{0, 0, 0};
    short_grid.boundsMax = Vec3{3, 0, 3};
    CHECK(std::isnan(heightmap_sample(short_grid, {1, 0, 1})));
}
