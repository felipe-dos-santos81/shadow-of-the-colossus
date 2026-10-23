// The entity behaviour machine (S11 Task 8, design D9 and §13.10). Every case is
// pure: tables in, runtime out. The tables here are TEST data — a Quadratus-like
// kneel table and Valus as a one-state machine — not the shipped encounter
// tables (those live with the EncounterDesc).
#include <doctest.h>

#include <array>
#include <cmath>
#include <limits>

#include "behaviour.h"
#include "math3d.h"
#include "valus.h"

namespace {

// --- A Quadratus-like table (test data; the recon's numbers) -----------------
//
// Weakpoints: 0 head, 1 rump (the back sigil), 2..5 the hoof soles fl, fr, rl, rr.
// States: 0 stand; per hoof h (0..3): 1+h down start, 5+h down loop, 9+h down end.
// Clips mirror the states' numbering: 0 wait, 1+h start (2.0 s), 5+h loop
// (1.667 s), 9+h end (3.333 s front, 3.667 s rear).
//
// Builder's calls, labelled: the kneel holds its loop for 5.0 s (authored —
// the recon found no kneel duration); the vitals damage in EVERY state (the
// kneel gates the climb's reach to the back, not the sigil's gate); a hoof stab
// while already kneeling is ignored; the stand turn rate is 45 deg / 2.33 s.
constexpr int kQStand = 0;
constexpr int kQHead = 0;
constexpr int kQRump = 1;
constexpr int kQHoof0 = 2;
constexpr float kQKneelHold = 5.0f;
constexpr float kQTurn = 45.0f / 2.33f;

constexpr int q_start(int hoof) { return 1 + hoof; }
constexpr int q_loop(int hoof) { return 5 + hoof; }
constexpr int q_end(int hoof) { return 9 + hoof; }

constexpr BehaviourStateDesc q_state(int index) {
    if (index == kQStand) {
        return {.clip = 0, .turnRateDegPerSecond = kQTurn};
    }
    if (index < 5) {
        return {.clip = index, .clipEndNext = q_loop(index - 1)};
    }
    if (index < 9) {
        return {.clip = index, .timerSeconds = kQKneelHold, .timerNext = q_end(index - 5)};
    }
    return {.clip = index, .clipEndNext = kQStand};
}

constexpr std::array<BehaviourStateDesc, 13> kQStates = {
    q_state(0), q_state(1), q_state(2),  q_state(3),  q_state(4),  q_state(5), q_state(6),
    q_state(7), q_state(8), q_state(9), q_state(10), q_state(11), q_state(12)};

constexpr std::array<WeakpointGate, 6> kQGates = {{
    {kQStand, kQHoof0 + 0, GateKind::Transition, q_start(0)},
    {kQStand, kQHoof0 + 1, GateKind::Transition, q_start(1)},
    {kQStand, kQHoof0 + 2, GateKind::Transition, q_start(2)},
    {kQStand, kQHoof0 + 3, GateKind::Transition, q_start(3)},
    {kBehaviourNone, kQHead, GateKind::Damage, kBehaviourNone},
    {kBehaviourNone, kQRump, GateKind::Damage, kBehaviourNone},
}};

constexpr BehaviourDesc kQuadratus{kQStates, {}, kQGates};
static_assert(behaviour_valid(kQuadratus));

constexpr std::array<float, 13> kQClips = {3.0f,   2.0f,   2.0f,   2.0f,   2.0f,
                                           1.667f, 1.667f, 1.667f, 1.667f, 3.333f,
                                           3.333f, 3.667f, 3.667f};

// --- Valus as a one-state machine (test data) --------------------------------
//
// One state: the stand loop, Valus's 30 deg/s slew, and his three attacks as the
// state's actions (slice order = ValusAttack - 1). Both shipped weakpoints
// (0 forehead, 1 right arm) damage in that state.
constexpr std::array<BehaviourAction, 3> kVActions = {{
    {1, kValusStompCooldownSeconds},
    {2, kValusStompCooldownSeconds},
    {3, kValusClubCooldownSeconds},
}};
constexpr std::array<BehaviourStateDesc, 1> kVStates = {
    {{.clip = 0, .turnRateDegPerSecond = kValusYawSlewDegPerSecond, .actionCount = 3}}};
constexpr std::array<WeakpointGate, 2> kVGates = {{
    {kBehaviourNone, 0, GateKind::Damage, kBehaviourNone},
    {kBehaviourNone, 1, GateKind::Damage, kBehaviourNone},
}};
constexpr BehaviourDesc kValus{kVStates, kVActions, kVGates};
static_assert(behaviour_valid(kValus));

// Stand, stomp L, stomp R, club (the club is 1.333 s, `minotaur_a_atk_weapon2_a`).
constexpr std::array<float, 4> kVClips = {3.0f, 2.0f, 2.0f, 1.333f};

BehaviourRuntime at(int state) {
    BehaviourRuntime rt;
    rt.state = state;
    return rt;
}

}  // namespace

