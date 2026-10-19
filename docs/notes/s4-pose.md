# S4.0 Task 1 — Valus's idle: bake, track mapping and pose record (2026-10-19)

The first colossus enters the arena. This note records the converter side of S4.0: the
`minotaur_a_stand` bake into `c01_minotaur_a.glb`, the 58-track → 238-joint mapping, the
bind-vs-bridged pose residuals, and the frame-0 **world** heights pinned by the new C# test
(the app-side test will reuse the same values).

**Status: the mapping and pose gate is MET.** No emission code changed (S2's `--clips` path and
`AnimBridge.ToGlb` are reused verbatim); the new C# tests are **existing-behavior pins** and run
green. The pose is upright and asymmetric in the authored way — head above the pelvis by
7.13 m, feet at ground, arms hanging from the shoulders — not folded or mirrored.

| | |
|---|---|
| Date | 2026-10-19 (local −03) |
| Machine | Apple M3 Max, macOS 27.0.1 (26A434), arm64 |
| Repo / branch | `sotc-native`, `s4-encounter` |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Inputs | `cp3shadow/anim/minotaur_a_stand.anb` (8,444 B; 26 frames, 58 tracks) + `cp3shadow/skel/minotaur_a.skb` (6,972 B; 73 names) + `models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.cmsh` |
| Input digests | `.anb` `e6253bcb651940209b3bb00fcdd993c68f3d151c6f12bb18677bb1d89dcaeae4`; `.skb` `1ff7171a5641b5976aab9144db5e73418ba9da4615f6bf7a6da491cd768b5abb` |
| Library (gitignored) | `out/library/models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb` |
| Artifacts (gitignored) | `out/re/s4/`: `minotaur-stand-decode-f0.json`, `minotaur-stand-pose-f0.json`, `minotaur-stand-measure.json`, `minotaur-stand-verify.json`, `minotaur-stand-stick.png`, `minotaur-stand-stick-back.png`, `player-stand-verify-regression.json` |
| Tool | `tools/re/anb/pose_dump.py` (decode / pose / render / measure / verify; `--glb` and `--skb` were already configurable — only the rig-generic paths below were added) |

## 1 · The bake

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
dotnet publish src/BpConvert.Cli -c Release -o out/bin
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.cmsh \
  --clips minotaur_a_stand --force
