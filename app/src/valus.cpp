#include "valus.h"

#include <algorithm>
#include <cmath>

namespace {

// Wraps an angle into (-180, 180]. A non-finite input answers 0: the yaw is
// written into a model matrix every frame, and a NaN there would erase the
// entity rather than hold it.
float wrap_deg(float degrees) {
    if (!std::isfinite(degrees)) {
        return 0.0f;
    }
    float wrapped = std::fmod(degrees + 180.0f, 360.0f);
    if (wrapped < 0.0f) {
        wrapped += 360.0f;
    }
    return wrapped - 180.0f;
}

// Horizontal distance test, inclusive at the range edge ("within 4.5 m"). The
// feet and the player both stand on the ground; ignoring y keeps a slope from
// hiding a stomp that is clearly in reach. No row: never in range.
bool foot_in_stomp_range(Vec3 playerPos, Vec3 foot, const AttackDesc* stomp) {
    if (stomp == nullptr) {
        return false;
    }
    const float dx = playerPos.x - foot.x;
    const float dz = playerPos.z - foot.z;
    return dx * dx + dz * dz <= stomp->reachMeters * stomp->reachMeters;
}

}  // namespace

ValusAttack select_valus_attack(const ValusAttackInput& input, const EncounterDesc& desc) {
    // Only an exactly grounded, unattached, living player is a target. A
    // non-finite cooldown is not ready (the <= test is written to reject NaN).
    if (!input.playerGrounded || input.playerAttached || input.defeated) {
        return ValusAttack::None;
    }
    if (!(input.cooldownSeconds <= 0.0f)) {
        return ValusAttack::None;
    }

    const bool left_in_range = foot_in_stomp_range(
        input.playerPos, input.footL, valus_attack_desc(ValusAttack::StompL, desc));
    const bool right_in_range = foot_in_stomp_range(
        input.playerPos, input.footR, valus_attack_desc(ValusAttack::StompR, desc));
    if (left_in_range || right_in_range) {
        if (left_in_range && right_in_range) {
            // Both feet: alternate, so a player standing between them sees a
            // two-footed attack instead of the same foot every cooldown.
            return input.stompLeftNext ? ValusAttack::StompL : ValusAttack::StompR;
        }
        return left_in_range ? ValusAttack::StompL : ValusAttack::StompR;
    }

    // The club: within 14 m and in front of the entity's yaw forward. The dot
    // is measured on the horizontal plane, against the normalized direction.
    const AttackDesc* club = valus_attack_desc(ValusAttack::Club, desc);
    if (club == nullptr) {
        return ValusAttack::None;
    }
    const float dx = input.playerPos.x - input.entityPos.x;
    const float dz = input.playerPos.z - input.entityPos.z;
    const float distance_squared = dx * dx + dz * dz;
    if (!(distance_squared <= club->reachMeters * club->reachMeters)) {
        return ValusAttack::None;  // also rejects NaN
    }
    const float distance = std::sqrt(distance_squared);
    if (!(distance > 1e-6f)) {
        return ValusAttack::None;  // the player is inside the entity: no facing
    }
    const float yaw = input.entityYawDeg * kDegToRad;
    const Vec3 forward{std::sin(yaw), 0.0f, std::cos(yaw)};
    const float facing = (dx * forward.x + dz * forward.z) / distance;
    return facing > club->facingDot ? ValusAttack::Club : ValusAttack::None;
}

bool attack_impact_due(ValusAttack attack, float clipSeconds, bool alreadyHit,
                       const EncounterDesc& desc) {
    if (alreadyHit || !std::isfinite(clipSeconds) || clipSeconds < 0.0f) {
        return false;
    }
    const AttackDesc* row = valus_attack_desc(attack, desc);
    return row != nullptr && clipSeconds >= row->impactBegin && clipSeconds <= row->impactEnd;
}

float valus_attack_cooldown_seconds(ValusAttack attack, const EncounterDesc& desc) {
    const AttackDesc* row = valus_attack_desc(attack, desc);
    return row != nullptr ? row->cooldownSeconds : 0.0f;
}

bool valus_attack_cancelled(bool landedStrike, ValusAttack attack) {
    return landedStrike && attack != ValusAttack::None;
}

float entity_yaw_deg(Quat rotation) {
    if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y) ||
        !std::isfinite(rotation.z) || !std::isfinite(rotation.w)) {
        return 0.0f;
    }
    // atan2 is scale-invariant, so the quaternion need not be normalized; a
    // zero quaternion has no direction and answers 0.
    return wrap_deg(2.0f * std::atan2(rotation.y, rotation.w) / kDegToRad);
}

float valus_yaw_slew(float currentYawDeg, Vec3 entityPos, Vec3 playerPos,
                     float maxRateDegPerSecond, float dtSeconds) {
    const float current = wrap_deg(currentYawDeg);
    if (!std::isfinite(dtSeconds) || dtSeconds <= 0.0f ||
        !std::isfinite(maxRateDegPerSecond)) {
        return current;
    }
    const float dx = playerPos.x - entityPos.x;
    const float dz = playerPos.z - entityPos.z;
    const float length_squared = dx * dx + dz * dz;
    if (!(length_squared > 1e-8f)) {
        return current;  // the player is on the entity: hold the facing
    }
    // The app's yaw convention (player.h): +Z is (sin yaw, 0, cos yaw).
    const float target = std::atan2(dx, dz) / kDegToRad;
    const float delta = wrap_deg(target - current);
    const float step = std::fabs(maxRateDegPerSecond) * dtSeconds;
    return wrap_deg(current + std::clamp(delta, -step, step));
}
