#include "scr_library.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "scr_cdef.h"

namespace {

// The fns (eboot VAs; S12 recon findings §3/§4, predicates.json; S13 D5: predsem.py).
enum : uint32_t {
    kTrue = 0x832600, kHit = 0x832620, kPlayerSurface = 0x832bb0, kBossMotionIs = 0x833090,
    kBossMotionStarted = 0x833100, kBossMotionEnded = 0x833180, kBossMotionInLayers = 0x833200,
    kPlayerMotionIs = 0x833360, kPlayerMotionStarted = 0x8333e0, kPlayerControlModeIs = 0x833500,
    kArrivedGoal = 0x8337e0, kLostTarget = 0x833800, kSensed = 0x833880, kNotSensed = 0x8338b0,
    kPlayerDistGe = 0x833920, kPlayerDistLe = 0x833960, kPlayerBearingIn = 0x833a70, kTimeInModeGe = 0x833d50,
    kGameFlag = 0x8346e0, kSignalArrowPresent = 0x8350c0, kModeJustEntered = 0x835220,
    kSwingSpaceClear = 0x8356b0,
    // D5's batch.
    kHitKind = 0x832660, kHitRegion = 0x832710, kHpPctGe = 0x834730, kHpPctLe = 0x834770,
    kTimeSinceTransitionGe = 0x833d80, kTimeSinceAttackGe = 0x834700, kAngerGe = 0x82afd0, kAngerLe = 0x82b000,
    kModeIs = 0x8350d0, kBossControlModeIs = 0x8332f0, kPerceivedNow = 0x8338e0, kPerceivedBearingLe = 0x833a20,
    kPlayerDist3dLe = 0x8339e0, kPlayerRiding = 0x832e30, kBossAttrBits = 0x832f30, kCloakEquipped = 0x835290,
    kDemoCameraActive = 0x834fb0,

