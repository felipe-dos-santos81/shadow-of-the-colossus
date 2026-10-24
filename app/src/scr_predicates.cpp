#include "scr_predicates.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <unordered_map>

namespace {

namespace k = scr_knight;

// The fns (eboot VAs; recon findings §3/§4, predicates.json).
enum : uint32_t {
    kTrue = 0x832600, kHit = 0x832620, kPlayerSurface = 0x832bb0, kBossMotionIs = 0x833090,
    kBossMotionStarted = 0x833100, kBossMotionEnded = 0x833180, kBossMotionInLayers = 0x833200,
    kPlayerMotionIs = 0x833360, kPlayerMotionStarted = 0x8333e0, kPlayerControlModeIs = 0x833500,
    kArrivedGoal = 0x8337e0, kLostTarget = 0x833800, kSensed = 0x833880, kNotSensed = 0x8338b0,
    kPlayerDistGe = 0x833920, kPlayerDistLe = 0x833960, kPlayerBearingIn = 0x833a70, kTimeInModeGe = 0x833d50,
    kGameFlag = 0x8346e0, kSignalArrowPresent = 0x8350c0, kModeJustEntered = 0x835220,
    kSwingSpaceClear = 0x8356b0, kKnightFlag0 = 0x6feae0,

    kGoalPlayer = 0x82e480, kGoalLastseenY = 0x82e570, kRouteReplan = 0x82e920, kGoalPlayerGround = 0x82e9c0,
    kRayCtrl = 0x82ef20, kIfCtrl = 0x82f370, kIfRun = 0x82f5b0, kRun = 0x82f5e0, kCheckpoint = 0x8302a0,
    kSetFlag = 0x830330, kLookIfMotion = 0x830360, kStageLoad = 0x831f20, kStageUnload = 0x831f50,
    kMoveParam = 0x8325a0, kKnightSet1 = 0x6fe580, kKnightSet2 = 0x6fe5a0, kKnightSet4 = 0x6fe5c0,
    kKnightShake = 0x6fe5e0,
};

constexpr int kCtrlFailure = 6;              // Failure_issue_knight_attack (data, the export)
constexpr float kSwingSample = 1.0f;         // §13.3 stand-in (authored): sample every 1 m
constexpr float kSwingRise = 1.0f;           // §13.3 stand-in (authored): a rise > 1.0 m blocks
constexpr float kTurnDeg = 30.0f;            // §13.1 FALLBACK (R1): turn past ±30°, else walk
constexpr float kSteerDeg = 3.0f;            // R1 decode: DAT_0128d284 = 0.05236 rad (steer and arrival)
constexpr float kMoveBackRadius = 75.0f;     // data: KNIGHT_MOVE_BACK r 75 (t44 default)
constexpr float kMoveBackHalfDeg = 45.0f;    // data: KNIGHT_MOVE_BACK 90° wide, yawed 180°

float argf(const std::vector<ScrArg>& args, const char* slot) {
    const ScrArg* a = scr_arg(args, slot);
    return a == nullptr ? 0.0f : static_cast<float>(a->num);
}

// A q-slot (u64 from "0x..."); 0xffffffffffffffff (-1) stays all ones.
uint64_t argu(const std::vector<ScrArg>& args, const char* slot) {
    const double v = scr_arg(args, slot)->num;
    return v >= 9.2e18 ? ~uint64_t{0} : static_cast<uint64_t>(v);
}

// "PLAYER:ph_motion_def[604]" -> 604; -1 without an index.
int bracket(const std::string& text) {
    const size_t open = text.rfind('[');
    return open == std::string::npos ? -1 : std::atoi(text.c_str() + open + 1);
}

int arg_clip(const std::vector<ScrArg>& args, const char* slot) { return scr_arg(args, slot)->clip; }
int arg_index(const std::vector<ScrArg>& args, const char* slot) { return bracket(scr_arg(args, slot)->text); }

Vec3 forward_of(const ScrWorld& w) {
    const float r = w.bossYawDeg * kDegToRad;
    return {std::sin(r), 0.0f, std::cos(r)};
}
Vec3 left_of(const ScrWorld& w) {  // glb +x at yaw 0: the model's left
    const float r = w.bossYawDeg * kDegToRad;
    return {std::cos(r), 0.0f, -std::sin(r)};
}

// The app bearing (degrees, + = his left) from his root to `p` (quadratus_fight's).
float bearing_to(const ScrWorld& w, const Vec3& p) {
    const float dx = p.x - w.bossPosition.x, dz = p.z - w.bossPosition.z;
    return std::remainder(std::atan2(dx, dz) / kDegToRad - w.bossYawDeg, 360.0f);
}

// The player's contact attribute (FUN_00716270) as the app knows it: bit 0 parented
// (ALWAYS:C32 tests mask 1 value 1), the boss region attr & 0x3f0 while attached, and
// the owner kind attr & 0x3c00000. §13.3 stand-in (authored): the arena ground is owner
// kind 0x800000, one of KNIGHT:C5's two battle-area kinds; EVENT1's 0x400000 is never set.
uint64_t contact_attr(const ScrWorld& w) {
    if (w.playerAttached) return 0x1u | (static_cast<uint64_t>(w.playerAttachedRegion) & 0x3f0u);
    return w.playerOnArenaGround ? 0x800000u : 0u;
}

// FUN_006fb790: value -1 matches any bit of mask, else (attr & mask) == value.
bool attr_matches(uint64_t attr, uint64_t mask, uint64_t value) {
    return value == ~uint64_t{0} ? (attr & mask) != 0 : (attr & mask) == value;
}

uint32_t region(const ScrWorld& w) { return static_cast<uint32_t>(contact_attr(w) & 0x3f0u); }

// --- Predicates (findings §3; predicates.json semantics, all binary unless noted) ---

bool p_true(const ScrClause&, const ScrWorld&, const ScrRuntime&) { return true; }
bool p_hit(const ScrClause&, const ScrWorld& w, const ScrRuntime&) { return w.hit; }
bool p_player_surface(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return attr_matches(contact_attr(w), argu(c.args, "q28"), argu(c.args, "q30"));
}
bool p_boss_motion_is(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return w.bossMotion == arg_clip(c.args, "h50");
}
bool p_boss_motion_started(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return w.bossMotionSwitched && w.bossMotion == arg_clip(c.args, "h50");
}
bool p_boss_motion_ended(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return w.bossMotionSwitched && w.bossPreviousMotion == arg_clip(c.args, "h50");
}
// Any active layer; S12 plays the base layer only (§13.7), so this is the base motion.
bool p_boss_motion_in_layers(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return w.bossMotion == arg_clip(c.args, "h50");
}
bool p_player_motion_is(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return w.playerPresent && w.playerMotion == arg_index(c.args, "h50");
}
bool p_player_motion_started(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return w.playerPresent && w.playerMotionStarted && w.playerMotion == arg_index(c.args, "h50");
}
bool p_player_control_mode_is(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return w.playerPresent && w.playerControlMode == arg_index(c.args, "h50");
}
bool p_arrived_goal(const ScrClause&, const ScrWorld&, const ScrRuntime& rt) { return rt.arrived; }
// FUN_0081d690 reads the follower's route state; the app's straight-line follower is
// lost without a kind (-1, as the binary) or, outside kind 10's live player, without a
// point goal (authored).
bool p_lost_target(const ScrClause&, const ScrWorld&, const ScrRuntime& rt) {
    return rt.locoKind < 0 || (rt.locoKind != 10 && !rt.hasGoal);
}
bool p_sensed(const ScrClause&, const ScrWorld& w, const ScrRuntime& rt) { return w.playerPresent && rt.sensed; }
bool p_not_sensed(const ScrClause&, const ScrWorld& w, const ScrRuntime& rt) {
    return !(w.playerPresent && rt.sensed);
}
bool p_player_dist_ge(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return !w.playerPresent || w.distanceHorizontal >= argf(c.args, "f1c");
}
bool p_player_dist_le(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return w.playerPresent && w.distanceHorizontal <= argf(c.args, "f1c");
}
// R5 decode: the game's bearing is + toward his RIGHT, the app's + toward his left.
bool p_player_bearing_in(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    const float game = -w.bearingDeg;
    return w.playerPresent && argf(c.args, "f1c") <= game && game <= argf(c.args, "f20");
}
bool p_time_in_mode_ge(const ScrClause& c, const ScrWorld&, const ScrRuntime& rt) {
    return rt.timer1 >= argf(c.args, "f1c");
}
bool p_game_flag(const ScrClause& c, const ScrWorld& w, const ScrRuntime& rt) {
    const size_t i = static_cast<size_t>(argf(c.args, "i10"));
    return w.flags.test(i) || rt.flags.test(i);
}
bool p_signal_arrow_present(const ScrClause&, const ScrWorld&, const ScrRuntime&) {
    return false;  // no kabura-ya (whistling arrow) in the app (findings §3: "new — false")
}
bool p_mode_just_entered(const ScrClause& c, const ScrWorld&, const ScrRuntime& rt) {
    return rt.transitioned && rt.current == scr_arg(c.args, "h50")->mode;
}
bool p_swing_space_clear(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return scr_swing_space_clear(w, argf(c.args, "f1c"), argf(c.args, "f20"), argf(c.args, "f24"));
}
bool p_knight_flag0(const ScrClause&, const ScrWorld&, const ScrRuntime& rt) { return (rt.bossBits & 1u) != 0; }

// --- Actions (findings §4; behaviour.md (e) for the shake) ---

void a_noop(const ScrAction&, ScrCtx&) {}  // stage_load/unload, checkpoint, route_replan
// The slam/stomp failure probe never reports class 0xc00 (§13.3, authored): slams never fail.
void a_ray_ctrl(const ScrAction&, ScrCtx&) {}

void a_if_run(const ScrAction& a, ScrCtx& ctx) {
    if (scr_eval(ctx.graph, ctx.registry, scr_arg(a.args, "h40")->cond, ctx.runtime, ctx.world)) {
        scr_run(ctx, scr_arg(a.args, "h50")->actions);
    }
}
void a_if_ctrl(const ScrAction& a, ScrCtx& ctx) {
    if (scr_eval(ctx.graph, ctx.registry, scr_arg(a.args, "h40")->cond, ctx.runtime, ctx.world)) {
        scr_request(ctx.out, scr_arg(a.args, "h60")->ctrl);
    }
}
void a_run(const ScrAction& a, ScrCtx& ctx) { scr_run(ctx, scr_arg(a.args, "h50")->actions); }

// While the boss plays h40, aim at the player (FUN_007feb10; a guess, predicates.json).
void a_look_if_motion(const ScrAction& a, ScrCtx& ctx) {
    if (ctx.world.bossMotion == arg_clip(a.args, "h40")) ctx.runtime.aimAtPlayer = true;
}

void set_goal(ScrRuntime& rt, const Vec3& p) {  // FUN_0081cfc0: a fixed point
    rt.goal = p;
    rt.hasGoal = true;
    rt.arrived = false;
}
void a_goal_player(const ScrAction& a, ScrCtx& ctx) {  // a snapshot, not the live player (R1)
    const Vec3 p = ctx.world.playerPosition;
    set_goal(ctx.runtime, {p.x, p.y + argf(a.args, "f10"), p.z});
}
void a_goal_player_ground(const ScrAction& a, ScrCtx& ctx) {
    const Vec3 p = ctx.world.playerGroundPosition;
    set_goal(ctx.runtime, {p.x, p.y + argf(a.args, "f10"), p.z});
}
void a_goal_lastseen_y(const ScrAction& a, ScrCtx& ctx) {
    const Vec3 p = ctx.runtime.lastKnown;
    set_goal(ctx.runtime, {p.x, argf(a.args, "f10"), p.z});
}
void a_move_param(const ScrAction& a, ScrCtx& ctx) { ctx.runtime.moveParam = static_cast<int>(argf(a.args, "i08")); }
void a_set_flag(const ScrAction& a, ScrCtx& ctx) {
    ctx.runtime.flags.set(static_cast<size_t>(argf(a.args, "i08")));
}

void a_knight_set1(const ScrAction&, ScrCtx& ctx) { ctx.runtime.bossBits |= 1u; }  // FUN_006fe580
void a_knight_set2(const ScrAction&, ScrCtx& ctx) {                                // FUN_006fe5a0
    if ((ctx.runtime.bossBits & 4u) == 0) ctx.runtime.bossBits |= 2u;
}
void a_knight_set4(const ScrAction&, ScrCtx& ctx) {  // FUN_006fe5c0
    if ((ctx.runtime.bossBits & 4u) == 0) ctx.runtime.bossBits |= 4u;
}

// FUN_006fe5e0(f10 cooldown, f14 stop hold, f18 walk limit) — binary (knight/dumps/k1-dec.txt):
// bit 2 (walking since a walk started) times a forced stop after f18 s, clearing bits
// 1|2 f14 s on once bit 1 (stopped) is set; bit 4 (a refuse clip ended) is a cooldown of
// f10 s, double rate on the head/neck, with no swing; otherwise the region picks a swing.
void a_knight_shake(const ScrAction& a, ScrCtx& ctx) {
    ScrRuntime& rt = ctx.runtime;
    const float cooldown = argf(a.args, "f10"), hold = argf(a.args, "f14"), limit = argf(a.args, "f18");
    float& walk = rt.bossTimer[0];  // W'+0x4c84
    float& cool = rt.bossTimer[1];  // W'+0x4c88
    const uint32_t r = region(ctx.world);
    const bool head = r == 0x10 || r == 0x20;
    if ((rt.bossBits & 2u) != 0) {
        // Verbatim: with bits 1 and 2 set and walk < limit, walk gains dt twice this frame.
        if (walk < limit) walk += rt.dt;
        if (walk >= limit) scr_request(ctx.out, k::kCtrlForcedStop);
        if ((rt.bossBits & 1u) != 0 && (walk += rt.dt) >= hold + limit) {
            rt.bossBits ^= 3u;
            walk = 0.0f;
        }
    }
    if ((rt.bossBits & 4u) != 0) {
        cool += rt.dt;
        if (head) cool += rt.dt;
        if (cool >= cooldown) {
            rt.bossBits ^= 4u;
            cool = 0.0f;
        }
        return;
    }
    switch (r) {
        case 0x10: case 0x20: scr_request(ctx.out, k::kCtrlRockHead); break;
        case 0x160: case 0x170: case 0x180: case 0x190: case 0x1a0: case 0x1c0: case 0x1e0: case 0x1f0: case 0x50:
            scr_request(ctx.out, k::kCtrlRockBody);
            break;
        case 0x60: case 0xe0: case 0xf0: case 0xa0: case 0xb0: case 0x40: scr_request(ctx.out, k::kCtrlRockArmR); break;
        case 0x80: case 0x100: case 0x110: case 0xc0: case 0xd0: scr_request(ctx.out, k::kCtrlRockArmL); break;
        default: break;
    }
}

// The new mode's slowstatus at a switch (findings §1 step 9; R1 "Arm"): the relposparam
// (W+0x4b00), the kind (W+0x4730; the kind entries keep the point goal, binary), +0c
// (W+0x3dc), and FUN_0085a960's attack arm (table, time since attack 0).
void enter_mode(int mode, ScrCtx& ctx) {
    const ScrLocomotion& l = ctx.graph.modes[mode].locomotion;
    ScrRuntime& rt = ctx.runtime;
    rt.relpos = l.perception;
    rt.locoKind = l.kind;
    rt.moveParam = l.p0c;
    // Guess: the attainrange's first non-zero float (+0x10: 12 m; 30 m in lose_sight) is the
    // arrival radius.
    rt.arriveRadius = l.attain.size() > 4 ? l.attain[4] : 0.0f;
    const int table = l.attack.empty() ? -1 : bracket(l.attack);
    if (table != rt.attackTable) {  // authored: a newly armed table starts unselected
        rt.attackRow = -1;
        rt.timeInRow = 0.0f;
    }
    rt.attackTable = table;
    rt.timeSinceAttack = 0.0f;
}

// --- Locomotion and the attack runner (R1) ---

// The slowleg's request for one frame toward `target` (R1 move codes): arrived -> brake
// when facing within 3°, else turn in place; a target in the MOVE_BACK sector -> back
// (rows with MOVE_BACK only; the undecoded sign test read as "behind him", authored);
// past ±30° -> turn in place, else walk (§13.1 FALLBACK); steer past 3° (R1 decode).
void gait(ScrRuntime& rt, const ScrWorld& w, const Vec3& target, bool arrived, float faceDeg, bool moveBack,
          ScrStepOut& out) {
    if (arrived) {
        rt.steerDeg = 0.0f;
        if (std::fabs(faceDeg) <= kSteerDeg) scr_request(out, k::kCtrlBrake);
        else scr_request(out, faceDeg > 0.0f ? k::kCtrlStopTurnLeft : k::kCtrlStopTurnRight);
        return;
    }
    const float b = bearing_to(w, target);
    rt.steerDeg = b;
    const float dist = std::hypot(target.x - w.bossPosition.x, target.z - w.bossPosition.z);
    if (moveBack && std::fabs(b) >= 180.0f - kMoveBackHalfDeg && dist <= kMoveBackRadius) {
        scr_request(out, k::kCtrlBackward);
        return;
    }
    // The game's yaw error is -b (R5): >= 0 turns right.
    if (std::fabs(b) > kTurnDeg) scr_request(out, b > 0.0f ? k::kCtrlStopTurnLeft : k::kCtrlStopTurnRight);
    else scr_request(out, k::kCtrlAccel);
    if (std::fabs(b) > kSteerDeg) scr_request(out, b > 0.0f ? k::kCtrlTurnLeft : k::kCtrlTurnRight);
}

// FUN_0085ab40 (R1), kind 10: the armed table's rows from kAttack[attackTable] to the terminator.
void attack_runner(const ScrGraph& g, ScrRuntime& rt, const ScrWorld& w, float dt, ScrStepOut& out) {
    if (rt.attackTable < 0) return;
    rt.timeSinceAttack += dt;
    rt.timeInRow += dt;
    const auto inside = [&](const char* name) { return w.playerInJudge && w.playerInJudge(name); };
    const int first = rt.attackTable;
    // onEnd (FUN_0085b910): a row's motion retired -> time since attack 0, cooldown +f4.
    if (w.bossMotionSwitched) {
        for (int i = first; k::kAttack[i].fix != nullptr; ++i) {
            if (w.bossPreviousMotion == scr_clip(g, k::kAttack[i].motion)) {
                rt.timeSinceAttack = 0.0f;
                rt.cooldown = k::kAttack[i].cooldownS;
            }
        }
    }
    // Select (a) FIX, (b) ATK, (c) the default row, when unselected or past +34.
    if (rt.attackRow < 0 || rt.timeInRow > k::kAttack[rt.attackRow].reselect) {
        int pick = -1;
        for (int i = first; pick < 0 && k::kAttack[i].fix != nullptr; ++i) {
            if (inside(k::kAttack[i].fix)) pick = i;
        }
        for (int i = first; pick < 0 && k::kAttack[i].fix != nullptr; ++i) {
            if (inside(k::kAttack[i].atk)) pick = i;
        }
        for (int i = first; pick < 0 && k::kAttack[i].fix != nullptr; ++i) {
            if (k::kAttack[i].isDefault) pick = i;
        }
        if (pick != rt.attackRow) rt.timeInRow = 0.0f;
        rt.attackRow = pick;
    }
    if (rt.attackRow < 0) return;  // Gaius's tables always carry a default row
    const k::AttackRow& row = k::kAttack[rt.attackRow];

    // The goal: the live player brought into the row's ATTAIN cylinder (the meaning is
    // R1's guess) — he heads for the point that puts the player at the attain centre.
    rt.arrived = inside(row.attain);
    const Vec3 f = forward_of(w), l = left_of(w), p = w.playerPosition;
    const Vec3 target{p.x - f.x * row.attainZ - l.x * row.attainX, p.y, p.z - f.z * row.attainZ - l.z * row.attainX};
    gait(rt, w, target, rt.arrived, bearing_to(w, p), true, out);

    // Issue: the cooldown holds it unless +f8 and arrived; the selected row's ATK, else
    // (root kind 1, FUN_0085ba60) the first row whose ATK holds the player.
    if (rt.cooldown > 0.0f && !(row.issueArrived && rt.arrived)) return;
    if (inside(row.atk)) {
        scr_request(out, row.issue);
        return;
    }
    for (int i = first; k::kAttack[i].fix != nullptr; ++i) {
        if (inside(k::kAttack[i].atk)) {
            scr_request(out, k::kAttack[i].issue);
            return;
        }
    }
}

// The walker's step 3 for Gaius's root kind 1 (the slowleg, every frame; R1).
void locomotion(const ScrGraph& g, ScrRuntime& rt, const ScrWorld& w, float dt, ScrStepOut& out) {
    rt.steerDeg = 0.0f;
    rt.cooldown = std::max(0.0f, rt.cooldown - dt);  // ticks in every kind (R1)
    switch (rt.locoKind) {
        case 0:  // modes 4/6: the point goal (a player snapshot) until arrived
            if (!rt.hasGoal) return;
            if (std::hypot(rt.goal.x - w.bossPosition.x, rt.goal.z - w.bossPosition.z) <= rt.arriveRadius) {
                rt.arrived = true;
            }
            gait(rt, w, rt.goal, rt.arrived, 0.0f, false, out);
            return;
        case 10: attack_runner(g, rt, w, dt, out); return;
        default:
            // -1: the slowleg is off (data: slowstatus +60 = 0 on every kind -1 row).
            // 1: no waypoint, so no request — mode 3 (R1 binary + data) and mode 12
            // (authored: no goal; the shake-off comes from its actions).
            return;
    }
}

// --- Control-def conditions of the base control-assign rows (R2 "Values") ---

enum class CDef { False, Mortality, Landing, NoGround, FailLegR, FailLegL, FailSlam };

const std::unordered_map<std::string_view, CDef>& cdefs() {
    static const std::unordered_map<std::string_view, CDef> table = {
        {"BOSS_CONTROL:control-def[44]", CDef::Mortality},        // HP <= 0 (op 0x76)
        {"PLAYER_CONTROL:control-def[2999]", CDef::Landing},      // his root on the ground
        {"PLAYER_CONTROL:control-def[3151]", CDef::NoGround},     // his root has no ground
        {"KNIGHT_CONTROL:control-def[20]", CDef::FailLegR},       // failure bit ∧ control-mode 7
        {"KNIGHT_CONTROL:control-def[23]", CDef::FailLegL},       // failure bit ∧ control-mode 8
        {"KNIGHT_CONTROL:control-def[26]", CDef::FailSlam},       // failure bit ∧ control-mode 6
        // Stick X rotations: the AI's stick X is its turn amount, so these fire with the
        // rows' own Turn bits — false here, the bit fires the row.
        {"BOSS_CONTROL:control-def[6]", CDef::False}, {"BOSS_CONTROL:control-def[10]", CDef::False},
        {"BOSS_CONTROL:control-def[14]", CDef::False}, {"BOSS_CONTROL:control-def[18]", CDef::False},
        // Pad buttons/holds/stick Y and the impossible pair: never true for the AI.
        {"COMMON_CONTROL:control-def[18]", CDef::False}, {"COMMON_CONTROL:control-def[42]", CDef::False},
        {"COMMON_CONTROL:control-def[60]", CDef::False}, {"COMMON_CONTROL:control-def[64]", CDef::False},
        {"COMMON_CONTROL:control-def[70]", CDef::False}, {"COMMON_CONTROL:control-def[123]", CDef::False},
        {"COMMON_CONTROL:control-def[127]", CDef::False}, {"COMMON_CONTROL:control-def[131]", CDef::False},
        {"COMMON_CONTROL:control-def[145]", CDef::False},
        {"KNIGHT_CONTROL:control-def[0]", CDef::False}, {"KNIGHT_CONTROL:control-def[2]", CDef::False},
        {"KNIGHT_CONTROL:control-def[5]", CDef::False}, {"KNIGHT_CONTROL:control-def[8]", CDef::False},
        {"KNIGHT_CONTROL:control-def[11]", CDef::False}, {"KNIGHT_CONTROL:control-def[14]", CDef::False},
        {"KNIGHT_CONTROL:control-def[17]", CDef::False}, {"KNIGHT_CONTROL:control-def[29]", CDef::False},
        {"KNIGHT_CONTROL:control-def[32]", CDef::False}, {"KNIGHT_CONTROL:control-def[35]", CDef::False},
        {"KNIGHT_CONTROL:control-def[38]", CDef::False},
    };
    return table;
}

// "BOSS_CONTROL:control-def[44] 'BOSS_MORTALITY_CONDITIONS'" -> "BOSS_CONTROL:control-def[44]"
std::string_view cdef_key(const std::string& when) {
    const size_t close = when.find(']');
    return close == std::string::npos ? std::string_view(when) : std::string_view(when).substr(0, close + 1);
}

bool requested(const std::vector<int>& ctrls, int bit) {
    return std::find(ctrls.begin(), ctrls.end(), bit) != ctrls.end();
}

bool cdef_holds(CDef d, const std::vector<int>& ctrls, const ScrWorld& w) {
    switch (d) {
        case CDef::Mortality: return !(w.hp > 0.0f);
        case CDef::Landing: return w.bossOnGround;
        case CDef::NoGround: return !w.bossOnGround;
        case CDef::FailLegR: return requested(ctrls, kCtrlFailure) && w.bossControlMode == 7;
        case CDef::FailLegL: return requested(ctrls, kCtrlFailure) && w.bossControlMode == 8;
        case CDef::FailSlam: return requested(ctrls, kCtrlFailure) && w.bossControlMode == 6;
        case CDef::False: return false;
    }
    return false;
}

// What each handler reads, checked once at load: "" when the record carries it.
std::string missing_args(uint32_t fn, const std::vector<ScrArg>& a) {
    const auto has = [&](const char* s) { return scr_arg(a, s) != nullptr; };
    const auto clip = [&](const char* s) { return has(s) && scr_arg(a, s)->clip >= 0; };
    const auto index = [&](const char* s) { return has(s) && bracket(scr_arg(a, s)->text) >= 0; };
    const auto flag = [&](const char* s) { return has(s) && scr_arg(a, s)->num >= 0.0 && scr_arg(a, s)->num < 512.0; };
    const auto handle = [&](const char* s, int ScrArg::*field) {  // a resolved ref or a null (true) handle
        return has(s) && (scr_arg(a, s)->null || scr_arg(a, s)->*field >= 0);
    };
    switch (fn) {
        case kBossMotionIs: case kBossMotionStarted: case kBossMotionEnded: case kBossMotionInLayers:
            return clip("h50") ? "" : "h50 (a clip)";
        case kPlayerMotionIs: case kPlayerMotionStarted: case kPlayerControlModeIs:
            return index("h50") ? "" : "h50 (an indexed handle)";
        case kPlayerDistGe: case kPlayerDistLe: case kTimeInModeGe: return has("f1c") ? "" : "f1c";
        case kPlayerBearingIn: return has("f1c") && has("f20") ? "" : "f1c/f20";
        case kSwingSpaceClear: return has("f1c") && has("f20") && has("f24") ? "" : "f1c/f20/f24";
        case kGameFlag: return flag("i10") ? "" : "i10 (a flag < 512)";
        case kModeJustEntered: return has("h50") && scr_arg(a, "h50")->mode >= 0 ? "" : "h50 (a mode)";
        case kPlayerSurface:  // no gameobject filter: the app has no contact object
            return has("q28") && has("q30") && (!has("h40") || scr_arg(a, "h40")->null) ? "" : "q28/q30, null h40";
        case kIfRun: return handle("h40", &ScrArg::cond) && handle("h50", &ScrArg::actions) ? "" : "h40/h50";
        case kIfCtrl:
            return handle("h40", &ScrArg::cond) && has("h60") && scr_arg(a, "h60")->ctrl >= 0 ? "" : "h40/h60";
        case kRun: return has("h50") && scr_arg(a, "h50")->actions >= 0 ? "" : "h50 (an action list)";
        case kLookIfMotion: return clip("h40") ? "" : "h40 (a clip)";
        case kGoalPlayer: case kGoalPlayerGround: case kGoalLastseenY: return has("f10") ? "" : "f10";
        case kMoveParam: return has("i08") ? "" : "i08";
        case kSetFlag: return flag("i08") ? "" : "i08 (a flag < 512)";
        case kKnightShake: return has("f10") && has("f14") && has("f18") ? "" : "f10/f14/f18";
        default: return "";
    }
}

}  // namespace

