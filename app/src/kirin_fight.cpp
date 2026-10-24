#include "kirin_fight.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>

#include "kirin_root.h"
#include "scr_kirin.h"
#include "scr_library.h"

namespace kd = encounters::kirin_detail;

namespace {

// A per-frame table at clip time `t` (frame = t · fps), linear between frames, clamped
// (knight_fight's sampling, on her tables).
template <typename F>
auto sample(int frames, float fps, float t, F at) {
    const float f = std::clamp(std::isfinite(t) ? t * fps : 0.0f, 0.0f, static_cast<float>(frames - 1));
    const int i = std::min(static_cast<int>(f), frames - 1);
    const int j = std::min(i + 1, frames - 1);
    const float u = f - static_cast<float>(i);
    return at(i) * (1.0f - u) + at(j) * u;
}

// Her clip's root (x, z) in its clip-start frame at t (kirin_root.h); 0 without a series.
Vec3 root_at(std::string_view clip, float t) {
    for (const kirin_root::RootSeries& r : kirin_root::kRoots) {
        if (clip != r.clip || r.frames < 1) continue;
        return {sample(r.frames, r.fps, t, [&r](int k) { return r.xz[k][0]; }), 0.0f,
                sample(r.frames, r.fps, t, [&r](int k) { return r.xz[k][1]; })};
    }
    return {};
}

// Her turn clip's heading at t (+ = left); 0 for every other clip.
float turn_at(std::string_view clip, float t) {
    for (const kirin_root::TurnHeading& h : kirin_root::kTurnHeading) {
        if (clip == h.clip) return sample(h.frames, h.fps, t, [&h](int k) { return h.deg[k]; });
    }
    return 0.0f;
}

std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

// Every height of her route graph on her ground (§13.2: the nodes sit 1.3–8.8 m up; a clear
// test from a raw node would start in the air).
void snap_routes(KirinState& k, const Heightmap& ground) {
    const auto snap = [&](Vec3& p) {
        const float y = kirin_ground(k, ground, p);
        if (std::isfinite(y)) p.y = y;
    };
    for (RouteGraph::Node& n : k.routes.nodes) snap(n.pos);
    for (RouteGraph::Route& r : k.routes.routes) snap(r.pos);
    for (RouteGraph::Gp& g : k.routes.gps) snap(g.pos);
    for (RouteGraph::Point& p : k.routes.points) snap(p.pos);
}

}  // namespace

uint64_t kirin_attr_word(const TerrainHit& hit) {
    return hit.found ? (static_cast<uint64_t>(hit.cls) << 48) | hit.owner : 0u;
}

float kirin_ground(const KirinState& k, const Heightmap& ground, Vec3 p) {
    const TerrainHit boss = terrain_boss_ground(k.terrain, p);
    if (boss.found) return boss.y;
    // A hole (the nearest node empty: water, the channel) or the outside is no ground; this also
    // keeps heightmap_floor off its whole-grid fallback (measured: 13 ms frames).
    if (std::isnan(heightmap_node(ground, p))) return std::numeric_limits<float>::quiet_NaN();
    return heightmap_floor(ground, p, kd::kGroundStep);
}

bool kirin_walkable(const KirinState& k, const Heightmap& ground, Vec3 from, Vec3 to, Vec3& stop) {
    stop = from;
    float y = kirin_ground(k, ground, from);
    if (!std::isfinite(y) || !heightmap_in_bounds(ground, from)) return false;
    stop.y = y;
    const float d = std::hypot(to.x - from.x, to.z - from.z);
    const int n = std::max(1, static_cast<int>(std::ceil(d / kd::kWalkSample)));
    for (int i = 1; i <= n; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(n);
        const Vec3 p{from.x + (to.x - from.x) * u, y, from.z + (to.z - from.z) * u};
        if (!heightmap_in_bounds(ground, p)) return false;
        const float g = kirin_ground(k, ground, p);
        if (!std::isfinite(g) || std::fabs(g - y) > kd::kGroundStep) return false;
        y = g;
        stop = {p.x, g, p.z};
    }
    return true;
}

