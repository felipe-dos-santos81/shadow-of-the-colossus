#pragma once

#include <array>

#include "../encounter_desc.h"
#include "../kirin_root.h"

// Phaedra (colossus 11 in the census, `KIRIN`, `c04_kirin.glb`, clip stem `kirin_a`,
// arena_04) — S13 Task 13, design D9/D10/D12. Data only: the runtime (her exported
// graph, routes, terrain, additive layer, the climb in the frame step) is Task 15's.
// Her `behaviour` is empty — the graph (`KIRIN.graph.json`) drives her — and so are
// `attacks` (her damage is the sidecar's rows, D11).
//
// Provenance tags on every value:
//   data <VA|path> — read from shipped files (glb names, .clb grip regions, cgpr
//                    records) or the definition tables in eboot.elf (cited by VA)
//   measured       — computed from the baked glb (FK, skinning) or the arena
//   authored       — the builder's pick, to be tuned on the user's walk
// Sources: .superpowers/sdd/2026-10-23-s13-recon/ kirin/findings.md ("KI": §1 joints,
// weak points, meshes; §3 the band table, stand f0: r50/r90, slopes), graph/findings.md
// ("GR" §3: weak points, HP, the neck trigger), clb/findings.md ("CL": grip regions by
// bone; her legs have none; tools/re/clb/boss_regions.py reproduces them).
//
// Frames (measured, KI): entity frame = glb frame, +z front, +x the model's LEFT, +y up;
// origin = the ground under the standing root (stand root y 9.86). Clips are 15 fps (data,
// KI §2) and named BY NAME (byte-sorted in the glb).

