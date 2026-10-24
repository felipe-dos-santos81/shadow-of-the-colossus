#include "knight_fight.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

#include "knight_root.h"

namespace ka = encounters::knight_detail;

namespace {

// The entity id the events' §13.5 filter compares (PoseView::entity vs PlayerView): the
// fight has one colossus.
constexpr int kEntityId = 0;

const knight_root::RootSeries* root_series(std::string_view clip) {
    for (const knight_root::RootSeries& r : knight_root::kRoots) {
        if (clip == r.clip) return &r;
    }
    return nullptr;
}

const knight_root::TurnHeading* turn_series(std::string_view clip) {
    for (const knight_root::TurnHeading& h : knight_root::kTurnHeading) {
        if (clip == h.clip) return &h;
    }
    return nullptr;
}

// A per-frame table at clip time `t` (frame = t · fps), linear between frames, clamped.
template <typename F>
auto sample(int frames, float fps, float t, F at) {
    const float f = std::clamp(std::isfinite(t) ? t * fps : 0.0f, 0.0f, static_cast<float>(frames - 1));
    const int i = std::min(static_cast<int>(f), frames - 1);
    const int j = std::min(i + 1, frames - 1);
    const float u = f - static_cast<float>(i);
    return at(i) * (1.0f - u) + at(j) * u;
}

// The clip's root (x, z) in its clip-start frame at time t (knight_root.h); 0 without a series.
Vec3 root_at(std::string_view clip, float t) {
    const knight_root::RootSeries* r = root_series(clip);
    if (r == nullptr || r->frames < 1) return {};
    const float x = sample(r->frames, r->fps, t, [r](int k) { return r->xz[k][0]; });
    const float z = sample(r->frames, r->fps, t, [r](int k) { return r->xz[k][1]; });
    return {x, 0.0f, z};
}

// The yaw a turn clip's bones have turned by time t (+ = left; 0 for every other clip).
float turn_heading(std::string_view clip, float t) {
    const knight_root::TurnHeading* h = turn_series(clip);
    if (h == nullptr) return 0.0f;
    return sample(h->frames, h->fps, t, [h](int k) { return h->deg[k]; });
}

// `local` (+z his front, +x his left) in the world at yaw `yawDeg` (quadratus_fight's).
Vec3 rotate(Vec3 local, float yawDeg) {
    const float r = yawDeg * kDegToRad;
    const float c = std::cos(r), s = std::sin(r);
    return {local.x * c + local.z * s, 0.0f, -local.x * s + local.z * c};
}

Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

// "KNIGHT_ANOTHER:else_orient_def[3] '...'" -> 3; -1 when the orient is no else edge. The
// N names the control-mode the edge returns to (a reading: else_orient_def[0/1/3/6/7/8/9/10]
// are the standby/still/walk/attack/leg_r/leg_l/fall/death returns, the control-modes
// of those numbers).
int else_control_mode(const std::string& orient) {
    const size_t at = orient.find("else_orient_def[");
    if (at == std::string::npos) return -1;
    return std::atoi(orient.c_str() + at + 16);
}

// The else edge out of `clip` (its first), or -1; `cm` its control-mode.
int else_edge(const ScrGraph& g, int clip, int& cm) {
    for (size_t i = 0; i < g.motionGraph.size(); ++i) {
        const ScrMotionEdge& e = g.motionGraph[i];
        if (e.from == clip) {
            cm = else_control_mode(e.orient);
            if (cm >= 0) return static_cast<int>(i);
        }
    }
    return -1;
}

bool requested(const std::vector<int>& ctrls, int bit) {
    return std::find(ctrls.begin(), ctrls.end(), bit) != ctrls.end();
}

}  // namespace

const char* knight_clip_name(const KnightCore& core, int clip) {
    return clip >= 0 && clip < static_cast<int>(core.graph.clipNames.size())
               ? core.graph.clipNames[static_cast<size_t>(clip)].c_str()
               : "";
}