bool kirin_core_load(KnightCore& core, KirinState& k, const std::string& graphJson,
                     const std::string& routeJson, const std::string& terrainJson,
                     const std::vector<std::string>& clipNames, const std::vector<float>& seconds,
                     const std::vector<EventsVolume>& judgeDefaults,
                     const std::vector<std::string>& judgeNames, const std::string& standClip,
                     const Heightmap& ground, std::string& why) {
    if (seconds.size() != clipNames.size()) {
        why = "one length per clip name";
        return false;
    }
    if (!route_load(routeJson, k.routes, why) || !terrain_load(terrainJson, k.terrain, why)) return false;
    core.registry = scr_registry_kirin();
    // The load order scr_kirin.h names: the two binds before the check (it refuses either unbound).
    if (!scr_graph_load(graphJson, clipNames, core.registry, core.graph, why) ||
        !scr_attack_attain(core.graph, judgeDefaults, why) || !scr_move_back_bind(core.graph, judgeDefaults, why) ||
        !scr_kirin_check(core.graph, &k.routes, judgeNames, why)) {
        return false;
    }
    k.addEdges = scr_additive_edges(core.graph);
    core.seconds = seconds;
    core.rootless.assign(clipNames.size(), 0);
    for (size_t i = 0; i < clipNames.size(); ++i) {
        for (const char* name : kirin_root::kNoRoot) core.rootless[i] |= clipNames[i] == name;
        for (const char* name : kirin_root::kUnusable) core.rootless[i] |= clipNames[i] == name;
    }
    core.rootAt = root_at;
    core.turnAt = turn_at;
    core.blendFps = kd::kBlendFps;
    core.boundRadius = 0.0f;  // D9: no authored clamp — her walks are the nav graph's
    core.standClip = scr_clip(core.graph, standClip);
    if (core.standClip < 0 || core.rootless[static_cast<size_t>(core.standClip)]) {
        why = "the stand clip '" + standClip + "' is not a rooted clip of the graph";
        return false;
    }
    if (knight_else_edge(core.graph, core.standClip, core.standControlMode) < 0) {
        why = "the graph has no else edge out of '" + standClip + "' (its control-mode)";
        return false;
    }
    snap_routes(k, ground);
    return true;
}

void kirin_core_reset(KnightCore& core, KirinState& k, float yawDeg) {
    knight_core_reset(core, yawDeg);  // the runtime replaced: its RouteState too (closed links reopen)
    k.layers = AddLayers{};
    k.orients.clear();
}

KnightThink kirin_think(KnightCore& c, KirinState& k, ScrWorld& w, const Heightmap& ground, Vec3& root,
                        float dt) {
    if (!(dt >= 0.0f) || !std::isfinite(dt)) dt = 0.0f;
    w.bossPosition = root;
    w.bossOnGround = true;  // authored: her root follows her ground every frame
    w.bossGroundAttr = kirin_attr_word(terrain_boss_ground(k.terrain, root));
    w.routes = &k.routes;
    // Op 0x56 / boss_motion_in_layers: the base motion or any layer (Task 14's by-clip reading).
    w.bossLayerPlaying = [&c, &k](int clip) { return clip == c.motion.clip || additive_playing(k.layers, clip); };
    w.groundAt = [&k, &ground, y = root.y](float x, float z, float& h) {
        h = kirin_ground(k, ground, Vec3{x, y, z});
        return std::isfinite(h);
    };
    // authored: ScrWorld::ray's two readers — the sight ray and the nav walk's clear test — share
    // one test, "her ground is walkable between" (the arena's ruins are walls and roofs to her).
    w.ray = [&k, &ground](const Vec3& a, const Vec3& b, Vec3& hit) { return !kirin_walkable(k, ground, a, b, hit); };

    const KnightThink t = knight_think(c, w, dt);
    // The additive layers in the game's order (scr_kirin.h): the add rows see the layers before
    // they step, then the requests; at dt 0 nothing (the binary returns early).
    k.orients = scr_control_additive(c.graph, c.rt, t.step.controls, w);
    if (dt > 0.0f) {
        additive_step(k.layers, c.seconds, dt);
        for (const std::string& o : k.orients) additive_request(k.layers, k.addEdges, o);
    }
    // Her root over her ground; a step onto no ground, or one rising or sinking more than
    // kGroundStep, is refused (authored: a wall or a drop holds her).
    const Vec3 to{root.x + t.travel.x, root.y, root.z + t.travel.z};
    if (heightmap_in_bounds(ground, to)) {
        const float y = kirin_ground(k, ground, to);
        if (std::isfinite(y) && std::fabs(y - root.y) <= kd::kGroundStep) root = {to.x, y, to.z};
    }
    return t;
}

