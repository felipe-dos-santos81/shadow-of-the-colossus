#include "quadratus_fight.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace q = encounters::quadratus_detail;

namespace {

const BehaviourDesc& table() { return encounters::kQuadratus.behaviour; }

bool in(int state, std::initializer_list<int> states) {
    return std::find(states.begin(), states.end(), state) != states.end();
}

int state_clip(int state) {
    return state >= 0 && state < q::S::Count ? q::kStates[static_cast<std::size_t>(state)].clip : -1;
}

const char* clip_name(int clip) {
    return clip >= 0 && clip < q::C::Count ? q::kClips[static_cast<std::size_t>(clip)] : "";
}

// A one-shot state's clip clamps; a state that holds (clipEndNext -1: the stand,
// the walk, a kneel's loop, rising_return's stand) loops it.
bool state_loops(int state) {
    return state >= 0 && state < q::S::Count &&
           q::kStates[static_cast<std::size_t>(state)].clipEndNext == kBehaviourNone;
}

float clip_yaw(int state) {
    for (const q::ClipYaw& y : q::kClipYaw) {
        if (y.state == state) {
            return y.degrees;
        }
    }
    return 0.0f;
}

// The state whose clip is the chain's impact clip (kAttacks[i].clip).
int impact_state(int chain) {
    const q::AttackChain& c = q::kAttackChains[static_cast<std::size_t>(chain)];
    for (int s = c.first; s <= c.last; ++s) {
        if (std::string_view(clip_name(state_clip(s))) == q::kAttacks[static_cast<std::size_t>(chain)].clip) {
            return s;
        }
    }
    return -1;
}

// His root travel `local` (the clip-start frame: +z his front, +x his left)
// rotated by his yaw onto the ground under the new origin. A move off the map or
// onto no floor is refused (he stays).
void travel(Encounter& encounter, const Heightmap& ground, Vec3 local) {
    if (local.x == 0.0f && local.z == 0.0f) {
        return;
    }
    const float r = encounter.entityYawDeg * kDegToRad;
    const float c = std::cos(r);
    const float s = std::sin(r);
    Vec3 to{encounter.entityPosition.x + local.x * c + local.z * s, encounter.entityPosition.y,
            encounter.entityPosition.z - local.x * s + local.z * c};
    if (!heightmap_in_bounds(ground, to)) {
        return;
    }
    const float floor = heightmap_floor(ground, to, kQuadGroundStep);
    if (!std::isfinite(floor)) {
        return;
    }
    to.y = floor;
    encounter.entityPosition = to;
}

float share(const Encounter& encounter) {
    return encounter.desc->maxHealth / static_cast<float>(q::kSigilOrder.size());
}

void enter(QuadratusFight& fight, int state) {
    fight.rt = behaviour_enter(table(), fight.rt, state);
    fight.lastTime = 0.0f;
}

}  // namespace

const char* quad_state_name(int state) {
    static constexpr const char* kNames[q::S::Count] = {
        "Stand", "Search", "WalkStart", "Walk", "WalkEnd", "TurnL", "TurnR", "WalkTurnL",
        "WalkTurnR", "Back", "AttackRStart", "AttackR", "AttackRLoop", "AttackREnd",
        "AttackLStart", "AttackL", "AttackLLoop", "AttackLEnd", "DownBLStart", "DownBLLoop",
        "DownBLEnd", "DownBRStart", "DownBRLoop", "DownBREnd", "DownFLStart", "DownFLLoop",
        "DownFLEnd", "DownFRStart", "DownFRLoop", "DownFREnd", "RiseReturnB", "RiseReturnF",
        "RefuseHead", "RefuseShoulder", "RefuseArmL", "RefuseArmR", "RefuseLegL", "RefuseLegR",
        "Angry", "Death"};
    return state >= 0 && state < q::S::Count ? kNames[state] : "none";
}

const char* quad_decision_name(QuadDecision d) {
    switch (d) {
        case QuadDecision::None: return "none";
        case QuadDecision::TurnL: return "turn_l";
        case QuadDecision::TurnR: return "turn_r";
        case QuadDecision::Walk: return "walk";
        case QuadDecision::WalkTurnL: return "walk_turn_l";
        case QuadDecision::WalkTurnR: return "walk_turn_r";
        case QuadDecision::WalkEnd: return "walk_end";
        case QuadDecision::AttackL: return "attack_l";
        case QuadDecision::AttackR: return "attack_r";
        case QuadDecision::Shake: return "shake";
    }
    return "none";
}