void knight_world_player(ScrWorld& w, bool whistlePressed, bool dead) {
    w.playerMotion = whistlePressed ? ka::kWhistleMotion : -1;
    w.playerMotionStarted = whistlePressed;
    w.playerControlMode = dead ? ka::kPlayerDeathControlMode : -1;
}

bool knight_core_load(KnightCore& core, const std::string& graphJson,
                      const std::vector<std::string>& clipNames, const std::vector<float>& seconds,
                      const std::vector<std::string>& judgeNames, const std::string& standClip,
                      std::string& why) {
    if (seconds.size() != clipNames.size()) {
        why = "one length per clip name";
        return false;
    }
    core.registry = scr_registry_knight();
    if (!scr_graph_load(graphJson, clipNames, core.registry, core.graph, why)) return false;
    // The guard the strict loader cannot make (knight-specific handles, judge names).
    if (!scr_knight_check(core.graph, judgeNames, why)) return false;
    core.seconds = seconds;
    core.rootless.assign(clipNames.size(), 0);
    for (size_t i = 0; i < clipNames.size(); ++i) {
        for (const char* name : knight_root::kNoRoot) {
            if (clipNames[i] == name) core.rootless[i] = 1;
        }
    }
    core.standClip = scr_clip(core.graph, standClip);
    if (core.standClip < 0 || core.rootless[static_cast<size_t>(core.standClip)]) {
        why = "the stand clip '" + standClip + "' is not a rooted clip of the graph";
        return false;
    }
    if (else_edge(core.graph, core.standClip, core.standControlMode) < 0) {
        why = "the graph has no else edge out of '" + standClip + "' (its control-mode)";
        return false;
    }
    return true;
}

void knight_core_reset(KnightCore& core, float yawDeg) {
    scr_runtime_start(core.graph, core.rt, /*retry=*/true);  // §13.8: mode 3, not standby
    core.motion = KnightMotion{};
    core.motion.clip = core.standClip;
    core.motion.controlMode = core.standControlMode;
    core.rt.controlMode = core.standControlMode;
    core.yawDeg = yawDeg;
}

Vec3 knight_clamp_bound(Vec3 p, Vec3 centre, float radius) {
    const float dx = p.x - centre.x, dz = p.z - centre.z;
    const float d = std::hypot(dx, dz);
    if (!(radius > 0.0f) || d <= radius) return p;
    return {centre.x + dx * radius / d, p.y, centre.z + dz * radius / d};
}

