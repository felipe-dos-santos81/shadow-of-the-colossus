#pragma once

#include <string>
#include <vector>

#include "scr_graph.h"

// Gaius's (`KNIGHT`) half of the mode-graph interpreter (S12 Task 5, design §13):
// his 23 predicates (the 24th, `sub`, is the evaluator's own) and 18 action kinds,
// perception, his locomotion kinds 0/1/10 and the attackdata runner, and the
// control -> clip resolution through the exported motion graph. Pure: no SDL, no GPU.
//
// Sources (labels on each value): the recon `graph/findings.md` §2-§4 and
// `predicates.json` (binary), `knight/behaviour.md` (data/binary), and the RE note
// docs/notes/s12-recon.md: R1 (the attack runner FUN_0085ab40 and the kinds), R2
// (control-def conditions: a row fires on its control bit OR its condition; the last
// firing row wins), R5 (the app bearing (+ = left) is minus the game's).
//
// A frame: fill the ScrWorld (scr_world_relate for the distances and bearing), call
// scr_knight_step, then resolve the requested controls with scr_control_clip.

namespace scr_knight {

// Control bits (data: KNIGHT's control-assign rows, script-ctrl-def +8 & 63; the
// unit test checks each against its row's orient in the export).
inline constexpr int kCtrlRightFoot = 2;    // Issue_knight_right_foot_attack
inline constexpr int kCtrlLeftFoot = 3;     // Issue_knight_left_foot_attack
inline constexpr int kCtrlSlam = 4;         // Issue_knight_sword_attack
inline constexpr int kCtrlStab = 5;         // Knight_sword_attack_2_issue
inline constexpr int kCtrlRockHead = 8;     // 'Head_swing'
inline constexpr int kCtrlRockBody = 9;     // 'Body_swing'
inline constexpr int kCtrlRockArmR = 10;    // 'Right_arm_swing'
inline constexpr int kCtrlRockArmL = 11;    // 'Left_arm_swing'
inline constexpr int kCtrlForcedStop = 12;  // Knight_forced_stop 'Force_wait'
inline constexpr int kCtrlTurnRight = 48;   // the slowleg's steer bits (R1 FUN_008166e0)
inline constexpr int kCtrlTurnLeft = 49;
inline constexpr int kCtrlAccel = 50;       // move code 0 'acceleration'
inline constexpr int kCtrlBackward = 52;    // move code 4
inline constexpr int kCtrlStopTurnRight = 55;  // move code 2
inline constexpr int kCtrlStopTurnLeft = 56;
inline constexpr int kCtrlBrake = 57;       // move code 3 'Sudden_braking'

// One `KNIGHT_SWORD` scr_attackdata row (data 0x2232a98, R1 "Row numbers"). Every
// row: sensing 0, condition null, control-mode list null, chain percent 100, no chain.
struct AttackRow {
    const char* fix;     // +50 FIX volume (judge names as the sidecar spells them)
    const char* atk;     // +60 ATK volume
    const char* attain;  // +90 ATTAIN volume
    int issue;           // +a0 issue control bit
    const char* motion;  // +c0 the row's motion (clip stem)
    float reselect;      // +34 s
    float cooldownS;     // +f4 s
    bool issueArrived;   // +f8: issue during the cooldown once arrived
    bool isDefault;      // +30
    float attainX, attainZ, attainR;  // the ATTAIN cylinder in his frame (glb x, z; data t44)
};

// Rows 0..7; rows 3 and 7 are the tables' terminators (fix == nullptr; the runner walks a
// table to its terminator, so mode 10's table [4] needs row 7 — S12 Task 9 fix: with 7
// rows its walk read past the array). Mode 9 arms [0], mode 10 [4].
inline constexpr AttackRow kAttack[8] = {
    {"KNIGHT_LEG_L_ATK_FIX", "KNIGHT_LEG_L_ATK", "KNIGHT_LEG_L_ATK_ATTAIN", kCtrlLeftFoot,
     "knight_a_atk_leg_l_start", 5.0f, 15.0f, false, false, 3.0f, 7.0f, 2.0f},
    {"KNIGHT_LEG_R_ATK_FIX", "KNIGHT_LEG_R_ATK", "KNIGHT_LEG_R_ATK_ATTAIN", kCtrlRightFoot,
     "knight_a_atk_leg_r_start", 5.0f, 15.0f, false, false, -3.0f, 7.0f, 2.0f},
    {"KNIGHT_WEAPON_ATK_FIX", "KNIGHT_WEAPON_ATK", "KNIGHT_WEAPON_ATK_ATTAIN", kCtrlSlam,
     "knight_a_atk1_start", 15.0f, 10.0f, true, true, 0.0f, 30.0f, 5.0f},
    {nullptr, nullptr, nullptr, -1, nullptr, 0.0f, 0.0f, false, false, 0.0f, 0.0f, 0.0f},
    {"KNIGHT_LEG_L_ATK_FIX", "KNIGHT_LEG_L_ATK", "KNIGHT_LEG_L_ATK_ATTAIN", kCtrlLeftFoot,
     "knight_a_atk_leg_l_start", 5.0f, 15.0f, false, false, 3.0f, 7.0f, 2.0f},
    {"KNIGHT_LEG_R_ATK_FIX", "KNIGHT_LEG_R_ATK", "KNIGHT_LEG_R_ATK_ATTAIN", kCtrlRightFoot,
     "knight_a_atk_leg_r_start", 5.0f, 15.0f, false, false, -3.0f, 7.0f, 2.0f},
    {"KNIGHT_WEAPON_ATK2_FIX", "KNIGHT_WEAPON_ATK2", "KNIGHT_WEAPON_ATK2_ATTAIN", kCtrlStab,
     "knight_a_atk2_start", 15.0f, 8.0f, true, true, 0.0f, 16.0f, 4.0f},
    {nullptr, nullptr, nullptr, -1, nullptr, 0.0f, 0.0f, false, false, 0.0f, 0.0f, 0.0f},
};

}  // namespace scr_knight