TEST_CASE("behaviour_valid rejects every out-of-range index") {
    CHECK_FALSE(behaviour_valid(BehaviourDesc{}));  // no states

    const std::array<BehaviourStateDesc, 1> badNext = {{{.clipEndNext = 1}}};
    CHECK_FALSE(behaviour_valid({badNext, {}, {}}));
    const std::array<BehaviourStateDesc, 1> badTimer = {{{.timerNext = -2}}};
    CHECK_FALSE(behaviour_valid({badTimer, {}, {}}));
    const std::array<BehaviourStateDesc, 1> badSlice = {{{.firstAction = 2, .actionCount = 2}}};
    CHECK_FALSE(behaviour_valid({badSlice, kVActions, {}}));
    const std::array<BehaviourStateDesc, 1> negSlice = {{{.firstAction = -1, .actionCount = 1}}};
    CHECK_FALSE(behaviour_valid({negSlice, kVActions, {}}));

    const std::array<BehaviourStateDesc, 1> one = {{{.clip = 0}}};
    const std::array<WeakpointGate, 1> badTarget = {{{0, 0, GateKind::Transition, 1}}};
    CHECK_FALSE(behaviour_valid({one, {}, badTarget}));
    const std::array<WeakpointGate, 1> noTarget = {{{0, 0, GateKind::Transition, -1}}};
    CHECK_FALSE(behaviour_valid({one, {}, noTarget}));
    const std::array<WeakpointGate, 1> badState = {{{3, 0, GateKind::Damage, -1}}};
    CHECK_FALSE(behaviour_valid({one, {}, badState}));
    const std::array<WeakpointGate, 1> badWeak = {{{0, -1, GateKind::Damage, -1}}};
    CHECK_FALSE(behaviour_valid({one, {}, badWeak}));

    CHECK(behaviour_valid({one, {}, {}}));
}

TEST_CASE("a clip end moves the state on and keeps the overshoot") {
    BehaviourRuntime rt = at(q_start(2));
    rt = step_behaviour(kQuadratus, rt, 1.9f, kQClips);
    CHECK(rt.state == q_start(2));
    CHECK(rt.stateSeconds == doctest::Approx(1.9f));
    CHECK(behaviour_clip(kQuadratus, rt) == q_start(2));

    rt = step_behaviour(kQuadratus, rt, 0.25f, kQClips);  // 2.15 s: the 2.0 s start ends
    CHECK(rt.state == q_loop(2));
    CHECK(rt.stateSeconds == doctest::Approx(0.15f));
    CHECK(behaviour_clip(kQuadratus, rt) == q_loop(2));
}