KnightThink knight_think(KnightCore& c, ScrWorld& w, float dt) {
    KnightThink out;
    KnightMotion& m = c.motion;
    if (!(dt >= 0.0f) || !std::isfinite(dt)) dt = 0.0f;
    out.modeBefore = c.rt.current;
    out.clipBefore = m.clip;
    const std::string_view name = knight_clip_name(c, m.clip);

    // The world's motion fields: this frame's clip, the last switch, the in-progress turn.
    w.bossMotion = m.clip;
    w.bossPreviousMotion = m.previous;
    w.bossMotionSwitched = m.switched;
    w.bossControlMode = m.controlMode;
    w.bossYawDeg = c.yawDeg + turn_heading(name, m.time);
    scr_world_relate(w);
    m.switched = false;

    const auto clamp_goal = [&c]() {
        if (c.rt.hasGoal) c.rt.goal = knight_clamp_bound(c.rt.goal, c.boundCentre, c.boundRadius);
    };
    clamp_goal();  // Review Focus 5: his graph never tests walkability
    out.step = scr_knight_step(c.graph, c.registry, c.rt, w, dt);
    clamp_goal();

    // The clock.
    const float len = c.seconds[static_cast<size_t>(m.clip)];
    const float t0 = m.time;
    const bool wasHeld = m.held;
    if (!wasHeld) m.time += dt;
    const bool ended = !wasHeld && m.time >= len;
    // A held clip (a refused rootless target, a one-shot with nowhere to go) is at its end:
    // its deferred (p30 -1) edges and its else edge may still leave it.
    const bool atEnd = ended || wasHeld;
    // A playing turn is cut once its own turn bits are no longer requested: the gait fires
    // them only past R1's 3° steer gate (or, arrived, while his facing is off by more), so
    // the turn has reached his goal's bearing. Every edge out of a turn is taken at its end
    // (p30 -1): without the cut one ~90° turn overshoots and he turns back (Task 5's note).
    const knight_root::TurnHeading* turn = turn_series(name);
    bool cut = false;
    if (turn != nullptr && !atEnd) {
        const bool left = turn->deg[turn->frames - 1] > 0.0f;
        const bool still = left ? requested(out.step.controls, scr_knight::kCtrlTurnLeft) ||
                                      requested(out.step.controls, scr_knight::kCtrlStopTurnLeft)
                                : requested(out.step.controls, scr_knight::kCtrlTurnRight) ||
                                      requested(out.step.controls, scr_knight::kCtrlStopTurnRight);
        cut = !still;
    }
    out.turnCut = cut;

    // The requests through the motion graph: an edge taken now (p30 >= 0) or at the clip's
    // end (p30 -1: the else/chain edges, every edge out of a turn) — a cut counts as an end.
    const int cmBefore = c.rt.controlMode, edgeBefore = c.rt.controlEdge;
    c.rt.controlMode = m.controlMode;
    int to = scr_control_clip(c.graph, c.rt, out.step.controls, w);
    int edge = to >= 0 ? c.rt.controlEdge : -1;
    int nextCm = to >= 0 ? c.rt.controlMode : m.controlMode;
    if (to >= 0 && c.graph.motionGraph[static_cast<size_t>(edge)].p30 < 0.0f && !atEnd && !cut) {
        to = -1;  // waits for the clip's end; asked again every frame
        edge = -1;
        nextCm = m.controlMode;
    }
    if (to < 0) {
        c.rt.controlMode = cmBefore;
        c.rt.controlEdge = edgeBefore;
    }
    if (to < 0 && (atEnd || cut)) {
        int cm = -1;
        edge = else_edge(c.graph, m.clip, cm);
        if (edge >= 0) {
            to = c.graph.motionGraph[static_cast<size_t>(edge)].to;
            nextCm = cm;
            out.viaElse = true;
        }
    }
    if (to >= 0 && c.rootless[static_cast<size_t>(to)]) {
        to = -1;  // never raw (his feet would sit 12.7 m under the ground): hold the last frame
        edge = -1;
        out.refused = true;
        m.held = true;
        m.time = len;
    }

    // His root travel (each clip's own series, in its start frame) and a turn's yaw.
    const float endT = std::min(m.time, len);
    const float atSwitch = ended ? len : m.time;
    Vec3 local = sub(root_at(name, to >= 0 ? atSwitch : endT), root_at(name, t0));
    out.travel = rotate(local, c.yawDeg);
    if (!wasHeld && (to >= 0 || ended)) {
        // A turn adds the yaw its bones turned (its end or the cut), once.
        out.yawAddedDeg = turn_heading(name, atSwitch);
        c.yawDeg = std::remainder(c.yawDeg + out.yawAddedDeg, 360.0f);
    }
    if (to >= 0 && to == m.clip) {
        // A loop's else edge onto itself: wrap.
        m.time -= len;
        m.instance += 1;
        m.held = false;  // a held clip wrapping onto itself plays again (not t 0 forever)
        out.wrapped = true;
        out.travel = add(out.travel, rotate(root_at(name, m.time), c.yawDeg));
    } else if (to >= 0) {
        const float leftover = ended ? m.time - len : 0.0f;
        m.previous = m.clip;
        m.clip = to;
        m.time = std::min(leftover, c.seconds[static_cast<size_t>(to)]);
        m.switched = true;
        m.controlMode = nextCm;
        m.instance += 1;
        m.held = false;
        const float p34 = c.graph.motionGraph[static_cast<size_t>(edge)].p34;
        m.blendSeconds = p34 > 0.0f ? p34 / ka::kBlendFps : 0.0f;
        m.blendElapsed = m.blendSeconds > 0.0f ? 0.0f : -1.0f;
        out.travel = add(out.travel, rotate(root_at(knight_clip_name(c, to), m.time), c.yawDeg));
    } else if (ended && !m.held) {
        m.held = true;  // a one-shot with nowhere to go holds its last frame
        m.time = len;
    }
    out.edge = edge;
    out.clipAfter = m.clip;
    return out;
}

