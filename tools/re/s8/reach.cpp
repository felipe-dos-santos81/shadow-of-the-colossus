// S8 gate (R1): walk a stage's ground with the app's own heightmap code and dump
// what is reachable, for tools/re/s8/placements.py. Scratch-built against the
// read-only app sources (the S7 reach-harness approach); never part of the app.
//
//   reach shrine <lib> <shrine.manifest> <out.bin> [sx sy sz]
//       layered 1 m map (the frozen S7 shrine walk), BFS on a 0.25 m lattice from
//       the manifest's spawn (or the given start), per-axis moves through heightmap_walkable, feet by
//       heightmap_floor (player_update's rule). Dumps the feet grid (NaN = unreached).
//   reach plain <lib> <sx> <sy> <sz> <out.bin> <glb>...   (sx sy sz: the start)
//       unlayered 1 m map of the given glbs (identity transforms), BFS over the
//       grid nodes from the node nearest the spawn with the design-D3 refuseHoles
//       rule emulated: a move is refused when its target node has no ground (NaN),
//       or the node height drops > 1.5 m or rises > 1.0 m. Dumps the node heights
//       and the reached heights (NaN = unreached).
//
// Dump: int32 W, int32 H, float32 minX, float32 minZ, float32 step, then one or
// two float32[W*H] arrays (row j = z, column i = x).
//
// Build (from the repo root):
//   c++ -std=c++20 -O2 -Iapp/src -Iapp/third_party tools/re/s8/reach.cpp \
//     app/src/asset_io.cpp app/src/manifest.cpp app/src/heightmap.cpp app/src/anim.cpp app/src/player.cpp \
//     app/src/collision.cpp -o <scratch>/reach
#include "asset_io.h"
#include "heightmap.h"
#include "manifest.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

static void dump(const char* path, int W, int H, float x0, float z0, float step,
                 const std::vector<const std::vector<float>*>& arrays) {
    FILE* f = std::fopen(path, "wb");
    if (!f) throw std::runtime_error(std::string("cannot write ") + path);
    std::fwrite(&W, 4, 1, f); std::fwrite(&H, 4, 1, f);
    std::fwrite(&x0, 4, 1, f); std::fwrite(&z0, 4, 1, f); std::fwrite(&step, 4, 1, f);
    for (auto* a : arrays) std::fwrite(a->data(), 4, a->size(), f);
    std::fclose(f);
}

static const char* kPlayer = "models/shadow/characters/wander/_cmn/wander.glb";

static int shrine(const char* lib, const char* manifestPath, const char* out, const Vec3* start) {
    SceneManifest m = parse_manifest(manifestPath);
    if (start) m.spawnPosition = *start;
    Scene scene = compose_scene(lib, m, kPlayer);
    Heightmap hm = build_heightmap(scene, 1.0f, true);
    const float h = 0.25f;
    const int W = (int)std::ceil((hm.boundsMax.x - hm.boundsMin.x) / h) + 1;
    const int H = (int)std::ceil((hm.boundsMax.z - hm.boundsMin.z) / h) + 1;
    std::vector<float> feet((size_t)W * H, NAN);
    auto idx = [&](int i, int j) { return (size_t)j * W + i; };
    auto px = [&](int i) { return hm.boundsMin.x + i * h; };
    auto pz = [&](int j) { return hm.boundsMin.z + j * h; };
    const int si = (int)std::lround((m.spawnPosition.x - hm.boundsMin.x) / h);
    const int sj = (int)std::lround((m.spawnPosition.z - hm.boundsMin.z) / h);
    feet[idx(si, sj)] = heightmap_floor(hm, Vec3{px(si), m.spawnPosition.y, pz(sj)}, kFloorStep);
    std::queue<std::pair<int, int>> q;
    q.push({si, sj});
    long reached = 0;
    while (!q.empty()) {
        auto [i, j] = q.front(); q.pop(); ++reached;
        const Vec3 from{px(i), feet[idx(i, j)], pz(j)};
        const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto& dd : d) {
            const int ni = i + dd[0], nj = j + dd[1];
            if (ni < 0 || nj < 0 || ni >= W || nj >= H || !std::isnan(feet[idx(ni, nj)])) continue;
            const Vec3 to{px(ni), from.y, pz(nj)};
            if (!heightmap_walkable(hm, from, to)) continue;
            const float y = heightmap_floor(hm, to, kFloorStep);
            if (!std::isfinite(y)) continue;
            feet[idx(ni, nj)] = y;
            q.push({ni, nj});
        }
    }
    std::printf("shrine grid %dx%d nodes, lattice %dx%d @%.2f m, spawn (%.3f, %.3f, %.3f) feet %.3f, reached %ld points\n",
                hm.width, hm.height, W, H, h, m.spawnPosition.x, m.spawnPosition.y, m.spawnPosition.z,
                feet[idx(si, sj)], reached);
    dump(out, W, H, hm.boundsMin.x, hm.boundsMin.z, h, {&feet});
    return 0;
}