    kGoalPlayer = 0x82e480, kGoalLastseenY = 0x82e570, kRouteReplan = 0x82e920, kGoalPlayerGround = 0x82e9c0,
    kRayCtrl = 0x82ef20, kIfCtrl = 0x82f370, kIfRun = 0x82f5b0, kRun = 0x82f5e0, kCheckpoint = 0x8302a0,
    kSetFlag = 0x830330, kLookIfMotion = 0x830360, kStageLoad = 0x831f20, kStageUnload = 0x831f50,
    kMoveParam = 0x8325a0,
    // D5's batch.
    kAngerSet = 0x8313d0, kAngerAdd = 0x831400, kGoalLastseen = 0x82e500, kTurnToPlayer = 0x82fae0,
};

constexpr float kSwingSample = 1.0f;         // §13.3 stand-in (authored): sample every 1 m
constexpr float kSwingRise = 1.0f;           // §13.3 stand-in (authored): a rise > 1.0 m blocks
constexpr float kTurnDeg = 30.0f;            // §13.1 FALLBACK (R1): turn past ±30°, else walk
constexpr float kSteerDeg = 3.0f;            // R1 decode: DAT_0128d284 = 0.05236 rad (steer and arrival)
// The MOVE_BACK sector: Gaius's (data: KNIGHT_MOVE_BACK r 75, 90° wide, yawed 180°; t44 default),
// used for a slowstatus judge (+78) whose sector scr_move_back_bind did not bind (S13 Task 14).
constexpr float kMoveBackRadius = 75.0f;
constexpr float kMoveBackHalfDeg = 45.0f;
constexpr float kNoFacing = 1e-3f;           // authored: a face point this close (m) to his root has no bearing
constexpr float kAngerMax = 100.0f;          // binary DAT_0128dd90 (FUN_0082aef0's clamp)
constexpr float kPercent = 100.0f;           // binary DAT_0128e02c (hp_pct_*)

// A q-slot (u64 from "0x..."); 0xffffffffffffffff (-1) stays all ones.
uint64_t argu(const std::vector<ScrArg>& args, const char* slot) {
    const double v = scr_arg(args, slot)->num;
    return v >= 9.2e18 ? ~uint64_t{0} : static_cast<uint64_t>(v);
}

int arg_clip(const std::vector<ScrArg>& args, const char* slot) { return scr_arg(args, slot)->clip; }
int arg_index(const std::vector<ScrArg>& args, const char* slot) { return scr_handle_index(scr_arg(args, slot)->text); }

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
// on foot the ground word under him (S13 D8: `class << 48 | owner`). Gaius's glue sets
// owner 0x800000 on the arena ground (§13.3 stand-in, authored), one of KNIGHT:C5's two
// battle-area kinds; EVENT1's 0x400000 is never set.
uint64_t contact_attr(const ScrWorld& w) {
    if (w.playerAttached) return 0x1u | (static_cast<uint64_t>(w.playerAttachedRegion) & 0x3f0u);
    return w.playerGroundAttr;
}

// FUN_006fb790: value -1 matches any bit of mask, else (attr & mask) == value.
bool attr_matches(uint64_t attr, uint64_t mask, uint64_t value) {
    return value == ~uint64_t{0} ? (attr & mask) != 0 : (attr & mask) == value;
}

float hp_pct(const ScrWorld& w) { return w.hpMax > 0.0f ? kPercent * w.hp / w.hpMax : 0.0f; }

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
// Any active layer (actor+0x470): the base motion, or a layer the app plays (world.bossLayerPlaying;
// null: the base only, as S12 — Gaius has no additive layer).
bool p_boss_motion_in_layers(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    const int clip = arg_clip(c.args, "h50");
    return w.bossMotion == clip || (w.bossLayerPlaying && w.bossLayerPlaying(clip));
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
    return !w.playerPresent || w.distanceHorizontal >= scr_argf(c.args, "f1c");
}
bool p_player_dist_le(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return w.playerPresent && w.distanceHorizontal <= scr_argf(c.args, "f1c");
}
// R5 decode: the game's bearing is + toward his RIGHT, the app's + toward his left.
bool p_player_bearing_in(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    const float game = -w.bearingDeg;
    return w.playerPresent && scr_argf(c.args, "f1c") <= game && game <= scr_argf(c.args, "f20");
}
bool p_time_in_mode_ge(const ScrClause& c, const ScrWorld&, const ScrRuntime& rt) {
    return rt.timer1 >= scr_argf(c.args, "f1c");
}
bool p_game_flag(const ScrClause& c, const ScrWorld& w, const ScrRuntime& rt) {
    const size_t i = static_cast<size_t>(scr_argf(c.args, "i10"));
    return w.flags.test(i) || rt.flags.test(i);
}
bool p_signal_arrow_present(const ScrClause&, const ScrWorld&, const ScrRuntime&) {
    return false;  // gap: no kabura-ya (whistling arrow) in the app (findings §3: "new — false")
}
bool p_mode_just_entered(const ScrClause& c, const ScrWorld&, const ScrRuntime& rt) {
    return rt.transitioned && rt.current == scr_arg(c.args, "h50")->mode;
}
bool p_swing_space_clear(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return scr_swing_space_clear(w, scr_argf(c.args, "f1c"), scr_argf(c.args, "f20"), scr_argf(c.args, "f24"));
}

// D5 (binary, predsem.py; S13 graph findings §2).
// Hit this frame with kind rec[0x15]: 0 -> {0, 2, 4, 5}, 1 -> {1, 3}, else kind == i10.
bool p_hit_kind(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    if (!w.hit) return false;
    const int sel = static_cast<int>(scr_argf(c.args, "i10")), k = w.hitKind;
    if (sel == 0) return k == 0 || k == 2 || k == 4 || k == 5;
    if (sel == 1) return k == 1 || k == 3;
    return k == sel;
}
bool p_hit_region(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return w.hit && attr_matches(static_cast<uint64_t>(w.hitRegion), argu(c.args, "q28"), argu(c.args, "q30"));
}
bool p_hp_pct_ge(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) { return hp_pct(w) >= scr_argf(c.args, "f1c"); }
bool p_hp_pct_le(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) { return hp_pct(w) <= scr_argf(c.args, "f1c"); }
bool p_time_since_transition_ge(const ScrClause& c, const ScrWorld&, const ScrRuntime& rt) {
    return rt.timer2 >= scr_argf(c.args, "f1c");
}
bool p_time_since_attack_ge(const ScrClause& c, const ScrWorld&, const ScrRuntime& rt) {
    return rt.timeSinceAttack >= scr_argf(c.args, "f1c");
}
bool p_anger_ge(const ScrClause& c, const ScrWorld&, const ScrRuntime& rt) { return rt.anger >= scr_argf(c.args, "f1c"); }
bool p_anger_le(const ScrClause& c, const ScrWorld&, const ScrRuntime& rt) { return rt.anger <= scr_argf(c.args, "f1c"); }
bool p_mode_is(const ScrClause& c, const ScrWorld&, const ScrRuntime& rt) {
    return rt.current == scr_arg(c.args, "h50")->mode;
}
bool p_boss_control_mode_is(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return w.bossControlMode == arg_index(c.args, "h50");
}
bool p_perceived_now(const ScrClause&, const ScrWorld&, const ScrRuntime& rt) { return rt.seenNow || rt.nearNow; }
// |the perception bearing| (W+0x4af4; measured from his root, as scr_perceive) <= f1c.
bool p_perceived_bearing_le(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return w.playerPresent && std::fabs(w.bearingDeg) <= scr_argf(c.args, "f1c");
}
// False without a player, as player_dist_le (guess: predsem does not say).
bool p_player_dist3d_le(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return w.playerPresent && w.distance3d <= scr_argf(c.args, "f1c");
}
bool p_player_riding(const ScrClause&, const ScrWorld& w, const ScrRuntime&) { return w.playerPresent && w.playerRiding; }
// The boss's own attribute word (+0x8c0) as his ground under his root (S13 D8); the binary's
// transform-flag gate (+0x140) is not modelled.
bool p_boss_attr_bits(const ScrClause& c, const ScrWorld& w, const ScrRuntime&) {
    return attr_matches(w.bossGroundAttr, argu(c.args, "q28"), argu(c.args, "q30"));
}
bool p_false(const ScrClause&, const ScrWorld&, const ScrRuntime&) { return false; }  // gaps: cloak, demo camera

// --- Actions (findings §4) ---

void a_noop(const ScrAction&, ScrCtx&) {}  // stage_load/unload, checkpoint, route_replan
// The slam/stomp failure probe never reports class 0xc00 (§13.3, authored): slams never fail. A gap.
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
    set_goal(ctx.runtime, {p.x, p.y + scr_argf(a.args, "f10"), p.z});
}
void a_goal_player_ground(const ScrAction& a, ScrCtx& ctx) {
    const Vec3 p = ctx.world.playerGroundPosition;
    set_goal(ctx.runtime, {p.x, p.y + scr_argf(a.args, "f10"), p.z});
}
void a_goal_lastseen_y(const ScrAction& a, ScrCtx& ctx) {
    const Vec3 p = ctx.runtime.lastKnown;
    set_goal(ctx.runtime, {p.x, scr_argf(a.args, "f10"), p.z});
}
void a_goal_lastseen(const ScrAction& a, ScrCtx& ctx) {  // D5: the last-known point + (0, f10, 0)
    const Vec3 p = ctx.runtime.lastKnown;
    set_goal(ctx.runtime, {p.x, p.y + scr_argf(a.args, "f10"), p.z});
}
void a_move_param(const ScrAction& a, ScrCtx& ctx) { ctx.runtime.moveParam = static_cast<int>(scr_argf(a.args, "i08")); }
void a_set_flag(const ScrAction& a, ScrCtx& ctx) {
    ctx.runtime.flags.set(static_cast<size_t>(scr_argf(a.args, "i08")));
}
void a_anger_set(const ScrAction& a, ScrCtx& ctx) { ctx.runtime.anger = scr_argf(a.args, "f10"); }
void a_anger_add(const ScrAction& a, ScrCtx& ctx) { ctx.runtime.anger += scr_argf(a.args, "f10"); }
// The game's bearing (R5: minus the app's) past +f10 -> Stop_right_turn (426), past -f10 ->
// Stop_turn_left (427).
void a_turn_to_player(const ScrAction& a, ScrCtx& ctx) {
    const float game = -ctx.world.bearingDeg, limit = scr_argf(a.args, "f10");
    if (game > limit) scr_request(ctx.out, kScrCtrlStopTurnRight);
    else if (game < -limit) scr_request(ctx.out, kScrCtrlStopTurnLeft);
}

