#include "selftests.h"

#include "capture.h"
#include "encounter_rt.h"
#include "s13_script.h"
#include "scr_cdef.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace kd = encounters::kirin_detail;

namespace {

enum Phase {
    kApproach,  // out to her platform until she stomps
    kDodge,     // off her facing line until the stomp chain ends
    kToTunnel,  // around the ruins' south side into the west stair tunnel (terrain class 7)
    kPeep,      // stand in it, whistling, until she peeps from the tunnel's GP
    kLeave,     // down the stair, round the ring corridor, up and out of the north-west tunnel
    kToTail,    // to her tail's low end; grab it in its watch_* window
    kClimb,     // tail -> back -> withers -> neck low
    kNeck,      // strike the neck trigger twice: neck down 1, then 2
    kToHead,    // neck low -> neck high -> head
    kShake,     // hold on on the head through a shake
    kHeadSigil, // strike the head sigil to her death
    kDeath,     // watch the death to Done: the corpse stays
    kReset,     // R: her opening state
    kPhaseCount,
};
constexpr const char* kPhaseNames[kPhaseCount] = {"approach", "dodge", "to_tunnel", "peep", "leave",
                                                  "to_tail", "climb", "neck", "to_head", "shake",
                                                  "head_sigil", "death", "reset"};
constexpr float kBudget[kPhaseCount] = {120.0f, 30.0f, 90.0f, 120.0f, 60.0f, 60.0f, 90.0f,
                                        120.0f, 90.0f, 90.0f, 150.0f, 20.0f, 1.0f};
constexpr int kS13PerfFrames = 240;

// The walks (authored, the script; measured on arena_04's layered map: x, z in m). The
// approach: off the start's ruin roof, east along the plain to where it joins her platform
// (z >= 150), then at her. The tunnel: round the ruins' south side to the west stair's mouth
// and in (the unit test's class-7 point). The leave: down the west stair, north along the ring
// corridor's west side, up the north-west stair (terrain class 8) and out of its mouth.
struct Waypoint {
    float x, z;
};
constexpr Waypoint kApproachPath[] = {{1000.0f, 152.0f}, {1040.0f, 160.0f}, {1068.0f, 162.0f}};
constexpr Waypoint kTunnelPath[] = {{1030.0f, 108.0f}, {990.0f, 106.0f}, {961.0f, 112.0f},
                                    {966.0f, 116.5f}, {971.75f, 120.75f}};
constexpr Waypoint kLeavePath[] = {{976.0f, 125.5f}, {981.0f, 131.0f}, {982.5f, 134.0f},
                                   {982.5f, 146.0f}, {981.0f, 148.5f}, {976.0f, 154.0f},
                                   {970.0f, 159.5f}, {965.0f, 164.0f}};
// Round the ruins to behind her: from the north-west mouth north of the north-west mound, east,
// south along the ring's east roof to her hind side (she peeps from route 4's GP facing WNW).
constexpr Waypoint kAroundPath[] = {{964.0f, 172.0f}, {996.0f, 172.0f}, {996.0f, 128.0f}};
constexpr float kArrive = 0.6f;       // m: a waypoint reached
// The tail entry (authored, the script; measured on the peep: her tail tip hangs between her hind
// feet, 2–3 m from each, inside every hind footstep's 4.35 m GROUNDDEST_SHOCK2 sphere): wait
// kStagingBehind m behind the tip (along her root -> tip), go in once watch_loop has run
// kGoAfterLoop s (watch_start's last footstep shock, f42–44 + 18 frames at 30 fps, has ended),
// give up past kGiveUpInEnd s into watch_end (its first footstep, f15 = 1.0 s).
constexpr float kStagingBehind = 7.0f;
constexpr float kGoAfterLoop = 0.5f;
constexpr float kGiveUpInEnd = 0.9f;
constexpr float kDodgeOff = 14.0f;    // m off her facing line (authored: past the stomps' hooves)
constexpr float kWhistleEvery = 1.0f; // s between whistle presses while hiding (the unit test's)
// Authored (the script): the grip below which it holds still through a shake.
constexpr float kHoldBelow = 30.0f;
// The neck trigger sits on a hung band (neck_low) where her neck swing drains the grip while the
// rider is on it: it goes up from the withers (walked: the grip regenerates) only with
// kRestTo grip, and strikes as soon as it reaches the trigger with kStrikeHung. On the head
// (walked) it strikes between her shakes with kStrikeWalked.
constexpr float kRestTo = 95.0f;
constexpr float kStrikeHung = 10.0f;
constexpr float kStrikeWalked = 40.0f;
constexpr float kReachMargin = 0.2f;   // m inside the shell before a charge starts
constexpr float kDeathStillAt = 4.5f;  // s into the death: mid-fall
// The neck-down still's side view (authored): her standing root height (9.86 m, KI) up, 32 m off.
constexpr float kSideHeight = 9.86f;
constexpr float kSideDistance = 32.0f;
constexpr float kSidePitch = 8.0f;
// Her neck-down stages' game flags (data, graph findings: 587 "neck down 1", 588 "neck down 2").
constexpr int kNeckFlag1 = 587;
constexpr int kNeckFlag2 = 588;

bool trace() {
    static const bool on = std::getenv("SOTC_S13_TRACE") != nullptr;
    return on;
}

void next(S13Script& sc, int phase) {
    sc.phasesDone = std::max(sc.phasesDone, phase);
    sc.phase = phase;
    sc.phaseSeconds = 0.0f;
    sc.step = 0;
    sc.stepSeconds = 0.0f;
    sc.lastDistance = -1.0f;
    sc.waypoint = 0;
    sc.axis = 0;
    sc.along = 1.0f;
    sc.flips = 0;
    sc.progressSeconds = 0.0f;
    sc.detourSeconds = 0.0f;
}

KirinFight& fight(StageCtx& ctx) { return ctx.kirin; }
KnightCore& core(StageCtx& ctx) { return ctx.kirin.base.core; }

const char* clip_name(StageCtx& ctx) { return knight_clip_name(core(ctx), core(ctx).motion.clip); }
bool clip_starts(StageCtx& ctx, const char* prefix) { return std::string(clip_name(ctx)).rfind(prefix, 0) == 0; }
const char* mode_name(StageCtx& ctx, int mode) { return knight_mode_name(ctx.kirin.base, mode); }

int entity_frame(StageCtx& ctx) { return static_cast<int>(std::floor(core(ctx).motion.time * 30.0f)); }

void walk_along(StageCtx& ctx, PlayerInput& in, float hx, float hz, bool run) {
    const float h = std::atan2(hx, hz) / kDegToRad;
    ctx.player.yawDeg = h;
    ctx.follow.yawOffsetDeg = 0.0f;
    ctx.follow.pitchOffsetDeg = 0.0f;
    ctx.camera.yaw = std::remainder(h + 180.0f, 360.0f);
    in.forward = true;
    in.run = run;
}

// Toward (x, z), sidestepping a stall (S12's): 1.5 s without 0.5 m of progress turns the walk
// 70° aside for 1 s, alternating sides. True once within `arrive`.
bool walk_to(S13Script& sc, StageCtx& ctx, PlayerInput& in, float x, float z, bool run, float arrive) {
    const Vec3 me = ctx.player.position;
    if (std::hypot(x - me.x, z - me.z) <= arrive) return true;
    float hx = x - me.x, hz = z - me.z;
    sc.progressSeconds += kFixedFrameSeconds;
    if (std::hypot(me.x - sc.progressFrom.x, me.z - sc.progressFrom.z) > 0.5f) {
        sc.progressFrom = me;
        sc.progressSeconds = 0.0f;
    } else if (sc.progressSeconds > 1.5f && sc.detourSeconds <= 0.0f) {
        sc.detourSeconds = 1.0f;
        sc.detourSide = -sc.detourSide;
        sc.progressSeconds = 0.0f;
    }
    if (sc.detourSeconds > 0.0f) {
        sc.detourSeconds -= kFixedFrameSeconds;
        const float a = sc.detourSide * 70.0f * kDegToRad;
        const float rx = hx * std::cos(a) + hz * std::sin(a), rz = -hx * std::sin(a) + hz * std::cos(a);
        hx = rx;
        hz = rz;
    }
    walk_along(ctx, in, hx, hz, run);
    return false;
}

// Along `path` from sc.waypoint; true at its end.
template <std::size_t N>
bool follow(S13Script& sc, StageCtx& ctx, PlayerInput& in, const Waypoint (&path)[N], bool run) {
    while (sc.waypoint < static_cast<int>(N)) {
        const Waypoint& w = path[sc.waypoint];
        if (!walk_to(sc, ctx, in, w.x, w.z, run, kArrive)) return false;
        ++sc.waypoint;
        in = PlayerInput{};
    }
    return true;
}

int fail(const S13Script& sc, const std::string& message) {
    std::fprintf(stderr, "sotc-viewer: --%s: %s\n", std::string(sc.prefix) == "s13" ? "s13-selftest" : "s13-e2e",
                 message.c_str());
    return 1;
}

const std::vector<Mat4>& entity_palette(StageCtx& ctx) { return ctx.play.palettes[ctx.entityRig]; }

JointLookup joints(StageCtx& ctx) {
    return [&ctx](std::string_view name, Vec3& out) {
        return posed_joint_world_position(ctx.scene, ctx.entityRig, entity_palette(ctx), std::string(name), out);
    };
}

bool shell(StageCtx& ctx, int weakpoint, Vec3& centre, float& radius) {
    const char* material = weakpoint_material_name(ctx.enc().weakpoints, weakpoint);
    return material != nullptr &&
           posed_weakpoint_proxy(ctx.scene, ctx.entityRig, entity_palette(ctx), material, centre, radius);
}

bool attached(const StageCtx& ctx) { return mode_is_attached(ctx.combat.mode); }
bool on_ground(const StageCtx& ctx) {
    return ctx.combat.mode == PlayerMode::Grounded || ctx.combat.mode == PlayerMode::Staggered;
}
const char* band_name(const StageCtx& ctx) { return climb_band_name(ctx.enc().bands, ctx.combat.band); }
bool tail_band(int band) { return band >= kd::B::TailStart && band <= kd::B::TailEnd; }

// A refuse clip (base or layer) is shaking her now (kShakes' rows, active or not).
std::string shaking(StageCtx& ctx) {
    const KnightCore& c = core(ctx);
    for (const kd::KirinShake& k : kd::kShakes) {
        const int clip = scr_clip(c.graph, k.clip);
        if (clip >= 0 && (clip == c.motion.clip || additive_playing(fight(ctx).state.layers, clip))) return k.clip;
    }
    return "";
}

// The additive clips playing, comma-separated.
std::string layers_playing(StageCtx& ctx) {
    std::string out;
    for (const AddLayer& l : fight(ctx).state.layers.slot) {
        if (l.state == AddLayer::Free) continue;
        out += (out.empty() ? "" : ",") + std::string(knight_clip_name(core(ctx), l.clip));
    }
    return out.empty() ? "none" : out;
}

// kStagingBehind m behind her tail tip, along her root -> tip (x/z).
Vec3 tail_staging(StageCtx& ctx) {
    Vec3 tip{};
    joints(ctx)(climb_band(ctx.enc().bands, kd::B::TailLoop).boneA, tip);
    const Vec3 her = ctx.encounter.entityPosition;
    const float dx = tip.x - her.x, dz = tip.z - her.z, d = std::max(1e-3f, std::hypot(dx, dz));
    return {tip.x + kStagingBehind * dx / d, tip.y, tip.z + kStagingBehind * dz / d};
}

// One step of the climb toward `target` up the chain (W), down it (S). Her shakes are long and
// repeat (refuse_body loops while a rider is on her back), their drain small: it climbs through
// them, and holds still only while shaken with less than kHoldBelow grip.
void climb_toward(StageCtx& ctx, PlayerInput& in, int target) {
    in.grab = true;
    const int band = ctx.combat.band;
    if (band < 0 || band == target) return;
    if (!shaking(ctx).empty() && ctx.combat.stamina < kHoldBelow) return;
    const bool up = target > band || tail_band(band);
    in.forward = up;
    in.back = !up;
}

// The strike routine on `target`: along/around the band until a release would reach its
// shell, charge to full, release, then re-grip in the catch window (S11's/S12's).
void strike_routine(S13Script& sc, StageCtx& ctx, PlayerInput& in, int target, int home) {
    sc.stepSeconds += kFixedFrameSeconds;
    const bool hungHome = !climb_band(ctx.enc().bands, home).horizontal;
    switch (sc.step) {
        case 0: {
            in.grab = true;
            if (!attached(ctx)) return;
            if (ctx.combat.band != home) {
                // Off its band: rest on a walked one first when the band is hung.
                const bool walkedHere = climb_band(ctx.enc().bands, ctx.combat.band).horizontal;
                if (!(hungHome && walkedHere && ctx.combat.stamina < kRestTo)) climb_toward(ctx, in, home);
                sc.lastDistance = -1.0f;
                return;
            }
            if ((!hungHome && !shaking(ctx).empty()) ||
                ctx.combat.stamina < (hungHome ? kStrikeHung : kStrikeWalked)) {
                sc.lastDistance = -1.0f;
                return;
            }
            const WeakpointProxyLookup inner = [&ctx](int w, Vec3& c, float& r) {
                if (!shell(ctx, w, c, r)) return false;
                r -= kReachMargin;
                return r > 0.0f;
            };
            if (strike_reaches_shell(ctx.player.position, target, inner)) {
                sc.step = 1;
                sc.stepSeconds = 0.0f;
                in.strike = true;
                return;
            }
            Vec3 centre{};
            float radius = 0.0f;
            if (!shell(ctx, target, centre, radius)) return;
            const float d = player_capsule_distance(ctx.player.position, centre);
            if (sc.lastDistance >= 0.0f && d > sc.lastDistance + 1e-4f) {
                if (sc.axis == 0) sc.along = -sc.along;
                else sc.lateral = -sc.lateral;
                if (++sc.flips >= 2) {
                    sc.axis = 1 - sc.axis;
                    sc.flips = 0;
                }
            } else if (sc.lastDistance >= 0.0f && d < sc.lastDistance - 1e-4f) {
                sc.flips = 0;
            }
            sc.lastDistance = d;
            // Never off the band's ends while searching along it.
            if (sc.axis == 0 && ((sc.along < 0.0f && ctx.combat.bandT < 0.05f) ||
                                 (sc.along > 0.0f && ctx.combat.bandT > 0.95f))) {
                sc.along = -sc.along;
            }
            if (sc.axis == 0) {
                in.forward = sc.along > 0.0f;
                in.back = sc.along < 0.0f;
            } else {
                in.right = sc.lateral > 0.0f;
                in.left = sc.lateral < 0.0f;
            }
            return;
        }
        case 1:
            in.grab = true;
            in.strike = true;
            if (sc.stepSeconds + 1e-4f >= ctx.tuning.chargeSeconds) {
                sc.step = 2;
                sc.stepSeconds = 0.0f;
            }
            return;
        case 2:
            in.grab = true;
            ++sc.strikes;
            sc.step = 3;
            return;
        case 3:
            sc.step = 4;
            return;
        case 4:
            in.grab = true;
            sc.step = 0;
            sc.lastDistance = -1.0f;
            return;
        default:
            return;
    }
}

// Her coverage against her own registry (the coverage report's categories, D13): predicates and
// actions by fn (the evaluator's `sub` counts as implemented), control-def opcodes, locomotion kinds
// (registry.kindGaps as gaps), and the foot-IK aim (a gap when a row names an aim bone, §13 ruling 4).
struct Cover {
    int implemented = 0, gap = 0, missing = 0;
};
std::string coverage_line(const KnightCore& c, Cover& total) {
    const ScrRegistry& r = c.registry;
    const auto fn_status = [&r](uint32_t fn, bool action) {
        if (r.gaps.count(fn) != 0) return 1;
        if (fn == kScrSubFn || (action ? r.actions.count(fn) : r.predicates.count(fn)) != 0) return 0;
        return 2;
    };
    std::set<uint32_t> preds, acts;
    std::set<int> ops, kinds;
    for (const ScrCondition& cond : c.graph.conditions) {
        for (const ScrClause& cl : cond.clauses) preds.insert(cl.fn);
    }
    for (const ScrActionList& list : c.graph.actions) {
        for (const ScrAction& a : list.records) acts.insert(a.fn);
    }
    for (const auto& [handle, recs] : c.graph.controlDefs) {
        for (const ScrCdefRecord& rec : recs) ops.insert(rec.op);
    }
    for (const ScrMode& m : c.graph.modes) {
        if (!m.locomotion.row.empty()) kinds.insert(m.locomotion.kind);
    }
    std::string out;
    const auto add = [&out, &total](const char* name, const Cover& v) {
        char buf[64];
        std::snprintf(buf, sizeof buf, " %s=%d/%d/%d", name, v.implemented, v.gap, v.missing);
        out += buf;
        total.implemented += v.implemented;
        total.gap += v.gap;
        total.missing += v.missing;
    };
    const auto bump = [](Cover& v, int s) { (s == 0 ? v.implemented : s == 1 ? v.gap : v.missing) += 1; };
    Cover p, a, o, k;
    for (uint32_t fn : preds) bump(p, fn_status(fn, false));
    for (uint32_t fn : acts) bump(a, fn_status(fn, true));
    for (int op : ops) {
        int s = 2;
        for (const ScrCdefOp& known : kScrCdefOps) {
            if (known.op == op) s = known.gap ? 1 : 0;
        }
        bump(o, s);
    }
    for (int kind : kinds) bump(k, r.kindGaps.count(kind) != 0 ? 1 : r.kinds.count(kind) != 0 ? 0 : 2);
    Cover f;
    for (const auto& [handle, rows] : c.graph.attackdata) {
        if (std::any_of(rows.begin(), rows.end(), [](const ScrAttackRow& row) { return !row.aimBone.empty(); })) {
            bump(f, 1);  // scr_registry_dump's `feature aim gap`
            break;
        }
    }
    add("preds", p);
    add("acts", a);
    add("ops", o);
    add("kinds", k);
    add("feats", f);
    return out;
}

}  // namespace