// --- The scene's side ----------------------------------------------------------

namespace {

std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

int joint_of(const KnightFight& f, std::string_view name) {
    for (size_t i = 0; i < f.jointNames.size(); ++i) {
        if (f.jointNames[i] == name) return static_cast<int>(i);
    }
    return -1;
}

PoseFrame frame_of(const KnightFight& f, const std::vector<Mat4>& joints, const Mat4& model) {
    PoseFrame p;
    p.entity = model;
    p.hipY = f.hipJoint >= 0 && static_cast<size_t>(f.hipJoint) < joints.size()
                 ? joints[static_cast<size_t>(f.hipJoint)].m[13] - model.m[13]
                 : 0.0f;
    p.joint = [&f, &joints](std::string_view name, Mat4& out) {
        const int j = joint_of(f, name);
        if (j < 0 || static_cast<size_t>(j) >= joints.size()) return false;
        out = joints[static_cast<size_t>(j)];
        return true;
    };
    return p;
}

// This frame's pose (last frame's as `prev`; the first posed frame is its own prev).
PoseView pose_view(const KnightFight& f) {
    PoseView v;
    v.entity = kEntityId;  // the §13.5 filter's id (PoseView's -1 default would disable it)
    v.now = frame_of(f, f.joints, f.model);
    v.prev = f.havePrev ? frame_of(f, f.jointsPrev, f.modelPrev) : v.now;
    v.optBones = f.optBones;
    return v;
}

PlayerView player_view(const PlayerState& player, const PlayerCombat& combat) {
    PlayerView p;
    p.feet = player.position;
    if (mode_is_attached(combat.mode)) {
        p.attachedTo = kEntityId;  // gripping or standing on him: either way §13.5 holds
        p.standingOn = kEntityId;
    }
    return p;
}

// His root's ground: the layered floor under it within a step (kGroundStep), kept when
// there is none.
Vec3 grounded(const Heightmap& ground, Vec3 at) {
    const float floor = heightmap_floor(ground, at, ka::kGroundStep);
    if (std::isfinite(floor)) at.y = floor;
    return at;
}

}  // namespace

bool knight_events_check(const EventsFile& ev, const std::vector<std::string>& joints,
                         const std::vector<std::string>& optBones, std::string& why) {
    const auto clip_parent = [&](const EventsParent& p, const std::string& row) {
        if (p.kind == EventsParent::Spawn) {
            why = "the sidecar row " + row + " has a spawn parent on a clip";
            return false;
        }
        if (p.kind == EventsParent::Bone && std::find(joints.begin(), joints.end(), p.bone) == joints.end()) {
            why = "the sidecar row " + row + " names bone " + p.bone + ", not in his rig";
            return false;
        }
        if (p.kind == EventsParent::OptBone &&
            (p.optBone < 0 || p.optBone >= static_cast<int>(optBones.size()))) {
            why = "the sidecar row " + row + " names OptBone " + std::to_string(p.optBone) + " of " +
                  std::to_string(optBones.size());
            return false;
        }
        return true;
    };
    bool hasDef = false;
    for (const EventsVolume& v : ev.judgeDefaults) {
        if (!clip_parent(v.parent, "judgeDefaults/" + v.name)) return false;
    }
    for (const EventsClip& c : ev.clips) {
        for (const EventsVolume& v : c.damage) {
            if (!clip_parent(v.parent, c.name + "/" + v.name)) return false;
            hasDef = hasDef || v.hasDef;
        }
        for (const EventsVolume& v : c.judge) {
            if (!clip_parent(v.parent, c.name + "/" + v.name)) return false;
        }
        for (const EventsEffect& e : c.effects) {
            if (!clip_parent(e.parent, c.name + "/" + e.name)) return false;
        }
    }
    for (const EventsSpawned& s : ev.spawned) {
        for (const EventsVolume& v : s.damage) {
            const EventsParent::Kind k = v.parent.kind;
            if (k != EventsParent::Spawn && k != EventsParent::Entity && k != EventsParent::EntityRoot) {
                why = "the spawned row " + s.name + "/" + v.name + " has a bone parent (spawn or entity only)";
                return false;
            }
            hasDef = hasDef || v.hasDef;
        }
    }
    if (ev.spawned.empty() || !hasDef) {
        why = "the sidecar has no spawned objanims or no damage def (bake from the repo root: "
              "the converter reads tools/re/objanim/ relative to its working directory)";
        return false;
    }
    return true;
}

