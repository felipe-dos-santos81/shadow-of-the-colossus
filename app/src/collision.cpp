#include "collision.h"

#include "anim.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

// The ruling's iteration count: enough for overlapping capsules to settle.
constexpr int kResolveIterations = 2;

// Below this the player is considered to sit on the capsule's axis: the exit
// direction is then the horizontal perpendicular (the shortest exit anyway).
constexpr float kAxisEpsilon = 1e-5f;

bool is_finite(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

}  // namespace

float player_capsule_distance(Vec3 feet, Vec3 point) {
    // The closest point on the player's own segment, not on the point's XZ:
    // the horizontal offset is the reach that makes an attack miss.
    const Vec3 closest{feet.x, std::clamp(point.y, feet.y, feet.y + kPlayerCapsuleHeight),
                       feet.z};
    const float dx = point.x - closest.x;
    const float dy = point.y - closest.y;
    const float dz = point.z - closest.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void resolve_player_collision(PlayerState& player, const std::vector<Capsule>& capsules) {
    const float feet = player.position.y;
    const float head = feet + kPlayerCapsuleHeight;

    // The plan pins two passes. Consequence: a player *placed* exactly between
    // two capsules whose exclusion zones overlap (Valus's feet are ~2.7 m
    // apart with 2.05 m clearances) can remain inside the union. Walking
    // cannot reach that point — the zones overlap — so it is a spawn/teleport
    // edge only; Task 6 must keep the spawn outside the proxy. Do not raise
    // the iteration count here without revisiting the ruling and the test that
    // documents this behavior (test_anim.cpp, "Valus' posed palette drives the
    // capsule proxy and each leg pushes the player out", the between-feet
    // block at the end).
    for (int iteration = 0; iteration < kResolveIterations; ++iteration) {
        for (const Capsule& capsule : capsules) {
            if (!is_finite(capsule.a) || !is_finite(capsule.b) || !std::isfinite(capsule.r)) {
                continue;  // a malformed proxy never writes NaN into the player
            }
            const float radius = std::max(capsule.r, 0.0f);

            // Vertical gating: a capsule entirely above the player's head or
            // below its feet is walk-under/walk-over, not a wall.
            const float low = std::min(capsule.a.y, capsule.b.y) - radius;
            const float high = std::max(capsule.a.y, capsule.b.y) + radius;
            if (high < feet || low > head) {
                continue;
            }

            // The horizontal silhouette: the XZ projection of the capsule's
            // axis, swept by the capsule radius. Find the closest point on it.
            const float dx = capsule.b.x - capsule.a.x;
            const float dz = capsule.b.z - capsule.a.z;
            const float length_squared = dx * dx + dz * dz;
            float t = 0.0f;
            if (length_squared > 0.0f) {
                t = ((player.position.x - capsule.a.x) * dx +
                     (player.position.z - capsule.a.z) * dz) /
                    length_squared;
                t = std::clamp(t, 0.0f, 1.0f);
            }
            const float axis_x = capsule.a.x + dx * t;
            const float axis_z = capsule.a.z + dz * t;

            const float min_distance = kPlayerCapsuleRadius + radius;
            const float offset_x = player.position.x - axis_x;
            const float offset_z = player.position.z - axis_z;
            const float distance = std::sqrt(offset_x * offset_x + offset_z * offset_z);
            if (!(distance < min_distance)) {
                continue;  // clear, or exactly on the boundary
            }

            float dir_x = 1.0f;
            float dir_z = 0.0f;
            if (distance > kAxisEpsilon) {
                dir_x = offset_x / distance;
                dir_z = offset_z / distance;
            } else if (length_squared > kAxisEpsilon) {
                // On the axis: step off perpendicular to it. (dz, -dx) is the
                // pinned side; either perpendicular is the same short exit.
                const float inverse_length = 1.0f / std::sqrt(length_squared);
                dir_x = dz * inverse_length;
                dir_z = -dx * inverse_length;
            }

            player.position.x = axis_x + dir_x * min_distance;
            player.position.z = axis_z + dir_z * min_distance;
            // Deliberately no y write: resolution is horizontal only.
        }
    }
}

void resolve_player_collision(PlayerState& player, const std::vector<Capsule>& capsules,
                              const Heightmap& heightmap) {
    resolve_player_collision(player, capsules);
    const float ground = heightmap_floor(heightmap, player.position, kFloorStep);
    if (std::isfinite(ground)) {
        player.position.y = ground;
    }
}

namespace {

struct CapsuleSpec {
    const char* from;
    const char* to;
    float radius;
};

// The pinned Valus proxy (see collision.h). Legs: hip -> knee -> ankle -> toe;
// torso: waist -> chest -> neck. Radii measured from the posed body mesh around
// each segment (dominant-weight vertices, p99/max: thigh 1.61/2.00, shin
// 1.49/1.56, foot 1.68/1.75; +0.35 m player at resolution).
constexpr CapsuleSpec kValusCapsules[] = {
    {"Bone_MOMO_L", "Bone_SUNE_L", 1.7f},       // left thigh
    {"Bone_SUNE_L", "Bone_TOE_L", 1.6f},        // left shin
    {"Bone_TOE_L", "Bone_TUMASAKI_L", 1.7f},    // left foot
    {"Bone_MOMO_R", "Bone_SUNE_R", 1.7f},       // right thigh
    {"Bone_SUNE_R", "Bone_TOE_R", 1.6f},        // right shin
    {"Bone_TOE_R", "Bone_TUMASAKI_R", 1.7f},    // right foot
    {"Bone_HARA", "Bone_MUNE", 2.0f},           // waist -> chest
    {"Bone_MUNE", "Bone_KUBI", 2.0f},           // chest -> neck
};

}  // namespace

std::vector<Capsule> entity_collision_capsules(const Scene& scene, std::size_t rigIndex,
                                               const std::vector<Mat4>& palette) {
    std::vector<Capsule> capsules;
    capsules.reserve(std::size(kValusCapsules));
    for (std::size_t segment = 0; segment < std::size(kValusCapsules); ++segment) {
        const CapsuleSpec& spec = kValusCapsules[segment];
        Vec3 from{};
        Vec3 to{};
        if (!posed_joint_world_position(scene, rigIndex, palette, spec.from, from) ||
            !posed_joint_world_position(scene, rigIndex, palette, spec.to, to)) {
            continue;  // not this rig's skeleton (or a missing joint)
        }
        Capsule capsule{from, to, spec.radius};
        capsule.rig = static_cast<int>(rigIndex);
        capsule.segment = static_cast<int>(segment);
        capsules.push_back(capsule);
    }
    return capsules;
}