bool s13_start(S13Script& sc, StageCtx& ctx, double loadMs) {
    if (&ctx.enc() != &encounters::kKirin || !ctx.kirin_wired || ctx.entityRig >= ctx.playbacks.size() ||
        !ctx.death_wired) {
        return false;
    }
    const KnightCore& c = core(ctx);
    const KirinState& k = fight(ctx).state;
    std::printf("%s-load stage=arena encounter=%s meshes=%zu ground=%dx%d layers=%d entity=%.2f,%.2f,%.2f "
                "yaw=%.2f spawn=%.2f,%.2f,%.2f return=%d graph=%s modes=%zu edges=%zu add_edges=%zu routes=%zu "
                "gps=%zu nodes=%zu terrain=%zu boss_ground=%zu start=%s clip=%s ms=%.1f\n",
                sc.prefix, ctx.enc().id, ctx.scene.meshes.size(), ctx.heightmap.width, ctx.heightmap.height,
                ctx.heightmap.maxLayers, ctx.encounter.entityPosition.x, ctx.encounter.entityPosition.y,
                ctx.encounter.entityPosition.z, ctx.encounter.entityYawDeg, ctx.stage_spawn.x, ctx.stage_spawn.y,
                ctx.stage_spawn.z, ctx.return_wired ? 1 : 0, c.graph.boss.c_str(), c.graph.modes.size(),
                c.graph.motionGraph.size(), k.addEdges.size(), k.routes.routes.size(), k.routes.gps.size(),
                k.routes.nodes.size(), k.terrain.stage.size(), k.terrain.boss.size(),
                mode_name(ctx, c.rt.pending), clip_name(ctx), loadMs);
    return true;
}