const char* knight_mode_name(const KnightFight& f, int mode) {
    return mode >= 0 && mode < static_cast<int>(f.core.graph.modes.size())
               ? f.core.graph.modes[static_cast<size_t>(mode)].name.c_str()
               : "none";
}

bool knight_fight_load(KnightFight& f, const Scene& scene, std::size_t entityRig,
                       const Playback& entity, const std::string& libraryDir,
                       const std::string& entityGlb, const std::string& standClip, Vec3 placement,
                       Vec3 playerStart, std::string& why) {
    // kArena vs the composed manifest (the same values live in probe.py STAGES["arena_03"];
    // nothing else checks they agree).
    const ka::KnightArena& a = ka::kArena;
    if (std::fabs(placement.x - a.spawnX) > 1e-3f || std::fabs(placement.z - a.spawnZ) > 1e-3f ||
        std::fabs(placement.y - a.spawnGroundY) > 1e-3f) {
        why = "the manifest places him off kArena's spawn (re-compose arena_03 or fix knight.h)";
        return false;
    }
    if (std::fabs(playerStart.x - a.playerStartX) > 1e-3f || std::fabs(playerStart.z - a.playerStartZ) > 1e-3f) {
        why = "the manifest's player start is off kArena's (re-compose arena_03 or fix knight.h)";
        return false;
    }
    const std::string graphPath = libraryDir + "/behaviour/" + ka::kGraphBoss + ".graph.json";
    const std::string json = read_text(graphPath);
    if (json.empty()) {
        why = graphPath + " is missing or empty (`make behaviour` into SOTC_LIB)";
        return false;
    }
    std::filesystem::path sidecar = std::filesystem::path(libraryDir) / entityGlb;
    sidecar.replace_extension(".events.json");
    if (!events_load(sidecar.string(), f.events, why)) return false;
    std::vector<std::string> names;
    std::vector<float> seconds;
    for (const AnimationCpu& clip : entity.clips) {
        names.push_back(clip.name);
        seconds.push_back(clip.duration);
    }
    std::vector<std::string> judges;
    for (const EventsVolume& v : f.events.judgeDefaults) judges.push_back(v.name);
    for (const EventsClip& c : f.events.clips) {
        for (const EventsVolume& v : c.judge) judges.push_back(v.name);
    }
    // The graph's clip indices are the playback's: scr_graph_load keeps `names`' order.
    if (!knight_core_load(f.core, json, names, seconds, judges, standClip, why)) return false;
    f.core.boundCentre = {a.boundCentreX, 0.0f, a.boundCentreZ};
    f.core.boundRadius = a.boundRadius;
    f.eventsClip.clear();
    for (const std::string& n : names) f.eventsClip.push_back(events_clip(f.events, n));
    f.deathClip = scr_clip(f.core.graph, encounters::kKnight.death.clip);
    if (f.deathClip < 0) {
        why = std::string("his glb carries no ") + encounters::kKnight.death.clip;
        return false;
    }
    const RigCpu& rig = rig_at(scene, entityRig);
    f.jointNames.clear();
    f.optBones.clear();
    for (const JointCpu& j : rig.skin.joints) {
        f.jointNames.push_back(j.name);
        if (j.name.rfind("OptBone_", 0) == 0) f.optBones.push_back(j.name);
    }
    if (!knight_events_check(f.events, f.jointNames, f.optBones, why)) return false;
    f.hipJoint = joint_of(f, ka::kHipJoint);
    if (f.hipJoint < 0) {
        why = std::string("his rig has no ") + ka::kHipJoint;
        return false;
    }
    return true;
}

