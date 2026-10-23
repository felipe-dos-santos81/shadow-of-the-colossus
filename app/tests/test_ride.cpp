// Riding Agro (S8 Task 5, design §6, D7, D9-D12). Pure: no scene, no window
// and no clock — the clip chains by name, the blend marks, the kick buffer, the
// rein, the mount/dismount sides, the rider's root tables, the steering, the
// measured speeds, the whistle approach and the reset.
#include <doctest.h>

#include <cmath>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "anim.h"
#include "ride.h"
#include "ride_points.h"

namespace {

constexpr float kDt = 1.0f / 60.0f;

const RideContext kNear{{-1.0f, 0.0f, 0.0f}, true, true};

RideInput none() { return {}; }
RideInput kick() {
    RideInput in;
    in.kickPressed = true;
    return in;
}
RideInput mount() {
    RideInput in;
    in.mountPressed = true;
    return in;
}
RideInput rein(bool pressed, bool held) {
    RideInput in;
    in.reinPressed = pressed;
    in.reinHeld = held;
    return in;
}
RideInput steer(float x) {
    RideInput in;
    in.steerX = x;
    return in;
}

// Steps `seconds` of `in` at kDt, appending every new horse clip to `seq`.
RideState run(RideState s, float seconds, const RideInput& in,
              std::vector<std::string>* seq = nullptr, const RideContext& ctx = kNear) {
    const int n = static_cast<int>(std::lround(seconds / kDt));
    for (int i = 0; i < n; ++i) {
        const std::string before = s.horseClip;
        s = step_ride(s, kDt, in, ctx);
        if (seq && s.horseClip != before) seq->push_back(s.horseClip);
    }
    return s;
}

// Riding at Stand on stand1, mounted from `x`'s side, blend finished.
RideState riding(float x = -1.0f) {
    RideState s = step_ride(reset_ride(), kDt, mount(), {{x, 0.0f, 0.0f}, true, true});
    REQUIRE(s.phase == RidePhase::Mounting);
    s = run(s, 2.0f, none());
    REQUIRE(s.phase == RidePhase::Riding);
    return s;
}

// Kick once and play until the gait's loop has run past its blend.
RideState up(RideState s) {
    s = step_ride(s, kDt, kick(), kNear);
    return run(s, 3.5f, none());
}
RideState at(Gait g) {
    RideState s = riding();
    for (int i = 0; i < static_cast<int>(g); ++i) s = up(s);
    REQUIRE(s.gait == g);
    return s;
}

// A tap: press, held one step, release.
RideState tap(RideState s) {
    s = step_ride(s, kDt, rein(true, true), kNear);
    return step_ride(s, kDt, rein(false, false), kNear);
}

float duration(const char* clip) {
    for (const auto& r : ride_points::kRootZ)
        if (std::string(r.clip) == clip) return (r.frames - 1) / r.fps;
    for (const auto& r : ride_points::kRiderXZ)
        if (std::string(r.clip) == clip) return (r.frames - 1) / r.fps;
    return -1.0f;
}

}  // namespace

TEST_CASE("ride_points.h carries placements.json's tables (R2)") {
    CHECK(ride_points::kSpeedWalk == 1.31f);
    CHECK(ride_points::kSpeedTrot == 5.29f);
    CHECK(ride_points::kSpeedGallop == 14.7f);
    CHECK(ride_points::kSpeedBack == -1.1f);
    CHECK(std::size(ride_points::kRootZ) == 14);
    CHECK(std::size(ride_points::kRiderXZ) == 5);
    CHECK(duration("horse_a_uma_run_start") == doctest::Approx(24.0f / 15.0f));
    CHECK(duration("player_a_uma_stand_getoff_fall") == doctest::Approx(7.0f / 16.1499f));
}

TEST_CASE("the mount picks stand_ride_l/r by the player's x sign, then rides at Stand") {
    RideState l = step_ride(reset_ride(), kDt, mount(), {{-1.0f, 0, 0.3f}, true, true});
    CHECK(l.phase == RidePhase::Mounting);
    CHECK(l.side == Side::Left);
    CHECK(l.riderClip == "player_a_uma_stand_ride_l");
    CHECK(l.horseClip == "horse_a_uma_stand1");
    CHECK(l.clipTime == 0.0f);
    CHECK(l.blendFrom == 0.0f);

    RideState r = step_ride(reset_ride(), kDt, mount(), {{1.2f, 0, -0.5f}, true, true});
    CHECK(r.side == Side::Right);
    CHECK(r.riderClip == "player_a_uma_stand_ride_r");

    l = run(l, duration("player_a_uma_stand_ride_l") + 0.1f, none());
    CHECK(l.phase == RidePhase::Riding);
    CHECK(l.gait == Gait::Stand);
    CHECK(l.horseClip == "horse_a_uma_stand1");
    CHECK(l.riderClip == "player_a_uma_stand1_player");
}

