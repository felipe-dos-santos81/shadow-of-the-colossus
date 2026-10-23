# S11 acceptance results (2026-10-23) — Quadratus

Spec milestone S11 (`docs/superpowers/specs/2026-10-23-s10-s11-acceleration-design.md`, goals G5/G6,
decisions D8–D10, rulings §13.10–13.14): the second colossus, Quadratus (`colossus_02_bull_a`, stem
`boss_a`), in his own arena (`arena_02`). Stab a lifted rear hoof and he kneels; climb the fur of the
kneeling leg; strike the back sigil; walk his back and hold through the shake-off; climb the neck and
strike the head sigil; he dies and his body stays; `R` resets. His fight is silent (D10).

**Status: implemented; the scripted acceptance is PASS. The user's walk is PENDING (§11).**
`scripts/s11-acceptance.sh` runs green end to end (exit 0). It covers:
- the inputs, checked before anything runs: the game tree, the library with Quadratus's bake (his
  `clips.json` holds `boss_a_stand`/`death`/`down_b_l_start`), Wander, and `out/re/stage/arena_02.manifest`
  (composed when absent; it must say `encounter quadratus`);
- the **frozen S9 acceptance first** (`scripts/s9-acceptance.sh`, unchanged: `make verify-long`'s chain
  before S11). It builds the converter and the app, runs both suites, and runs S8 → S7 → S6 → S5 → S4.2 →
  S4.1 → S4 → S3 inside it;
- five flag refusals (exit 2);
- the deterministic `s11-*` proof (`--s11-selftest --s11-stills`), parsed line by line;
- four 2560×1440 stills with pixel-span checks;
- the end-to-end loop (`--s11-e2e`): one arena load, no shrine load, its lines equal to the selftest's.

The script does not judge whether the fight feels like the game's. That is the user's walk (§11).

| | |
|---|---|
| Date | 2026-10-23 local (−03). The recorded run: stamped `2026-10-23T17:19:47Z`–`17:34:29Z` (wall 882 s = 14 min 42 s). The S9 chain inside it took 824 s (`17:19:47Z`–`17:33:31Z`); the S11 part 58 s. |
| Machine | Apple M3 Max, macOS 27.0.1, arm64. 1280×720 points → 2560×1440 px captures. |
| Repo / branch | `sotc-native`, branch `s11-accept` (worktree `wt-s11-accept`) off main `3044b2b`. Script `818abd5`; goldens `b9505bc`. |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Library (gitignored) | `SOTC_LIB=<repo>/out/library`, the base. Quadratus's bake (his glb + 58 `boss_a_*` clips) was promoted into it at the S11 lane merge (ruling A16: `make bake-quadratus` into the base, 24 s; only his glb, his `clips.json` and `catalog.json` changed; 43 rewritten PNGs byte-identical). `make bake` now includes it. |
| arena_02 manifest (gitignored) | `out/re/stage/arena_02.manifest` (`probe.py compose --stage arena_02`, `make arena02-manifest`): 32 instances (19 world-frame glbs + 13 csml-placed bridge pieces), `encounter quadratus`; the viewer's scene is 214 meshes. |
| Acceptance script | **`scripts/s11-acceptance.sh`** (bash 3.2); `make accept` / `make accept-s11` |
| Run log (gitignored) | `out/s11-acceptance-run.log`, which ends `s11-acceptance: PASS —` |
| S9 regression log (gitignored) | `out/s11-s9-regression.log`, which ends `s9-acceptance: PASS —` |