// --- Locomotion and the attack runner (R1) ---

// The MOVE_BACK sector behind him (radius 0: none).
struct BackSector {
    float radius = 0.0f, halfDeg = 0.0f;
};

// The slowleg's request for one frame toward `target` (R1 move codes): arrived -> brake
// when facing within 3°, else turn in place; a target in the MOVE_BACK sector -> back
// (rows with MOVE_BACK only; the undecoded sign test read as "behind him", authored);
// past ±30° -> turn in place, else walk (§13.1 FALLBACK); steer past 3° (R1 decode).
void gait(ScrRuntime& rt, const ScrWorld& w, const Vec3& target, bool arrived, float faceDeg, BackSector back,
          ScrStepOut& out) {
    if (arrived) {
        rt.steerDeg = 0.0f;
        if (std::fabs(faceDeg) <= kSteerDeg) scr_request(out, kScrCtrlBrake);
        else scr_request(out, faceDeg > 0.0f ? kScrCtrlStopTurnLeft : kScrCtrlStopTurnRight);
        return;
    }
    const float b = bearing_to(w, target);
    rt.steerDeg = b;
    const float dist = std::hypot(target.x - w.bossPosition.x, target.z - w.bossPosition.z);
    if (back.radius > 0.0f && std::fabs(b) >= 180.0f - back.halfDeg && dist <= back.radius) {
        scr_request(out, kScrCtrlBackward);
        return;
    }
    // The game's yaw error is -b (R5): >= 0 turns right.
    if (std::fabs(b) > kTurnDeg) scr_request(out, b > 0.0f ? kScrCtrlStopTurnLeft : kScrCtrlStopTurnRight);
    else scr_request(out, kScrCtrlAccel);
    if (std::fabs(b) > kSteerDeg) scr_request(out, b > 0.0f ? kScrCtrlTurnLeft : kScrCtrlTurnRight);
}

