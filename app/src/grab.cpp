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
float segment_segment_distance(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2, float* tOut = nullptr) {
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
    if (tOut != nullptr) {
        *tOut = t;  // the parameter on the second segment
    }
    return length(sub(closest_1, closest_2));
}

}  // namespace

const ClimbBand& climb_band(std::span<const ClimbBand> bands, int id) {
    if (id < 0 || static_cast<std::size_t>(id) >= bands.size()) {
        static constexpr ClimbBand kNoBand{"", "", 0.0f, ClimbBandId::None, ClimbBandId::None,
                                           -1};
        return kNoBand;
    }
    return bands[static_cast<std::size_t>(id)];
}

const ClimbBand& climb_band(ClimbBandId id) { return climb_band(encounters::kValus.bands, id); }

const char* climb_band_name(std::span<const ClimbBand> bands, int id) {
    const char* name = climb_band(bands, id).name;
    return name != nullptr ? name : "none";
}

const char* climb_band_name(ClimbBandId id) {
    return climb_band_name(encounters::kValus.bands, id);
}

const char* weakpoint_material_name(std::span<const WeakpointDesc> weakpoints, int weakpoint) {
    if (weakpoint < 0 || static_cast<std::size_t>(weakpoint) >= weakpoints.size()) {
        return nullptr;
    }
    return weakpoints[static_cast<std::size_t>(weakpoint)].material;
}

const char* weakpoint_material_name(int weakpoint) {
    return weakpoint_material_name(encounters::kValus.weakpoints, weakpoint);
}

float band_radius_at(const ClimbBand& band, float t) {
    if (!(band.radiusB >= 0.0f)) {
        return band.radius;
    }
    const float u = std::isfinite(t) ? std::clamp(t, 0.0f, 1.0f) : 0.0f;
    return band.radius + (band.radiusB - band.radius) * u;
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
        add(axis, scale(radial, band_radius_at(band, u) + kPlayerCapsuleRadius));
    if (!is_finite(candidate) || !is_finite(radial)) {
        return false;
    }
    point = candidate;
    normal = radial;
    return true;
}

ClimbBandId nearest_climb_band(std::span<const ClimbBand> bands, const JointLookup& lookup,
                               Vec3 playerPos, float maxSurfaceDistance,
                               std::string_view playingClip, int frame) {
    if (!lookup || !is_finite(playerPos) || !std::isfinite(maxSurfaceDistance) ||
        maxSurfaceDistance < 0.0f) {
        return ClimbBandId::None;
    }
    // The player capsule axis: feet at playerPos, kPlayerCapsuleHeight above.
    const Vec3 playerTop{playerPos.x, playerPos.y + kPlayerCapsuleHeight, playerPos.z};
    if (!is_finite(playerTop)) {
        return ClimbBandId::None;
    }

    float best_surface_distance = std::numeric_limits<float>::infinity();
    ClimbBandId best = ClimbBandId::None;
    for (int i = 0; i < static_cast<int>(bands.size()); ++i) {
        const ClimbBand& band = bands[static_cast<std::size_t>(i)];
        Vec3 a{};
        Vec3 b{};
        if (!band_enabled(band, playingClip, frame) || !climb_band_segment(band, lookup, a, b)) {
            continue;
        }
        float bandT = 0.0f;
        float axis_distance = 0.0f;
        if (band.grabEntryLowEnd) {
            bandT = b.y < a.y ? 1.0f : 0.0f;  // band_low_end_t on the resolved segment
            axis_distance = player_capsule_distance(playerPos, bandT > 0.0f ? b : a);
        } else {
            axis_distance = segment_segment_distance(playerPos, playerTop, a, b, &bandT);
        }
        // Surface-to-surface: pull both capsule radii out of the centreline
        // distance (a tapered band's radius at the closest point). Keeping it
        // unclamped makes an overlapping band rank by how close its axis is,
        // which is the band the player stands on.
        const float surface_distance =
            axis_distance - kPlayerCapsuleRadius - band_radius_at(band, bandT);
        if (surface_distance < best_surface_distance) {
            best_surface_distance = surface_distance;
            best = i;
        }
    }

    if (best == ClimbBandId::None || best_surface_distance > maxSurfaceDistance) {
        return ClimbBandId::None;
    }
    return best;
}

ClimbBandId nearest_climb_band(const JointLookup& lookup, Vec3 playerPos,
                               float maxSurfaceDistance) {
    return nearest_climb_band(encounters::kValus.bands, lookup, playerPos, maxSurfaceDistance);
}