bool quadratus_fight_load(QuadratusFight& fight, const Scene& scene, std::size_t entityRig,
                          const Playback& entity, const Playback& player, std::string& why) {
    for (int c = 0; c < q::C::Count; ++c) {
        const int index = clip_index_named(entity, q::kClips[static_cast<std::size_t>(c)]);
        if (index < 0) {
            why = std::string("his glb carries no ") + q::kClips[static_cast<std::size_t>(c)];
            return false;
        }
        fight.clip[static_cast<std::size_t>(c)] = index;
        fight.seconds[static_cast<std::size_t>(c)] =
            entity.clips[static_cast<std::size_t>(index)].duration;
    }
    fight.stabClip = clip_index_named(player, kQuadStabClip);
    if (fight.stabClip < 0) {
        why = std::string("the player glb carries no ") + kQuadStabClip;
        return false;
    }
    fight.stabDuration = player.clips[static_cast<std::size_t>(fight.stabClip)].duration;
    if (!quadratus_hoof_soles(scene, entityRig, fight.soles)) {
        why = "his hoof soles did not resolve (a toe bone, a sole material or boss_a_stand)";
        return false;
    }
    return true;
}

void quadratus_fight_reset(QuadratusFight& fight, Encounter& encounter, Playback& entity) {
    fight.rt = BehaviourRuntime{};
    fight.rt.state = q::S::Stand;
    fight.lastClip = q::C::Stand;
    fight.lastTime = 0.0f;
    fight.chain = -1;
    fight.impactDone = false;
    fight.sigilSpent = {};
    fight.cycleSeconds = 0.0f;
    fight.stabPauseSeconds = 0.0f;
    fight.strikeWasDown = false;
    fight.stabSeconds = -1.0f;
    encounter.entityPosition = fight.basePosition;
    encounter.entityYawDeg = fight.baseYawDeg;
    entity.index = fight.clip[q::C::Stand];
    entity.time = 0.0f;
    entity.pinned = false;
    entity.playing = false;
}

QuadDecision quadratus_decide(const QuadSituation& s) {
    const bool free = in(s.state, {q::S::Stand, q::S::Walk, q::S::Search});
    if (s.attached) {
        // The shake rule (data: kShakeRule): within the attached cycle's window,
        // not paused by a stab, not already shuddering. While ridden he does
        // nothing else (authored: no turns or attacks with a climber on him).
        if (free && s.stabPauseSeconds <= 0.0f &&
            std::fmod(s.cycleSeconds, q::kShakeRule.attachedCycleSeconds) <
                q::kShakeRule.windowSeconds) {
            return QuadDecision::Shake;
        }
        return s.state == q::S::Walk && s.walkCycleEnded ? QuadDecision::WalkEnd
                                                          : QuadDecision::None;
    }
    if (!s.grounded) {
        return QuadDecision::None;
    }
    const float facing = std::fabs(s.bearingDeg);
    if (s.state == q::S::Stand) {
        // The attack (data: the row's ±20° front gate; its 25 m reach is
        // authored): the stomping hoof on the player's side.
        const int side = s.bearingDeg >= 0.0f ? 0 : 1;  // kAttacks: 0 left, 1 right
        const AttackDesc& attack = q::kAttacks[static_cast<std::size_t>(side)];
        if (s.cooldownSeconds <= 0.0f && facing <= std::acos(attack.facingDot) / kDegToRad &&
            s.distance <= attack.reachMeters && s.distance >= kQuadAttackMinMeters) {
            return side == 0 ? QuadDecision::AttackL : QuadDecision::AttackR;
        }
        if (facing > q::kTurnTriggerDeg && s.distance > kQuadTurnMinMeters) {  // data: ±30°
            return s.bearingDeg > 0.0f ? QuadDecision::TurnL : QuadDecision::TurnR;
        }
        if (s.distance > kQuadWalkStartMeters) {
            return QuadDecision::Walk;
        }
        return QuadDecision::None;
    }
    if (s.state == q::S::Walk && s.walkCycleEnded) {
        if (s.distance <= kQuadWalkStopMeters) {
            return QuadDecision::WalkEnd;
        }
        if (facing > q::kTurnTriggerDeg) {
            return s.bearingDeg > 0.0f ? QuadDecision::WalkTurnL : QuadDecision::WalkTurnR;
        }
    }
    return QuadDecision::None;
}

