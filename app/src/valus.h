#pragma once

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

// The design's pinned numbers (§7.4). Exported so the app and the tests read the
// same constants instead of restating them.
inline constexpr float kValusStompRangeMeters = 4.5f;
inline constexpr float kValusClubRangeMeters = 14.0f;
inline constexpr float kValusClubFacingDot = 0.2f;
inline constexpr float kValusStompDirectRangeMeters = 2.5f;
inline constexpr float kValusStompWaveRangeMeters = 6.0f;
inline constexpr float kValusClubHitRangeMeters = 2.5f;
inline constexpr float kValusYawSlewDegPerSecond = 30.0f;
inline constexpr float kValusStompCooldownSeconds = 4.0f;
inline constexpr float kValusClubCooldownSeconds = 5.0f;
inline constexpr float kValusStompImpactBegin = 0.55f;
inline constexpr float kValusStompImpactEnd = 0.70f;
inline constexpr float kValusClubImpactBegin = 0.45f;
inline constexpr float kValusClubImpactEnd = 0.65f;

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

// The attack Valus starts this frame, or None. Range is horizontal (the feet
// and the player both stand on the ground); the club's facing dot is on the
// horizontal player direction.
ValusAttack select_valus_attack(const ValusAttackInput& input);

// Whether an in-progress attack's pinned impact window is open at
// `clipSeconds`, and has not already fired. A stomp's window is [0.55, 0.70] of
// the clip, a club's [0.45, 0.65]; `alreadyHit` makes the impact once-only.
// None, a negative/non-finite time, or a hit already applied all answer false.
bool attack_impact_due(ValusAttack attack, float clipSeconds, bool alreadyHit);

// The cooldown an attack sets when its clip ends: 4 s for a stomp, 5 s for the
// club, 0 for None.
float valus_attack_cooldown_seconds(ValusAttack attack);

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
