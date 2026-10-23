#include "quadratus_motion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

#include "anim.h"
#include "quadratus_root.h"

namespace qr = quadratus_root;

namespace {

Vec3 apply(const Mat4& m, Vec3 p) {
    return Vec3{m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
                m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
                m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}

const qr::RootSeries* series(std::string_view clip) {
    for (const qr::RootSeries& r : qr::kRoots) {
        if (clip == r.clip) {
            return r.frames >= 2 ? &r : nullptr;  // a 1-frame series has no interval
        }
    }
    return nullptr;
}

// The root (x, z) at clip time t: a loop adds its cycle delta per whole cycle, a
// one-shot clamps; linear between frames.
Vec3 sample(const qr::RootSeries& r, float t) {
    const int last = r.frames - 1;
    const float length = static_cast<float>(last) / r.fps;
    float cycles = 0.0f;
    if (r.loop) {
        cycles = std::floor(t / length);
        t -= cycles * length;
    }
    const float u = std::clamp(t * r.fps, 0.0f, static_cast<float>(last));
    const int i = std::min(static_cast<int>(u), last - 1);
    const float a = u - static_cast<float>(i);
    const auto at = [&](int c) {
        return r.xyz[i][c] + (r.xyz[i + 1][c] - r.xyz[i][c]) * a + cycles * r.xyz[last][c];
    };
    return Vec3{at(0), 0.0f, at(2)};
}

}  // namespace

Vec3 quadratus_root_delta(std::string_view clip, float t0, float t1) {
    const qr::RootSeries* r = series(clip);
    if (!r || !std::isfinite(t0) || !std::isfinite(t1)) {
        return Vec3{0.0f, 0.0f, 0.0f};
    }
    const Vec3 a = sample(*r, t0);
    const Vec3 b = sample(*r, t1);
    return Vec3{b.x - a.x, 0.0f, b.z - a.z};
}

bool quadratus_has_root(std::string_view clip) { return series(clip) != nullptr; }

float quadratus_turn_yaw_deg(std::string_view clip) {
    for (const qr::TurnYaw& t : qr::kTurnYaw) {
        if (clip == t.clip) {
            return t.deg;
        }
    }
    return 0.0f;
}

bool quadratus_hoof_soles(const Scene& scene, std::size_t rigIndex, HoofSoles& out) {
    constexpr const char* kToe[] = {"Bone_TOE_L", "Bone_TOE_R", "Bone_B_TOE_L", "Bone_B_TOE_R"};
    constexpr const char* kMaterial[] = {"colossus_02_hoof_weakpoint_fl", "colossus_02_hoof_weakpoint_fr",
                                         "colossus_02_hoof_weakpoint_rl", "colossus_02_hoof_weakpoint_rr"};
    const RigCpu& rig = rig_at(scene, rigIndex);
    const auto stand = std::find_if(rig.animations.begin(), rig.animations.end(),
                                    [](const AnimationCpu& a) { return a.name == "boss_a_stand"; });
    if (stand == rig.animations.end()) {
        return false;
    }
    const std::vector<Mat4> palette = animation_pose(scene, rigIndex, *stand, 0.0f);
    HoofSoles soles;
    for (int h = 0; h < 4; ++h) {
        const auto& joints = rig.skin.joints;
        const auto toe = std::find_if(joints.begin(), joints.end(),
                                      [&](const JointCpu& j) { return j.name == kToe[h]; });
        if (toe == joints.end() || palette.size() != joints.size()) {
            return false;
        }
        const auto slot = static_cast<std::uint16_t>(toe - joints.begin());
        const Mat4& ibm = toe->inverseBind;
        const Mat4& pose = palette[slot];
        HoofSole& sole = soles[h];
        for (std::size_t m = rig.meshBegin; m < std::min(rig.meshEnd, scene.meshes.size()); ++m) {
            const MeshCpu& mesh = scene.meshes[m];
            if (mesh.materialIndex < 0 ||
                static_cast<std::size_t>(mesh.materialIndex) >= scene.materials.size() ||
                scene.materials[static_cast<std::size_t>(mesh.materialIndex)].name != kMaterial[h]) {
                continue;
            }
            for (const Vertex& v : mesh.vertices) {
                if (v.j[0] != slot || !(v.w[0] > 0.999f)) {
                    return false;
                }
                const Vec3 p{v.px, v.py, v.pz};
                sole.points.push_back(apply(ibm, p));
                const float y = apply(pose, p).y;
                if (sole.points.size() == 1 || y < sole.restY) {
                    sole.restY = y;
                }
            }
        }
        if (sole.points.empty() || !std::isfinite(sole.restY)) {
            return false;
        }
    }
    out = std::move(soles);
    return true;
}

float quadratus_hoof_lift(const HoofSole& sole, const Mat4& m, float groundY) {
    // Row 1 of the column-major matrix: the posed y of each sole point.
    float lowest = std::numeric_limits<float>::quiet_NaN();
    for (std::size_t k = 0; k < sole.points.size(); ++k) {
        const Vec3 p = sole.points[k];
        const float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
        if (k == 0 || y < lowest) {
            lowest = y;
        }
    }
    const float lift = lowest - groundY - sole.restY;
    return std::isfinite(lift) ? lift : std::numeric_limits<float>::quiet_NaN();
}

const char* quadratus_hoof_kneel_clip(Hoof hoof) {
    constexpr const char* kKneel[] = {"boss_a_down_f_l_start", "boss_a_down_f_r_start",
                                      "boss_a_down_b_l_start", "boss_a_down_b_r_start"};
    return kKneel[static_cast<int>(hoof)];
}

bool quadratus_hoof_kneels(const HoofSole& sole, const Mat4& toeWorld, float groundY,
                           bool downOrRising, Vec3 feet, int weakpoint,
                           const WeakpointProxyLookup& lookup, float liftMin) {
    return !downOrRising && quadratus_hoof_lift(sole, toeWorld, groundY) >= liftMin &&
           strike_reaches_shell(feet, weakpoint, lookup);
}