TEST_CASE("a looping clip holds its state until the timer fires") {
    BehaviourRuntime rt = at(q_loop(0));
    // Three loop lengths (5.001 s) pass 5.0 s; two (3.334 s) do not.
    rt = step_behaviour(kQuadratus, rt, 2.0f * 1.667f, kQClips);
    CHECK(rt.state == q_loop(0));
    rt = step_behaviour(kQuadratus, rt, 1.0f, kQClips);  // 4.334 s
    CHECK(rt.state == q_loop(0));
    rt = step_behaviour(kQuadratus, rt, 0.7f, kQClips);  // 5.034 s
    CHECK(rt.state == q_end(0));
    CHECK(rt.stateSeconds == doctest::Approx(0.034f).epsilon(1e-3));
}

TEST_CASE("the earlier of a clip end and a timer fires; a tie is the clip end") {
    const std::array<BehaviourStateDesc, 3> s = {{
        {.clip = 0, .clipEndNext = 1, .timerSeconds = 1.0f, .timerNext = 2},
        {.clip = 0},
        {.clip = 0},
    }};
    const BehaviourDesc d{s, {}, {}};
    const std::array<float, 1> longer = {2.0f};
    const std::array<float, 1> shorter = {0.5f};
    const std::array<float, 1> tie = {1.0f};
    CHECK(step_behaviour(d, at(0), 3.0f, longer).state == 2);
    CHECK(step_behaviour(d, at(0), 3.0f, shorter).state == 1);
    CHECK(step_behaviour(d, at(0), 3.0f, tie).state == 1);
}

TEST_CASE("a hoof stab kneels Quadratus and the kneel walks back to stand") {
    BehaviourRuntime rt = at(kQStand);
    rt = step_behaviour(kQuadratus, rt, 0.5f, kQClips);
    const BehaviourHit hit = behaviour_hit(kQuadratus, rt, kQHoof0 + 3);  // rear right
    CHECK(hit.kind == GateKind::Transition);
    rt = hit.runtime;
    CHECK(rt.state == q_start(3));
    CHECK(rt.stateSeconds == 0.0f);
    CHECK(behaviour_turn_rate(kQuadratus, rt) == 0.0f);  // a kneeling bull does not turn

    // 2.0 start + 5.0 hold + 3.667 rear end = 10.667 s, sliced two ways.
    BehaviourRuntime fine = rt;
    for (int i = 0; i < 600; ++i) {  // 10.0 s
        fine = step_behaviour(kQuadratus, fine, 1.0f / 60.0f, kQClips);
    }
    const BehaviourRuntime coarse = step_behaviour(kQuadratus, rt, 10.0f, kQClips);
    CHECK(fine.state == q_end(3));
    CHECK(coarse.state == q_end(3));
    CHECK(fine.stateSeconds == doctest::Approx(3.0f).epsilon(1e-3));
    CHECK(coarse.stateSeconds == doctest::Approx(3.0f).epsilon(1e-5));

    const BehaviourRuntime back = step_behaviour(kQuadratus, coarse, 0.7f, kQClips);
    CHECK(back.state == kQStand);
    CHECK(back.stateSeconds == doctest::Approx(0.033f).epsilon(1e-2));
    CHECK(behaviour_turn_rate(kQuadratus, back) == doctest::Approx(kQTurn));

    // One step across the whole chain lands in the same place.
    CHECK(step_behaviour(kQuadratus, rt, 10.7f, kQClips).state == kQStand);
}

