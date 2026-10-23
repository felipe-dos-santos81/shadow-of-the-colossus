// S9 gate Step 4: raw-node marches through the road's ground and its backdrop ray grid, with the
// app's own code (heightmap_node / ray_march / build_backdrop_grid), for tools/re/s9/probes.py.
// Scratch-built against the read-only app sources (the reach.cpp approach); never part of the app.
//
//   probes <lib> <road.manifest>      then one query per stdin line:
//     S x y z                  shade march from (x,y,z) toward the sun, 300 m
//     R x y z tx ty tz len     march from (x,y,z) toward the point (tx,ty,tz), len m
//     N x z                    the raw nodes under (x,z)
//   Each answer is one line: "<ground> <backdrop> <fmin>" (t in metres or nan); N prints the
//   ground node and the backdrop node. Step 0.5 m (cellSize/2, ruling B4), unit directions.
//
// Build (from the repo root):
//   c++ -std=c++20 -O2 -Iapp/src -Iapp/third_party tools/re/s9/probes.cpp \
//     app/src/asset_io.cpp app/src/manifest.cpp app/src/heightmap.cpp app/src/anim.cpp app/src/player.cpp \
//     app/src/collision.cpp -o <scratch>/probes
#include "asset_io.h"
#include "heightmap.h"
#include "manifest.h"
#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

static const char* kPlayer = "models/shadow/characters/wander/_cmn/wander.glb";
static constexpr float kCell = 1.0f, kStep = 0.5f;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: probes <lib> <road.manifest> < queries\n");
        return 2;
    }
    try {
        const SceneManifest m = parse_manifest(argv[2]);
        const Scene scene = compose_scene(argv[1], m, kPlayer);
        const Heightmap ground = build_heightmap(scene, kCell, false);
        const Heightmap back = build_backdrop_grid(scene, kCell);
        std::fprintf(stderr, "ground %dx%d x %.1f..%.1f z %.1f..%.1f; backdrop %dx%d x %.1f..%.1f z %.1f..%.1f\n",
                     ground.width, ground.height, ground.boundsMin.x, ground.boundsMax.x, ground.boundsMin.z,
                     ground.boundsMax.z, back.width, back.height, back.boundsMin.x, back.boundsMax.x,
                     back.boundsMin.z, back.boundsMax.z);
        const Vec3 sun = vec3_normalize(Vec3{0.4f, 0.8f, 0.6f});  // mesh.metal:93 / sword.h kSunDir
        std::string line;
        while (std::getline(std::cin, line)) {
            std::istringstream in(line);
            char op = 0;
            in >> op;
            if (op == 'N') {
                float x, z;
                in >> x >> z;
                std::printf("%g %g\n", heightmap_node(ground, Vec3{x, 0, z}), heightmap_node(back, Vec3{x, 0, z}));
            } else {
                float x, y, z, len = 300.0f;
                in >> x >> y >> z;
                Vec3 dir = sun;
                if (op == 'R') {
                    float tx, ty, tz;
                    in >> tx >> ty >> tz >> len;
                    dir = vec3_normalize(Vec3{tx - x, ty - y, tz - z});
                }
                const Vec3 o{x, y, z};
                const float a = ray_march(ground, o, dir, len, kStep), b = ray_march(back, o, dir, len, kStep);
                std::printf("%g %g %g\n", a, b, std::fmin(a, b));
            }
            std::fflush(stdout);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "probes: %s\n", e.what());
        return 1;
    }
    return 0;
}
