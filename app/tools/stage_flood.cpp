// The layered ground of a stage manifest, flooded from given points (S13, the arena_04 gate:
// `probe.py check-manifest --stage arena_04`). No SDL. It composes the manifest's ground the
// way the viewer does for a non-Valus arena (compose_scene, then build_heightmap: 1 m, layered,
// over the ground's own bounds — stage_setup.cpp) and floods every start over node-to-node
// moves that heightmap_walkable allows (8 neighbours, the layer a body on the source layer
// steps onto). It prints the grid on stdout and writes the arrays for the gate to read:
//
//   stage_flood <library> <manifest> <playerGlb> <out.bin> <x> <y> <z> [<x> <y> <z> ...]
//
// out.bin: "SFL1", int32 width, height, maxLayers, starts, float32 minX, minZ, cell; then
// float32 layers[height][width][maxLayers] (NaN-padded), uint8 blocked[same], and per start
// uint64 reach[height][width] (bit k = layer k reached).
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "asset_io.h"
#include "heightmap.h"
#include "manifest.h"

namespace {

// The layer a body at `y` stands on at node (i, j) (heightmap.cpp's select_layer).
int select_layer(const Heightmap& h, int i, int j, float y) {
    const float* node = &h.layers[(static_cast<std::size_t>(j) * h.width + i) * h.maxLayers];
    int best = -1;
    for (int k = 0; k < h.maxLayers && !std::isnan(node[k]); ++k) {
        if (best < 0 || node[k] <= y + kFloorStep) best = k;
    }
    return best;
}

std::vector<uint64_t> flood(const Heightmap& h, Vec3 start) {
    const int W = h.width, H = h.height, L = h.maxLayers;
    std::vector<uint64_t> reach(static_cast<std::size_t>(W) * H, 0);
    const int si = static_cast<int>(std::floor((start.x - h.boundsMin.x) / h.cellSize + 0.5f));
    const int sj = static_cast<int>(std::floor((start.z - h.boundsMin.z) / h.cellSize + 0.5f));
    if (si < 0 || sj < 0 || si >= W || sj >= H) return reach;
    const int sk = select_layer(h, si, sj, start.y);
    if (sk < 0) return reach;
    std::vector<std::array<int, 3>> stack{{si, sj, sk}};
    reach[static_cast<std::size_t>(sj) * W + si] |= 1ull << sk;
    while (!stack.empty()) {
        const auto [i, j, k] = stack.back();
        stack.pop_back();
        const float y = h.layers[(static_cast<std::size_t>(j) * W + i) * L + k];
        const Vec3 from{h.boundsMin.x + i * h.cellSize, y, h.boundsMin.z + j * h.cellSize};
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                const int ni = i + di, nj = j + dj;
                if ((di == 0 && dj == 0) || ni < 0 || nj < 0 || ni >= W || nj >= H) continue;
                const Vec3 to{h.boundsMin.x + ni * h.cellSize, y, h.boundsMin.z + nj * h.cellSize};
                if (!heightmap_walkable(h, from, to)) continue;
                const int nk = select_layer(h, ni, nj, y);
                if (nk < 0) continue;
                uint64_t& at = reach[static_cast<std::size_t>(nj) * W + ni];
                if (at >> nk & 1) continue;
                at |= 1ull << nk;
                stack.push_back({ni, nj, nk});
            }
        }
    }
    return reach;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 8 || (argc - 5) % 3 != 0) {
        std::fprintf(stderr,
                     "usage: stage_flood <library> <manifest> <playerGlb> <out.bin> <x> <y> <z> "
                     "[<x> <y> <z> ...]\n");
        return 2;
    }
    try {
        SceneManifest manifest = parse_manifest(argv[2], /*acceptEncounter=*/true);
        // Only the ground builds the map (staticBounds: the ground's own extent); the
        // rigs and the backdrops never rasterize, so they are not loaded.
        manifest.entities.clear();
        manifest.horses.clear();
        manifest.posed.clear();
        manifest.backdrops.clear();
        const Scene scene = compose_scene(argv[1], manifest, argv[3]);
        const Heightmap h = build_heightmap(scene, 1.0f, /*layered=*/true, /*staticBounds=*/true);

        std::vector<std::vector<uint64_t>> reaches;
        for (int a = 5; a + 2 < argc; a += 3) {
            reaches.push_back(flood(h, Vec3{static_cast<float>(std::atof(argv[a])),
                                            static_cast<float>(std::atof(argv[a + 1])),
                                            static_cast<float>(std::atof(argv[a + 2]))}));
        }
        std::ofstream out(argv[4], std::ios::binary);
        const int32_t ints[4] = {h.width, h.height, h.maxLayers, static_cast<int32_t>(reaches.size())};
        const float floats[3] = {h.boundsMin.x, h.boundsMin.z, h.cellSize};
        out.write("SFL1", 4);
        out.write(reinterpret_cast<const char*>(ints), sizeof ints);
        out.write(reinterpret_cast<const char*>(floats), sizeof floats);
        out.write(reinterpret_cast<const char*>(h.layers.data()),
                  static_cast<std::streamsize>(h.layers.size() * sizeof(float)));
        out.write(reinterpret_cast<const char*>(h.blocked.data()),
                  static_cast<std::streamsize>(h.blocked.size()));
        for (const auto& reach : reaches) {
            out.write(reinterpret_cast<const char*>(reach.data()),
                      static_cast<std::streamsize>(reach.size() * sizeof(uint64_t)));
        }
        if (!out) throw std::runtime_error(std::string("cannot write ") + argv[4]);
        const std::size_t bytes = h.heights.size() * 4 + h.layers.size() * 4 + h.blocked.size();
        std::printf("stage_flood: grid %dx%d layers %d bounds x[%.3f,%.3f] z[%.3f,%.3f] bytes %zu\n",
                    h.width, h.height, h.maxLayers, h.boundsMin.x, h.boundsMax.x, h.boundsMin.z,
                    h.boundsMax.z, bytes);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "stage_flood: %s\n", error.what());
        return 1;
    }
    return 0;
}
