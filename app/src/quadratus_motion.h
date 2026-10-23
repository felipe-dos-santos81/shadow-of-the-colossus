#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include "math3d.h"
#include "player_state.h"
#include "scene.h"

// Quadratus's measured root motion and the hoof-hit rule (S11 Task 10b). Pure: no
// I/O, no clock; the root data is the generated `quadratus_root.h`, the hoof soles
// come from his loaded glb (tools/re/s11/
// quadratus_root.py; the recon is .superpowers/sdd/2026-10-23-s10-s11-acceleration/
// s11-quadratus-recon/clips.md). Clip names are the glb's (`boss_a_walk`, ...).

// The root travel of `clip` between clip times `t0` and `t1` (seconds since the clip
// started, unwrapped), in the clip-start frame: +z his front, +x his left. The bake
// zeroes the root's x/z in the glb, so this is the travel the entity must make itself;
// y is 0 here because the glb's own root track already poses it (never add it twice).
// A loop clip (the generated `loop` flag) wraps: each whole cycle adds its cycle
// delta (walk +20 m z, back -10 m z). A one-shot clamps to [0, length]. The caller
// rotates the delta by the yaw at the clip's start, not the live yaw (clips.md §1:
// yaw = deg, forward = (sin yaw, 0, cos yaw), so world = (x cos + z sin, -x sin + z cos)).
// A clip with no root series (quadratus_root::kNoRoot: death_loop, the add_*_foot
// damages, boot, ...), an unknown clip or a non-finite time answers 0.
Vec3 quadratus_root_delta(std::string_view clip, float t0, float t1);

// Whether `clip` carries a root table (false for kNoRoot and unknown clips).
bool quadratus_has_root(std::string_view clip);

// The yaw (deg, + = a left turn) a turn clip adds at its end: turn_l +45.00,
// turn_r -44.99, walk_turn_l +45.04, walk_turn_r -44.30 (measured, body axis). The
// turn lives in the bones, so the entity's yaw holds while the clip plays and takes
// this at the clip end. 0 for every other clip.
float quadratus_turn_yaw_deg(std::string_view clip);

// The four hooves, fl, fr, rl, rr (the left hooves are +x).
enum class Hoof { FrontLeft, FrontRight, RearLeft, RearRight };

// One hoof's sole, read from his glb at load (no mesh data is committed): the
// `colossus_02_hoof_weakpoint_<k>` vertices in their toe bone's local frame
// (inverseBind * position; the soles are skinned 100 % to Bone_TOE_L/R,
// Bone_B_TOE_L/R), and `restY`, their lowest model-space y at `boss_a_stand`
// frame 0 (clips.md: fl +0.007, fr -0.034, rl +0.072, rr +0.024).
struct HoofSole {
    std::vector<Vec3> points;
    float restY = 0.0f;
};
using HoofSoles = std::array<HoofSole, 4>;  // indexed by Hoof

// Fills `out` from rig `rigIndex` of a loaded scene. False — `out` untouched — when
// a toe bone, a sole material or `boss_a_stand` is missing, or a sole vertex is not
// fully skinned to its toe bone.
bool quadratus_hoof_soles(const Scene& scene, std::size_t rigIndex, HoofSoles& out);

// Authored — our approximation of the game's geometric reach, not a game rule: the
// game's hoof gate is data (a sword hit on the sole region, no lift test in the
// binary), and the sole faces down, so the sword only reaches it with the hoof up.
// This is the sole lift that stands in for that (clips.md §3). Fallback 0.3 m if
// the walk finds the rear windows too tight (stand rr 0.40 -> 0.60 s, turn rl/rr
// 0.27/0.47 -> 0.40/0.60 s).
inline constexpr float kHoofLiftMin = 0.5f;

// How far the sole is lifted (m): its lowest posed point (the sole's points through
// `toeWorld`, the toe bone's world matrix — anim.h's rig_bone_world) above `groundY`
// (the ground under the hoof), minus `restY`. NaN when the sole is empty or the
// answer is not finite.
float quadratus_hoof_lift(const HoofSole& sole, const Mat4& toeWorld, float groundY);

// The kneel a hoof hit starts: fl/fr -> boss_a_down_f_l/r_start, rl/rr ->
// boss_a_down_b_l/r_start.
const char* quadratus_hoof_kneel_clip(Hoof hoof);

// The hoof-hit rule (an authored approximation, clips.md §3), on the strike's release
// frame: a strike on the hoof's weakpoint (`weakpoint`, its index in the lookup — the
// caller pairs it with the same hoof's sole and toe matrix) kneels him — a Transition,
// no damage — when he is not `downOrRising`, the sole is lifted >= `liftMin` and
// strike_reaches_shell(feet, weakpoint, lookup) holds. `downOrRising` is the caller's
// behaviour flag: true in the sprain/down chain, rising, rising_return and death (the
// game checks the sprain transition in almost every other mode; s11-behaviour-re/
// findings.md §3). It is the only state input: a clip name cannot tell rising_return,
// which plays an ordinary clip, from the free states.
bool quadratus_hoof_kneels(const HoofSole& sole, const Mat4& toeWorld, float groundY,
                           bool downOrRising, Vec3 feet, int weakpoint,
                           const WeakpointProxyLookup& lookup, float liftMin = kHoofLiftMin);
