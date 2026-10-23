#pragma once

#include <array>

// The shrine's authored and measured values, transcribed by hand (ruling R1:
// the app parses no JSON) from `tools/re/s7/shrine_placements.json` (Task 0,
// commit 2856683; `docs/notes/s7-recon.md` §2–§3). A JSON edit needs a matching
// edit here and a rebuild; `scripts/s7-acceptance.sh` cross-checks the printed
// probe coordinates against the JSON.

// `flow` (source: authored — gate R2 found no timings in the binary): the
// return's hold on the corpse, the fade out, the fade in, and the fade colour.
inline constexpr float kShrineHoldSeconds = 2.0f;
inline constexpr float kShrineFadeOutSeconds = 1.0f;
inline constexpr float kShrineFadeInSeconds = 1.5f;
inline constexpr std::array<float, 3> kShrineFadeColour{1.0f, 1.0f, 1.0f};

// `probes` (authored points; their floors are measured in s7-recon.md §3.4):
// XZ only, in the shrine glbs' frame.
struct ShrinePoint {
    float x, z;
};
inline constexpr ShrinePoint kProbeAltarStep{0.0f, 20.0f};
inline constexpr ShrinePoint kProbeHall{0.0f, -20.0f};
inline constexpr ShrinePoint kProbeStair{0.0f, -68.0f};
inline constexpr ShrinePoint kProbeWallFrom{0.0f, -13.0f};
inline constexpr ShrinePoint kProbeWallTo{14.0f, -13.0f};

// `wake` (source: data): the two baked clips the Wake phase plays in order, and
// the pelvis height above the pose's lowest body joint (`wake.pelvis_m`: 0.10 m
// lying, 0.72 m standing; 0.55 m at the cut04 -> cut05 boundary from the
// recon's §3.1 table). The clips carry no root series, so the pelvis sits at
// the model origin: the app plants the pose's lowest body joint on the floor
// (ruling R5), and --s7-selftest checks that plant against these three values.
inline constexpr const char* kWakeClipFirst = "player_a_home_boss01_cut04";
inline constexpr const char* kWakeClipSecond = "player_a_home_boss01_cut05";
inline constexpr float kWakePelvisLying = 0.10f;
inline constexpr float kWakePelvisKneeling = 0.55f;
inline constexpr float kWakePelvisStanding = 0.72f;
// The body joints the recon measured the lowest point over
// (`tools/re/anb/pose_dump.py` CORE_BONES + ARM_BONES; the helper/cloth joints
// hang below the body and are skipped).
inline constexpr std::array<const char*, 21> kWakeBodyJoints{
    "Bone_KOSI",   "Bone_MOMO_L", "Bone_SUNE_L", "Bone_TOE_L",    "Bone_TUMASAKI_L",
    "Bone_MOMO_R", "Bone_SUNE_R", "Bone_TOE_R",  "Bone_TUMASAKI_R", "Bone_HARA",
    "Bone_MUNE",   "Bone_KUBI",   "Bone_KAO",    "Bone_KATA_L",   "Bone_JOWA_L",
    "Bone_UDE_L",  "Bone_HAND_A_L", "Bone_KATA_R", "Bone_JOWA_R", "Bone_UDE_R",
    "Bone_HAND_A_R"};