# ok models/.../c01_minotaur_a.cmsh -> .../c01_minotaur_a.glb (15574 ms)
# in scope: 1  skipped: 0  converted: 1  failed: 0  pending: 0
```

The catalog entry stays `ok` (`meshes` 12, `vertices` 476,666, `error` null).

| glb property | value |
|---|---|
| size | **38,090,040 B** (un-animated M0: 38,049,000 B; +41,040 B for the clip) |
| sha256 | **`398b6814fbb99bf6ef8c94578d7e2185af5ffcaa3be583c178adc08743943275`** |
| meshes / nodes / skins | 12 / 250 / 1 (238 joints) |
| animations | **1** — `minotaur_a_stand` |
| channels | **59** = **58 rotation + 1 translation** (`Bone_root`) |
| rotation keys | **21 static** (one key at t = 0) + **37 keyed** (26 keys each) |
| translation keys | 26 (`Bone_root`), x/z zeroed by the writer, y raw |
| duration | **1.6666666 s** = 25 / 15 (rate 0.5 → 15 fps) |
| interpolation | LINEAR everywhere; every rotation unit length |
| node form | the 58 rotation targets + `Bone_root` are SRT (59); the other 179 joints keep `matrix`; no `matrix` on an animated node |

Root series at frame 0: `(0.0276, 7.9896, −2.7842)`; y over the loop 7.7902 … 8.0318 (the
pelvis bob). The writer keeps only y (x/z are the controller's).

## 2 · The mapping

`minotaur_a.skb` is a **73-name part bank: 58 record-bearing core bones + 15 `OptBone_*`
joints**. The record run is `Bone_KOSI` … `Bone_OYA_C_R` (index 57); the optional joints run
`OptBone_IKFootR` … `OptBone_IKHandL`. Parents are sane and topological: two roots
(`Bone_KOSI`, `Bone_HARA`), the spine `HARA → MUNE → KUBI → KAO`, limbs under their parents;
every record-bearing parent index precedes its record, all 15 `OptBone_*` report −1.

`minotaur_a_stand` has 58 tracks (21 static type `0x00`; keyed: `0x03`×7, `0x04`×7, `0x05`×7,
`0x06`×3, `0x0D`×13). Track i is bank record i; matching the names against the converted glb:

- **58/58 tracks map, 0 unresolved**, to **58 distinct joints** of the **238-joint** glb.
- The **13 type-`0xD` tracks** (indices 2, 6, 15, 21, 29, 30, 32, 33, 35, 36, 38, 39, 42) are in
  the mapped set — e.g. `Bone_SUNE_L`/`_R`, `Bone_UDE_L`/`_R`, the left `KO_*`/`KUSURI_*`/
  `NAKA_*`/`HITOSASI_*` chains, and `Bone_KO_A_R`.
- C# pins: `SkbReaderTests.Minotaur_bank_decodes`,
  `TrackMapTests.Minotaur_stand_tracks_all_resolve_into_valus_glb`,
  `AnimWriterTests.Minotaur_stand_animation_reloads_with_pinned_shape`.

## 3 · The bridge and the residuals

The S2 convention is unchanged and transfers to Valus: the tool's bridged local is
`conj_i(anb) = (x, −y, −z, w)` composed as a **row-vector** matrix; the writer emits the
column-vector inverse `(−x, y, z, w)` into the glb. Evidence on this rig (frame 0):

| check | measured | gate |
|---|---|---|
| `conj_i(.skb q2)` vs the glb bind local, all 58 records | **max 12.90°** (`Bone_KO_A_L`); 54/58 ≤ 1°; all ≤ 20° | ≤ 20° |
| composed bind vs the glb world matrices | **1.07e-14** | ≤ 1e-4 |
| adjacent-key dot (925 pairs) | **min 0.9995** | ≥ 0.95 |
| keyed track lengths | 26/26 on all 37 | = frames |

The residual is the angle between the bridged decoded local and the glb's bind local, per bone.
`f0` is the written frame-0 value; the band is min…max across the 26-frame loop.

**Core chain (legs/spine/head)**

| bone | type | f0 | loop band |
|---|---|---|---|
| `Bone_KOSI` | 0x04 | 2.93 | 2.93 … 7.67 |
| `Bone_MOMO_L` | 0x06 | 20.95 | 20.68 … 28.18 |
| `Bone_SUNE_L` | 0x0D | 44.66 | 43.35 … 53.31 |
| `Bone_TOE_L` | 0x03 | 27.61 | 27.08 … 37.80 |
| `Bone_TUMASAKI_L` | static | 0.00 | 0.00 |
| `Bone_MOMO_R` | 0x04 | 11.26 | 10.54 … 13.31 |
| `Bone_SUNE_R` | 0x0D | 35.91 | 33.38 … 40.84 |
| `Bone_TOE_R` | 0x03 | 27.50 | 26.57 … 33.08 |
| `Bone_TUMASAKI_R` | static | 0.00 | 0.00 |
| `Bone_HARA` | 0x05 | 15.27 | 13.08 … 15.27 |
| `Bone_MUNE` | 0x05 | 25.07 | 24.62 … 25.43 |
| `Bone_KUBI` | 0x05 | 1.29 | 0.94 … 12.73 |
| `Bone_KAO` | 0x05 | 4.52 | 2.79 … 11.68 |

**Arm chain**

| bone | type | f0 | loop band |
|---|---|---|---|
| `Bone_KATA_L` | 0x05 | 25.20 | 24.75 … 27.14 |
| `Bone_JOWA_L` | 0x04 | 58.49 | 57.60 … 59.27 |
| `Bone_UDE_L` | 0x0D | 44.77 | 31.99 … 44.86 |
| `Bone_HAND_A_L` | 0x05 | 30.75 | 28.91 … 34.87 |
| `Bone_KATA_R` | 0x06 | 10.19 | 5.86 … 10.58 |
| `Bone_JOWA_R` | 0x05 | **86.18** | 84.55 … 87.56 |
| `Bone_UDE_R` | 0x0D | 24.87 | 16.58 … 26.92 |
| `Bone_HAND_A_R` | 0x03 | 18.64 | 18.41 … 23.49 |

**Head/face statics and the secondary chains (the remaining 37 bones)**

| bone | type | f0 | loop band |
|---|---|---|---|
| `Bone_MABUTA_L` / `Bone_EYE_L` | static | 0.00 | 0.00 |
| `Bone_MABUTA_R` / `Bone_EYE_R` | static | 0.00 | 0.00 |
| `Bone_KE_A` | 0x04 | 0.26 | 0.26 … 13.35 |
| `Bone_KE_B` | 0x04 | 5.85 | 1.10 … 10.79 |
| `Bone_KE_C` | 0x06 | 0.00 | 0.00 |
| `Bone_KO_A_L` | 0x03 | 53.89 | 53.89 … 57.52 |
| `Bone_KO_B_L` | 0x0D | 81.56 | 81.56 … 87.38 |
| `Bone_KO_C_L` | 0x0D | 52.43 | 52.43 … 56.17 |
| `Bone_KUSURI_A_L` | 0x03 | 55.22 | 55.22 … 63.74 |
| `Bone_KUSURI_B_L` | 0x0D | 68.64 | 68.64 … 79.36 |
| `Bone_KUSURI_C_L` | 0x0D | 75.51 | 75.51 … 87.29 |
| `Bone_NAKA_A_L` | 0x03 | 36.77 | 36.77 … 45.56 |
| `Bone_NAKA_B_L` | 0x0D | 53.07 | 53.07 … 65.77 |
| `Bone_NAKA_C_L` | 0x0D | 44.90 | 44.90 … 55.65 |
| `Bone_HITOSASI_A_L` | 0x04 | 43.36 | 43.36 … 48.54 |
| `Bone_HITOSASI_B_L` | 0x0D | 61.02 | 61.02 … 69.74 |
| `Bone_HITOSASI_C_L` | 0x0D | 51.63 | 51.63 … 59.01 |
| `Bone_OYA_A_L` | 0x03 | 8.62 | 8.62 … 13.70 |
| `Bone_OYA_B_L` | 0x04 | 18.19 | 18.19 … 24.65 |
| `Bone_OYA_C_L` | 0x0D | 4.06 | 0.08 … 4.06 |
| `Bone_KO_A_R` | static | 30.13 | 30.13 |
| `Bone_KO_B_R` | static | 52.75 | 52.75 |
| `Bone_KO_C_R` | static | 34.84 | 34.84 |
| `Bone_KUSURI_A_R` | static | 50.08 | 50.08 |
| `Bone_KUSURI_B_R` | static | 61.55 | 61.55 |
| `Bone_KUSURI_C_R` | static | 57.38 | 57.38 |
| `Bone_NAKA_A_R` | static | 54.35 | 54.35 |
| `Bone_NAKA_B_R` | static | 60.39 | 60.39 |
| `Bone_NAKA_C_R` | static | 42.27 | 42.27 |
| `Bone_HITOSASI_A_R` | static | 37.38 | 37.38 |
| `Bone_HITOSASI_B_R` | static | 52.88 | 52.88 |
| `Bone_HITOSASI_C_R` | static | 52.88 | 52.88 |
| `Bone_OYA_A_R` | static | 33.15 | 33.15 |
| `Bone_OYA_B_R` | static | 10.76 | 10.76 |
| `Bone_OYA_C_R` | static | 16.00 | 16.00 |

Family bands: **core 0.00 … 44.66°**, **arm 10.19 … 86.18°**, **all 58 bones 0.00 … 86.18°**.

**Reading the numbers.** These are distances from a T-pose bind, not convention errors: the
legs bend into a relaxed stance (knees 33–53°), the arms come down from the horizontal bind
(`JOWA_R` 86° is the upper arm at its most relaxed; the left side 58°), and the secondary chains
(`KO_*`, `KUSURI_*`, `NAKA_*`, `HITOSASI_*`, `OYA_*`) sit mostly 30–87° off their straight bind.
The right-side secondary chains are **static** while their left twins are keyed — consistent
with authored asymmetry, not a mapping slip: the same names, the same bind cross-file check
(54/58 records ≤ 1°), and a pose that is upright and mirror-consistent where the data is. No
player band is applied: the S2 bands (core ≤ 40°, arm ≤ 60°) were calibrated on Wander's
proportions and would mislabel Valus's longer, heavier arms.

## 4 · The frame-0 world pose (the values the app test pins)

`pose_dump.py pose --frame 0` composes the row-vector world pose; the C# pin
(`AnimWriterTests.Minotaur_stand_world_pose_puts_the_head_above_the_pelvis_at_the_decoded_heights`)
composes the **written glb** with column-vector node semantics and asserts the same heights
±0.02 m, so a future bridge/transpose slip shows as a height shift.

| joint | world x | **y (pinned)** | z |
|---|---|---|---|
| `Bone_KOSI` (pelvis) | +0.0276 | **7.9896** | −2.7842 |
| `Bone_HARA` | +0.0276 | 7.9896 | −2.7842 |
| `Bone_MUNE` | +0.1126 | **10.9251** | −3.3973 |
| `Bone_KUBI` | +0.1163 | **14.6764** | −2.3309 |
| `Bone_KAO` (head) | −0.6353 | **15.1153** | +0.1266 |
| `Bone_MOMO_L` / `_R` | +1.3885 / −1.3722 | 7.4016 / 7.5147 | −2.7276 / −2.5595 |
| `Bone_SUNE_L` / `_R` | +1.8767 / −1.6485 | 3.9192 / 3.7939 | −1.2090 / −1.6931 |
| `Bone_TOE_L` / `_R` | +1.8486 / −1.5894 | 2.1200 / 2.0622 | −2.7546 / −3.3132 |
| `Bone_TUMASAKI_L` (toe tip) | +1.8518 | **0.0565** | −2.5343 |
| `Bone_TUMASAKI_R` (toe tip, **lowest body joint**) | −1.5893 | **−0.0031** | −3.1095 |
| `Bone_HAND_A_L` / `_R` | +5.2676 / −4.1089 | 4.7655 / 5.4457 | −2.2477 / −4.1246 |

- **Head above the pelvis by 7.1257 m**; total height (`KAO` − lowest foot) **15.1184 m**.
- **Feet lowest**: among the `Bone_*` body joints the minimum frame-0 y is `Bone_TUMASAKI_R`
  (−0.0031). The overall minimum is `OptBone_atk_weapon` (−0.7437), an optional helper joint,
  not a body bone — the generic verify and the C# pin scope the check to `Bone_*`.
- Stance: toe tips 3.4893 m apart (x 3.4411, 0.231·height), z stagger 0.5752 m (right back).
- Hands hang from the shoulder joints (y 14.22 / 14.45) down to y 4.77 / 5.45
  (0.31–0.36·height) — the long-armed colossus silhouette, outside Wander's absolute-metre hand
  band by design.
- Loop invariants: head−pelvis stays 6.9763 … 7.1257 m; feet stay −0.0032 … 0.0566 m; root y
  7.7902 … 8.0318; every world translation finite (max |coordinate| 15.1475 m). **No frame is
  folded or mirrored.**
- The written glb evaluates identically: SharpGLTF's `GetWorldMatrix(animation, 0)` agrees with
  the manual column-vector composition within 1e-3 m on `Bone_KOSI`, `Bone_KAO` and
  `Bone_TUMASAKI_R`.

## 5 · The still

The tool's own path (no engine render):

```bash
python3 tools/re/anb/pose_dump.py render cp3shadow/anim/minotaur_a_stand.anb \
  --glb out/library/models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb \
  --frames 0 --view back --out out/re/s4/minotaur-stand-stick-back.png
