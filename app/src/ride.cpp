#include "ride.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

#include "anim.h"
#include "ride_points.h"

namespace {

namespace rp = ride_points;

// A stalled or NaN frame must not poison the clocks; a negative dt never rewinds.
float non_negative_dt(float dt) { return (std::isfinite(dt) && dt > 0.0f) ? dt : 0.0f; }

constexpr const char* kStand1 = "horse_a_uma_stand1";
constexpr const char* kHorsePrefix = "horse_a_uma_";

const rp::RootZ* root_z(const std::string& clip) {
    for (const rp::RootZ& r : rp::kRootZ)
        if (clip == r.clip) return &r;
    return nullptr;
}

const rp::RiderXZ* rider_xz(const std::string& clip) {
    for (const rp::RiderXZ& r : rp::kRiderXZ)
        if (clip == r.clip) return &r;
    return nullptr;
}

// A non-loop clip's runtime duration, (frames - 1) / fps; 0 for a loop (never ends).
float clip_duration(const std::string& clip) {
    if (const rp::RootZ* r = root_z(clip)) return (r->frames - 1) / r->fps;
    if (const rp::RiderXZ* r = rider_xz(clip)) return (r->frames - 1) / r->fps;
    return 0.0f;
}

// What follows a non-loop Agro clip when it ends (design §6's chains).
const char* next_horse_clip(const std::string& clip) {
    static const char* const kNext[][2] = {
        {"walk_start", "walk"},      {"run_n_start", "run_n"},    {"run_start", "run"},
        {"run_from_run_n", "run_n"}, {"run_n_from_walk", "walk"}, {"walk_stop", "stand1"},
        {"run_n_stop", "stand1"},    {"run_stop1", "run_stop2"},  {"run_stop2", "run_stop3"},
        {"run_stop3", "stand1"},     {"back_start", "back"},      {"back_stop", "stand1"},
        {"neigh", "neigh_end"},      {"neigh_end", "stand1"},
    };
    for (const auto& n : kNext)
        if (clip == std::string(kHorsePrefix) + n[0]) return n[1];
    return nullptr;
}

std::string horse(const char* name) { return std::string(kHorsePrefix) + name; }

// The rider's paired clip: player_a_uma_<name>; stand1 pairs with stand1_player.
std::string rider_for(const std::string& horseClip) {
    if (horseClip == kStand1) return "player_a_uma_stand1_player";
    return "player_a_uma_" + horseClip.substr(std::strlen(kHorsePrefix));
}

// A switch's blend mark: `t` seconds into the 0.25 s blend, or done.
float blend_mark(float t) { return t < kClipBlendSeconds ? t : -1.0f; }

void advance_blend(float& blendFrom, float dt) {
    if (blendFrom >= 0.0f) blendFrom = blend_mark(blendFrom + dt);
}

void play(RideState& s, const std::string& horseClip, const std::string& riderClip,
          float t = 0.0f) {
    s.horseClip = horseClip;
    s.riderClip = riderClip;
    s.clipTime = t;
    s.blendFrom = blend_mark(t);
}

void play_horse(RideState& s, const char* name, float t = 0.0f) {
    const std::string h = horse(name);
    play(s, h, rider_for(h), t);
}

const char* side_suffix(Side side) { return side == Side::Left ? "l" : "r"; }

// The dismount side: the one mounted from if walkable, else the other, else none.
bool pick_side(Side preferred, const RideContext& ctx, Side& out) {
    const bool left = ctx.leftWalkable, right = ctx.rightWalkable;
    if (preferred == Side::Left ? left : right) {
        out = preferred;
        return true;
    }
    if (preferred == Side::Left ? right : left) {
        out = preferred == Side::Left ? Side::Right : Side::Left;
        return true;
    }
    return false;
}

void start_dismount(RideState& s, Side side) {
    s.phase = RidePhase::Dismounting;
    s.side = side;
    s.gait = Gait::Stand;
    s.dismountQueued = false;
    s.steer = 0.0f;
    play(s, kStand1, std::string("player_a_uma_stand_getoff_") + side_suffix(side) + "_start");
}

float yaw_rate(Gait g) {
    switch (g) {
        case Gait::Stand: return kYawRateStand;
        case Gait::Walk: return kYawRateWalk;
        case Gait::Trot: return kYawRateTrot;
        case Gait::Gallop: return kYawRateGallop;
        case Gait::Back: return kYawRateWalk;  // builder's call: Back turns at Walk's rate
    }
    return 0.0f;
}

float wrap_deg(float deg) {
    deg = std::fmod(deg + 180.0f, 360.0f);
    if (deg < 0.0f) deg += 360.0f;
    return deg - 180.0f;
}

// Linear interpolation of a per-frame [x, z] table at `t` seconds.
Vec3 sample_xz(const rp::RiderXZ& r, float t) {
    const float u = std::clamp(t * r.fps, 0.0f, static_cast<float>(r.frames - 1));
    const int i = std::min(static_cast<int>(u), r.frames - 2);
    const float f = u - static_cast<float>(i);
    return Vec3{r.xz[i][0] + (r.xz[i + 1][0] - r.xz[i][0]) * f, 0.0f,
                r.xz[i][1] + (r.xz[i + 1][1] - r.xz[i][1]) * f};
}

// The seated rider's root (`getoff_l_start` frame 0, = `stand1_player`'s root).
Vec3 seat() {
    return Vec3{rp::kXZ_stand_getoff_l_start[0][0], 0.0f, rp::kXZ_stand_getoff_l_start[0][1]};
}

void downshift(RideState& s) {
    switch (s.gait) {
        case Gait::Gallop:
            s.gait = Gait::Trot;
            play_horse(s, "run_from_run_n");
            break;
        case Gait::Trot:
            s.gait = Gait::Walk;
            play_horse(s, "run_n_from_walk");
            break;
        case Gait::Walk:
            s.gait = Gait::Stand;
            play_horse(s, "walk_stop");
            break;
        default: break;
    }
}

void stop(RideState& s) {
    switch (s.gait) {
        case Gait::Gallop: play_horse(s, "run_stop1"); break;
        case Gait::Trot: play_horse(s, "run_n_stop"); break;
        case Gait::Walk: play_horse(s, "walk_stop"); break;
        default: return;
    }
    s.gait = Gait::Stand;
}

void upshift(RideState& s) {
    switch (s.gait) {
        case Gait::Stand:
            s.gait = Gait::Walk;
            play_horse(s, "walk_start");
            break;
        case Gait::Walk:
            s.gait = Gait::Trot;
            play_horse(s, "run_n_start");
            break;
        case Gait::Trot:
            s.gait = Gait::Gallop;
            play_horse(s, "run_start");
            break;
        default: break;  // Gallop is the top; Back ignores kicks
    }
}

// Ruling R8: kicks and dismount presses are ignored while these play.
bool in_transition(const std::string& clip) {
    static const char* const kTransitions[] = {"run_stop1",      "run_stop2", "run_stop3",
                                               "run_n_stop",     "walk_stop", "run_from_run_n",
                                               "run_n_from_walk"};
    for (const char* t : kTransitions)
        if (clip == std::string(kHorsePrefix) + t) return true;
    return false;
}

bool moving_gait(Gait g) { return g == Gait::Walk || g == Gait::Trot || g == Gait::Gallop; }

// The kick buffer (D9): ages advance and expire after the window; the cooldown runs down.
void age_kicks(RideState& s, float dt) {
    for (float& k : s.kicks)
        if (k >= 0.0f && (k += dt) >= kKickWindowSeconds) k = -1.0f;
    s.kickCooldown = std::max(0.0f, s.kickCooldown - dt);
}

void kick(RideState& s) {
    if (s.gait == Gait::Back || s.kickCooldown > 0.0f || in_transition(s.horseClip)) return;
    if (ride_speed(s) >= kKickRefuseFraction * rp::kSpeedGallop) {
        s.kickRefused = true;
        return;
    }
    for (float& k : s.kicks)
        if (k < 0.0f) {
            k = 0.0f;
            break;
        }
    if (std::all_of(s.kicks.begin(), s.kicks.end(), [](float k) { return k >= 0.0f; })) {
        s.kickCooldown = kKickCooldownSeconds;
        s.kicks = {-1.0f, -1.0f, -1.0f};
    }
    s.dismountQueued = false;
    upshift(s);
}

void rein(RideState& s, float dt, const RideInput& in) {
    if (in.reinHeld || in.reinPressed) {
        const float before = s.reinSeconds;
        s.reinSeconds = before < 0.0f ? 0.0f : before + dt;
        if (before < kReinHoldSeconds && s.reinSeconds >= kReinHoldSeconds && moving_gait(s.gait))
            stop(s);
    }
    if (!in.reinHeld && s.reinSeconds >= 0.0f) {  // released (or a press and release in one step)
        if (s.reinSeconds < kReinHoldSeconds && moving_gait(s.gait)) downshift(s);
        s.reinSeconds = -1.0f;
    }
    if (s.gait == Gait::Stand && s.horseClip == kStand1 && in.reinHeld) {
        s.gait = Gait::Back;
        play_horse(s, "back_start");
    } else if (s.gait == Gait::Back && !in.reinHeld) {
        s.gait = Gait::Stand;
        play_horse(s, "back_stop");
    }
}

void steer(RideState& s, float dt, float x) {
    x = std::isfinite(x) ? std::clamp(x, -1.0f, 1.0f) : 0.0f;
    const float mag = std::fabs(x);
    const float target = mag < kSteerDeadzone
                             ? 0.0f
                             : std::copysign((mag - kSteerDeadzone) / (1.0f - kSteerDeadzone), x);
    const float w = 1.0f - std::pow(1.0f - kSteerBlend, dt / kSteerFrameSeconds);
    s.steer = std::clamp(s.steer + (target - s.steer) * w, -1.0f, 1.0f);
    s.yawDeg = wrap_deg(s.yawDeg - s.steer * yaw_rate(s.gait) * dt);
}

// Riding: the clip clock and its chain; a long frame may cross several clips.
void advance_riding(RideState& s, float dt) {
    s.clipTime += dt;
    advance_blend(s.blendFrom, dt);
    for (float d = clip_duration(s.horseClip); d > 0.0f && s.clipTime >= d;
         d = clip_duration(s.horseClip)) {
        const char* next = next_horse_clip(s.horseClip);
        if (!next) break;
        play_horse(s, next, s.clipTime - d);
    }
}

}  // namespace