void knight_fight_reset(KnightFight& f, Encounter& encounter, Playback& entity) {
    encounter.entityPosition = f.basePosition;
    encounter.entityYawDeg = f.baseYawDeg;
    knight_core_reset(f.core, f.baseYawDeg);
    f.spent = {};
    f.struck = false;
    f.struckRegion = 0;
    f.throwOff = false;
    f.evPrev = EventsClipState{};
    f.evNow = EventsClipState{};
    f.evNow.clip = f.eventsClip[static_cast<size_t>(f.core.standClip)];
    f.spawns.active.clear();
    f.havePrev = false;
    f.deathMode = -1;
    f.deathGraphClip = -1;
    f.think = KnightThink{};
    entity.index = f.core.standClip;
    entity.time = 0.0f;
    entity.pinned = true;
    entity.playing = false;
}

KnightThink knight_fight_step(KnightFight& f, Encounter& encounter, Playback& entity,
                              const Heightmap& ground, const PlayerState& player,
                              const PlayerCombat& combat, bool whistleHeld, float dt) {
    KnightCore& c = f.core;
    KnightMotion& m = c.motion;
    if (!(dt >= 0.0f) || !std::isfinite(dt)) dt = 0.0f;
    f.judgeFired.clear();

    ScrWorld w;
    w.playerPosition = player.position;
    w.playerGroundPosition = grounded(ground, player.position);
    w.playerAttached = mode_is_attached(combat.mode);
    w.playerAttachedRegion =
        w.playerAttached && combat.band >= 0 && combat.band < static_cast<int>(ka::kBandRegion.size())
            ? ka::kBandRegion[static_cast<size_t>(static_cast<int>(combat.band))]
            : 0;
    // §13.3 stand-in (authored): the battle area (owner 0x800000) is the arena's ground under a
    // player on foot.
    w.playerGroundAttr = !w.playerAttached && combat.mode != PlayerMode::Falling &&
                                 heightmap_in_bounds(ground, player.position)
                             ? 0x800000u
                             : 0u;
    // R-N: the S8 whistle (a press, as ride_presses reads it) and the player's death.
    knight_world_player(w, whistleHeld && !f.whistleBefore, combat.mode == PlayerMode::Dead);
    f.whistleBefore = whistleHeld;
    w.hit = f.struck;
    w.hitRegion = f.struckRegion;
    f.struck = false;
    w.hp = encounter.defeated ? 0.0f : encounter.health;
    w.bossPosition = encounter.entityPosition;
    w.bossOnGround = true;  // authored: his root follows the ground every frame
    // §13.3: the layered ground's raw node (its top) for swing_space_clear; none = a drop.
    w.groundAt = [&ground](float x, float z, float& h) {
        h = heightmap_node(ground, Vec3{x, 0.0f, z});
        return std::isfinite(h);
    };
    // Perception's sight ray (authored): arena_03 has no sight-blocking geometry — its
    // ground is the disc he stands on (never a wall) and the rest are backdrops.
    w.ray = nullptr;
    // Ruling R-B: the attack runner's judge volumes, on last frame's pose.
    const PlayerView pv = player_view(player, combat);
    w.playerInJudge = [&f, pv](std::string_view name) {
        if (!f.havePose) return false;
        const bool in = events_player_in_judge(f.events, name, f.evNow, pose_view(f), pv);
        if (in) f.judgeFired.emplace_back(name);
        return in;
    };

    KnightThink t;
    if (encounter.defeated) {
        if (f.deathMode < 0) {
            // D12: the graph's death once (forced death -> its root death mode; the
            // mortality row -> the death clip), then the death sequence owns the clip.
            // The mortality row (HP <= 0) takes the death clip into the root's death
            // control-mode; in it the walker forces the root death mode (§1 step 6): two
            // thinks, at no time. The cap (authored: 3 = those two + one spare) ends the loop
            // for a graph that never gets there; the s12-death line then shows its mode.
            for (int i = 0; i < 3 && c.rt.current != c.graph.root.deathMode; ++i) {
                t = knight_think(c, w, 0.0f);
            }
            f.deathMode = c.rt.current;
            f.deathGraphClip = m.clip;
            m.previous = m.clip;
            m.clip = f.deathClip;
            m.time = 0.0f;
            m.held = false;
            m.blendElapsed = -1.0f;
            m.instance += 1;
        }
        // He travels the death clip's root at the death machine's clock and holds.
        const std::string_view name = knight_clip_name(c, f.deathClip);
        const float now = entity.time;
        t.travel = rotate(sub(root_at(name, now), root_at(name, m.time)), c.yawDeg);
        m.time = now;
    } else {
        t = knight_think(c, w, dt);
    }
    // His root over the ground, clamped into his disc (Review Focus 5); the unclamped
    // reach and the clamped frames are the s12-bound line's evidence.
    Vec3 to = add(encounter.entityPosition, t.travel);
    const float reach = std::hypot(to.x - c.boundCentre.x, to.z - c.boundCentre.z);
    f.maxUnclampedDistance = std::max(f.maxUnclampedDistance, reach);
    if (reach > c.boundRadius) ++f.clampedFrames;
    to = knight_clamp_bound(to, c.boundCentre, c.boundRadius);
    if (heightmap_in_bounds(ground, to)) encounter.entityPosition = grounded(ground, to);
    encounter.entityYawDeg = c.yawDeg;
    f.maxBoundDistance = std::max(f.maxBoundDistance,
                                  std::hypot(encounter.entityPosition.x - c.boundCentre.x,
                                             encounter.entityPosition.z - c.boundCentre.z));
    if (!encounter.defeated) {
        // The entity's clip at the motion's clock (clamped: the app samples, never wraps).
        entity.index = m.clip;
        entity.time = std::min(m.time, c.seconds[static_cast<size_t>(m.clip)]);
        entity.pinned = true;
        entity.playing = false;
    }
    // The events' clip state: a new instance on every start or wrap (Task 8's contract).
    f.evPrev = f.evNow;
    const int evClip = f.eventsClip[static_cast<size_t>(m.clip)];
    if (m.instance != f.evNow.instance || evClip != f.evNow.clip) {
        if (evClip == f.evNow.clip && !t.wrapped) f.evPrev.clip = -1;  // a restart, not a loop
        f.evNow.clip = evClip;
        f.evNow.instance = m.instance;
        f.evNow.hit = 0;
    }
    f.evNow.frame = m.time * (evClip >= 0 ? f.events.clips[static_cast<size_t>(evClip)].fps : 30.0f);
    f.think = t;
    return t;
}