// --- The scene's side ----------------------------------------------------------

bool kirin_fight_load(KirinFight& f, const Scene& scene, std::size_t entityRig, const Playback& entity,
                      const Heightmap& ground, const std::string& libraryDir, const std::string& entityGlb,
                      const std::string& standClip, Vec3 placement, Vec3 playerStart, std::string& why) {
    // kArena vs the composed manifest (the same values live in probe.py STAGES["arena_04"]).
    const kd::KirinArena& a = kd::kArena;
    if (std::fabs(placement.x - a.spawnX) > 1e-3f || std::fabs(placement.z - a.spawnZ) > 1e-3f ||
        std::fabs(placement.y - a.spawnGroundY) > 1e-3f) {
        why = "the manifest places her off kArena's spawn (re-compose arena_04 or fix kirin.h)";
        return false;
    }
    if (std::fabs(playerStart.x - a.playerStartX) > 1e-3f || std::fabs(playerStart.z - a.playerStartZ) > 1e-3f) {
        why = "the manifest's player start is off kArena's (re-compose arena_04 or fix kirin.h)";
        return false;
    }
    std::string text[3];
    const std::string paths[3] = {libraryDir + "/behaviour/" + kd::kGraphBoss + ".graph.json",
                                  libraryDir + "/" + kd::kRouteFile, libraryDir + "/" + kd::kTerrainFile};
    for (int i = 0; i < 3; ++i) {
        text[i] = read_text(paths[i]);
        if (text[i].empty()) {
            why = paths[i] + " is missing or empty (`make behaviour routes terrain` into SOTC_LIB)";
            return false;
        }
    }
    KnightFight& b = f.base;
    std::filesystem::path sidecar = std::filesystem::path(libraryDir) / entityGlb;
    sidecar.replace_extension(".events.json");
    if (!events_load(sidecar.string(), b.events, why)) return false;
    std::vector<std::string> names, judges;
    std::vector<float> seconds;
    for (const AnimationCpu& clip : entity.clips) {
        names.push_back(clip.name);
        seconds.push_back(clip.duration);
    }
    for (const EventsVolume& v : b.events.judgeDefaults) judges.push_back(v.name);
    for (const EventsClip& c : b.events.clips) {
        for (const EventsVolume& v : c.judge) judges.push_back(v.name);
    }
    // The graph's clip indices are the playback's: scr_graph_load keeps `names`' order.
    if (!kirin_core_load(b.core, f.k, text[0], text[1], text[2], names, seconds, b.events.judgeDefaults, judges,
                         standClip, ground, why)) {
        return false;
    }
    b.eventsClip.clear();
    for (const std::string& n : names) b.eventsClip.push_back(events_clip(b.events, n));
    b.deathClip = scr_clip(b.core.graph, encounters::kKirin.death.clip);
    f.deathLoopClip = scr_clip(b.core.graph, kd::kDeathLoopClip);
    if (b.deathClip < 0 || f.deathLoopClip < 0) {
        why = std::string("her glb lacks ") + encounters::kKirin.death.clip + " or " + kd::kDeathLoopClip;
        return false;
    }
    const RigCpu& rig = rig_at(scene, entityRig);
    b.jointNames.clear();
    b.optBones.clear();
    for (const JointCpu& j : rig.skin.joints) {
        b.jointNames.push_back(j.name);
        if (j.name.rfind("OptBone_", 0) == 0) b.optBones.push_back(j.name);
    }
    if (!knight_events_check(b.events, b.jointNames, b.optBones, why)) return false;
    b.hipJoint = static_cast<int>(std::find(b.jointNames.begin(), b.jointNames.end(), kd::kHipJoint) -
                                  b.jointNames.begin());
    if (b.hipJoint >= static_cast<int>(b.jointNames.size())) {
        why = std::string("her rig has no ") + kd::kHipJoint;
        return false;
    }
    return true;
}