ScrRegistry scr_registry_knight() {
    ScrRegistry r;
    r.predicates = {
        {kTrue, p_true}, {kHit, p_hit}, {kPlayerSurface, p_player_surface}, {kBossMotionIs, p_boss_motion_is},
        {kBossMotionStarted, p_boss_motion_started}, {kBossMotionEnded, p_boss_motion_ended},
        {kBossMotionInLayers, p_boss_motion_in_layers}, {kPlayerMotionIs, p_player_motion_is},
        {kPlayerMotionStarted, p_player_motion_started}, {kPlayerControlModeIs, p_player_control_mode_is},
        {kArrivedGoal, p_arrived_goal}, {kLostTarget, p_lost_target}, {kSensed, p_sensed},
        {kNotSensed, p_not_sensed}, {kPlayerDistGe, p_player_dist_ge}, {kPlayerDistLe, p_player_dist_le},
        {kPlayerBearingIn, p_player_bearing_in}, {kTimeInModeGe, p_time_in_mode_ge}, {kGameFlag, p_game_flag},
        {kSignalArrowPresent, p_signal_arrow_present}, {kModeJustEntered, p_mode_just_entered},
        {kSwingSpaceClear, p_swing_space_clear}, {kKnightFlag0, p_knight_flag0},
    };
    r.actions = {
        {kIfRun, a_if_run}, {kIfCtrl, a_if_ctrl}, {kRun, a_run}, {kRayCtrl, a_ray_ctrl},
        {kLookIfMotion, a_look_if_motion}, {kGoalPlayer, a_goal_player}, {kGoalPlayerGround, a_goal_player_ground},
        {kGoalLastseenY, a_goal_lastseen_y}, {kRouteReplan, a_noop}, {kMoveParam, a_move_param},
        {kSetFlag, a_set_flag}, {kCheckpoint, a_noop}, {kStageLoad, a_noop}, {kStageUnload, a_noop},
        {kKnightShake, a_knight_shake}, {kKnightSet1, a_knight_set1}, {kKnightSet2, a_knight_set2},
        {kKnightSet4, a_knight_set4},
    };
    r.enterMode = enter_mode;
    return r;
}

