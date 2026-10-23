// Quadratus's root motion and the hoof-hit rule (S11 Task 10b). The expected numbers
// are the recon's (s11-quadratus-recon/clips.md §1–§3), measured independently of the
// generated tables: the x sign from the planted hooves, the lift windows from every
// sole vertex. The hoof cases pose his glb from the library (SOTC_LIB, required).
#include <doctest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string_view>

#include "anim.h"
#include "asset_io.h"
#include "quadratus_motion.h"
#include "quadratus_root.h"

namespace {

constexpr float kWalkLength = 32.0f / 15.0f;
constexpr int kStandFrames = 66;  // boss_a_stand: 66 frames @ 15 fps
constexpr const char* kToe[] = {"Bone_TOE_L", "Bone_TOE_R", "Bone_B_TOE_L", "Bone_B_TOE_R"};

// test_anim.cpp's contract: SOTC_LIB is required; a relative value is repo-relative.
std::filesystem::path test_library() {
    const char* value = std::getenv("SOTC_LIB");
    if (value == nullptr || value[0] == '\0') {
        throw std::runtime_error("Set SOTC_LIB to the converted asset library.");
    }
    std::filesystem::path library = value;
    if (library.is_relative()) {
        library = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / library;
    }
    return library;
}

// His glb (rig 0), its soles, and each toe bone's world matrix at every stand frame.
struct Quadratus {
    Scene scene;
    HoofSoles soles;
    Mat4 toe[4][kStandFrames];
};

const Quadratus& quadratus() {
    static const Quadratus q = [] {
        Quadratus out;
        out.scene = load_scene(test_library(),
                               "models/shadow/characters/colossus_02_bull_a/_cmn/colossus_02_bull_a.glb");
        if (!quadratus_hoof_soles(out.scene, 0, out.soles)) {
            throw std::runtime_error("no hoof soles in his glb (is SOTC_LIB the s11-quadratus view?)");
        }
        const RigCpu& rig = out.scene.rigs.at(0);
        const AnimationCpu* stand = nullptr;
        for (const AnimationCpu& a : rig.animations) {
            if (a.name == "boss_a_stand") stand = &a;
        }
        for (int f = 0; f < kStandFrames; ++f) {
            const std::vector<Mat4> palette = animation_pose(out.scene, 0, *stand, f / 15.0f);
            for (int h = 0; h < 4; ++h) {
                if (!rig_bone_world(out.scene, 0, palette, kToe[h], out.toe[h][f])) {
                    throw std::runtime_error("no toe bone");
                }
            }
        }
        return out;
    }();
    return q;
}

// Frames of `stand` whose sole lift reaches `threshold`, and the first such frame.
int lifted_frames(Hoof hoof, float threshold, int* first = nullptr) {
    const Quadratus& q = quadratus();
    const int h = static_cast<int>(hoof);
    int n = 0;
    for (int f = 0; f < kStandFrames; ++f) {
        if (quadratus_hoof_lift(q.soles[h], q.toe[h][f], 0.0f) >= threshold) {
            if (n++ == 0 && first) *first = f;
        }
    }
    return n;
}

const WeakpointProxyLookup kNear = [](int, Vec3& c, float& r) {
    c = Vec3{0.0f, 1.0f, 0.0f};
    r = 2.6f;
    return true;
};
const WeakpointProxyLookup kFar = [](int, Vec3& c, float& r) {
    c = Vec3{10.0f, 1.0f, 0.0f};
    r = 2.6f;
    return true;
};

}  // namespace

TEST_CASE("quadratus root: the x sign is the planted-hoof one (glb x = -anb x)") {
    // walk sways x 0 -> +1.94 -> 0 (clips.md §1); with the anb's sign it would be -1.94.
    float maxX = 0.0f;
    for (int i = 0; i <= 32; ++i) {
        maxX = std::fmax(maxX, quadratus_root_delta("boss_a_walk", 0.0f, i / 15.0f).x);
    }
    CHECK(maxX == doctest::Approx(1.94f).epsilon(0.01));
    CHECK(quadratus_root_delta("boss_a_walk_start", 0.0f, 10.0f).x == doctest::Approx(-0.991f).epsilon(0.002));
    CHECK(quadratus_root_delta("boss_a_walk_end", 0.0f, 10.0f).x == doctest::Approx(0.991f).epsilon(0.002));
    CHECK(quadratus_root_delta("boss_a_walk_turn_r", 0.0f, 10.0f).x == doctest::Approx(-1.539f).epsilon(0.002));
    CHECK(quadratus_root_delta("boss_a_death", 0.0f, 10.0f).x == doctest::Approx(-9.727f).epsilon(0.001));
}

