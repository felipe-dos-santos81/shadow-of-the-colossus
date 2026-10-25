#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "objanim_events.h"
#include "scr_graph.h"

// The shared predicate/action library every colossus's registry starts from (S13 design D5,
// Task 5): the 0x82…/0x83… handlers, perception, the anger model, the locomotion kinds
// −1/0/1/10, the attackdata runner on exported rows. Pure: no SDL, no GPU.
//
// Sources (labels on each value): S12's recon `graph/findings.md` §2-§4 and the RE note
// docs/notes/s12-recon.md (R1 the attack runner FUN_0085ab40 and the kinds, R5 the app bearing
// (+ = left) is minus the game's); S13's recon `graph/findings.md` §2 (the anger model
// FUN_0082aef0, hit_kind, hit_region, hp_pct_ge, boss_attr_bits) and tools/re/scr/predsem.py
// (the rest of the D5 batch, binary).

// The slowleg's request bits (R1 FUN_008166e0; the same script-ctrl-defs in every boss's export:
// KNIGHT and MAMMOTH map Turn_right 419 -> 48 … Sudden_braking 428 -> 57).
inline constexpr int kScrCtrlTurnRight = 48;      // Turn_right
inline constexpr int kScrCtrlTurnLeft = 49;       // Left_turn
inline constexpr int kScrCtrlAccel = 50;          // move code 0 'acceleration'
inline constexpr int kScrCtrlBackward = 52;       // move code 4
inline constexpr int kScrCtrlStopTurnRight = 55;  // move code 2 Stop_right_turn (426)
inline constexpr int kScrCtrlStopTurnLeft = 56;   // Stop_turn_left (427)
inline constexpr int kScrCtrlBrake = 57;          // move code 3 'Sudden_braking'

// Registers the shared handlers (gaps included, in `registry.gaps`) and the kinds −1/0/1/10.
// Leaves enterMode and locomotion to the caller.
void scr_library_add(ScrRegistry& registry);

// The new mode's slowstatus at a switch (findings §1 step 9; R1 "Arm"): the relposparam, the
// kind, +0c, the arrival radius and FUN_0085a960's attack arm. A registry's enterMode calls it.
void scr_library_enter_mode(int mode, ScrCtx& ctx);

// One frame of a boss: the walker's steps 2-5 (perception, the anger model, locomotion — kinds
// −1/0/1/10 here, any other through registry.locomotion — and kind 10's attack runner), then
// scr_step. The controls: locomotion/attack requests first.
ScrStepOut scr_boss_step(const ScrGraph& graph, const ScrRegistry& registry, ScrRuntime& runtime,
                         const ScrWorld& world, float dt);

// S12's slowleg gait toward `goal` (R1: steer bits 48/49, accel 50, stop-turn 55/56, brake 57).
// Arrived, it faces `goal`; the goal itself (equal to runtime.goal, or under his root) has no
// facing and he brakes (Task 8's hook hands it the goal when a kind sets no facing).
void scr_slowleg(ScrRuntime& runtime, const ScrWorld& world, const Vec3& goal, bool arrived, ScrStepOut& out);

// Binds every attackdata row's ATTAIN cylinder from the events sidecar's judge defaults: the
// `attain` volume's static key (parent skelton) turned R(180°) into his frame, attainX = −x,
// attainZ = −z, attainR = size.x (R4; S12's kAttack values). False with `error` naming a row
// whose `attain` the sidecar does not carry that way.
bool scr_attack_attain(ScrGraph& graph, const std::vector<EventsVolume>& judgeDefaults, std::string& error);

// Binds every mode's MOVE_BACK sector (ScrLocomotion::backRadius/backHalfDeg) from its slowstatus judge (+78)
// volume in the sidecar's judge defaults: radius size.x, half width angle / 2 (data; the centre offset is not
// read, as the library's Gaius sector). False with `error` naming a judge the sidecar lacks or does not carry
// as a static skelton volume yawed 180° (the sector behind him). S13 Task 14.
bool scr_move_back_bind(ScrGraph& graph, const std::vector<EventsVolume>& judgeDefaults, std::string& error);

// What a library handler reads that `args` lacks ("" when complete or `fn` is not the
// library's): a boss's check walks its graph with it, since the handlers read their slots unchecked.
std::string scr_library_missing_args(uint32_t fn, const std::vector<ScrArg>& args);

// What a boss's load-time check varies in the shared walk (scr_check_graph).
struct ScrCheckRules {
    bool additiveRows = false;    // the additive control-assign rows' control-defs too (a boss that plays the layer)
    bool optionalJudges = false;  // an empty attackdata FIX/ATK/ATTAIN name is allowed (else it must be a sidecar name)
    bool boundAttain = false;     // a row naming an ATTAIN volume must have its cylinder bound (scr_attack_attain ran)
    // The argument slots the boss's handlers read ("" when complete): the library's plus his own.
    std::string (*missingArgs)(uint32_t fn, const std::vector<ScrArg>& args) = scr_library_missing_args;
};

// The checks every boss's load-time check shares (S13 Task 14: scr_knight_check, scr_kirin_check): each
// control-assign row's control-def is exported (base rows; additive too per `rules`), each mode's attackdata
// handle is an exported table, each row has a motion and its judge volumes are in `judgeNames` (an ALT ATK when
// named), and every clause and action record carries the slots its handler reads. False with `error` naming it.
bool scr_check_graph(const ScrGraph& graph, const std::vector<std::string>& judgeNames, const ScrCheckRules& rules,
                     std::string& error);

// Fills distanceHorizontal, distance3d and bearingDeg (+ = his left) from the boss's and the
// player's positions and his yaw.
void scr_world_relate(ScrWorld& world);

// Perception, one frame (findings §2; FUN_0083aae0 near/seen, FUN_0083a6f0 latch).
void scr_perceive(ScrRuntime& runtime, const ScrWorld& world, float dt);

// FUN_008356b0 with the §13.3 stand-in: three rays from his root to forward·f ± side·s
// (scaled), each clear unless the ground, sampled every 1 m, rises > 1.0 m above his root's
// ground (walls block, drop-offs do not).
bool scr_swing_space_clear(const ScrWorld& world, float forward, float side, float scale);

// What `registry` can run, one line each (S13 design D13, the coverage report's input): every
// fn `<hex> implemented|gap` (sorted; the evaluator's own `sub` 832610 always), every locomotion
// kind `kind <n>` (sorted), every evaluator opcode `op 0x<hh> implemented|gap` (kScrCdefOps).
std::string scr_registry_dump(const ScrRegistry& registry);