bool scr_knight_check(const ScrGraph& graph, const std::vector<std::string>& judgeNames, std::string& error) {
    const auto fn_text = [](uint32_t fn) {
        char s[16];
        std::snprintf(s, sizeof s, "0x%x", fn);
        return std::string(s);
    };
    for (size_t i = 0; i < graph.controlModes.size(); ++i) {
        for (const ScrAssign& row : graph.controlModes[i].assign) {
            if (row.layer == "base" && cdefs().find(cdef_key(row.when)) == cdefs().end()) {
                error = "control_mode " + std::to_string(i) + ": control-def not decoded: " + row.when;
                return false;
            }
        }
    }
    for (const ScrMode& m : graph.modes) {
        const int table = m.locomotion.attack.empty() ? -1 : bracket(m.locomotion.attack);
        if (!m.locomotion.attack.empty() && table != 0 && table != 4) {
            error = "mode " + std::to_string(m.id) + ": attackdata not carried: " + m.locomotion.attack;
            return false;
        }
    }
    for (const scr_knight::AttackRow& row : scr_knight::kAttack) {
        if (row.fix == nullptr) continue;
        if (scr_clip(graph, row.motion) < 0) {
            error = std::string("attackdata motion is not a clip: ") + row.motion;
            return false;
        }
        for (const char* name : {row.fix, row.atk, row.attain}) {
            if (std::find(judgeNames.begin(), judgeNames.end(), name) == judgeNames.end()) {
                error = std::string("attackdata judge volume not in the sidecar: ") + name;
                return false;
            }
        }
    }
    for (const ScrCondition& c : graph.conditions) {
        for (const ScrClause& cl : c.clauses) {
            const std::string miss = missing_args(cl.fn, cl.args);
            if (!miss.empty()) {
                error = c.key + ": " + cl.pred + " (" + fn_text(cl.fn) + ") needs " + miss;
                return false;
            }
        }
    }
    for (const ScrActionList& l : graph.actions) {
        for (const ScrAction& a : l.records) {
            const std::string miss = missing_args(a.fn, a.args);
            if (!miss.empty()) {
                error = l.key + ": " + a.act + " (" + fn_text(a.fn) + ") needs " + miss;
                return false;
            }
        }
    }
    return true;
}