bool quadratus_hoof_blocked(int state, bool attached) {
    return attached || std::find(q::kNoHoofGateStates.begin(), q::kNoHoofGateStates.end(), state) !=
                           q::kNoHoofGateStates.end();
}

QuadratusFrame quadratus_fight_step(QuadratusFight& fight, Encounter& encounter, Playback& entity,
                                    const Heightmap& ground, const PlayerState& player,
                                    const PlayerCombat& combat, float dt) {
    QuadratusFrame out;
    out.stateBefore = fight.rt.state;
    if (!(dt >= 0.0f) || !std::isfinite(dt)) {
        dt = 0.0f;
    }
    if (fight.stabSeconds >= 0.0f) {
        fight.stabSeconds += dt;
        if (fight.stabSeconds >= fight.stabDuration) {
            fight.stabSeconds = -1.0f;
        }
    }
    if (encounter.defeated) {
        // The death sequence drives the clip (advance_death_frame pins its time);
        // he travels its root (the fall moves him 17.5 m, C§1) and holds.
        if (fight.rt.state != q::S::Death) {
            enter(fight, q::S::Death);
        }
        const float t = entity.time;
        if (fight.lastClip != q::C::Death) {
            fight.lastClip = q::C::Death;
            fight.lastTime = 0.0f;
        }
        travel(encounter, ground, quadratus_root_delta(clip_name(q::C::Death), fight.lastTime, t));
        fight.lastTime = t;
        out.stateAfter = fight.rt.state;
        return out;
    }

    const bool attached = mode_is_attached(combat.mode);
    if (combat.mode == PlayerMode::Grounded) {
        fight.cycleSeconds = 0.0f;  // the attached cycle restarts on the ground
    } else if (attached) {
        fight.cycleSeconds += dt;
    }
    fight.stabPauseSeconds = std::max(0.0f, fight.stabPauseSeconds - dt);

    // The behaviour clock.
    const int before = fight.rt.state;
    const float beforeT = fight.rt.stateSeconds;
    fight.rt = step_behaviour(table(), fight.rt, dt, fight.seconds);
    if (fight.rt.state != before) {
        // The old clip's remaining root, then the yaw a turn adds at its end.
        const float oldEnd = beforeT + (dt - fight.rt.stateSeconds);
        travel(encounter, ground,
               quadratus_root_delta(clip_name(state_clip(before)), fight.lastTime, oldEnd));
        out.yawAddedDeg = clip_yaw(before);
        encounter.entityYawDeg = std::remainder(encounter.entityYawDeg + out.yawAddedDeg, 360.0f);
        if (fight.chain >= 0) {
            const int impact = impact_state(fight.chain);
            if (before == impact && !fight.impactDone) {
                fight.impactDone = true;  // the window ends AT the clip end (C§4): inclusive
                out.impactDue = true;
                out.impactAttack = fight.chain;
            }
            if (before == q::kAttackChains[static_cast<std::size_t>(fight.chain)].last) {
                // A5: the chain's last state sets the attack row's cooldown.
                fight.rt.cooldownSeconds =
                    q::kAttacks[static_cast<std::size_t>(fight.chain)].cooldownSeconds;
                fight.chain = -1;
                out.chainEnded = true;
            }
        }
        fight.lastTime = 0.0f;
    }
    travel(encounter, ground,
           quadratus_root_delta(clip_name(state_clip(fight.rt.state)), fight.lastTime,
                                fight.rt.stateSeconds));
    fight.lastTime = fight.rt.stateSeconds;
    if (fight.chain >= 0 && !fight.impactDone && fight.rt.state == impact_state(fight.chain)) {
        const AttackDesc& attack = q::kAttacks[static_cast<std::size_t>(fight.chain)];
        const float length = fight.seconds[static_cast<std::size_t>(state_clip(fight.rt.state))];
        if (length > 0.0f && fight.rt.stateSeconds >= attack.impactBegin * length) {
            fight.impactDone = true;
            out.impactDue = true;
            out.impactAttack = fight.chain;
        }
    }

    // His choice against the player.
    QuadSituation s;
    s.state = fight.rt.state;
    const float walk = fight.seconds[q::C::Walk];
    s.walkCycleEnded = fight.rt.state == q::S::Walk && walk > 0.0f &&
                       (fight.rt.stateSeconds < dt ||
                        std::floor(fight.rt.stateSeconds / walk) !=
                            std::floor((fight.rt.stateSeconds - dt) / walk));
    const float dx = player.position.x - encounter.entityPosition.x;
    const float dz = player.position.z - encounter.entityPosition.z;
    s.distance = std::sqrt(dx * dx + dz * dz);
    s.bearingDeg = std::remainder(std::atan2(dx, dz) / kDegToRad - encounter.entityYawDeg, 360.0f);
    s.cooldownSeconds = fight.rt.cooldownSeconds;
    s.grounded = combat.mode == PlayerMode::Grounded;
    s.attached = attached;
    s.cycleSeconds = fight.cycleSeconds;
    s.stabPauseSeconds = fight.stabPauseSeconds;
    out.decision = quadratus_decide(s);
    switch (out.decision) {
        case QuadDecision::None: break;
        case QuadDecision::TurnL: enter(fight, q::S::TurnL); break;
        case QuadDecision::TurnR: enter(fight, q::S::TurnR); break;
        case QuadDecision::Walk: enter(fight, q::S::WalkStart); break;
        case QuadDecision::WalkTurnL: enter(fight, q::S::WalkTurnL); break;
        case QuadDecision::WalkTurnR: enter(fight, q::S::WalkTurnR); break;
        case QuadDecision::WalkEnd: enter(fight, q::S::WalkEnd); break;
        case QuadDecision::Shake: enter(fight, q::kShakeRule.state); break;
        case QuadDecision::AttackL:
        case QuadDecision::AttackR:
            fight.chain = out.decision == QuadDecision::AttackL ? 0 : 1;
            fight.impactDone = false;
            enter(fight, q::kAttackChains[static_cast<std::size_t>(fight.chain)].first);
            break;
    }
    out.stateAfter = fight.rt.state;

    // The entity's clip: the app writes the time (the clock is the machine's).
    const int clip = state_clip(fight.rt.state);
    fight.lastClip = clip;
    entity.index = fight.clip[static_cast<std::size_t>(clip)];
    entity.time = fight.rt.stateSeconds;
    entity.pinned = !state_loops(fight.rt.state);
    entity.playing = false;
    return out;
}