TEST_CASE("gates are per state: a hoof kneels from stand and is ignored while kneeling") {
    CHECK(behaviour_gate(kQuadratus, kQStand, kQHoof0).kind == GateKind::Transition);
    CHECK(behaviour_gate(kQuadratus, kQStand, kQHoof0).target == q_start(0));
    for (int s = 1; s < 13; ++s) {
        CHECK(behaviour_gate(kQuadratus, s, kQHoof0).kind == GateKind::Ignore);
        // The vitals' gate is a wildcard: damage in every state (builder's call).
        CHECK(behaviour_gate(kQuadratus, s, kQHead).kind == GateKind::Damage);
        CHECK(behaviour_gate(kQuadratus, s, kQRump).kind == GateKind::Damage);
    }
    BehaviourRuntime kneeling = at(q_loop(1));
    kneeling.stateSeconds = 1.0f;
    const BehaviourHit again = behaviour_hit(kQuadratus, kneeling, kQHoof0 + 1);
    CHECK(again.kind == GateKind::Ignore);
    CHECK(again.runtime.state == q_loop(1));
    CHECK(again.runtime.stateSeconds == 1.0f);
    const BehaviourHit rump = behaviour_hit(kQuadratus, kneeling, kQRump);
    CHECK(rump.kind == GateKind::Damage);
    CHECK(rump.runtime.state == q_loop(1));  // damage never moves the state
}

TEST_CASE("the same weakpoint damages in one state and transitions in another") {
    // State 0: weakpoint 7 damages. State 1: weakpoint 7 sends the entity to 2.
    const std::array<BehaviourStateDesc, 3> s = {{{.clip = 0}, {.clip = 0}, {.clip = 0}}};
    const std::array<WeakpointGate, 2> g = {{
        {1, 7, GateKind::Transition, 2},
        {kBehaviourNone, 7, GateKind::Damage, kBehaviourNone},
    }};
    const BehaviourDesc d{s, {}, g};
    REQUIRE(behaviour_valid(d));
    CHECK(behaviour_hit(d, at(0), 7).kind == GateKind::Damage);
    CHECK(behaviour_hit(d, at(0), 7).runtime.state == 0);
    CHECK(behaviour_hit(d, at(2), 7).kind == GateKind::Damage);
    const BehaviourHit h = behaviour_hit(d, at(1), 7);
    CHECK(h.kind == GateKind::Transition);
    CHECK(h.runtime.state == 2);
    CHECK(behaviour_hit(d, at(1), 6).kind == GateKind::Ignore);  // no row
}

TEST_CASE("invalid indices are inert") {
    // An out-of-range runtime state: no step, no hit, no clip, no turn.
    for (int bad : {-1, 13, 1000}) {
        BehaviourRuntime rt = at(bad);
        rt.stateSeconds = 0.5f;
        const BehaviourRuntime stepped = step_behaviour(kQuadratus, rt, 1.0f, kQClips);
        CHECK(stepped.state == bad);
        CHECK(stepped.stateSeconds == 0.5f);
        CHECK(behaviour_hit(kQuadratus, rt, kQHead).kind == GateKind::Ignore);
        CHECK(behaviour_clip(kQuadratus, rt) == kBehaviourNone);
        CHECK(behaviour_turn_rate(kQuadratus, rt) == 0.0f);
    }
    // A negative or unknown weakpoint meets no gate.
    CHECK(behaviour_hit(kQuadratus, at(kQStand), -1).kind == GateKind::Ignore);
    CHECK(behaviour_hit(kQuadratus, at(kQStand), 99).kind == GateKind::Ignore);

    // An invalid table answers nothing, even for a row that looks fine.
    const std::array<BehaviourStateDesc, 1> one = {{{.clip = 0}}};
    const std::array<WeakpointGate, 2> g = {{
        {0, 0, GateKind::Damage, kBehaviourNone},
        {0, 1, GateKind::Transition, 5},
    }};
    const BehaviourDesc broken{one, {}, g};
    REQUIRE_FALSE(behaviour_valid(broken));
    CHECK(behaviour_hit(broken, at(0), 0).kind == GateKind::Ignore);
    CHECK(behaviour_hit(broken, at(0), 1).kind == GateKind::Ignore);
    CHECK(step_behaviour(broken, at(0), 1.0f, kQClips).stateSeconds == 0.0f);

    // A clip with no usable duration never ends its state.
    const std::array<BehaviourStateDesc, 2> s = {{{.clip = 5, .clipEndNext = 1}, {.clip = 0}}};
    const BehaviourDesc d{s, {}, {}};
    CHECK(step_behaviour(d, at(0), 100.0f, kVClips).state == 0);  // clip 5 past a 4-clip table
    const std::array<float, 6> zero = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f};
    CHECK(step_behaviour(d, at(0), 100.0f, zero).state == 0);
    const std::array<float, 6> nan = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                      std::numeric_limits<float>::quiet_NaN()};
    CHECK(step_behaviour(d, at(0), 100.0f, nan).state == 0);

    // A requested action outside the state's slice does not start.
    CHECK(step_behaviour(kValus, at(0), 0.1f, kVClips, 3).action == kBehaviourNone);
    CHECK(step_behaviour(kValus, at(0), 0.1f, kVClips, -2).action == kBehaviourNone);
    CHECK(step_behaviour(kQuadratus, at(kQStand), 0.1f, kQClips, 0).action == kBehaviourNone);
}