RideState step_ride(RideState s, float dt, const RideInput& in, const RideContext& ctx) {
    dt = non_negative_dt(dt);
    s.kickRefused = false;
    age_kicks(s, dt);

    switch (s.phase) {
        case RidePhase::OnFoot: {
            const Vec3 p = ctx.playerInHorse;
            if (in.mountPressed && std::fabs(p.x) <= kMountReach &&
                std::fabs(p.z) <= kMountHalfLength) {
                const float yaw = s.yawDeg;
                s = reset_ride();
                s.yawDeg = yaw;
                s.phase = RidePhase::Mounting;
                s.side = p.x < 0.0f ? Side::Left : Side::Right;
                play(s, kStand1, std::string("player_a_uma_stand_ride_") + side_suffix(s.side));
            }
            break;
        }
        case RidePhase::Mounting: {
            s.clipTime += dt;
            advance_blend(s.blendFrom, dt);
            const float d = clip_duration(s.riderClip);
            if (s.clipTime >= d) {
                s.phase = RidePhase::Riding;
                s.gait = Gait::Stand;
                play(s, kStand1, rider_for(kStand1), s.clipTime - d);
            }
            break;
        }
        case RidePhase::Dismounting: {
            s.clipTime += dt;
            advance_blend(s.blendFrom, dt);
            float d = clip_duration(s.riderClip);
            if (s.clipTime >= d && s.riderClip != "player_a_uma_stand_getoff_fall") {
                play(s, kStand1, "player_a_uma_stand_getoff_fall", s.clipTime - d);
                d = clip_duration(s.riderClip);
            }
            if (s.clipTime >= d) {  // on foot; the side stays for dismount_offset(side)
                const float yaw = s.yawDeg;
                const Side side = s.side;
                s = reset_ride();
                s.yawDeg = yaw;
                s.side = side;
            }
            break;
        }
        case RidePhase::Riding: {
            advance_riding(s, dt);
            Side side = s.side;
            if (s.dismountQueued && s.horseClip == kStand1) {
                s.dismountQueued = false;
                if (pick_side(s.side, ctx, side)) {
                    start_dismount(s, side);
                    break;
                }
            }
            steer(s, dt, in.steerX);
            if (in.mountPressed && (s.gait == Gait::Stand || s.gait == Gait::Walk) &&
                !in_transition(s.horseClip) && pick_side(s.side, ctx, side)) {
                if (s.horseClip == kStand1) {
                    start_dismount(s, side);
                    break;
                }
                if (s.gait == Gait::Walk) stop(s);
                s.dismountQueued = true;
            }
            if (in.kickPressed) kick(s);
            rein(s, dt, in);
            break;
        }
    }
    s.speed = ride_speed(s);
    return s;
}