namespace encounters {
namespace kirin_detail {

// --- Strike targets -----------------------------------------------------------

namespace W {
enum : int { HeadSigil, NeckTrigger, Count };
}  // namespace W

// - The head sigil: material `weakpoint_01_int` (data: the glb's name for the
//   `weakpoint_01` shell, 690 v on Bone_HEAD, KI §1); reaction `add_dmg_head_min`
//   (data: motion-def[54] "Kirin_head_damage_(small)", additive).
// - The neck trigger: the `trigger_01` mesh (data, 120 v on KUBI/KUBI_B, the nape,
//   13.3 m standing, facing up; KI §1). A strike on it is a hit of region 0x20 fed to
//   the graph (the neck-down chain, D10) and deals no HP; it has no hurt clip of its own.
// weakpoint_02/03_int (the shoulder sigils, regions 0x70/0x90, pools 40) are live only
// in `kirin_second` (Hard, data 0x16c0850): unwired.
inline constexpr std::array<WeakpointDesc, W::Count> kWeakpoints{{
    {.material = "weakpoint_01_int", .reactionClip = "kirin_a_add_dmg_head_min"},
    {.material = "trigger_01", .reactionClip = nullptr},
}};

// Each target's damage region (data, `kirin_first` 0x16c08a0: row 0 head 0x10, row 14
// neck 0x20 `trigger_a_01`, +14 = 10, +90 -> "Giraffe;_neck_down_2_flag_Enable").
inline constexpr std::array<int, W::Count> kWeakpointRegion{{0x10, 0x20}};

// Each target's damage pool: head 160 (data, `kirin_first` row 0 +18 = 160, her HP);
// the trigger none (data: row 14 has no +18 pool).
inline constexpr std::array<float, W::Count> kWeakpointPool{{160.0f, 0.0f}};

// The hit kind a trigger strike reports: 0, the sword (data, GR §3: a hit on 0x20 by
// sword, harpoon or kind 4 starts the neck-down; the player's strike is the sword).
inline constexpr int kTriggerHitKind = 0;

// --- Climb bands (D10) --------------------------------------------------------

namespace B {
enum : int { TailStart, TailLoop, TailEnd, Back, Withers, NeckLow, NeckHigh, Head, Count };
}  // namespace B

// The chain (KI §3, stand f0, measured): tail (hang) -> hips/back (walk +15°) -> withers
// (walk +20.5°) -> neck low (hang +73°) -> neck high (+35°: walked; D8 hangs it when
// steeper) -> head (walk +7°, the sigil on top). Radii: the band's r50 (measured).
// Regions: CL, one per band (kBandRegion); her legs carry no grip face (data), so no band
// runs on a leg.
//
// The tail: Bone_TAIL (the tip, its LOW end) -> Bone_MATA_L (the rump, where the back
// band starts): the tail row (TAIL -> TAIL_A, r50 0.78, 67°) and the rump row (TAIL_A ->
// MATA_L, r50 1.0) as one tapered segment (authored), because the tail's own bones carry
// no grip face and 0x1e0 sits on KOSI/HARA/MATA_L (data, CL). TAIL_A lies 0.57 m (stand)
// to 0.92 m (watch_loop) off that axis (measured): a known simplification.
//
// The entry (D10, authored): E grabs the tail at its low end only while she crouches to
// peep (`watch_*`). The S12 fields carry one window per band, so the tail is three bands
// on one surface, one per watch clip. Each window = the clip's frames where the tail tip
// (Bone_TAIL) is at most 3.73 m above her ground — the player capsule top 1.7 + its radius
// 0.35 + the tail radius 0.78 + CombatTuning::grabRange 0.9 (measured per frame at the
// clips' own 15 fps: watch_start reaches it at f23 (3.56 m) and ends 1.68 m down;
// watch_loop 1.67–1.90 m throughout; watch_end leaves it after f17 (3.65 m; f18 3.75)).
// The windows are written in the runtime's frame convention, 30 fps (kWindowFps, ruling
// R-AD): the 15 fps bounds doubled — watch_start [46, end), watch_end [0, 36). carryFraction 1.0
// (authored): a window closing (watch_start -> watch_loop -> watch_end -> stand) never
// drops a rider — he keeps the tail and climbs to the back as usual. The back's `down` is
// None (authored): the tail is an entry, not a way down.
inline constexpr ClimbBand kTail{.boneA = "Bone_TAIL", .boneB = "Bone_MATA_L", .radius = 0.78f,
                                 .up = B::Back, .down = ClimbBandId::None, .weakpoint = -1,
                                 .name = "tail", .radiusB = 1.0f, .carryFraction = 1.0f,
                                 .grabEntryLowEnd = true};
constexpr ClimbBand tail_window(const char* clip, int from, int to) {
    ClimbBand band = kTail;
    band.enableClip = clip;
    band.enableFrom = from;
    band.enableTo = to;
    return band;
}

inline constexpr std::array<ClimbBand, B::Count> kBands{{
    tail_window("kirin_a_watch_start", 46, -1),
    tail_window("kirin_a_watch_loop", 0, -1),
    tail_window("kirin_a_watch_end", 0, 36),
    {.boneA = "Bone_MATA_L", .boneB = "Bone_MUNE", .radius = 1.86f, .up = B::Withers,
     .down = ClimbBandId::None, .weakpoint = -1, .horizontal = true, .name = "back"},
    {.boneA = "Bone_MUNE", .boneB = "Bone_KUBI", .radius = 2.29f, .up = B::NeckLow,
     .down = B::Back, .weakpoint = -1, .horizontal = true, .name = "withers"},
    // The trigger sits on this band: t 0.70, 0.87 m off the axis (measured, stand f0).
    {.boneA = "Bone_KUBI", .boneB = "Bone_KUBI_B", .radius = 1.74f, .up = B::NeckHigh,
     .down = B::Withers, .weakpoint = W::NeckTrigger, .name = "neck_low"},
    {.boneA = "Bone_KUBI_B", .boneB = "Bone_HEAD", .radius = 1.39f, .up = B::Head,
     .down = B::NeckLow, .weakpoint = -1, .horizontal = true, .name = "neck_high"},
    {.boneA = "Bone_HEAD", .boneB = "OptBone_Look", .radius = 1.18f, .up = ClimbBandId::None,
     .down = B::NeckHigh, .weakpoint = W::HeadSigil, .horizontal = true, .name = "head"},
}};

// The region (attr & 0x3f0) a player on each band touches: the graph's `player_surface`
// and shake-off rows read it. Codes: data (CL: 0x1e0 on MATA_L, 0x1a0 on MUNE, 0x60 on
// MUNE, 0x20 on KUBI/KUBI_B, 0x10 on HEAD); the one code per band where its bones carry
// several (back 0x1a0 of 0x1a0–0x1d0, withers 0x60 of 0x60/0x80): authored.
inline constexpr std::array<int, B::Count> kBandRegion{
    {0x1e0, 0x1e0, 0x1e0, 0x1a0, 0x60, 0x20, 0x20, 0x10}};

// The frame rate band windows are counted at: the runtime's convention (interactive.cpp:
// entityFrame = floor(clip time × 30); ruling R-AD), whatever the clip's own rate — hers
// are 15 fps (data, KI §2), so a 15 fps frame f is window frame 2f.
inline constexpr float kWindowFps = 30.0f;

// --- Collision ----------------------------------------------------------------

// The cgpr per-bone radii (data, KI §1 "B": HEAD 1.0, KUBI_B 1.1, KUBI 1.5, MUNE 2.5,
// HARA/KOSI 2.0, leg bones 1.0); the segments between her joints: authored. No tail
// capsule (no cgpr record): the tail is the climb entry.
inline constexpr std::array<CapsuleDesc, 17> kCapsules{{
    {.from = "Bone_MOMO_L", .to = "Bone_SUNE_L", .radius = 1.0f},
    {.from = "Bone_SUNE_L", .to = "Bone_B_TOE_L", .radius = 1.0f},
    {.from = "Bone_B_TOE_L", .to = "OptBone_IKFootBL", .radius = 1.0f},
    {.from = "Bone_MOMO_R", .to = "Bone_SUNE_R", .radius = 1.0f},
    {.from = "Bone_SUNE_R", .to = "Bone_B_TOE_R", .radius = 1.0f},
    {.from = "Bone_B_TOE_R", .to = "OptBone_IKFootBR", .radius = 1.0f},
    {.from = "Bone_JOWAN_L", .to = "Bone_UDE_L", .radius = 1.0f},
    {.from = "Bone_UDE_L", .to = "Bone_TOE_L", .radius = 1.0f},
    {.from = "Bone_TOE_L", .to = "OptBone_IKFootFL", .radius = 1.0f},
    {.from = "Bone_JOWAN_R", .to = "Bone_UDE_R", .radius = 1.0f},
    {.from = "Bone_UDE_R", .to = "Bone_TOE_R", .radius = 1.0f},
    {.from = "Bone_TOE_R", .to = "OptBone_IKFootFR", .radius = 1.0f},
    {.from = "Bone_KOSI", .to = "Bone_MUNE", .radius = 2.0f},
    {.from = "Bone_MUNE", .to = "Bone_KUBI", .radius = 2.5f},
    {.from = "Bone_KUBI", .to = "Bone_KUBI_B", .radius = 1.5f},
    {.from = "Bone_KUBI_B", .to = "Bone_HEAD", .radius = 1.1f},
    {.from = "Bone_HEAD", .to = "OptBone_Look", .radius = 1.0f},
}};

// --- Death (D12) --------------------------------------------------------------

// `kirin_a_death` plays once, then `death_loop` (1 frame, data: motion-def death ->
// death_loop, GR §3) is held. Neither has a root series: the root drops 9.86 -> 2.44 m
// over the death clip (measured, kirin_root.h).
inline constexpr const char* kDeathLoopClip = "kirin_a_death_loop";
inline constexpr kirin_root::DeathRoot kDeathRootY = kirin_root::kDeathRoot;

// --- The arena (D9) -----------------------------------------------------------

// What the EncounterDesc has no field for; Task 15 reads it. The entity and player
// placements are also composed into out/re/stage/arena_04.manifest (probe.py
// STAGES["arena_04"], the same values).
struct KirinArena {
    float spawnX, spawnZ, spawnGroundY;  // her placement
    float faceX, faceZ;                  // the point she faces at spawn
    float playerStartX, playerStartY, playerStartZ;
    // The backstop (D9): a player inside this box is returned to the start.
    float backstopMinX, backstopMaxX, backstopMinY, backstopMaxY, backstopMinZ, backstopMaxZ;
};
inline constexpr KirinArena kArena{
    // data-supported: `kirin_standby_col` spans x 1108–1131, z 140–158 (CL headline 9);
    // ground 53.76 m (measured, the manifest's entity line, R-Y; the boss-ground layer
    // reads y≈53.9 there, R-P).
    .spawnX = 1122.0f, .spawnZ = 147.0f, .spawnGroundY = 53.76f,
    // authored: facing the ruins' centre (yaw -93.04° in the manifest).
    .faceX = 990.0f, .faceZ = 140.0f,
    // data: the player start (arena04 recon §7), as the manifest's spawn line.
    .playerStartX = 984.0f, .playerStartY = 52.5f, .playerStartZ = 142.0f,
    // The north road's end, reached from the start (measured, Task 12's gate: nodes x
    // 986–994, z −19…−15, feet y 60.34–61.69), padded half a 1 m cell outward in x and
    // toward the map edge in z, and ±~2 m in y (authored). Unconditional (authored).
    .backstopMinX = 985.5f, .backstopMaxX = 994.5f,
    .backstopMinY = 58.0f, .backstopMaxY = 64.0f,
    .backstopMinZ = -19.5f, .backstopMaxZ = -15.0f,
};

// Whether a player whose feet are at (x, y, z) is inside the backstop box.
constexpr bool in_backstop(float x, float y, float z) {
    return x >= kArena.backstopMinX && x <= kArena.backstopMaxX && y >= kArena.backstopMinY &&
           y <= kArena.backstopMaxY && z >= kArena.backstopMinZ && z <= kArena.backstopMaxZ;
}

// A silent cue (no curated Phaedra audio map yet).
inline constexpr SoundCue kSilent{.clip = nullptr, .gain = 1.0f, .loop = false};

// The exported graph the fight runs: <SOTC_LIB>/behaviour/<kGraphBoss>.graph.json.
inline constexpr const char* kGraphBoss = "KIRIN";  // data: the eboot's boss name
// Her route graph (`make routes`) and arena terrain (`make terrain`), under <SOTC_LIB>.
inline constexpr const char* kRouteFile = "behaviour/kirin.route.json";  // data: kirin.path's stem
inline constexpr const char* kTerrainFile = "stage/arena_04.terrain.json";  // data: the stage's name

// --- The runtime (S13 Task 15, kirin_fight) -----------------------------------

// Her ground (ruling §13.2): the boss-ground layer under her root, else the layered
// heightmap's floor within this step; a move whose ground rises or sinks more than it in
// one frame is refused (authored, as Gaius's kGroundStep). Her nav graph's clear test
// walks a segment over that ground every kWalkSample m with the same rule (authored).
inline constexpr float kGroundStep = 2.0f;
inline constexpr float kWalkSample = 1.0f;

// A motion-graph edge's p34 is its blend in game frames at 30 fps (guess: Gaius's reading;
// her clips are 15 fps but the game steps at 30).
inline constexpr float kBlendFps = 30.0f;

// The skelton parent's hip (the judge volumes' hipY): Bone_KOSI, a child of Bone_root
// (data, the glb) — which of the root's two body children the game lifts by is a guess.
inline constexpr const char* kHipJoint = "Bone_KOSI";

// The sidecar's damage_def reactions (+0x18, raw): 4 = the IMPACT_CORE rows (60/30, the
// heaviest; data) read as a heavy hurt (authored, as Gaius's reading of 4). Her sidecar
// has no throw-off row (data: reactions 4, 5, 9, 10 only), so none releases a rider.
inline constexpr float kHeavyReaction = 4.0f;
inline constexpr float kNoThrowOffReaction = -1.0f;

// The rockfall feed (binary FUN_006fde60 / FUN_006fdec0, graph findings "Rockfall feed"): a
// stomp's ground-destruction spawn adds this to her rockfall timer (W'+0x4c84,
// ScrRuntime::bossTimer[0]) while armed (W'+0x4c88), capped.
inline constexpr float kRockfallFeedSeconds = 0.5f;
inline constexpr float kRockfallCapSeconds = 5.0f;
// Which fired effects are those spawns (ruling R-AJ, guess): the binary feeds from the
// KIRIN_GROUND_DEST_LWS rows, which her sidecar does not carry by name; the stomps' type-29
// effects (KIRIN_FOOT_ATK, KIRIN_FOOT_ATK_ST — data, c04_kirin.events.json) stand in for them,
// and her walk/turn/wait footsteps (KIRIN_FOOT, KIRIN_FOOT_B, KIRIN_WAIT_FOOT) do not feed.
inline constexpr const char* kRockfallEffectTag = "_ATK";

}  // namespace kirin_detail

inline constexpr EncounterDesc kKirin{
    .id = "kirin",
    // data: `kirin_first` 0x16c08a0 row 0 +18 = 160 (the head pool; GR §3).
    .maxHealth = 160.0f,
    // chest: Bone_MUNE (the camera orbit / tendril centre; authored as Gaius). feet: her
    // front IK feet (data: the stomp rows' aim bones, attackdata KIRIN 0x22329f8 rows 0/1).
    .joints = {.chest = "Bone_MUNE", .footL = "OptBone_IKFootFL", .footR = "OptBone_IKFootFR"},
    // No corpse swap (as S11/S12): death_loop is held where the root drop leaves her.
    .props = {},
    .weakpoints = kirin_detail::kWeakpoints,
    // A landed hit off both targets: the chest hurt clip (data: motion-def[55]
    // "Kirin_chest_(before)_damage"; additive; which hit plays it: authored).
    .bodyReactionClip = "kirin_a_add_mune_front_damage",
    .bands = kirin_detail::kBands,
    .capsules = kirin_detail::kCapsules,
    // measured: turn_l/turn_r turn +40.00°/−39.93° in 2.0 s (kirin_root.h) = 20 °/s.
    .turnDegPerSec = 20.0f,
    .attacks = {},
    // `kirin_a_death` 7.333 s (measured, 110 intervals at 15 fps). Tendril window and the
    // 8.0 s end: Valus's S6 values (authored, as Gaius). Camera keys: Gaius's (authored;
    // her body 16.4 m tall). One root: the head sigil. Fallback chest: the standing root
    // height 9.86 m (measured, KI).
    .death = {.clip = "kirin_a_death",
              .motionSeconds = 7.3333335f,
              .tendrilStartSeconds = 0.4f,
              .tendrilEndSeconds = 3.0f,
              .endSeconds = 8.0f,
              .roots = 1,
              .fallbackChestHeight = 9.86f,
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
    .cues = {.stompStart = kirin_detail::kSilent,
             .stompImpact = kirin_detail::kSilent,
             .clubStart = kirin_detail::kSilent,
             .clubImpact = kirin_detail::kSilent,
             .clubCancel = kirin_detail::kSilent,
             .reaction = kirin_detail::kSilent,
             .defeat = kirin_detail::kSilent,
             .footLeft = kirin_detail::kSilent,
             .footRight = kirin_detail::kSilent,
             .clubSwing = kirin_detail::kSilent,
             .music = nullptr,  // silent
             .musicGain = 0.0f},
    // authored: Gaius's framing scaled to her height (16.4 m vs 24.3 m).
    .targetCam = {.distanceMin = 22.0f,
                  .distanceMax = 36.0f,
                  .defaultDistance = 28.0f,
                  .defaultPitchDeg = 10.0f,
                  .pitchLimitDeg = 80.0f},
    // Empty: her exported mode graph drives her.
    .behaviour = {},
};

// Every link in range: the band chain (up/down/branch/carryTo), the bands' targets, the
// tail windows (enableTo < 0 = open to the clip's end).
constexpr bool kirin_links_valid() {
    namespace k = kirin_detail;
    const auto band = [](int i) { return i == ClimbBandId::None || (i >= 0 && i < k::B::Count); };
    for (const ClimbBand& b : k::kBands) {
        if (!band(b.up) || !band(b.down) || !band(b.branch) || !band(b.carryTo) ||
            !(b.weakpoint == -1 || (b.weakpoint >= 0 && b.weakpoint < k::W::Count))) {
            return false;
        }
        if (b.enableClip != nullptr && !(b.enableFrom >= 0 && (b.enableTo < 0 || b.enableFrom < b.enableTo))) {
            return false;
        }
    }
    return true;
}

static_assert(kirin_links_valid());

}  // namespace encounters
