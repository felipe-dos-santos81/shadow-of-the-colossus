#include "kirin_fight.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

#include "json_reader.h"
#include "kirin_root.h"
#include "scr_kirin.h"
#include "scr_library.h"

namespace kd = encounters::kirin_detail;

namespace {

// Her clip's root (x, z) in its clip-start frame at t (kirin_root.h); 0 without a series.
Vec3 root_at(std::string_view clip, float t) {
    for (const kirin_root::RootSeries& r : kirin_root::kRoots) {
        if (clip != r.clip || r.frames < 1) continue;
        return {knight_sample(r.frames, r.fps, t, [&r](int k) { return r.xz[k][0]; }), 0.0f,
                knight_sample(r.frames, r.fps, t, [&r](int k) { return r.xz[k][1]; })};
    }
    return {};
}

// Her turn clip's heading at t (+ = left); 0 for every other clip.
float turn_at(std::string_view clip, float t) {
    for (const kirin_root::TurnHeading& h : kirin_root::kTurnHeading) {
        if (clip == h.clip) return knight_sample(h.frames, h.fps, t, [&h](int k) { return h.deg[k]; });
    }
    return 0.0f;
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
    // authored: a hole (the nearest node empty: water, the channel) or the outside is no ground for
    // her; this also keeps heightmap_floor off its whole-grid fallback (measured: 13 ms frames).
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
                     const std::vector<std::string>& glbClips, const std::vector<float>& glbSeconds,
                     const std::vector<EventsVolume>& judgeDefaults,
                     const std::vector<std::string>& judgeNames, const std::string& standClip,
                     const Heightmap& ground, std::string& why) {
    if (glbSeconds.size() != glbClips.size()) {
        why = "one length per clip name";
        return false;
    }
    // Her graph names clips she does not carry: today one, from the 8 SWORD_ONLY control-defs every
    // boss shares (COMMON_BOSS_CONTROL:control-def[10], [14], [18], [28], [35], [86], [102], [109]),
    // which test "boss motion is knight[23] knight_a_atk1_start" (expect 0) — binary: an identity
    // compare against KNIGHT's motion-def never holds for her. Any control-def handle naming another
    // boss's motion row (under control_defs, a motion row that is not kirin[…]) is admitted, after
    // her clips (never a playback index), 0 s and rootless: the clause reads "not playing". Any other
    // unknown clip — a kirin_a_* one a stale bake lacks — is still refused by the loader.
    std::vector<std::string> clipNames = glbClips;
    std::vector<float> seconds = glbSeconds;
    std::vector<char> foreign(clipNames.size(), 0);
    const Json doc = json_parse(graphJson);
    if (const Json* defs = doc.get("control_defs"); defs != nullptr && defs->kind == Json::Object) {
        for (const auto& [handle, records] : defs->object) {
            for (const Json& rec : records.array) {
                const Json* h = rec.get("handle");
                const Json* motion = h != nullptr ? h->get("motion") : nullptr;
                const Json* clip = h != nullptr ? h->get("clip") : nullptr;
                if (motion == nullptr || clip == nullptr || motion->kind != Json::String ||
                    clip->kind != Json::String || motion->string.rfind("kirin[", 0) == 0 ||
                    std::find(clipNames.begin(), clipNames.end(), clip->string) != clipNames.end()) {
                    continue;
                }
                clipNames.push_back(clip->string);
                seconds.push_back(0.0f);
                foreign.push_back(1);
            }
        }
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
        core.rootless[i] = foreign[i];
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

KnightThink kirin_death_think(KnightCore& c, KirinState& k, ScrWorld& w, const Heightmap& ground, Vec3& root,
                              int deathClip) {
    if (deathClip < 0 || deathClip >= static_cast<int>(c.rootless.size())) return kirin_think(c, k, w, ground, root, 0.0f);
    char& rootless = c.rootless[static_cast<size_t>(deathClip)];
    const char keep = rootless;
    rootless = 0;
    const KnightThink t = kirin_think(c, k, w, ground, root, 0.0f);
    rootless = keep;
    return t;
}

// --- The scene's side ----------------------------------------------------------

bool kirin_fight_load(KirinFight& f, const Scene& scene, std::size_t entityRig, const Playback& entity,
                      const Heightmap& ground, const std::string& libraryDir, const std::string& entityGlb,
                      const std::string& standClip, Vec3 placement, Vec3 playerStart, std::string& why) {
    // kArena vs the composed manifest (the same values live in probe.py STAGES["arena_04"]).
    const kd::KirinArena& a = kd::kArena;
    if (!knight_check_arena(placement, playerStart, Vec3{a.spawnX, a.spawnGroundY, a.spawnZ}, a.playerStartX,
                            a.playerStartZ, "arena_04", "kirin.h", why)) {
        return false;
    }
    std::string text[3];
    const std::string paths[3] = {libraryDir + "/behaviour/" + kd::kGraphBoss + ".graph.json",
                                  libraryDir + "/" + kd::kRouteFile, libraryDir + "/" + kd::kTerrainFile};
    for (int i = 0; i < 3; ++i) {
        text[i] = knight_read_text(paths[i]);
        if (text[i].empty()) {
            why = paths[i] + " is missing or empty (`make behaviour routes terrain` into SOTC_LIB)";
            return false;
        }
    }
    KnightFight& b = f.base;
    std::vector<std::string> names, judges;
    std::vector<float> seconds;
    if (!knight_scene_sidecar(b, entity, libraryDir, entityGlb, names, seconds, judges, why) ||
        !kirin_core_load(b.core, f.state, text[0], text[1], text[2], names, seconds, b.events.judgeDefaults, judges,
                         standClip, ground, why) ||
        !knight_scene_bind(b, scene, entityRig, encounters::kKirin.death.clip, kd::kHipJoint, why)) {
        return false;
    }
    f.deathLoopClip = scr_clip(b.core.graph, kd::kDeathLoopClip);
    if (f.deathLoopClip < 0) {
        why = std::string("her glb carries no ") + kd::kDeathLoopClip;
        return false;
    }
    return true;
}

void kirin_fight_reset(KirinFight& f, Encounter& encounter, Playback& entity) {
    knight_fight_reset(f.base, encounter, entity);  // the core from the retry start, the stand
    f.state.layers = AddLayers{};
    f.state.orients.clear();
    f.spent = {};
    f.struckKind = 0;
    f.rockfallFed = 0.0f;
    encounter.entityLift = f.baseLift;
}

float kirin_death_lift(bool loopPlaying, float deathSeconds, float deathLength) {
    const kirin_root::DeathRoot& d = kd::kDeathRootY;
    if (loopPlaying || !(deathLength > 0.0f)) return d.kTo;
    const float u = std::clamp(deathSeconds / deathLength, 0.0f, 1.0f);
    return d.kFrom + (d.kTo - d.kFrom) * u;
}

KnightThink kirin_fight_step(KirinFight& f, Encounter& encounter, Playback& entity, const Heightmap& ground,
                             const PlayerState& player, const PlayerCombat& combat, bool whistleHeld, float dt) {
    KnightFight& b = f.base;
    KnightCore& c = b.core;
    KnightMotion& m = c.motion;
    if (!(dt >= 0.0f) || !std::isfinite(dt)) dt = 0.0f;

    // The player's side of the world (shared), then hers: the strike's kind, the max HP and
    // (D8) the terrain class and owner under the player on foot (data, arena_04.terrain.json).
    ScrWorld w;
    const bool struck = b.struck;
    knight_world_player_side(b, w, encounter, ground, kd::kGroundStep, kd::kBandRegion, player, combat, whistleHeld);
    w.hitKind = struck ? f.struckKind : -1;
    w.hpMax = encounters::kKirin.maxHealth;
    w.playerGroundAttr = !w.playerAttached && combat.mode != PlayerMode::Falling
                             ? kirin_attr_word(terrain_at(f.state.terrain, player.position))
                             : 0u;

    Vec3 root = encounter.entityPosition;
    KnightThink t;
    if (encounter.defeated) {
        if (b.deathMode < 0) {
            t = knight_enter_death(b, [&] { return kirin_death_think(c, f.state, w, ground, root, b.deathClip); });
            // authored: the death clip is her whole pose — the layers are cut (no think steps them
            // after the defeat, so a layer left playing would freeze on her corpse).
            f.state.layers = AddLayers{};
            f.state.orients.clear();
        }
        // The death clip has no root series: no travel. Her root drops kDeathRootY while it plays,
        // then death_loop (its held pose) holds the drop's end.
        const float len = c.seconds[static_cast<size_t>(b.deathClip)];
        if (entity.index == b.deathClip && entity.time >= std::min(len, encounters::kKirin.death.motionSeconds)) {
            entity.index = f.deathLoopClip;
            entity.time = 0.0f;
        }
        const bool looping = entity.index == f.deathLoopClip;
        encounter.entityLift = f.baseLift + kirin_death_lift(looping, entity.time, len);
        m.time = looping ? len : std::min(entity.time, len);
    } else {
        t = kirin_think(c, f.state, w, ground, root, dt);
    }
    encounter.entityPosition = root;
    encounter.entityYawDeg = c.yawDeg;
    knight_fight_sync(b, entity, encounter.defeated, t);
    return t;
}

void kirin_fight_pose(KirinFight& f, const Scene& scene, std::size_t entityRig, const Playback& entity, float dt,
                      std::vector<Mat4>& palette) {
    knight_fight_pose(f.base, scene, entityRig, entity, dt, palette, &f.state.layers);
}

std::vector<DamageHit> kirin_fight_events(KirinFight& f, PlayerState& player, PlayerCombat& combat,
                                          const CombatTuning& tuning, float dt, StepEvents* events) {
    KnightFight& b = f.base;
    std::vector<DamageHit> landed =
        knight_fight_events(b, player, combat, tuning, dt, events, kd::kHeavyReaction, kd::kNoThrowOffReaction);
    // The rockfall feed (R-AJ): the stomps' fired effects while armed.
    ScrRuntime& rt = b.core.rt;
    if (b.havePose) {
        const bool armed = (rt.bossBits & kScrKirinRockfallArmed) != 0;
        for (const EventFire& e : events_fired(b.events, b.evNow, b.evPrev, knight_pose_view(b))) {
            const float before = rt.bossTimer[0];
            rt.bossTimer[0] = kirin_rockfall_feed(before, e.name, armed);
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

float kirin_rockfall_feed(float timer, std::string_view effect, bool armed) {
    if (!armed || effect.find(kd::kRockfallEffectTag) == std::string_view::npos) return timer;
    return std::min(kd::kRockfallCapSeconds, timer + kd::kRockfallFeedSeconds);
}

ShakeOff kirin_fight_shake(const KirinFight& f) {
    ShakeOff s;
    const KnightCore& c = f.base.core;
    // The base motion's refuse clip and every layer playing an additive one, the strongest wins.
    const auto take = [&s](std::string_view clip, float t) {
        for (const kd::KirinShake& k : kd::kShakes) {
            if (clip == k.clip && t >= k.activeBegin && t <= k.activeEnd) {
                s.staminaDrain = std::max(s.staminaDrain, k.drainPerSecond);
            }
        }
    };
    take(knight_clip_name(c, c.motion.clip), c.motion.time);
    for (const AddLayer& l : f.state.layers.slot) {
        // A layer's clock is its frame at 30 per s (additive.cpp kLayerFps, data DAT_0126dac4).
        if (l.state != AddLayer::Free) take(knight_clip_name(c, l.clip), l.frame / 30.0f);
    }
    return s;
}
