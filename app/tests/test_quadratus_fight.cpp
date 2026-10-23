// S11 Task 10: Quadratus's runtime glue (quadratus_fight.h) — his choices, the
// one hoof rule, the hit routing through the gates and the sigil order, the
// shake, and one frame's clock, turn yaw, root travel and attack impact. Pure:
// no library, the clip lengths are clips.md's measured values.

#include <doctest.h>

#include <cmath>
#include <vector>

#include "quadratus_fight.h"

namespace q = encounters::quadratus_detail;

namespace {

QuadSituation grounded(int state, float bearing, float distance, float cooldown = 0.0f) {
    QuadSituation s;
    s.state = state;
    s.bearingDeg = bearing;
    s.distance = distance;
    s.cooldownSeconds = cooldown;
    s.grounded = true;
    return s;
}

QuadSituation ridden(int state, float cycle, float pause = 0.0f) {
    QuadSituation s;
    s.state = state;
    s.attached = true;
    s.cycleSeconds = cycle;
    s.stabPauseSeconds = pause;
    return s;
}

// A fight whose clip table is the identity with clips.md's lengths (s).
QuadratusFight fight_of_lengths() {
    QuadratusFight fight;
    for (int c = 0; c < q::C::Count; ++c) {
        fight.clip[static_cast<std::size_t>(c)] = c;
        fight.seconds[static_cast<std::size_t>(c)] = 2.0f;
    }
    fight.seconds[q::C::Stand] = 4.333f;
    fight.seconds[q::C::Walk] = 2.1333f;
    fight.seconds[q::C::WalkStart] = 1.8667f;
    fight.seconds[q::C::WalkEnd] = 1.3333f;
    fight.seconds[q::C::TurnL] = 2.3333f;
    fight.seconds[q::C::TurnR] = 2.3333f;
    fight.seconds[q::C::AttackRStart] = 2.1006f;
    fight.seconds[q::C::AttackL] = 1.8335f;
    fight.seconds[q::C::AttackR] = 1.8335f;
    fight.seconds[q::C::AttackLLoop] = 0.0333f;
    fight.seconds[q::C::AttackRLoop] = 0.0333f;
    fight.seconds[q::C::AttackLEnd] = 3.4667f;
    fight.seconds[q::C::AttackREnd] = 3.4667f;
    fight.seconds[q::C::RefuseShoulder] = 4.3333f;
    fight.seconds[q::C::Death] = 5.3333f;
    return fight;
}

Heightmap flat_ground(float y = 0.0f) {
    Heightmap h;
    h.cellSize = 1.0f;
    h.width = 401;
    h.height = 401;
    h.heights.assign(static_cast<std::size_t>(h.width * h.height), y);
    h.boundsMin = Vec3{-200.0f, 0.0f, -200.0f};
    h.boundsMax = Vec3{200.0f, 0.0f, 200.0f};
    return h;
}

Encounter quadratus_encounter() {
    Encounter e;
    e.desc = &encounters::kQuadratus;
    e.health = e.desc->maxHealth;
    return e;
}

// A playback carrying a stand-in death clip (index 0) for start_encounter_one_shot.
Playback death_playback() {
    Playback p;
    p.clips.push_back(AnimationCpu{"boss_a_death", 5.3333f, {}});
    return p;
}

// Steps `seconds` at 1/60 s; returns every frame's result.
std::vector<QuadratusFrame> run(QuadratusFight& fight, Encounter& e, const Heightmap& ground,
                                const PlayerState& player, const PlayerCombat& combat,
                                float seconds) {
    Playback entity;
    std::vector<QuadratusFrame> frames;
    const int n = static_cast<int>(std::lround(seconds * 60.0f));
    for (int i = 0; i < n; ++i) {
        frames.push_back(quadratus_fight_step(fight, e, entity, ground, player, combat, 1.0f / 60.0f));
    }
    return frames;
}

}  // namespace