TEST_CASE("the mount is refused out of reach") {
    for (Vec3 p :
         {Vec3{1.7f, 0, 0}, Vec3{-1.61f, 0, 0}, Vec3{1.0f, 0, 0.61f}, Vec3{-1.0f, 0, -0.7f}}) {
        RideState s = step_ride(reset_ride(), kDt, mount(), {p, true, true});
        CHECK(s.phase == RidePhase::OnFoot);
        CHECK(s.horseClip.empty());
    }
    // Without the press nothing happens in reach either.
    CHECK(step_ride(reset_ride(), kDt, none(), kNear).phase == RidePhase::OnFoot);
}

TEST_CASE("kicks upshift one gait each through the start clips, rider paired") {
    std::vector<std::string> seq, rider;
    RideState s = riding();
    for (int i = 0; i < 3; ++i) {
        s = step_ride(s, kDt, kick(), kNear);
        seq.push_back(s.horseClip);
        rider.push_back(s.riderClip);
        s = run(s, 3.5f, none(), &seq);
    }
    CHECK(s.gait == Gait::Gallop);
    std::vector<std::string> w;
    for (const char* n : {"walk_start", "walk", "run_n_start", "run_n", "run_start", "run"})
        w.push_back(std::string("horse_a_uma_") + n);
    CHECK(seq == w);
    CHECK(rider == std::vector<std::string>{"player_a_uma_walk_start", "player_a_uma_run_n_start",
                                            "player_a_uma_run_start"});
    CHECK(s.riderClip == "player_a_uma_run");
}

TEST_CASE("the gait changes when the start clip begins") {
    RideState s = step_ride(riding(), kDt, kick(), kNear);
    CHECK(s.gait == Gait::Walk);
    CHECK(s.horseClip == "horse_a_uma_walk_start");
}

TEST_CASE("every clip switch sets blendFrom; it runs 0.25 s and clears") {
    RideState s = step_ride(riding(), kDt, kick(), kNear);
    CHECK(s.blendFrom == 0.0f);
    s = step_ride(s, 0.1f, none(), kNear);
    CHECK(s.blendFrom == doctest::Approx(0.1f));
    s = step_ride(s, 0.2f, none(), kNear);
    CHECK(s.blendFrom == -1.0f);
    // walk_start ends: the overshoot carries into walk and into the blend.
    const float left = duration("horse_a_uma_walk_start") - s.clipTime;
    s = step_ride(s, left + 0.05f, none(), kNear);
    CHECK(s.horseClip == "horse_a_uma_walk");
    CHECK(s.clipTime == doctest::Approx(0.05f).epsilon(1e-3));
    CHECK(s.blendFrom == doctest::Approx(0.05f).epsilon(1e-3));
}

TEST_CASE("blends start only at clip switches, never inside a loop") {
    RideState s = at(Gait::Walk);
    CHECK(s.blendFrom == -1.0f);
    for (int i = 0; i < 600; ++i) {  // 10 s of walk: the loop wraps many times
        s = step_ride(s, kDt, none(), kNear);
        REQUIRE(s.horseClip == "horse_a_uma_walk");
        REQUIRE(s.blendFrom == -1.0f);
    }
    // Every switch in a stop chain sets it.
    s = at(Gait::Gallop);
    s = run(s, 0.6f, rein(true, true));
    s = step_ride(s, kDt, rein(false, false), kNear);
    std::string last = s.horseClip;
    int switches = 0;
    for (int i = 0; i < 240; ++i) {
        s = step_ride(s, kDt, none(), kNear);
        if (s.horseClip != last) {
            ++switches;
            CHECK(s.blendFrom >= 0.0f);
            CHECK(s.blendFrom < kClipBlendSeconds);
            last = s.horseClip;
        }
    }
    CHECK(switches == 3);  // stop1 -> stop2 -> stop3 -> stand1
}

TEST_CASE("kick spam: three kicks inside 1.0 s start a 3.0 s cooldown") {
    RideState s = riding();
    s = step_ride(s, kDt, kick(), kNear);
    s = step_ride(s, 0.2f, kick(), kNear);
    CHECK(s.kickCooldown == 0.0f);
    s = step_ride(s, 0.2f, kick(), kNear);
    CHECK(s.gait == Gait::Gallop);
    CHECK(s.kickCooldown == doctest::Approx(kKickCooldownSeconds));
    CHECK(s.kicks == std::array<float, 3>{-1, -1, -1});
    // Down a gait, then a kick inside the cooldown is ignored...
    s = tap(s);
    REQUIRE(s.gait == Gait::Trot);
    s = step_ride(s, 1.0f, kick(), kNear);
    CHECK(s.gait == Gait::Trot);
    CHECK_FALSE(s.kickRefused);
    // ...and counts again once it ends.
    s = run(s, 2.1f, none());
    s = step_ride(s, kDt, kick(), kNear);
    CHECK(s.gait == Gait::Gallop);
}

TEST_CASE("kicks expire after 1.0 s: spaced kicks never start the cooldown") {
    RideState s = riding();
    s = step_ride(s, kDt, kick(), kNear);
    s = run(s, 1.1f, none());
    s = step_ride(s, kDt, kick(), kNear);
    s = run(s, 1.1f, none());
    s = step_ride(s, kDt, kick(), kNear);
    CHECK(s.gait == Gait::Gallop);
    CHECK(s.kickCooldown == 0.0f);
}

