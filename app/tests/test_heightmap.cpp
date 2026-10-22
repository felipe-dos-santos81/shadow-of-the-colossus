// Heightmap tests: the W1.3 rasterizer (S3 arena plan) — fixture scenes built
// in code, no asset library needed. The scenes mirror what the loader
// produces: meshes with indices 0..n-1, a world-space bounds pair, and the
// rig index (the W1.4 "player-less build" is now the S4 rig filter: only
// `mesh.rig < 0` rasterizes).
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
#include "player.h"
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

// A scene with one static (rig -1) mesh holding `positions` (one triangle per
// vertex triple, indices 0..n-1) under `model`, carrying the world-space
// vertex bounds a loaded scene would carry.
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

TEST_CASE("build_heightmap rasterizes only rig-less (static) meshes") {
    // The S4 ground contract: mesh 0 is the ground plane at y = 3 (the
    // lower-left half of [0, 10]^2), mesh 1 is the player's body (rig 0) at
    // y = 5 covering the origin, mesh 2 is an entity (rig 1) at y = 7 over the
    // same cell. Only the rig-less mesh may become ground.
    Scene scene = scene_from_triangles({{0, 3, 0}, {10, 3, 0}, {0, 3, 10}});
    MeshCpu player;
    player.vertices = {vertex_at({0, 5, 0}), vertex_at({2, 5, 0}), vertex_at({0, 5, 2})};
    player.indices = {0, 1, 2};
    player.rig = 0;
    scene.meshes.push_back(std::move(player));
    MeshCpu entity;
    entity.vertices = {vertex_at({0, 7, 0}), vertex_at({2, 7, 0}), vertex_at({0, 7, 2})};
    entity.indices = {0, 1, 2};
    entity.rig = 1;
    scene.meshes.push_back(std::move(entity));
    scene.boundsMax.y = 7.0f;  // a loaded scene's bounds cover the rigged meshes too

    const Heightmap hm = build_heightmap(scene, 1.0f);

    // The rigged meshes at y = 5 and y = 7 lose the max-Y rule: the static
    // plane's 3 is the ground.
    CHECK(heightmap_sample(hm, {0.5f, 0, 0.5f}) == doctest::Approx(3.0f));
    // The grid keeps the scene's own bounds: the walkable area is unchanged.
    CHECK(hm.boundsMin.x == doctest::Approx(scene.boundsMin.x));
    CHECK(hm.boundsMax.x == doctest::Approx(scene.boundsMax.x));
    CHECK(hm.boundsMax.y == doctest::Approx(scene.boundsMax.y));
    CHECK(hm.boundsMax.y == doctest::Approx(7.0f));  // the entity is in the bounds
}

TEST_CASE("an entity placed over a cell changes no ground sample") {
    // The same static ground, with and without a rigged entity covering the
    // whole footprint at a higher Y: every sample must be identical, and both
    // builds still sample the static ground.
    Scene ground_only = scene_from_triangles({{0, 3, 0}, {10, 3, 0}, {10, 3, 10},
                                              {0, 3, 0}, {10, 3, 10}, {0, 3, 10}});
    Scene with_entity = ground_only;
    MeshCpu entity;
    entity.vertices = {vertex_at({0, 40, 0}), vertex_at({10, 40, 0}), vertex_at({0, 40, 10})};
    entity.indices = {0, 1, 2};
    entity.rig = 1;
    with_entity.meshes.push_back(std::move(entity));
    with_entity.boundsMax.y = 40.0f;  // the entity extends the scene's bounds

    const Heightmap plain = build_heightmap(ground_only, 1.0f);
    const Heightmap placed = build_heightmap(with_entity, 1.0f);

    REQUIRE(plain.heights.size() == placed.heights.size());
    for (std::size_t i = 0; i < plain.heights.size(); ++i) {
        CHECK(plain.heights[i] == placed.heights[i]);
    }
    CHECK(heightmap_sample(placed, {5, 0, 5}) == doctest::Approx(3.0f));

    // A player rig (0) is excluded the same way.
    Scene with_player = ground_only;
    MeshCpu player = entity;
    player.rig = 0;
    with_player.meshes.push_back(std::move(player));
    const Heightmap player_build = build_heightmap(with_player, 1.0f);
    CHECK(heightmap_sample(player_build, {5, 0, 5}) == doctest::Approx(3.0f));
}