namespace {

float unit_or_zero(float v) { return std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f; }

float max_across(const ClimbBand& band, float t) {
    return band_radius_at(band, t) * kSurfaceWalkMaxAngleDeg * kDegToRad;
}

}  // namespace

bool surface_walk_point(const ClimbBand& band, const JointLookup& lookup, float t, float across,
                        Vec3 up, Vec3& point, Vec3& normal) {
    if (!std::isfinite(t) || !std::isfinite(across) || !is_finite(up) ||
        !std::isfinite(band.radius) || !(band.radius > 0.0f)) {
        return false;
    }
    Vec3 a{};
    Vec3 b{};
    if (!climb_band_segment(band, lookup, a, b)) {
        return false;
    }
    const Vec3 ab = sub(b, a);
    const Vec3 forward = scale(ab, 1.0f / length(ab));
    const Vec3 top_raw = sub(up, scale(forward, vec3_dot(up, forward)));
    const float top_length = length(top_raw);
    if (!std::isfinite(top_length) || top_length <= kAxisEpsilon) {
        return false;  // the band runs along `up`: it has no top to stand on
    }
    const Vec3 top = scale(top_raw, 1.0f / top_length);
    const Vec3 right = vec3_cross(forward, top);

    const float radius = band_radius_at(band, t);
    if (!(radius > 0.0f)) {
        return false;
    }
    const float limit = max_across(band, t);
    const float angle = std::clamp(across, -limit, limit) / radius;
    const Vec3 n = add(scale(top, std::cos(angle)), scale(right, std::sin(angle)));
    const Vec3 axis = add(a, scale(ab, std::clamp(t, 0.0f, 1.0f)));
    const Vec3 candidate = add(axis, scale(n, radius));
    if (!is_finite(candidate) || !is_finite(n)) {
        return false;
    }
    point = candidate;
    normal = n;
    return true;
}

bool surface_walk_step(const ClimbBand& band, const JointLookup& lookup, float along,
                       float lateral, float speed, float dt, float& t, float& across) {
    if (!std::isfinite(band.radius) || !(band.radius > 0.0f)) {
        return false;
    }
    Vec3 a{};
    Vec3 b{};
    if (!climb_band_segment(band, lookup, a, b)) {
        return false;
    }
    const float step = speed * dt;
    if (!std::isfinite(step) || !(dt > 0.0f)) {
        return true;
    }
    const float next_t = t + unit_or_zero(along) * step / length(sub(b, a));
    const float next_across = across + unit_or_zero(lateral) * step;
    if (std::isfinite(next_t)) {
        t = std::clamp(next_t, 0.0f, 1.0f);
    }
    const float limit = max_across(band, t);
    if (std::isfinite(next_across) && std::isfinite(limit)) {
        across = std::clamp(next_across, -limit, limit);
    }
    return true;
}

bool climb_attach_point(const ClimbBand& band, const JointLookup& lookup, float t, float across,
                        Vec3 hint, Vec3& point, Vec3& normal) {
    if (band_walkable(band, lookup)) {
        return surface_walk_point(band, lookup, t, across, Vec3{0.0f, 1.0f, 0.0f}, point, normal);
    }
    return climb_surface_point(band, lookup, t, hint, point, normal);
}

bool band_enabled(const ClimbBand& band, std::string_view playingClip, int frame) {
    if (band.enableClip == nullptr) {
        return true;
    }
    return playingClip == band.enableClip && frame >= band.enableFrom &&
           (band.enableTo < 0 || frame < band.enableTo);
}

bool band_walkable(const ClimbBand& band, const JointLookup& lookup) {
    Vec3 a{};
    Vec3 b{};
    if (!band.horizontal || !climb_band_segment(band, lookup, a, b)) {
        return false;
    }
    // The top is +Y less its along-axis part, so its y is 1 - f.y^2 over its
    // length sqrt(1 - f.y^2): top.y = sqrt(1 - f.y^2), the cosine of the pitch.
    const Vec3 ab = sub(b, a);
    const float fy = ab.y / length(ab);
    return std::sqrt(std::max(0.0f, 1.0f - fy * fy)) >=
           std::cos(kWalkableBandMaxSlopeDeg * kDegToRad);
}

float band_low_end_t(const ClimbBand& band, const JointLookup& lookup) {
    Vec3 a{};
    Vec3 b{};
    if (!climb_band_segment(band, lookup, a, b)) {
        return 0.0f;
    }
    return b.y < a.y ? 1.0f : 0.0f;
}