void kirin_fight_reset(KirinFight& f, Encounter& encounter, Playback& entity) {
    knight_fight_reset(f.base, encounter, entity);  // the core from the retry start, the stand
    f.k.layers = AddLayers{};
    f.k.orients.clear();
    f.spent = {};
    f.struckKind = 0;
    f.rockfallFed = 0.0f;
    encounter.entityLift = f.baseLift;
}

KnightThink kirin_fight_step(KirinFight& f, Encounter& encounter, Playback& entity, const Heightmap& ground,
                             const PlayerState& player, const PlayerCombat& combat, bool whistleHeld, float dt) {
    KnightFight& b = f.base;
    KnightCore& c = b.core;
    KnightMotion& m = c.motion;
    if (!(dt >= 0.0f) || !std::isfinite(dt)) dt = 0.0f;
    b.judgeFired.clear();

    // The player's side of the world.
    ScrWorld w;
    w.playerPosition = player.position;
    w.playerGroundPosition = player.position;
    const float floor = heightmap_floor(ground, player.position, kd::kGroundStep);
    if (std::isfinite(floor)) w.playerGroundPosition.y = floor;
    w.playerAttached = mode_is_attached(combat.mode);
    w.playerAttachedRegion =
        w.playerAttached && combat.band >= 0 && combat.band < static_cast<int>(kd::kBandRegion.size())
            ? kd::kBandRegion[static_cast<size_t>(static_cast<int>(combat.band))]
            : 0;
    // D8: the terrain class and owner under the player on foot (data, arena_04.terrain.json).
    w.playerGroundAttr = !w.playerAttached && combat.mode != PlayerMode::Falling
                             ? kirin_attr_word(terrain_at(f.k.terrain, player.position))
                             : 0u;
    // The S8 whistle (a press) and the player's death: the PLAYER tables' indices (Gaius's R-N).
    knight_world_player(w, whistleHeld && !b.whistleBefore, combat.mode == PlayerMode::Dead);
    b.whistleBefore = whistleHeld;
    w.hit = b.struck;
    w.hitRegion = b.struckRegion;
    w.hitKind = b.struck ? f.struckKind : -1;
    b.struck = false;
    w.hp = encounter.defeated ? 0.0f : encounter.health;
    w.hpMax = encounter.desc != nullptr ? encounter.desc->maxHealth : encounters::kKirin.maxHealth;
    // The attack runner's judge volumes, on last frame's pose (ruling R-B).
    const PlayerView pv = knight_player_view(player, combat);
    w.playerInJudge = [&b, pv](std::string_view name) {
        if (!b.havePose) return false;
        const bool in = events_player_in_judge(b.events, name, b.evNow, knight_pose_view(b), pv);
        if (in) b.judgeFired.emplace_back(name);
        return in;
    };

    Vec3 root = encounter.entityPosition;
    KnightThink t;
    if (encounter.defeated) {
        if (b.deathMode < 0) {
            // D12: the graph's death once — the mortality row takes the death control-mode, in
            // which the walker forces mode 5 — two thinks at no time (the cap: those two + one spare,
            // authored as Gaius's); then the death sequence owns the clip.
            for (int i = 0; i < 3 && c.rt.current != c.graph.root.deathMode; ++i) {
                t = kirin_think(c, f.k, w, ground, root, 0.0f);
            }
            b.deathMode = c.rt.current;
            b.deathGraphClip = m.clip;
            m.previous = m.clip;
            m.clip = b.deathClip;
            m.held = false;
            m.blendElapsed = -1.0f;
            m.instance += 1;
        }
        // The death clip has no root series: no travel; her root drops kDeathRootY over it
        // (measured; linear in time: authored), then death_loop (its held pose) is held.
        const float len = c.seconds[static_cast<size_t>(b.deathClip)];
        const float u = len > 0.0f ? std::clamp(entity.time / len, 0.0f, 1.0f) : 1.0f;
        encounter.entityLift = f.baseLift + kd::kDeathRootY.kFrom + (kd::kDeathRootY.kTo - kd::kDeathRootY.kFrom) * u;
        if (entity.index == b.deathClip && entity.time >= std::min(len, encounters::kKirin.death.motionSeconds)) {
            entity.index = f.deathLoopClip;
            entity.time = 0.0f;
        }
        m.time = std::min(entity.time, len);
    } else {
        t = kirin_think(c, f.k, w, ground, root, dt);
    }
    encounter.entityPosition = root;
    encounter.entityYawDeg = c.yawDeg;
    knight_fight_sync(b, entity, encounter.defeated, t);
    return t;
}