int scr_clip(const ScrGraph& graph, const std::string& name) {
    const auto it = std::find(graph.clipNames.begin(), graph.clipNames.end(), name);
    return it == graph.clipNames.end() ? -1 : static_cast<int>(it - graph.clipNames.begin());
}

void scr_world_relate(ScrWorld& w) {
    const Vec3 d{w.playerPosition.x - w.bossPosition.x, w.playerPosition.y - w.bossPosition.y,
                 w.playerPosition.z - w.bossPosition.z};
    w.distanceHorizontal = std::hypot(d.x, d.z);
    w.distance3d = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    w.bearingDeg = bearing_to(w, w.playerPosition);
}

// FUN_0083aae0: near = horizontal distance <= [0] and |dy| <= [1]; seen = within [2],
// |bearing| <= [3] (half FOV), pitch in [[5], [4]] and the sight ray clear. Measured from
// his root (the no-bone path; whether his relposparam names a bone is not decoded: guess).
// FUN_0083a6f0 latch (the [6]/[7]/[8] roles: findings §2's strong guess): sensed after [7]
// s seen or [8] s near, released after [6] s neither.
void scr_perceive(ScrRuntime& rt, const ScrWorld& w, float dt) {
    bool seen = false, near = false;
    const bool params = rt.relpos.size() >= 10;
    if (params && w.playerPresent) {
        const float* p = rt.relpos.data();
        const Vec3 d{w.playerPosition.x - w.bossPosition.x, w.playerPosition.y - w.bossPosition.y,
                     w.playerPosition.z - w.bossPosition.z};
        const float horiz = std::hypot(d.x, d.z);
        const float pitch = std::atan2(d.y, horiz) / kDegToRad;
        near = horiz <= p[0] && std::fabs(d.y) <= p[1];
        Vec3 hit;
        seen = std::hypot(horiz, d.y) <= p[2] && std::fabs(bearing_to(w, w.playerPosition)) <= p[3] && pitch <= p[4] &&
               pitch >= p[5] && !(w.ray && w.ray(w.bossPosition, w.playerPosition, hit));
    }
    rt.seenFor = seen ? rt.seenFor + dt : 0.0f;
    rt.nearFor = near ? rt.nearFor + dt : 0.0f;
    if (seen || near) {
        rt.lastKnown = w.playerPosition;
        rt.unsensedFor = 0.0f;
    } else if (rt.sensed) {
        rt.unsensedFor += dt;
        if (!params || rt.unsensedFor >= rt.relpos[6]) rt.sensed = false;
    }
    if (params && ((seen && rt.seenFor >= rt.relpos[7]) || (near && rt.nearFor >= rt.relpos[8]))) rt.sensed = true;
}

