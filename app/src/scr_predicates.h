#pragma once

#include <string>
#include <vector>

#include "scr_graph.h"
#include "scr_library.h"

// Gaius's (`KNIGHT`) half of the mode-graph interpreter (S12 Task 5, design §13; S13 Task 5
// moved the shared handlers, perception, locomotion and the attackdata runner to scr_library):
// his boss-local predicate and action kinds, his registry and load-time check, and the
// control -> clip resolution through the exported motion graph. Pure: no SDL, no GPU.
//
// Sources (labels on each value): the recon `graph/findings.md` §2-§4 and
// `predicates.json` (binary), `knight/behaviour.md` (data/binary), and the RE note
// docs/notes/s12-recon.md: R2 (control-def conditions: a row fires on its control bit OR its
// condition; the last firing row wins).
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
inline constexpr int kCtrlTurnRight = kScrCtrlTurnRight;  // the slowleg's (scr_library.h)
inline constexpr int kCtrlTurnLeft = kScrCtrlTurnLeft;
inline constexpr int kCtrlAccel = kScrCtrlAccel;
inline constexpr int kCtrlBackward = kScrCtrlBackward;
inline constexpr int kCtrlStopTurnRight = kScrCtrlStopTurnRight;
inline constexpr int kCtrlStopTurnLeft = kScrCtrlStopTurnLeft;
inline constexpr int kCtrlBrake = kScrCtrlBrake;

}  // namespace scr_knight

// The shared library (scr_library_add) plus his boss-local predicate and action kinds; the
// mode-entry hook is scr_library_enter_mode (perception params, locomotion kind, the attack arm).
ScrRegistry scr_registry_knight();

// The load-time checks the strict loader cannot make (the caller — Task 9 — must call it
// right after scr_graph_load: the handlers dereference the argument slots it proves): every base
// control-assign condition is exported (scr_cdef_eval reads its records), every mode's attackdata
// handle is an exported table, every row's motion is a clip of the graph, every argument slot the
// handlers read is present (scr_library_missing_args and his own), and every judge volume a row
// names is in `judgeNames` (the sidecar's: an unknown name would answer false at run time — R-B).
// False with `error` naming it.
bool scr_knight_check(const ScrGraph& graph, const std::vector<std::string>& judgeNames, std::string& error);

// The index of clip `name` in the graph's clip names, or -1.
int scr_clip(const ScrGraph& graph, const std::string& name);

// One frame of Gaius: scr_boss_step.
ScrStepOut scr_knight_step(const ScrGraph& graph, const ScrRegistry& registry, ScrRuntime& rt,
                           const ScrWorld& world, float dt);

// The clip the requested controls lead to from the boss's current motion
// (world.bossMotion) in his control-mode (world.bossControlMode): the control-mode's
// base rows fire on a requested bit or their control-def (R2; scr_cdef_eval), the last firing row
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