void knight_fight_pose(KnightFight& f, const Scene& scene, std::size_t entityRig,
                       const Playback& entity, float dt, std::vector<Mat4>& palette) {
    KnightMotion& m = f.core.motion;
    // The switch's blend (the edge's p34), on blended_palette's 0.25 s scale.
    const float from = m.blendElapsed >= 0.0f && m.blendSeconds > 0.0f
                           ? m.blendElapsed / m.blendSeconds * kClipBlendSeconds
                           : -1.0f;
    if (m.blendElapsed >= 0.0f) {
        m.blendElapsed += std::max(dt, 0.0f);
        if (m.blendElapsed >= m.blendSeconds) m.blendElapsed = -1.0f;
    }
    if (entity.index >= 0 && static_cast<size_t>(entity.index) < entity.clips.size()) {
        const AnimationCpu& clip = entity.clips[static_cast<size_t>(entity.index)];
        std::vector<Mat4> blended = blended_palette(scene, entityRig, entity, f.blend, clip.name,
                                                    playback_time(entity, clip.duration), from);
        if (!blended.empty()) palette = std::move(blended);
    }
    // Every joint's world matrix: model · palette · bindWorld (rig_bone_world's product).
    const RigCpu& rig = rig_at(scene, entityRig);
    f.jointsPrev.swap(f.joints);
    f.modelPrev = f.model;
    f.havePrev = f.havePose;
    f.model = rig.meshBegin < rig.meshEnd && rig.meshBegin < scene.meshes.size()
                  ? scene.meshes[rig.meshBegin].model
                  : mat4_identity();
    f.joints.resize(rig.skin.joints.size());
    for (size_t j = 0; j < rig.skin.joints.size() && j < palette.size(); ++j) {
        f.joints[j] = mat4_mul(mat4_mul(f.model, palette[j]), rig.skin.joints[j].bindWorld);
    }
    f.havePose = !f.joints.empty();
}

