#include "selftests.h"

#include "capture.h"
#include "encounter_rt.h"
#include "s12_script.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ka = encounters::knight_detail;

namespace {

enum Phase {
    kApproach,   // walk at him until he starts a slam
    kDodge,      // run across the blade's line, past the impact
    kToBlade,    // to the stuck blade's low end; grab it when its window opens
    kBlade,      // walk the blade up; ride it as it lifts; off its hilt onto the forearm
    kClimb,      // forearm -> upper arm -> shoulder -> neck -> head
    kHeadSigil,  // strike the head sigil until its pool is spent
    kShake,      // hold on through a shake
    kToBelly,    // down the chain to the belly sigil
    kBellySigil, // strike the belly sigil to his death
    kDeath,      // watch the death to Done: the corpse stays
    kReset,      // R: his opening state
    kExposure,   // walk into his next slam: one damage row lands (s12-hit)
    kPhaseCount,
};
constexpr const char* kPhaseNames[kPhaseCount] = {"approach", "dodge", "to_blade", "blade", "climb",
                                                  "head_sigil", "shake", "to_belly", "belly_sigil",
                                                  "death", "reset", "exposure"};
constexpr float kBudget[kPhaseCount] = {60.0f, 20.0f, 20.0f, 20.0f, 90.0f, 150.0f,
                                        60.0f, 90.0f, 150.0f, 20.0f, 1.0f, 30.0f};
constexpr int kS12PerfFrames = 240;
// Where the dodge waits (ruling R-L): off his facing line by the slam's reach — the widest
// lateral reach of its damage rows (KNIGHT_SWORD_ATK1*, KNIGHT_ATK_SHOCK: the sidecar's
// rows of the slam's clips up to the box's last frame, posed on his clip-local skeleton at
// every active frame; slam_reach) + the capsule radius + kDodgeMargin — and kLowEndAhead
// along it; it leaves for the low end once that last frame has passed.
// authored (selftest): the margin, the clips, the ahead distance (measured on this arena:
// the blade's low end lands 26.5 m ahead of his root).
constexpr float kDodgeMargin = 0.3f;
constexpr float kLowEndAhead = 27.0f;
constexpr const char* kSlamClips[] = {"knight_a_atk1_start", "knight_a_atk1", "knight_a_atk1_2"};
constexpr int kSlamBoxLastFrame = 4;  // data: KNIGHT_SWORD_ATK1_DMG's window ends atk1_2 f4
// Authored (the script): the grip it keeps — hung below kRestBelow it heads for a walked
// band; onto the belly only above kEnterHungAbove (the climb down and three strikes).
constexpr float kRestBelow = 5.0f;  // well under kGateMargin: a hung band entered is crossed
constexpr float kEnterHungAbove = 85.0f;
constexpr float kFreshShake = 1.0f;  // s after his shake ended: still a fresh cooldown
constexpr float kGateMargin = 15.0f;  // grip over a hung band's climb cost (staminaClimb x its length)
// Authored (the script): a strike starts only this long after his shake ended — inside his
// cooldown (5 s on the head, double rate; 10 s on the belly) with the charge and the catch —
// and with this much grip (on the head the shake leaves ~5: the regen to 55 takes ~2 s).
constexpr float kStrikeWindowHead = 3.0f;
constexpr float kStrikeWindowBelly = 8.0f;
constexpr float kStrikeAboveHead = 55.0f;
constexpr float kStrikeAboveBelly = 25.0f;
constexpr float kReachMargin = 0.3f;  // m inside the shell before a charge starts
constexpr float kDeathStillAt = 4.5f;  // s into the death: mid-fall

bool trace() {
    static const bool on = std::getenv("SOTC_S12_TRACE") != nullptr;
    return on;
}

void next(S12Script& sc, int phase) {
    sc.phasesDone = std::max(sc.phasesDone, phase);
    sc.phase = phase;
    sc.phaseSeconds = 0.0f;
    sc.step = 0;
    sc.stepSeconds = 0.0f;
    sc.lastDistance = -1.0f;
}

void walk_along(StageCtx& ctx, PlayerInput& in, float hx, float hz, bool run) {
    const float h = std::atan2(hx, hz) / kDegToRad;
    ctx.player.yawDeg = h;
    ctx.follow.yawOffsetDeg = 0.0f;
    ctx.follow.pitchOffsetDeg = 0.0f;
    ctx.camera.yaw = std::remainder(h + 180.0f, 360.0f);
    in.forward = true;
    in.run = run;
}

const char* clip_name(const StageCtx& ctx) {
    return knight_clip_name(ctx.knight.core, ctx.knight.core.motion.clip);
}

int fail(const S12Script& sc, const std::string& message) {
    std::fprintf(stderr, "sotc-viewer: --%s: %s\n",
                 std::string(sc.prefix) == "s12" ? "s12-selftest" : "s12-e2e", message.c_str());
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

WeakpointProxyLookup proxy(StageCtx& ctx) {
    return [&ctx](int weakpoint, Vec3& centre, float& radius) { return shell(ctx, weakpoint, centre, radius); };
}

bool attached(const StageCtx& ctx) { return mode_is_attached(ctx.combat.mode); }

const char* band_name(const StageCtx& ctx) { return climb_band_name(ctx.enc().bands, ctx.combat.band); }

int entity_frame(const StageCtx& ctx) {
    return static_cast<int>(std::floor(ctx.knight.core.motion.time * 30.0f));
}

bool clip_is(const StageCtx& ctx, const char* name) { return std::string(clip_name(ctx)) == name; }

// The blade's low end (its posed lower joint).
bool blade_low_end(StageCtx& ctx, Vec3& out) {
    const ClimbBand& b = climb_band(ctx.enc().bands, ka::B::Blade);
    Vec3 a{}, e{};
    if (!climb_band_segment(b, joints(ctx), a, e)) return false;
    out = a.y <= e.y ? a : e;
    return true;
}

// The graph's own swing_space_clear clause (its args) on his pose now: the slam's rays.
bool rays_clear(StageCtx& ctx) {
    const ScrGraph& g = ctx.knight.core.graph;
    for (const ScrCondition& c : g.conditions) {
        for (const ScrClause& cl : c.clauses) {
            if (cl.pred != "swing_space_clear") continue;
            const ScrArg* f = scr_arg(cl.args, "f1c");
            const ScrArg* sd = scr_arg(cl.args, "f20");
            const ScrArg* sc = scr_arg(cl.args, "f24");
            ScrWorld w;
            w.bossPosition = ctx.encounter.entityPosition;
            w.bossYawDeg = ctx.encounter.entityYawDeg;
            const Heightmap& ground = ctx.heightmap;
            w.groundAt = [&ground](float x, float z, float& h) {
                h = heightmap_node(ground, Vec3{x, 0.0f, z});
                return std::isfinite(h);
            };
            return scr_swing_space_clear(w, f ? static_cast<float>(f->num) : 0.0f,
                                         sd ? static_cast<float>(sd->num) : 0.0f,
                                         sc ? static_cast<float>(sc->num) : 1.0f);
        }
    }
    return false;
}

// A slam damage row (the dodge's set): the blade's rows and the shock (KNIGHT_ATK_SHOCK2 is
// the stuck blade's, after the box's last frame).
bool slam_row(const std::string& name) {
    return name.rfind("KNIGHT_SWORD_ATK1", 0) == 0 || name == "KNIGHT_ATK_SHOCK";
}

// The slam's widest reach across his facing line (m) and its row: every slam row with an
// amount, posed at each active frame on his clip-local skeleton (glb frame: +z his front,
// so the reach across is |x| of the volume's farthest corner; a sphere's |x| + radius).
float slam_reach(StageCtx& ctx, std::string& widest) {
    const KnightFight& kn = ctx.knight;
    const RigCpu& rig = ctx.scene.rigs[ctx.entityRig];
    const Playback& entity = ctx.playbacks[ctx.entityRig];
    float reach = 0.0f;
    for (const char* clipName : kSlamClips) {
        const int ev = events_clip(kn.events, clipName);
        const int ci = clip_index_named(entity, clipName);
        if (ev < 0 || ci < 0) continue;
        const EventsClip& clip = kn.events.clips[static_cast<std::size_t>(ev)];
        const bool last = std::string(clipName) == kSlamClips[std::size(kSlamClips) - 1];
        for (const EventsVolume& row : clip.damage) {
            if (!row.hasDef || !(row.amount > 0.0f) || !slam_row(row.name)) continue;
            for (const auto& [from, to] : row.windows) {
                for (int f = from; f <= to && !(last && f > kSlamBoxLastFrame); ++f) {
                    const std::vector<Mat4> palette = animation_pose(
                        ctx.scene, ctx.entityRig, entity.clips[static_cast<std::size_t>(ci)], f / clip.fps);
                    PoseFrame pose;
                    pose.joint = [&rig, &palette](std::string_view name, Mat4& out) {
                        for (std::size_t j = 0; j < rig.skin.joints.size() && j < palette.size(); ++j) {
                            if (rig.skin.joints[j].name == name) {
                                out = mat4_mul(palette[j], rig.skin.joints[j].bindWorld);
                                return true;
                            }
                        }
                        return false;
                    };
                    const PosedVolume v = events_pose(row, static_cast<float>(f), pose, kn.optBones);
                    float x = 0.0f;
                    if (row.shape == EventsVolume::Sphere) {
                        x = std::fabs(v.centre.x) + v.size.x;
                    } else {
                        for (int c = 0; c < 8; ++c) {
                            float cx = v.centre.x;
                            for (int a = 0; a < 3; ++a) {
                                const float sz = a == 0 ? v.size.x : a == 1 ? v.size.y : v.size.z;
                                cx += ((c >> a) & 1 ? sz : -sz) * v.axis[a].x;
                            }
                            x = std::max(x, std::fabs(cx));
                        }
                    }
                    if (x > reach) {
                        reach = x;
                        widest = row.name;
                    }
                }
            }
        }
    }
    return reach;
}

// His shake (a refuse clip) is playing.
bool shaking(const StageCtx& ctx) {
    const std::string name = clip_name(ctx);
    for (const ka::KnightShake& k : ka::kShakes) {
        if (name == k.clip) return true;
    }
    return false;
}

Vec3 sub3(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

bool walked(StageCtx& ctx, int band) {
    return band >= 0 && band_walkable(climb_band(ctx.enc().bands, band), joints(ctx)) &&
           band_enabled(climb_band(ctx.enc().bands, band), clip_name(ctx), entity_frame(ctx));
}

// Hung bands between `band` and the nearest walked one along `up` (or `down`) links; 99 none.
int hung_to_walked(StageCtx& ctx, int band, bool up) {
    int n = band;
    for (int steps = 0; steps < 8; ++steps) {
        const ClimbBand& b = climb_band(ctx.enc().bands, n);
        n = up ? b.up : b.down;
        if (n < 0 || !band_enabled(climb_band(ctx.enc().bands, n), clip_name(ctx), entity_frame(ctx))) return 99;
        if (walked(ctx, n)) return steps;
    }
    return 99;
}

// The script's map of his bands (authored: the chain, with the belly off the shoulder's top):
// a band's place on the way from the blade (0) to the head (5); the belly hangs at 3.5.
float depth(int band) {
    return band == ka::B::Belly ? 3.5f : static_cast<float>(band);
}

// One step of the climb toward `target`: up (W) or down (S) the chain, W + D at the
// shoulder's top for the belly. On a walked band it stands through his shakes and steps onto
// a hung band only just after one with the grip to cross it; hung, shaken or tired, it goes
// onto a walked neighbour (or holds still when there is none next to it).
void climb_toward(S12Script& sc, StageCtx& ctx, PlayerInput& in, int target) {
    in.grab = true;
    const int band = ctx.combat.band;
    if (band < 0 || band == target) return;
    const ClimbBand& b = climb_band(ctx.enc().bands, band);
    const float stamina = ctx.combat.stamina;
    const bool shake = shaking(ctx);
    if (walked(ctx, band)) {
        if (shake) return;  // stand through it
    } else if (shake || stamina < kRestBelow) {
        // Hung: onto a walked neighbour (up first); with none next to it, hold still.
        if (hung_to_walked(ctx, band, true) == 0) in.forward = true;
        else if (hung_to_walked(ctx, band, false) == 0) in.back = true;
        else if (!shake) in.forward = hung_to_walked(ctx, band, true) <= hung_to_walked(ctx, band, false),
                         in.back = !in.forward;
        return;
    }
    // The next band this way, and the gate onto a hung one.
    int next;
    bool down = false;
    if (target == ka::B::Belly && band == ka::B::Shoulder) {
        next = ka::B::Belly;
    } else if (depth(target) > depth(band) && band != ka::B::Belly) {
        next = b.up;
    } else {
        next = b.down;
        down = true;
    }
    // Onto a hung band only just after one of his shakes (his cooldown — 10 s; 5 s on the
    // head and neck, knight_shake — is the time to cross it) and with the grip to climb it
    // (the belly: to strike it too).
    if (next >= 0 && !walked(ctx, next) && band != ka::B::Forearm && band != ka::B::UpperArm) {
        Vec3 a{}, e{};
        float need = kEnterHungAbove;
        if (next != ka::B::Belly && climb_band_segment(climb_band(ctx.enc().bands, next), joints(ctx), a, e)) {
            need = kGateMargin + ctx.tuning.staminaClimb * std::sqrt(vec3_dot(sub3(e, a), sub3(e, a))) /
                                     ctx.tuning.climbSpeed;
        }
        if (stamina < need || !(sc.sinceShake >= 0.0f && sc.sinceShake <= kFreshShake)) return;
    }
    if (next == ka::B::Belly) {
        in.forward = true;
        in.right = true;
    } else if (down) {
        in.back = true;
    } else {
        in.forward = true;
    }
}

// The strike routine on `target` (a sigil): along/around the band until a release would
// reach its shell, charge to full, release, then re-grip in the catch window (S11's).
void strike_routine(S12Script& sc, StageCtx& ctx, PlayerInput& in, int target) {
    sc.stepSeconds += kFixedFrameSeconds;
    switch (sc.step) {
        case 0: {
            in.grab = true;
            if (!attached(ctx)) return;
            const int home = target == ka::W::HeadSigil ? ka::B::Head : ka::B::Belly;
            if (ctx.combat.band != home || shaking(ctx)) {
                climb_toward(sc, ctx, in, home);  // back to its band; hold on through a shake
                sc.lastDistance = -1.0f;
                return;
            }
            // Strike only early in his cooldown after a shake (a swing and its catch inside
            // it), with grip for the charge.
            const float window = target == ka::W::HeadSigil ? kStrikeWindowHead : kStrikeWindowBelly;
            const float grip = target == ka::W::HeadSigil ? kStrikeAboveHead : kStrikeAboveBelly;
            if (!(sc.sinceShake >= 0.0f && sc.sinceShake <= window) || ctx.combat.stamina < grip) {
                sc.lastDistance = -1.0f;
                return;
            }
            // With a margin: the charge takes 0.9 s and his body sways under the climber.
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
            sc.strikeBand = ctx.combat.band;
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

}  // namespace

bool s12_start(S12Script& sc, StageCtx& ctx, double loadMs) {
    if (&ctx.enc() != &encounters::kKnight || !ctx.knight_wired ||
        ctx.entityRig >= ctx.playbacks.size() || !ctx.death_wired) {
        return false;
    }
    std::printf("%s-load stage=arena encounter=%s meshes=%zu ground=%dx%d layers=%d "
                "entity=%.2f,%.2f,%.2f yaw=%.2f spawn=%.2f,%.2f,%.2f return=%d graph=%s modes=%zu "
                "edges=%zu start=%s clip=%s ms=%.1f\n",
                sc.prefix, ctx.enc().id, ctx.scene.meshes.size(), ctx.heightmap.width,
                ctx.heightmap.height, ctx.heightmap.maxLayers, ctx.encounter.entityPosition.x,
                ctx.encounter.entityPosition.y, ctx.encounter.entityPosition.z,
                ctx.encounter.entityYawDeg, ctx.stage_spawn.x, ctx.stage_spawn.y, ctx.stage_spawn.z,
                ctx.return_wired ? 1 : 0, ctx.knight.core.graph.boss.c_str(),
                ctx.knight.core.graph.modes.size(), ctx.knight.core.graph.motionGraph.size(),
                knight_mode_name(ctx.knight, ctx.knight.core.rt.pending), clip_name(ctx), loadMs);
    sc.dodgeReach = slam_reach(ctx, sc.dodgeWidest);
    sc.dodgeOff = sc.dodgeReach + kPlayerCapsuleRadius + kDodgeMargin;
    return sc.dodgeReach > 0.0f;
}

PlayerInput s12_input(S12Script& sc, StageCtx& ctx) {
    PlayerInput in{};
    const float dt = kFixedFrameSeconds;
    sc.seconds += dt;
    sc.phaseSeconds += dt;
    ++sc.frames;
    sc.still.clear();
    const Vec3 me = ctx.player.position;
    const Vec3 him = ctx.encounter.entityPosition;
    switch (sc.phase) {
        case kApproach:
        case kExposure:  // the reset's fresh fight: at him, into his next slam
            walk_along(ctx, in, him.x - me.x, him.z - me.z, /*run=*/false);
            break;
        case kDodge: {
            // Across his facing line first (out of every slam row's reach), then along it to
            // beside where the blade's low end lands (kLowEndAhead), on the side nearer his
            // disc's centre.
            const float r = ctx.encounter.entityYawDeg * kDegToRad;
            const float off = (me.x - him.x) * std::cos(r) - (me.z - him.z) * std::sin(r);
            if (off * sc.dodgeSide < sc.dodgeOff) {
                walk_along(ctx, in, sc.dodgeSide * std::cos(r), -sc.dodgeSide * std::sin(r), /*run=*/true);
                break;
            }
            const Vec3 to{him.x + kLowEndAhead * std::sin(r) + sc.dodgeSide * (sc.dodgeOff + 0.1f) * std::cos(r), me.y,
                          him.z + kLowEndAhead * std::cos(r) - sc.dodgeSide * (sc.dodgeOff + 0.1f) * std::sin(r)};
            const float togo = std::hypot(to.x - me.x, to.z - me.z);
            if (togo > 0.3f) walk_along(ctx, in, to.x - me.x, to.z - me.z, /*run=*/true);
            break;
        }
        case kToBlade: {
            Vec3 low{};
            if (!blade_low_end(ctx, low)) break;
            const ClimbBandId reach = nearest_climb_band(ctx.enc().bands, joints(ctx), me, ctx.tuning.grabRange,
                                                         clip_name(ctx), entity_frame(ctx));
            if (reach == ka::B::Blade) {
                in.grab = !sc.grabPressed;  // a fresh press
                sc.grabPressed = !sc.grabPressed;
                break;
            }
            const float togo = std::hypot(low.x - me.x, low.z - me.z);
            if (togo > 0.2f) walk_along(ctx, in, low.x - me.x, low.z - me.z, /*run=*/true);
            break;
        }
        case kBlade:
            in.grab = true;
            in.forward = true;
            break;
        case kClimb:
            climb_toward(sc, ctx, in, ka::B::Head);
            break;
        case kToBelly:
            climb_toward(sc, ctx, in, ka::B::Belly);  // neck -> shoulder -> W + D down his front
            break;
        case kHeadSigil:
            strike_routine(sc, ctx, in, ka::W::HeadSigil);
            break;
        case kBellySigil:
            strike_routine(sc, ctx, in, ka::W::BellySigil);
            break;
        case kShake:
            climb_toward(sc, ctx, in, ka::B::Head);  // stand on his head through it
            break;
        default:
            break;
    }
    return in;
}

std::optional<int> s12_after(S12Script& sc, StageCtx& ctx) {
    const char* p = sc.prefix;
    KnightFight& kn = ctx.knight;
    PlayerCombat& combat = ctx.combat;
    Encounter& encounter = ctx.encounter;
    const DeathState& death = ctx.play.death;
    if (trace() && sc.frames % 15 == 0) {
        const Vec3 me = ctx.player.position, him = encounter.entityPosition;
        std::fprintf(stderr, "trace t=%.2f ph=%s mode=%s clip=%s t=%.2f cm=%d him=%.1f,%.1f,%.1f yaw=%.1f me=%.1f,%.1f,%.1f d=%.1f pm=%s band=%s bt=%.2f st=%.0f hp=%.0f ctrls=",
                     sc.seconds, kPhaseNames[sc.phase], knight_mode_name(kn, kn.core.rt.current), clip_name(ctx), kn.core.motion.time,
                     kn.core.motion.controlMode, him.x, him.y, him.z, encounter.entityYawDeg, me.x, me.y, me.z,
                     std::hypot(me.x - him.x, me.z - him.z), player_mode_name(combat.mode), band_name(ctx), combat.bandT,
                     combat.stamina, combat.health);
        for (int c : kn.think.step.controls) std::fprintf(stderr, "%d,", c);
        std::fprintf(stderr, " judge=");
        for (const std::string& j : kn.judgeFired) std::fprintf(stderr, "%s,", j.c_str());
        Vec3 low{};
        if (clip_is(ctx, "knight_a_atk1_3") && blade_low_end(ctx, low)) {
            Vec3 a{}, e{};
            climb_band_segment(climb_band(ctx.enc().bands, ka::B::Blade), joints(ctx), a, e);
            std::fprintf(stderr, " low=%.1f,%.1f,%.1f a=%.1f,%.1f,%.1f e=%.1f,%.1f,%.1f cap=%.2f fr=%d", low.x, low.y, low.z,
                         a.x, a.y, a.z, e.x, e.y, e.z, player_capsule_distance(me, low), entity_frame(ctx));
        }
        std::fprintf(stderr, "\n");
    }
    if (sc.phase != kReset && sc.phaseSeconds > kBudget[sc.phase]) {
        char where[200];
        std::snprintf(where, sizeof(where), "phase %s over its %.0f s budget (mode %s, clip %s, player %s, band %s, %.1f,%.1f,%.1f)",
                      kPhaseNames[sc.phase], kBudget[sc.phase], knight_mode_name(kn, kn.core.rt.current),
                      clip_name(ctx), player_mode_name(combat.mode), band_name(ctx), ctx.player.position.x,
                      ctx.player.position.y, ctx.player.position.z);
        return fail(sc, where);
    }
    if (combat.mode == PlayerMode::Dead) {
        return fail(sc, std::string("the player died in phase ") + kPhaseNames[sc.phase]);
    }
    // The graph's mode transitions, named.
    if (kn.core.rt.current != sc.lastMode) {
        std::printf("%s-mode from=%s to=%s clip=%s at=%.1f\n", p, knight_mode_name(kn, sc.lastMode),
                    knight_mode_name(kn, kn.core.rt.current), clip_name(ctx), sc.seconds);
        sc.lastMode = kn.core.rt.current;
    }
    // The first judge volume that held the player: which, and where he stood (the
    // forward-sector check: bearing + = his left, |bearing| < 90 = in front).
    if (!sc.judged && !kn.judgeFired.empty()) {
        const Vec3 me = ctx.player.position, him = encounter.entityPosition;
        const float bearing = std::remainder(std::atan2(me.x - him.x, me.z - him.z) / kDegToRad - encounter.entityYawDeg, 360.0f);
        std::printf("%s-judge volume=%s bearing=%.1f dist=%.1f sector=%s at=%.1f\n", p, kn.judgeFired.front().c_str(),
                    bearing, std::hypot(me.x - him.x, me.z - him.z), std::fabs(bearing) < 90.0f ? "forward" : "back",
                    sc.seconds);
        sc.judged = true;
    }
    // The slam's own rows (the blade box) during the dodge: they must miss.
    // Rows that overlapped the player count, landed or not (an earlier hit's i-frames would
    // refuse the blade's damage, not move the blade).
    for (const DamageHit& h : kn.overlaps) {
        if (sc.phase == kDodge && slam_row(h.name)) {
            if (sc.swordRows++ == 0) {
                // Where it overlapped: the row, his clip and frame, the player off his facing line.
                const float r = encounter.entityYawDeg * kDegToRad;
                const Vec3 me = ctx.player.position, him = encounter.entityPosition;
                char at[200];
                std::snprintf(at, sizeof(at), " first=%s clip=%s frame=%d off_line=%.2f ahead=%.2f", h.name.c_str(),
                              clip_name(ctx), entity_frame(ctx),
                              (me.x - him.x) * std::cos(r) - (me.z - him.z) * std::sin(r),
                              (me.x - him.x) * std::sin(r) + (me.z - him.z) * std::cos(r));
                sc.swordFirst = at;
            }
        }
    }
    for (const DamageHit& h : ctx.knightHits) {
        if (h.reaction == ka::kThrowOffReaction) ++sc.throwOffs;
    }
    // His damage lands on the player only in the exposure (ruling R-L): once, named there.
    for (const DamageHit& h : ctx.knightHits) {
        if (!(h.amount > 0.0f)) continue;
        if (sc.phase != kExposure) {
            return fail(sc, "a damage row landed outside the exposure: " + h.name + " in phase " +
                                kPhaseNames[sc.phase]);
        }
        std::printf("%s-hit row=%s amount=%.0f clip=%s player_hp=%.0f mode=%s at=%.1f\n", p, h.name.c_str(),
                    h.amount, clip_name(ctx), combat.health, player_mode_name(combat.mode), sc.seconds);
        return 0;
    }
    // The climb's route, once per band.
    const int band = attached(ctx) ? static_cast<int>(combat.band) : -1;
    if (band != sc.lastBand && band >= 0) {
        std::printf("%s-climb band=%s t=%.2f stamina=%.1f clip=%s at=%.1f\n", p, band_name(ctx), combat.bandT,
                    combat.stamina, clip_name(ctx), sc.seconds);
        if (sc.route.empty() || sc.route.back() != band_name(ctx)) sc.route.emplace_back(band_name(ctx));
    }
    sc.lastBand = band;
    if (ctx.knightHit.weakpoint >= 0) {
        std::printf("%s-strike weakpoint=%s band=%s damage=%.0f spent=%d health=%.0f killed=%d at=%.1f\n", p,
                    weakpoint_material_name(ctx.enc().weakpoints, ctx.knightHit.weakpoint),
                    climb_band_name(ctx.enc().bands, sc.strikeBand), ctx.knightHit.damage, ctx.knightHit.spent ? 1 : 0,
                    encounter.health, ctx.knightHit.killed ? 1 : 0, sc.seconds);
    }
    // His shakes: their end (the belly's timing) and, attached through one, the survival.
    const bool shake = shaking(ctx);
    if (shake) {
        if (!sc.shookLast) {
            sc.shakeClip = clip_name(ctx);
            sc.shakeLow = combat.stamina;
            sc.sawShake = attached(ctx);
        }
        sc.shakeLow = std::min(sc.shakeLow, combat.stamina);
        sc.sawShake = sc.sawShake && attached(ctx);
        sc.sinceShake = -1.0f;
    } else if (sc.shookLast) {
        sc.sinceShake = 0.0f;
    } else if (sc.sinceShake >= 0.0f) {
        sc.sinceShake += kFixedFrameSeconds;
    }
    const bool shakeEnded = sc.shookLast && !shake;
    sc.shookLast = shake;
    if (ctx.knightBackstop) {
        std::printf("%s-backstop at=%.1f\n", p, sc.seconds);
    }

    switch (sc.phase) {
        case kApproach:
            if (clip_is(ctx, "knight_a_atk1_start") || clip_is(ctx, "knight_a_atk2_start")) {
                ++sc.slams;
                const Vec3 me = ctx.player.position, him = encounter.entityPosition;
                std::printf("%s-slam n=%d choice=%s rays=%d mode=%s dist=%.1f at=%.1f\n", p, sc.slams,
                            clip_is(ctx, "knight_a_atk1_start") ? "slam" : "stab", rays_clear(ctx) ? 1 : 0,
                            knight_mode_name(kn, kn.core.rt.current), std::hypot(me.x - him.x, me.z - him.z), sc.seconds);
                // Across his facing, toward his disc's centre.
                const float r = encounter.entityYawDeg * kDegToRad;
                const Vec3 c = kn.core.boundCentre;
                sc.dodgeSide = (c.x - me.x) * std::cos(r) - (c.z - me.z) * std::sin(r) >= 0.0f ? 1.0f : -1.0f;
                next(sc, kDodge);
            }
            break;
        case kDodge:
            if (clip_is(ctx, "knight_a_atk1_3") ||
                (clip_is(ctx, "knight_a_atk1_2") && entity_frame(ctx) > kSlamBoxLastFrame)) {
                {
                    const float r = encounter.entityYawDeg * kDegToRad;
                    const Vec3 me = ctx.player.position, him = encounter.entityPosition;
                    std::printf("%s-dodge sword_rows=%d%s off_line=%.2f need=%.2f widest=%s reach=%.2f player_hp=%.0f "
                                "mode=%s at=%.1f\n",
                                p, sc.swordRows, sc.swordFirst.c_str(),
                                std::fabs((me.x - him.x) * std::cos(r) - (me.z - him.z) * std::sin(r)), sc.dodgeOff,
                                sc.dodgeWidest.c_str(), sc.dodgeReach, combat.health, player_mode_name(combat.mode),
                                sc.seconds);
                }
                if (sc.swordRows != 0) return fail(sc, "the slam's blade landed on the player");
                next(sc, kToBlade);
            }
            break;
        case kToBlade:
            if (attached(ctx) && combat.band == ka::B::Blade) {
                sc.grabFrame = entity_frame(ctx);
                sc.still = "blade";
                next(sc, kBlade);
            } else if (!clip_is(ctx, "knight_a_atk1_2") && !clip_is(ctx, "knight_a_atk1_3")) {
                next(sc, kApproach);  // the window passed: the next slam
            }
            break;
        case kBlade: {
            const ClimbBand& blade = climb_band(ctx.enc().bands, ka::B::Blade);
            if (sc.closeFrame < 0 && (!clip_is(ctx, blade.enableClip) || entity_frame(ctx) >= blade.enableTo)) {
                // The bandT the close decided on (a fall at the close has already left the band:
                // its last bandT on it).
                sc.closeFrame = entity_frame(ctx);
                sc.closeT = combat.band == ka::B::Blade ? combat.bandT : sc.lastBladeT;
            }
            if (combat.band == ka::B::Blade) sc.lastBladeT = combat.bandT;
            const bool off = !attached(ctx);
            if (combat.band == ka::B::Forearm || off) {
                std::printf("%s-blade window=%d..%d grab_frame=%d close_frame=%d close_t=%.2f ride_from=%.2f margin=%.2f "
                            "carried=%d band=%s slams=%d at=%.1f\n",
                            p, blade.enableFrom, blade.enableTo, sc.grabFrame, sc.closeFrame, sc.closeT,
                            1.0f - blade.carryFraction, sc.closeT - (1.0f - blade.carryFraction),
                            !off && combat.band == ka::B::Forearm ? 1 : 0, off ? "none" : band_name(ctx), sc.slams,
                            sc.seconds);
                if (off) return fail(sc, "the blade dropped the climber");
                next(sc, kClimb);
            }
            break;
        }
        case kClimb:
            if (!attached(ctx)) return fail(sc, "the climber fell off the arm");
            if (combat.band == ka::B::Head) {
                sc.still = "head";
                std::string route;
                for (const std::string& r : sc.route) route += (route.empty() ? "" : ",") + r;
                std::printf("%s-route bands=%s stamina=%.1f at=%.1f\n", p, route.c_str(), combat.stamina, sc.seconds);
                next(sc, kHeadSigil);
            }
            break;
        case kHeadSigil:
        case kBellySigil: {
            const int target = sc.phase == kHeadSigil ? ka::W::HeadSigil : ka::W::BellySigil;
            const std::size_t w = static_cast<std::size_t>(target);
            if (encounter.defeated) {
                std::printf("%s-sigil done=belly strikes=%d spent=%.0f health=%.0f at=%.1f\n", p, sc.strikes,
                            kn.spent[w], encounter.health, sc.seconds);
                sc.diedAt = encounter.entityPosition;
                next(sc, kDeath);
            } else if (kn.spent[w] >= ka::kWeakpointPool[w] && sc.step == 0 && attached(ctx)) {
                std::printf("%s-sigil done=head strikes=%d spent=%.0f health=%.0f at=%.1f\n", p, sc.strikes,
                            kn.spent[w], encounter.health, sc.seconds);
                sc.strikes = 0;
                next(sc, kShake);
            } else if (!attached(ctx) && (combat.mode == PlayerMode::Grounded || combat.mode == PlayerMode::Staggered)) {
                return fail(sc, std::string("the catch missed on the ") + (target == ka::W::HeadSigil ? "head" : "belly"));
            }
            break;
        }
        case kShake:
            if (!attached(ctx)) return fail(sc, "the shake threw the climber");
            if (shakeEnded && sc.sawShake) {
                std::printf("%s-shake survived=1 clip=%s band=%s low=%.1f stamina=%.1f at=%.1f\n", p,
                            sc.shakeClip.c_str(), band_name(ctx), sc.shakeLow, combat.stamina, sc.seconds);
                next(sc, kToBelly);
            }
            break;
        case kToBelly:
            if (!attached(ctx)) return fail(sc, "the climber fell on the way to the belly");
            if (combat.band == ka::B::Belly) {
                sc.still = "belly";
                next(sc, kBellySigil);
            }
            break;
        case kDeath: {
            const int phase = static_cast<int>(death.phase);
            if (phase != sc.lastDeathPhase) {
                std::printf("%s-death phase=%s total=%.3f mode=%s graph_clip=%s clip=%s\n", p,
                            death_phase_name(death.phase), death.total, knight_mode_name(kn, kn.deathMode),
                            knight_clip_name(kn.core, kn.deathGraphClip), clip_name(ctx));
                sc.lastDeathPhase = phase;
            }
            if (death.total >= kDeathStillAt && death.total - kFixedFrameSeconds < kDeathStillAt) {
                sc.still = "death";
            }
            if (kn.deathMode != kn.core.graph.root.deathMode) {
                return fail(sc, "the graph did not enter its root death mode");
            }
            if (death.phase == DeathPhase::Done) {
                const Playback& entity = ctx.playbacks[ctx.entityRig];
                const RigCpu& rig = ctx.scene.rigs[ctx.entityRig];
                const bool live = mesh_range_visible(ctx.scene, rig.meshBegin, rig.meshEnd);
                const float moved = std::hypot(encounter.entityPosition.x - sc.diedAt.x,
                                               encounter.entityPosition.z - sc.diedAt.z);
                std::printf("%s-corpse live_body=%d clip=%s t=%.3f held=%d moved=%.2f player=%s throw_offs=%d "
                            "return=%s\n", p,
                            live ? 1 : 0, entity.clips[static_cast<std::size_t>(entity.index)].name.c_str(),
                            entity.time, entity.pinned ? 1 : 0, moved, player_mode_name(combat.mode), sc.throwOffs,
                            return_phase_name(ctx.carry.ret.phase));
                if (!live || ctx.carry.ret.phase != ReturnPhase::Idle) {
                    return fail(sc, "the corpse did not stay (or the return ran)");
                }
                std::printf("%s-bound max=%.2f unclamped=%.2f clamped_frames=%d radius=%.1f centre=%.0f,%.0f\n", p,
                            kn.maxBoundDistance, kn.maxUnclampedDistance, kn.clampedFrames, kn.core.boundRadius,
                            kn.core.boundCentre.x, kn.core.boundCentre.z);
                if (kn.maxBoundDistance > kn.core.boundRadius + 1e-3f) {
                    return fail(sc, "his root left his 36 m disc");
                }
                sc.wantR = true;
                next(sc, kReset);
            }
            break;
        }
        case kReset: {
            if (sc.wantR) break;  // the caller presses R, then calls again
            const bool home = std::hypot(encounter.entityPosition.x - kn.basePosition.x,
                                         encounter.entityPosition.z - kn.basePosition.z) < 1e-3f &&
                              std::fabs(encounter.entityYawDeg - kn.baseYawDeg) < 1e-3f;
            std::printf("%s-reset health=%.0f defeated=%d mode=%s clip=%s at_base=%d player_hp=%.0f pmode=%s death=%s\n",
                        p, encounter.health, encounter.defeated ? 1 : 0, knight_mode_name(kn, kn.core.rt.pending),
                        clip_name(ctx), home ? 1 : 0, combat.health, player_mode_name(combat.mode),
                        death_phase_name(death.phase));
            if (encounter.health != encounter.desc->maxHealth || encounter.defeated || !home ||
                death.phase != DeathPhase::Idle || kn.core.rt.pending != kn.core.graph.root.retryStartMode) {
                return fail(sc, "R did not bring Gaius back to his opening state");
            }
            next(sc, kExposure);
            break;
        }
        default:
            break;
    }
    return std::nullopt;
}

// --s12-selftest: the S12 timeline in Gaius's arena (one stage).
SelftestResult run_s12(StageCtx& ctx, RunState& run) {
    const AppOptions& opts = run.opts;
    S12Script sc;
    sc.prefix = "s12";
    if (!s12_start(sc, ctx, ctx.loadMs)) {
        return fail(sc, "the --manifest arena is not Gaius's (encounter knight, his bake and graph) "
                        "or his fight is unwired");
    }
    ctx.play.audioOn = run.audio.enabled;
    ctx.play.palettes = update_all_poses(run.renderer, ctx.scene, ctx.playbacks, 0.0f, ctx.club);
    FrameResult last;
    const auto render = [&]() {
        run.renderer.set_hud(ctx.hud_state(/*visible=*/true));
        run.renderer.render_frame(ctx.scene, camera_view_proj(ctx.camera, run.shell.win),
                                  /*acquire_swapchain=*/true,
                                  last.effect.active ? &last.effect : nullptr, nullptr);
    };
    const auto step = [&]() -> std::optional<int> {
        FrameInput in;
        in.dt = kFixedFrameSeconds;
        in.input = s12_input(sc, ctx);
        last = frame_step(ctx, in);
        if (last.exitCode) {
            return last.exitCode;
        }
        std::optional<int> code = s12_after(sc, ctx);
        if (!code && sc.wantR) {
            ctx.press_reset(ctx.play.death, ctx.play.deathCueFired, &ctx.play.deathCameraSet);
            sc.wantR = false;
            code = s12_after(sc, ctx);
        }
        if (!sc.still.empty() && !opts.s12StillsPath.empty()) {
            with_clocks_restored(ctx.playbacks, [&]() {
                render();
                write_screenshot(run.renderer, run.shell.win,
                                 still_png_path(opts.s12StillsPath, sc.still.c_str()));
            });
        }
        return code;
    };
    for (;;) {
        SDL_PumpEvents();
        const std::optional<int> code = step();
        if (code) {
            if (*code != 0) {
                return *code;
            }
            break;
        }
    }
    render();  // warm-up
    const Uint64 perfStart = SDL_GetTicksNS();
    for (int i = 0; i < kS12PerfFrames; ++i) {
        FrameInput in;
        in.dt = kFixedFrameSeconds;
        last = frame_step(ctx, in);
        render();
    }
    const double perfMs = static_cast<double>(SDL_GetTicksNS() - perfStart) / 1e6;
    std::printf("s12-perf frames=%d frame_ms=%.3f drawn=%d culled=%d\n", kS12PerfFrames,
                perfMs / static_cast<double>(kS12PerfFrames), run.renderer.drawn_last_frame(),
                run.renderer.culled_last_frame());
    std::printf("s12-status phases_done=%d frames=%d seconds=%.1f health=%.0f\n", sc.phasesDone,
                sc.frames, sc.seconds, ctx.encounter.health);
    print_status_line(opts, run.shell.win);
    return 0;
}
