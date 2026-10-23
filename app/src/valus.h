#pragma once

#include "encounters/valus_desc.h"
#include "math3d.h"

// Valus's attack decision (S4.2 Task 7, ruling R8): a pure, headless-testable
// module so the selection matrix and the one-impact-per-clip window do not live
// inside the 2,800-line app loop. `app.cpp` owns the state (Encounter), the
// playback switch and the damage; everything that answers a question about the
// world answers it here.
//
// Selection is deliberately conservative: only a player who is exactly
// `PlayerMode::Grounded` and not attached is a target (design §7.4). A stomp
// wins over a club whenever either foot is in range, because Valus is already
// standing on the threat; the club is the long reach and needs the player in
// front (`dot > 0.2`, measured on the horizontal plane against the entity's
// yaw forward). Cooldown, defeat and a non-grounded target all answer None.

enum class ValusAttack { None, StompL, StompR, Club };

// The table row of `attack` (S11 Task 7: Valus's attacks are rows of his
// EncounterDesc, indexed ValusAttack - 1; None has no row), or nullptr.
constexpr const AttackDesc* valus_attack_desc(ValusAttack attack, const EncounterDesc& desc) {
    const int index = static_cast<int>(attack) - 1;
    return index >= 0 && static_cast<std::size_t>(index) < desc.attacks.size()
               ? &desc.attacks[static_cast<std::size_t>(index)]
               : nullptr;
}
static_assert(static_cast<int>(ValusAttack::Club) == static_cast<int>(encounters::kValus.attacks.size()),
              "ValusAttack names Valus's attack rows (+ None)");

// One frame's attack decision inputs. The fields are ruling R8's list, plus
// `stompLeftNext`: alternation needs one bit of memory, and carrying it in the
// struct keeps `select_valus_attack` pure and the rule testable. The app flips
// it after each stomp, so two stomps in a row alternate feet; with only one foot
// in range the reachable foot wins and the flag still follows it.
struct ValusAttackInput {
    bool playerGrounded = false;   // PlayerMode::Grounded exactly
    bool playerAttached = false;   // Grabbing / Climbing / Striking
    bool defeated = false;
    float cooldownSeconds = 0.0f;
    Vec3 playerPos{};
    Vec3 entityPos{};
    float entityYawDeg = 0.0f;
    Vec3 footL{};
    Vec3 footR{};
    bool stompLeftNext = true;
};

// The attack the encounter starts this frame, or None. Range is horizontal (the
// feet and the player both stand on the ground); the club's facing dot is on
// the horizontal player direction. Every range and gate is `desc`'s attack row
// (S11 M-1); an attack without a row is never chosen.
ValusAttack select_valus_attack(const ValusAttackInput& input, const EncounterDesc& desc);

// Whether an in-progress attack's impact window (its row's [impactBegin,
// impactEnd]; Valus: a stomp's [0.55, 0.70], the club's [0.45, 0.65]) is open
// at `clipSeconds`, and has not already fired; `alreadyHit` makes the impact
// once-only. None (or no row), a negative/non-finite time, or a hit already
// applied all answer false.
bool attack_impact_due(ValusAttack attack, float clipSeconds, bool alreadyHit,
                       const EncounterDesc& desc);

// The cooldown an attack sets when its clip ends: its row's (Valus: 4 s for a
// stomp, 5 s for the club), 0 for None or no row.
float valus_attack_cooldown_seconds(ValusAttack attack, const EncounterDesc& desc);

// Whether `attack`'s in-progress clip is cancelled: only a landed strike does
// (design §7.4), and only when there is an attack to cancel. A whiff leaves the
// telegraph and its pending impact alone.
bool valus_attack_cancelled(bool landedStrike, ValusAttack attack);

// The yaw (degrees, wrapped into (-180, 180]) of a manifest entity rotation.
// The encounter manifest places Valus with a yaw-only quaternion; a non-yaw
// rotation keeps only its yaw component, which is all the slew can drive.
float entity_yaw_deg(Quat rotation);

// `currentYawDeg` slewed toward `playerPos` by at most
// `maxRateDegPerSecond * dtSeconds` on the shortest path. Returns the wrapped
// yaw. A zero-length player direction, a non-positive/non-finite dt, or a
// non-finite rate/yaw leaves the current yaw unchanged (wrapped) — never NaN.
float valus_yaw_slew(float currentYawDeg, Vec3 entityPos, Vec3 playerPos,
                     float maxRateDegPerSecond, float dtSeconds);
