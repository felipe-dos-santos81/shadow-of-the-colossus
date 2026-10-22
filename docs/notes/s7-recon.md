# S7 — the RE gates: idols, the post-defeat flow, the wake (2026-10-22, Task 0)

| gate | verdict | what S7 adopts |
|---|---|---|
| **R1** idol placement | **PASS** (data, not code) | 16 positions + yaws + model refs from `cathedral_int.cgpr`; slot 1 broken = `Broke` + `skirt` records |
| **R2** post-defeat flow | **FALLBACK** | order and timings stay the design defaults (`hold 2.0 / fade-out 1.0 / fade-in 1.5 s`, white), `source: authored` |
| **R3** wake / spawn / probes | **PASS** (wake, spawn position) + authored (spawn yaw, Mono, probes) | wake = `player_a_home_boss01_cut04` + `cut05` (15.456 s); spawn = the `cathedral_int.sgpr` start in the glbs' frame |

The output is `tools/re/s7/shrine_placements.json`, composed from the shipped data by
`tools/re/s7/idols.py place`. Static RE only; read-only on the game tree, the library and
`out/re/eboot.elf`; the Ghidra runs used a **copy** of `out/re/ghidra` under the session
scratchpad (`-readOnly`), never the shared project.

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export SOTC_LIB=/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library
python3 -I tools/re/s7/idols.py xref out/re/eboot.elf          # R1: the string references
python3 -I tools/re/s7/wake_clip.py                             # R3: the 11 cuts, the wake chain
python3 -I tools/re/s7/idols.py place                           # writes shrine_placements.json
```

Validate the JSON (16 idols, slot 1 broken, Mono, spawn, wake, flow, probes; exit 0 + `OK`):

```bash
python3 -I -c "import json;d=json.load(open('tools/re/s7/shrine_placements.json'));I=d['idols'];assert [i['slot'] for i in I]==list(range(1,17)) and all(len(i['p'])==3 for i in I);assert d['broken']['slot']==1 and len(d['broken']['glbs'])==2;assert d['mono']['clip']=='girl_a_death_loop' and d['mono']['glb'].endswith('mono.glb');assert 8.5<=d['spawn']['p'][1]<=12.5;assert d['wake']['clips'] and d['wake']['seconds']>0;assert set(d['flow'])>={'hold_s','fade_out_s','fade_in_s','colour','source'};P=d['probes'];assert all(len(P[k])==2 for k in ('altar_step','hall','stair')) and set(P['wall'])=={'from','to'};print('OK idols=16 broken=1 mono spawn wake flow probes')"
```

**Conventions of the JSON.** Positions are metres in the cathedral glbs' frame. `yaw_deg` is the
app's +Y yaw (0 faces +Z, 90 faces +X; quaternion `(0, sin(y/2), 0, cos(y/2))` — `probe.py
yaw_quaternion`). `source` is `data` (read from shipped data records — a value added to the brief's
`binary|authored`; the idols use it), `binary` (none here) or `authored` (a builder value). Keys beyond the brief's schema are additive provenance: `idols[].record/offset`,
`broken.ps/yaw_deg/records`, `spawn.yaw_source/record`, `wake.root_series/pelvis_m/end_heading_deg`.

---

## 1 · R1 — idol placement: PASS (from data)

### 1.1 The binary: the names are game-state keys, not placements

`idols.py xref` (stdlib RIP-relative LEA/MOV scan + RELATIVE relocations) — each of the 32
`BossStatus_Alive_*` / `BossStatue_Whole_*` strings has **exactly one** code reference and no data
reference, all in one function:

| string family | string VAs | code refs | function |
|---|---|---|---|
| `BossStatus_Alive_<16 bosses>` | `0x101581a` … `0x1015987` | 16 (`0x539c85` … `0x53a3ba`) | `FUN_00539aa0` (18,292 B) |
| `BossStatue_Whole_<16 bosses>` | `0x101599d` … `0x1015b0a` | 16 (`0x53a435` … `0x53ab6a`) | `FUN_00539aa0` |
| `HOME_STATUE_COMMON` | `0x1039a6d` | 0 | data slot `0x1665e58` (a name table) |

Decompiled (`FindSampler.java decompile 0x539aa0`): `FUN_00539aa0` registers ~170 named game-state
variables with `FUN_004db2f0(DAT_02eff438, index, &name, callback)` — `GameState_*` 0–1,
`BossStatus_Alive_*` 0x02–0x11, **`BossStatue_Whole_*` 0x12–0x21** (callback `FUN_0053e9c0`),
`ItemCollected_*` 0x22–0x32, `ItemEquipped_*` 0x36–0x49, then the event names. No transform or
table is touched: the binary exposes "is idol N whole" to the world data; it does not place them.

### 1.2 The data: `cathedral_int.cgpr` places all 16

`scripts/shadow/world/cathedral/_cmn/cathedral_int.cgpr`: 208,413 B → **1,197,505 B** (Oodle
`8c0a`, via `tools/re/stage/spawn_probe.py`), **1,611** named records, every transform a proper
rotation (det 1.000). Per boss, in the registry's order, the file holds `<Boss>_Cutscene`,
`<Boss>_Statue_Whole`, `<Boss>_Statue_Broke`/`Statue_Broke`, `Statue_Broke_skirt`,
`Statue_Breaking`, the light/smoke/spark emitters. Each `_Statue_Whole` record's body carries one
`$/models/shadow/objects/actors/statues/****/<name>_<8hex>.cmdl` reference; `idols.py` maps it to the
converted `_cmn/<name>.glb` (suffix stripped, `****` → `_cmn`) and fails if the glb is missing.

| slot | record @offset | p (x, y, z) | yaw_deg | glb (`…/statues/_cmn/`) |
|---|---|---|---|---|
| 1 | `Minotar_A_Statue_Whole` @0xd189 | 14.000, 9.637, −3.060 | −90.000 | `mino_a_statue.glb` |
| 2 | `Mammoth_Statue_Whole` @0xf2b9 | −14.159, 9.592, −3.066 | 90.007 | `mammoth_statue.glb` |
| 3 | `Knight_Statue_Whole` @0x115c7 | 14.000, 9.637, −9.398 | −90.000 | `knight_statue.glb` |
| 4 | `Kirin_Statue_Whole` @0x13894 | −14.159, 9.592, −9.428 | 90.007 | `kirin_statue1.glb` |
| 5 | `Bird_Statue_Whole` @0x15b82 | 14.000, 9.637, −15.719 | −90.000 | `bird_statue.glb` |
| 6 | `MinotaurB_Statue_Whole` @0x17eb5 | −14.159, 9.592, −15.528 | 90.007 | `mino_b_statue.glb` |
| 7 | `Eel_Statue_Whole` @0x1a219 | 14.000, 9.637, −21.796 | −90.000 | `eel_statue.glb` |
| 8 | `Yamori_Statue_Whole` @0x1c67b | −14.159, 9.592, −21.888 | 90.007 | `colossus_08_lizard_statue.glb` |
| 9 | `Tortoise_Statue_Whole` @0x1ea1d | 14.000, 9.637, −28.061 | −90.000 | `colossus_09_tortoise_statue.glb` |
| 10 | `Narga_Statue_Whole` @0x20d96 | −14.159, 9.592, −28.060 | 90.007 | `sand_serpent_statue.glb` |
| 11 | `Leo_Statue_Whole` @0x230e1 | 14.000, 9.637, −34.401 | −90.000 | `lion_statue.glb` |
| 12 | `Poseidon_Statue_Whole` @0x253db | −14.159, 9.592, −34.406 | 90.007 | `lake_turtle_statue.glb` |
| 13 | `Snake_Statue_Whole` @0x2770b | 14.000, 9.637, −40.621 | −90.000 | `sky_serpent_statue.glb` |
| 14 | `Ceberus_Statue_Whole` @0x29a62 | −14.159, 9.592, −40.591 | 90.007 | `cerberus_statue.glb` |
| 15 | `MinotaurC_Statue_Whole` @0x2bd5e | 14.000, 9.637, −46.735 | −90.000 | `mino_c_statue.glb` |
| 16 | `Evis_Statue_Whole` @0x2e3c0 | −14.159, 9.592, −46.870 | 90.007 | `mega_maid_statue.glb` |

Slot 1 broken (`broken`): `Minotar_A_Statue_Broke` @0xd2fe at (14.000, 9.656, −3.107) →
`colossus_01_minotaur_a/_cmn/mino_a_statue_broke.glb`, `Statue_Broke_skirt` @0xd522 at
(14.000, 9.661, −3.107) → `…/mino_a_statue_broke_skirt.glb`, both yaw −90.000. (The records carry
two refs each, a hash-suffixed stub and the full name; the full-name ref is used.)

### 1.3 The frame and the yaw — measured, not assumed

- **Same frame as the glbs, no offset, no mirror.** The cgpr is in the cathedral glbs' frame: the
  idol bases (y 9.592/9.637) sit on the alcove floor measured in `cathedral_interior.glb` (9.62 at
  (14, −18.7)); the hall floor is 9.0 between them. X-mirror check on two asymmetric records of
  `cathedral_ext_shared.cgpr`: `SecretRoomDoor` (−77.68, 3.38, −66.31) has geometry at x −77.7
  (`cathedral_new_firstlevel.glb`, surfaces 0.23…7.43) and none at +77.7; `back_secret_path`
  (35.15, 55.81, −110.32) has surfaces 55.01–55.93 at +35.15 and none near 55.8 at −35.15.
- **Yaw.** The cgpr transform is row-vector (row 3 = translation), so row 0 is the image of local
  +X; for the app's +Y yaw θ that image is (cos θ, 0, −sin θ) → **θ = atan2(−row0.z, row0.x)**
  (spawn_probe's printed `yaw` is the negative). The statue glbs face +Z (bbox z −1.48…3.04 for
  `mino_a_statue.glb`, the front reaching +Z; `mammoth_statue.glb` head band +1.11 m in z). With
  θ = −90° the x=+14 idol occupies x 10.96…15.48, inside its alcove (walls at x ≈ 10 and ≈ 16 in
  the body band); the opposite sign puts it at 12.52…17.04, through the outer wall. The idols face
  the hall's centre line.

## 2 · R2 — the post-defeat flow: FALLBACK (timings authored)

Time-boxed. Decompiled on the scratch copy (`FindSampler.java decompile|callers`):

| address | what it is (measured) |
|---|---|
| `0x7df820` `FUN_007df820` | **not the post-defeat flow**: a one-shot (`DAT_02dcdf44` 0→1→2) that sets flag 45 (`FUN_007e6280/7e6510(0x2d)`), reads flags 217–232 (`HOME_ITEM*_GET_EVENT`, `FUN_007dbba0(0xd9…0xe8)`), clears all flags (`FUN_007e6b40`), re-sets the 217–232 that were set (`FUN_007dbb00`), and bumps a counter (`FUN_007dcfe0(n + 1)`) — a carry-over/reset path |
| `0x5707e0` `FUN_005707e0` (5,313 B) | the **flag-change hook**: for flags 217–232 it pushes 16 game-state handles (`DAT_02f12250` … `DAT_02f12340`, stride 0x10) through `FUN_0052ae40` — the idol state sync; other branches update further handles and `FUN_00574de0(0x16/0x17/0x1b)` |
| callers of `0x5707e0` (6) | `0x7db870` (script `SetGameFlag`: bitset `DAT_0325f3b0`, 0x27a = 634 flags, "Invalid GameFlag integer"), `0x7dbb00` (`SetFlag(n)`), `0x7dc020`, `0x7ddbb0`, `0x7ddc60`, `0x7ddeb0` |
| `0x1e9e50` `CMOLdrFadeToColor` | a **message-object loader** (data-driven): reads one property id `0x2e1d008a` into `+0x28`; no colour/duration constant in code |
| `0x302c60`, `0x33e1a0`, `0x33e810`, `0x4dec70` | 33–57 B wrappers around the `FadeToColor` / `FadeTransition` / `LoadWormholeFade*` names; no constants |

The property id `0x2e1d008a` occurs in **none** of the 342 shipped `.cgpr` bodies (LE or BE), and
`Fade` names occur only in `shadowmain`, `op1`, `ui/global`, `arena_10` — none on the
`DEAD_MINOTOUR_A` (45) → `MINOTOUR_A_HOME_END` (540) path. The flow is script-driven (the flags are
set through the script `SetGameFlag`), so its order and timings are not recoverable statically
within the box. **Adopted:** the design order `Hold → FadeOut → Load → FadeIn → Wake → Free`, hold
2.0 s, fade-out 1.0 s, fade-in 1.5 s, colour white `[1, 1, 1]` — `flow.source: authored`.

## 3 · R3 — the wake, the spawn, the probes

### 3.1 The wake: `cut04` + `cut05` — PASS (data)

`tools/re/s7/wake_clip.py` decodes the 11 cuts through `tools/re/anb/pose_dump.py` on
`wander.glb`. **None of the 11 carries a root position series** (parse2 `root_keys = None`), so a
bake leaves `Bone_root` at its bind translation (0, 0, 0) — the pelvis at the model origin. The
measure is therefore the pelvis (`Bone_KOSI`) height above the pose's lowest body joint
(`CORE_BONES + ARM_BONES`). Seconds = the baked duration `(frames − 1) / fps`, fps = the Q12 rate
field × 30 (`AnbReader.cs`, `GlbWriter.cs`).

| cut | frames | fps | s | keyed/29 | pelvis start → end (m) | head start → end (m) | |
|---|---|---|---|---|---|---|---|
| 01 | 104 | 30.000 | 3.433 | 0 | 0.10 → 0.10 | 0.06 → 0.06 | lying |
| 02 | 24 | 30.000 | 0.767 | 0 | 0.10 → 0.10 | 0.06 → 0.06 | lying |
| 03 | 34 | 30.000 | 1.100 | 3 | 0.10 → 0.10 | 0.06 → 0.06 | lying |
| **04** | 124 | 10.261 | **11.987** | 29 | **0.10 → 0.55** | 0.06 → 0.73 | rise (to kneeling) |
| **05** | 54 | 15.278 | **3.469** | 29 | **0.55 → 0.72** | 0.73 → 1.09 | rise (standing from frame 5) |
| 06 | 136 | 14.670 | 9.202 | 29 | 0.72 → 0.72 | 1.09 → 1.10 | standing |
| 07 | 201 | 9.932 | 20.138 | 29 | 0.71 → 0.72 | 1.08 → 1.09 | standing |
| 08 | 148 | 1.333 | 110.277 | 21 | 0.73 → 0.73 | 1.09 → 1.10 | standing |
| 09 | 76 | 10.239 | 7.325 | 16 | 0.73 → 0.73 | 1.10 → 1.11 | standing |
| 10 | 51 | 29.590 | 1.690 | 17 | 0.73 → 0.73 | 1.11 → 1.11 | standing |
| 11 | 26 | 1.406 | 17.778 | 17 | 0.73 → 0.73 | 1.11 → 1.10 | standing |

`summary cuts=11 frames=978 wake=player_a_home_boss01_cut04,player_a_home_boss01_cut05
wake_s=15.456 complete=True pelvis=0.10->0.72 end_heading_deg=2.5 stand_heading_deg=0.7`.
Lying = 0.10 m (≤ 0.4), standing = 0.72 m (the stand loop: 0.67 m). The wake ends with the feet
facing +Z (2.5°; the stand loop 0.7°), so the spawn yaw applies unrotated. While lying (cut01) the
body lies along the clip's Z axis, head toward +Z (`Bone_KAO` z +0.36, `Bone_TOE_L` z −0.69).
The rates are the converter's reading of the rate field (cut08/11 play at 1.3–1.4 fps); they are
what a bake will play. The JSON's `wake` object: `clips [cut04, cut05]`, `seconds 15.456`, `source: data`.

### 3.2 The spawn: the `cathedral_int.sgpr` start — PASS (position data, yaw authored)

`misc/playerstartpositions.txt` gives `world/cathedral/cathedral_int.sgpr` = (300, 10, −299) (world
frame). `home.cgpr` (16,935 B → 58,513 B, 86 records) has `level_setup` @0xda54 at
**(300.000, 0.071, −300.000)**; the cathedral's own `level_setup` is (0, 0, 0). Subtracting it:

| start record | world | → glb frame | measured surface | check |
|---|---|---|---|---|
| `cathedral_int.sgpr` | (300, 10, −299) | (0, 9.93, 1) | floor 8.998 (`cathedral_interior.glb`) | 0.93 m above the floor |
| `cathedral_garden.sgpr` | (310, 95, −340) | (10, 94.93, −40) | 92.70 (`cathedral_garden_ground.glb`) | 2.2 m above the ground |

Spawn = **(0.000, 8.998, 1.000)**, the floor under the start, `source: data`. Spawn floor band
**[8.5, 12.5] m** (ruling R6: the recon's 9–11 was an approximation; the hall floor measures 8.999). It is the hall's centre line, 21.4 m in front of the altar's face (z 22.41), under the
cgpr's `DorminLight1` (0, 58, 3) and next to `wander2dormin` (0, 9, 1.454). The start records carry
no yaw: **yaw 0 (facing +Z, the altar) is authored** (`yaw_source: authored`).

### 3.3 Mono — authored

The altar is `cathedral_interior.glb` mesh 21 (`lyr_temple_altar_01`), x −1.37…1.37, z 22.41…23.59,
top **11.302** at (0, 23); the dais trim (mesh 5) spans z 21.16…24.88 at y 10.54–10.70.
`girl_a_death_loop` (46 frames, no root series) decoded on `mono.glb` lies along the clip's X axis
(head −X at 0.43 m, feet +X at 0.81–0.93 m, all body joints within y −0.03…+0.12 of the pelvis), so
yaw 0 lays her along the altar's long (X) axis. `mono.p = (0, 11.402, 23)` = altar top + 0.10 m
(the pelvis joint above the back), `seconds 0.0`, `yaw 0`, **`source: authored`**. The bake keeps
`Bone_root` at bind (no root series), so the pose's pelvis is the glb origin.

### 3.4 The probes — authored points, measured floors

`cathedral_interior.glb` + `cathedral_interior_collision_stairs_player_collision.glb` (the floor),
every surface in the column:

| probe | xz | lowest surface | surfaces | column max | what it proves |
|---|---|---|---|---|---|
| `altar_step` | (0, 20) | 10.305 | 4 | 10.395 in these 2 glbs; the roof above is in others (`cathint_shadow` 32.03, `cathedral_new_firstlevel` 83.17) | the dais top step (z 16 → 20: 9.0 → 10.3) |
| `hall` | (0, −20) | 8.999 | 2 | 37.562 (interior); 109.98 with the garden glb | floor vs the hall roof / garden above |
| `stair` | (0, −68) | 9.789 | 16 | 89.221 | the back stair (z −64 → −72: 9.05 → 11.02) under stacked geometry |
| `wall` | (0, −13) → (14, −13) | — | to: 9.62, 10.49, 11.06, 11.83, … | — | ends inside the alcove divider between idols 3/5 (steep faces in the body band from x ≈ 9.4 to 16 at z −12…−14); the steep face is at x ≈ 9.33–9.39, so the walk must stop near x ≈ 9.3 |

## 4 · Files

| file | what |
|---|---|
| `tools/re/s7/idols.py` | `xref` (stdlib string → code/data references) and `place` (composes the JSON; reuses `tools/re/stage/spawn_probe.py` for the Oodle/cgpr/glb decode and `wake_clip.py`) |
| `tools/re/s7/wake_clip.py` | the 11-cut pelvis/head analysis and the wake chain |
| `tools/re/s7/shrine_placements.json` | the gate output (no game content: paths, numbers, record names) |

## 5 · Notes for the W1/W4 consumers

- The wake clips have **no root series**: baked, the pelvis stays at the model origin. Played at
  the spawn, nothing lifts the pelvis (in `player_a_stand` the root series supplies it, y ≈ 0.74);
  the app needs the pelvis lift from the pose (`wake.pelvis_m`: 0.10 → 0.72 m over the wake) or an
  equivalent foot plant, or the lying body sits wherever the model origin is placed. The same holds for Mono's `posed` sample (pelvis at the origin → `mono.p` already adds it).
- `cut04`'s rate is 10.261 fps (11.987 s for 124 frames); the S7 `Wake` phase should use the baked
  duration (15.456 s for both cuts).
- Measured idol heights: `mino_a_statue` 10.87, `mammoth_statue` 10.83, `knight_statue` 10.68 m;
  the hall ceiling over the centre line is ≈ 37.5 m.

## 6 · Bakes (Task 8, 2026-10-22)

`out/library` is shared by every branch, so the clip pins were relaxed on `main` first (`>= 15` plus
the 15 S2–S6 names in order; the root-series loop in `test_anim.cpp` covers only the first fifteen,
because the wake clips carry no root translation series). Then, from `sotc-native/` with
`SOTC_GAME_ROOT` and `SOTC_LIB=$PWD/out/library` exported and no acceptance running:

```bash
dotnet publish src/BpConvert.Cli -c Release -o out/bin
caffeinate -dimsu dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out "$SOTC_LIB" \
  --under models/shadow/characters/mono/_cmn/mono.cmsh --clips girl_a_death_loop --force
caffeinate -dimsu dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out "$SOTC_LIB" \
  --under models/shadow/characters/wander/_cmn/wander.cmsh --force --clips \
  player_a_stand,player_a_walk,player_a_run,player_a_grip_stand_crouch,player_a_grip_loop,\
player_a_grip_move_f1,player_a_grip_move_f2,player_a_grip_float_u_hi_attack,player_a_atk1,\
player_a_damage_m_fly2,player_a_fall_damage_to_death,player_a_damage_h_fly1,\
player_a_damage_h_fly_end,player_a_fall_damage_start,player_a_fall_damage_end,\
player_a_home_boss01_cut04,player_a_home_boss01_cut05
```

Resulting `clips.json` lists: `wander` — the 15 S2–S6 clips in their previous order, then
`player_a_home_boss01_cut04` (index 15) and `player_a_home_boss01_cut05` (index 16); `mono` —
`girl_a_death_loop` (index 0; `mono.glb` had no clips before; `mono_lod1/2.glb` are unchanged).
The shared library therefore changed in `wander.glb`, `wander.clips.json`, `mono.glb` and
`catalog.json`. Restore, if ever needed, by re-running the Wander command with only the first 15 names
and the Mono command without `--clips`.