bool scr_swing_space_clear(const ScrWorld& w, float forward, float side, float scale) {
    if (!w.groundAt) return true;  // nothing to rise
    const float s = scale > 0.0f ? scale : 1.0f;  // binary: a non-positive scale falls back to the default
    const Vec3 f = forward_of(w), l = left_of(w), o = w.bossPosition;
    for (const float lateral : {0.0f, side, -side}) {  // centre, left, right (FUN_008356b0 ray order)
        const float ex = (f.x * forward + l.x * lateral) * s, ez = (f.z * forward + l.z * lateral) * s;
        const float len = std::hypot(ex, ez);
        for (float t = kSwingSample; len > 0.0f; t += kSwingSample) {
            const float u = std::min(t, len) / len;
            float h = 0.0f;
            if (w.groundAt(o.x + ex * u, o.z + ez * u, h) && h > o.y + kSwingRise) return false;
            if (t >= len) break;
        }
    }
    return true;
}

ScrStepOut scr_knight_step(const ScrGraph& graph, const ScrRegistry& registry, ScrRuntime& rt,
                           const ScrWorld& world, float dt) {
    rt.dt = dt;
    rt.aimAtPlayer = false;
    scr_perceive(rt, world, dt);            // step 2
    ScrStepOut pre;
    locomotion(graph, rt, world, dt, pre);  // step 3 (the slowleg; kind 10's attack runner)
    ScrStepOut out = scr_step(graph, registry, rt, world, dt);
    for (const int bit : out.controls) scr_request(pre, bit);
    out.controls = std::move(pre.controls);
    return out;
}

