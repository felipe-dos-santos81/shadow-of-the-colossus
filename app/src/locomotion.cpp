#include "locomotion.h"

#include <algorithm>
#include <cmath>

namespace {

float unit_or_zero(float v) { return std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f; }

}  // namespace

GroundQuery heightmap_ground(const Heightmap& heightmap) {
    return [&heightmap](Vec3 from, Vec3 to, float& floorY) {
        if (!heightmap_in_bounds(heightmap, to) || !heightmap_walkable(heightmap, from, to)) {
            return false;
        }
        floorY = heightmap_floor(heightmap, to, kFloorStep);
        return true;
    };
}

LocomotionResult locomotion_step(EntityMotion& motion, float drive, float turn,
                                 const LocomotionTuning& tuning, const GroundQuery& ground,
                                 float dt) {
    if (!std::isfinite(dt) || dt <= 0.0f) {
        return LocomotionResult::Still;
    }
    const float t = unit_or_zero(turn);
    if (t != 0.0f) {
        const float yaw = std::remainder(motion.yawDeg - t * tuning.turnRateDeg * dt, 360.0f);
        if (std::isfinite(yaw)) {
            motion.yawDeg = yaw;
        }
    }

    const float d = unit_or_zero(drive);
    const float speed = d * (d > 0.0f ? tuning.walkSpeed : tuning.backSpeed);
    if (!std::isfinite(speed) || speed == 0.0f) {
        return LocomotionResult::Still;
    }
    const float yawRad = motion.yawDeg * kDegToRad;
    const Vec3 to{motion.position.x + std::sin(yawRad) * speed * dt, motion.position.y,
                  motion.position.z + std::cos(yawRad) * speed * dt};
    float floorY = 0.0f;
    if (!ground || !ground(motion.position, to, floorY) || !std::isfinite(floorY)) {
        return LocomotionResult::Refused;
    }
    motion.position = Vec3{to.x, floorY, to.z};
    return LocomotionResult::Moved;
}
