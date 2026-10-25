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

// One run on the arena: her core reset at her spawn facing the ruins, a scripted player.
struct Run {
    Loaded l;
    KnightCore& c = l.core;
    KirinState& k = l.k;
    Vec3 root;
    ScrWorld w;
    float maxOff = 0.0f, maxMs = 0.0f, sumMs = 0.0f;
    int frames = 0;
    bool outside = false;

    Run() {
        load(l);
        const kd::KirinArena& a = kd::kArena;
        kirin_core_reset(c, k, std::atan2(a.faceX - a.spawnX, a.faceZ - a.spawnZ) / kDegToRad);
        root = {a.spawnX, a.spawnGroundY, a.spawnZ};
        root.y = kirin_ground(k, arena_ground(), root);
        REQUIRE(std::isfinite(root.y));
    }
    Run(const Run&) = delete;
    // The player on the layered floor at `p` (p.y: the layer's reference), the terrain word under him.
    void stand(Vec3 p) {
        p.y = heightmap_floor(arena_ground(), p, 2.0f);
        w.playerPosition = w.playerGroundPosition = p;
        w.playerGroundAttr = kirin_attr_word(terrain_at(k.terrain, p));
    }
    void frame(bool whistle = false, float dt = 1.0f / 30.0f) {
        knight_world_player(w, whistle, false);
        const auto t0 = std::chrono::steady_clock::now();
        kirin_think(c, k, w, arena_ground(), root, dt);
        const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
        maxMs = std::max(maxMs, ms);
        sumMs += ms;
        ++frames;
        outside = outside || !heightmap_in_bounds(arena_ground(), root);
        maxOff = std::max(maxOff, std::fabs(root.y - ground_under(k, root)));
    }
    // The lure: 30 m in front of her on the platform, she senses the player (patrol 3 -> attack 6).
    void lure() {
        stand({1092.0f, 53.8f, 146.0f});
        for (int i = 0; i < 300 && c.rt.current != 6; ++i) frame();
        REQUIRE(c.rt.current == 6);
    }
};

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

TEST_CASE("Phaedra loads from her glb's own clip names: a foreign clip a shared control-def names never plays") {
    // The viewer passes the glb's clips (her kirin_a_* only); KIRIN's graph also names
    // knight_a_atk1_start (COMMON_BOSS_CONTROL:control-def[102], a "not playing" clause).
    const std::string graph = read_file(lib() + "/behaviour/KIRIN.graph.json");
    std::vector<std::string> names;
    for (const std::string& n : export_clips(graph)) {
        if (n.rfind("kirin_a_", 0) == 0) names.push_back(n);
    }
    EventsFile events;
    std::string why;
    REQUIRE(events_load(lib() + "/models/shadow/characters/colossus_04_kirin/_cmn/c04_kirin.events.json", events,
                        why));
    std::vector<std::string> judges;
    for (const EventsVolume& v : events.judgeDefaults) judges.push_back(v.name);
    for (const EventsClip& c : events.clips) {
        for (const EventsVolume& v : c.judge) judges.push_back(v.name);
    }
    Loaded l;
    const bool ok = kirin_core_load(l.core, l.k, graph, read_file(lib() + "/" + kd::kRouteFile),
                                    read_file(lib() + "/" + kd::kTerrainFile), names, lengths(names),
                                    events.judgeDefaults, judges, "kirin_a_stand", arena_ground(), why);
    INFO(why);
    REQUIRE(ok);
    const int foreign = scr_clip(l.core.graph, "knight_a_atk1_start");
    REQUIRE(foreign >= static_cast<int>(names.size()));  // after her own: never a playback index
    CHECK(l.core.rootless[static_cast<size_t>(foreign)]);
    CHECK(l.core.seconds.size() == l.core.graph.clipNames.size());
}

TEST_CASE("a kirin_a_* clip her glb lacks is still refused (a stale bake never loads silently)") {
    const std::string graph = read_file(lib() + "/behaviour/KIRIN.graph.json");
    std::vector<std::string> names;
    for (const std::string& n : export_clips(graph)) {
        if (n.rfind("kirin_a_", 0) == 0 && n != "kirin_a_add_neck_refuse_loop") names.push_back(n);
    }
    EventsFile events;
    std::string why;
    REQUIRE(events_load(lib() + "/models/shadow/characters/colossus_04_kirin/_cmn/c04_kirin.events.json", events,
                        why));
    std::vector<std::string> judges;
    for (const EventsVolume& v : events.judgeDefaults) judges.push_back(v.name);
    for (const EventsClip& c : events.clips) {
        for (const EventsVolume& v : c.judge) judges.push_back(v.name);
    }
    Loaded l;
    CHECK_FALSE(kirin_core_load(l.core, l.k, graph, read_file(lib() + "/" + kd::kRouteFile),
                                read_file(lib() + "/" + kd::kTerrainFile), names, lengths(names),
                                events.judgeDefaults, judges, "kirin_a_stand", arena_ground(), why));
    CHECK(why.find("kirin_a_add_neck_refuse_loop") != std::string::npos);
}