TEST_CASE("a non-finite or negative dt advances nothing") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    BehaviourRuntime rt = at(q_start(0));
    rt.stateSeconds = 1.0f;
    rt.cooldownSeconds = 2.0f;
    for (float dt : {nan, inf, -inf, -1.0f}) {
        const BehaviourRuntime out = step_behaviour(kQuadratus, rt, dt, kQClips);
        CHECK(out.state == q_start(0));
        CHECK(out.stateSeconds == 1.0f);
        CHECK(out.cooldownSeconds == 2.0f);
        CHECK(std::isfinite(out.stateSeconds));
    }
    // A non-finite cooldown is spent rather than kept (as advance_valus_attack).
    BehaviourRuntime poisoned = at(0);
    poisoned.cooldownSeconds = nan;
    CHECK(step_behaviour(kValus, poisoned, nan, kVClips).cooldownSeconds == 0.0f);
    poisoned.cooldownSeconds = inf;
    CHECK(step_behaviour(kValus, poisoned, 0.1f, kVClips).cooldownSeconds == 0.0f);
}

TEST_CASE("an action runs its clip, sets its cooldown, and gates the next one") {
    BehaviourRuntime rt = step_behaviour(kValus, at(0), 1.0f / 60.0f, kVClips, 2);  // club
    CHECK(rt.action == 2);
    CHECK(rt.actionSeconds == 0.0f);  // the start frame does not advance the clip
    CHECK(behaviour_clip(kValus, rt) == 3);

    rt = step_behaviour(kValus, rt, 1.0f, kVClips, 0);  // a request mid-action is ignored
    CHECK(rt.action == 2);
    CHECK(rt.actionSeconds == doctest::Approx(1.0f));
    rt = step_behaviour(kValus, rt, 0.5f, kVClips, 0);  // 1.5 s: the 1.333 s club ends
    CHECK(rt.action == kBehaviourNone);
    CHECK(rt.cooldownSeconds == kValusClubCooldownSeconds);
    CHECK(behaviour_clip(kValus, rt) == 0);

    rt = step_behaviour(kValus, rt, 1.0f, kVClips, 0);  // still cooling down
    CHECK(rt.action == kBehaviourNone);
    CHECK(rt.cooldownSeconds == doctest::Approx(4.0f));
    rt = step_behaviour(kValus, rt, 4.0f, kVClips, 0);  // spent this frame: starts
    CHECK(rt.action == 0);

    // A zero-length action clip never starts (no zero-length telegraph).
    const std::array<float, 4> noClub = {3.0f, 2.0f, 2.0f, 0.0f};
    CHECK(step_behaviour(kValus, at(0), 0.1f, noClub, 2).action == kBehaviourNone);
}

