// S13 Task 10: the terrain class/owner lookup on the exported arena_04
// (`make terrain` -> $SOTC_LIB/stage/arena_04.terrain.json). Fails, never skips, without it.
#include <doctest.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "terrain.h"

namespace {

const TerrainMap& arena04() {
    static TerrainMap map;
    static bool loaded = false;
    if (!loaded) {
        const char* lib = std::getenv("SOTC_LIB");
        if (!lib) throw std::runtime_error("Set SOTC_LIB to the converted asset library.");
        std::ifstream in(std::string(lib) + "/stage/arena_04.terrain.json");
        if (!in) throw std::runtime_error("arena_04.terrain.json missing: run `make terrain`");
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string err;
        if (!terrain_load(ss.str(), map, err)) throw std::runtime_error(err);
        loaded = true;
    }
    return map;
}

}  // namespace

TEST_CASE("terrain: rejects a bad document") {
    TerrainMap m;
    std::string err;
    CHECK_FALSE(terrain_load("{\"format\":\"nope\"}", m, err));
    CHECK(err.find("sotc-terrain/0") != std::string::npos);
    CHECK_FALSE(terrain_load("{\"format\":\"sotc-terrain/0\",\"stage_tris\":[{\"v\":[[0,0,0]],\"class\":1,\"owner\":0}],\"boss_ground\":[]}", m, err));
    CHECK_FALSE(terrain_load("not json", m, err));
}

TEST_CASE("terrain: window and highest-surface rule on a synthetic tile") {
    const char* doc = R"({"format":"sotc-terrain/0","stage_tris":[
      {"v":[[0,0,0],[10,0,0],[0,0,10]],"class":3,"owner":0},
      {"v":[[0,5,0],[10,5,0],[0,5,10]],"class":4,"owner":4194304}],"boss_ground":[]})";
    TerrainMap m;
    std::string err;
    REQUIRE(terrain_load(doc, m, err));
    CHECK(terrain_at(m, {2, 0.2f, 2}).cls == 3);
    CHECK(terrain_at(m, {2, 5.2f, 2}).cls == 4);
    CHECK(terrain_at(m, {2, 5.2f, 2}).owner == 0x400000);
    CHECK_FALSE(terrain_at(m, {2, 2.5f, 2}).found);   // neither surface within [-1, +0.5]
    CHECK_FALSE(terrain_at(m, {20, 0.0f, 2}).found);  // outside the grid
    CHECK_FALSE(terrain_boss_ground(m, {2, 0.0f, 2}).found);
}

TEST_CASE("terrain: arena_04 classes under the player") {
    const TerrainMap& m = arena04();
    CHECK_FALSE(terrain_at(m, {984, 52.5f, 142}).found);  // the player start: unclassified
    CHECK(terrain_at(m, {984, 52.5f, 142}).cls == 0);

    // the tunnel stairs (a class 6..9 triangle's centroid each, measured on the export)
    struct S { float x, y, z; int cls; };
    for (const S& s : {S{1009.085f, 51.321f, 121.024f, 6}, S{971.746f, 52.033f, 120.745f, 7},
                       S{980.960f, 46.201f, 146.985f, 8}, S{999.040f, 46.201f, 146.981f, 9}}) {
        const TerrainHit h = terrain_at(m, {s.x, s.y, s.z});
        CHECK(h.found);
        CHECK(h.cls == s.cls);
        CHECK(h.y == doctest::Approx(s.y).epsilon(0.001));
    }

    // Review Focus 3: the class-4 floor under the roof answers from below only
    const TerrainHit under = terrain_at(m, {990, 44.5f, 140});
    CHECK(under.cls == 4);
    CHECK_FALSE(terrain_at(m, {990, 55.5f, 140}).found);
    CHECK(terrain_at(m, {990, 55.5f, 140}).cls == 0);

    // a hill triangle's centroid
    const TerrainHit hill = terrain_at(m, {953.595f, 45.6f, 14.880f});
    CHECK(hill.cls == 3);
    CHECK(hill.y == doctest::Approx(45.52f).epsilon(0.01));
}

TEST_CASE("terrain: arena_04 boss ground") {
    const TerrainMap& m = arena04();
    const TerrainHit a = terrain_boss_ground(m, {990, 60, 140});
    CHECK(a.found);
    CHECK(a.owner == 0x400000);
    CHECK(a.y >= 50.7f);
    CHECK(a.y <= 55.2f);
    // the platform (Phaedra's spawn): the export has owner-0 boss ground at y ~53.9 there
    const TerrainHit b = terrain_boss_ground(m, {1122, 60, 147});
    CHECK(b.found);
    CHECK(b.owner == 0);
    CHECK(b.y == doctest::Approx(53.9f).epsilon(0.01));
    CHECK_FALSE(terrain_boss_ground(m, {1122, 60, 300}).found);
    CHECK_FALSE(terrain_boss_ground(m, {990, 40, 140}).found);  // the surface (>= 50.7) is above y + 2
}
