// Phaedra wired (S13 Task 15): the pure layer of kirin_fight — her exported graph, routes and
// terrain from SOTC_LIB (`make behaviour routes terrain` into the view) and the arena_04 layered
// heightmap composed from out/re/stage/arena_04.manifest (`make arena04-manifest`) as the viewer
// builds it. A case fails, never skips, without them; nothing derived is in git.
#include <doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include "asset_io.h"
#include "kirin_fight.h"
#include "kirin_root.h"
#include "knight_fixture.h"
#include "manifest.h"
#include "scr_predicates.h"

namespace {

using knight_fixture::export_clips;
using knight_fixture::kRepoRoot;
using knight_fixture::read_file;
namespace kd = encounters::kirin_detail;

std::string lib() {
    const char* l = std::getenv("SOTC_LIB");
    if (l == nullptr || l[0] == '\0') throw std::runtime_error("Set SOTC_LIB to the library view.");
    return l;
}

// The arena's ground as stage_setup builds it for a non-Valus arena (1 m, layered, over the
// ground's own bounds); the rigs and backdrops never rasterize, so they are not loaded.
const Heightmap& arena_ground() {
    static const Heightmap h = [] {
        SceneManifest m = parse_manifest(kRepoRoot / "out/re/stage/arena_04.manifest", /*acceptEncounter=*/true);
        m.entities.clear();
        m.horses.clear();
        m.posed.clear();
        m.backdrops.clear();
        const Scene scene = compose_scene(lib(), m, "models/shadow/characters/wander/_cmn/wander.glb");
        return build_heightmap(scene, 1.0f, /*layered=*/true, /*staticBounds=*/true);
    }();
    return h;
}

// The clips' lengths: kirin_root's frames for the clips with a root series, 1 s for the rest
// (the rootless ones are never played raw; the add clips only time the layers).
std::vector<float> lengths(const std::vector<std::string>& names) {
    std::vector<float> out;
    for (const std::string& name : names) {
        float s = 1.0f;
        for (const kirin_root::RootSeries& r : kirin_root::kRoots) {
            if (name == r.clip) s = static_cast<float>(r.frames - 1) / r.fps;
        }
        out.push_back(s);
    }
    return out;
}

struct Loaded {
    KnightCore core;
    KirinState k;
};

void load(Loaded& l) {
    const std::string graph = read_file(lib() + "/behaviour/KIRIN.graph.json");
    EventsFile events;
    std::string why;
    REQUIRE_MESSAGE(events_load(lib() + "/models/shadow/characters/colossus_04_kirin/_cmn/c04_kirin.events.json",
                                events, why),
                    why);
    std::vector<std::string> judges;
    for (const EventsVolume& v : events.judgeDefaults) judges.push_back(v.name);
    for (const EventsClip& c : events.clips) {
        for (const EventsVolume& v : c.judge) judges.push_back(v.name);
    }
    const std::vector<std::string> names = export_clips(graph);
    const bool ok = kirin_core_load(l.core, l.k, graph, read_file(lib() + "/" + kd::kRouteFile),
                                    read_file(lib() + "/" + kd::kTerrainFile), names, lengths(names),
                                    events.judgeDefaults, judges, "kirin_a_stand", arena_ground(), why);
    INFO(why);
    REQUIRE(ok);
}

// Ruling §13.2's ground, computed here from the two sources: the boss-ground layer under her
// root, else the layered heightmap's floor within 2.0 m (over ground: kirin_ground refuses holes).
float ground_under(const KirinState& k, Vec3 p) {
    const TerrainHit b = terrain_boss_ground(k.terrain, p);
    return b.found ? b.y : heightmap_floor(arena_ground(), p, 2.0f);
}

}  // namespace