TEST_CASE("he stomps a grounded player within the ±20° front gate and 25 m, on the player's side") {
    CHECK(quadratus_decide(grounded(q::S::Stand, 10.0f, 20.0f)) == QuadDecision::AttackL);
    CHECK(quadratus_decide(grounded(q::S::Stand, -10.0f, 20.0f)) == QuadDecision::AttackR);
    CHECK(quadratus_decide(grounded(q::S::Stand, 19.0f, 25.0f)) == QuadDecision::AttackL);
    // Out of the gate or reach, or cooling down: he turns, walks or waits.
    CHECK(quadratus_decide(grounded(q::S::Stand, 25.0f, 20.0f)) == QuadDecision::None);
    CHECK(quadratus_decide(grounded(q::S::Stand, 0.0f, 20.0f, 2.0f)) == QuadDecision::None);
    CHECK(quadratus_decide(grounded(q::S::Stand, 0.0f, 28.0f)) == QuadDecision::None);
    CHECK(quadratus_decide(grounded(q::S::Stand, 0.0f, 40.0f)) == QuadDecision::Walk);
    // The ±30° turn trigger (data), a left turn for a positive bearing.
    CHECK(quadratus_decide(grounded(q::S::Stand, 31.0f, 10.0f)) == QuadDecision::TurnL);
    CHECK(quadratus_decide(grounded(q::S::Stand, -120.0f, 10.0f)) == QuadDecision::TurnR);
    // Mid-turn or mid-attack he decides nothing.
    CHECK(quadratus_decide(grounded(q::S::TurnL, 90.0f, 10.0f)) == QuadDecision::None);
    CHECK(quadratus_decide(grounded(q::S::AttackL, 0.0f, 10.0f)) == QuadDecision::None);
    // A player in the air or hurt is no target.
    QuadSituation air = grounded(q::S::Stand, 0.0f, 10.0f);
    air.grounded = false;
    CHECK(quadratus_decide(air) == QuadDecision::None);
}

TEST_CASE("he leaves the walk only at a cycle's end: a turn or the stop") {
    QuadSituation s = grounded(q::S::Walk, 45.0f, 50.0f);
    CHECK(quadratus_decide(s) == QuadDecision::None);
    s.walkCycleEnded = true;
    CHECK(quadratus_decide(s) == QuadDecision::WalkTurnL);
    s.bearingDeg = -45.0f;
    CHECK(quadratus_decide(s) == QuadDecision::WalkTurnR);
    s.bearingDeg = 0.0f;
    s.distance = 20.0f;
    CHECK(quadratus_decide(s) == QuadDecision::WalkEnd);
}

TEST_CASE("a rider is shaken in the first 5 s of each 20 s cycle, never in a stab's pause") {
    CHECK(quadratus_decide(ridden(q::S::Stand, 0.0f)) == QuadDecision::Shake);
    CHECK(quadratus_decide(ridden(q::S::Stand, 4.9f)) == QuadDecision::Shake);
    CHECK(quadratus_decide(ridden(q::S::Stand, 5.1f)) == QuadDecision::None);
    CHECK(quadratus_decide(ridden(q::S::Stand, 21.0f)) == QuadDecision::Shake);
    CHECK(quadratus_decide(ridden(q::S::Stand, 1.0f, 2.0f)) == QuadDecision::None);
    // Not while kneeling, rising or already shuddering.
    CHECK(quadratus_decide(ridden(q::S::DownBLLoop, 0.0f)) == QuadDecision::None);
    CHECK(quadratus_decide(ridden(q::S::RiseReturnB, 0.0f)) == QuadDecision::None);
    CHECK(quadratus_decide(ridden(q::S::RefuseShoulder, 0.0f)) == QuadDecision::None);
}