TEST_CASE("a damaging hit cancels the action; a transition drops it") {
    BehaviourRuntime rt = step_behaviour(kValus, at(0), 0.1f, kVClips, 1);
    rt = step_behaviour(kValus, rt, 0.6f, kVClips);
    REQUIRE(rt.action == 1);
    const BehaviourHit hit = behaviour_hit(kValus, rt, 0);
    CHECK(hit.kind == GateKind::Damage);
    CHECK(hit.runtime.action == kBehaviourNone);
    CHECK(hit.runtime.actionSeconds == 0.0f);
    CHECK(hit.cancelledCooldownSeconds == kValusStompCooldownSeconds);
    CHECK(hit.runtime.cooldownSeconds == 0.0f);  // returned, not set: the app defers it
    CHECK(behaviour_hit(kValus, at(0), 0).cancelledCooldownSeconds == 0.0f);

    // A transition hit mid-action enters the target with no action.
    const std::array<BehaviourStateDesc, 2> s = {{{.clip = 0, .actionCount = 1}, {.clip = 0}}};
    const std::array<BehaviourAction, 1> a = {{{1, 3.0f}}};
    const std::array<WeakpointGate, 1> g = {{{0, 0, GateKind::Transition, 1}}};
    const BehaviourDesc d{s, a, g};
    BehaviourRuntime busy = step_behaviour(d, at(0), 0.1f, kVClips, 0);
    busy.cooldownSeconds = 1.5f;
    REQUIRE(busy.action == 0);
    const BehaviourHit moved = behaviour_hit(d, busy, 0);
    CHECK(moved.kind == GateKind::Transition);
    CHECK(moved.runtime.state == 1);
    CHECK(moved.runtime.action == kBehaviourNone);
    CHECK(moved.runtime.cooldownSeconds == 1.5f);
}