// The armed table's exported rows (R-I: no terminator), or null.
const std::vector<ScrAttackRow>* armed_rows(const ScrGraph& g, const ScrRuntime& rt) {
    if (rt.attackTable < 0) return nullptr;
    const auto it = g.attackdata.find(g.modes[static_cast<size_t>(rt.attackTable)].locomotion.attack);
    return it == g.attackdata.end() ? nullptr : &it->second;
}

// FUN_0085ab40 (R1), kind 10, on the armed table's exported rows. Not read: the row condition
// +40, chain +08/+18, control-mode list +20, sensing +38, the alt ATK +70/+80, issue2 +b0 and
// the foot-IK aim +d0/+e0 (§13.4: a gap).
void attack_runner(const ScrGraph& g, ScrRuntime& rt, const ScrWorld& w, float dt, BackSector back, ScrStepOut& out) {
    const std::vector<ScrAttackRow>* table = armed_rows(g, rt);
    if (table == nullptr) return;
    const std::vector<ScrAttackRow>& rows = *table;
    const int n = static_cast<int>(rows.size());
    rt.timeSinceAttack += dt;
    rt.timeInRow += dt;
    const auto inside = [&](const std::string& name) {
        return !name.empty() && w.playerInJudge && w.playerInJudge(name);
    };
    // onEnd (FUN_0085b910): a row's motion retired -> time since attack 0, cooldown +f4.
    if (w.bossMotionSwitched) {
        for (const ScrAttackRow& row : rows) {
            if (row.motion >= 0 && w.bossPreviousMotion == row.motion) {
                rt.timeSinceAttack = 0.0f;
                rt.cooldown = row.cooldownS;
            }
        }
    }
    // Select (a) FIX, (b) ATK, (c) the default row, when unselected or past +34.
    if (rt.attackRow < 0 || rt.attackRow >= n || rt.timeInRow > rows[static_cast<size_t>(rt.attackRow)].reselectS) {
        int pick = -1;
        for (int i = 0; pick < 0 && i < n; ++i) {
            if (inside(rows[static_cast<size_t>(i)].fix)) pick = i;
        }
        for (int i = 0; pick < 0 && i < n; ++i) {
            if (inside(rows[static_cast<size_t>(i)].atk)) pick = i;
        }
        for (int i = 0; pick < 0 && i < n; ++i) {
            if (rows[static_cast<size_t>(i)].isDefault) pick = i;
        }
        if (pick != rt.attackRow) rt.timeInRow = 0.0f;
        rt.attackRow = pick;
    }
    if (rt.attackRow < 0) return;  // no default row (Gaius's tables always carry one)
    const ScrAttackRow& row = rows[static_cast<size_t>(rt.attackRow)];

    // The goal: the live player brought into the row's ATTAIN cylinder (the meaning is
    // R1's guess) — he heads for the point that puts the player at the attain centre.
    rt.arrived = inside(row.attain);
    const Vec3 f = forward_of(w), l = left_of(w), p = w.playerPosition;
    const Vec3 target{p.x - f.x * row.attainZ - l.x * row.attainX, p.y, p.z - f.z * row.attainZ - l.z * row.attainX};
    gait(rt, w, target, rt.arrived, bearing_to(w, p), back, out);

    // Issue: the cooldown holds it unless +f8 and arrived; the selected row's ATK, else
    // (root kind 1, FUN_0085ba60) the first row whose ATK holds the player.
    if (rt.cooldown > 0.0f && !(row.issueArrived && rt.arrived)) return;
    if (inside(row.atk)) {
        scr_request(out, row.issue);
        return;
    }
    for (const ScrAttackRow& r : rows) {
        if (inside(r.atk)) {
            scr_request(out, r.issue);
            return;
        }
    }
}