RideState reset_ride() { return RideState{}; }

float ride_speed(const RideState& s) {
    return s.phase == RidePhase::Riding ? horse_clip_speed(s.horseClip, s.clipTime) : 0.0f;
}

float horse_clip_speed(const std::string& clip, float t) {
    if (clip == horse("walk")) return rp::kSpeedWalk;
    if (clip == horse("run_n")) return rp::kSpeedTrot;
    if (clip == horse("run")) return rp::kSpeedGallop;
    if (clip == horse("back")) return rp::kSpeedBack;
    const rp::RootZ* r = root_z(clip);
    if (!r) return 0.0f;  // stand1, unknown
    const float u =
        std::isfinite(t) ? std::clamp(t * r->fps, 0.0f, static_cast<float>(r->frames - 1)) : 0.0f;
    const int i = std::min(static_cast<int>(u), r->frames - 2);
    return (r->z[i + 1] - r->z[i]) * r->fps;
}

Vec3 rider_offset(const RideState& s) {
    if (s.phase != RidePhase::Mounting && s.phase != RidePhase::Dismounting) return Vec3{0, 0, 0};
    const rp::RiderXZ* r = rider_xz(s.riderClip);
    if (!r) return Vec3{0, 0, 0};
    Vec3 o = sample_xz(*r, s.clipTime);
    // Ruling R5: the fall is authored after the left start only; mirror it on the right.
    if (s.side == Side::Right && s.riderClip == "player_a_uma_stand_getoff_fall") o.x = -o.x;
    const Vec3 seated = seat();
    return Vec3{o.x - seated.x, 0.0f, o.z - seated.z};
}