static int plain(const char* lib, Vec3 spawn, const char* out, int n, char** glbs) {
    SceneManifest m{};
    for (int k = 0; k < n; ++k)
        m.instances.push_back(ManifestInstance{glbs[k], Vec3{0, 0, 0}, Quat{0, 0, 0, 1}, Vec3{1, 1, 1}});
    m.spawnPosition = spawn; m.hasSpawn = true;
    Scene scene = compose_scene(lib, m, kPlayer);
    Heightmap hm = build_heightmap(scene, 1.0f, false);
    const int W = hm.width, H = hm.height;
    auto idx = [&](int i, int j) { return (size_t)j * W + i; };
    std::vector<float> reach((size_t)W * H, NAN);
    const int si = (int)std::lround((spawn.x - hm.boundsMin.x) / hm.cellSize);
    const int sj = (int)std::lround((spawn.z - hm.boundsMin.z) / hm.cellSize);
    if (std::isnan(hm.heights[idx(si, sj)])) throw std::runtime_error("spawn node has no ground");
    reach[idx(si, sj)] = hm.heights[idx(si, sj)];
    std::queue<std::pair<int, int>> q;
    q.push({si, sj});
    long reached = 0;
    while (!q.empty()) {
        auto [i, j] = q.front(); q.pop(); ++reached;
        const float y0 = hm.heights[idx(i, j)];
        const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto& dd : d) {
            const int ni = i + dd[0], nj = j + dd[1];
            if (ni < 0 || nj < 0 || ni >= W || nj >= H || !std::isnan(reach[idx(ni, nj)])) continue;
            const float y1 = hm.heights[idx(ni, nj)];
            if (std::isnan(y1) || y1 - y0 > 1.0f || y0 - y1 > 1.5f) continue;  // refuseHoles (D3)
            reach[idx(ni, nj)] = y1;
            q.push({ni, nj});
        }
    }
    std::printf("plain grid %dx%d nodes @%.1f m, bounds x %.1f..%.1f z %.1f..%.1f, spawn node (%d,%d) h %.3f, reached %ld nodes\n",
                W, H, hm.cellSize, hm.boundsMin.x, hm.boundsMax.x, hm.boundsMin.z, hm.boundsMax.z, si, sj,
                hm.heights[idx(si, sj)], reached);
    dump(out, W, H, hm.boundsMin.x, hm.boundsMin.z, hm.cellSize, {&hm.heights, &reach});
    return 0;
}

int main(int argc, char** argv) {
    try {
        if (argc == 5 && !std::strcmp(argv[1], "shrine")) return shrine(argv[2], argv[3], argv[4], nullptr);
        if (argc == 8 && !std::strcmp(argv[1], "shrine")) {
            const Vec3 start{std::stof(argv[5]), std::stof(argv[6]), std::stof(argv[7])};
            return shrine(argv[2], argv[3], argv[4], &start);
        }
        if (argc >= 8 && !std::strcmp(argv[1], "plain"))
            return plain(argv[2], Vec3{std::stof(argv[3]), std::stof(argv[4]), std::stof(argv[5])}, argv[6],
                         argc - 7, argv + 7);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "reach: %s\n", e.what());
        return 1;
    }
    std::fprintf(stderr, "usage: reach shrine <lib> <manifest> <out> [sx sy sz] | reach plain <lib> <sx> <sy> <sz> <out> <glb>...\n");
    return 2;
}