TEST_CASE("Phaedra loads with her routes snapped to her ground and her rootless clips refused") {
    Loaded l;
    load(l);
    CHECK(l.core.graph.modes.size() == 15);
    CHECK(l.k.addEdges.size() == 74);
    // Every node over ground sits on it (§13.2); a node over a hole (the channel, off the map's
    // edge: no ground under it for her) keeps its exported height.
    int holes = 0;
    for (const RouteGraph::Node& n : l.k.routes.nodes) {
        if (std::isnan(heightmap_node(arena_ground(), n.pos))) {
            ++holes;
            continue;
        }
        CHECK(n.pos.y == doctest::Approx(ground_under(l.k, n.pos)).epsilon(1e-4));
    }
    CHECK(holes < static_cast<int>(l.k.routes.nodes.size()) / 4);
    CHECK(l.core.rootless[static_cast<size_t>(scr_clip(l.core.graph, "kirin_a_boot"))]);
    CHECK(l.core.rootless[static_cast<size_t>(scr_clip(l.core.graph, "kirin_a_death"))]);
    CHECK_FALSE(l.core.rootless[static_cast<size_t>(scr_clip(l.core.graph, "kirin_a_walk"))]);
    // The terrain words (D8): a tunnel stair is class 7, the start is nothing; her spawn's
    // boss ground has owner 0 (R-P), the ruins 0x400000.
    CHECK(kirin_attr_word(terrain_at(l.k.terrain, {971.746f, 52.033f, 120.745f})) == 7ull << 48);
    CHECK(kirin_attr_word(terrain_at(l.k.terrain, {984.0f, 52.5f, 142.0f})) == 0u);
    CHECK(kirin_attr_word(terrain_boss_ground(l.k.terrain, {990.0f, 60.0f, 140.0f})) == 0x400000u);
}

TEST_CASE("a player in tunnel class 7 whistling brings her to route 4's GP within 60 s, on her ground") {
    Loaded l;
    load(l);
    KnightCore& c = l.core;
    KirinState& k = l.k;
    const kd::KirinArena& a = kd::kArena;
    const float yaw = std::atan2(a.faceX - a.spawnX, a.faceZ - a.spawnZ) / kDegToRad;
    kirin_core_reset(c, k, yaw);
    Vec3 root{a.spawnX, a.spawnGroundY, a.spawnZ};
    root.y = kirin_ground(k, arena_ground(), root);
    REQUIRE(std::isfinite(root.y));

    ScrWorld w;
    const auto stand = [&](Vec3 p) {
        p.y = heightmap_floor(arena_ground(), p, 2.0f);  // p.y: the layer's reference
        w.playerPosition = w.playerGroundPosition = p;
        w.playerGroundAttr = kirin_attr_word(terrain_at(k.terrain, p));
    };
    float maxOff = 0.0f, maxMs = 0.0f, sumMs = 0.0f;
    int frames = 0;
    bool outside = false;
    const auto frame = [&](bool whistle) {
        knight_world_player(w, whistle, false);
        const auto t0 = std::chrono::steady_clock::now();
        kirin_think(c, k, w, arena_ground(), root, 1.0f / 30.0f);
        const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
        maxMs = std::max(maxMs, ms);
        sumMs += ms;
        ++frames;
        outside = outside || !heightmap_in_bounds(arena_ground(), root);
        maxOff = std::max(maxOff, std::fabs(root.y - ground_under(k, root)));
    };

    // The lure: 30 m in front of her on the platform, she senses the player (patrol 3 -> attack 6).
    stand({1092.0f, 53.8f, 146.0f});
    for (int i = 0; i < 300 && c.rt.current != 6; ++i) frame(false);
    REQUIRE(c.rt.current == 6);

    // Into the west stair tunnel (class 7), whistling once a second.
    stand({971.746f, 52.033f, 120.745f});
    REQUIRE(w.playerGroundAttr == 7ull << 48);
    float arrivedAt = -1.0f;
    bool routed = false;
    for (int i = 0; i < 60 * 30 && arrivedAt < 0.0f; ++i) {
        frame(i % 30 == 0);
        routed = routed || (c.rt.current == 10 && c.rt.route.route == 4);
        if (routed && c.rt.route.gp >= 0) {
            const Vec3 gp = k.routes.gps[static_cast<size_t>(c.rt.route.gp)].pos;
            if (std::hypot(gp.x - root.x, gp.z - root.z) <= c.rt.arriveRadius) arrivedAt = i / 30.0f;
        }
    }
    MESSAGE("arrived at route 4's GP after " << arrivedAt << " s (mode " << c.rt.current << ", gp " << c.rt.route.gp
                                             << "); think " << sumMs / static_cast<float>(frames) << " ms mean, "
                                             << maxMs << " ms max over " << frames << " frames");
    CHECK(routed);
    CHECK(arrivedAt >= 0.0f);
    // Arrived (move_done), she peeps: light (11) on the ground, depth (12) on the ruins' boss ground.
    for (int i = 0; i < 30 && c.rt.current == 10; ++i) frame(false);
    MESSAGE("peeping mode " << c.rt.current << ", her ground word "
                            << static_cast<unsigned long long>(w.bossGroundAttr));
    CHECK((c.rt.current == 11 || c.rt.current == 12));
    CHECK_FALSE(outside);
    CHECK(maxOff <= 0.1f);
}