int scr_control_clip(const ScrGraph& graph, ScrRuntime& rt, const std::vector<int>& ctrls, const ScrWorld& world) {
    const int cm = world.bossControlMode;
    if (cm < 0 || cm >= static_cast<int>(graph.controlModes.size())) return -1;
    const ScrAssign* fired = nullptr;
    for (const ScrAssign& row : graph.controlModes[cm].assign) {
        if (row.layer != "base") continue;  // §13.7: no additive layer in S12
        const auto it = cdefs().find(cdef_key(row.when));  // scr_knight_check proved every base row's known
        const bool cond = it != cdefs().end() && cdef_holds(it->second, ctrls, world);
        if ((row.ctrl >= 0 && requested(ctrls, row.ctrl)) || cond) fired = &row;  // the last firing row wins
    }
    if (fired == nullptr) return -1;
    // The edge from the current motion; else an any-motion edge (null `from`: the always
    // table — fall, death; the additive table's null-`from` edges are reached only by the
    // dropped additive rows' orients).
    int to = -1, edge = -1;
    for (size_t i = 0; i < graph.motionGraph.size(); ++i) {
        const ScrMotionEdge& e = graph.motionGraph[i];
        if (e.orient != fired->orient) continue;
        if (e.from == world.bossMotion) {
            to = e.to;
            edge = static_cast<int>(i);
            break;
        }
        if (e.from < 0 && to < 0) {
            to = e.to;
            edge = static_cast<int>(i);
        }
    }
    // No edge for that orient from here, or a self-edge (turn_r + Right_rotation_Tachidomari
    // -> turn_r): the playing clip keeps playing.
    if (to < 0 || to == world.bossMotion) return -1;
    rt.controlMode = fired->nextControlMode;
    rt.controlEdge = edge;
    return to;
}

int scr_control_clip(const ScrGraph& graph, ScrRuntime& rt, int ctrl, const ScrWorld& world) {
    return scr_control_clip(graph, rt, std::vector<int>{ctrl}, world);
}