PlayerInput s13_input(S13Script& sc, StageCtx& ctx) {
    PlayerInput in{};
    sc.whistle = false;
    sc.seconds += kFixedFrameSeconds;
    sc.phaseSeconds += kFixedFrameSeconds;
    ++sc.frames;
    sc.still.clear();
    sc.stillSide = false;
    const Vec3 me = ctx.player.position;
    const Vec3 her = ctx.encounter.entityPosition;
    switch (sc.phase) {
        case kApproach:
            if (follow(sc, ctx, in, kApproachPath, /*run=*/false)) {
                walk_to(sc, ctx, in, her.x, her.z, /*run=*/false, 0.0f);
            }
            break;
        case kDodge: {
            // Across her facing line, away from her disc's side the player is on.
            const float r = ctx.encounter.entityYawDeg * kDegToRad;
            const float off = (me.x - her.x) * std::cos(r) - (me.z - her.z) * std::sin(r);
            const float side = off >= 0.0f ? 1.0f : -1.0f;
            if (std::fabs(off) < kDodgeOff) walk_along(ctx, in, side * std::cos(r), -side * std::sin(r), true);
            break;
        }
        case kToTunnel:
            follow(sc, ctx, in, kTunnelPath, /*run=*/true);
            break;
        case kPeep:
            // Still in the stair; a whistle press once a second (ALWAYS:C120, her whistle rows).
            sc.whistle = std::fmod(sc.phaseSeconds, kWhistleEvery) < kFixedFrameSeconds;
            break;
        case kLeave:
            follow(sc, ctx, in, kLeavePath, /*run=*/true);
            break;
        case kToTail: {
            // Round her front (she peeps WNW) to behind her, wait off her hind feet's footstep
            // shocks until the loop's quiet frames, then run in under the tail and grab its tip.
            if (sc.step == 0) {
                if (follow(sc, ctx, in, kAroundPath, /*run=*/true)) {
                    sc.step = 1;
                    sc.staging = tail_staging(ctx);
                }
                break;
            }
            if (sc.step == 1) {
                if (walk_to(sc, ctx, in, sc.staging.x, sc.staging.z, /*run=*/true, kArrive)) {
                    in = PlayerInput{};
                    const std::string clip = clip_name(ctx);
                    if (clip == "kirin_a_watch_loop" && core(ctx).motion.time >= kGoAfterLoop) {
                        if (sc.retries == 0) sc.still = "peeping";  // behind her, as she peeps
                        sc.step = 2;
                    }
                }
                break;
            }
            const ClimbBandId reach = nearest_climb_band(ctx.enc().bands, joints(ctx), me, ctx.tuning.grabRange,
                                                         clip_name(ctx), entity_frame(ctx));
            if (tail_band(reach)) {
                in.grab = !sc.grabPressed;  // a fresh press
                sc.grabPressed = !sc.grabPressed;
                break;
            }
            // Too late in the window (her watch_end footsteps come next): back to the staging spot.
            if (!clip_starts(ctx, "kirin_a_watch_loop") &&
                !(clip_starts(ctx, "kirin_a_watch_end") && core(ctx).motion.time < kGiveUpInEnd)) {
                ++sc.retries;
                sc.step = 1;
                break;
            }
            Vec3 tip{};
            if (joints(ctx)(climb_band(ctx.enc().bands, kd::B::TailLoop).boneA, tip)) {
                walk_to(sc, ctx, in, tip.x, tip.z, /*run=*/true, 0.2f);
            }
            break;
        }
        case kClimb:
            climb_toward(ctx, in, kd::B::Withers);
            break;
        case kNeck:
            strike_routine(sc, ctx, in, kd::W::NeckTrigger, kd::B::NeckLow);
            break;
        case kToHead:
        case kShake:
            climb_toward(ctx, in, kd::B::Head);
            break;
        case kHeadSigil:
            strike_routine(sc, ctx, in, kd::W::HeadSigil, kd::B::Head);
            break;
        default:
            break;
    }
    return in;
}