TEST_CASE("the one hoof rule: the 21 hoof-ignoring states and attachment block it") {
    for (int state = 0; state < q::S::Count; ++state) {
        CAPTURE(quad_state_name(state));
        const bool listed = std::find(q::kNoHoofGateStates.begin(), q::kNoHoofGateStates.end(),
                                      state) != q::kNoHoofGateStates.end();
        CHECK(quadratus_hoof_blocked(state, false) == listed);
        CHECK(quadratus_hoof_blocked(state, true));
        // The same rule the gate table carries (a hoof hit is a Transition
        // exactly where the rule leaves it open).
        CHECK((behaviour_gate(encounters::kQuadratus.behaviour, state, q::W::HoofRL).kind ==
               GateKind::Transition) == !listed);
    }
    CHECK_FALSE(quadratus_hoof_blocked(q::S::Stand, false));
    CHECK(quadratus_hoof_blocked(q::S::DownBRLoop, false));
}

TEST_CASE("hits route through the gates: back sigil first, each sigil to its share, then death") {
    QuadratusFight fight = fight_of_lengths();
    Encounter e = quadratus_encounter();
    Playback entity = death_playback();
    EntityClips clips;
    clips.death = 0;

    // The head before the back is sealed: no damage.
    QuadratusHit hit = quadratus_fight_hit(fight, e, entity, clips, q::W::HeadSigil, 34.0f);
    CHECK(hit.kind == GateKind::Ignore);
    CHECK(hit.sealed);
    CHECK(e.health == 220.0f);
    CHECK(quadratus_open_sigil(fight, e) == q::W::BackSigil);

    // The back takes 34, 34, 34, then the 8 left of its 110 share.
    for (const float want : {34.0f, 34.0f, 34.0f, 8.0f}) {
        hit = quadratus_fight_hit(fight, e, entity, clips, q::W::BackSigil, 34.0f);
        CHECK(hit.kind == GateKind::Damage);
        CHECK(hit.damage == doctest::Approx(want));
    }
    CHECK(e.health == doctest::Approx(110.0f));
    CHECK(quadratus_open_sigil(fight, e) == q::W::HeadSigil);
    hit = quadratus_fight_hit(fight, e, entity, clips, q::W::BackSigil, 34.0f);
    CHECK(hit.sealed);  // spent
    CHECK(e.health == doctest::Approx(110.0f));

    // A hoof on the climb (no strike reaches one; the gate still routes it): a
    // Transition into its kneel, no damage.
    hit = quadratus_fight_hit(fight, e, entity, clips, q::W::HoofRL, 34.0f);
    CHECK(hit.kind == GateKind::Transition);
    CHECK(fight.rt.state == q::S::DownBLStart);
    CHECK(e.health == doctest::Approx(110.0f));
    hit = quadratus_fight_hit(fight, e, entity, clips, q::W::HoofRR, 34.0f);
    CHECK(hit.kind == GateKind::Ignore);  // a kneel ignores every hoof (tagged deviation)
    CHECK(fight.rt.state == q::S::DownBLStart);

    // The head: 34, 34, 34, then the killing 8.
    for (int i = 0; i < 3; ++i) {
        hit = quadratus_fight_hit(fight, e, entity, clips, q::W::HeadSigil, 34.0f);
        CHECK_FALSE(hit.killed);
    }
    hit = quadratus_fight_hit(fight, e, entity, clips, q::W::HeadSigil, 34.0f);
    CHECK(hit.killed);
    CHECK(hit.damage == doctest::Approx(8.0f));
    CHECK(e.defeated);
    CHECK(e.health == 0.0f);
    CHECK(e.deathWeakpoint == q::W::HeadSigil);
    CHECK(e.reactionClip == 0);  // the death one-shot started
    CHECK(fight.rt.state == q::S::Death);
    CHECK(quadratus_open_sigil(fight, e) == -1);

    // Nothing counts after: no damage, no transition.
    hit = quadratus_fight_hit(fight, e, entity, clips, q::W::HoofRL, 34.0f);
    CHECK(hit.kind == GateKind::Ignore);
    CHECK(fight.rt.state == q::S::Death);
}