// The walker's step 3 for root kind 1 (the slowleg, every frame; R1): kinds −1/0/1/10 here,
// any other through the registry's hook (Task 8's routes).
void locomotion(ScrCtx& ctx, float dt) {
    const ScrGraph& g = ctx.graph;
    ScrRuntime& rt = ctx.runtime;
    const ScrWorld& w = ctx.world;
    rt.steerDeg = 0.0f;
    rt.cooldown = std::max(0.0f, rt.cooldown - dt);  // ticks in every kind (R1)
    switch (rt.locoKind) {
        case -1:  // the slowleg is off (data: slowstatus +60 = 0 on every kind -1 row)
        case 1:   // no waypoint, so no request — Gaius's mode 3 (R1 binary + data) and mode 12
                  // (authored: no goal; the shake-off comes from its actions). Runs as no-steer:
                  // the binary's kind-1 walk (follower +378 = 1) is undecoded, a gap (R-AL).
            return;
        case 0:  // the point goal (a player snapshot) until arrived
            if (!rt.hasGoal) return;
            if (std::hypot(rt.goal.x - w.bossPosition.x, rt.goal.z - w.bossPosition.z) <= rt.arriveRadius) {
                rt.arrived = true;
            }
            gait(rt, w, rt.goal, rt.arrived, 0.0f, {}, ctx.out);
            return;
        case 10: {
            // MOVE_BACK rows: the slowstatus names a judge (+78; Gaius's KNIGHT_MOVE_BACK, Phaedra's KIRIN_BACK),
            // its sector bound from the sidecar, else Gaius's.
            BackSector back;
            if (rt.attackTable >= 0) {
                const ScrLocomotion& l = g.modes[static_cast<size_t>(rt.attackTable)].locomotion;
                if (!l.judge.empty()) {
                    back = l.backRadius > 0.0f ? BackSector{l.backRadius, l.backHalfDeg}
                                               : BackSector{kMoveBackRadius, kMoveBackHalfDeg};
                }
            }
            attack_runner(g, rt, w, dt, back, ctx.out);
            return;
        }
        default:
            if (ctx.registry.locomotion != nullptr) ctx.registry.locomotion(rt.locoKind, ctx, dt);
            return;
    }
}

// FUN_0082aef0: on a hit, kind 1/2/4 adds a, 0/5 adds b, 3 adds a when the flag is set; no hit
// decays by decay·dt; clamped to [0, 100]. A null scr_angry_status skips the add and the decay,
// not the clamp (binary).
void anger_model(const ScrGraph& g, ScrRuntime& rt, const ScrWorld& w, float dt) {
    const std::vector<float>& a = g.root.anger;  // {decay, a, b, flag}
    if (a.size() >= 4 && w.hit) {
        const int k = w.hitKind;
        if (k == 1 || k == 2 || k == 4 || (k == 3 && a[3] != 0.0f)) rt.anger += a[1];
        else if (k == 0 || k == 5) rt.anger += a[2];
    } else if (a.size() >= 4) {
        rt.anger -= a[0] * dt;
    }
    rt.anger = std::min(kAngerMax, rt.anger);
    if (!(rt.anger > 0.0f)) rt.anger = 0.0f;
}

}  // namespace

float scr_argf(const std::vector<ScrArg>& args, const char* slot) {
    const ScrArg* a = scr_arg(args, slot);
    return a == nullptr ? 0.0f : static_cast<float>(a->num);
}

int scr_handle_index(const std::string& text) {
    const size_t open = text.rfind('[');
    return open == std::string::npos ? -1 : std::atoi(text.c_str() + open + 1);
}

std::string scr_handle_name(const std::string& handle) {
    const size_t open = handle.find('\'');
    return open == std::string::npos ? handle : handle.substr(open + 1, handle.size() - open - 2);
}