// Valus as a one-state machine reproduces S4.2's decisions. The reference is
// app.cpp's sequence restated over valus.cpp's functions — the reaction tick and
// its deferred cooldown (advance_encounter_reaction), the yaw slew for a
// grounded player, the attack input built before the cooldown tick
// (valus_attack_input), advance_valus_attack, and a landed strike's cancel
// (apply_combat_events → cancel_valus_attack). The machine run replaces the
// attack state with BehaviourRuntime and the slew rate with the state's.
TEST_CASE("Valus as a one-state machine makes the same decisions as valus.cpp") {
    struct World {
        Vec3 player;
        bool grounded;
        bool attached;
    };
    const auto world_at = [](float t) {
        if (t < 2.0f) return World{{0.0f, 0.0f, 30.0f}, true, false};   // far: nothing
        if (t < 12.0f) return World{{8.0f, 0.0f, 8.0f}, true, false};   // front: the club
        if (t < 36.0f) return World{{0.0f, 0.0f, 1.5f}, true, false};   // between the feet
        if (t < 39.0f) return World{{0.0f, 0.0f, 1.5f}, false, true};   // on him
        return World{{0.0f, 0.0f, -10.0f}, true, false};                 // behind: he turns
    };
    const Vec3 entity{0.0f, 0.0f, 0.0f};
    const Vec3 footL{-3.0f, 0.0f, 1.0f};
    const Vec3 footR{3.0f, 0.0f, 1.0f};
    constexpr float kReaction = 1.0f;
    const auto attack_clip_seconds = [](ValusAttack a) {
        return a == ValusAttack::Club ? kVClips[3] : kVClips[1];
    };
    const auto input_for = [&](const World& w, float cooldown, float yaw, bool leftNext) {
        ValusAttackInput in;
        in.playerGrounded = w.grounded;
        in.playerAttached = w.attached;
        in.cooldownSeconds = cooldown;
        in.playerPos = w.player;
        in.entityPos = entity;
        in.entityYawDeg = yaw;
        in.footL = footL;
        in.footR = footR;
        in.stompLeftNext = leftNext;
        return in;
    };

    // The reference: app.cpp's Encounter fields.
    ValusAttack attack = ValusAttack::None;
    float attackSeconds = 0.0f, cooldown = 0.0f, pending = 0.0f, yaw = 0.0f, reaction = -1.0f;
    bool leftNext = true;
    // The machine run.
    BehaviourRuntime rt;
    float mPending = 0.0f, mYaw = 0.0f, mReaction = -1.0f;
    bool mLeftNext = true;

    int starts[4] = {0, 0, 0, 0};
    int cancels = 0, mismatch = -1, frames = 0;
    bool struckStomp = false, struckClub = false, struckIdle = false;
    float t = 0.0f;
    for (int i = 0; t < 52.0f; ++i, ++frames) {
        const float dt = (i % 3 == 0) ? 1.0f / 30.0f : 1.0f / 60.0f;
        t += dt;
        const World w = world_at(t);

        // advance_encounter_reaction: the reaction ends, the deferred cooldown starts.
        if (reaction >= 0.0f && (reaction += dt) >= kReaction) {
            reaction = -1.0f;
            if (pending > 0.0f) { cooldown = pending; pending = 0.0f; }
        }
        if (mReaction >= 0.0f && (mReaction += dt) >= kReaction) {
            mReaction = -1.0f;
            if (mPending > 0.0f) { rt.cooldownSeconds = mPending; mPending = 0.0f; }
        }

        // The yaw slew, then the input (pre-tick cooldown), then the attack step.
        if (w.grounded) {
            yaw = valus_yaw_slew(yaw, entity, w.player, kValusYawSlewDegPerSecond, dt);
            mYaw = valus_yaw_slew(mYaw, entity, w.player, behaviour_turn_rate(kValus, rt), dt);
        }
        const ValusAttackInput in = input_for(w, cooldown, yaw, leftNext);
        const ValusAttackInput mIn = input_for(w, rt.cooldownSeconds, mYaw, mLeftNext);

        cooldown = std::max(0.0f, cooldown - dt);
        // Note: select_valus_attack is shared by both arms of this equivalence test, so
        // selection itself is not proven here (test_valus.cpp covers it).
        if (attack == ValusAttack::None) {
            if (reaction < 0.0f) {
                const ValusAttack chosen = select_valus_attack(in);
                if (chosen != ValusAttack::None) {
                    attack = chosen;
                    attackSeconds = 0.0f;
                    ++starts[static_cast<int>(chosen)];
                    if (chosen != ValusAttack::Club) leftNext = chosen == ValusAttack::StompR;
                }
            }
        } else if ((attackSeconds += dt) >= attack_clip_seconds(attack)) {
            cooldown = valus_attack_cooldown_seconds(attack);
            attack = ValusAttack::None;
            attackSeconds = 0.0f;
        }

        int request = kBehaviourNone;
        if (rt.action == kBehaviourNone && mReaction < 0.0f) {
            request = static_cast<int>(select_valus_attack(mIn)) - 1;  // None -> -1
        }
        const int before = rt.action;
        rt = step_behaviour(kValus, rt, dt, kVClips, request);
        if (before == kBehaviourNone && rt.action != kBehaviourNone && rt.action != 2) {
            mLeftNext = rt.action == 1;
        }

        // Scripted strikes (the player's step runs after Valus's): once mid-stomp,
        // once mid-club, once with nothing to cancel.
        bool strike = false;
        if (!struckStomp && attack != ValusAttack::None && attack != ValusAttack::Club &&
            attackSeconds > 0.5f) {
            strike = struckStomp = true;
        } else if (!struckClub && attack == ValusAttack::Club && attackSeconds > 0.3f && t > 39.0f) {
            strike = struckClub = true;
        } else if (!struckIdle && t > 37.0f && attack == ValusAttack::None) {
            strike = struckIdle = true;
        }
        if (strike) {
            if (valus_attack_cancelled(true, attack)) {
                pending = valus_attack_cooldown_seconds(attack);
                attack = ValusAttack::None;
                attackSeconds = 0.0f;
                ++cancels;
            }
            reaction = 0.0f;
            const BehaviourHit hit = behaviour_hit(kValus, rt, 0);
            CHECK(hit.kind == GateKind::Damage);
            rt = hit.runtime;
            if (hit.cancelledCooldownSeconds > 0.0f) mPending = hit.cancelledCooldownSeconds;
            mReaction = 0.0f;
        }

        const ValusAttack mAttack = static_cast<ValusAttack>(rt.action + 1);
        if (mismatch < 0 && (mAttack != attack || rt.actionSeconds != attackSeconds ||
                             rt.cooldownSeconds != cooldown || mYaw != yaw ||
                             mLeftNext != leftNext || mPending != pending)) {
            mismatch = i;
        }
    }

    CHECK(mismatch == -1);
    CHECK(frames > 2300);
    // The script exercised every decision: both stomps, the club before and after
    // the turn, two cancels, and the turn itself (he ends facing the player behind).
    CHECK(starts[static_cast<int>(ValusAttack::StompL)] >= 2);
    CHECK(starts[static_cast<int>(ValusAttack::StompR)] >= 2);
    CHECK(starts[static_cast<int>(ValusAttack::Club)] >= 2);
    CHECK(cancels == 2);
    CHECK(struckIdle);
    CHECK(std::fabs(std::fabs(yaw) - 180.0f) < 1.0f);
}