TEST_CASE("a kick at >= 0.9 x gallop speed is refused; the gait never exceeds Gallop") {
    RideState s = at(Gait::Gallop);
    REQUIRE(ride_speed(s) >= kKickRefuseFraction * ride_points::kSpeedGallop);
    s = step_ride(s, kDt, kick(), kNear);
    CHECK(s.kickRefused);
    CHECK(s.gait == Gait::Gallop);
    CHECK(s.horseClip == "horse_a_uma_run");
    CHECK(s.kicks == std::array<float, 3>{-1, -1, -1});
    s = step_ride(s, kDt, none(), kNear);
    CHECK_FALSE(s.kickRefused);
}

TEST_CASE("a tap of S downshifts one gait through the downshift clips") {
    std::vector<std::string> seq;
    RideState s = at(Gait::Gallop);
    s = tap(s);
    seq.push_back(s.horseClip);
    CHECK(s.gait == Gait::Trot);
    s = run(s, 3.0f, none(), &seq);
    s = tap(s);
    seq.push_back(s.horseClip);
    CHECK(s.gait == Gait::Walk);
    s = run(s, 3.0f, none(), &seq);
    s = tap(s);
    seq.push_back(s.horseClip);
    CHECK(s.gait == Gait::Stand);
    s = run(s, 3.0f, none(), &seq);
    std::vector<std::string> w;
    for (const char* n :
         {"run_from_run_n", "run_n", "run_n_from_walk", "walk", "walk_stop", "stand1"})
        w.push_back(std::string("horse_a_uma_") + n);
    CHECK(seq == w);
    CHECK(s.riderClip == "player_a_uma_stand1_player");
}

TEST_CASE("holding S 0.5 s stops through each gait's stop clips") {
    struct Case {
        Gait g;
        std::vector<const char*> chain;
    };
    for (const Case& c :
         {Case{Gait::Gallop, {"run_stop1", "run_stop2", "run_stop3", "stand1"}},
          Case{Gait::Trot, {"run_n_stop", "stand1"}}, Case{Gait::Walk, {"walk_stop", "stand1"}}}) {
        CAPTURE(gait_name(c.g));
        RideState s = at(c.g);
        s = step_ride(s, kDt, rein(true, true), kNear);
        s = run(s, 0.45f, rein(false, true));
        CHECK(s.gait == c.g);  // not yet: 0.45 s
        CHECK(s.horseClip != "horse_a_uma_" + std::string(c.chain[0]));
        std::vector<std::string> seq;
        s = run(s, 0.1f, rein(false, true), &seq);
        CHECK(s.gait == Gait::Stand);
        s = step_ride(s, kDt, rein(false, false), kNear);  // release: no extra downshift
        s = run(s, 3.5f, none(), &seq);
        std::vector<std::string> w;
        for (const char* n : c.chain) w.push_back(std::string("horse_a_uma_") + n);
        CHECK(seq == w);
    }
}

TEST_CASE("S at a standstill backs up while held: back_start, back, back_stop") {
    std::vector<std::string> seq;
    RideState s = riding();
    s = step_ride(s, kDt, rein(true, true), kNear);
    seq.push_back(s.horseClip);
    CHECK(s.gait == Gait::Back);
    s = run(s, 2.0f, rein(false, true), &seq);
    CHECK(ride_speed(s) == doctest::Approx(ride_points::kSpeedBack));
    s = step_ride(s, kDt, rein(false, false), kNear);
    seq.push_back(s.horseClip);
    CHECK(s.gait == Gait::Stand);
    s = run(s, 2.0f, none(), &seq);
    std::vector<std::string> w;
    for (const char* n : {"back_start", "back", "back_stop", "stand1"})
        w.push_back(std::string("horse_a_uma_") + n);
    CHECK(seq == w);
    CHECK(s.riderClip == "player_a_uma_stand1_player");
}

TEST_CASE("releasing S during back_start plays back_stop at once") {
    RideState s = step_ride(riding(), kDt, rein(true, true), kNear);
    s = step_ride(s, 0.1f, rein(false, true), kNear);
    REQUIRE(s.horseClip == "horse_a_uma_back_start");
    s = step_ride(s, kDt, rein(false, false), kNear);
    CHECK(s.horseClip == "horse_a_uma_back_stop");
    CHECK(s.blendFrom == 0.0f);
}