std::vector<DamageHit> knight_fight_events(KnightFight& f, PlayerState& player, PlayerCombat& combat,
                                           const CombatTuning& tuning, float dt, StepEvents* events) {
    std::vector<DamageHit> landed;
    f.throwOff = false;  // last frame's throw-off was read by this frame's step
    f.overlaps.clear();
    if (!f.havePose) return landed;
    const PoseView pose = pose_view(f);
    const PlayerView pv = player_view(player, combat);
    // Effects start their spawned damage (the footsteps' knight_walk_dmg).
    f.spawns.fire(f.events, events_fired(f.events, f.evNow, f.evPrev, pose));
    f.spawns.step(f.events, dt);
    std::vector<DamageHit> hits = events_damage(f.events, f.evNow, f.evPrev, pose, pv);
    for (const DamageHit& h : hits) f.evNow.hit |= uint64_t{1} << h.row;
    for (const DamageHit& h : f.spawns.damage(f.events, pv, kEntityId)) hits.push_back(h);
    f.overlaps = hits;  // every row that overlapped him, landed or not (i-frames, §13.5 aside)
    // D12: the throw-off reaches a player attached to him (§13.5 holds back the rest).
    if (pv.attachedTo == kEntityId) {
        PoseView open = pose;
        open.entity = -1;
        for (const DamageHit& h : events_damage(f.events, f.evNow, f.evPrev, open, pv)) {
            if (h.reaction == ka::kThrowOffReaction) {
                f.evNow.hit |= uint64_t{1} << h.row;
                f.throwOff = true;
                landed.push_back(h);
            }
        }
    }
    // Overlapping rows: the highest amount lands (D6).
    const DamageHit* best = nullptr;
    for (const DamageHit& h : hits) {
        if (best == nullptr || h.amount > best->amount) best = &h;
    }
    if (best != nullptr && best->amount > 0.0f &&
        player_take_damage(combat, best->amount, best->reaction == ka::kHeavyReaction, tuning, events)) {
        landed.push_back(*best);
    }
    return landed;
}

KnightHit knight_fight_hit(KnightFight& f, Encounter& encounter, Playback& entity,
                           const EntityClips& clips, int weakpoint, float damage) {
    KnightHit h;
    h.weakpoint = weakpoint;
    if (encounter.defeated || !(damage > 0.0f) || weakpoint < 0 || weakpoint >= ka::W::Count) return h;
    const size_t w = static_cast<size_t>(weakpoint);
    const float left = ka::kWeakpointPool[w] - f.spent[w];
    if (!(left > 0.0f)) {
        h.spent = true;
        return h;
    }
    h.damage = std::min({damage, left, encounter.health});
    f.spent[w] += h.damage;
    encounter.health = std::max(0.0f, encounter.health - h.damage);
    f.struck = true;  // the graph's "I was attacked" next frame
    f.struckRegion = ka::kWeakpointRegion[w];
    if (encounter.health <= 0.0f) {
        encounter.health = 0.0f;
        encounter.defeated = true;
        encounter.deathWeakpoint = weakpoint;
        start_encounter_one_shot(entity, encounter, clips.death);
        h.killed = true;
    }
    return h;
}

ShakeOff knight_fight_shake(const KnightFight& f) {
    ShakeOff s;
    const std::string_view name = knight_clip_name(f.core, f.core.motion.clip);
    for (const ka::KnightShake& k : ka::kShakes) {
        if (name == k.clip && f.core.motion.time >= k.activeBegin && f.core.motion.time <= k.activeEnd) {
            s.staminaDrain = k.drainPerSecond;
        }
    }
    s.forceRelease = f.throwOff;
    return s;
}