std::optional<int> s13_after(S13Script& sc, StageCtx& ctx) {
    const char* p = sc.prefix;
    KirinFight& f = fight(ctx);
    KnightCore& c = core(ctx);
    PlayerCombat& combat = ctx.combat;
    Encounter& encounter = ctx.encounter;
    const DeathState& death = ctx.play.death;
    const Vec3 me = ctx.player.position, her = encounter.entityPosition;
    if (trace() && sc.frames % 15 == 0) {
        Vec3 tip{}, bl{}, br{};
        joints(ctx)("Bone_TAIL", tip);
        joints(ctx)("Bone_B_TOE_L", bl);
        joints(ctx)("Bone_B_TOE_R", br);
        std::fprintf(stderr,
                     "trace t=%.2f ph=%s wp=%d mode=%s clip=%s ct=%.2f route=%d gp=%d her=%.1f,%.1f,%.1f yaw=%.1f "
                     "me=%.1f,%.1f,%.1f d=%.1f tail=%.1f,%.1f,%.1f pm=%s band=%s bt=%.2f st=%.0f hp=%.0f php=%.0f "
                     "layers=%s gw=%llx pw=%s bl=%.1f,%.1f br=%.1f,%.1f\n",
                     sc.seconds, kPhaseNames[sc.phase], sc.waypoint, mode_name(ctx, c.rt.current), clip_name(ctx),
                     c.motion.time, c.rt.route.route, c.rt.route.gp, her.x, her.y, her.z, encounter.entityYawDeg,
                     me.x, me.y, me.z, std::hypot(me.x - her.x, me.z - her.z), tip.x, tip.y, tip.z,
                     player_mode_name(combat.mode), band_name(ctx), combat.bandT, combat.stamina, encounter.health,
                     combat.health, layers_playing(ctx).c_str(),
                     static_cast<unsigned long long>(kirin_attr_word(terrain_boss_ground(f.state.terrain, her))),
                     kirin_attr_word(terrain_at(f.state.terrain, me)) >> 48 ? "tunnel" : "-", bl.x, bl.z, br.x, br.z);
    }
    if (sc.phase != kReset && sc.phaseSeconds > kBudget[sc.phase]) {
        char where[240];
        std::snprintf(where, sizeof(where), "phase %s over its %.0f s budget (mode %s, clip %s, player %s, band %s, "
                      "%.1f,%.1f,%.1f; her %.1f,%.1f,%.1f)", kPhaseNames[sc.phase], kBudget[sc.phase],
                      mode_name(ctx, c.rt.current), clip_name(ctx), player_mode_name(combat.mode), band_name(ctx),
                      me.x, me.y, me.z, her.x, her.y, her.z);
        return fail(sc, where);
    }
    if (combat.mode == PlayerMode::Dead) {
        return fail(sc, std::string("the player died in phase ") + kPhaseNames[sc.phase]);
    }
    // The graph's mode transitions, named.
    if (c.rt.current != sc.lastMode) {
        std::printf("%s-mode from=%s to=%s clip=%s at=%.1f\n", p, mode_name(ctx, sc.lastMode),
                    mode_name(ctx, c.rt.current), clip_name(ctx), sc.seconds);
        sc.lastMode = c.rt.current;
    }
    // The route follower: the route and GP it picks, then her arrival there (her root within the
    // runtime's arrive radius of the GP), once per pick.
    if (c.rt.route.route != sc.lastRoute || c.rt.route.gp != sc.lastGp) {
        if (c.rt.route.route >= 0 || sc.lastRoute >= 0) {
            std::printf("%s-route event=pick route=%d gp=%d kind=%d mode=%s at=%.1f\n", p, c.rt.route.route,
                        c.rt.route.gp, c.rt.route.kind, mode_name(ctx, c.rt.current), sc.seconds);
        }
        sc.lastRoute = c.rt.route.route;
        sc.lastGp = c.rt.route.gp;
        sc.arrivedGp = false;
    }
    if (c.rt.route.gp >= 0 && !sc.arrivedGp) {
        const Vec3 gp = f.state.routes.gps[static_cast<size_t>(c.rt.route.gp)].pos;
        if (std::hypot(gp.x - her.x, gp.z - her.z) <= c.rt.arriveRadius) {
            sc.arrivedGp = true;
            if (sc.arrivedAt < 0.0f) sc.arrivedAt = sc.seconds;
            std::printf("%s-route event=arrive route=%d gp=%d kind=%d mode=%s arrived=%.1f at=%.1f\n", p,
                        c.rt.route.route, c.rt.route.gp, c.rt.route.kind, mode_name(ctx, c.rt.current), sc.seconds,
                        sc.seconds);
        }
    }
    // Her damage on the player: never in this script (the dodge proves the stomp missed).
    for (const DamageHit& h : ctx.kirinHits) {
        if (h.amount > 0.0f) {
            return fail(sc, "a damage row landed: " + h.name + " in phase " + kPhaseNames[sc.phase]);
        }
    }
    // The climb's route, once per band.
    const int band = attached(ctx) ? static_cast<int>(combat.band) : -1;
    if (band != sc.lastBand && band >= 0) {
        std::printf("%s-climb band=%s t=%.2f stamina=%.1f clip=%s layers=%s at=%.1f\n", p, band_name(ctx),
                    combat.bandT, combat.stamina, clip_name(ctx), layers_playing(ctx).c_str(), sc.seconds);
        const std::string name = tail_band(band) ? "tail" : band_name(ctx);
        if (sc.route.empty() || sc.route.back() != name) sc.route.push_back(name);
        if (band == kd::B::Withers && sc.route.size() == 3) sc.still = "climb";  // the first time on it
        if (band == kd::B::NeckHigh && sc.phase == kToHead) {
            sc.still = "neck_down_2";  // up her lowered neck, seen from her side
            sc.stillSide = true;
        }
    }
    sc.lastBand = band;
    // Her shakes, attached through one: the survival.
    const std::string shake = shaking(ctx);
    const bool shakeEnded = sc.shookLast && shake.empty();
    if (!shake.empty()) {
        if (!sc.shookLast) {
            sc.shakeClip = shake;
            sc.shakeStart = combat.stamina;
            sc.shakeLow = combat.stamina;
            sc.shakeDrain = 0.0f;
            sc.sawShake = attached(ctx);
        }
        sc.shakeLow = std::min(sc.shakeLow, combat.stamina);
        sc.shakeDrain = std::max(sc.shakeDrain, kirin_fight_shake(f).staminaDrain);
        sc.sawShake = sc.sawShake && attached(ctx);
    }
    sc.shookLast = !shake.empty();
    // The first shake met on the hung neck (neck_low: the grip drains there, nothing regenerates):
    // its clip, its strongest drain and the stamina it cost until it ended, the climber left the
    // band, or the grip broke (a strike, a drop). On this route it is her base refuse_tail still
    // playing (the S12 rule drains wherever the rider is); her neck swing (add_neck_refuse_*) is not
    // met on it (the climber leaves neck_low within ~3 s each visit: a strike or a grip break).
    const bool onNeck = attached(ctx) && combat.band == kd::B::NeckLow;
    if (!sc.hungDone && !sc.hungActive && onNeck && !shake.empty()) {
        sc.hungActive = true;
        sc.hungClip = shake;
        sc.hungStart = combat.stamina;
        sc.hungFrom = sc.seconds;
        sc.hungDrain = 0.0f;
    }
    if (sc.hungActive) {
        if (onNeck && !shake.empty()) {
            sc.hungEnd = combat.stamina;
            sc.hungDrain = std::max(sc.hungDrain, kirin_fight_shake(f).staminaDrain);
        } else {
            std::printf("%s-shake_hung clip=%s band=neck_low drain=%.1f start=%.1f end=%.1f loss=%.1f seconds=%.2f "
                        "ended=%s at=%.1f\n", p, sc.hungClip.c_str(), sc.hungDrain, sc.hungStart, sc.hungEnd,
                        sc.hungStart - sc.hungEnd, sc.seconds - sc.hungFrom,
                        !attached(ctx) ? "grip" : !onNeck ? "left_band" : "shake_end", sc.seconds);
            sc.hungActive = false;
            sc.hungDone = true;
        }
    }
    // After the peep: the first frame her root's ground word changes off the ruins' boss ground — the
    // ground handoff (her ground: the boss layer, else the layered heightmap within kGroundStep).
    // Ruling R-AL: her modes 3 and 14 are locomotion kind 1, which the shared slowleg does not steer
    // (S12 R1: no waypoint, no request), so this walk is the else-edge walk loop on her heading, not
    // a chosen exit.
    const uint64_t ground = kirin_attr_word(terrain_boss_ground(f.state.terrain, her));
    if (sc.peeped && !sc.leftRuins && ground != sc.peepGround && !encounter.defeated) {
        sc.leftRuins = true;
        std::printf("%s-ground_handoff from=0x%llx to=0x%llx her=%.1f,%.1f,%.1f yaw=%.1f mode=%s clip=%s rider=%s "
                    "at=%.1f\n", p,
                    static_cast<unsigned long long>(sc.peepGround), static_cast<unsigned long long>(ground), her.x,
                    her.y, her.z, encounter.entityYawDeg, mode_name(ctx, c.rt.current), clip_name(ctx),
                    attached(ctx) ? band_name(ctx) : "none", sc.seconds);
    }
    if (ctx.kirinBackstop) {
        std::printf("%s-backstop at=%.1f\n", p, sc.seconds);
    }

    switch (sc.phase) {
        case kApproach:
            if (clip_starts(ctx, "kirin_a_atack_leg_") || clip_starts(ctx, "kirin_a_threat")) {
                sc.stompClip = clip_name(ctx);
                std::printf("%s-stomp clip=%s mode=%s dist=%.1f at=%.1f\n", p, clip_name(ctx),
                            mode_name(ctx, c.rt.current), std::hypot(me.x - her.x, me.z - her.z), sc.seconds);
                next(sc, kDodge);
            }
            break;
        case kDodge: {
            const float r = encounter.entityYawDeg * kDegToRad;
            sc.stompOff = std::max(sc.stompOff, std::fabs((me.x - her.x) * std::cos(r) - (me.z - her.z) * std::sin(r)));
            sc.stompNear = std::min(sc.stompNear, std::hypot(me.x - her.x, me.z - her.z));
            for (const DamageHit& h : f.base.overlaps) {
                if (h.amount > 0.0f) ++sc.stompRows;
            }
            if (!clip_starts(ctx, "kirin_a_atack_leg_") && !clip_starts(ctx, "kirin_a_threat")) {
                std::printf("%s-dodge clip=%s rows=%d off_line=%.2f nearest=%.2f player_hp=%.0f end_clip=%s at=%.1f\n",
                            p, sc.stompClip.c_str(), sc.stompRows, sc.stompOff, sc.stompNear, combat.health,
                            clip_name(ctx), sc.seconds);
                if (sc.stompRows != 0) return fail(sc, "a stomp's damage volume overlapped the player");
                next(sc, kToTunnel);
            }
            break;
        }
        case kToTunnel:
            if (sc.waypoint >= static_cast<int>(std::size(kTunnelPath))) {
                const TerrainHit t = terrain_at(f.state.terrain, me);
                std::printf("%s-hide class=%d at=%.1f,%.1f,%.1f mode=%s her_dist=%.1f t=%.1f\n", p, t.found ? t.cls : -1,
                            me.x, me.y, me.z, mode_name(ctx, c.rt.current), std::hypot(me.x - her.x, me.z - her.z),
                            sc.seconds);
                next(sc, kPeep);
            }
            break;
        case kPeep: {
            if ((c.rt.current == 11 || c.rt.current == 12) && clip_starts(ctx, "kirin_a_watch")) {
                const uint64_t owner = kirin_attr_word(terrain_boss_ground(f.state.terrain, her));
                std::printf("%s-peep mode=%s route=%d gp=%d ground=0x%llx arrived=%.1f clip=%s her=%.1f,%.1f,%.1f "
                            "yaw=%.1f at=%.1f\n",
                            p, mode_name(ctx, c.rt.current), c.rt.route.route, c.rt.route.gp,
                            static_cast<unsigned long long>(owner), sc.arrivedAt, clip_name(ctx), her.x, her.y,
                            her.z, encounter.entityYawDeg, sc.seconds);
                sc.peeped = true;
                sc.peepGround = owner;
                next(sc, kLeave);
            }
            break;
        }
        case kLeave:
            if (sc.waypoint >= static_cast<int>(std::size(kLeavePath))) next(sc, kToTail);
            break;
        case kToTail:
            if (attached(ctx) && tail_band(combat.band)) {
                sc.entryClip = clip_name(ctx);
                sc.entryFrame = entity_frame(ctx);
                const ClimbBand& b = climb_band(ctx.enc().bands, combat.band);
                std::printf("%s-entry kind=tail band=%s clip=%s frame=%d window=%d..%d mode=%s retries=%d "
                            "player_hp=%.0f at=%.1f\n", p, band_name(ctx), sc.entryClip.c_str(), sc.entryFrame,
                            b.enableFrom, b.enableTo, mode_name(ctx, c.rt.current), sc.retries, combat.health,
                            sc.seconds);
                next(sc, kClimb);
            }
            break;
        case kClimb:
            if (!attached(ctx)) return fail(sc, "the climber fell on the way to the withers");
            if (combat.band == kd::B::Withers) next(sc, kNeck);
            break;
        case kNeck: {
            if (on_ground(ctx)) return fail(sc, "the catch missed at the neck");
            if (ctx.kirinHit.weakpoint == kd::W::NeckTrigger) ++sc.necks;
            // A stage is down once the graph sets its flag (data: 587 neck down 1, 588 neck down 2).
            const int stage = sc.neckLines + 1;
            if (stage <= 2 && sc.necks >= stage && c.rt.flags.test(stage == 1 ? kNeckFlag1 : kNeckFlag2)) {
                std::printf("%s-neck stage=%d strikes=%d mode=%s layers=%s flags=%d,%d at=%.1f\n", p, stage,
                            sc.necks, mode_name(ctx, c.rt.current), layers_playing(ctx).c_str(),
                            c.rt.flags.test(kNeckFlag1) ? 1 : 0, c.rt.flags.test(kNeckFlag2) ? 1 : 0, sc.seconds);
                ++sc.neckLines;
            }
            if (sc.neckLines == 2 && sc.step == 0 && attached(ctx)) next(sc, kToHead);
            break;
        }
        case kToHead:
            if (!attached(ctx)) return fail(sc, "the climber fell on the way to the head");
            if (combat.band == kd::B::Head) {
                std::string route;
                for (const std::string& r : sc.route) route += (route.empty() ? "" : ",") + r;
                std::printf("%s-route_climb bands=%s stamina=%.1f layers=%s at=%.1f\n", p, route.c_str(),
                            combat.stamina, layers_playing(ctx).c_str(), sc.seconds);
                next(sc, kShake);
            }
            break;
        case kShake:
            if (!attached(ctx)) return fail(sc, "the shake threw the climber");
            if (shakeEnded && sc.sawShake) {
                std::printf("%s-shake survived=1 clip=%s band=%s drain=%.1f start=%.1f low=%.1f end=%.1f loss=%.1f "
                            "at=%.1f\n", p, sc.shakeClip.c_str(), band_name(ctx), sc.shakeDrain, sc.shakeStart,
                            sc.shakeLow, combat.stamina, sc.shakeStart - combat.stamina, sc.seconds);
                sc.strikes = 0;
                next(sc, kHeadSigil);
            }
            break;
        case kHeadSigil: {
            if (ctx.kirinHit.weakpoint == kd::W::HeadSigil) {
                std::printf("%s-strike weakpoint=%s damage=%.0f health=%.0f killed=%d at=%.1f\n", p,
                            weakpoint_material_name(ctx.enc().weakpoints, ctx.kirinHit.weakpoint),
                            ctx.kirinHit.damage, encounter.health, ctx.kirinHit.killed ? 1 : 0, sc.seconds);
            }
            if (encounter.defeated) {
                std::printf("%s-sigil target=head strikes=%d spent=%.0f health=%.0f at=%.1f\n", p, sc.strikes,
                            f.spent[kd::W::HeadSigil], encounter.health, sc.seconds);
                sc.diedAt = her;
                next(sc, kDeath);
            } else if (on_ground(ctx)) {
                return fail(sc, "the catch missed on the head");
            }
            break;
        }
        case kDeath: {
            const int phase = static_cast<int>(death.phase);
            if (phase != sc.lastDeathPhase) {
                std::printf("%s-death phase=%s total=%.3f mode=%s graph_clip=%s clip=%s lift=%.2f\n", p,
                            death_phase_name(death.phase), death.total, mode_name(ctx, f.base.deathMode),
                            knight_clip_name(c, f.base.deathGraphClip), clip_name(ctx), encounter.entityLift);
                sc.lastDeathPhase = phase;
            }
            if (death.total >= kDeathStillAt && death.total - kFixedFrameSeconds < kDeathStillAt) sc.still = "death";
            if (f.base.deathMode != c.graph.root.deathMode) return fail(sc, "the graph did not enter its death mode");
            if (death.phase == DeathPhase::Done) {
                const Playback& entity = ctx.playbacks[ctx.entityRig];
                const RigCpu& rig = ctx.scene.rigs[ctx.entityRig];
                const bool live = mesh_range_visible(ctx.scene, rig.meshBegin, rig.meshEnd);
                std::printf("%s-corpse live_body=%d clip=%s held=%d lift=%.2f moved=%.2f player=%s return=%s\n", p,
                            live ? 1 : 0, entity.clips[static_cast<std::size_t>(entity.index)].name.c_str(),
                            entity.pinned ? 1 : 0, encounter.entityLift,
                            std::hypot(her.x - sc.diedAt.x, her.z - sc.diedAt.z), player_mode_name(combat.mode),
                            return_phase_name(ctx.carry.ret.phase));
                if (!live || ctx.carry.ret.phase != ReturnPhase::Idle) {
                    return fail(sc, "the corpse did not stay (or the return ran)");
                }
                if (!sc.leftRuins) return fail(sc, "her root never handed off the ruins' boss ground after the peep");
                if (!sc.hungDone) return fail(sc, "no shake was met on the hung neck");
                sc.wantR = true;
                next(sc, kReset);
            }
            break;
        }
        case kReset: {
            if (sc.wantR) break;  // the caller presses R, then calls again
            const bool home = std::hypot(her.x - f.base.basePosition.x, her.z - f.base.basePosition.z) < 1e-3f &&
                              std::fabs(encounter.entityYawDeg - f.base.baseYawDeg) < 1e-3f;
            std::printf("%s-reset health=%.0f defeated=%d mode=%s clip=%s at_base=%d layers=%s player_hp=%.0f "
                        "pmode=%s death=%s\n",
                        p, encounter.health, encounter.defeated ? 1 : 0, mode_name(ctx, c.rt.pending),
                        clip_name(ctx), home ? 1 : 0, layers_playing(ctx).c_str(), combat.health,
                        player_mode_name(combat.mode), death_phase_name(death.phase));
            if (encounter.health != encounter.desc->maxHealth || encounter.defeated || !home ||
                death.phase != DeathPhase::Idle || c.rt.pending != c.graph.root.retryStartMode) {
                return fail(sc, "R did not bring Phaedra back to her opening state");
            }
            Cover total;
            const std::string cover = coverage_line(c, total);
            std::printf("%s-coverage boss=%s%s total=%d/%d/%d\n", p, c.graph.boss.c_str(), cover.c_str(),
                        total.implemented, total.gap, total.missing);
            if (total.missing != 0) return fail(sc, "her graph uses an item her registry does not know");
            return 0;
        }
        default:
            break;
    }
    return std::nullopt;
}