TEST_CASE("a scene whose meshes are all rigged has no ground to build") {
    // Nothing static to rasterize: the same "no ground" answer the S3
    // meshCount = 0 case gave, so the app's no-ground path is unchanged.
    Scene scene = scene_from_triangles({{0, 5, 0}, {2, 5, 0}, {0, 5, 2}});
    scene.meshes[0].rig = 0;
    CHECK_THROWS_WITH_AS(build_heightmap(scene, 1.0f), "no static meshes", std::runtime_error);
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

// ---- S7: the layered ground ------------------------------------------------

namespace {

// Two triangles: the horizontal quad [x0, x1] x [z0, z1] at height y.
void add_floor(std::vector<Vec3>& out, float x0, float x1, float z0, float z1, float y) {
    out.insert(out.end(), {{x0, y, z0}, {x1, y, z0}, {x0, y, z1}});
    out.insert(out.end(), {{x1, y, z0}, {x1, y, z1}, {x0, y, z1}});
}

// Two triangles: the vertical quad on x = x, spanning z0..z1 and y0..y1.
void add_wall_x(std::vector<Vec3>& out, float x, float z0, float z1, float y0, float y1) {
    out.insert(out.end(), {{x, y0, z0}, {x, y1, z0}, {x, y0, z1}});
    out.insert(out.end(), {{x, y1, z0}, {x, y1, z1}, {x, y0, z1}});
}

}  // namespace

TEST_CASE("an unlayered map's floor is its sample and every move is walkable") {
    std::vector<Vec3> tris;
    add_floor(tris, 0, 10, 0, 10, 2.0f);
    const Heightmap hm = build_heightmap(scene_from_triangles(tris), 1.0f);
    CHECK(hm.maxLayers == 0);
    for (const float x : {0.0f, 3.3f, 9.9f}) {
        const float z = 10.0f - x;
        const float sample = heightmap_sample(hm, {x, 0, z});
        CHECK(heightmap_floor(hm, {x, -50, z}, kFloorStep) == sample);
        CHECK(heightmap_floor(hm, {x, 500, z}, kFloorStep) == sample);
        CHECK(heightmap_walkable(hm, {x, 0, z}, {x + 5, 0, z}));
        CHECK(heightmap_walkable(hm, {x, 0, z}, {x, 900, z + 50}));
    }
    // A hand-built map with no grid at all stays defined.
    CHECK(std::isnan(heightmap_floor(Heightmap{}, {0, 0, 0}, kFloorStep)));
    CHECK(heightmap_walkable(Heightmap{}, {0, 0, 0}, {1, 0, 0}));
}

TEST_CASE("a layered two-storey column answers the floor under the feet") {
    std::vector<Vec3> tris;
    add_floor(tris, -4, 4, -4, 4, 9.0f);
    add_floor(tris, -4, 4, -4, 4, 60.0f);
    const Heightmap hm = build_heightmap(scene_from_triangles(tris), 1.0f, true);
    CHECK(hm.maxLayers == kHeightmapLayers);
    CHECK(heightmap_floor(hm, {0, 9.2f, 0}, kFloorStep) == doctest::Approx(9.0f));
    CHECK(heightmap_floor(hm, {0, 61.0f, 0}, kFloorStep) == doctest::Approx(60.0f));
    CHECK(heightmap_floor(hm, {0, 5.0f, 0}, kFloorStep) == doctest::Approx(9.0f));
    CHECK(heightmap_floor(hm, {0.5f, 9.2f, 0.5f}, kFloorStep) == doctest::Approx(9.0f));
    // The unlayered query still sees the top.
    CHECK(heightmap_sample(hm, {0, 0, 0}) == doctest::Approx(60.0f));
    // Walking on the lower storey under the roof stays walkable.
    CHECK(heightmap_walkable(hm, {0, 9, 0}, {1, 9, 0}));
}

TEST_CASE("layers within 0.05 m merge and a full node keeps the highest") {
    std::vector<Vec3> tris;
    add_floor(tris, 0, 4, 0, 4, 1.00f);
    add_floor(tris, 0, 4, 0, 4, 1.03f);  // merges into 1.03
    for (int k = 0; k < kHeightmapLayers + 2; ++k) {
        add_floor(tris, 0, 4, 0, 4, 10.0f * static_cast<float>(k + 1));
    }
    const Heightmap hm = build_heightmap(scene_from_triangles(tris), 1.0f, true);
    // kHeightmapLayers + 2 storeys + the merged pair: the three lowest (1.03, 10,
    // 20) are dropped, the kHeightmapLayers highest survive from 30 up.
    CHECK(heightmap_floor(hm, {2, 0, 2}, kFloorStep) == doctest::Approx(30.0f));
    CHECK(heightmap_floor(hm, {2, 100, 2}, kFloorStep) == doctest::Approx(100.0f));
    CHECK(heightmap_floor(hm, {2, 45, 2}, kFloorStep) == doctest::Approx(40.0f));
}

TEST_CASE("a step within kFloorStep is climbed") {
    std::vector<Vec3> tris;
    add_floor(tris, -4, 0, -4, 4, 9.0f);
    add_floor(tris, 0, 4, -4, 4, 9.5f);
    add_floor(tris, 0, 4, -4, 4, 12.0f);  // a walkway over the second
    const Heightmap hm = build_heightmap(scene_from_triangles(tris), 1.0f, true);
    CHECK(heightmap_floor(hm, {1, 9.0f, 0}, kFloorStep) == doctest::Approx(9.5f));
    CHECK(heightmap_floor(hm, {-2, 9.0f, 0}, kFloorStep) == doctest::Approx(9.0f));
    CHECK(heightmap_floor(hm, {1, 12.2f, 0}, kFloorStep) == doctest::Approx(12.0f));
    CHECK(heightmap_walkable(hm, {-2, 9.0f, 0}, {1, 9.0f, 0}));
}

TEST_CASE("a rise above kFloorStep or a drop below kFloorDrop is not walkable") {
    std::vector<Vec3> tris;
    add_floor(tris, -4, 0, -4, 4, 9.0f);
    add_floor(tris, 0, 4, -4, 4, 10.0f);  // a 1.0 m ledge
    add_floor(tris, 4, 8, -4, 4, 5.0f);   // a 5 m drop from the ledge
    const Heightmap hm = build_heightmap(scene_from_triangles(tris), 1.0f, true);
    CHECK_FALSE(heightmap_walkable(hm, {-2, 9, 0}, {1, 9, 0}));   // up 1.0
    CHECK(heightmap_walkable(hm, {1, 10, 0}, {-2, 10, 0}));       // down 1.0
    CHECK_FALSE(heightmap_walkable(hm, {2, 10, 0}, {6, 10, 0}));  // down 5.0
}

TEST_CASE("a wall in the body band blocks, a ceiling above it does not") {
    std::vector<Vec3> tris;
    add_floor(tris, -4, 6, -4, 4, 9.0f);
    add_wall_x(tris, 3.0f, -4, 4, 9.0f, 13.0f);
    add_floor(tris, -4, 6, -4, 4, 20.0f);
    const Heightmap hm = build_heightmap(scene_from_triangles(tris), 1.0f, true);
    CHECK_FALSE(heightmap_walkable(hm, {1, 9, 0}, {3.5f, 9, 0}));
    CHECK_FALSE(heightmap_walkable(hm, {5, 9, 0}, {2.6f, 9, 0}));
    CHECK(heightmap_walkable(hm, {1, 9, 0}, {1, 9, 2}));
    // The roof's own band (y 20.6..21.8) is above the wall's top (13).
    CHECK(heightmap_walkable(hm, {1, 20, 0}, {3.5f, 20, 0}));
    // A wall is never ground: the floor under it is still the floor.
    CHECK(heightmap_floor(hm, {3, 9, 0}, kFloorStep) == doctest::Approx(9.0f));
}

TEST_CASE("a move with no floor within kFloorDrop below is refused") {
    std::vector<Vec3> tris;
    add_floor(tris, -4, 4, -4, 4, 9.0f);
    add_floor(tris, 4, 12, -4, 4, 0.0f);  // the exterior ground
    const Heightmap hm = build_heightmap(scene_from_triangles(tris), 1.0f, true);
    CHECK_FALSE(heightmap_walkable(hm, {3, 9, 0}, {5, 9, 0}));
    CHECK(heightmap_walkable(hm, {1, 9, 0}, {3, 9, 0}));
}

TEST_CASE("a layered target over a hole is refused, a source over one may leave") {
    std::vector<Vec3> tris;
    add_floor(tris, 0, 4, 0, 4, 9.0f);
    add_floor(tris, 8, 12, 0, 4, 9.0f);  // a gap at x 4..8
    const Heightmap hm = build_heightmap(scene_from_triangles(tris), 1.0f, true);
    CHECK_FALSE(heightmap_walkable(hm, {3, 9, 2}, {6, 9, 2}));
    CHECK(heightmap_walkable(hm, {6, 9, 2}, {7, 9, 2}));
}

TEST_CASE("player_update slides along a wall and stays on its floor") {
    std::vector<Vec3> tris;
    add_floor(tris, -4, 12, -4, 12, 9.0f);
    add_wall_x(tris, 6.0f, -4, 12, 9.0f, 13.0f);
    const Heightmap hm = build_heightmap(scene_from_triangles(tris), 0.5f, true);

    PlayerState state;
    state.position = {2.0f, 9.0f, 0.0f};
    PlayerInput input;
    input.forward = true;  // camera yaw 90: forward is +X, right is +Z
    input.right = true;
    for (int i = 0; i < 120; ++i) {
        player_update(state, input, 0.05f, hm, 90.0f);
    }
    CHECK(state.position.x < 6.0f);  // never through the wall
    CHECK(state.position.x > 4.0f);  // it did walk up to it
    CHECK(state.position.z > 5.0f);  // and slid along it
    CHECK(state.position.y == doctest::Approx(9.0f));
}

TEST_CASE("a body inside a wall's strip can walk out but not deeper in") {
    std::vector<Vec3> tris;
    add_floor(tris, -4, 8, -4, 4, 9.0f);
    add_wall_x(tris, 3.0f, -4, 4, 9.0f, 13.0f);
    const Heightmap hm = build_heightmap(scene_from_triangles(tris), 1.0f, true);

    // Standing on the wall's own node: stepping out either side is allowed.
    CHECK(heightmap_walkable(hm, {3.4f, 9, 0}, {4.4f, 9, 0}));
    CHECK(heightmap_walkable(hm, {3.4f, 9, 0}, {2.4f, 9, 0}));
    // Standing in the outer ring (node x = 4): out is fine, back in is not.
    CHECK(heightmap_walkable(hm, {4.2f, 9, 0}, {4.8f, 9, 0}));
    CHECK_FALSE(heightmap_walkable(hm, {4.2f, 9, 0}, {3.4f, 9, 0}));
    // Walked out in small steps, the body ends up in free ground.
    Vec3 p{3.4f, 9, 0};
    for (int i = 0; i < 20; ++i) {
        const Vec3 next{p.x + 0.1f, p.y, p.z};
        if (heightmap_walkable(hm, p, next)) {
            p = next;
        }
    }
    CHECK(p.x > 5.0f);
}

TEST_CASE("an unlayered build leaves layers and blocked empty") {
    std::vector<Vec3> tris;
    add_floor(tris, 0, 4, 0, 4, 1.0f);
    add_wall_x(tris, 2.0f, 0, 4, 1.0f, 3.0f);
    const Heightmap hm = build_heightmap(scene_from_triangles(tris), 1.0f);
    CHECK(hm.layers.empty());
    CHECK(hm.blocked.empty());
}