ShakeOff quadratus_shake(const QuadratusFight& fight) {
    for (const q::ShakeDesc& shake : q::kShakes) {
        if (shake.state == fight.rt.state && fight.rt.stateSeconds >= shake.activeBegin &&
            fight.rt.stateSeconds <= shake.activeEnd) {
            return ShakeOff{shake.drainPerSecond, false};
        }
    }
    return ShakeOff{};
}

void quadratus_fight_struck(QuadratusFight& fight) {
    fight.stabPauseSeconds = q::kShakeRule.stabPauseSeconds;
}

int quadratus_open_sigil(const QuadratusFight& fight, const Encounter& encounter) {
    const float each = share(encounter);
    for (std::size_t i = 0; i < q::kSigilOrder.size(); ++i) {
        if (fight.sigilSpent[i] < each) {
            return q::kSigilOrder[i];
        }
    }
    return -1;
}

QuadratusHit quadratus_fight_hit(QuadratusFight& fight, Encounter& encounter, Playback& entity,
                                 const EntityClips& clips, int weakpoint, float damage) {
    QuadratusHit out;
    out.weakpoint = weakpoint;
    if (encounter.defeated || !(damage > 0.0f)) {
        return out;  // nothing counts in Death, and a post-death hit is ignored
    }
    const BehaviourHit hit = behaviour_hit(table(), fight.rt, weakpoint);
    out.kind = hit.kind;
    if (hit.kind == GateKind::Ignore) {
        return out;
    }
    if (hit.kind == GateKind::Transition) {
        fight.rt = hit.runtime;  // a hoof: its kneel, no damage
        fight.chain = -1;
        fight.lastTime = 0.0f;
        return out;
    }
    const int open = quadratus_open_sigil(fight, encounter);
    if (weakpoint != open) {
        out.kind = GateKind::Ignore;
        out.sealed = true;  // the head before the back (kSigilOrder)
        return out;
    }
    std::size_t k = 0;
    while (q::kSigilOrder[k] != open) {
        ++k;
    }
    const float amount = std::min(damage, share(encounter) - fight.sigilSpent[k]);
    fight.sigilSpent[k] += amount;
    encounter.health = std::max(0.0f, encounter.health - amount);
    out.damage = amount;
    if (quadratus_open_sigil(fight, encounter) < 0 || encounter.health <= 0.0f) {
        encounter.health = 0.0f;
        encounter.defeated = true;
        encounter.pendingCooldownSeconds = 0.0f;
        encounter.deathWeakpoint = weakpoint;
        start_encounter_one_shot(entity, encounter, clips.death);
        enter(fight, q::S::Death);
        fight.chain = -1;
        fight.lastClip = q::C::Death;
        out.killed = true;
    }
    return out;
}

