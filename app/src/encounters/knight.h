#pragma once

#include <array>

#include "../encounter_desc.h"

// Gaius (colossus 3, `colossus_03_knight`, clip stem `knight_a`) — S12 Task 6,
// design D7/D9/D11/D12 and rulings §13.4/§13.7. Data only: the runtime (the
// exported mode graph, the events sidecar, the climb in the frame step) is
// Task 9's. His `behaviour` is empty — the graph (`KNIGHT.graph.json`) replaces
// it (D1) — and so are `attacks` (his damage is the sidecar's rows, D6).
//
// Provenance tags on every value:
//   data <VA> — read from shipped files (glb names, cgpr records) or from the
//               definition tables in eboot.elf (cited by VA)
//   measured  — computed from the baked glb / .anb root series / the arena glbs
//   R<n>      — the S12 RE gate's decode (docs/notes/s12-recon.md)
//   authored  — the builder's pick, to be tuned on the user's walk
// Sources: .superpowers/sdd/2026-10-23-s12-recon/sword/findings.md ("SW", the
// band chain), knight/behaviour.md + knight.json ("KN", HP, regions), arena03/
// ("AR"); the S12 c3 recon (2026-10-23-s10-s11-acceleration/s12-c3-recon/
// recon.md, "C3": materials, skeleton, capsules, placement).
//
// Frames (measured, C3 §2): entity frame = glb frame, +z front, +x the model's
// LEFT, +y up; origin = the ground under the stand root (root y 12.662).
// Clips are byte-sorted in the glb: everything here names clips BY NAME.