// Exactly KNIGHT's predicates (minus `sub`), his action kinds and the mode-entry hook
// that applies a mode's slowstatus (perception params, locomotion kind, the attack arm).
ScrRegistry scr_registry_knight();

// The load-time checks the strict loader cannot make (the caller — Task 9 — must call it
// right after scr_graph_load: the handlers dereference the argument slots it proves): every base control-assign
// condition is one R2 decoded, every mode's attackdata is a table kAttack carries,
// every row's motion is a clip of the graph, every argument slot the handlers read is
// present, and every judge volume kAttack names is in `judgeNames` (the sidecar's: an
// unknown name would answer false at run time — R-B). False with `error` naming it.
bool scr_knight_check(const ScrGraph& graph, const std::vector<std::string>& judgeNames, std::string& error);

// The index of clip `name` in the graph's clip names, or -1.
int scr_clip(const ScrGraph& graph, const std::string& name);

// Fills distanceHorizontal, distance3d and bearingDeg (+ = his left) from the boss's
// and the player's positions and his yaw.
void scr_world_relate(ScrWorld& world);

// Perception, one frame (findings §2; FUN_0083aae0 near/seen, FUN_0083a6f0 latch).
void scr_perceive(ScrRuntime& rt, const ScrWorld& world, float dt);

// FUN_008356b0 with the §13.3 stand-in: three rays from his root to forward·f ± side·s
// (scaled), each clear unless the ground, sampled every 1 m, rises > 1.0 m above his
// root's ground (walls block, drop-offs do not).
bool scr_swing_space_clear(const ScrWorld& world, float forward, float side, float scale);

// One frame of Gaius: perception, his locomotion kind and attack runner (the walker's
// steps 2-3), then scr_step. The controls: locomotion/attack requests first.
ScrStepOut scr_knight_step(const ScrGraph& graph, const ScrRegistry& registry, ScrRuntime& rt,
                           const ScrWorld& world, float dt);

// The clip the requested controls lead to from the boss's current motion
// (world.bossMotion) in his control-mode (world.bossControlMode): the control-mode's
// base rows fire on a requested bit or their control-def (R2), the last firing row
// wins, and its orient's motion-graph edge from the current motion (else an any-motion
// edge: the null-`from` always table) gives the clip. Additive rows are dropped (§13.7).
// -1 when nothing changes: no firing row, no edge, or a self-edge (the playing clip keeps
// playing — restarting it would freeze him on its first frame). On a new clip,
// rt.controlMode = the row's next control-mode and rt.controlEdge = the edge taken, whose
// p34 is the blend in frames (data reading: 10 by default, 20 stand -> stomp, 5 from the
// failure clips — behaviour.md) and whose p30 is -1 on edges taken at the clip's end (the
// else/chain edges; a reading: every edge out of a turn clip has p30 -1).
//
// For the caller (Task 9), the turn overshoot: the stop-turn clips (turn_r/turn_l) rotate a
// fixed ~90° and every edge out of them, turn -> turn and turn -> walk_start included, is
// p30 -1, so honouring p30 lets one turn carry him past a 10° error and the next frame's
// gait asks for the opposite turn. The gait stops asking once |rt.steerDeg| <= 3° (R1's
// gate), so the caller should cut a playing turn (stop its root yaw, or leave it early)
// when |rt.steerDeg| falls inside 3° rather than play the whole clip.
int scr_control_clip(const ScrGraph& graph, ScrRuntime& rt, const std::vector<int>& ctrls, const ScrWorld& world);
int scr_control_clip(const ScrGraph& graph, ScrRuntime& rt, int ctrl, const ScrWorld& world);