TEST_CASE("dismount: _start then _fall on both sides, then on foot") {
    for (Side side : {Side::Left, Side::Right}) {
        const char* l = side == Side::Left ? "l" : "r";
        CAPTURE(l);
        RideState s = riding(side == Side::Left ? -1.0f : 1.0f);
        REQUIRE(s.side == side);
        s = step_ride(s, kDt, mount(), kNear);
        CHECK(s.phase == RidePhase::Dismounting);
        CHECK(s.side == side);
        CHECK(s.riderClip == std::string("player_a_uma_stand_getoff_") + l + "_start");
        CHECK(s.horseClip == "horse_a_uma_stand1");
        CHECK(s.blendFrom == 0.0f);
        std::vector<std::string> rider{s.riderClip};
        for (int i = 0; i < 120 && s.phase == RidePhase::Dismounting; ++i) {
            s = step_ride(s, kDt, none(), kNear);
            if (!s.riderClip.empty() && s.riderClip != rider.back()) {
                rider.push_back(s.riderClip);
                CHECK(s.blendFrom >= 0.0f);
            }
        }
        CHECK(rider ==
              std::vector<std::string>{std::string("player_a_uma_stand_getoff_") + l + "_start",
                                       "player_a_uma_stand_getoff_fall"});
        CHECK(s.phase == RidePhase::OnFoot);
        CHECK(s.horseClip.empty());
        CHECK(s.riderClip.empty());
        // The completion step keeps the dismount's side for dismount_offset(side).
        CHECK(s.side == side);
        CHECK(dismount_offset(s.side).x * (side == Side::Left ? -1.0f : 1.0f) > 0.85f);
    }
}

TEST_CASE("dismount refused at trot and gallop") {
    for (Gait g : {Gait::Trot, Gait::Gallop}) {
        RideState s = step_ride(at(g), kDt, mount(), kNear);
        CHECK(s.phase == RidePhase::Riding);
        CHECK(s.gait == g);
        CHECK_FALSE(s.dismountQueued);
    }
}

TEST_CASE("dismount at Walk stops Agro first, then gets off at stand1") {
    RideState s = step_ride(at(Gait::Walk), kDt, mount(), kNear);
    CHECK(s.phase == RidePhase::Riding);
    CHECK(s.horseClip == "horse_a_uma_walk_stop");
    CHECK(s.dismountQueued);
    s = run(s, duration("horse_a_uma_walk_stop") + 0.05f, none());
    CHECK(s.phase == RidePhase::Dismounting);
    CHECK(s.riderClip == "player_a_uma_stand_getoff_l_start");
}

TEST_CASE("dismount picks the walkable side") {
    RideState s = riding(-1.0f);  // mounted on the left
    s = step_ride(s, kDt, mount(), {{0, 0, 0}, false, true});
    CHECK(s.phase == RidePhase::Dismounting);
    CHECK(s.side == Side::Right);
    CHECK(s.riderClip == "player_a_uma_stand_getoff_r_start");
    s = riding(1.0f);  // mounted on the right, both sides open: keep it
    s = step_ride(s, kDt, mount(), kNear);
    CHECK(s.side == Side::Right);
}

TEST_CASE("dismount refused with no walkable side") {
    RideState s = step_ride(riding(), kDt, mount(), {{0, 0, 0}, false, false});
    CHECK(s.phase == RidePhase::Riding);
    CHECK(s.gait == Gait::Stand);
    CHECK(s.horseClip == "horse_a_uma_stand1");
}

TEST_CASE("rider_offset follows rider_root_xz while mounting, seat-relative, 0 while riding") {
    const auto& t = ride_points::kXZ_stand_ride_l;
    const float seatZ = ride_points::kXZ_stand_getoff_l_start[0][1];
    RideState s = step_ride(reset_ride(), kDt, mount(), kNear);
    for (int k = 0; k < 26; k += 5) {
        RideState q = s;
        q.clipTime = k / 15.0f;
        const Vec3 o = rider_offset(q);
        CHECK(o.x == doctest::Approx(t[k][0]).epsilon(1e-4));
        CHECK(o.y == 0.0f);
        CHECK(o.z == doctest::Approx(t[k][1] - seatZ).epsilon(1e-4));
    }
    // Halfway between frames 3 and 4: interpolated.
    RideState q = s;
    q.clipTime = 3.5f / 15.0f;
    CHECK(rider_offset(q).x == doctest::Approx(0.5f * (t[3][0] + t[4][0])));
    // The left mount ends where riding starts (0); riding stays 0 at any gait.
    s = run(s, 2.0f, none());
    REQUIRE(s.phase == RidePhase::Riding);
    const Vec3 r = rider_offset(s);
    CHECK(r.x == 0.0f);
    CHECK(r.z == 0.0f);
    q.clipTime = duration("player_a_uma_stand_ride_l");
    CHECK(std::fabs(rider_offset(q).z) < 1e-4f);
    const Vec3 g = rider_offset(at(Gait::Gallop));
    CHECK((g.x == 0.0f && g.z == 0.0f));
    CHECK(rider_offset(reset_ride()).x == 0.0f);
}