## 1 · The acceptance run

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export SOTC_LIB=<repo>/out/library
caffeinate -dimsu ./scripts/s11-acceptance.sh    # exit 0
```

- **The S9 chain** ran unchanged and printed its own `s9-acceptance: PASS —` (824 s): builds, `dotnet
  test` and `ctest` (2/2), the S8 chain (S7 → … → S3) inside it, the S9 decode (36 entries), the composes,
  the road manifest `3af63a18…` (12/12 gating assertions). Its outputs are not re-asserted here.
- **The S11 part** (58 s): the five refusals, the selftest with stills, the e2e, the summary. The script
  ended `s11-acceptance: PASS — …` (exit 0).

- **The flag refusals.** Each exits 2 before any load: `--s11-stills` without `--s11-selftest`;
  `--s11-selftest` with `--s11-e2e`; `--s11-e2e` with `--stage shrine`; `--s11-selftest` without
  `--manifest` (each with an `--s11-*` message); `--stage plain` with `--s11-selftest` (main.cpp's general
  rule, "--stage plain is an interactive start", meets it first).
- **The runner.** `tools/verify/s11.sh` is now the second copy of the script's gates, like s3…s9
  (`test_coverage.py`: s11 61/61 present, 4 excluded — the nested S9 run, its PASS line, the viewer build;
  7 viewer runs). The s11 goldens (7 files) are blessed (§10).

## 2 · The deterministic proof (`--s11-selftest`)

Fixed 1/60 s steps with a scripted player who acts like one watching the game: walk at him and step out of
his stomp, stab a rear hoof while it is lifted, circle to the kneeling leg, climb, strike, rest on the back
when the grip runs low, hold through the shake, climb the neck and strike the head. The lines (one run; a
second run and both e2e runs equal them, timings aside):

```
s11-load stage=arena encounter=quadratus meshes=214 ground=1111x688 layers=64 entity=240.00,-96.74,-925.00 yaw=21.68 spawn=273.00,-98.31,-842.00 return=0 ms=979.6
s11-attack chain=attack_r state=AttackRStart dist=24.9 bearing=-0.4 at=5.9
s11-charge chain=attack_r dodged=1 impact_dist=27.5 player_hp=100 at=9.7
s11-hoof weakpoint=colossus_02_hoof_weakpoint_rl lift=0.59 stabs=1 state=DownBLStart damage=0 health=220 at=20.0
s11-grab band=rear_left_hoof state=DownBLLoop stamina=100.0 at=25.7
  (climb: rear_left_hoof → shin 29.1 → thigh 32.0 → rump 36.7 → back 40.9; the rump/back boundary crossed
   back and forth 40.9–48.1 s — the script's coordinate descent and its rest on the back)
s11-strike weakpoint=weakpoint_05 band=back gate=damage damage=34 health=186 killed=0 at=49.7
s11-strike weakpoint=weakpoint_05 band=rump gate=damage damage=34 health=152 killed=0 at=50.7
s11-strike weakpoint=weakpoint_05 band=rump gate=damage damage=34 health=118 killed=0 at=52.8
s11-strike weakpoint=weakpoint_05 band=rump gate=damage damage=8 health=110 killed=0 at=53.8
s11-sigil done=back strikes=5 health=110 at=53.8
s11-back band=back t=0.45 across=0.00 feet_y=20.85 standing=1 stamina=100.0 state=Stand at=57.9
s11-shake start state=RefuseShoulder band=back stamina=100.0 at=65.9
s11-shake survived=1 band=back low=58.5 stamina=80.2 at=74.6
  (climb: neck 79.4 → head 84.0)
s11-strike weakpoint=weakpoint_04 band=head gate=damage damage=34 health=76 killed=0 at=84.9
s11-strike weakpoint=weakpoint_04 band=head gate=damage damage=34 health=42 killed=0 at=85.9
s11-strike weakpoint=weakpoint_04 band=head gate=damage damage=34 health=8 killed=0 at=86.9
s11-strike weakpoint=weakpoint_04 band=head gate=damage damage=8 health=0 killed=1 at=87.8
s11-death phase=Motion total=0.000 state=Death
s11-death phase=Collapse total=5.333 state=Death
s11-death phase=Done total=8.000 state=Death
s11-corpse live_body=1 clip=boss_a_death t=5.333 held=1 moved=17.50 yaw=66.68 return=Idle
s11-reset health=220 defeated=0 state=Stand at_base=1 player_hp=100 mode=grounded death=Idle open=weakpoint_05
s11-perf frames=240 frame_ms=8.296 drawn=98 culled=116
s11-status phases_done=11 frames=5751 seconds=95.9 health=220
s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 frames=3
```

| Line | Gate (the script and `s11.sh`) | Measured |
|---|---|---|
| order | load, attack, charge, hoof, grab, sigil, back, shake ×2, death ×3, corpse, reset, perf, status — exactly | as gated |
| `s11-load` | `stage=arena encounter=quadratus`, a layered ground (`layers=64`), `return=0` | 214 meshes; ground 1111×688 nodes (248 MB, bounded by the ground meshes' XZ extent); **851.6–1073.7 ms over ten runs (§5)** |
| `s11-charge` | `dodged=1`, `player_hp=100` | his rear-up stomp lands 27.5 m from the player, 3.8 s after its start |
| `s11-hoof` | a rear hoof (`_rl`/`_rr`), `DownB[LR]Start`, `damage=0 health=220` | the rear-left sole 0.59 m up (rule 0.5 m) on the first stab |
| `s11-grab` | in `DownB[LR](Start\|Loop)` | the hoof band at 25.7 s, stamina 100 |
| `s11-sigil` | `done=back`, `health=110` | 5 strikes (34 + 34 + 34 + 8 capped at the half) |
| `s11-back` | `band=back`, `standing=1` | feet y 20.85 m at t 0.45 |
| `s11-shake` | `survived=1 band=back` | `refuse_shoulder` at 65.9 s; stamina low 58.5 |
| strikes | back sigil (`weakpoint_05`) strictly before the head's (`weakpoint_04`); no `sealed`/`ignore`/`transition`; the last `health=0 killed=1` | 5 + 4 |
| `s11-death` | `Motion Collapse Done` | 0.000 / 5.333 / 8.000 s |
| `s11-corpse` | `live_body=1 clip=boss_a_death t=5.333 held=1 moved=17.x … return=Idle` | the death clip's root carries him 17.50 m; no shrine (returnWired off, §13.13) |
| `s11-reset` | exact | — |
| `s11-perf` | 240 frames, `0 < frame_ms <= 16.7` | **8.289–8.355 ms over five runs (§5)** (drawn 98, culled 116) |
| `s11-status` | `phases_done=11` | 5,751 frames, 95.9 s simulated |
| `s1-status` | `windowed=1 relative_mouse=0 mouse_auto_capture=0` | as gated |

## 3 · The end-to-end loop (`--s11-e2e`)

The same script through the interactive loop (`run_interactive`; its `R` is the key handler's,
`press_r_key`). Gated: exit 0; one `s11e2e-done`; an `s1-status`; **one `s7-load` line, `stage=arena`** —
no shrine load after his death; the `s11e2e-*` lines equal the selftest's `s11-*` lines with `load`,
`perf`, `status` and `done` aside. Measured: `s11e2e-done frames=5751 seconds=95.9`, load 905.5 ms.

## 4 · The stills

Each is one `s1-screenshot` line, PNG magic, > 5,000 bytes, and a channel span ≥ 30 (the viewer's
`s1-pixels` line).

| Still | Bytes | Span | Shows (Task 10's reading of the same frames) |
|---|---|---|---|
| `kneel.png` | 9,622,670 | 255 | Wander at his kneeling rear, the turned-up sole |
| `back.png` | 7,380,064 | 255 | the climb camera on the spine plates |
| `head.png` | 9,679,880 | 255 | Wander beside the head sigil |
| `death.png` | 11,618,605 | 255 | mid-fall at 4.5 s, on the low key |

The span is the largest per-channel max − min of the viewer's `s1-pixels` line (the gate is ≥ 30).

## 5 · Load and frame time

| Run | arena_02 load (ms) | frame (ms, 240 frames) |
|---|---|---|
| this acceptance: selftest / e2e | 979.6 / 905.5 | 8.296 |
| Task 10 fix round: selftest ×2 / e2e ×2 | 851.6, 853.3 / 855.1, 856.5 | 8.355, 8.337 |
| Task 10 first round: selftest ×2 / e2e ×2 | 1073.7, 891.6 / 952.0, 869.7 | 8.345, 8.289 |

The budget is S9's: `0 < frame_ms <= 16.7` (60 fps). The load is the scene upload, the layered heightmap
(1111 × 688 × 64, 0.23 s) and his rig.

## 6 · What came from where

Labels as in `app/src/encounters/quadratus.h`: **data** (shipped files, or the definition tables embedded
in `eboot.elf`), **measured** (from the baked glb / `.anb` root series), **authored** (the builder's pick,
to be tuned on the walk).

### 6.1 From `eboot.elf`'s behaviour tables (data)

Behaviour is data in `eboot.elf`, not in the game tree: a typed definition database in the data segment
(`.superpowers/sdd/2026-10-23-s10-s11-acceleration/s11-behaviour-re/findings.md`). Quadratus is the
MAMMOTH graph: `scr_mode_status` 0x1616e70 (21 modes), conditions 0x1616dd0 (172), `scr_mode_orient`
0x2317010 (102 transitions). S11 takes from it:

| Value | Where in the eboot | Used as |
|---|---|---|
| HP 220 (330 read as Hard: guess) | `boss_param[1]` 0x16d1e70 via `boss_type_def` MAMMOTH | `kQuadratus.maxHealth` |
| the hoof gate: a sword hit on sole region FR 0x380 / BR 0x390 / FL 0x3a0 / BL 0x3b0 → sprain mode 9–12 → the `down_*` clips | `FUN_00832710(mask 0x3f0, region)`; regions in `boss_damage` `mammoth_first` 0x16c0da0 | the four `kHooves` rows, a Transition gate |
| which modes check the soles (all but sprain/rising_return/attached/death) | the mode graph | the 21 `kNoHoofGateStates` |
| sprain ends at 20 s on its transition timer | cond[67], `FUN_00833d80` on boss+0x4b54 | `kSprainSeconds` |
| the 45 s mode-time cap | cond[65], `FUN_00833d50` on boss+0x4b50 | recorded, not modelled (§8) |
| rising 2 s; rising_return 10 s | cond[69], cond[71] | `kRisingSeconds`, `kRiseReturnSeconds` |
| the shake-off: attached on a hide region, not already in `refuse_shoulder`, in the first 5 s of a 20 s attached cycle; a stab pauses it 3 s | `func_def[11]` = cond[95] → control 351 `Mammoth_shudder`; cond[91]; mode 20 cond[93] | `kShakeRule` {5 s, 20 s, 3 s} |
| turn when the bearing leaves ±30° | mode 7, `FUN_0082fae0(30)` | `kTurnTriggerDeg` |
| the front cone 20° (cos 20° = 0.9397) | cond[43] `P_is_at_the_front`, +1c = 20 | the stomp's `facingDot` (its role as the attack gate is the RE lane's reading) |
| the stomp's damage joints `OptBone_IKFootFL/FR` | `scr_attackdata` 0x22329a8 [0]/[1] skelfocus | `kAttacks[].damageJoint`, the death feet |
| the sigils' regions: head 0x10, back 0x30; weakpoint_06 (0x190) damage only in `mammoth_second` (Hard) | `boss_damage` | head + back wired; weakpoint_06 unwired |
| the damage multipliers {0.34, 0.034, 0.51, 0.34} / flesh {1, 0.1, 1.5, 1} | `mammoth_first` rows | recorded in `kSigilDamageScale`, not applied (the HP formula is open) |

### 6.2 From the shipped files (data) and measured

- **Placement**: no spawn record exists (like Valus). Player spawn (273, −842) from
  `misc/playerstartpositions.txt:3`, ground −98.308 (measured). His spot (240, −925), yaw 21.68°, ground
  −96.741: authored, from the recon.
- **Clips**: the 58 `boss_a_*` clips (data). Durations, root travel (walk 9.375 m/s, back 5.0 m/s; root x
  negated: glb x = −anb x), turn angles (±45° per turn clip; walk_turn_r −44.3°), the kneels (rear drops
  the hip 6.9 m, front 0.75 m), the stomp's impact window [0.929, 1.0] (t 1.703–1.834 s), the death clip
  5.333 s and its 17.5 m root travel: measured (`s11-quadratus-recon/clips.md`).
- **Bands and capsules**: the climb chain's radii from the entity cgpr's per-bone records (data: B_TOE
  2.5, SUNE 2.0, MOMO 3.0, KUBI 6.0, HEAD 3.0, back 7); the rump's 4.3 m at MATA (measured p99 4.26–4.32);
  the back's 1.5 → 6.3 m taper (measured). The 95 sole points and the test fixtures are derived from the
  library glb at load/test time, never committed (ruling A15).
- **The shake drains** per refuse clip: the region bone's measured p90 angular speed / 10 (authored scale
  of a measured value), applied inside the measured active window.

### 6.3 Authored (to tune on the walk)

| Value | Where | Why |
|---|---|---|
| hoof lift 0.5 m for a stab to count | `kHooves[].liftMeters` | the game's region → triangle map is open; proposed from the measured lift windows (0.3 m the fallback) |
| **blade reach 0.6 m** past the body capsule (ground stab only) | `kQuadBladeReachMeters` | the hoof capsule (r 2.5) holds the player 2.85 m off against a 2.4–2.8 m sole shell; without it no stab lands. Measure from `wander_sword.glb` later |
| a hoof stab is a state change only (no damage) | the gate rows | the game's sole rows are flesh-like; the recon/G6 reading |
| sigil share = maxHealth / 2 = 110 | `quadratus_fight_hit` | the HP formula is open |
| sigil order back → head | `kSigilOrder` | the role guess (recon R§2) |
| rump end radius 1.6 m, carrying the back sigil (taper 4.3 → 1.6) | `quadratus.h` | the sigil sits on the rump's rear face (Task 10 probe); see §7 |
| walk start > 60 m, stop ≤ 50 m; no stomp within 12 m; no turn within 2 m | `quadratus_fight.h` | a walk cycle carries 20 m; at 30 m he walked onto the player |
| stomp reach 25 m, hit 3.0 m, wave 8.0 m, damage 35 / wave 15, cooldown 5 s | `kAttacks` | Valus's numbers scaled; the game's type-46 volume is not ported |
| ground step 2 m for his travel | `kQuadGroundStep` | |
| stab clip `player_a_atk1`, hit on its first frame | `kQuadStabClip` | |
| standing on a horizontal band regenerates (25/s); surface pace 2.2 m/s | `player_state`, `CombatTuning` | |
| no choices while ridden; the shake cycle resets on the ground only | `quadratus_decide` | |
| the death camera keys (pull 30 m / low 34 m −12° +25° / wide 55 m +15° +45°) | `quadratus.h` | chosen from stills; `MOMMOTH_DEATH_CAM` is not decoded |
| the tendril window 0.4 / 3.0 s and the 8.0 s end | `kQuadratus.death` | Valus's S6 values, unscaled |

## 7 · Deviations

- **The step order (review M4).** The brief read "walk the back → the back sigil". The run is: stab a
  rear hoof → kneel → climb the fur (hoof → shin → thigh → rump) → **strike the back sigil from the back's
  rear end and the rump's top** → then walk the back (`s11-back`) and hold through the shake → neck → head
  sigil. It follows from where the sigil is: on the rump's rear face, 0.9–1.4 m behind the
  MATA→Weak_point2 axis and 1–3 m below Weak_point2, out of reach from the back's top. `s11-sigil` prints
  before `s11-back`, and the event-order gate pins that order.
- **The blade reach 0.6 m** (authored, above): it makes the hoof rule work at all.
- **The rump taper** 4.3 → 1.6 m (review I1): a constant 1.6 m sank the climber ~1.4 m into the hips.
- **The 45 s sprain cap is moot**: the Down* states ignore hoof re-hits (no pop-up, no infinite hold), so
  nothing refreshes the 20 s timer and the cap can never be reached. Modelling it needs a re-hit path and a
  second clock.
- **The tendril window is still Valus's** (0.4 / 3.0 s): equal to his, so `effect.cpp`'s envelope is
  right for both; `EffectFrame` does not carry a per-encounter window.
- **Silent fight (D10).** His cues wait for the census's bank mapping (`boss01` holds `mammo_*`/`mavo_*`,
  his stems) and an audition. Only the player's shared cues play; his stomp row has `swingSeconds −1` (no
  whoosh; `maybe_club_swing` returns on < 0).
- **No hit reactions, no anger.** The `add_*` reactions are full-pose bakes that would pop him out of a
  kneel; mode 13 (grabbing the downed foot cuts the sprain to 2 s), the anger model and the Angry state,
  and the refuse_* swings other than `refuse_shoulder` are not played.
- **The neck hand-over drops sideways**: the back's top hands the climber to the side of the neck (a
  radial band).
- **Hold E on the back**: standing on his back is not gripping (no drain), but releasing E there drops
  the player through the body.
- **A cross-hoof hit mid-kneel is ignored** (tagged in `quadratus_fight.h`).

## 8 · The Valus divergence (recorded; the user: "Fix later")

The eboot tables (`s11-behaviour-re/findings.md` §4) show Valus differing from S4.2's authored fight: he
goes down on one weak spot only (the left calf, region 0x140; it ends at 25 s / 55 s or on release); his
shake-off is per region (weapon 0x200 → an arm swing; head 0x10 → a head shake that stops after 15 s; body
0x2c0… → a body swing that stops after 5 s; then a 10 s wait); his attack window is ±30° with a 5 s
attack loop; his HP is 200 (300 Hard). The user (2026-10-23): **"Fix later."** S11 does not touch Valus:
the s3–s9 goldens are byte-identical and the frozen chain is green. The backlog item: drive Valus from his
`scr_mode` tables (MINOTOUR_A `scr_mode_status` 0x162f7c0, 128 transitions at 0x23171f0), then re-bless
the affected goldens deliberately, ideally after the user's walk.

## 9 · The Valus audio-bank conflict (recorded, not corrected — §13.14)

S5's frozen `tools/re/audio/s5_map.json` takes Valus's cues from bank `boss01`. By cue stem, `boss01` is
Quadratus's bank (`mammo_` 122, `mavo_` 41), while the `minotaur_`/`mivo_`/`minoboot_` stems sit in
`boss15` (and `boss22`); the `boss01_*` name prefix occurs in both (71 names in `boss01`, 105 in `boss15`)
(`docs/notes/colossi-census.md`). S11 changes nothing here: `s5_map.json` is frozen, Valus's cues are as
S5 shipped them, and Quadratus is silent. A correction (an audition, or a Ghidra chain walk of `boss15`)
is the user's separate decision.

## 10 · The runner (`scripts/verify`)

- **Blessed**: `scripts/verify --bless --only s11` (bless is per milestone: only `golden/s11/` is
  written; `PIN` moves to `818abd5`, the commit that adds `scripts/s11-acceptance.sh`, so that script is
  frozen too; `git diff --stat tools/verify/golden/` = `PIN` + 7 new s11 files; s3–s9 byte-identical).
  Run twice from fresh runs (each bless itself runs the section twice): the two blessed trees are
  byte-identical. Masks added — timing only, each seen to vary run to run in Task 10's runs: `s11-load ms`
  (1073.7 / 891.6), `s11e2e-load ms` (952.0 / 869.7), `s11-perf frame_ms` (8.345 / 8.289). No other field
  moved.
- **Agreement**: `scripts/verify --only s11` PASS (60 s); a one-value mutation of the blessed golden
  (`s11-corpse … moved=17.51`) FAILs naming that line; reverted.
- **Full run**: `scripts/verify` on `b9505bc` (a fresh worktree's stamps): rc 0, 294 s wall — dotnet PASS
  59 s, ctest PASS 10 s, s3 9 s, s4 19 s, s41 21 s, s42 10 s, s5 8 s, s6 19 s, s7 30 s, s8 52 s, s9 100 s,
  all against unchanged goldens; s11 on its stamp from the `--only s11` PASS, then `--only s11 --force`
  PASS (58 s).

## 11 · The user's walk — PENDING

```bash
export SOTC_LIB=<repo>/out/library
make quadratus        # app/build/sotc-viewer … --manifest out/re/stage/arena_02.manifest
```

| # | Check | Do this | Expected | Result |
|---|---|---|---|---|
| 1 | the fight's feel | walk toward him from the spawn | he stands, turns to face you past ±30°, walks when you are far (> 60 m) and stops near 50 m | ☐ |
| 2 | the charge | stay in front of him within ~25 m | he rears and stomps; stepping aside dodges it; a hit hurts | ☐ |
| 3 | the hoof stab windows | strike (`J`) under a rear hoof as it lifts | the stab lands only while the sole is up (≥ 0.5 m); a planted hoof ignores it | ☐ |
| 4 | the kneel | land a rear-hoof stab | he kneels on that leg (no damage) for ~20 s, then rises; front hooves kneel the front | ☐ |
| 5 | the fur climb | grab (`E`) the kneeling leg's fur | hoof → shin → thigh → rump; stamina drains while gripping | ☐ |
| 6 | the back sigil | from the rump's top or the back's rear end, strike the rump sigil | it takes damage; the head sigil is sealed until it is spent (health 110) | ☐ |
| 7 | the back walk | step onto his back (hold `E`) | you stand and walk along the spine (`W`/`S`, `A`/`D` across); stamina regenerates | ☐ |
| 8 | the shake-off | stay on him | in the first 5 s of each 20 s he throws his shoulders; holding on survives it | ☐ |
| 9 | the head sigil | climb the neck to the head; strike | the last blow kills him | ☐ |
| 10 | the death | watch | his fall (5.3 s) on the authored camera; the tendrils from the two sigils; the body stays where it fell; **no** return to the shrine | ☐ |
| 11 | the camera | through the climb and the back walk | the follow/climb camera keeps him and you in view; no clipping inside his body | ☐ |
| 12 | `R` | press `R` mid-fight, mounted on him, and after his death | he is back at his spot, standing, health 220, back sigil open; you on the ground at the spawn | ☐ |
| 13 | silence | listen | his fight is silent apart from Wander's cues (D10) | ☐ |
| 14 | window, cursor, fps | move the mouse; watch the title | a 1280×720 window, the cursor free, the fps in the ≥ 60 class | ☐ |

Known rough edges to expect (§7): hold `E` on the back; the neck hand-over drops you to the neck's side;
no reactions to sigil hits; the 0.6 m blade reach and the 0.5 m lift are authored.

**Verdict: PENDING.** S11's done-line closes only when the user has filled this table.

## 12 · The rulings that shaped S11 (`.superpowers/sdd/2026-10-23-s10-s11-acceleration/progress.md`)

A1–A3 and A7–A10 scheduled S10's tooling (the census, the library view, the runner, `run_flags`, the
split) around S8/S9 and S11; S11 rests on what they built (the runner it is blessed into, the library
view its bake was made in, the `app.cpp` split it is wired through).

| Ruling | What it decided for S11 |
|---|---|
| A4 | the pure half of S11 Task 8 (behaviour machine + gates; locomotion, surface walk, shake-off) as new modules with unit tests, before the app split |
| A5 | a multi-clip attack chain is a run of states and the app sets the cooldown on the chain's last state; `EncounterDesc` embeds `BehaviourDesc`; the gates live there |
| A6 | Task 9's data slice first: `probe.py STAGES['arena_02']`, the arena_02 composer, the opt-in `encounter <id>` manifest directive |
| A11 | Quadratus's bake in its own library view (`out/libviews/s11-quadratus`), `make bake-quadratus` |
| A12 | the read-only clip analysis (root motion, the x sign, hoof-lift windows, timings) → `s11-quadratus-recon/clips.md` |
| A13 | the read-only lanes: where behaviour lives (→ the eboot tables, §6.1), colossus 3 (→ S12), the world (→ §13) |
| A14 | the three parallel lanes: 9q `quadratus.h` + registry; 10a the M-1 conversion (`encounter_desc.h`/`valus_desc.h`); 10b the generated root table (x negated) + the hoof-hit rule |
| A15 | no committed mesh vertices: root tables stay in source; the 95 sole points and the glb-sampled fixture are derived at load/test time |
| A16 | the Quadratus bake promoted to the base `out/library` at the lane merge; `bake-quadratus` joins `make bake`; unit tests never read `out/re/stage` manifests |

## 13 · What's next

- **S12, colossus 3 — Gaius** (`colossus_03_knight`, stem `knight_a`;
  `s12-c3-recon/recon.md`). A tilted disc in a lake (centre (−300, −1400), ~52 m radius, 5.4° tilt), 10
  world-frame glbs, no local-frame pieces; the player start is data (`playerstartpositions.txt:4`), his
  spot a pick (−300, −1420). The five S11 mechanics fit; new: a walk-on sword ramp (a band that exists
  only while atk1 holds), breakable armour (6 forearm + 2 neck fragments, trigger unknown), the rim drop
  (25–30 m into the lake: a barrier or a fall → reset), the blade as a hazard. About 60 % data/measured,
  40 % authored. Carry S11's debt: move the authored tuning in `quadratus_fight.h` into the table when a
  second quadruped arrives (review M2); the layered-map size.
- **The world milestone** (`world-recon/recon.md`). The game streams by a hand-painted zone map
  (`shadowzones.cpu.cctx`, 1026×1024 over [−2500, 2500]²) and a 192-zone table, at Area granularity; no
  distance rings. The smallest step needs no loader: "the way south", one composed stage = the road +
  arena_02 (64 glbs, 3,708 meshes, 2.47 M vertices; reach 1,040 m from the shrine to his basin over the
  landbridge). The csml props (257k instances; `home` alone 12k) need instancing + LOD and are their own
  milestone.
- **The objanim event tracks the converter drops.** Each clip's events live in a sibling
  `objanim_<stem>_<clip>.anb` (flag-set format, which `AnbReader.cs:108` refuses): type 46 = the per-frame
  attack volumes (Quadratus's right stomp ≈ (±18.1, 2.2, 37.3), 20×15×30 m), 44 = IK/reach volumes, 29 =
  effect triggers (foot dust/sound), 11 = foot-fall objects. Sound cues are per-motion `{frame, sound}`
  tables in the executable (`se-frame-table` 0x1d5fcd0, 264 rows). Decoding them replaces the authored
  stomp volumes and is the way to his audio.
- **Valus from the game's data** (§8) and **the bank audition** (§9): the user's calls.

## 14 · Evidence files (all gitignored)

- `out/s11-acceptance-run.log`, `out/s11-s9-regression.log` (in the worktree `wt-s11-accept`).
- `out/s11/s11-selftest.txt`, `out/s11/s11-e2e.txt`, `out/s11/stills/{kneel,back,head,death}.png`.
- The runner's: `out/verify/wt-s11-accept-<hash>/` (logs, candidates).
- Task reports and reviews: `.superpowers/sdd/2026-10-23-s10-s11-acceleration/task-{7,8*,9*,10*,11}-*.md`.