TEST_CASE("a player in tunnel class 7 whistling brings her to route 4's GP within 60 s, on her ground") {
    Run r;
    KnightCore& c = r.c;
    r.lure();
    // Into the west stair tunnel (class 7), whistling once a second.
    r.stand({971.746f, 52.033f, 120.745f});
    REQUIRE(r.w.playerGroundAttr == 7ull << 48);
    float arrivedAt = -1.0f;
    bool routed = false;
    for (int i = 0; i < 60 * 30 && arrivedAt < 0.0f; ++i) {
        r.frame(i % 30 == 0);
        routed = routed || (c.rt.current == 10 && c.rt.route.route == 4);
        if (routed && c.rt.route.gp >= 0) {
            const Vec3 gp = r.k.routes.gps[static_cast<size_t>(c.rt.route.gp)].pos;
            if (std::hypot(gp.x - r.root.x, gp.z - r.root.z) <= c.rt.arriveRadius) arrivedAt = i / 30.0f;
        }
    }
    MESSAGE("arrived at route 4's GP after " << arrivedAt << " s (mode " << c.rt.current << ", gp " << c.rt.route.gp
                                             << "); think " << r.sumMs / static_cast<float>(r.frames) << " ms mean, "
                                             << r.maxMs << " ms max over " << r.frames << " frames");
    CHECK(routed);
    CHECK(arrivedAt >= 0.0f);
    // Arrived (move_done), she peeps: light (11) on the ground, depth (12) on the ruins' boss ground.
    for (int i = 0; i < 30 && c.rt.current == 10; ++i) r.frame();
    MESSAGE("peeping mode " << c.rt.current << ", her ground word "
                            << static_cast<unsigned long long>(r.w.bossGroundAttr));
    CHECK((c.rt.current == 11 || c.rt.current == 12));
    CHECK_FALSE(r.outside);
    CHECK(r.maxOff <= 0.1f);
}

TEST_CASE("R (the core reset) reopens the hill links patrol closed") {
    Run r;
    r.stand({1092.0f, 53.8f, 300.0f});  // out of her sight: she patrols
    for (int i = 0; i < 30 && r.c.rt.current != 3; ++i) r.frame();
    REQUIRE(r.c.rt.current == 3);
    // Patrol's entry (KIRIN:F69, link_set tag 1) closed every tag-1 link (data: links 26-34, ...).
    const auto closed = [&r](size_t li) {
        const std::vector<int>& live = r.c.rt.route.linkFlags;
        const int f = live.size() == r.k.routes.links.size() ? live[li] : r.k.routes.links[li].flags;
        return (f & 4) != 0;
    };
    std::vector<size_t> hill;
    for (size_t i = 0; i < r.k.routes.links.size(); ++i) {
        const RouteGraph::Link& l = r.k.routes.links[i];
        if (l.tags[0] == 1 || l.tags[1] == 1) hill.push_back(i);
    }
    REQUIRE_FALSE(hill.empty());
    for (const size_t li : hill) CHECK(closed(li));
    kirin_core_reset(r.c, r.k, 0.0f);  // kirin_fight_reset's R runs the same knight_core_reset
    for (const size_t li : hill) CHECK_FALSE(closed(li));
}

TEST_CASE("no additive request at dt 0; the same frame at 1/30 s requests the neck-down layer") {
    Run r;
    r.lure();
    const int neck = scr_clip(r.c.graph, "kirin_a_add_neck_down_1_start");
    REQUIRE(neck >= 0);
    const auto playing = [&r](int clip) { return additive_playing(r.k.layers, clip); };
    REQUIRE_FALSE(playing(neck));
    // A sword hit on the neck trigger (region 0x20): the graph's neck-down rows fire.
    r.w.hit = true;
    r.w.hitRegion = 0x20;
    r.w.hitKind = kd::kTriggerHitKind;
    const AddLayers before = r.k.layers;
    r.frame(false, 0.0f);
    CHECK_FALSE(r.k.orients.empty());  // the add rows fired ...
    CHECK_FALSE(playing(neck));        // ... and nothing was requested or stepped (the binary returns early)
    for (size_t i = 0; i < before.slot.size(); ++i) {
        CHECK(r.k.layers.slot[i].state == before.slot[i].state);
        CHECK(r.k.layers.slot[i].frame == before.slot[i].frame);
    }
    r.frame(false, 1.0f / 30.0f);
    CHECK_FALSE(r.k.orients.empty());
    CHECK(playing(neck));
}