void scr_library_add(ScrRegistry& r) {
    const std::pair<uint32_t, ScrPredicateFn> preds[] = {
        {kTrue, p_true}, {kHit, p_hit}, {kPlayerSurface, p_player_surface}, {kBossMotionIs, p_boss_motion_is},
        {kBossMotionStarted, p_boss_motion_started}, {kBossMotionEnded, p_boss_motion_ended},
        {kBossMotionInLayers, p_boss_motion_in_layers}, {kPlayerMotionIs, p_player_motion_is},
        {kPlayerMotionStarted, p_player_motion_started}, {kPlayerControlModeIs, p_player_control_mode_is},
        {kArrivedGoal, p_arrived_goal}, {kLostTarget, p_lost_target}, {kSensed, p_sensed},
        {kNotSensed, p_not_sensed}, {kPlayerDistGe, p_player_dist_ge}, {kPlayerDistLe, p_player_dist_le},
        {kPlayerBearingIn, p_player_bearing_in}, {kTimeInModeGe, p_time_in_mode_ge}, {kGameFlag, p_game_flag},
        {kSignalArrowPresent, p_signal_arrow_present}, {kModeJustEntered, p_mode_just_entered},
        {kSwingSpaceClear, p_swing_space_clear},
        {kHitKind, p_hit_kind}, {kHitRegion, p_hit_region}, {kHpPctGe, p_hp_pct_ge}, {kHpPctLe, p_hp_pct_le},
        {kTimeSinceTransitionGe, p_time_since_transition_ge}, {kTimeSinceAttackGe, p_time_since_attack_ge},
        {kAngerGe, p_anger_ge}, {kAngerLe, p_anger_le}, {kModeIs, p_mode_is},
        {kBossControlModeIs, p_boss_control_mode_is}, {kPerceivedNow, p_perceived_now},
        {kPerceivedBearingLe, p_perceived_bearing_le}, {kPlayerDist3dLe, p_player_dist3d_le},
        {kPlayerRiding, p_player_riding}, {kBossAttrBits, p_boss_attr_bits}, {kCloakEquipped, p_false},
        {kDemoCameraActive, p_false},
    };
    const std::pair<uint32_t, ScrActionFn> acts[] = {
        {kIfRun, a_if_run}, {kIfCtrl, a_if_ctrl}, {kRun, a_run}, {kRayCtrl, a_ray_ctrl},
        {kLookIfMotion, a_look_if_motion}, {kGoalPlayer, a_goal_player}, {kGoalPlayerGround, a_goal_player_ground},
        {kGoalLastseenY, a_goal_lastseen_y}, {kRouteReplan, a_noop}, {kMoveParam, a_move_param},
        {kSetFlag, a_set_flag}, {kCheckpoint, a_noop}, {kStageLoad, a_noop}, {kStageUnload, a_noop},
        {kAngerSet, a_anger_set}, {kAngerAdd, a_anger_add}, {kGoalLastseen, a_goal_lastseen},
        {kTurnToPlayer, a_turn_to_player},
    };
    for (const auto& [fn, p] : preds) r.predicates[fn] = p;
    for (const auto& [fn, a] : acts) r.actions[fn] = a;
    // D4: labelled no-ops (state the app does not model).
    for (const uint32_t fn : {kCloakEquipped, kDemoCameraActive, kSignalArrowPresent, kRayCtrl}) r.gaps.insert(fn);
    for (const int kind : {-1, 0, 1, 10}) r.kinds.insert(kind);
    // R-AL: kind 1 runs as no-steer (S12 R1's reading); the binary's kind-1 walk (the follower's
    // +378 = 1, the nav-loop patrol) is undecoded, so it is counted as a gap.
    r.kindGaps.insert(1);
}

void scr_library_enter_mode(int mode, ScrCtx& ctx) {
    const ScrLocomotion& l = ctx.graph.modes[static_cast<size_t>(mode)].locomotion;
    ScrRuntime& rt = ctx.runtime;
    rt.relpos = l.perception;
    rt.locoKind = l.kind;
    rt.moveParam = l.p0c;
    // Guess: the attainrange's first non-zero float (+0x10: 12 m; 30 m in lose_sight) is the
    // arrival radius.
    rt.arriveRadius = l.attain.size() > 4 ? l.attain[4] : 0.0f;
    static const std::string kNone;
    const std::string& armed =
        rt.attackTable >= 0 ? ctx.graph.modes[static_cast<size_t>(rt.attackTable)].locomotion.attack : kNone;
    if (armed != l.attack) {  // authored: a newly armed table starts unselected
        rt.attackRow = -1;
        rt.timeInRow = 0.0f;
    }
    rt.attackTable = l.attack.empty() ? -1 : mode;
    rt.timeSinceAttack = 0.0f;
}

ScrStepOut scr_boss_step(const ScrGraph& graph, const ScrRegistry& registry, ScrRuntime& rt, const ScrWorld& world,
                         float dt) {
    rt.dt = dt;
    rt.aimAtPlayer = false;
    scr_perceive(rt, world, dt);          // step 2
    anger_model(graph, rt, world, dt);    // FUN_0082aef0
    ScrStepOut pre;
    ScrCtx ctx{graph, registry, rt, world, pre};
    locomotion(ctx, dt);                  // step 3 (the slowleg; kind 10's attack runner)
    ScrStepOut out = scr_step(graph, registry, rt, world, dt);
    for (const int bit : out.controls) scr_request(pre, bit);
    out.controls = std::move(pre.controls);
    return out;
}

void scr_slowleg(ScrRuntime& rt, const ScrWorld& w, const Vec3& goal, bool arrived, ScrStepOut& out) {
    const bool own = goal.x == rt.goal.x && goal.z == rt.goal.z;
    const bool under = std::hypot(goal.x - w.bossPosition.x, goal.z - w.bossPosition.z) <= kNoFacing;
    gait(rt, w, goal, arrived, arrived && !own && !under ? bearing_to(w, goal) : 0.0f, {}, out);
}

