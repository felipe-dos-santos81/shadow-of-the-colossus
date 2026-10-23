#include "selftests.h"

#include "capture.h"
#include "encounter_rt.h"
#include "s11_script.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace q = encounters::quadratus_detail;

namespace {

// --s11-selftest / --s11-e2e (Task 10, authored): the script's phases, their
// budgets (fixed-step seconds) and its tolerances.
enum Phase {
    kApproach,   // walk at him from the spawn until he starts a stomp
    kDodge,      // run across his front, away from the stomping hoof, past the impact
    kHoof,       // stab a rear hoof in its lift window
    kGrab,       // grab his fur from the ground
    kClimb,      // climb to the rump
    kBackSigil,  // strike the back sigil from the rump's rear face
    kBack,       // up onto the back, surface-walk it
    kShake,      // hold on through a shake
    kToHead,     // walk the back on, climb the neck to the head
    kHeadSigil,  // strike the head sigil to his death
    kDeath,      // watch the death to Done
    kReset,      // R: his opening state
    kPhaseCount,
};
constexpr const char* kPhaseNames[kPhaseCount] = {
    "approach", "dodge", "hoof", "grab", "climb", "back_sigil", "back", "shake", "to_head",
    "head_sigil", "death", "reset"};
constexpr float kBudget[kPhaseCount] = {60.0f, 20.0f, 150.0f, 40.0f, 60.0f, 150.0f,
                                        40.0f, 60.0f, 60.0f,  150.0f, 20.0f, 1.0f};
// The way round his body to a band (authored, from the probe: his legs'
// capsules reach ~8 m from his origin in a rear kneel): circle at kRoundOut,
// kRoundStep degrees ahead of the player, until the band's bearing is near.
constexpr float kRoundOut = 9.0f;
constexpr float kRoundStep = 25.0f;
constexpr float kGrabOutside = 0.5f;  // m off the band's surface the walk aims at
constexpr float kRestBelow = 30.0f;   // stamina: go and stand on his back below this ...
constexpr float kRestUntil = 95.0f;   // ... until this
constexpr float kBackStopT = 0.45f;     // where he stands for the shake (the back's middle)
constexpr float kDeathStillAt = 4.5f;   // s into the death: mid-fall, on the low key
constexpr float kResetTolerance = 1e-3f;
constexpr int kS11PerfFrames = 240;

void next(S11Script& sc, int phase) {
    sc.phase = phase;
    sc.phaseSeconds = 0.0f;
    sc.step = 0;
    sc.stepSeconds = 0.0f;
    sc.lastDistance = -1.0f;
    sc.axis = 1;  // the descent tries round the band first (a sigil faces one side)
    sc.flips = 0;
}

// The scripted look along a walk (the S8/S9 e2e scripts' walk_to): the player
// faces the heading, the follow camera sits behind it, W walks it.
void walk_along(StageCtx& ctx, PlayerInput& in, float hx, float hz, bool run) {
    const float h = std::atan2(hx, hz) / kDegToRad;
    ctx.player.yawDeg = h;
    ctx.follow.yawOffsetDeg = 0.0f;
    ctx.follow.pitchOffsetDeg = 0.0f;
    ctx.camera.yaw = std::remainder(h + 180.0f, 360.0f);
    in.forward = true;
    in.run = run;
}

// A walk toward `to` that sidesteps when it stalls (a leg's capsule in the way):
// after 0.5 s with under 0.1 m of progress it walks 90 deg off the heading for
// 0.75 s, alternating sides.
void walk_toward(S11Script& sc, StageCtx& ctx, PlayerInput& in, Vec3 to) {
    const Vec3 me = ctx.player.position;
    sc.progressSeconds += kFixedFrameSeconds;
    if (std::hypot(me.x - sc.progressFrom.x, me.z - sc.progressFrom.z) > 0.1f) {
        sc.progressFrom = me;
        sc.progressSeconds = 0.0f;
    }
    if (sc.progressSeconds > 0.5f && sc.detourSeconds <= 0.0f) {
        sc.detourSeconds = 0.75f;
        sc.detourSide = -sc.detourSide;
        sc.progressSeconds = 0.0f;
    }
    // Round his body: off the target's bearing, circle him at kRoundOut toward
    // it (his legs' capsules hold a straight walk); near it, walk straight in.
    const Vec3 him = ctx.encounter.entityPosition;
    const float yaw = ctx.encounter.entityYawDeg;
    const Vec3 pl = to_body_frame(me, him, yaw);
    const Vec3 tl = to_body_frame(to, him, yaw);
    const float ap = std::atan2(pl.x, pl.z);
    const float da = std::remainder(std::atan2(tl.x, tl.z) - ap, 2.0f * kPi);
    if (std::fabs(da) > kRoundStep * kDegToRad) {
        const float a = ap + (da > 0.0f ? 1.0f : -1.0f) * kRoundStep * kDegToRad;
        to = from_body_frame(Vec3{kRoundOut * std::sin(a), 0.0f, kRoundOut * std::cos(a)}, him, yaw);
    }
    float hx = to.x - me.x;
    float hz = to.z - me.z;
    if (sc.detourSeconds > 0.0f) {
        sc.detourSeconds -= kFixedFrameSeconds;
        const float ox = hx;
        hx = sc.detourSide * hz;
        hz = -sc.detourSide * ox;
    }
    walk_along(ctx, in, hx, hz, /*run=*/false);
}

const std::vector<Mat4>& entity_palette(StageCtx& ctx) {
    return ctx.play.palettes[ctx.entityRig];
}

bool shell(StageCtx& ctx, int weakpoint, Vec3& centre, float& radius) {
    const char* material = weakpoint_material_name(ctx.enc().weakpoints, weakpoint);
    return material != nullptr &&
           posed_weakpoint_proxy(ctx.scene, ctx.entityRig, entity_palette(ctx), material, centre,
                                 radius);
}

WeakpointProxyLookup proxy(StageCtx& ctx) {
    return [&ctx](int weakpoint, Vec3& centre, float& radius) {
        return shell(ctx, weakpoint, centre, radius);
    };
}

JointLookup joints(StageCtx& ctx) {
    return [&ctx](std::string_view name, Vec3& out) {
        return posed_joint_world_position(ctx.scene, ctx.entityRig, entity_palette(ctx),
                                          std::string(name), out);
    };
}

bool attached(const StageCtx& ctx) { return mode_is_attached(ctx.combat.mode); }

const char* band_name(const StageCtx& ctx) {
    return climb_band_name(ctx.enc().bands, ctx.combat.band);
}

// The strike routine on `target` (a sigil): orbit/climb until a release would
// reach its shell, charge to full, release, then re-grip in the catch window.
void strike_routine(S11Script& sc, StageCtx& ctx, PlayerInput& in, int target) {
    const float dt = kFixedFrameSeconds;
    sc.stepSeconds += dt;
    switch (sc.step) {
        case 0: {  // align
            in.grab = true;
            if (!attached(ctx)) {
                return;
            }
            // Low on grip: up onto his back (standing regains it), then down again.
            const bool onBack = climb_band(ctx.enc().bands, ctx.combat.band).horizontal;
            const bool shaking = ctx.quad.rt.state == q::kShakeRule.state;
            if (sc.resting || ctx.combat.stamina < kRestBelow || (shaking && !onBack)) {
                sc.resting = !(onBack && ctx.combat.stamina >= kRestUntil && !shaking);
                if (sc.resting) {
                    // Along the chain to the back (up from the rump, down from the
                    // neck); there, stand.
                    if (!onBack) {
                        int up = ctx.combat.band;
                        for (int guard = 0; guard < 16 && up >= 0 && up != q::B::Back; ++guard) {
                            up = climb_band(ctx.enc().bands, up).up;
                        }
                        in.forward = up == q::B::Back;
                        in.back = up != q::B::Back;
                    }
                    sc.lastDistance = -1.0f;
                    return;
                }
            }
            if (quadratus_open_sigil(ctx.quad, ctx.encounter) != target) {
                return;  // spent: hold on (the phase moves on)
            }
            if (target == q::W::HeadSigil && ctx.combat.band != q::B::Head) {
                in.forward = true;  // caught short of the head (the neck): on up
                sc.lastDistance = -1.0f;
                return;
            }
            if (strike_reaches_shell(ctx.player.position, target, proxy(ctx))) {
                sc.step = 1;
                sc.stepSeconds = 0.0f;
                in.strike = true;
                return;
            }
            Vec3 centre{};
            float radius = 0.0f;
            if (!shell(ctx, target, centre, radius)) {
                return;
            }
            // Coordinate descent on the capsule's distance to the shell: along the
            // band (W/S), then around or across it (A/D); a step that moves away
            // flips that axis's sign, a second flip in a row moves to the other.
            const float d = player_capsule_distance(ctx.player.position, centre);
            if (sc.lastDistance >= 0.0f && d > sc.lastDistance + 1e-4f) {
                if (sc.axis == 0) {
                    sc.along = -sc.along;
                } else {
                    sc.lateral = -sc.lateral;
                }
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
        case 1:  // charge to full (chargeSeconds)
            in.grab = true;
            in.strike = true;
            if (sc.stepSeconds + 1e-4f >= ctx.tuning.chargeSeconds) {
                sc.step = 2;
                sc.stepSeconds = 0.0f;
            }
            return;
        case 2:  // release: the swing lands (or whiffs) and shakes the climber off
            in.grab = true;
            ++sc.strikes;
            sc.strikeBand = ctx.combat.band;
            sc.step = 3;
            return;
        case 3:  // let go of E for one frame ...
            sc.step = 4;
            return;
        case 4:  // ... and grip again inside the catch window
            in.grab = true;
            sc.step = 0;
            sc.lastDistance = -1.0f;
            return;
        default:
            return;
    }
}

int fail(const S11Script& sc, const std::string& message) {
    std::fprintf(stderr, "sotc-viewer: --%s: %s\n",
                 std::string(sc.prefix) == "s11" ? "s11-selftest" : "s11-e2e", message.c_str());
    return 1;
}

}  // namespace

bool s11_start(S11Script& sc, StageCtx& ctx, double loadMs) {
    if (&ctx.enc() != &encounters::kQuadratus || !ctx.quad_wired ||
        ctx.entityRig >= ctx.playbacks.size() || !ctx.death_wired) {
        return false;
    }
    std::printf("%s-load stage=arena encounter=%s meshes=%zu ground=%dx%d layers=%d "
                "entity=%.2f,%.2f,%.2f yaw=%.2f spawn=%.2f,%.2f,%.2f return=%d ms=%.1f\n",
                sc.prefix, ctx.enc().id, ctx.scene.meshes.size(), ctx.heightmap.width,
                ctx.heightmap.height, ctx.heightmap.maxLayers, ctx.encounter.entityPosition.x,
                ctx.encounter.entityPosition.y, ctx.encounter.entityPosition.z,
                ctx.encounter.entityYawDeg, ctx.stage_spawn.x, ctx.stage_spawn.y,
                ctx.stage_spawn.z, ctx.return_wired ? 1 : 0, loadMs);
    return true;
}

PlayerInput s11_input(S11Script& sc, StageCtx& ctx) {
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
            walk_along(ctx, in, him.x - me.x, him.z - me.z, /*run=*/true);
            break;
        case kDodge: {
            // Away from the stomping hoof: his right for the left stomp.
            const float r = ctx.encounter.entityYawDeg * kDegToRad;
            const float side = sc.chain == 0 ? -1.0f : 1.0f;  // his left is (cos, -sin)
            walk_along(ctx, in, side * std::cos(r), -side * std::sin(r), /*run=*/true);
            break;
        }
        case kHoof: {
            // The rear hoof nearer the player; the stab when it is in its window.
            int row = 0;
            float best = 1e9f;
            Vec3 aim{};
            for (int k = 0; k < 2; ++k) {
                Vec3 c{};
                float rad = 0.0f;
                if (shell(ctx, q::kHooves[static_cast<std::size_t>(k)].weakpoint, c, rad)) {
                    const float d = std::hypot(c.x - me.x, c.z - me.z);
                    if (d < best) {
                        best = d;
                        row = k;
                        aim = c;
                    }
                }
            }
            sc.targetHoof = row;
            const bool stabbing = quadratus_stabbing(ctx.quad, ctx.combat);
            bool window = false;
            for (std::size_t k = 0; k < 2; ++k) {  // the rear hooves, rows 0/1
                window = window || quadratus_hoof_window(ctx.quad, ctx.encounter, ctx.scene,
                                                         ctx.entityRig, entity_palette(ctx),
                                                         ctx.heightmap, ctx.player, ctx.combat,
                                                         proxy(ctx), k);
            }
            if (window && !stabbing && !sc.stabPressed) {
                in.strike = true;
                sc.stabPressed = true;
            } else {
                sc.stabPressed = false;
            }
            const float togo = std::hypot(aim.x - me.x, aim.z - me.z);
            if (!stabbing && !in.strike && togo > 0.3f) {
                walk_along(ctx, in, aim.x - me.x, aim.z - me.z, /*run=*/togo > 6.0f);
            }
            break;
        }
        case kGrab: {
            if (attached(ctx)) {
                in.grab = true;
                break;
            }
            if (ctx.combat.mode != PlayerMode::Grounded) {
                break;  // landing, staggered
            }
            const ClimbBandId band = nearest_climb_band(ctx.enc().bands, joints(ctx), me,
                                                        ctx.tuning.grabRange);
            if (band != ClimbBandId::None) {
                in.grab = sc.step == 1;  // a fresh press: released, then pressed
                sc.step = sc.step == 1 ? 0 : 1;
                break;
            }
            // Toward the fur: the stabbed hoof's band (the fur from the ground up),
            // a step outside its surface, circling round his legs to it.
            // The rear hoof band nearer the player.
            int hoofBand = q::B::RearLeftHoof;
            float nearest = std::numeric_limits<float>::infinity();
            for (const int candidate : {q::B::RearLeftHoof, q::B::RearRightHoof}) {
                Vec3 ca{};
                Vec3 ce{};
                if (climb_band_segment(climb_band(ctx.enc().bands, candidate), joints(ctx), ca, ce)) {
                    const float d = std::hypot(0.5f * (ca.x + ce.x) - me.x, 0.5f * (ca.z + ce.z) - me.z);
                    if (d < nearest) {
                        nearest = d;
                        hoofBand = candidate;
                    }
                }
            }
            const ClimbBand& b = climb_band(ctx.enc().bands, hoofBand);
            Vec3 a{};
            Vec3 e{};
            if (climb_band_segment(b, joints(ctx), a, e)) {
                const Vec3 him = ctx.encounter.entityPosition;
                Vec3 to{0.5f * (a.x + e.x), me.y, 0.5f * (a.z + e.z)};
                const float ox = to.x - him.x;
                const float oz = to.z - him.z;
                const float on = std::hypot(ox, oz);
                if (on > 1e-3f) {
                    const float out = b.radius + kPlayerCapsuleRadius + kGrabOutside;
                    to.x += ox / on * out;
                    to.z += oz / on * out;
                }
                walk_toward(sc, ctx, in, to);
            }
            break;
        }
        case kClimb:
        case kBack:
        case kToHead:
            in.grab = true;
            in.forward = true;
            break;
        case kShake:
            in.grab = true;
            break;
        case kBackSigil:
            strike_routine(sc, ctx, in, q::W::BackSigil);
            break;
        case kHeadSigil:
            strike_routine(sc, ctx, in, q::W::HeadSigil);
            break;
        default:
            break;
    }
    return in;
}

std::optional<int> s11_after(S11Script& sc, StageCtx& ctx) {
    const char* p = sc.prefix;
    PlayerCombat& combat = ctx.combat;
    Encounter& encounter = ctx.encounter;
    QuadratusFight& quad = ctx.quad;
    const DeathState& death = ctx.play.death;
    if (sc.phase < kReset && sc.phaseSeconds > kBudget[sc.phase]) {
        char where[160];
        std::snprintf(where, sizeof(where),
                      "phase %s over its %.0f s budget (state %s, mode %s, band %s, player "
                      "%.1f,%.1f,%.1f)",
                      kPhaseNames[sc.phase], kBudget[sc.phase], quad_state_name(quad.rt.state),
                      player_mode_name(combat.mode), band_name(ctx), ctx.player.position.x,
                      ctx.player.position.y, ctx.player.position.z);
        return fail(sc, where);
    }
    if (combat.mode == PlayerMode::Dead) {
        return fail(sc, std::string("the player died in phase ") + kPhaseNames[sc.phase]);
    }
    // The climb's route, once per band.
    const int band = attached(ctx) ? static_cast<int>(combat.band) : -1;
    if (band != sc.lastBand && band >= 0) {
        std::printf("%s-climb band=%s t=%.2f stamina=%.1f state=%s at=%.1f\n", p, band_name(ctx),
                    combat.bandT, combat.stamina, quad_state_name(quad.rt.state), sc.seconds);
    }
    sc.lastBand = band;
    // Every strike that landed on a weakpoint, as his gates routed it.
    if (ctx.quadHit.weakpoint >= 0) {
        std::printf("%s-strike weakpoint=%s band=%s gate=%s damage=%.0f health=%.0f killed=%d "
                    "at=%.1f\n",
                    p, weakpoint_material_name(ctx.enc().weakpoints, ctx.quadHit.weakpoint),
                    climb_band_name(ctx.enc().bands, sc.strikeBand),
                    ctx.quadHit.sealed                         ? "sealed"
                    : ctx.quadHit.kind == GateKind::Damage     ? "damage"
                    : ctx.quadHit.kind == GateKind::Transition ? "transition"
                                                               : "ignore",
                    ctx.quadHit.damage, encounter.health, ctx.quadHit.killed ? 1 : 0, sc.seconds);
    }

    switch (sc.phase) {
        case kApproach:
            if (ctx.quadFrame.decision == QuadDecision::AttackL ||
                ctx.quadFrame.decision == QuadDecision::AttackR) {
                sc.chain = ctx.quadFrame.decision == QuadDecision::AttackL ? 0 : 1;
                const float dx = ctx.player.position.x - encounter.entityPosition.x;
                const float dz = ctx.player.position.z - encounter.entityPosition.z;
                std::printf("%s-attack chain=%s state=%s dist=%.1f bearing=%.1f at=%.1f\n", p,
                            quad_decision_name(ctx.quadFrame.decision),
                            quad_state_name(quad.rt.state), std::hypot(dx, dz),
                            std::remainder(std::atan2(dx, dz) / kDegToRad -
                                               encounter.entityYawDeg,
                                           360.0f),
                            sc.seconds);
                next(sc, kDodge);
            }
            break;
        case kDodge:
            if (ctx.quadFrame.impactDue) {
                Vec3 foot{};
                float dist = -1.0f;
                if (posed_joint_world_position(
                        ctx.scene, ctx.entityRig, entity_palette(ctx),
                        q::kAttacks[static_cast<std::size_t>(sc.chain)].damageJoint, foot)) {
                    dist = player_capsule_distance(ctx.player.position, foot);
                }
                const bool dodged = combat.health >= ctx.tuning.maxHealth &&
                                    combat.mode != PlayerMode::Hurt;
                std::printf("%s-charge chain=%s dodged=%d impact_dist=%.1f player_hp=%.0f at=%.1f\n",
                            p, sc.chain == 0 ? "attack_l" : "attack_r", dodged ? 1 : 0, dist,
                            combat.health, sc.seconds);
                if (!dodged || !(dist > q::kAttacks[0].waveRangeMeters)) {
                    return fail(sc, "the charge was not dodged");
                }
                next(sc, kHoof);
            }
            break;
        case kHoof:
            if (ctx.quadStab.started) {
                ++sc.strikes;
            }
            if (ctx.quadStab.hoof >= 0) {
                const int row = ctx.quadStab.hoof == q::W::HoofRL ? 0 : 1;
                std::printf("%s-hoof weakpoint=%s lift=%.2f stabs=%d state=%s damage=0 "
                            "health=%.0f at=%.1f\n",
                            p, weakpoint_material_name(ctx.enc().weakpoints, ctx.quadStab.hoof),
                            ctx.quadStab.lift[static_cast<std::size_t>(row)], sc.strikes,
                            quad_state_name(quad.rt.state), encounter.health, sc.seconds);
                if (encounter.health != encounter.desc->maxHealth) {
                    return fail(sc, "a hoof hit dealt damage");
                }
                sc.targetHoof = row;
                sc.strikes = 0;
                next(sc, kGrab);
            }
            break;
        case kGrab:
            if (quad.rt.state != sc.lastQuadState &&
                (quad.rt.state == q::S::DownBLLoop || quad.rt.state == q::S::DownBRLoop)) {
                sc.still = "kneel";
            }
            if (attached(ctx)) {
                std::printf("%s-grab band=%s state=%s stamina=%.1f at=%.1f\n", p, band_name(ctx),
                            quad_state_name(quad.rt.state), combat.stamina, sc.seconds);
                next(sc, kClimb);
            }
            break;
        case kClimb:
            if (!attached(ctx) && combat.mode != PlayerMode::Falling) {
                next(sc, kGrab);
            } else if (combat.band == q::B::Rump || combat.band == q::B::Back) {
                next(sc, quadratus_open_sigil(quad, encounter) == q::W::BackSigil ? kBackSigil
                                                                                  : kBack);
            }
            break;
        case kBackSigil:
        case kHeadSigil: {
            const int target = sc.phase == kBackSigil ? q::W::BackSigil : q::W::HeadSigil;
            if (sc.phase == kHeadSigil && sc.step == 1 && !sc.headStill) {
                sc.headStill = true;
                sc.still = "head";
            }
            if (encounter.defeated) {
                sc.diedAt = encounter.entityPosition;
                sc.diedAtYaw = encounter.entityYawDeg;
                next(sc, kDeath);
            } else if (quadratus_open_sigil(quad, encounter) != target && sc.step == 0 &&
                       attached(ctx)) {  // done, and caught again
                std::printf("%s-sigil done=%s strikes=%d health=%.0f at=%.1f\n", p,
                            target == q::W::BackSigil ? "back" : "head", sc.strikes,
                            encounter.health, sc.seconds);
                sc.strikes = 0;
                next(sc, kBack);
            } else if (!attached(ctx) && (combat.mode == PlayerMode::Grounded ||
                                          combat.mode == PlayerMode::Staggered)) {
                next(sc, kGrab);  // the catch missed: climb again
            }
            break;
        }
        case kBack:
            if (!attached(ctx) && combat.mode != PlayerMode::Falling) {
                next(sc, kGrab);
            } else if (combat.band == q::B::Back && combat.bandT >= kBackStopT) {
                std::printf("%s-back band=back t=%.2f across=%.2f feet_y=%.2f standing=%d "
                            "stamina=%.1f state=%s at=%.1f\n",
                            p, combat.bandT, combat.bandAcross,
                            ctx.player.position.y - encounter.entityPosition.y,
                            climb_band(ctx.enc().bands, combat.band).horizontal ? 1 : 0,
                            combat.stamina, quad_state_name(quad.rt.state), sc.seconds);
                sc.still = "back";
                next(sc, kShake);
            }
            break;
        case kShake:
            if (!attached(ctx)) {
                return fail(sc, "the shake threw the climber");
            }
            if (quad.rt.state == q::kShakeRule.state) {
                if (!sc.sawShake) {
                    std::printf("%s-shake start state=%s band=%s stamina=%.1f at=%.1f\n", p,
                                quad_state_name(quad.rt.state), band_name(ctx), combat.stamina,
                                sc.seconds);
                }
                sc.sawShake = true;
                sc.shakeLow = std::min(sc.shakeLow, combat.stamina);
            } else if (sc.sawShake) {
                std::printf("%s-shake survived=1 band=%s low=%.1f stamina=%.1f at=%.1f\n", p,
                            band_name(ctx), sc.shakeLow, combat.stamina, sc.seconds);
                next(sc, kToHead);
            }
            break;
        case kToHead:
            if (!attached(ctx) && combat.mode != PlayerMode::Falling) {
                next(sc, kGrab);
            } else if (combat.band == q::B::Head) {
                next(sc, kHeadSigil);
            }
            break;
        case kDeath: {
            const int phase = static_cast<int>(death.phase);
            if (phase != sc.lastDeathPhase) {
                std::printf("%s-death phase=%s total=%.3f state=%s\n", p,
                            death_phase_name(death.phase), death.total,
                            quad_state_name(quad.rt.state));
                sc.lastDeathPhase = phase;
            }
            if (death.total >= kDeathStillAt &&
                death.total - kFixedFrameSeconds < kDeathStillAt) {
                sc.still = "death";
            }
            if (death.phase == DeathPhase::Done) {
                const Playback& entity = ctx.playbacks[ctx.entityRig];
                const RigCpu& rig = ctx.scene.rigs[ctx.entityRig];
                const bool live = mesh_range_visible(ctx.scene, rig.meshBegin, rig.meshEnd);
                const float moved = std::hypot(encounter.entityPosition.x - sc.diedAt.x,
                                               encounter.entityPosition.z - sc.diedAt.z);
                std::printf("%s-corpse live_body=%d clip=%s t=%.3f held=%d moved=%.2f yaw=%.2f "
                            "return=%s\n",
                            p, live ? 1 : 0,
                            entity.clips[static_cast<std::size_t>(entity.index)].name.c_str(),
                            entity.time, entity.pinned ? 1 : 0, moved, encounter.entityYawDeg,
                            return_phase_name(ctx.carry.ret.phase));
                if (!live || ctx.carry.ret.phase != ReturnPhase::Idle) {
                    return fail(sc, "the corpse did not stay (or the return ran)");
                }
                sc.wantR = true;
                next(sc, kReset);
            }
            break;
        }
        case kReset: {
            if (sc.wantR) {
                break;  // the caller presses R, then calls again
            }
            const bool home =
                std::hypot(encounter.entityPosition.x - quad.basePosition.x,
                           encounter.entityPosition.z - quad.basePosition.z) < kResetTolerance &&
                std::fabs(encounter.entityYawDeg - quad.baseYawDeg) < kResetTolerance;
            std::printf("%s-reset health=%.0f defeated=%d state=%s at_base=%d player_hp=%.0f "
                        "mode=%s death=%s open=%s\n",
                        p, encounter.health, encounter.defeated ? 1 : 0,
                        quad_state_name(quad.rt.state), home ? 1 : 0, combat.health,
                        player_mode_name(combat.mode), death_phase_name(death.phase),
                        weakpoint_material_name(ctx.enc().weakpoints,
                                                quadratus_open_sigil(quad, encounter)));
            if (encounter.health != encounter.desc->maxHealth || encounter.defeated ||
                quad.rt.state != q::S::Stand || !home || death.phase != DeathPhase::Idle ||
                quadratus_open_sigil(quad, encounter) != q::W::BackSigil) {
                return fail(sc, "R did not bring Quadratus back to his opening state");
            }
            return 0;
        }
        default:
            break;
    }
    sc.lastQuadState = quad.rt.state;
    return std::nullopt;
}

// --s11-selftest: the S11 timeline in Quadratus's arena (one stage).
SelftestResult run_s11(StageCtx& ctx, RunState& run) {
    const AppOptions& opts = run.opts;
    S11Script sc;
    sc.prefix = "s11";
    if (!s11_start(sc, ctx, ctx.loadMs)) {
        return fail(sc, "the --manifest arena is not Quadratus's (encounter quadratus, his "
                        "bake) or his fight is unwired");
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
        in.input = s11_input(sc, ctx);
        last = frame_step(ctx, in);
        if (last.exitCode) {
            return last.exitCode;
        }
        std::optional<int> code = s11_after(sc, ctx);
        if (!code && sc.wantR) {
            ctx.press_reset(ctx.play.death, ctx.play.deathCueFired, &ctx.play.deathCameraSet);
            sc.wantR = false;
            code = s11_after(sc, ctx);
        }
        if (!sc.still.empty() && !opts.s11StillsPath.empty()) {
            with_clocks_restored(ctx.playbacks, [&]() {
                render();
                write_screenshot(run.renderer, run.shell.win,
                                 still_png_path(opts.s11StillsPath, sc.still.c_str()));
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
    // The perf run: his opening, rendered (the frame_ms the e2e's loop draws).
    render();  // warm-up
    const Uint64 perfStart = SDL_GetTicksNS();
    for (int i = 0; i < kS11PerfFrames; ++i) {
        FrameInput in;
        in.dt = kFixedFrameSeconds;
        last = frame_step(ctx, in);
        render();
    }
    const double perfMs = static_cast<double>(SDL_GetTicksNS() - perfStart) / 1e6;
    std::printf("s11-perf frames=%d frame_ms=%.3f drawn=%d culled=%d\n", kS11PerfFrames,
                perfMs / static_cast<double>(kS11PerfFrames), run.renderer.drawn_last_frame(),
                run.renderer.culled_last_frame());
    std::printf("s11-status phases=%d frames=%d seconds=%.1f health=%.0f\n", kPhaseCount,
                sc.frames, sc.seconds, ctx.encounter.health);
    print_status_line(opts, run.shell.win);
    return 0;
}