TEST_CASE("behaviour_enter resets the state clock and the action, and keeps the cooldown") {
    BehaviourRuntime rt = at(0);
    rt.stateSeconds = 1.25f;
    rt.action = 0;
    rt.actionSeconds = 0.4f;
    rt.cooldownSeconds = 2.0f;
    const BehaviourRuntime in = behaviour_enter(kValus, rt, 0);
    CHECK(in.state == 0);
    CHECK(in.stateSeconds == 0.0f);
    CHECK(in.action == kBehaviourNone);
    CHECK(in.actionSeconds == 0.0f);
    CHECK(in.cooldownSeconds == 2.0f);

    BehaviourRuntime stand = at(kQStand);
    stand.stateSeconds = 0.7f;
    const BehaviourRuntime to = behaviour_enter(kQuadratus, stand, 5);
    CHECK(to.state == 5);
    CHECK(to.stateSeconds == 0.0f);
}

TEST_CASE("behaviour_enter and behaviour_gate are inert for an invalid table or state") {
    BehaviourRuntime rt = at(kQStand);
    rt.stateSeconds = 0.5f;
    rt.action = 0;
    for (int bad : {-1, 13, 1000}) {
        const BehaviourRuntime out = behaviour_enter(kQuadratus, rt, bad);
        CHECK(out.state == kQStand);
        CHECK(out.stateSeconds == 0.5f);
        CHECK(out.action == 0);
        CHECK(behaviour_gate(kQuadratus, bad, kQHead).kind == GateKind::Ignore);
    }
    // A wildcard (-1) row must not answer an out-of-range state.
    const std::array<BehaviourStateDesc, 1> one = {{{.clip = 0}}};
    const std::array<WeakpointGate, 1> wild = {{{kBehaviourNone, 0, GateKind::Damage, kBehaviourNone}}};
    const BehaviourDesc ok{one, {}, wild};
    REQUIRE(behaviour_valid(ok));
    CHECK(behaviour_gate(ok, 0, 0).kind == GateKind::Damage);
    CHECK(behaviour_gate(ok, 3, 0).kind == GateKind::Ignore);

    const std::array<WeakpointGate, 2> bad = {{
        {0, 0, GateKind::Damage, kBehaviourNone},
        {0, 1, GateKind::Transition, 5},
    }};
    const BehaviourDesc broken{one, {}, bad};
    REQUIRE_FALSE(behaviour_valid(broken));
    CHECK(behaviour_enter(broken, rt, 0).stateSeconds == 0.5f);
    CHECK(behaviour_gate(broken, 0, 0).kind == GateKind::Ignore);
}
