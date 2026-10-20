#include "grab.h"

#include "collision.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace {

// Below this the hint is considered to sit on the band's axis: the radial is
// then undefined, so the pinned +X fallback is used (the same threshold shape
// as collision.cpp's axis epsilon).
constexpr float kAxisEpsilon = 1e-5f;

bool is_finite(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

Vec3 add(Vec3 a, Vec3 b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(Vec3 a, Vec3 b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(Vec3 v, float s) { return Vec3{v.x * s, v.y * s, v.z * s}; }
float length(Vec3 v) { return std::sqrt(vec3_dot(v, v)); }

// Closest distance between segments [p1, q1] and [p2, q2] (Ericson,
// Real-Time Collision Detection, 5.1.9). All inputs are finite and both
// segments have non-zero length at the call site.
float segment_segment_distance(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2) {
    const Vec3 d1 = sub(q1, p1);
    const Vec3 d2 = sub(q2, p2);
    const Vec3 r = sub(p1, p2);
    const float a = vec3_dot(d1, d1);
    const float e = vec3_dot(d2, d2);
    const float f = vec3_dot(d2, r);

    constexpr float kDegenerate = 1e-12f;
    float s = 0.0f;
    float t = 0.0f;
    if (a <= kDegenerate) {
        s = 0.0f;
        t = std::clamp(f / e, 0.0f, 1.0f);
    } else if (e <= kDegenerate) {
        t = 0.0f;
        const float c = vec3_dot(d1, r);
        s = std::clamp(-c / a, 0.0f, 1.0f);
    } else {
        const float c = vec3_dot(d1, r);
        const float b = vec3_dot(d1, d2);
        const float denominator = a * e - b * b;
        if (denominator != 0.0f) {
            s = std::clamp((b * f - c * e) / denominator, 0.0f, 1.0f);
        } else {
            s = 0.0f;
        }
        t = (b * s + f) / e;
        if (t < 0.0f) {
            t = 0.0f;
            s = std::clamp(-c / a, 0.0f, 1.0f);
        } else if (t > 1.0f) {
            t = 1.0f;
            s = std::clamp((b - c) / a, 0.0f, 1.0f);
        }
    }

    const Vec3 closest_1 = add(p1, scale(d1, s));
    const Vec3 closest_2 = add(p2, scale(d2, t));
    return length(sub(closest_1, closest_2));
}

// The pinned S4 band table. Radii are the S4 capsule measurements; the chain
// order is foot -> shin -> thigh -> waist -> chest -> neck -> head, with the
// right arm hanging off the chest.
constexpr ClimbBand kClimbBands[] = {
    {"Bone_TUMASAKI_L", "Bone_TOE_L", 1.7f, ClimbBandId::LeftShin, ClimbBandId::Count, -1},
    {"Bone_TOE_L", "Bone_MOMO_L", 1.6f, ClimbBandId::LeftThigh, ClimbBandId::LeftFoot, -1},
    {"Bone_MOMO_L", "Bone_KOSI", 1.7f, ClimbBandId::Waist, ClimbBandId::LeftShin, -1},
    {"Bone_KOSI", "Bone_HARA", 2.0f, ClimbBandId::Chest, ClimbBandId::LeftThigh, -1},
    {"Bone_HARA", "Bone_MUNE", 2.0f, ClimbBandId::Neck, ClimbBandId::Waist, -1},
    {"Bone_MUNE", "Bone_KUBI", 1.4f, ClimbBandId::Head, ClimbBandId::Chest, -1},
    {"Bone_KUBI", "Bone_KAO", 1.2f, ClimbBandId::Count, ClimbBandId::Neck, 0},
    {"Bone_MUNE", "Bone_KATA_R", 1.5f, ClimbBandId::RightArm, ClimbBandId::Chest, -1},
    {"Bone_KATA_R", "Bone_UDE_R", 1.2f, ClimbBandId::Count, ClimbBandId::RightShoulder, 1},
};

static_assert(std::size(kClimbBands) == static_cast<std::size_t>(ClimbBandId::Count),
              "the climb band table must cover exactly the enum's bands");

}  // namespace

const ClimbBand& climb_band(ClimbBandId id) {
    const int index = static_cast<int>(id);
    if (index < 0 || index >= static_cast<int>(ClimbBandId::Count)) {
        static constexpr ClimbBand kNoBand{"", "", 0.0f, ClimbBandId::Count, ClimbBandId::Count,
                                           -1};
        return kNoBand;
    }
    return kClimbBands[index];
}

const char* climb_band_name(ClimbBandId id) {
    switch (id) {
        case ClimbBandId::LeftFoot:
            return "left_foot";
        case ClimbBandId::LeftShin:
            return "left_shin";
        case ClimbBandId::LeftThigh:
            return "left_thigh";
        case ClimbBandId::Waist:
            return "waist";
        case ClimbBandId::Chest:
            return "chest";
        case ClimbBandId::Neck:
            return "neck";
        case ClimbBandId::Head:
            return "head";
        case ClimbBandId::RightShoulder:
            return "right_shoulder";
        case ClimbBandId::RightArm:
            return "right_arm";
        case ClimbBandId::Count:
            break;
    }
    return "none";
}

bool climb_band_segment(const ClimbBand& band, const JointLookup& lookup, Vec3& a, Vec3& b) {
    if (!lookup || band.boneA == nullptr || band.boneB == nullptr) {
        return false;
    }
    Vec3 from{};
    Vec3 to{};
    if (!lookup(band.boneA, from) || !lookup(band.boneB, to)) {
        return false;
    }
    if (!is_finite(from) || !is_finite(to)) {
        return false;
    }
    const Vec3 delta = sub(to, from);
    if (vec3_dot(delta, delta) <= 0.0f) {
        return false;  // a zero-length band has no surface parameter to walk
    }
    a = from;
    b = to;
    return true;
}

bool climb_surface_point(const ClimbBand& band, const JointLookup& lookup, float t, Vec3 hint,
                         Vec3& point, Vec3& normal) {
    if (!std::isfinite(t) || !std::isfinite(band.radius) || band.radius < 0.0f ||
        !is_finite(hint)) {
        return false;
    }
    Vec3 a{};
    Vec3 b{};
    if (!climb_band_segment(band, lookup, a, b)) {
        return false;
    }

    const float u = std::clamp(t, 0.0f, 1.0f);
    const Vec3 axis = add(a, scale(sub(b, a), u));
    if (!is_finite(axis)) {
        return false;
    }

    // Horizontal radial from the axis towards the hint; +X when the hint sits
    // on the axis (or so close that the direction is numerical noise).
    const float dx = hint.x - axis.x;
    const float dz = hint.z - axis.z;
    const float distance_squared = dx * dx + dz * dz;
    if (!std::isfinite(distance_squared)) {
        return false;
    }
    Vec3 radial{1.0f, 0.0f, 0.0f};
    if (distance_squared > kAxisEpsilon * kAxisEpsilon) {
        const float inverse_distance = 1.0f / std::sqrt(distance_squared);
        radial = Vec3{dx * inverse_distance, 0.0f, dz * inverse_distance};
    }

    const Vec3 candidate =
        add(axis, scale(radial, band.radius + kPlayerCapsuleRadius));
    if (!is_finite(candidate) || !is_finite(radial)) {
        return false;
    }
    point = candidate;
    normal = radial;
    return true;
}

ClimbBandId nearest_climb_band(const JointLookup& lookup, Vec3 playerPos,
                               float maxSurfaceDistance) {
    if (!lookup || !is_finite(playerPos) || !std::isfinite(maxSurfaceDistance) ||
        maxSurfaceDistance < 0.0f) {
        return ClimbBandId::Count;
    }
    // The player capsule axis: feet at playerPos, kPlayerCapsuleHeight above.
    const Vec3 playerTop{playerPos.x, playerPos.y + kPlayerCapsuleHeight, playerPos.z};
    if (!is_finite(playerTop)) {
        return ClimbBandId::Count;
    }

    float best_surface_distance = std::numeric_limits<float>::infinity();
    ClimbBandId best = ClimbBandId::Count;
    for (int i = 0; i < static_cast<int>(ClimbBandId::Count); ++i) {
        const ClimbBand& band = climb_band(static_cast<ClimbBandId>(i));
        Vec3 a{};
        Vec3 b{};
        if (!climb_band_segment(band, lookup, a, b)) {
            continue;
        }
        const float axis_distance = segment_segment_distance(playerPos, playerTop, a, b);
        // Surface-to-surface: pull both capsule radii out of the centreline
        // distance. Keeping it unclamped makes an overlapping band rank by how
        // close its axis is, which is the band the player stands on.
        const float surface_distance = axis_distance - kPlayerCapsuleRadius - band.radius;
        if (surface_distance < best_surface_distance) {
            best_surface_distance = surface_distance;
            best = static_cast<ClimbBandId>(i);
        }
    }

    if (best == ClimbBandId::Count || best_surface_distance > maxSurfaceDistance) {
        return ClimbBandId::Count;
    }
    return best;
}