TEST_CASE("HP 0: the mortality row takes her rootless death clip, the graph reaches its death mode") {
    Run r;
    r.lure();
    const int death = scr_clip(r.c.graph, "kirin_a_death");
    REQUIRE(death >= 0);
    REQUIRE(r.c.rootless[static_cast<size_t>(death)]);  // never raw in play (kNoRoot)
    r.w.hp = 0.0f;
    // A plain think refuses the rootless clip: the control-mode never reaches the death's, no forced death.
    {
        Run plain;
        plain.lure();
        plain.w.hp = 0.0f;
        for (int i = 0; i < 3; ++i) kirin_think(plain.c, plain.k, plain.w, arena_ground(), plain.root, 0.0f);
        CHECK(plain.c.rt.current != plain.c.graph.root.deathMode);
    }
    for (int i = 0; i < 3 && r.c.rt.current != r.c.graph.root.deathMode; ++i) {
        kirin_death_think(r.c, r.k, r.w, arena_ground(), r.root, death);
    }
    CHECK(r.c.rt.current == r.c.graph.root.deathMode);
    CHECK(r.c.rootless[static_cast<size_t>(death)]);  // restored
}

TEST_CASE("the shake-off drain (R-AH): a base refuse clip inside its window, an additive swing in a layer") {
    KirinFight f;
    Loaded l;
    load(l);
    f.base.core = l.core;
    KnightCore& c = f.base.core;
    const auto drain = [&f] { return kirin_fight_shake(f).staminaDrain; };
    c.motion.clip = scr_clip(c.graph, "kirin_a_refuse_tail");
    c.motion.time = 3.0f;
    CHECK(drain() == doctest::Approx(20.0f));  // measured: kd::kShakes, active 2.20..4.93 s
    c.motion.time = 1.0f;
    CHECK(drain() == 0.0f);  // before its window
    c.motion.clip = scr_clip(c.graph, "kirin_a_stand");
    CHECK(drain() == 0.0f);
    f.state.layers.slot[1].clip = scr_clip(c.graph, "kirin_a_add_head_refuse");
    f.state.layers.slot[1].state = AddLayer::Playing;
    f.state.layers.slot[1].frame = 30.0f;  // 1 s on the layer clock
    CHECK(drain() == doctest::Approx(23.5f));
    CHECK_FALSE(kirin_fight_shake(f).forceRelease);
}

TEST_CASE("the death's root drop: kDeathRootY over the death clip, its end once death_loop holds") {
    const kirin_root::DeathRoot& d = kd::kDeathRootY;
    CHECK(kirin_death_lift(false, 0.0f, 7.0f) == doctest::Approx(d.kFrom));
    CHECK(kirin_death_lift(false, 3.5f, 7.0f) == doctest::Approx((d.kFrom + d.kTo) * 0.5f));
    CHECK(kirin_death_lift(false, 7.0f, 7.0f) == doctest::Approx(d.kTo));
    // After the swap death_loop plays at t 0 and never advances: the lift stays at the rest.
    CHECK(kirin_death_lift(true, 0.0f, 7.0f) == doctest::Approx(d.kTo));
}

TEST_CASE("the rockfall feed (R-AJ): a stomp feeds while armed, a walk footstep never, capped at 5 s") {
    CHECK(kirin_rockfall_feed(0.0f, "KIRIN_FOOT_ATK", true) == doctest::Approx(kd::kRockfallFeedSeconds));
    CHECK(kirin_rockfall_feed(1.0f, "KIRIN_FOOT_ATK_ST", true) == doctest::Approx(1.5f));
    CHECK(kirin_rockfall_feed(0.0f, "KIRIN_FOOT_ATK", false) == 0.0f);  // disarmed
    for (const char* walk : {"KIRIN_FOOT", "KIRIN_FOOT_B", "KIRIN_WAIT_FOOT", "KIRIN_DEATH"}) {
        CHECK(kirin_rockfall_feed(0.0f, walk, true) == 0.0f);
    }
    CHECK(kirin_rockfall_feed(4.8f, "KIRIN_FOOT_ATK", true) == doctest::Approx(kd::kRockfallCapSeconds));
}