namespace encounters {
namespace knight_detail {

// --- Weakpoints ---------------------------------------------------------------

namespace W {
enum : int { HeadSigil, BellySigil, Count };
}  // namespace W

// Materials: data (C3 §2; the cgpr spots weak1_Spot -> Bone_KAO, weak2_Spot ->
// Bone_HARA). Reactions: data (KN regions, KNIGHT_ADD_CONTROL: 0x10 ->
// add_head_damage, 0x50 -> add_hara_front_damage); both are additive clips,
// dropped at runtime with no additive layer (ruling §13.7).
// weakpoint_03 (region 0x110, back of the left upper arm) is live only in
// `knight_second` (Hard, data 0x16c0b70): unwired.
inline constexpr std::array<WeakpointDesc, W::Count> kWeakpoints{{
    {.material = "weakpoint_01", .reactionClip = "knight_a_add_head_damage"},
    {.material = "weakpoint_02", .reactionClip = "knight_a_add_hara_front_damage"},
}};

// Each shell's damage region (data, `knight_first` 0x16c0bc0: head 0x10 index 1,
// belly 0x50 "MOUTH" index 2; both live from spawn, no gate, pool 86 each).
inline constexpr std::array<int, W::Count> kWeakpointRegion{{0x10, 0x50}};

// --- Climb bands (D7) ---------------------------------------------------------

namespace B {
enum : int { Blade, Forearm, UpperArm, Shoulder, Neck, Head, Count };
}  // namespace B

// The chain (SW "Proposed bands"; joints: data, C3 §2; radii, tapers and the
// walk/hang split: measured in the stuck hold, SW): blade (walked) -> forearm
// (walk) -> upper arm (hang, 49.5–64.1°) -> shoulder (walk) -> neck (hang) ->
// head (walk; weakpoint_01 0.83–0.97 m from its top). The belly sigil has no
// band yet (its route is Task 9's, design §8).
// The blade runs its LOW end (the tip, OptBone_IK_atk) -> the hilt
// (OptBone_sword, 0.058 m from the wrist, measured), so bandT >= 1 -
// carryFraction is the part next to his fist (grab.h band_low_end_t).
//   enable window: R3 decode — `knight_a_atk1_3` frames [9, 75): after the fling
//     volume (track 43, f0–8) until KNIGHT_PULLSWD fires at 75.0.
//   walkSpeed 5.0 m/s: authored (§13.4, the run speed, not the 2.2 m/s surface
//     walk). carryFraction 0.4: authored (§13.4, ruling R-J) — high on
//     the blade at the close, the player rides it up and climbs off onto the forearm.
//   grabEntryLowEnd: authored (D7 — the tilted disc leaves the low end 1.6–2.9 m
//     up, beyond the 1.0 m step: a grab, not the game's walk-on).
inline constexpr std::array<ClimbBand, B::Count> kBands{{
    {.boneA = "OptBone_IK_atk", .boneB = "OptBone_sword", .radius = 0.33f, .up = B::Forearm,
     .down = ClimbBandId::None, .weakpoint = -1, .horizontal = true, .name = "blade",
     .radiusB = 0.55f, .enableClip = "knight_a_atk1_3", .enableFrom = 9, .enableTo = 75,
     .walkSpeed = 5.0f, .carryFraction = 0.4f, .carryTo = B::Forearm, .grabEntryLowEnd = true},
    {.boneA = "Bone_HAND_A_R", .boneB = "Bone_UDE_R", .radius = 1.05f, .up = B::UpperArm,
     .down = B::Blade, .weakpoint = -1, .horizontal = true, .name = "forearm", .radiusB = 2.7f},
    {.boneA = "Bone_UDE_R", .boneB = "Bone_JOWA_R", .radius = 0.9f, .up = B::Shoulder,
     .down = B::Forearm, .weakpoint = -1, .name = "upper_arm", .radiusB = 1.75f},
    {.boneA = "Bone_JOWA_R", .boneB = "Bone_KATA_R", .radius = 2.0f, .up = B::Neck,
     .down = B::UpperArm, .weakpoint = -1, .horizontal = true, .name = "shoulder"},
    {.boneA = "Bone_KUBI", .boneB = "Bone_KAO", .radius = 1.9f, .up = B::Head, .down = B::Shoulder,
     .weakpoint = -1, .name = "neck"},
    {.boneA = "Bone_KAO", .boneB = "OptBone_Look", .radius = 1.45f, .up = ClimbBandId::None,
     .down = B::Neck, .weakpoint = W::HeadSigil, .horizontal = true, .name = "head"},
}};

// --- Collision ----------------------------------------------------------------

// No per-bone capsule block in his cgpr (data, C3 §4); radii are the collision
// proxy's per-bone p99 (measured, C3 json `body_capsules_candidate`). The
// candidate blade capsule (OptBone_sword -> IK_atk, r 1.1) is left out
// (authored): the blade is a climb surface, its low end the grab entry, and its
// hazard is the sidecar's damage rows (D6). The torso (12.7 m up) is gated out
// vertically for a player on the ground.
inline constexpr std::array<CapsuleDesc, 12> kCapsules{{
    {.from = "Bone_MOMO_L", .to = "Bone_SUNE_L", .radius = 2.0f},
    {.from = "Bone_SUNE_L", .to = "Bone_TOE_L", .radius = 1.93f},
    {.from = "Bone_MOMO_R", .to = "Bone_SUNE_R", .radius = 2.0f},
    {.from = "Bone_SUNE_R", .to = "Bone_TOE_R", .radius = 1.93f},
    {.from = "Bone_KOSI", .to = "Bone_MUNE", .radius = 3.2f},
    {.from = "Bone_MUNE", .to = "Bone_KUBI", .radius = 4.1f},
    {.from = "Bone_KUBI", .to = "Bone_KAO", .radius = 3.6f},
    {.from = "Bone_KAO", .to = "OptBone_Look", .radius = 2.6f},
    {.from = "Bone_JOWA_R", .to = "Bone_UDE_R", .radius = 3.0f},
    {.from = "Bone_UDE_R", .to = "Bone_HAND_A_R", .radius = 3.6f},
    {.from = "Bone_JOWA_L", .to = "Bone_UDE_L", .radius = 2.9f},
    {.from = "Bone_UDE_L", .to = "Bone_HAND_A_L", .radius = 1.6f},
}};

// --- The arena (D9) -----------------------------------------------------------

// What the EncounterDesc has no field for; Task 9 reads it. The entity and
// player placements are also composed into out/re/stage/arena_03.manifest
// (probe.py STAGES["arena_03"], the same values).
struct KnightArena {
    float spawnX, spawnZ, spawnGroundY;  // his placement
    float playerStartX, playerStartZ;    // the player's
    float boundCentreX, boundCentreZ, boundRadius;
    float fallBackstopY;
};
inline constexpr KnightArena kArena{
    // authored (builder pick, C3 §1: no spawn record in the data); ground measured
    // on the tower glbs (arena_03_disktop). He faces the player start (yaw −1.27°).
    .spawnX = -300.0f, .spawnZ = -1420.0f, .spawnGroundY = 47.657f,
    // data: misc/playerstartpositions.txt:4 (−301 44 −1375); ground 43.33 measured.
    .playerStartX = -301.0f, .playerStartZ = -1375.0f,
    // centre: data (the disc's records, C3 §1, e.g. "A03 Fog Platform"); the 36 m
    // clamp on his root: authored (D9; his graph never tests walkability, AR §3:
    // rim 47.5–53.0 m).
    .boundCentreX = -300.0f, .boundCentreZ = -1400.0f, .boundRadius = 36.0f,
    // authored (D9, AR §3): a player below y 34 is reset to the start (the disc
    // top is 40.1–50.6 m; past the lip the drop is 22–49 m).
    .fallBackstopY = 34.0f,
};

// A silent cue (D10: no curated Gaius audio map yet).
inline constexpr SoundCue kSilent{.clip = nullptr, .gain = 1.0f, .loop = false};

}  // namespace knight_detail

inline constexpr EncounterDesc kKnight{
    .id = "knight",
    // data: boss_param DEFAULT[2] @0x16d1f78 = {170, 255, …} via boss_type_def
    // 0x16d62f8 (Normal 170; 255 Hard).
    .maxHealth = 170.0f,
    // chest: Bone_MUNE (the camera orbit / tendril centre; authored as Quadratus).
    // feet: his IK feet (data, joint names; the sidecar's footstep spawns ride
    // the foot bones).
    .joints = {.chest = "Bone_MUNE", .footL = "OptBone_IKFootL", .footR = "OptBone_IKFootR"},
    // No weapon prop (the sword is his own mesh, rigid on HAND_A_R, C3 §2) and no
    // corpse swap: death holds its last frame where its root left him (D12).
    .props = {},
    .weakpoints = knight_detail::kWeakpoints,
    // A landed hit off both shells: the chest hurt clip (data, KN region 0x160
    // BREAST_R -> add_mune_damage); additive, dropped like the sigils' (§13.7).
    .bodyReactionClip = "knight_a_add_mune_damage",
    .bands = knight_detail::kBands,
    .capsules = knight_detail::kCapsules,
    // measured: turn_l/turn_r turn 90.01°/−89.98° in 40 frames at 30 fps (1.333 s)
    // = 67.5 °/s (knight_root.h kTurnHeading) — an aim target only.
    .turnDegPerSec = 67.5f,
    .attacks = {},
    // `knight_a_knight_death` (motion-def[15]): 180 intervals / 30 fps = 6.0 s
    // (measured); played once and HELD on its last frame (D12; death_loop has
    // no root series). Root travel (+3.91, −10.23, +5.28) m (measured, C3 §5).
    // Tendril window and the 8.0 s end: Valus's S6 values (authored, as for
    // Quadratus). Camera keys: Quadratus's (authored; his body 24.3 m tall vs
    // Quadratus's 26 m; the game's camera_knight_death_camera_01/02 are
    // undecoded). Two roots: the two sigils. Fallback chest: the stand root
    // height 12.662 m (measured, C3 §1).
    .death = {.clip = "knight_a_knight_death",
              .motionSeconds = 6.0f,
              .tendrilStartSeconds = 0.4f,
              .tendrilEndSeconds = 3.0f,
              .endSeconds = 8.0f,
              .roots = 2,
              .fallbackChestHeight = 12.662f,
              .camera = {.pullSeconds = 0.4f,
                         .lowSeconds = 3.0f,
                         .wideSeconds = 6.2f,
                         .followSeconds = 8.0f,
                         .pullDistance = 30.0f,
                         .pullEyeHeight = 8.0f,
                         .pullTargetHeight = 16.0f,
                         .pullPitchDeg = -15.5f,  // -asin((16 - 8) / 30)
                         .lowTargetHeight = 10.0f,
                         .lowDistance = 34.0f,
                         .lowPitchDeg = -12.0f,
                         .lowYawOffsetDeg = 25.0f,
                         .wideTargetHeight = 4.0f,
                         .wideDistance = 55.0f,
                         .widePitchDeg = 15.0f,
                         .wideYawOffsetDeg = 45.0f}},
    .cues = {.stompStart = knight_detail::kSilent,
             .stompImpact = knight_detail::kSilent,
             .clubStart = knight_detail::kSilent,
             .clubImpact = knight_detail::kSilent,
             .clubCancel = knight_detail::kSilent,
             .reaction = knight_detail::kSilent,
             .defeat = knight_detail::kSilent,
             .footLeft = knight_detail::kSilent,
             .footRight = knight_detail::kSilent,
             .clubSwing = knight_detail::kSilent,
             .music = nullptr,  // no music (D10)
             .musicGain = 0.0f},
    // authored: Quadratus's framing (a body of similar size: 24.3 m tall, C3 §2).
    .targetCam = {.distanceMin = 30.0f,
                  .distanceMax = 45.0f,
                  .defaultDistance = 38.0f,
                  .defaultPitchDeg = 10.0f,
                  .pitchLimitDeg = 80.0f},
    // Empty: his exported mode graph drives him (D1/D2).
    .behaviour = {},
};

// Every link in range: the band chain (up/down/branch/carryTo), the bands'
// shells, the blade's window. Gaius's behaviour is empty, so behaviour_valid
// does not apply.
constexpr bool knight_links_valid() {
    namespace k = knight_detail;
    const auto band = [](int i) { return i == ClimbBandId::None || (i >= 0 && i < k::B::Count); };
    for (const ClimbBand& b : k::kBands) {
        if (!band(b.up) || !band(b.down) || !band(b.branch) || !band(b.carryTo) ||
            !(b.weakpoint == -1 || (b.weakpoint >= 0 && b.weakpoint < k::W::Count))) {
            return false;
        }
        if (b.enableClip != nullptr && !(b.enableFrom >= 0 && b.enableFrom < b.enableTo)) {
            return false;
        }
    }
    return true;
}

static_assert(knight_links_valid());

}  // namespace encounters
