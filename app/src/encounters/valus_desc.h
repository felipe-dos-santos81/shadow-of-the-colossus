#pragma once

#include <array>

#include "../encounter_desc.h"

// Valus (colossus 1, `minotaur_a`): S4–S6's literals, moved here byte for byte
// in their old order (S11 Task 7, design D8) — band ties and capsule indices
// depend on the order. Sources: the band table (grab.cpp), the capsule proxy
// (collision.cpp), the attack constants (valus.h, S4.2 §7.4), the damages
// (player_state.h's CombatTuning), the clip and glb names (encounter_rt.h), the
// death timeline and cinematic (death.h, camera.h), the cue rows (audio_map.h),
// the target camera (camera.h) and the HUD's max health (app.h).
// test_encounter.cpp holds those literals as the oracle.

namespace encounters {
namespace valus_detail {

using B = ClimbBandId;

// The S4 band table. Radii are the S4 capsule measurements; the chain runs foot
// -> shin -> thigh -> waist -> chest -> neck -> head, with the right arm hanging
// off the chest. The chest carries the only branch: W plus any lateral input at
// its top (W+A too) crosses to the right shoulder (S4.2 design §7.3). Weakpoint
// 0 is the forehead, 1 the right arm.
inline constexpr std::array<ClimbBand, 9> kBands{{
    {"Bone_TUMASAKI_L", "Bone_TOE_L", 1.7f, B::LeftShin, B::None, -1, B::None, false, "left_foot"},
    {"Bone_TOE_L", "Bone_MOMO_L", 1.6f, B::LeftThigh, B::LeftFoot, -1, B::None, false, "left_shin"},
    {"Bone_MOMO_L", "Bone_KOSI", 1.7f, B::Waist, B::LeftShin, -1, B::None, false, "left_thigh"},
    {"Bone_KOSI", "Bone_HARA", 2.0f, B::Chest, B::LeftThigh, -1, B::None, false, "waist"},
    {"Bone_HARA", "Bone_MUNE", 2.0f, B::Neck, B::Waist, -1, B::RightShoulder, false, "chest"},
    {"Bone_MUNE", "Bone_KUBI", 1.6f, B::Head, B::Chest, -1, B::None, false, "neck"},
    {"Bone_KUBI", "Bone_KAO", 1.75f, B::None, B::Neck, 0, B::None, false, "head"},
    {"Bone_MUNE", "Bone_KATA_R", 1.5f, B::RightArm, B::Chest, -1, B::None, false, "right_shoulder"},
    {"Bone_KATA_R", "Bone_UDE_R", 1.2f, B::None, B::RightShoulder, 1, B::None, false, "right_arm"},
}};

// The collision proxy. Legs: hip -> knee -> ankle -> toe; torso: waist -> chest
// -> neck. Radii measured from the posed body mesh around each segment
// (dominant-weight vertices, p99/max: thigh 1.61/2.00, shin 1.49/1.56, foot
// 1.68/1.75; +0.35 m player at resolution).
inline constexpr std::array<CapsuleDesc, 8> kCapsules{{
    {"Bone_MOMO_L", "Bone_SUNE_L", 1.7f},     // left thigh
    {"Bone_SUNE_L", "Bone_TOE_L", 1.6f},      // left shin
    {"Bone_TOE_L", "Bone_TUMASAKI_L", 1.7f},  // left foot
    {"Bone_MOMO_R", "Bone_SUNE_R", 1.7f},     // right thigh
    {"Bone_SUNE_R", "Bone_TOE_R", 1.6f},      // right shin
    {"Bone_TOE_R", "Bone_TUMASAKI_R", 1.7f},  // right foot
    {"Bone_HARA", "Bone_MUNE", 2.0f},         // waist -> chest
    {"Bone_MUNE", "Bone_KUBI", 2.0f},         // chest -> neck
}};

// The club rides the rig's OptBone_sword (S4.2); the dead body and its
// dirtmound replace the live rig at the collapse (S6).
inline constexpr std::array<PropDesc, 3> kProps{{
    {PropRole::Weapon,
     "models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a_weapon.glb",
     "OptBone_sword"},
    {PropRole::Corpse,
     "models/shadow/characters/colossus_01_minotaur_a_deadbody/_cmn/c01_minotaur_a_dead_body.glb",
     nullptr},
    {PropRole::Ground,
     "models/shadow/characters/colossus_01_minotaur_a_deadbody/_cmn/"
     "c01_minotaur_a_dead_dirtmound.glb",
     nullptr},
}};

// The R3 reactions per shell: 0 the forehead, 1 the right arm.
inline constexpr std::array<WeakpointDesc, 2> kWeakpoints{{
    {"weakpoint_03", "minotaur_a_add_head_damage"},
    {"weakpoint_04", "minotaur_a_add_ude_r_damage"},
}};

// Indexed ValusAttack - 1 (StompL, StompR, Club; None has no row). The design's
// pinned numbers (§7.4); the club's swing phase is the S6 whoosh (tuned by ear
// against the 1.333 s clip); the damages are S4.2's (§7.5).
inline constexpr std::array<AttackDesc, 3> kAttacks{{
    {"minotaur_a_atk_leg_l", "Bone_TUMASAKI_L", 4.5f, -1.0f, 2.5f, 6.0f, 4.0f, 0.55f, 0.70f,
     -1.0f, 35.0f, 15.0f, false},
    {"minotaur_a_atk_leg_r", "Bone_TUMASAKI_R", 4.5f, -1.0f, 2.5f, 6.0f, 4.0f, 0.55f, 0.70f,
     -1.0f, 35.0f, 15.0f, false},
    {"minotaur_a_atk_weapon2_a", "OptBone_hit", 14.0f, 0.2f, 2.5f, 0.0f, 5.0f, 0.45f, 0.65f,
     0.30f, 45.0f, 0.0f, true},
}};

inline constexpr float kTurnDegPerSec = 30.0f;

// Valus as a one-state behaviour (S11 Task 8 proves it reproduces S4.2): the
// stand loop, the slew rate, the three attacks as the state's actions (clip
// indices into stand/stompL/stompR/club), both shells damage. Embedded, not yet
// driving the runtime (valus.cpp still does, from the same numbers).
inline constexpr std::array<BehaviourStateDesc, 1> kStates{
    {{.clip = 0, .turnRateDegPerSecond = kTurnDegPerSec, .actionCount = 3}}};
inline constexpr std::array<BehaviourAction, 3> kActions{{
    {1, kAttacks[0].cooldownSeconds},
    {2, kAttacks[1].cooldownSeconds},
    {3, kAttacks[2].cooldownSeconds},
}};
inline constexpr std::array<WeakpointGate, 2> kGates{{
    {kBehaviourNone, 0, GateKind::Damage, kBehaviourNone},
    {kBehaviourNone, 1, GateKind::Damage, kBehaviourNone},
}};

}  // namespace valus_detail

inline constexpr EncounterDesc kValus{
    .id = "valus",
    .maxHealth = 100.0f,
    .joints = {"Bone_MUNE", "Bone_TUMASAKI_L", "Bone_TUMASAKI_R"},
    .props = valus_detail::kProps,
    .weakpoints = valus_detail::kWeakpoints,
    .bodyReactionClip = "minotaur_a_add_mune_r_damage",
    .bands = valus_detail::kBands,
    .capsules = valus_detail::kCapsules,
    .turnDegPerSec = valus_detail::kTurnDegPerSec,
    .attacks = valus_detail::kAttacks,
    // The death clip's runtime duration is its maximum input time, 93 /
    // 15.08056640625 s (R8, S6); the tendril window, the 8.0 s end and the
    // cinematic keys are the builder's calls (S6 design §8.1/§9, §10). The
    // pull pitch puts the eye 4 m up: -asin((9 - 4) / 12).
    .death = {"minotaur_a_death_front", 6.166877f, 0.4f, 3.0f, 8.0f, 2, 8.0f,
              {0.4f, 3.0f, 6.2f, 8.0f, 12.0f, 4.0f, 9.0f, -24.6243f, 10.0f, 8.0f, -40.0f, 25.0f,
               2.0f, 24.0f, 12.0f, 45.0f}},
    .cues = {{"valus.telegraph", 0.60f, false},      // StompStart
             {"valus.impact", 0.50f, false},         // StompImpact
             {"valus.telegraph", 0.60f, false},      // ClubStart
             {"valus.impact", 0.50f, false},         // ClubImpact
             {nullptr, 1.00f, false},                // ClubCancel: deferred
             {"valus.hurt", 0.60f, false, false},    // ValusReaction
             {"valus.death", 0.80f, false, true},    // ValusDefeat: the S6 death roar
             {nullptr, 1.00f, false, false},         // FootLeft: deferred
             {nullptr, 1.00f, false, false},         // FootRight: deferred
             {"valus.club_swing", 0.70f, false, true},  // ClubSwing: the S6 club whoosh
             "music.boss",
             0.45f},
    .targetCam = {22.0f, 30.0f, 26.0f, 10.0f, 80.0f},
    .behaviour = {valus_detail::kStates, valus_detail::kActions, valus_detail::kGates},
};

static_assert(kValus.bands.size() == static_cast<std::size_t>(ClimbBandId::Count),
              "Valus's band table must cover exactly ClimbBandId's names");
static_assert(behaviour_valid(kValus.behaviour));

}  // namespace encounters
