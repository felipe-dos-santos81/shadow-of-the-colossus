#pragma once

#include <array>
#include <cstddef>

#include "../encounter_desc.h"
#include "valus_desc.h"

// Quadratus (colossus 2, `colossus_02_bull_a`, clip stem `boss_a`) — S11 Task 9,
// design D8/D9, rulings §13.10–13.14 and A5. Data only: the runtime that plays it
// (locomotion with root motion, the hoof-lift rule, the surface walk, shake-off,
// the anger model) is Task 10's.
//
// Provenance tags on every value:
//   data     — read from shipped files (glb names, the entity cgpr's per-bone
//              records) or from the definition tables embedded in eboot.elf
//              (cited by VA)
//   binary   — eboot strings / decompiled code (motion-name roles)
//   measured — computed from the baked glb / .anb root series
//   authored — the builder's pick, to be tuned on the user's walk
// Sources (.superpowers/sdd/2026-10-23-s10-s11-acceleration/): s11-quadratus-recon/
// recon.md ("R§n"), clips.md ("C§n"), quadratus.json, clips.json; s11-behaviour-re/
// findings.md ("F") and dumps/mammoth_behaviour.txt — the MAMMOTH mode graph
// (scr_mode_status 0x1616e70, 21 modes; conditions 0x1616dd0). "mode N" below is
// the game's MAMMOTH mode index, "motion N" is BOSS_A motion-def[N] (0x1616e20).
//
// Frames (measured, C§0): entity frame = glb frame, +z front, +x the model's
// LEFT, +y up; origin = the ground under the stand root (hip 14.528 m up).
// App yaw: +yaw turns +z toward +x, i.e. a LEFT turn.
//
// The glb's clips are byte-sorted, so everything here names clips BY NAME;
// `BehaviourStateDesc::clip` indexes `kClips` below, which the runtime resolves
// against the glb by name at load.