bool scr_attack_attain(ScrGraph& graph, const std::vector<EventsVolume>& judgeDefaults, std::string& error) {
    for (auto& [key, rows] : graph.attackdata) {
        for (ScrAttackRow& row : rows) {
            if (row.attain.empty()) continue;
            const auto it = std::find_if(judgeDefaults.begin(), judgeDefaults.end(),
                                         [&](const EventsVolume& v) { return v.name == row.attain; });
            if (it == judgeDefaults.end() || it->parent.kind != EventsParent::Skelton || it->keys.size() != 1 ||
                it->keys[0].f != -1) {
                error = key + ": attain '" + row.attain + "' is not a static skelton judge default of the sidecar";
                return false;
            }
            // R(180°) about y into his frame (R4); + 0.0f keeps a 0 positive.
            row.attainX = -it->keys[0].pos.x + 0.0f;
            row.attainZ = -it->keys[0].pos.z + 0.0f;
            row.attainR = it->keys[0].size.x;
        }
    }
    return true;
}

bool scr_move_back_bind(ScrGraph& graph, const std::vector<EventsVolume>& judgeDefaults, std::string& error) {
    for (ScrMode& m : graph.modes) {
        ScrLocomotion& l = m.locomotion;
        if (l.judge.empty()) continue;
        const std::string name = scr_handle_name(l.judge);
        const auto it = std::find_if(judgeDefaults.begin(), judgeDefaults.end(),
                                     [&](const EventsVolume& v) { return v.name == name; });
        // The sector is read as a skelton cylinder yawed 180° (behind him), as KNIGHT_MOVE_BACK and KIRIN_BACK.
        if (it == judgeDefaults.end() || it->parent.kind != EventsParent::Skelton || it->keys.size() != 1 ||
            it->keys[0].f != -1 || std::fabs(it->keys[0].quat.y) < 0.999f) {
            error = "mode " + std::to_string(m.id) + ": MOVE_BACK judge '" + name +
                    "' is not a static skelton judge default yawed 180° in the sidecar";
            return false;
        }
        l.backRadius = it->keys[0].size.x;               // data: the cylinder's radius
        l.backHalfDeg = it->angle * 0.5f / kDegToRad;  // data: the full sector width (rad) halved
    }
    return true;
}

std::string scr_library_missing_args(uint32_t fn, const std::vector<ScrArg>& a) {
    const auto has = [&](const char* s) { return scr_arg(a, s) != nullptr; };
    const auto clip = [&](const char* s) { return has(s) && scr_arg(a, s)->clip >= 0; };
    const auto index = [&](const char* s) { return has(s) && scr_handle_index(scr_arg(a, s)->text) >= 0; };
    const auto mode = [&](const char* s) { return has(s) && scr_arg(a, s)->mode >= 0; };
    const auto flag = [&](const char* s) {
        return has(s) && scr_arg(a, s)->num >= 0.0 && scr_arg(a, s)->num < static_cast<double>(kScrFlags);
    };
    const auto handle = [&](const char* s, int ScrArg::*field) {  // a resolved ref or a null (true) handle
        return has(s) && (scr_arg(a, s)->null || scr_arg(a, s)->*field >= 0);
    };
    switch (fn) {
        case kBossMotionIs: case kBossMotionStarted: case kBossMotionEnded: case kBossMotionInLayers:
            return clip("h50") ? "" : "h50 (a clip)";
        case kPlayerMotionIs: case kPlayerMotionStarted: case kPlayerControlModeIs: case kBossControlModeIs:
            return index("h50") ? "" : "h50 (an indexed handle)";
        case kPlayerDistGe: case kPlayerDistLe: case kTimeInModeGe: case kHpPctGe: case kHpPctLe:
        case kTimeSinceTransitionGe: case kTimeSinceAttackGe: case kAngerGe: case kAngerLe: case kPerceivedBearingLe:
        case kPlayerDist3dLe:
            return has("f1c") ? "" : "f1c";
        case kPlayerBearingIn: return has("f1c") && has("f20") ? "" : "f1c/f20";
        case kSwingSpaceClear: return has("f1c") && has("f20") && has("f24") ? "" : "f1c/f20/f24";
        case kGameFlag: return flag("i10") ? "" : "i10 (a flag < kScrFlags)";
        case kHitKind: return has("i10") ? "" : "i10";
        case kModeJustEntered: case kModeIs: return mode("h50") ? "" : "h50 (a mode)";
        case kPlayerSurface:  // no gameobject filter: the app has no contact object
            return has("q28") && has("q30") && (!has("h40") || scr_arg(a, "h40")->null) ? "" : "q28/q30, null h40";
        case kHitRegion: case kBossAttrBits: return has("q28") && has("q30") ? "" : "q28/q30";
        case kIfRun: return handle("h40", &ScrArg::cond) && handle("h50", &ScrArg::actions) ? "" : "h40/h50";
        case kIfCtrl:
            return handle("h40", &ScrArg::cond) && has("h60") && scr_arg(a, "h60")->ctrl >= 0 ? "" : "h40/h60";
        case kRun: return has("h50") && scr_arg(a, "h50")->actions >= 0 ? "" : "h50 (an action list)";
        case kLookIfMotion: return clip("h40") ? "" : "h40 (a clip)";
        case kGoalPlayer: case kGoalPlayerGround: case kGoalLastseenY: case kGoalLastseen: case kAngerSet:
        case kAngerAdd: case kTurnToPlayer:
            return has("f10") ? "" : "f10";
        case kMoveParam: return has("i08") ? "" : "i08";
        case kSetFlag: return flag("i08") ? "" : "i08 (a flag < kScrFlags)";
        default: return "";
    }
}