TEST_CASE("rider_offset is continuous through both dismounts; the right fall mirrors x (R5)") {
    for (Side side : {Side::Left, Side::Right}) {
        const float sign = side == Side::Left ? -1.0f : 1.0f;
        CAPTURE(sign);
        RideState s = riding(sign);
        s = step_ride(s, kDt, mount(), kNear);
        REQUIRE(s.phase == RidePhase::Dismounting);
        Vec3 prev = rider_offset(s);
        CHECK(std::fabs(prev.x) < 1e-4f);  // continuous with Riding's 0
        CHECK(std::fabs(prev.z) < 1e-4f);
        float maxJump = 0.0f;
        while (true) {
            RideState n = step_ride(s, kDt, none(), kNear);
            if (n.phase != RidePhase::Dismounting) break;
            const Vec3 o = rider_offset(n);
            maxJump = std::fmax(maxJump, std::hypot(o.x - prev.x, o.z - prev.z));
            prev = o;
            s = n;
        }
        // The fastest table step is ~1 m/s (getoff_*_start), ~1.7 cm per 60 Hz frame;
        // a missing mirror would jump 1.335 m.
        CHECK(maxJump < 0.03f);
        CHECK(prev.x * sign > 0.85f);  // on the dismount side, near the fall's end
        const Vec3 end = dismount_offset(side);
        CHECK(end.x == doctest::Approx(sign * 0.9273f));
        CHECK(end.z == doctest::Approx(0.1327f - ride_points::kXZ_stand_getoff_l_start[0][1]));
    }
}

TEST_CASE("steering: deadzone 0.3, 1/0.7 rescale, 0.5 blend per 30 Hz frame") {
    RideState s = riding();
    CHECK(step_ride(s, kSteerFrameSeconds, steer(0.29f), kNear).steer == 0.0f);
    CHECK(step_ride(s, kSteerFrameSeconds, steer(-0.29f), kNear).steer == 0.0f);
    CHECK(step_ride(s, kSteerFrameSeconds, steer(1.0f), kNear).steer == doctest::Approx(0.5f));
    // 0.65 -> (0.65 - 0.3) / 0.7 = 0.5 -> half of it in one frame.
    CHECK(step_ride(s, kSteerFrameSeconds, steer(0.65f), kNear).steer == doctest::Approx(0.25f));
    CHECK(step_ride(s, kSteerFrameSeconds, steer(-1.0f), kNear).steer == doctest::Approx(-0.5f));
    // Two half frames blend as much as one frame.
    RideState h = step_ride(s, kSteerFrameSeconds / 2, steer(1.0f), kNear);
    h = step_ride(h, kSteerFrameSeconds / 2, steer(1.0f), kNear);
    CHECK(h.steer == doctest::Approx(0.5f));
    // Out-of-range input is clamped.
    RideState c = run(s, 1.0f, steer(3.0f));
    CHECK(c.steer == doctest::Approx(1.0f));
}

TEST_CASE("the yaw rate at full steer is the gait's: 90 / 60 / 60 / 45 deg/s, right = -yaw") {
    struct Case {
        Gait g;
        float rate;
    };
    for (const Case& c : {Case{Gait::Stand, kYawRateStand}, Case{Gait::Walk, kYawRateWalk},
                          Case{Gait::Trot, kYawRateTrot}, Case{Gait::Gallop, kYawRateGallop}}) {
        CAPTURE(gait_name(c.g));
        RideState s = at(c.g);
        s = run(s, 1.0f, steer(1.0f));  // the blend converges
        const float y0 = s.yawDeg;
        s = run(s, 1.0f, steer(1.0f));
        float d = s.yawDeg - y0;
        if (d > 180.0f) d -= 360.0f;
        if (d < -180.0f) d += 360.0f;
        CHECK(d == doctest::Approx(-c.rate).epsilon(1e-3));
        CHECK(s.gait == c.g);
    }
    CHECK(kYawRateStand == 90.0f);
    CHECK(kYawRateWalk == 60.0f);
    CHECK(kYawRateTrot == 60.0f);
    CHECK(kYawRateGallop == 45.0f);
}

TEST_CASE("the yaw stays within [-180, 180)") {
    RideState s = run(riding(), 10.0f, steer(-1.0f));
    CHECK(s.yawDeg >= -180.0f);
    CHECK(s.yawDeg < 180.0f);
}

TEST_CASE("ride_speed: the loops' measured speeds, root_z for the transition clips") {
    CHECK(ride_speed(riding()) == 0.0f);
    CHECK(ride_speed(at(Gait::Walk)) == doctest::Approx(1.31f));
    CHECK(ride_speed(at(Gait::Trot)) == doctest::Approx(5.29f));
    CHECK(ride_speed(at(Gait::Gallop)) == doctest::Approx(14.7f));
    CHECK(at(Gait::Gallop).speed == doctest::Approx(14.7f));
    // run_start between frames 7 and 8 (15 fps): the segment's speed.
    const auto& z = ride_points::kZ_run_start;
    CHECK(horse_clip_speed("horse_a_uma_run_start", 7.5f / 15.0f) ==
          doctest::Approx((z[8] - z[7]) * 15.0f));
    // walk_start at 15.9961 fps: frame 2.5.
    const auto& w = ride_points::kZ_walk_start;
    CHECK(horse_clip_speed("horse_a_uma_walk_start", 2.5f / 15.9961f) ==
          doctest::Approx((w[3] - w[2]) * 15.9961f));
    // Clamped to the clip's last segment; unknown clips do not move.
    CHECK(horse_clip_speed("horse_a_uma_run_start", 99.0f) ==
          doctest::Approx((z[24] - z[23]) * 15.0f));
    CHECK(horse_clip_speed("horse_a_uma_back", 0.3f) == doctest::Approx(-1.1f));
    CHECK(horse_clip_speed("nope", 0.3f) == 0.0f);
    // Mounting / on foot: no travel.
    CHECK(ride_speed(step_ride(reset_ride(), kDt, mount(), kNear)) == 0.0f);
    // The travel over run_start integrates to its root z (16.54 m).
    RideState s = step_ride(at(Gait::Trot), kDt, kick(), kNear);
    REQUIRE(s.horseClip == "horse_a_uma_run_start");
    float travel = 0.0f;
    while (s.horseClip == "horse_a_uma_run_start") {
        s = step_ride(s, 1.0f / 600.0f, none(), kNear);
        if (s.horseClip == "horse_a_uma_run_start") travel += s.speed / 600.0f;
    }
    CHECK(travel == doctest::Approx(16.5392f).epsilon(0.01));
}