bool quadratus_hoof_window(const QuadratusFight& fight, const Encounter& encounter,
                           const Scene& scene, std::size_t entityRig,
                           const std::vector<Mat4>& palette, const Heightmap& ground,
                           const PlayerState& player, const PlayerCombat& combat,
                           const WeakpointProxyLookup& proxy, std::size_t row, float* lift) {
    if (lift != nullptr) {
        *lift = std::numeric_limits<float>::quiet_NaN();
    }
    if (row >= q::kHooves.size()) {
        return false;
    }
    const q::HoofDesc& hoofRow = q::kHooves[row];
    // quadratus_motion's Hoof order (fl, fr, rl, rr) by the row's toe bone.
    const std::string_view joint = hoofRow.joint;
    const Hoof hoof = joint == "Bone_TOE_L"     ? Hoof::FrontLeft
                      : joint == "Bone_TOE_R"   ? Hoof::FrontRight
                      : joint == "Bone_B_TOE_L" ? Hoof::RearLeft
                                                : Hoof::RearRight;
    Mat4 toe{};
    if (!rig_bone_world(scene, entityRig, palette, hoofRow.joint, toe)) {
        return false;
    }
    const float floor = heightmap_floor(
        ground, Vec3{toe.m[12], encounter.entityPosition.y + 1.0f, toe.m[14]}, kQuadGroundStep);
    const HoofSole& sole = fight.soles[static_cast<std::size_t>(hoof)];
    if (lift != nullptr) {
        *lift = quadratus_hoof_lift(sole, toe, floor);
    }
    // The stab's sword reaches past the body capsule (kQuadStabReachMeters).
    const WeakpointProxyLookup reach = [&proxy](int weakpoint, Vec3& centre, float& radius) {
        if (!proxy || !proxy(weakpoint, centre, radius)) {
            return false;
        }
        radius += kQuadStabReachMeters;
        return true;
    };
    return quadratus_hoof_kneels(sole, toe, floor,
                                 quadratus_hoof_blocked(fight.rt.state, mode_is_attached(combat.mode)),
                                 player.position, hoofRow.weakpoint, reach, hoofRow.liftMeters);
}

QuadratusStab quadratus_ground_stab(QuadratusFight& fight, Encounter& encounter, const Scene& scene,
                                    std::size_t entityRig, const std::vector<Mat4>& palette,
                                    const Heightmap& ground, const PlayerState& player,
                                    const PlayerCombat& combat, const PlayerInput& input,
                                    const WeakpointProxyLookup& proxy) {
    QuadratusStab out;
    out.lift.fill(std::numeric_limits<float>::quiet_NaN());
    const bool pressed = input.strike && !fight.strikeWasDown;
    fight.strikeWasDown = input.strike;
    if (!pressed || combat.mode != PlayerMode::Grounded || fight.stabSeconds >= 0.0f ||
        encounter.defeated) {
        return out;
    }
    out.started = true;
    fight.stabSeconds = 0.0f;
    for (std::size_t i = 0; i < q::kHooves.size(); ++i) {
        const bool kneels = quadratus_hoof_window(fight, encounter, scene, entityRig, palette, ground,
                                                  player, combat, proxy, i, &out.lift[i]);
        if (out.hoof < 0 && kneels) {
            const BehaviourHit hit = behaviour_hit(table(), fight.rt, q::kHooves[i].weakpoint);
            if (hit.kind == GateKind::Transition) {
                fight.rt = hit.runtime;
                fight.chain = -1;
                fight.lastTime = 0.0f;
                out.hoof = q::kHooves[i].weakpoint;
            }
        }
    }
    return out;
}

bool quadratus_stabbing(const QuadratusFight& fight, const PlayerCombat& combat) {
    return fight.stabSeconds >= 0.0f && combat.mode == PlayerMode::Grounded;
}