bool scr_check_graph(const ScrGraph& graph, const std::vector<std::string>& judgeNames, const ScrCheckRules& rules,
                     std::string& error) {
    const auto known = [&](const std::string& name) {
        return std::find(judgeNames.begin(), judgeNames.end(), name) != judgeNames.end();
    };
    const auto fn_text = [](uint32_t fn) {
        char s[16];
        std::snprintf(s, sizeof s, "0x%x", fn);
        return std::string(s);
    };
    for (size_t i = 0; i < graph.controlModes.size(); ++i) {
        for (const ScrAssign& row : graph.controlModes[i].assign) {
            const bool read = rules.additiveRows || row.layer == "base";
            if (read && graph.controlDefs.find(row.when) == graph.controlDefs.end()) {
                error = "control_mode " + std::to_string(i) + ": control-def not exported: " + row.when;
                return false;
            }
        }
    }
    for (const ScrMode& m : graph.modes) {
        if (!m.locomotion.attack.empty() && graph.attackdata.find(m.locomotion.attack) == graph.attackdata.end()) {
            error = "mode " + std::to_string(m.id) + ": attackdata not exported: " + m.locomotion.attack;
            return false;
        }
    }
    for (const auto& [key, rows] : graph.attackdata) {
        for (const ScrAttackRow& row : rows) {
            if (row.motion < 0) {
                error = key + ": an attackdata row has no motion";
                return false;
            }
            for (const std::string* name : {&row.fix, &row.atk, &row.attain, &row.altAtk}) {
                const bool optional = rules.optionalJudges || name == &row.altAtk;
                if (!(optional && name->empty()) && !known(*name)) {
                    error = key + ": attackdata judge volume not in the sidecar: " + *name;
                    return false;
                }
            }
            if (rules.boundAttain && !row.attain.empty() && !(row.attainR > 0.0f)) {
                error = key + ": the ATTAIN cylinder of " + row.attain + " is unbound (scr_attack_attain)";
                return false;
            }
        }
    }
    for (const ScrCondition& c : graph.conditions) {
        for (const ScrClause& cl : c.clauses) {
            const std::string miss = rules.missingArgs(cl.fn, cl.args);
            if (!miss.empty()) {
                error = c.key + ": " + cl.pred + " (" + fn_text(cl.fn) + ") needs " + miss;
                return false;
            }
        }
    }
    for (const ScrActionList& l : graph.actions) {
        for (const ScrAction& a : l.records) {
            const std::string miss = rules.missingArgs(a.fn, a.args);
            if (!miss.empty()) {
                error = l.key + ": " + a.act + " (" + fn_text(a.fn) + ") needs " + miss;
                return false;
            }
        }
    }
    return true;
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
    rt.seenNow = seen;
    rt.nearNow = near;
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

std::string scr_registry_dump(const ScrRegistry& registry) {
    std::set<uint32_t> fns(registry.gaps.begin(), registry.gaps.end());
    fns.insert(kScrSubFn);  // scr_eval evaluates `sub` itself
    for (const auto& [fn, handler] : registry.predicates) fns.insert(fn);
    for (const auto& [fn, handler] : registry.actions) fns.insert(fn);
    std::string out;
    char line[48];
    for (const uint32_t fn : fns) {
        std::snprintf(line, sizeof line, "%x %s\n", fn, registry.gaps.count(fn) != 0 ? "gap" : "implemented");
        out += line;
    }
    for (const int kind : std::set<int>(registry.kinds.begin(), registry.kinds.end())) {
        out += "kind " + std::to_string(kind) + (registry.kindGaps.count(kind) != 0 ? " gap" : "") + "\n";
    }
    for (const ScrCdefOp& op : kScrCdefOps) {
        std::snprintf(line, sizeof line, "op 0x%02x %s\n", op.op, op.gap ? "gap" : "implemented");
        out += line;
    }
    // §13 ruling 4: the stomp foot-IK aim (attackdata +d0/+e0) is exported but never read; the
    // stomps play as authored clips.
    out += "feature aim gap\n";
    return out;
}