TEST_CASE("a non-finite or negative dt advances nothing") {
    RideState s = step_ride(riding(), kDt, kick(), kNear);
    for (float bad :
         {-1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        RideState n = step_ride(s, bad, none(), kNear);
        CHECK(n.clipTime == s.clipTime);
        CHECK(n.horseClip == s.horseClip);
        CHECK(std::isfinite(n.yawDeg));
    }
}

TEST_CASE("reset_ride: on foot, standing, no clip, empty buffer (stateless, so any phase)") {
    const RideState s = reset_ride();
    CHECK(s.phase == RidePhase::OnFoot);
    CHECK(s.gait == Gait::Stand);
    CHECK(s.horseClip.empty());
    CHECK(s.riderClip.empty());
    CHECK(s.blendFrom == -1.0f);
    CHECK(s.kicks == std::array<float, 3>{-1, -1, -1});
    CHECK(s.kickCooldown == 0.0f);
    CHECK_FALSE(s.kickRefused);
    CHECK_FALSE(s.dismountQueued);
    CHECK(s.reinSeconds == -1.0f);
    CHECK(s.speed == 0.0f);
    // And a reset machine mounts again.
    CHECK(step_ride(reset_ride(), kDt, mount(), kNear).phase == RidePhase::Mounting);
}

TEST_CASE("gait names") {
    CHECK(std::string(gait_name(Gait::Stand)) == "Stand");
    CHECK(std::string(gait_name(Gait::Walk)) == "Walk");
    CHECK(std::string(gait_name(Gait::Trot)) == "Trot");
    CHECK(std::string(gait_name(Gait::Gallop)) == "Gallop");
    CHECK(std::string(gait_name(Gait::Back)) == "Back");
}

// ---------------------------------------------------------------- the whistle

TEST_CASE("the whistle: Approach (gallop beyond 40 m, trot within), Neigh at 3 m, then Idle") {
    AgroState a = step_agro({}, kDt, false, 60.0f, false);
    CHECK(a.phase == AgroPhase::Idle);
    CHECK(a.clip == "horse_a_uma_stand1");
    a = step_agro(a, kDt, true, 60.0f, false);
    CHECK(a.phase == AgroPhase::Approach);
    CHECK(a.clip == "horse_a_uma_run");
    CHECK(a.blendFrom == 0.0f);
    CHECK(horse_clip_speed(a.clip, a.clipTime) == doctest::Approx(14.7f));
    a = step_agro(a, kDt, false, 41.0f, true);
    CHECK(a.clip == "horse_a_uma_run");
    a = step_agro(a, kDt, false, 37.0f, true);
    CHECK(a.clip == "horse_a_uma_run_n");
    CHECK(a.blendFrom == 0.0f);
    a = step_agro(a, kDt, false, 3.0f, true);
    CHECK(a.phase == AgroPhase::Neigh);
    CHECK(a.clip == "horse_a_uma_neigh");
    std::vector<std::string> seq{a.clip};
    for (int i = 0; i < 240; ++i) {
        a = step_agro(a, kDt, false, 3.0f, false);
        if (a.clip != seq.back()) seq.push_back(a.clip);
    }
    CHECK(seq == std::vector<std::string>{"horse_a_uma_neigh", "horse_a_uma_neigh_end",
                                          "horse_a_uma_stand1"});
    CHECK(a.phase == AgroPhase::Idle);
    CHECK_FALSE(a.warp);
}

TEST_CASE("the whistle out of range (or already beside him) is ignored") {
    AgroState a = step_agro({}, kDt, true, 150.5f, false);
    CHECK(a.phase == AgroPhase::Idle);
    a = step_agro({}, kDt, true, 2.5f, false);
    CHECK(a.phase == AgroPhase::Idle);
    a = step_agro({}, kDt, true, std::numeric_limits<float>::quiet_NaN(), false);
    CHECK(a.phase == AgroPhase::Idle);
    CHECK(step_agro({}, kDt, true, 150.0f, false).phase == AgroPhase::Approach);
}

TEST_CASE("10 s without progress raises the warp once; progress resets the clock") {
    AgroState a = step_agro({}, kDt, true, 80.0f, false);
    a = step_agro(a, 9.0f, false, 80.0f, false);
    CHECK(a.stuckSeconds == doctest::Approx(9.0f));
    a = step_agro(a, kDt, false, 80.0f, true);
    CHECK(a.stuckSeconds == 0.0f);
    a = step_agro(a, 9.9f, false, 80.0f, false);
    CHECK_FALSE(a.warp);
    a = step_agro(a, 0.2f, false, 80.0f, false);
    CHECK(a.warp);
    CHECK(a.stuckSeconds == 0.0f);
    CHECK(a.phase == AgroPhase::Approach);
    a = step_agro(a, kDt, false, 6.0f, true);
    CHECK_FALSE(a.warp);
    CHECK(kWarpBehind == 6.0f);
}

TEST_CASE("a default AgroState is Idle with no warp (the reset)") {
    const AgroState a{};
    CHECK(a.phase == AgroPhase::Idle);
    CHECK(a.stuckSeconds == 0.0f);
    CHECK_FALSE(a.warp);
}

TEST_CASE("R9: the approach's trot/gallop switch has 38/42 m hysteresis") {
    AgroState a = step_agro({}, kDt, true, 60.0f, false);
    REQUIRE(a.clip == "horse_a_uma_run");
    a = step_agro(a, 0.5f, false, 39.0f, true);  // inside the band: keep the gallop
    CHECK(a.clip == "horse_a_uma_run");
    CHECK(a.blendFrom == -1.0f);
    // Blocked at ~40 m: the distance wobbles across 40 without a single switch.
    for (int i = 0; i < 120; ++i) {
        a = step_agro(a, kDt, false, (i % 2) ? 39.0f : 41.0f, false);
        REQUIRE(a.clip == "horse_a_uma_run");
    }
    a = step_agro(a, kDt, false, 37.9f, true);
    CHECK(a.clip == "horse_a_uma_run_n");
    CHECK(a.blendFrom == 0.0f);
    a = step_agro(a, 0.5f, false, 41.5f, true);  // back inside the band: keep the trot
    CHECK(a.clip == "horse_a_uma_run_n");
    a = step_agro(a, kDt, false, 42.1f, true);
    CHECK(a.clip == "horse_a_uma_run");
    // The whistle step itself still splits at 40 m.
    CHECK(step_agro({}, kDt, true, 39.0f, false).clip == "horse_a_uma_run_n");
    CHECK(step_agro({}, kDt, true, 41.0f, false).clip == "horse_a_uma_run");
}

TEST_CASE("R8: a kick during a stop clip does not change the clip chain") {
    RideState s = at(Gait::Gallop);
    s = run(s, 0.6f, rein(true, true));
    s = step_ride(s, kDt, rein(false, false), kNear);
    REQUIRE(s.horseClip == "horse_a_uma_run_stop1");
    std::vector<std::string> seq{s.horseClip};
    s = step_ride(s, kDt, kick(), kNear);
    CHECK(s.horseClip == "horse_a_uma_run_stop1");
    CHECK(s.gait == Gait::Stand);
    CHECK_FALSE(s.kickRefused);
    CHECK(s.kicks == std::array<float, 3>{-1, -1, -1});  // not counted
    for (int i = 0; i < 180; ++i) {
        // Kick only while the current stop clip still has more than a step left, so
        // no kick lands on the step that reaches stand1.
        const bool k = i % 10 == 0 && s.clipTime + 2 * kDt < duration(s.horseClip.c_str());
        s = step_ride(s, kDt, k ? kick() : none(), kNear);
        if (s.horseClip != seq.back()) seq.push_back(s.horseClip);
        if (s.horseClip == "horse_a_uma_stand1") break;
    }
    std::vector<std::string> w;
    for (const char* n : {"run_stop1", "run_stop2", "run_stop3", "stand1"})
        w.push_back(std::string("horse_a_uma_") + n);
    CHECK(seq == w);
    // Every R8 clip ignores the kick, the downshifts included.
    for (const char* n : {"run_n_stop", "walk_stop", "run_from_run_n", "run_n_from_walk"}) {
        CAPTURE(n);
        RideState q = at(Gait::Trot);
        q.horseClip = std::string("horse_a_uma_") + n;
        q.clipTime = 0.1f;
        const Gait g = q.gait;
        q = step_ride(q, kDt, kick(), kNear);
        CHECK(q.horseClip == std::string("horse_a_uma_") + n);
        CHECK(q.gait == g);
    }
    // Once the loop is reached the kick counts again.
    s = step_ride(s, kDt, kick(), kNear);
    CHECK(s.horseClip == "horse_a_uma_walk_start");
}

TEST_CASE("R8: a dismount pressed during a downshift clip is refused, not queued") {
    RideState s = tap(at(Gait::Trot));
    REQUIRE(s.horseClip == "horse_a_uma_run_n_from_walk");
    REQUIRE(s.gait == Gait::Walk);
    s = step_ride(s, kDt, mount(), kNear);
    CHECK(s.phase == RidePhase::Riding);
    CHECK(s.horseClip == "horse_a_uma_run_n_from_walk");
    CHECK_FALSE(s.dismountQueued);
    s = run(s, 3.0f, none());  // reaches the walk loop and stays riding
    CHECK(s.horseClip == "horse_a_uma_walk");
    CHECK(s.phase == RidePhase::Riding);
    // Pressed again at Walk, the normal path runs: stop, then get off.
    s = step_ride(s, kDt, mount(), kNear);
    CHECK(s.dismountQueued);
    s = run(s, 1.0f, none());
    CHECK(s.phase == RidePhase::Dismounting);
}

TEST_CASE("R11: a refused move reads as a held rein: each moving gait stops, never backs up") {
    struct Case {
        Gait g;
        std::vector<const char*> chain;
    };
    for (const Case& c :
         {Case{Gait::Gallop, {"run_stop1", "run_stop2", "run_stop3", "stand1"}},
          Case{Gait::Trot, {"run_n_stop", "stand1"}}, Case{Gait::Walk, {"walk_stop", "stand1"}}}) {
        CAPTURE(gait_name(c.g));
        RideState s = at(c.g);
        std::vector<std::string> seq;
        // Blocked every frame the machine would move her (the app's refusal, fed back):
        // the hold timer stops her after 0.5 s; at stand1 nothing is refused, nothing held.
        for (int i = 0; i < 300; ++i) {
            const bool blocked = s.speed != 0.0f;
            const std::string before = s.horseClip;
            s = step_ride(s, kDt, blocked_rein(none(), s, blocked), kNear);
            if (s.horseClip != before) seq.push_back(s.horseClip);
            CHECK(s.gait != Gait::Back);
        }
        std::vector<std::string> w;
        for (const char* n : c.chain) w.push_back(std::string("horse_a_uma_") + n);
        CHECK(seq == w);
        CHECK(s.gait == Gait::Stand);
        CHECK(s.reinSeconds < 0.0f);
    }
}

TEST_CASE("R11: the refusal is ignored on foot, at a standstill and backing up; S still works") {
    const RideInput fed = blocked_rein(none(), riding(), true);
    CHECK_FALSE(fed.reinHeld);  // Stand: no back-up from a wall
    CHECK_FALSE(blocked_rein(none(), reset_ride(), true).reinHeld);
    RideState back = step_ride(riding(), kDt, rein(true, true), kNear);
    REQUIRE(back.gait == Gait::Back);
    CHECK_FALSE(blocked_rein(none(), back, true).reinHeld);
    CHECK(blocked_rein(none(), at(Gait::Walk), true).reinHeld);
    CHECK_FALSE(blocked_rein(none(), at(Gait::Walk), false).reinHeld);
    CHECK(blocked_rein(rein(true, true), riding(), false).reinHeld);  // the key itself passes
    CHECK(blocked_rein(rein(true, true), riding(), false).reinPressed);
}

TEST_CASE("R12: the mount's offset lerps from the on-foot spot to rider_offset over the blend") {
    const Vec3 foot{-1.2f, 0.4f, 0.3f};  // y is ignored: the clip carries the height
    RideState s = step_ride(reset_ride(), kDt, mount(), {{-1.2f, 0.0f, 0.3f}, true, true});
    REQUIRE(s.phase == RidePhase::Mounting);
    REQUIRE(s.blendFrom == 0.0f);
    Vec3 o = mount_offset(s, foot);
    CHECK(o.x == doctest::Approx(foot.x));
    CHECK(o.y == 0.0f);
    CHECK(o.z == doctest::Approx(foot.z));
    // Halfway through the blend: the midpoint of the two.
    RideState q = s;
    q.clipTime = 0.5f * kClipBlendSeconds;
    q.blendFrom = 0.5f * kClipBlendSeconds;
    o = mount_offset(q, foot);
    const Vec3 r = rider_offset(q);
    CHECK(o.x == doctest::Approx(0.5f * (foot.x + r.x)));
    CHECK(o.z == doctest::Approx(0.5f * (foot.z + r.z)));
    // Continuous across the blend's end, then rider_offset alone.
    s = run(s, kClipBlendSeconds - kDt, none());
    REQUIRE(s.blendFrom >= 0.0f);
    const Vec3 last = mount_offset(s, foot);
    s = run(s, kDt, none());
    REQUIRE(s.blendFrom < 0.0f);
    const Vec3 after = mount_offset(s, foot);
    // No jump at the blend's end: one frame of the lerp (~0.77 m / 15 frames) at most.
    CHECK(std::hypot(after.x - last.x, after.z - last.z) < 0.06f);
    CHECK(after.x == rider_offset(s).x);
    CHECK(after.z == rider_offset(s).z);
    // Riding and dismounting are rider_offset's.
    const RideState g = at(Gait::Gallop);
    CHECK(mount_offset(g, foot).x == 0.0f);
    CHECK(mount_offset(g, foot).z == 0.0f);
}