TEST_CASE("the refuse_shoulder shake drains only inside its active window") {
    QuadratusFight fight = fight_of_lengths();
    fight.rt.state = q::S::RefuseShoulder;
    fight.rt.stateSeconds = 0.1f;
    CHECK(quadratus_shake(fight).staminaDrain == 0.0f);
    fight.rt.stateSeconds = 1.0f;
    CHECK(quadratus_shake(fight).staminaDrain == doctest::Approx(34.8f));
    CHECK_FALSE(quadratus_shake(fight).forceRelease);
    fight.rt.stateSeconds = 3.6f;
    CHECK(quadratus_shake(fight).staminaDrain == 0.0f);
    fight.rt.state = q::S::Stand;
    fight.rt.stateSeconds = 1.0f;
    CHECK(quadratus_shake(fight).staminaDrain == 0.0f);

    quadratus_fight_struck(fight);
    CHECK(fight.stabPauseSeconds == doctest::Approx(3.0f));
}

TEST_CASE("a turn holds his yaw while it plays and adds its 45° at its end") {
    QuadratusFight fight = fight_of_lengths();
    Encounter e = quadratus_encounter();
    const Heightmap ground = flat_ground();
    PlayerState player;
    PlayerCombat combat;
    combat.mode = PlayerMode::Falling;  // no choice this test
    fight.rt.state = q::S::TurnL;
    const std::vector<QuadratusFrame> frames = run(fight, e, ground, player, combat, 2.3f);
    CHECK(e.entityYawDeg == 0.0f);
    const std::vector<QuadratusFrame> end = run(fight, e, ground, player, combat, 0.1f);
    CHECK(e.entityYawDeg == doctest::Approx(45.0f));
    CHECK(fight.rt.state == q::S::Stand);
}

TEST_CASE("the walk carries him 20 m per cycle along his yaw, on the ground") {
    QuadratusFight fight = fight_of_lengths();
    Encounter e = quadratus_encounter();
    e.entityYawDeg = 90.0f;  // facing +x
    const Heightmap ground = flat_ground(-3.0f);
    PlayerState player;
    PlayerCombat combat;
    combat.mode = PlayerMode::Falling;
    fight.rt.state = q::S::Walk;
    run(fight, e, ground, player, combat, 2.1333f);
    CHECK(e.entityPosition.x == doctest::Approx(20.0f).epsilon(0.01));
    CHECK(std::fabs(e.entityPosition.z) < 0.1f);
    CHECK(e.entityPosition.y == doctest::Approx(-3.0f));
}

TEST_CASE("a stomp chain's impact opens once at its window and its end sets the cooldown") {
    QuadratusFight fight = fight_of_lengths();
    Encounter e = quadratus_encounter();
    const Heightmap ground = flat_ground();
    PlayerState player;
    player.position = Vec3{2.0f, 0.0f, 20.0f};  // 5.7° to his left, 20 m: the left stomp
    PlayerCombat combat;
    std::vector<QuadratusFrame> frames = run(fight, e, ground, player, combat, 1.0f / 60.0f);
    REQUIRE(frames[0].decision == QuadDecision::AttackL);
    CHECK(fight.rt.state == q::S::AttackLStart);
    frames = run(fight, e, ground, player, combat, 9.0f);
    int impacts = 0;
    float impactAt = -1.0f;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].impactDue) {
            ++impacts;
            impactAt = static_cast<float>(i + 1) / 60.0f;
            CHECK(frames[i].impactAttack == 0);
        }
    }
    CHECK(impacts == 1);
    // attack_r_start 2.1006 + 0.929 x attack_l 1.8335 = 3.80 s after the start.
    CHECK(impactAt == doctest::Approx(3.80f).epsilon(0.01));
    // The chain (2.1006 + 1.8335 + 0.0333 + 3.4667 = 7.43 s) ends, its 5 s cooldown runs.
    CHECK(fight.chain == -1);
    CHECK(fight.rt.cooldownSeconds > 0.0f);
    CHECK(fight.rt.cooldownSeconds < 5.0f);
}