Vec3 dismount_offset(Side side) {
    const auto& last = rp::kXZ_stand_getoff_fall[std::size(rp::kXZ_stand_getoff_fall) - 1];
    const Vec3 seated = seat();
    const float x = side == Side::Right ? -last[0] : last[0];
    return Vec3{x - seated.x, 0.0f, last[1] - seated.z};
}

const char* gait_name(Gait gait) {
    switch (gait) {
        case Gait::Stand: return "Stand";
        case Gait::Walk: return "Walk";
        case Gait::Trot: return "Trot";
        case Gait::Gallop: return "Gallop";
        case Gait::Back: return "Back";
    }
    return "?";
}

AgroState step_agro(AgroState a, float dt, bool whistled, float distanceToPlayer, bool progressed) {
    dt = non_negative_dt(dt);
    a.warp = false;
    if (a.clip.empty()) a.clip = kStand1;  // the first step: no outgoing pose to blend from
    a.clipTime += dt;
    advance_blend(a.blendFrom, dt);
    const auto play_agro = [&a](const std::string& clip, float t) {
        a.clip = clip;
        a.clipTime = t;
        a.blendFrom = blend_mark(t);
    };
    const auto approach_clip = [](float d) {
        return horse(d > kApproachGallopBeyond ? "run" : "run_n");
    };

    switch (a.phase) {
        case AgroPhase::Idle:
            if (whistled && distanceToPlayer <= kWhistleRange && distanceToPlayer > kApproachStop) {
                a.phase = AgroPhase::Approach;
                a.stuckSeconds = 0.0f;
                play_agro(approach_clip(distanceToPlayer), 0.0f);
            }
            break;
        case AgroPhase::Approach:
            if (distanceToPlayer <= kApproachStop) {
                a.phase = AgroPhase::Neigh;
                a.stuckSeconds = 0.0f;
                play_agro(horse("neigh"), 0.0f);
                break;
            }
            // Ruling R9: gallop above 42 m, trot below 38 m, else keep the clip.
            if (distanceToPlayer > kApproachGallopAbove && a.clip != horse("run"))
                play_agro(horse("run"), 0.0f);
            else if (distanceToPlayer < kApproachTrotBelow && a.clip != horse("run_n"))
                play_agro(horse("run_n"), 0.0f);
            a.stuckSeconds = progressed ? 0.0f : a.stuckSeconds + dt;
            if (a.stuckSeconds >= kStuckWarpSeconds) {
                a.warp = true;
                a.stuckSeconds = 0.0f;
            }
            break;
        case AgroPhase::Neigh:
            for (float d = clip_duration(a.clip); d > 0.0f && a.clipTime >= d;
                 d = clip_duration(a.clip)) {
                const char* next = next_horse_clip(a.clip);
                play_agro(horse(next), a.clipTime - d);
                if (a.clip == kStand1) {
                    a.phase = AgroPhase::Idle;
                    break;
                }
            }
            break;
    }
    return a;
}