namespace encounters {
namespace quadratus_detail {

// --- The behaviour's clip table (names: data, the baked glb's 58 clips) -------

namespace C {
enum : int {
    Stand, Search, WalkStart, Walk, WalkEnd, TurnL, TurnR, WalkTurnL, WalkTurnR, Back,
    AttackRStart, AttackR, AttackRLoop, AttackREnd, AttackL, AttackLLoop, AttackLEnd,
    DownBLStart, DownBLLoop, DownBLEnd, DownBRStart, DownBRLoop, DownBREnd,
    DownFLStart, DownFLLoop, DownFLEnd, DownFRStart, DownFRLoop, DownFREnd,
    RefuseHead, RefuseShoulder, RefuseArmL, RefuseArmR, RefuseLegL, RefuseLegR,
    Angry, Death,
    Count,
};
}  // namespace C

// Never here (C§1, notes): `boss_a_death_loop`, `boss_a_add_*_foot_damage`,
// `boss_a_add_body_damage` (no root series: the body sinks 4.6–14.5 m) and
// `boss_a_boot` (absolute root y −83.5: draws him 98 m underground).
inline constexpr std::array<const char*, C::Count> kClips{{
    "boss_a_stand", "boss_a_search", "boss_a_walk_start", "boss_a_walk", "boss_a_walk_end",
    "boss_a_turn_l", "boss_a_turn_r", "boss_a_walk_turn_l", "boss_a_walk_turn_r", "boss_a_back",
    "boss_a_attack_r_start", "boss_a_attack_r", "boss_a_attack_r_loop", "boss_a_attack_r_end",
    "boss_a_attack_l", "boss_a_attack_l_loop", "boss_a_attack_l_end",
    "boss_a_down_b_l_start", "boss_a_down_b_l_loop", "boss_a_down_b_l_end",
    "boss_a_down_b_r_start", "boss_a_down_b_r_loop", "boss_a_down_b_r_end",
    "boss_a_down_f_l_start", "boss_a_down_f_l_loop", "boss_a_down_f_l_end",
    "boss_a_down_f_r_start", "boss_a_down_f_r_loop", "boss_a_down_f_r_end",
    "boss_a_refuse_head", "boss_a_refuse_shoulder", "boss_a_refuse_arm_l", "boss_a_refuse_arm_r",
    "boss_a_refuse_leg_l", "boss_a_refuse_leg_r",
    "boss_a_angry_1", "boss_a_death",
}};

// --- States -------------------------------------------------------------------

// Our states are clip-level; the game's 21 modes are condition-level (perception,
// anger, attachment). Each state names the mode(s) it plays in. Modes 0–3 (the
// boot / door cutscene) and 18 (hill ground-pound) are not reproduced.
namespace S {
enum : int {
    Stand, Search, WalkStart, Walk, WalkEnd, TurnL, TurnR, WalkTurnL, WalkTurnR, Back,
    AttackRStart, AttackR, AttackRLoop, AttackREnd,
    AttackLStart, AttackL, AttackLLoop, AttackLEnd,
    DownBLStart, DownBLLoop, DownBLEnd, DownBRStart, DownBRLoop, DownBREnd,
    DownFLStart, DownFLLoop, DownFLEnd, DownFRStart, DownFRLoop, DownFREnd,
    RiseReturnB, RiseReturnF,
    RefuseHead, RefuseShoulder, RefuseArmL, RefuseArmR, RefuseLegL, RefuseLegR,
    Angry, Death,
    Count,
};
}  // namespace S

// The sprain (modes 9–12) ends when its transition timer reaches 20 s (cond[67],
// data, FUN_00833d80 on +0x4b54) or its mode time 45 s (cond[65], data,
// FUN_00833d50 on +0x4b50). The mode is entered on the hit, so the held loop is
// 20 s minus the start clip (2.0 s, measured). The 45 s cap is not modelled: in
// the game a re-hit on a sole re-enters the sprain with the same control held
// (the loop keeps playing), which plausibly refreshes the 20 s timer while the
// mode time runs on to 45 s (the RE reading, not traced). Our Down* states
// ignore hoof hits (no re-hit), so the 20 s always ends the sprain first; the
// refresh-with-cap needs a second clock BehaviourDesc does not have (Task 10).
inline constexpr float kSprainSeconds = 20.0f;
inline constexpr float kDownStartSeconds = 2.0f;
inline constexpr float kSprainLoopSeconds = kSprainSeconds - kDownStartSeconds;
// Then mode 14 rising_return: 10 s (cond[71], data) before he may attack again.
// Its 10 s includes the down end clip (the mode has no motion of its own), so the
// stand after the end clip lasts 10 s minus the end clip (measured: rear 3.6667 s,
// front 3.3333 s).
inline constexpr float kRiseReturnSeconds = 10.0f;
inline constexpr float kDownBEndSeconds = 3.666667f;
inline constexpr float kDownFEndSeconds = 3.333333f;
// Mode 13 rising: the player grabbing the downed foot (cond[115]) cuts the
// sprain short; the rise lasts 2 s (cond[69], data) before mode 14. The table
// cannot see attachment: the app enters the down end state after this.
inline constexpr float kRisingSeconds = 2.0f;

// Which state follows which: the clip chains are measured continuous (C§1: each
// clip's last frame is the next one's first). The app makes the moves the table
// cannot (stand -> walk_start / turn / attack start / refuse / angry, walk ->
// walk_end / walk_turn) with behaviour_enter. Every turn rate is 0: the turn
// lives in the bones, and the app adds kClipYaw at the clip end (C§2).
inline constexpr std::array<BehaviourStateDesc, S::Count> kStates{{
    // modes 4/7/15: patrol, standby, attack-mode idle — motion 4 "Standing".
    {.clip = C::Stand},
    // mode 6 looked (motion 5), then mode 4 (cond "always true").
    {.clip = C::Search, .clipEndNext = S::Stand},
    // modes 4/5/15 (motions 8, 10, 11): the walk; its loop carries 9.375 m/s
    // of root (measured).
    {.clip = C::WalkStart, .clipEndNext = S::Walk},
    {.clip = C::Walk},
    {.clip = C::WalkEnd, .clipEndNext = S::Stand},
    // mode 7's turns (motions 7/6), the walk's (motions 13/12).
    {.clip = C::TurnL, .clipEndNext = S::Stand},
    {.clip = C::TurnR, .clipEndNext = S::Stand},
    {.clip = C::WalkTurnL, .clipEndNext = S::Walk},
    {.clip = C::WalkTurnR, .clipEndNext = S::Walk},
    // motion 14 "recession": one 10 m step back (one cycle: authored).
    {.clip = C::Back, .clipEndNext = S::Stand},
    // modes 15/17 (scr_attackdata 0x22329a8 [0]): the right arm attack, motions
    // 17 start -> 18 attack -> 19 landing loop -> 20 stop.
    {.clip = C::AttackRStart, .clipEndNext = S::AttackR},
    {.clip = C::AttackR, .clipEndNext = S::AttackRLoop},
    {.clip = C::AttackRLoop, .clipEndNext = S::AttackREnd},
    {.clip = C::AttackREnd, .clipEndNext = S::Stand},
    // [1]: the left arm attack (motions 21 -> 22 -> 23) has no start motion of
    // its own: it begins on attack_r_start's last frame (measured, C§1).
    {.clip = C::AttackRStart, .clipEndNext = S::AttackL},
    {.clip = C::AttackL, .clipEndNext = S::AttackLLoop},
    {.clip = C::AttackLLoop, .clipEndNext = S::AttackLEnd},
    {.clip = C::AttackLEnd, .clipEndNext = S::Stand},
    // modes 12/11/10/9, the sprains (motions 39–41, 36–38, 33–35, 30–32):
    // start -> loop (held) -> end -> mode 14's rest.
    {.clip = C::DownBLStart, .clipEndNext = S::DownBLLoop},
    {.clip = C::DownBLLoop, .timerSeconds = kSprainLoopSeconds, .timerNext = S::DownBLEnd},
    {.clip = C::DownBLEnd, .clipEndNext = S::RiseReturnB},
    {.clip = C::DownBRStart, .clipEndNext = S::DownBRLoop},
    {.clip = C::DownBRLoop, .timerSeconds = kSprainLoopSeconds, .timerNext = S::DownBREnd},
    {.clip = C::DownBREnd, .clipEndNext = S::RiseReturnB},
    {.clip = C::DownFLStart, .clipEndNext = S::DownFLLoop},
    {.clip = C::DownFLLoop, .timerSeconds = kSprainLoopSeconds, .timerNext = S::DownFLEnd},
    {.clip = C::DownFLEnd, .clipEndNext = S::RiseReturnF},
    {.clip = C::DownFRStart, .clipEndNext = S::DownFRLoop},
    {.clip = C::DownFRLoop, .timerSeconds = kSprainLoopSeconds, .timerNext = S::DownFREnd},
    {.clip = C::DownFREnd, .clipEndNext = S::RiseReturnF},
    // mode 14 rising_return, after a rear / a front kneel.
    {.clip = C::Stand, .timerSeconds = kRiseReturnSeconds - kDownBEndSeconds,
     .timerNext = S::Stand},
    {.clip = C::Stand, .timerSeconds = kRiseReturnSeconds - kDownFEndSeconds,
     .timerNext = S::Stand},
    // mode 19 parent (attached): the swings, motions 25–29 and 15 "shudder"
    // (refuse_shoulder, control 351, the only one whose issuer is known, F).
    // Every refuse clip closes on its first frame (measured), then stand.
    {.clip = C::RefuseHead, .clipEndNext = S::Stand},
    {.clip = C::RefuseShoulder, .clipEndNext = S::Stand},
    {.clip = C::RefuseArmL, .clipEndNext = S::Stand},
    {.clip = C::RefuseArmR, .clipEndNext = S::Stand},
    {.clip = C::RefuseLegL, .clipEndNext = S::Stand},
    {.clip = C::RefuseLegR, .clipEndNext = S::Stand},
    // mode 16 anger_playback (motion 16 "A_boss_anger"), then mode 17's attacks.
    {.clip = C::Angry, .clipEndNext = S::Stand},
    // mode 8 (motion 42; the game then plays 43 death_loop). The machine reads
    // -1 as "loops", but Death HOLDS its last frame (kHoldLastFrameState: the
    // baked death_loop is root-less and sinks him 4.6 m, C§notes). Reached only
    // by the app at HP 0 (the data forces mode 8 from common code, F).
    {.clip = C::Death},
}};

// The one state whose clip plays once and holds its last frame instead of
// looping (BehaviourStateDesc has no hold flag yet: Task 10).
inline constexpr int kHoldLastFrameState = S::Death;

// --- Weakpoints ---------------------------------------------------------------

// Index order puts the two sigils first so the runtime's head/arm reaction slots
// (0/1) and the tendrils' two roots land on the sigils.
namespace W {
enum : int { HeadSigil, BackSigil, HoofRL, HoofRR, HoofFL, HoofFR, Count };
}  // namespace W

// Materials: data (the glb's material names). Damage regions (data, boss_damage
// `mammoth_first` 0x16c0da0): head 0x10, back 0x30; the soles FR 0x380, BR
// 0x390, FL 0x3a0, BL 0x3b0. weakpoint_06 (region 0x190) is damage only in
// `mammoth_second` (Hard): unwired (F).
// Reactions: the sigils' add_* clips (motion 45 "Boss_head_damage"; the back
// sigil is skinned 125/125 to Bone_HARA, measured, so motion 48 "belly damage
// (right)" = add_hara_r stands in: authored); a hoof's reaction is its kneel's
// start clip (the Transition gate enters that state).
inline constexpr std::array<WeakpointDesc, W::Count> kWeakpoints{{
    {.material = "weakpoint_04", .reactionClip = "boss_a_add_head_damage"},
    {.material = "weakpoint_05", .reactionClip = "boss_a_add_hara_r_damage"},
    {.material = "colossus_02_hoof_weakpoint_rl", .reactionClip = "boss_a_down_b_l_start"},
    {.material = "colossus_02_hoof_weakpoint_rr", .reactionClip = "boss_a_down_b_r_start"},
    {.material = "colossus_02_hoof_weakpoint_fl", .reactionClip = "boss_a_down_f_l_start"},
    {.material = "colossus_02_hoof_weakpoint_fr", .reactionClip = "boss_a_down_f_r_start"},
}};

// The sigils' order (R§2 role guess, design G6): the back sigil, then the head.
// Quadratus-local until the desc carries an order (Task 10).
inline constexpr std::array<int, 2> kSigilOrder{{W::BackSigil, W::HeadSigil}};

// The damage multipliers of the sigils' regions and of plain flesh (data,
// `mammoth_first` rows 0x10/0x30 and 0x20…; the four fields' meanings — and the
// HP formula — are unresolved, F "Open"). Recorded, not yet applied.
inline constexpr std::array<float, 4> kSigilDamageScale{{0.34f, 0.034f, 0.51f, 0.34f}};
inline constexpr std::array<float, 4> kFleshDamageScale{{1.0f, 0.1f, 1.5f, 1.0f}};

// The hoof gate: a sword hit on a sole region -> that hoof's sprain mode (data,
// `FUN_00832710(mask 0x3f0, region)`, F §3). "No damage" is ours (authored, the
// recon / design G6 reading): the game's damage table gives the soles flesh-like
// rows (`mammoth_first` [35]–[38] @0x16d1bf0…0x16d1dd0: {1, 0.5, 1.5, 1,
// +14 = 10}); here the hit is a state change only. A hoof's shell, the joint
// its sole is 100 % skinned to (measured, 233–235 verts each), the kneel it
// starts, and the sole lift at which a strike counts (the game's region ->
// triangle mapping is still open, so the reach rule is ours: 0.5 m proposed from
// the measured windows, 0.3 m the recorded fallback, C§3). Quadratus-local.
struct HoofDesc {
    int weakpoint;
    const char* joint;
    int downState;
    float liftMeters;
};
inline constexpr std::array<HoofDesc, 4> kHooves{{
    {.weakpoint = W::HoofRL, .joint = "Bone_B_TOE_L", .downState = S::DownBLStart, .liftMeters = 0.5f},
    {.weakpoint = W::HoofRR, .joint = "Bone_B_TOE_R", .downState = S::DownBRStart, .liftMeters = 0.5f},
    {.weakpoint = W::HoofFL, .joint = "Bone_TOE_L", .downState = S::DownFLStart, .liftMeters = 0.5f},
    {.weakpoint = W::HoofFR, .joint = "Bone_TOE_R", .downState = S::DownFRStart, .liftMeters = 0.5f},
}};

// The states that ignore hoof hits:
// - the 12 kneel states: the sprain modes 9–12 do re-check the soles (data), but
//   a re-hit there re-enters the sprain with the loop still playing; a
//   Transition to Down*Start would pop him back to the standing pose (+6.9 m)
//   and restart the 20 s — so no re-hit (see kSprainSeconds); the end clips
//   play in mode 14 (motion graph: the loop holds only while the sprain
//   control does), which has no sole transition (data);
// - modes 14 (rising_return), 19/20 (attached) and 8 (death): no sole
//   transition (data).
// Every other mode — patrol, standby, lose-sight, the attack modes — checks all
// four soles (data).
inline constexpr std::array<int, 21> kNoHoofGateStates{{
    S::DownBLStart, S::DownBLLoop, S::DownBLEnd, S::DownBRStart, S::DownBRLoop, S::DownBREnd,
    S::DownFLStart, S::DownFLLoop, S::DownFLEnd, S::DownFRStart, S::DownFRLoop, S::DownFREnd,
    S::RiseReturnB, S::RiseReturnF, S::RefuseHead, S::RefuseShoulder, S::RefuseArmL,
    S::RefuseArmR, S::RefuseLegL, S::RefuseLegR, S::Death,
}};

// Ruling A5: the gates. Death ignores the sigils (a post-mortem hit counts for
// nothing); the no-gate states ignore the hooves; then a hoof hit anywhere else
// is a Transition into its kneel (no damage); the two sigils deal damage in
// every other state (the "only while attached" and "back before head" rules are
// the runtime's, from kSigilOrder).
inline constexpr std::size_t kGateCount =
    2 + kNoHoofGateStates.size() * kHooves.size() + kHooves.size() + 2;
constexpr std::array<WeakpointGate, kGateCount> make_gates() {
    std::array<WeakpointGate, kGateCount> gates{};
    std::size_t n = 0;
    gates[n++] = {.state = S::Death, .weakpoint = W::BackSigil, .kind = GateKind::Ignore};
    gates[n++] = {.state = S::Death, .weakpoint = W::HeadSigil, .kind = GateKind::Ignore};
    for (int state : kNoHoofGateStates) {
        for (const HoofDesc& hoof : kHooves) {
            gates[n++] = {.state = state, .weakpoint = hoof.weakpoint, .kind = GateKind::Ignore};
        }
    }
    for (const HoofDesc& hoof : kHooves) {
        gates[n++] = {.state = kBehaviourNone, .weakpoint = hoof.weakpoint,
                      .kind = GateKind::Transition, .target = hoof.downState};
    }
    gates[n++] = {.state = kBehaviourNone, .weakpoint = W::BackSigil, .kind = GateKind::Damage};
    gates[n++] = {.state = kBehaviourNone, .weakpoint = W::HeadSigil, .kind = GateKind::Damage};
    return gates;
}
inline constexpr std::array<WeakpointGate, kGateCount> kGates = make_gates();

// --- Climb bands --------------------------------------------------------------

namespace B {
enum : int {
    RearLeftHoof, RearLeftShin, RearLeftThigh, Rump, Back, Neck, Head,
    RearRightHoof, RearRightShin, RearRightThigh,
    Count,
};
}  // namespace B

// The chain (R§3 graph, the Valus pattern: guess): rear hoof -> shin -> thigh ->
// rump -> back (surface walk) -> neck -> head, both rear legs joining at the
// rump. Front legs omitted (R§3). Radii: the entity cgpr's per-bone B field
// (data; matches measured p50–p99): B_TOE 2.5, SUNE 2.0, MOMO 3.0, KUBI 6.0,
// HEAD 3.0. The rump's 4.3 is measured (MATA p99 4.26–4.32). MATA_L/R share a
// bind point, so both thighs end on the same rump start (measured).
// The hoof bands carry no shell (weakpoint -1, though the recon json links them
// to the rl/rr soles): a sole faces down and is struck from the ground beside the
// lifted hoof (the hoof gate), never by a climber on the band.
// The back: Weak_point2 -> KUBI, horizontal (stood on). Its surface is a slope
// (+17° standing, +35° in the rear kneel, C§5), so the band is TAPERED: 1.5 m
// at the rear (the back sigil end) to 6.3 m at KUBI (measured mesh-top above the
// axis 1.2–1.9 / 6.1–6.6 m, C§5). `radius` holds the rear value; the front one
// is kBandTaper below until ClimbBand carries a second radius (Task 10).
inline constexpr std::array<ClimbBand, B::Count> kBands{{
    {.boneA = "Bone_B_TOE_L", .boneB = "Bone_SUNE_L", .radius = 2.5f, .up = B::RearLeftShin,
     .down = ClimbBandId::None, .weakpoint = -1, .name = "rear_left_hoof"},
    {.boneA = "Bone_SUNE_L", .boneB = "Bone_MOMO_L", .radius = 2.0f, .up = B::RearLeftThigh,
     .down = B::RearLeftHoof, .weakpoint = -1, .name = "rear_left_shin"},
    {.boneA = "Bone_MOMO_L", .boneB = "Bone_MATA_L", .radius = 3.0f, .up = B::Rump,
     .down = B::RearLeftShin, .weakpoint = -1, .name = "rear_left_thigh"},
    {.boneA = "Bone_MATA_L", .boneB = "Weak_point2", .radius = 4.3f, .up = B::Back,
     .down = B::RearLeftThigh, .weakpoint = -1, .name = "rump"},
    {.boneA = "Weak_point2", .boneB = "Bone_KUBI", .radius = 1.5f, .up = B::Neck, .down = B::Rump,
     .weakpoint = W::BackSigil, .horizontal = true, .name = "back"},
    {.boneA = "Bone_KUBI", .boneB = "Bone_HEAD", .radius = 6.0f, .up = B::Head, .down = B::Back,
     .weakpoint = -1, .name = "neck"},
    {.boneA = "Bone_HEAD", .boneB = "OptBone_Look", .radius = 3.0f, .up = ClimbBandId::None,
     .down = B::Neck, .weakpoint = W::HeadSigil, .name = "head"},
    {.boneA = "Bone_B_TOE_R", .boneB = "Bone_SUNE_R", .radius = 2.5f, .up = B::RearRightShin,
     .down = ClimbBandId::None, .weakpoint = -1, .name = "rear_right_hoof"},
    {.boneA = "Bone_SUNE_R", .boneB = "Bone_MOMO_R", .radius = 2.0f, .up = B::RearRightThigh,
     .down = B::RearRightHoof, .weakpoint = -1, .name = "rear_right_shin"},
    {.boneA = "Bone_MOMO_R", .boneB = "Bone_MATA_R", .radius = 3.0f, .up = B::Rump,
     .down = B::RearRightShin, .weakpoint = -1, .name = "rear_right_thigh"},
}};

struct BandTaper {
    int band;
    float radiusA;  // at boneA
    float radiusB;  // at boneB
};
inline constexpr BandTaper kBandTaper{.band = B::Back, .radiusA = 1.5f, .radiusB = 6.3f};

// --- Shake-off ----------------------------------------------------------------

// The game's rule (data, func_def[11] = cond[95] -> control 351 "Mammoth_shudder"):
// while attached on a hide region (11 regions; their mapping to the bands is
// open, so every band counts here), not already playing refuse_shoulder, and
// within the first 5 s of the attached cycle (timer2 < 5 s) -> refuse_shoulder.
// The attached mode re-enters itself every 20 s (cond[91]), restarting that
// window; a stab pauses it 3 s (mode 20, cond[93], "Quit_Mammoth_shudder").
// The other refuse_* swings exist (motions 25–29) but their issuer is not found.
struct ShakeRule {
    int state;
    float windowSeconds;
    float attachedCycleSeconds;
    float stabPauseSeconds;
};
inline constexpr ShakeRule kShakeRule{.state = S::RefuseShoulder, .windowSeconds = 5.0f,
                                      .attachedCycleSeconds = 20.0f, .stabPauseSeconds = 3.0f};

// Each refuse clip's strength, for player_state's ShakeOff: the stamina drain per
// second (authored: the region bone's measured p90 angular speed / 10, C§6),
// applied only inside the active window (measured: frames above 90 °/s, s on the
// clip clock). No forced release (authored). Quadratus-local.
struct ShakeDesc {
    int state;
    float drainPerSecond;
    float activeBegin;
    float activeEnd;
};
inline constexpr std::array<ShakeDesc, 6> kShakes{{
    {.state = S::RefuseHead, .drainPerSecond = 30.6f, .activeBegin = 0.47f, .activeEnd = 2.53f},
    {.state = S::RefuseShoulder, .drainPerSecond = 34.8f, .activeBegin = 0.13f, .activeEnd = 3.47f},
    {.state = S::RefuseArmL, .drainPerSecond = 19.8f, .activeBegin = 0.27f, .activeEnd = 2.60f},
    {.state = S::RefuseArmR, .drainPerSecond = 20.3f, .activeBegin = 0.20f, .activeEnd = 2.27f},
    {.state = S::RefuseLegL, .drainPerSecond = 15.1f, .activeBegin = 0.73f, .activeEnd = 2.40f},
    {.state = S::RefuseLegR, .drainPerSecond = 13.3f, .activeBegin = 0.47f, .activeEnd = 2.33f},
}};

// --- Turns --------------------------------------------------------------------

// He turns when the player's bearing leaves ±30° (data, mode 7: FUN_0082fae0(30)).
inline constexpr float kTurnTriggerDeg = 30.0f;

// The yaw the app adds at a turn state's clip end (measured body-axis/HARA
// heading delta, C§2; + = left). Quadratus-local (the desc has no such field).
struct ClipYaw {
    int state;
    float degrees;
};
inline constexpr std::array<ClipYaw, 4> kClipYaw{{
    {.state = S::TurnL, .degrees = 45.0f},
    {.state = S::TurnR, .degrees = -45.0f},
    {.state = S::WalkTurnL, .degrees = 45.0f},
    {.state = S::WalkTurnR, .degrees = -44.3f},
}};

// --- Collision ----------------------------------------------------------------

// The recon's candidate proxy (R§4, json `body_capsules_candidate`): per leg
// shoulder/hip -> knee -> hoof -> IK foot, then back, neck, head. Radii: the
// cgpr B field (data) — TOE/B_TOE/UDE 2.5, SUNE 2.0, MOMO 3.0, back 7 (HARA/MUNE
// B), KUBI 6.0, HEAD 3.0 — and the upper front leg's 3.0 (measured JOWAN p50
// 2.35 / p99 3.62). The torso three sit above a 1.7 m player standing on the
// ground (gated out vertically); the legs collide.
inline constexpr std::array<CapsuleDesc, 17> kCapsules{{
    {.from = "Bone_KATA_L", .to = "Bone_UDE_L", .radius = 3.0f},
    {.from = "Bone_UDE_L", .to = "Bone_TOE_L", .radius = 2.5f},
    {.from = "Bone_TOE_L", .to = "OptBone_IKFootFL", .radius = 2.5f},
    {.from = "Bone_KATA_R", .to = "Bone_UDE_R", .radius = 3.0f},
    {.from = "Bone_UDE_R", .to = "Bone_TOE_R", .radius = 2.5f},
    {.from = "Bone_TOE_R", .to = "OptBone_IKFootFR", .radius = 2.5f},
    {.from = "Bone_MATA_L", .to = "Bone_MOMO_L", .radius = 3.0f},
    {.from = "Bone_MOMO_L", .to = "Bone_SUNE_L", .radius = 3.0f},
    {.from = "Bone_SUNE_L", .to = "Bone_B_TOE_L", .radius = 2.0f},
    {.from = "Bone_B_TOE_L", .to = "OptBone_IKFootBL", .radius = 2.5f},
    {.from = "Bone_MATA_R", .to = "Bone_MOMO_R", .radius = 3.0f},
    {.from = "Bone_MOMO_R", .to = "Bone_SUNE_R", .radius = 3.0f},
    {.from = "Bone_SUNE_R", .to = "Bone_B_TOE_R", .radius = 2.0f},
    {.from = "Bone_B_TOE_R", .to = "OptBone_IKFootBR", .radius = 2.5f},
    {.from = "Weak_point2", .to = "Bone_KUBI", .radius = 7.0f},
    {.from = "Bone_KUBI", .to = "Bone_HEAD", .radius = 6.0f},
    {.from = "Bone_HEAD", .to = "OptBone_Look", .radius = 3.0f},
}};

// --- Attacks ------------------------------------------------------------------

// Indexed ValusAttack - 1 (StompL, StompR; no club). `clip` is the chain's
// impact clip; the chain itself is kAttackChains' run of states (A5: the app
// sets the cooldown on the chain's last state).
//   damageJoint: data — scr_attackdata 0x22329a8 [1]/[0] skelfocus
//     OptBone_IKFootFL / FR (the sole point, a child of the toe bone).
//   reachMeters 25: authored — selection range from the entity origin; the
//     stomp lands 24.2 m ahead, 7.4 m aside (measured, C§4; scr_attainrange[0]
//     {8, 25, 180, 80} carries a 25 whose meaning is undecoded, F).
//   facingDot 0.9397 = cos 20°: the value is data (cond[43] "P_is_at_the_front",
//     +1c = 20); its role as the attack gate is the RE lane's reading — cond[43]
//     feeds func[30] -> control 342 "Mammoth_rise" (the rear-up start) in modes
//     7/16, while mode 15's own trigger (scr_attackdata / scr_attainrange
//     {8, 25, 180, 80}) is undecoded.
//   hit 3.0 / wave 8.0 m: authored — sole proxy 2.6 + player 0.35 (measured),
//     Valus's 6.0 wave scaled. (The game's volume is objanim type 46, ≈ 15 × 15
//     × 30 m at (±14.7, 3.3, 37.4), F: not ported.)
//   impact [0.929, 1.0]: measured — attack_r/l t ∈ [1.703, 1.834] s, the
//     stomping hoof's last three frames (lift 4.5 -> 2.5 -> 0 m); the window
//     ends AT the clip end, so the runtime must treat it as inclusive or carry
//     it into the loop clip (C§4).
//   cooldown 5.0 s: authored (scr_attackdata +0xf4 = 5, angry rows 10: meaning
//     undecoded). damage 35 / wave 15 (Valus's stomp numbers): authored.
//   swingSeconds -1: no whoosh (silent, D10).
inline constexpr std::array<AttackDesc, 2> kAttacks{{
    {.clip = "boss_a_attack_l", .damageJoint = "OptBone_IKFootFL", .reachMeters = 25.0f,
     .facingDot = 0.9397f, .hitRangeMeters = 3.0f, .waveRangeMeters = 8.0f,
     .cooldownSeconds = 5.0f, .impactBegin = 0.929f, .impactEnd = 1.0f, .swingSeconds = -1.0f,
     .damage = 35.0f, .waveDamage = 15.0f, .heavy = false},
    {.clip = "boss_a_attack_r", .damageJoint = "OptBone_IKFootFR", .reachMeters = 25.0f,
     .facingDot = 0.9397f, .hitRangeMeters = 3.0f, .waveRangeMeters = 8.0f,
     .cooldownSeconds = 5.0f, .impactBegin = 0.929f, .impactEnd = 1.0f, .swingSeconds = -1.0f,
     .damage = 35.0f, .waveDamage = 15.0f, .heavy = false},
}};

// kAttacks[i]'s run of states: the app enters `first`; `last`'s clip end sets
// the cooldown. Quadratus-local (the desc has no chain field).
struct AttackChain {
    int first;
    int last;
};
inline constexpr std::array<AttackChain, 2> kAttackChains{{
    {.first = S::AttackLStart, .last = S::AttackLEnd},
    {.first = S::AttackRStart, .last = S::AttackREnd},
}};

// The mean turn rate the clips realise (measured: turn_l/r 45° / 2.333 s =
// 19.3 °/s, C§2) — an aim target only; every state's own rate is 0.
inline constexpr float kTurnDegPerSec = 19.3f;

// A silent cue (D10: the S11 fight waits for the bank mapping and an audition;
// a null clip is audio_map's "deliberately silent").
inline constexpr SoundCue kSilent{.clip = nullptr, .gain = 1.0f, .loop = false};

}  // namespace quadratus_detail

inline constexpr EncounterDesc kQuadratus{
    .id = "quadratus",
    // data: boss_param[1] 0x16d1e70 = {220, 330, …} via boss_type_def MAMMOTH
    // (Normal 220; 330 read as Hard: guess, F). The damage per hit is authored
    // elsewhere (CombatTuning), so the fight runs ~2.2× Valus's 100.
    .maxHealth = 220.0f,
    // chest: Bone_MUNE (the camera orbit / tendril centre; bind z 6, measured).
    // feet: the stomping soles (data skelfocus, as kAttacks).
    .joints = {.chest = "Bone_MUNE", .footL = "OptBone_IKFootFL", .footR = "OptBone_IKFootFR"},
    // No weapon; no corpse swap — he holds death's last frame where the fall's
    // root travel left him (the shipped dead body sits at a fixed (245, −98,
    // −818), not where he falls; §13.13 "the corpse stays").
    .props = {},
    .weakpoints = quadratus_detail::kWeakpoints,
    // A landed hit off every shell. Motion 55 "Boss_body_damage" =
    // add_body_damage has no root series (sinks 14.5 m, C§notes); the back
    // sigil's add_hara_r stands in (authored).
    .bodyReactionClip = "boss_a_add_hara_r_damage",
    .bands = quadratus_detail::kBands,
    .capsules = quadratus_detail::kCapsules,
    .turnDegPerSec = quadratus_detail::kTurnDegPerSec,
    .attacks = quadratus_detail::kAttacks,
    // The death clip (motion 42): 80 intervals / 15 fps = 5.333 s (measured).
    // The tendril window, the 8.0 s end and the camera keys are Valus's S6
    // values (authored, unscaled: tune on the walk; the game's MOMMOTH_DEATH_CAM
    // is not decoded). Two roots: the two sigils (weakpoints 0/1). The
    // tendrils' fallback centre is the stand hip height 14.5 m (measured).
    .death = {.clip = "boss_a_death",
              .motionSeconds = 5.333333f,
              .tendrilStartSeconds = 0.4f,
              .tendrilEndSeconds = 3.0f,
              .endSeconds = 8.0f,
              .roots = 2,
              .fallbackChestHeight = 14.5f,
              .camera = kValus.death.camera},
    .cues = {.stompStart = quadratus_detail::kSilent,
             .stompImpact = quadratus_detail::kSilent,
             .clubStart = quadratus_detail::kSilent,
             .clubImpact = quadratus_detail::kSilent,
             .clubCancel = quadratus_detail::kSilent,
             .reaction = quadratus_detail::kSilent,
             .defeat = quadratus_detail::kSilent,
             .footLeft = quadratus_detail::kSilent,
             .footRight = quadratus_detail::kSilent,
             .clubSwing = quadratus_detail::kSilent,
             .music = nullptr,  // no music (D10)
             .musicGain = 0.0f},
    // Authored: Valus's framing scaled to a body ≈ 26 m tall / 32 m long (the
    // bind AABB, measured).
    .targetCam = {.distanceMin = 30.0f,
                  .distanceMax = 45.0f,
                  .defaultDistance = 38.0f,
                  .defaultPitchDeg = 10.0f,
                  .pitchLimitDeg = 80.0f},
    .behaviour = {quadratus_detail::kStates, {}, quadratus_detail::kGates},
};

// Every link in range: the band chain, the bands' shells, the hoof rows, the
// shake and yaw rows, the attack chains (behaviour_valid covers the machine).
constexpr bool quadratus_links_valid() {
    namespace q = quadratus_detail;
    const auto band = [](int i) { return i == ClimbBandId::None || (i >= 0 && i < q::B::Count); };
    const auto state = [](int i) { return i >= 0 && i < q::S::Count; };
    const auto weakpoint = [](int i) { return i >= 0 && i < q::W::Count; };
    for (const ClimbBand& b : q::kBands) {
        if (!band(b.up) || !band(b.down) || !band(b.branch) ||
            !(b.weakpoint == -1 || weakpoint(b.weakpoint))) {
            return false;
        }
    }
    for (const BehaviourStateDesc& s : q::kStates) {
        if (s.clip < 0 || s.clip >= q::C::Count) {
            return false;
        }
    }
    for (const q::HoofDesc& h : q::kHooves) {
        if (!weakpoint(h.weakpoint) || !state(h.downState)) {
            return false;
        }
    }
    for (const q::ShakeDesc& s : q::kShakes) {
        if (!state(s.state) || !(s.activeBegin < s.activeEnd)) {
            return false;
        }
    }
    for (const q::ClipYaw& y : q::kClipYaw) {
        if (!state(y.state)) {
            return false;
        }
    }
    for (const q::AttackChain& c : q::kAttackChains) {
        if (!state(c.first) || !state(c.last) || c.first > c.last) {
            return false;
        }
    }
    for (int w : q::kSigilOrder) {
        if (!weakpoint(w)) {
            return false;
        }
    }
    return state(q::kShakeRule.state) && state(q::kHoldLastFrameState) &&
           band(q::kBandTaper.band);
}

static_assert(behaviour_valid(kQuadratus.behaviour));
static_assert(quadratus_links_valid());
static_assert(kQuadratus.attacks.size() == quadratus_detail::kAttackChains.size());

}  // namespace encounters