void kirin_fight_pose(KirinFight& f, const Scene& scene, std::size_t entityRig, const Playback& entity, float dt,
                      std::vector<Mat4>& palette) {
    knight_fight_pose(f.base, scene, entityRig, entity, dt, palette, &f.k.layers);
}

std::vector<DamageHit> kirin_fight_events(KirinFight& f, PlayerState& player, PlayerCombat& combat,
                                          const CombatTuning& tuning, float dt, StepEvents* events) {
    KnightFight& b = f.base;
    std::vector<DamageHit> landed =
        knight_fight_events(b, player, combat, tuning, dt, events, kd::kHeavyReaction, kd::kNoThrowOffReaction);
    // The rockfall feed: each ground-destruction spawn fired this frame (an effect that spawns
    // an objanim; data reading of KIRIN_GROUND_DEST_LWS) while armed.
    ScrRuntime& rt = b.core.rt;
    if (b.havePose && (rt.bossBits & kScrKirinRockfallArmed) != 0) {
        for (const EventFire& e : events_fired(b.events, b.evNow, b.evPrev, knight_pose_view(b))) {
            const EventsClip& clip = b.events.clips[static_cast<size_t>(e.clip)];
            if (clip.effects[static_cast<size_t>(e.row)].spawns.empty()) continue;
            const float before = rt.bossTimer[0];
            rt.bossTimer[0] = std::min(kd::kRockfallCapSeconds, before + kd::kRockfallFeedSeconds);
            f.rockfallFed += rt.bossTimer[0] - before;
        }
    }
    return landed;
}

KnightHit kirin_fight_hit(KirinFight& f, Encounter& encounter, Playback& entity, const EntityClips& clips,
                          int weakpoint, float damage) {
    KnightHit h;
    h.weakpoint = weakpoint;
    if (encounter.defeated || !(damage > 0.0f) || weakpoint < 0 || weakpoint >= kd::W::Count) return h;
    const size_t w = static_cast<size_t>(weakpoint);
    const float pool = kd::kWeakpointPool[w];
    const float left = pool - f.spent[w];
    if (pool > 0.0f && !(left > 0.0f)) {
        h.spent = true;
        return h;
    }
    // The graph's "I was attacked" next frame: the target's region, the sword's kind.
    f.base.struck = true;
    f.base.struckRegion = kd::kWeakpointRegion[w];
    f.struckKind = kd::kTriggerHitKind;
    if (!(pool > 0.0f)) return h;  // the neck trigger: a hit for the graph, no HP (D10)
    h.damage = std::min({damage, left, encounter.health});
    f.spent[w] += h.damage;
    encounter.health = std::max(0.0f, encounter.health - h.damage);
    if (encounter.health <= 0.0f) {
        encounter.health = 0.0f;
        encounter.defeated = true;
        encounter.deathWeakpoint = weakpoint;
        start_encounter_one_shot(entity, encounter, clips.death);
        h.killed = true;
    }
    return h;
}

ShakeOff kirin_fight_shake(const KirinFight&) { return ShakeOff{}; }