TEST_CASE("quadratus root: travel, loops wrap, one-shots clamp, y stays in the glb") {
    const Vec3 walk = quadratus_root_delta("boss_a_walk", 0.0f, kWalkLength);
    CHECK(walk.z == doctest::Approx(19.999f).epsilon(0.001));
    CHECK(walk.x == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(walk.y == 0.0f);
    // Two cycles, and a cycle that straddles the wrap.
    CHECK(quadratus_root_delta("boss_a_walk", 0.0f, 2.0f * kWalkLength).z ==
          doctest::Approx(39.998f).epsilon(0.001));
    const Vec3 straddle = quadratus_root_delta("boss_a_walk", 0.5f * kWalkLength, 1.5f * kWalkLength);
    CHECK(straddle.z == doctest::Approx(19.999f).epsilon(0.001));
    CHECK(std::fabs(straddle.x) < 0.01f);
    CHECK(quadratus_root_delta("boss_a_back", 0.0f, 2.0f).z == doctest::Approx(-10.0f).epsilon(0.001));
    // Reversed times answer the negated delta.
    CHECK(quadratus_root_delta("boss_a_walk", kWalkLength, 0.0f).z == doctest::Approx(-19.999f).epsilon(0.001));
    // A one-shot clamps at its end: walk_start 8.613 + walk_end 6.387 = the 15 m chain.
    CHECK(quadratus_root_delta("boss_a_walk_start", 0.0f, 100.0f).z +
              quadratus_root_delta("boss_a_walk_end", -5.0f, 100.0f).z ==
          doctest::Approx(15.0f).epsilon(0.001));
    // The down_b chain ends 2.756 m forward (clips.md §1 chain nets).
    CHECK(quadratus_root_delta("boss_a_down_b_l_start", 0.0f, 2.0f).z +
              quadratus_root_delta("boss_a_down_b_l_end", 0.0f, 3.667f).z ==
          doctest::Approx(2.756f).epsilon(0.002));
    // Between keys the root is interpolated: walk at 0.5 s is halfway between frame 7
    // (0.645, 4.472) and frame 8 (0.900, 5.234) (clips.json root_rel_per_frame_glb).
    const Vec3 mid = quadratus_root_delta("boss_a_walk", 0.0f, 0.5f);
    CHECK(mid.x == doctest::Approx(0.7725f).epsilon(0.01));
    CHECK(mid.z == doctest::Approx(4.853f).epsilon(0.002));
    // The attack landing holds clamp: held past their end, they add nothing.
    for (const char* hold : {"boss_a_attack_r_loop", "boss_a_attack_l_loop"}) {
        const Vec3 once = quadratus_root_delta(hold, 0.0f, 1.0f / 30.0f);
        const Vec3 held = quadratus_root_delta(hold, 0.0f, 5.0f);
        CHECK(held.z == once.z);
        CHECK(held.x == once.x);
        CHECK(quadratus_root_delta(hold, 1.0f / 30.0f, 5.0f).z == 0.0f);
    }
    // Per-frame speed sits in the measured 7.9–12.3 m/s band.
    const float v = quadratus_root_delta("boss_a_walk", 0.3f, 0.3f + 1.0f / 15.0f).z * 15.0f;
    CHECK(v > 7.8f);
    CHECK(v < 12.4f);
}

TEST_CASE("quadratus root: clips without a root series move nothing and are flagged") {
    for (const char* clip : {"boss_a_death_loop", "boss_a_add_fl_foot_damage", "boss_a_add_fr_foot_damage",
                             "boss_a_boot", "boss_a_standby", "no_such_clip"}) {
        CHECK_FALSE(quadratus_has_root(clip));
        const Vec3 d = quadratus_root_delta(clip, 0.0f, 5.0f);
        CHECK(d.x == 0.0f);
        CHECK(d.z == 0.0f);
    }
    CHECK(std::size(quadratus_root::kNoRoot) == 11);
    CHECK(quadratus_has_root("boss_a_walk"));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(quadratus_root_delta("boss_a_walk", 0.0f, nan).z == 0.0f);
}

TEST_CASE("quadratus turn yaw: _l is +yaw (left), _r is -yaw, at the clip end") {
    CHECK(quadratus_turn_yaw_deg("boss_a_turn_l") == doctest::Approx(45.0f).epsilon(0.001));
    CHECK(quadratus_turn_yaw_deg("boss_a_turn_r") == doctest::Approx(-45.0f).epsilon(0.001));
    CHECK(quadratus_turn_yaw_deg("boss_a_walk_turn_l") == doctest::Approx(45.04f).epsilon(0.001));
    CHECK(quadratus_turn_yaw_deg("boss_a_walk_turn_r") == doctest::Approx(-44.3f).epsilon(0.001));
    CHECK(quadratus_turn_yaw_deg("boss_a_walk") == 0.0f);
    // The turn is in the bones: turn_l's root nets nothing.
    CHECK(std::fabs(quadratus_root_delta("boss_a_turn_l", 0.0f, 10.0f).z) < 0.01f);
}

TEST_CASE("quadratus hoof soles: read from his glb, rest = clips.md's stand sole minima") {
    const Quadratus& q = quadratus();
    const float rest[4] = {0.007f, -0.034f, 0.072f, 0.024f};
    for (int h = 0; h < 4; ++h) {
        CHECK(q.soles[h].points.size() >= 233);
        CHECK(q.soles[h].restY == doctest::Approx(rest[h]).epsilon(0.001));
    }
    HoofSoles none;
    CHECK_FALSE(quadratus_hoof_soles(Scene{}, 0, none));
    CHECK(none[0].points.empty());
}

TEST_CASE("quadratus hoof lift: stand's measured windows (clips.md §3)") {
    const Quadratus& q = quadratus();
    CHECK(kHoofLiftMin == 0.5f);
    // Frame 0 is the rest: no lift.
    for (int h = 0; h < 4; ++h) {
        CHECK(std::fabs(quadratus_hoof_lift(q.soles[h], q.toe[h][0], 0.0f)) < 1e-3f);
    }
    // >= 0.5 m: fl 2.60–2.87 (5 frames), fr 3.40–3.87 (8), rl never, rr 2.20–2.53 (6 = 0.40 s).
    int first = -1;
    CHECK(lifted_frames(Hoof::FrontLeft, kHoofLiftMin, &first) == 5);
    CHECK(first == 39);
    CHECK(lifted_frames(Hoof::FrontRight, kHoofLiftMin, &first) == 8);
    CHECK(first == 51);
    CHECK(lifted_frames(Hoof::RearLeft, kHoofLiftMin) == 0);
    CHECK(lifted_frames(Hoof::RearRight, kHoofLiftMin, &first) == 6);
    CHECK(first == 33);
    // rr's window opens on a 0.556 m frame (clips.json): pinned against the series.
    CHECK(quadratus_hoof_lift(q.soles[3], q.toe[3][33], 0.0f) == doctest::Approx(0.556f).epsilon(0.01));
    // The 0.3 m fallback: fl 0.40, fr 0.60, rl 0, rr 0.60 s.
    CHECK(lifted_frames(Hoof::FrontLeft, 0.3f) == 6);
    CHECK(lifted_frames(Hoof::FrontRight, 0.3f) == 9);
    CHECK(lifted_frames(Hoof::RearLeft, 0.3f) == 0);
    CHECK(lifted_frames(Hoof::RearRight, 0.3f) == 9);
    // The ground under the hoof: raised ground, less lift.
    CHECK(quadratus_hoof_lift(q.soles[3], q.toe[3][35], 1.0f) ==
          doctest::Approx(quadratus_hoof_lift(q.soles[3], q.toe[3][35], 0.0f) - 1.0f));
    // A non-finite matrix or an empty sole answers NaN, never a lift.
    Mat4 inf = q.toe[3][35];
    inf.m[13] = std::numeric_limits<float>::infinity();
    CHECK(std::isnan(quadratus_hoof_lift(q.soles[3], inf, 0.0f)));
    CHECK(std::isnan(quadratus_hoof_lift(HoofSole{}, q.toe[3][35], 0.0f)));
}

TEST_CASE("quadratus hoof hit: lift, reach and the down/rising flag all required") {
    const Quadratus& q = quadratus();
    const HoofSole& rr = q.soles[3];
    const Mat4& up = q.toe[3][35];    // rr lifted
    const Mat4& down = q.toe[3][0];   // rr planted
    const Vec3 feet{0.0f, 0.0f, 0.0f};
    CHECK(quadratus_hoof_kneels(rr, up, 0.0f, false, feet, 3, kNear));
    CHECK_FALSE(quadratus_hoof_kneels(rr, down, 0.0f, false, feet, 3, kNear));
    CHECK_FALSE(quadratus_hoof_kneels(rr, up, 0.0f, false, feet, 3, kFar));
    CHECK_FALSE(quadratus_hoof_kneels(rr, up, 0.0f, false, feet, 3, {}));
    // rising_return plays an ordinary clip (stand) with the hoof up: the flag refuses it,
    // as it does the sprain chain, rising and death.
    CHECK_FALSE(quadratus_hoof_kneels(rr, up, 0.0f, true, feet, 3, kNear));
    Mat4 inf = up;
    inf.m[13] = std::numeric_limits<float>::infinity();
    CHECK_FALSE(quadratus_hoof_kneels(rr, inf, 0.0f, false, feet, 3, kNear));
    const WeakpointProxyLookup infRadius = [](int, Vec3& c, float& r) {
        c = Vec3{0.0f, 1.0f, 0.0f};
        r = std::numeric_limits<float>::infinity();
        return true;
    };
    CHECK_FALSE(quadratus_hoof_kneels(rr, up, 0.0f, false, feet, 3, infRadius));
    CHECK(std::string_view(quadratus_hoof_kneel_clip(Hoof::RearLeft)) == "boss_a_down_b_l_start");
    CHECK(std::string_view(quadratus_hoof_kneel_clip(Hoof::FrontRight)) == "boss_a_down_f_r_start");
}