// --s13-selftest: the S13 timeline in Phaedra's arena (one stage).
SelftestResult run_s13(StageCtx& ctx, RunState& run) {
    const AppOptions& opts = run.opts;
    S13Script sc;
    sc.prefix = "s13";
    if (!s13_start(sc, ctx, ctx.loadMs)) {
        return fail(sc, "the --manifest arena is not Phaedra's (encounter kirin, her bake, graph, routes and "
                        "terrain) or her fight is unwired");
    }
    ctx.play.audioOn = run.audio.enabled;
    ctx.play.palettes = update_all_poses(run.renderer, ctx.scene, ctx.playbacks, 0.0f, ctx.club);
    FrameResult last;
    const auto render = [&]() {
        run.renderer.set_hud(ctx.hud_state(/*visible=*/true));
        run.renderer.render_frame(ctx.scene, camera_view_proj(ctx.camera, run.shell.win), /*acquire_swapchain=*/true,
                                  last.effect.active ? &last.effect : nullptr, nullptr);
    };
    for (;;) {
        SDL_PumpEvents();
        FrameInput in;
        in.dt = kFixedFrameSeconds;
        in.input = s13_input(sc, ctx);
        in.rideKeys.whistle = sc.whistle;
        last = frame_step(ctx, in);
        if (last.exitCode) return *last.exitCode;
        std::optional<int> code = s13_after(sc, ctx);
        if (!code && sc.wantR) {
            ctx.press_reset(ctx.play.death, ctx.play.deathCueFired, &ctx.play.deathCameraSet);
            sc.wantR = false;
            code = s13_after(sc, ctx);
        }
        if (!sc.still.empty() && !opts.s13StillsPath.empty()) {
            const Camera keep = ctx.camera;
            if (sc.stillSide) {
                // authored (the still only): her side, kSideDistance m off, her standing root height up.
                const Vec3 her = ctx.encounter.entityPosition;
                ctx.camera.target = {her.x, her.y + kSideHeight, her.z};
                ctx.camera.distance = kSideDistance;
                ctx.camera.yaw = std::remainder(ctx.encounter.entityYawDeg + 90.0f, 360.0f);
                ctx.camera.pitch = kSidePitch;
            }
            with_clocks_restored(ctx.playbacks, [&]() {
                render();
                write_screenshot(run.renderer, run.shell.win, still_png_path(opts.s13StillsPath, sc.still.c_str()));
            });
            ctx.camera = keep;
            sc.stillSide = false;
        }
        if (code) {
            if (*code != 0) return *code;
            break;
        }
    }
    render();  // warm-up
    const Uint64 perfStart = SDL_GetTicksNS();
    for (int i = 0; i < kS13PerfFrames; ++i) {
        FrameInput in;
        in.dt = kFixedFrameSeconds;
        last = frame_step(ctx, in);
        render();
    }
    const double perfMs = static_cast<double>(SDL_GetTicksNS() - perfStart) / 1e6;
    std::printf("s13-perf frames=%d frame_ms=%.3f drawn=%d culled=%d\n", kS13PerfFrames,
                perfMs / static_cast<double>(kS13PerfFrames), run.renderer.drawn_last_frame(),
                run.renderer.culled_last_frame());
    std::printf("s13-status phases_done=%d frames=%d seconds=%.1f health=%.0f\n", sc.phasesDone, sc.frames,
                sc.seconds, ctx.encounter.health);
    print_status_line(opts, run.shell.win);
    return 0;
}