python3 tools/re/anb/pose_dump.py render … --frames 0,6,12 --view side \
  --out out/re/s4/minotaur-stand-stick.png
```

`minotaur-stand-stick-back.png` (frame 0, back view) reads **upright**: `KAO` at the top, the
spine down through `KUBI`/`MUNE` to `HARA`/`KOSI`, both legs to the toe tips, and both arms
hanging from the shoulders outward and down, ending at about mid-thigh. `minotaur-stand-stick.png`
shows frames 0/6/12 from the side moving through the idle's small weight shift, with the head
staying at the top in every tile. This is the observation the milestone needed: **head above the
pelvis, feet lowest, no fold.**

## 6 · Verification and reproduce

```bash
# the filtered converter pins (4 new tests + the existing classes)
dotnet test tests/BpConvert.Tests -v q --filter "SkbReader|TrackMap|AnimWriter"
# Aprovado! — failed 0, passed 21, skipped 0, total 21

# the full converter suite (step 5)
dotnet test tests/BpConvert.Tests -v q
# Aprovado! — failed 0, passed 90, skipped 0, total 90 (49 s)

# the pose tool: player regression (the S2 bands are untouched)
python3 tools/re/anb/pose_dump.py verify
# 17/17 assertions pass (Bone_KAO 1.117, Bone_KOSI 0.743 — the S2 values)

# the pose tool: Valus, model-neutral checks + reported bands
python3 tools/re/anb/pose_dump.py verify --profile generic \
  --file cp3shadow/anim/minotaur_a_stand.anb \
  --glb out/library/models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb
# 9/9 assertions pass; bands and loop closure printed
```

The generic profile is new (the default `player` path is unchanged): a non-player rig has no
finger tracks and no player absolute-metre bands, so `verify` can now run the decode/bind/bridge
checks, uprightness, feet-lowest and finiteness, and **report** the residual bands and loop
closure instead of crashing or gating them on Wander's calibration. `measure` likewise now
skips bones a rig does not have instead of raising. The player assertions are unchanged
(17/17; the only added output is the `profile: player` stamp); the player `measure` still reads
`Bone_KOSI` 0.7429, `Bone_KAO` 1.1165, hands 0.6269/0.6243.

## 7 · What this note does and does not establish

- **Establishes:** the 58 tracks map 1:1 into the 238-joint glb; the S2 bridge convention holds
  cross-file on Valus (54/58 records ≤ 1°); the written glb's frame-0 world pose is upright at
  the decoded heights; the bake is one 26-frame, 59-channel animation of 1.6667 s.
- **Does not establish:** a capture match. **No Valus capture exists** (S2's xval was
  player-only), so this is decode-vs-bind + internal geometry + the rig's semantics, not a
  silhouette comparison. The phase-free caveat of `re2-xval.md` does not even arise here.
- The clip is effectively loop-closed but not byte-closed: **31/37 keyed tracks** have identical
  first/last key bytes; the worst first-to-last rotation is **0.0286°** (`Bone_KUBI`), i.e.
  quantization, not a discontinuity. The app may loop it without a visible snap.
- The 58-track body clips animate none of the banner's 38 joints (recon §1) — the banner stays in
  its bind pose; that is unchanged by this bake.
- `OptBone_atk_weapon` sits 0.74 m below the feet; it is an optional helper (no geometry of its
  own in this check) and is excluded from the feet-lowest assertion.
- No emission code, reader, or quaternion convention was changed to make this pass.
