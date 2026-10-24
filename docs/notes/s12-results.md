# S12 acceptance results (2026-10-24) — Gaius

Spec milestone S12 (`docs/superpowers/specs/2026-10-23-s12-gaius-design.md`, goals G1–G5, decisions D1–D12,
rulings §13): the third colossus, Gaius (`colossus_03_knight`, the `KNIGHT` tables, clip stem `knight_a`), on
his lake arena (`arena_03`). His fight runs from his **exported mode graph** (`sotc-scr-graph/0`, read from
`eboot.elf` by `tools/re/scr/graph_export.py`) through a generic interpreter (`app/src/scr_graph.*`,
`scr_predicates.*`), and from his **objanim events sidecar** (`sotc-objanim-events/1`, written by the
converter's `ObjAnimReader`). Dodge his slam, grab the stuck blade at its low end, ride it up onto the
forearm, climb to the head sigil, hold through the shake-off, strike the belly sigil; he dies and his body
stays; `R` resets. His fight is silent (D10).

**Status: implemented; the scripted acceptance is PASS. The user's walk is PENDING (§10).**
`scripts/s12-acceptance.sh` runs green end to end (exit 0). It covers:
- the inputs, checked before anything runs: the game tree, the library with Gaius's bake (his `clips.json`
  holds `knight_a_stand`/`atk1_3`/`knight_death`), Wander, `out/re/stage/arena_03.manifest` (composed when
  absent; it must say `encounter knight`), the exported graph `behaviour/KNIGHT.graph.json` with KNIGHT's
  counts (13 modes, 53 conditions, 40 transitions, 171 motion-graph edges), and the events sidecar
  `colossus_03_knight.events.json` (format, skeleton `knight_a`, glb frame, < 64 KB, the pinned rows);
- the **frozen S11 acceptance first** (`scripts/s11-acceptance.sh`, unchanged). It runs the frozen S9 chain
  (both builds, both suites, S8 → S7 → S6 → S5 → S4.2 → S4.1 → S4 → S3) and then S11;
- five flag refusals (exit 2);
- the deterministic `s12-*` proof (`--s12-selftest --s12-stills`), parsed line by line;
- four 2560×1440 stills with pixel-span checks;
- the end-to-end loop (`--s12-e2e`): one arena load, no shrine load, its lines equal to the selftest's.

The script does not judge whether the fight feels like the game's. That is the user's walk (§10).

| | |
|---|---|
| Date | 2026-10-24 local (−03). The recorded run, on `8630c24` (the blessed goldens): stamped `2026-10-24T17:35:17Z`–`17:51:06Z` (wall 949 s = 15 min 49 s). The S11 chain inside it took 880 s (its S9 chain 820 s); the S12 part 69 s. |
| Machine | Apple M3 Max, macOS 27.0.1, arm64. 1280×720 points → 2560×1440 px captures. Run from a desktop (Aqua) session (§9.3). |
| Repo / branch | `sotc-native`, branch `s12-gaius` (worktree `.claude/worktrees/s12-gaius`) off main `8eb25c9`. Script `5c2cb97` (the `PIN`); goldens `8630c24`. |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Library (gitignored) | `SOTC_LIB=<repo>/out/libviews/s12-gaius`, the branch's view (ruling R-K). It holds Gaius's bake (his glb, 62 `knight_a_*` clips, the events sidecar) and `behaviour/KNIGHT.graph.json`. Not yet in the base `out/library`: the merge promotes it (D11, A16 pattern), and `make bake` now includes `bake-knight` and `behaviour` (ruling R-C). |
| arena_03 manifest (gitignored) | `out/re/stage/arena_03.manifest` (`probe.py compose --stage arena_03`, `make arena03-manifest`): 4 tower glbs as ground, the rest and the water plane as `backdrop`, `encounter knight`; the viewer's scene is 93 meshes. |
| Acceptance script | **`scripts/s12-acceptance.sh`** (bash 3.2); `make accept` / `make accept-s12` |
| Run log (gitignored) | `out/s12-acceptance-run.log`, which ends `s12-acceptance: PASS —` |
| S11 regression log (gitignored) | `out/s12-s11-regression.log`, which ends `s11-acceptance: PASS —` |

## 1 · The acceptance run

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export SOTC_LIB=<repo>/out/libviews/s12-gaius     # <repo>/out/library after the merge promotes the bake
caffeinate -dimsu ./scripts/s12-acceptance.sh     # exit 0
```

- **The S11 chain** ran unchanged and printed its own `s11-acceptance: PASS —` (880 s): the S9 chain
  (820 s: builds, `dotnet test` and `ctest`, S8 → … → S3, the S9 decode, the composes), then S11's
  refusals, selftest, stills and e2e (arena_02 load 964.4 ms). Its outputs are not re-asserted here.
- **The S12 part** (69 s): the preflight (the KNIGHT export 13/53/40/171; the sidecar: `sotc-objanim-events/1`,
  `knight_a`, `glb`, 41,774 bytes, the pinned rows), the five refusals, the selftest with stills (9 s), the
  e2e (60 s), the summary. The script ended (exit 0):

```
s12-acceptance: PASS — the frozen S11 acceptance green (880 s: the S9 chain — builds, both suites, S8 → … → S3 — then S11 inside it); KNIGHT export 13/53/40/171 and the knight_a sidecar valid; five flag refusals exit 2; arena_03 load 909.7 ms (93 meshes, layered ground 125x124, no return); the graph from route_patrol; the slam dodged clear of every row (off_line 6.70 >= 6.65); the blade grabbed at f27 and ridden up onto the forearm (close_t 0.55, margin 0.05) in one slam; the climb to the head (stamina 87.0); head sigil 84, a shake survived (low 1.5), belly sigil 0, the kill; the death in Knight_death with his held death frame (moved 6.57 m) and no shrine; root max 27.60 m of 36; R exact; the exposure hit KNIGHT_ATK_SHOCK (5); frame 8.504 ms (drawn 85, culled 8); 6857 frames / 114.3 s; the e2e loop (frames=6857 seconds=114.3; load 905.0 ms) equal to the selftest, one arena load; four stills 2560x1440; windowed, cursor free; 949 s in all
```

- **The flag refusals.** Each exits 2 before any load: `--s12-stills` without `--s12-selftest`;
  `--s12-selftest` with `--s12-e2e`; `--s12-e2e` with `--stage shrine`; `--s12-selftest` without
  `--manifest` (each with an `--s12-*` message); `--stage plain` with `--s12-selftest` (main.cpp's general
  rule, "--stage plain is an interactive start", meets it first).
- **The runner.** `tools/verify/s12.sh` is the second copy of the script's gates, like s3…s11
  (`test_coverage.py`: s12 74/74 present, 4 excluded — the nested S11 run, its PASS line, the viewer build;
  7 viewer runs). The s12 goldens (7 files) are blessed (§9).

## 2 · The deterministic proof (`--s12-selftest`)

Fixed 1/60 s steps with a scripted player. He senses the player at the spawn and slams; the player runs
across his facing line, clear of every slam row, then to the blade's low end; grabs it in its window, walks
it at run speed, rides it up as he pulls it, climbs off onto the forearm, up the arm to the head; strikes
the head sigil between his shakes; holds through one; goes down the belly route; strikes the belly sigil.
After `R`, a deliberate exposure walks into his next slam so that one damage row lands (ruling R-L). The
lines (the blessed golden; timings masked; every run since has equalled it):

```
s12-load stage=arena encounter=knight meshes=93 ground=125x124 layers=64 entity=-300.00,47.66,-1420.00 yaw=-1.27 spawn=-301.00,43.33,-1375.00 return=0 graph=KNIGHT modes=13 edges=171 start=Knight_route_patrol clip=knight_a_stand ms=<masked>
s12-mode from=none to=Knight_route_patrol clip=knight_a_stand at=0.0
s12-mode from=Knight_route_patrol to=Knight_sword_attack_mode clip=knight_a_stand at=0.5
s12-judge volume=KNIGHT_WEAPON_ATK_FIX bearing=0.0 dist=43.8 sector=forward at=0.5
s12-slam n=1 choice=slam rays=1 mode=Knight_sword_attack_mode dist=43.8 at=0.5
s12-dodge sword_rows=0 off_line=6.70 need=6.65 widest=KNIGHT_ATK_SHOCK reach=6.00 player_hp=100 mode=grounded at=3.7
s12-climb band=blade t=0.00 stamina=100.0 clip=knight_a_atk1_3 at=4.9
s12-mode from=Knight_sword_attack_mode to=Knight_parent_mode clip=knight_a_atk1_3 at=4.9
s12-climb band=forearm t=0.00 stamina=100.0 clip=knight_a_atk1_end at=7.7
s12-blade window=9..75 grab_frame=27 close_frame=75 close_t=0.55 ride_from=0.50 margin=0.05 carried=1 band=forearm slams=1 at=7.7
  (climb: upper_arm 16.1 → shoulder 20.3 → neck 40.7 → head 44.0)
s12-route bands=blade,forearm,upper_arm,shoulder,neck,head stamina=87.0 at=44.0
s12-strike weakpoint=weakpoint_01 band=head damage=34 spent=0 health=136 killed=0 at=54.5
s12-strike weakpoint=weakpoint_01 band=head damage=34 spent=0 health=102 killed=0 at=65.7
s12-strike weakpoint=weakpoint_01 band=head damage=18 spent=0 health=84 killed=0 at=76.9
s12-sigil done=head strikes=3 spent=86 health=84 at=76.9
s12-shake survived=1 clip=knight_a_refuse_head band=head low=1.5 stamina=8.6 at=85.1
  (down: neck 86.0 → shoulder 89.3 → belly 94.4)
s12-strike weakpoint=weakpoint_02 band=belly damage=34 spent=0 health=50 killed=0 at=100.6
s12-strike weakpoint=weakpoint_02 band=belly damage=34 spent=0 health=16 killed=0 at=101.7
s12-strike weakpoint=weakpoint_02 band=belly damage=16 spent=0 health=0 killed=1 at=102.8
s12-sigil done=belly strikes=3 spent=84 health=0 at=102.8
s12-mode from=Knight_parent_mode to=Knight_death clip=knight_a_knight_death at=102.8
s12-death phase=Motion total=0.000 mode=Knight_death graph_clip=knight_a_knight_death clip=knight_a_knight_death
s12-death phase=Collapse total=6.000 mode=Knight_death graph_clip=knight_a_knight_death clip=knight_a_knight_death
s12-death phase=Done total=8.000 mode=Knight_death graph_clip=knight_a_knight_death clip=knight_a_knight_death
s12-corpse live_body=1 clip=knight_a_knight_death t=6.000 held=1 moved=6.57 player=grounded throw_offs=0 return=Idle
s12-bound max=27.60 unclamped=27.60 clamped_frames=0 radius=36.0 centre=-300,-1400
s12-reset health=170 defeated=0 mode=Knight_route_patrol clip=knight_a_stand at_base=1 player_hp=100 pmode=grounded death=Idle
s12-hit row=KNIGHT_ATK_SHOCK amount=5 clip=knight_a_atk1 player_hp=95 mode=hurt at=114.3
s12-perf frames=240 frame_ms=<masked> drawn=85 culled=8
s12-status phases_done=11 frames=6857 seconds=114.3 health=170
s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 frames=3
```

While the climber is on him the graph flips `Knight_parent_mode` ↔ `Knight_route_patrol` at each strike
(the lines between the strikes above; 17 `s12-mode` lines in all).

| Line | Gate (the script and `s12.sh`) | Measured |
|---|---|---|
| order | load, judge, slam, dodge, blade, route, sigil, shake, sigil, death ×3, corpse, bound, reset, hit, perf, status — exactly | as gated |
| `s12-load` | `encounter=knight`, `layers=64`, `return=0`, `graph=KNIGHT modes=13 edges=171 start=Knight_route_patrol clip=knight_a_stand` | 93 meshes; ground 125×124 nodes; **909.7 ms** this run (§5) |
| `s12-mode` | route_patrol → sword_attack_mode; sword_attack_mode → parent_mode | at 0.5 s; at 4.9 s (the grab) |
| `s12-judge` | `KNIGHT_WEAPON_ATK*`, `sector=forward` | `KNIGHT_WEAPON_ATK_FIX`, bearing 0.0°, 43.8 m |
| `s12-slam` | `n=1 choice=slam rays=1` | the three 35 m rays clear; fired from 43.8 m at 0.5 s |
| `s12-dodge` | `sword_rows=0`, `player_hp=100`, `off_line >= need` | 6.70 m off his line; need 6.65 = the widest posed row 6.00 (`KNIGHT_ATK_SHOCK`) + capsule 0.35 + margin 0.3 |
| `s12-blade` | `window=9..75`, `close_frame=75`, `close_t` 0.5x–0.9x, `ride_from=0.50`, `carried=1 band=forearm slams=1` | grab at `atk1_3` f27; bandT 0.55 at the f75 close; margin 0.05 |
| `s12-route` | `blade,forearm,upper_arm,shoulder,neck,head` | the head at 44.0 s, stamina 87.0 |
| `s12-sigil` | `head:84 belly:0`; strikes head then belly; the last kills | head 34 + 34 + 18 (pool 86), belly 34 + 34 + 16 (84 left) |
| `s12-shake` | `survived=1 clip=knight_a_refuse_*` | `refuse_head` on the head, stamina low 1.5 |
| `s12-death` | `Motion/Collapse/Done`, all in `Knight_death` | 0.000 / 6.000 / 8.000 s |
| `s12-corpse` | `clip=knight_a_knight_death t=6.000 held=1 … return=Idle` | the death clip's root moves him 6.57 m; no throw-off fired; no shrine |
| `s12-bound` | `max <= 36` | 27.60 m; the clamp never engaged (`clamped_frames=0`) |
| `s12-reset` | exact | — |
| `s12-hit` | one row, amount ≥ 1 | `KNIGHT_ATK_SHOCK` 5, in `knight_a_atk1`, at 114.3 s |
| `s12-perf` | 240 frames, `0 < frame_ms <= 16.7` | **8.504 ms** this run (§5) (drawn 85, culled 8) |
| `s12-status` | `phases_done=11` | 6,857 frames, 114.3 s simulated |
| `s1-status` | `windowed=1 relative_mouse=0 mouse_auto_capture=0` | as gated |

## 3 · The end-to-end loop (`--s12-e2e`)

The same script through the interactive loop. Gated: exit 0; one `s12e2e-done`; an `s1-status`; **one
`s7-load` line, `stage=arena`** (no shrine after his death: `returnWired` is off outside Valus's arena);
its `s12e2e-load` line gated like the selftest's; the `s12e2e-*` lines equal the selftest's `s12-*` lines
with `load`, `perf`, `status` and `done` aside. Measured: `s12e2e-done frames=6857 seconds=114.3`, load 905.0 ms, 60 s wall.

## 4 · The stills

Each is one `s1-screenshot` line, PNG magic, > 5,000 bytes, and a channel span ≥ 30 (the viewer's
`s1-pixels` line).

| Still | Bytes | Span | Shows |
|---|---|---|---|
| `blade.png` | 8,922,776 | 254 | the grab on the stuck blade (`atk1_3`, 4.9 s) |
| `head.png` | 6,524,782 | 255 | Wander on the head, at the sigil (44.0 s) |
| `belly.png` | 4,695,760 | 251 | Wander hung on the belly band (94.4 s) |
| `death.png` | 6,546,261 | 255 | the first frame of `knight_death` (102.8 s) |

The span is the largest per-channel max − min of the viewer's `s1-pixels` line (the gate is ≥ 30).

## 5 · Load and frame time

| Run | arena_03 load (ms) | frame (ms, 240 frames) |
|---|---|---|
| this acceptance: selftest / e2e | 909.7 / 905.0 | 8.504 |
| Task 9 fix rounds (two selftests, two e2e) | 880–1140 | 8.342–8.360 |

The budget is S9's: `0 < frame_ms <= 16.7` (60 fps). The ground is 125 × 124 × 64 nodes (the four tower
glbs only, AR §3).

## 6 · The R-gate verdicts (`docs/notes/s12-recon.md`)

| Gate | Verdict | What it settled |
|---|---|---|
| R1 · attack data and locomotion kinds (§13.1) | **CLOSED / FALLBACK in part** | `FUN_0085a960` only arms; the runner is `FUN_0085ab40` (kind 10): FIX row first, then ATK, then the default row; ATTAIN as the goal; cooldowns and re-selection; the control bits per kind. FALLBACK: the gait ladder (walk vs fast turn vs stop-turn in `FUN_008158d0`) keeps §13.1's ±30° rule; mode 12's goal is a guess, authored as none. The ATTAIN reading is a guess. |
| R2 · control-def conditions (§13.2) | **CLOSED** | a control-def is an AND of 0x68-byte records (expect at +0x58); all 28 opcodes in KNIGHT's 67 control-defs decoded; a row fires on (ctrl bit requested) OR (condition). |
| R3 · the blade window (§13.4) | **CLOSED** | `atk1_3` frames [9, 75): after the fling volume (track 43, f0–8), until `KNIGHT_PULLSWD` fires at f75 (before the measured stillness end, f77 within 0.25 m). 66 frames = 2.2 s. |
| R4 · damage-volume pose space (§13.6) | **CLOSED** | bone parent = the glb joint of the same skb name, offset (−x, y, z), quat (−x, y, z, w); entity (−x, y, z); skelton (x, hipY + y, −z). Residuals: slam blade 0.70 m / 0.2°, far end 0.09 m from the tip; foot spheres 1.92 m (R) / 0.54 m (L) from the footprint centres, inside r 7.26; footstep spawns 0 frames; Quadratus's t46 holds his impact 5.5 m inside (unmirrored it misses by 5.5 m). |
| R5 · the bearing sign (§13.9) | **CLOSED** | `player_bearing_in` = atan2(local x, local z) in the game frame, + = the boss's right; the app's bearing (+ = left) = −game bearing. |

## 7 · What came from where (§13.10)

Labels: **graph** — the exported `KNIGHT.graph.json` (eboot tables, read by the exporter, run by the
interpreter); **sidecar** — the objanim events sidecar (the shipped `objanim_knight_a_*.anb` tracks);
**R-decode** — an R-gate's decode of the binary (§6); **data** — another shipped table or file;
**measured** — computed from the bake or the arena glbs; **authored** — the builder's pick, labelled in
`app/src/encounters/knight.h`, `knight_fight.cpp` or the selftest script, to be tuned on the walk.

| Behaviour | Source |
|---|---|
| Modes, transitions, conditions, enter/update/exit actions; the start = retry mode 3 (`Knight_route_patrol`, §13.8); the death = root death mode 8 | graph |
| Control → clip (control-assign rows, motion-graph edges, `p30` timing, `p34` blend) | graph (the `p30`/`p34` readings: Task 5) |
| Else edge at a clip's end → control-mode N (`else_orient_def[N]`) | graph + **authored** reading (the pattern 0/1/3/6/7/8/9/10; not a decode) |
| Control-assign conditions (hurt, landing, mortality rows) | graph + R2 |
| Slam/stab choice, the attack rows, their cooldowns, approach and back-off (ATTAIN goal) | graph + R1 (the ATTAIN reading is a guess) |
| Walk vs turn choice, turn cut | **authored** §13.1 ±30° rule (R1 fallback); the 3° cut via the gait's bits |
| Perception latch (`scr_relposparam`) | graph; the sight ray never blocks (**authored**: no wall on the disc) |
| Shake-off triggers, cooldowns, regions | graph + data (`knight_shake`); which region each band is: **authored** |
| Shake strength (stamina drain, active window) | **authored** rule (Quadratus's p90 angular speed / 10), measured on Gaius's glb |
| Root motion, turn headings | measured (`knight_root.h`) |
| Damage on the player: rows, windows, poses, swept slam, footstep spawns (`knight_walk_dmg`) | sidecar + R4; the spawn link from `tools/re/objanim/knight_map.json` (curated) |
| Judge volumes (the attack runner's ATK/FIX/ATTAIN tests) | sidecar (+ R1 for their use) |
| Heavy hurt = reaction 4; throw-off = reaction 6 passes §13.5's filter | **authored** readings of sidecar data |
| No damage on a player attached to / standing on him (§13.5) | **authored** (the likely reaction filter, unverified) |
| HP 170; sigil pools 86 / 86; regions 0x10 head, 0x50 belly | data (HP meaning: a strong guess) |
| The blade window [9, 75) | R3 |
| Blade walk speed 5 m/s; ride threshold `carryFraction` 0.5 (R-M); grab entry at the low end (D7) | **authored** |
| The band chain blade → forearm → upper arm → shoulder → neck → head (radii, walk/hang) | measured (sword recon) |
| The belly band (Bone_KUBI → Bone_HARA, r 2.8 → 1.0, hung) and the shoulder's branch into it | measured (2.8) + **authored** (1.0, the route) |
| Slope gate 60° on walkable bands (D8) | **authored** (global; s3…s11 goldens unchanged) |
| Skelton volumes' hip height (Bone_KOSI, live) | data + R4 |
| His spawn (−300, −1420) | **authored** (no spawn record); its ground 47.657 measured |
| The player start (−301, −1375) | data (`misc/playerstartpositions.txt:4`) |
| Ground step 2 m, the 36 m clamp, the y 34 backstop | **authored** (D9) |
| `bossOnGround` always true; battle area = an on-foot player on the arena map; `swing_space_clear` rays; `ray_ctrl` never reports class 0xc00 | **authored** stand-ins (§13.3) |
| Camera keys, tendril window, death end 8.0 s | **authored** (Quadratus's / Valus's S6 values) |
| The selftest's dodge spot (posed reach + capsule + 0.3 m), its exposure, its strike timings | **authored** (script only, R-L) |

The authored list is the one the spec's success line asks for: the walk/turn rule, the shake strength, the
blade's speed / ride threshold / grab entry, the belly route, the arena values (spawn, clamp, backstop, step),
the §13.3 stand-ins, the reaction readings, the else-edge reading, the camera — every one labelled in source.

## 8 · Deviations

- **The blade grab entry is at its low end** (D7). The game lets you walk on; the tilted disc leaves the low
  end 1.6–2.9 m up, beyond the 1.0 m step, so S12 makes it a grab (`E`).
- **Riding the lift** (rulings R-J, R-M). §13.4's "carried onto the forearm" was replaced: a player on the
  blade's upper part when it lifts keeps holding it, rides it up and climbs onto the forearm through the
  band link (or releases `E` and falls). Below the threshold the player falls (D7). The threshold is
  `carryFraction` 0.5 (bandT ≥ 0.5), not §13.4's 0.4. R-I's 0.5 m carry reach is superseded.
- **The measured blade length is 14.3 m**, not the recon's 11.9 m. From the honest dodge spot the grab
  is at `atk1_3` f27 and the close finds the climber at bandT 0.55: a margin of 0.05 (≈ 0.7 m of blade,
  ≈ 4 frames at 5 m/s).
- **The §13.3 stand-ins**: the arena's collision attributes (ground = owner kinds 0x400000/0x800000),
  `swing_space_clear` (three 35 m rays sampled every 1 m, blocked by a rise > 1.0 m), `ray_ctrl` never
  reporting class 0xc00 — so slams never fail.
- **Additive rows dropped** (§13.7): there is no additive layer, so rows that request additive clips —
  the hurt reactions (`add_head_damage`, `add_hara_front_damage`, …), the arm raises (`add_arm_*_up_*`),
  discovery (`add_insight_dummy`); control bits 13/14/39 — resolve to no clip. He does not flinch.
- **No armour break** (§3): it is set by a failed slam on collision class 0xc00, which is never reported.
- **The water draws opaque** (a `backdrop` plane at y 17.0; the blended pass is deferred, §3).
- **Silent fight** (D10): no curated Gaius audio map; the sidecar's footstep events are where cues attach.
- **The belly band route and `else_orient_def[N] → control-mode N` are authored readings**, not decodes.
  Without the else-edge reading he would stay in control-mode 6 after an attack and never walk again.
  The belly band's taper cuts about 1 m into the chest plate's overhang at y 16–17 (visual).
- **`KNIGHT_PLAYER_OUT` is wired but never fires**: the death locks input, which releases `E` at the
  defeat, so the climber is already off by f109 (`throw_offs=0`).
- **The head shake leaves 1.5 stamina** in the scripted run (survived, barely). The authored drain rule
  makes the head the hardest place: he shakes there 55 % of the time the climber is on it.
- **The y 34 fall backstop is unproven**: wired, never crossed by the scripted run.
- **Behaviour the walk will see**: the first slam fires from 43.8 m, at the spawn, as soon as he senses the
  player; he backs up (`knight_a_back`) to bring the player to 30 m (R1's ATTAIN reading); while the climber
  is on him he walks with them.

## 9 · The runner (`scripts/verify`)

1. **Blessed**: `scripts/verify --bless --only s12` with `SOTC_LIB=out/libviews/s12-gaius`, on `5c2cb97`
   (the commit that adds `scripts/s12-acceptance.sh`; it becomes the `PIN`, so the script is frozen too).
   Two blesses from a clean tree (each runs the section twice: four fresh runs) are byte-identical (`diff -r`
   of the two `golden/s12/` trees and `PIN`: empty). No new mask: Task 9's three (`s12-load`/`s12e2e-load`
   `ms`, `s12-perf` `frame_ms`) cover every moving field. `git diff --stat 8eb25c9 -- tools/verify/golden/`
   lists only the 7 new `s12/gpu/` files, `PIN` and the README paragraph; s3…s11 goldens byte-identical.
   No golden holds an `audio:` line.
2. **Agreement**: a one-value mutation of the blessed golden (`s12-sigil done=head … health=85`) makes
   `scripts/verify --only s12` FAIL naming that line; reverted.
3. **Environment**: CoreAudio refuses output to a process in a background (non-Aqua) launchd session —
   e.g. a `tmux` server started outside the GUI session; `afplay` fails with −66681 there. In that state
   the e2e runs (Task 9 saw it in s8, s11 and s12) print `audio: no output device: CoreAudio error
   (AudioQueueStart): -66681`, and the goldens differ by that line. Run verify, the acceptance and any
   bless from a desktop terminal (`launchctl managername` = `Aqua`; `afplay -v 0
   /System/Library/Sounds/Tink.aiff` exits 0). This run was made that way.
4. **Full run**: after a full `make bake` into the view (ruling R-H, §11: 76 s; every glb, `clips.json`, PNG, the knight
   sidecar and `KNIGHT.graph.json` byte-identical before and after; the only change is three new sidecars,
   `agro`, `colossus_02_bull_a` and `wander` `.events.json`, which nothing reads), `make verify` re-ran every
   step whose library fingerprint moved: rc 0, 387 s wall — ctest 11 s, s3 8 s, s4 18 s, s41 21 s, s42 9 s,
   s5 7 s, s6 18 s, s7 29 s, s8 50 s, s9 99 s, s11 58 s, s12 68 s, all PASS against unchanged goldens (dotnet
   and audio on their stamps). A warm rerun: every step PASS on its stamp, 1.9 s. The app suite: 583
   doctest cases, 248,228 assertions.

## 10 · The user's walk — PENDING

```bash
export SOTC_LIB=<repo>/out/libviews/s12-gaius   # <repo>/out/library after the merge
make gaius        # app/build/sotc-viewer … --manifest out/re/stage/arena_03.manifest
```

| # | Check (design §11) | Do this | Expected | Result |
|---|---|---|---|---|
| 1 | he approaches and swings | stand in front of him | he senses you, faces you and swings: a slam when the ground 35 m ahead is clear, a stab otherwise | ☐ |
| 2 | whether he aims at you (D6) | stand off his line, then on it | the judge volumes open forward; he turns to bring you into his slam | ☐ |
| 3 | the slam | stay on his line within ~30 m | the blade comes down; stepping 6–7 m off his line clears it; a hit hurts | ☐ |
| 4 | the stuck blade | run to its low end after it lands; hold `E` | the grab works during `atk1_3` f9–75 (~2.2 s); the blade is walked at run speed | ☐ |
| 5 | riding the lift | be on the blade's upper half when he pulls it | you keep hold, ride it up, and climb onto the forearm; lower down you fall | ☐ |
| 6 | the climb chain | climb | blade → forearm (walk) → upper arm (hang) → shoulder (walk) → neck (hang) → head (walk) | ☐ |
| 7 | the head sigil | strike (`J`) on the head | three strikes spend it (health 84) | ☐ |
| 8 | a shake-off | stay on him | he shakes the region you are on (`refuse_head`, `_body`, `_arm_r`, `_arm_l`); holding on survives it; the head shake nearly empties the grip | ☐ |
| 9 | the belly sigil | from the shoulder's top, `W` + `D` into the belly route; strike | the last blow kills him | ☐ |
| 10 | the death | watch | `knight_death` plays and holds its last frame; the body stays; **no** return to the shrine | ☐ |
| 11 | the corpse and `R` | press `R` mid-fight, on him, and after his death | he is back at his spot in `route_patrol`, health 170; you on the ground at the start | ☐ |
| 12 | the water | look at the lake | it reads as a lake (opaque, §8) | ☐ |
| 13 | the rim | walk off the disc's edge | no fall-through; below y 34 you are put back at the start | ☐ |
| 14 | window, cursor, fps | move the mouse; watch the title | a 1280×720 window, the cursor free, the fps in the ≥ 60 class | ☐ |
| 15 | which foot he stomps with (R5) | stand to his left, then right | the foot on your side | ☐ |

Known rough edges to expect (§8): the grab, not a walk-on; a thin ride margin (start up the blade early);
no flinch on sigil hits; the opaque water; silence.

**Verdict: PENDING.** S12's done-line closes only when the user has filled this table.

## 11 · The rulings that shaped S12 (`.superpowers/sdd/2026-10-23-s12-gaius/progress.md`)

| Ruling | What it decided | Changes what the user sees |
|---|---|---|
| R-A | Task 7 (band enable/walk/carry fields) before Task 6 (Gaius's table) | no |
| R-B | the attack runner reads judge volumes through a `ScrWorld::playerInJudge` callback | no |
| R-C | `behaviour` and `bake-knight` join `make bake` at Task 10 (done) | no |
| R-D | the Makefile target is `arena03-manifest` | no |
| R-E | tasks ran serially: T1, T2, T3, T4, T7, T6, T8, T5, T9, T10 | no |
| R-F | R1's decoded attack runner (FIX first, then ATK, then the default row) supersedes §13.1's "inside ATK and outside FIX"; walk/turn keeps the ±30° rule | **yes** — which attack he picks and when |
| R-G | the export's `fn` is `"0x832610"` and `control_modes` a dict keyed by index | no |
| R-H | the sidecar build on every frozen bake is accepted; proven at the full `make bake` (done, §9.4: byte-identical glbs) | no |
| R-I | a 0.5 m carry reach (superseded by R-J) | — |
| R-J | a player on the blade's upper part when it lifts rides it up and climbs onto the forearm (replaces §13.4's carry; R-I removed) | **yes** — riding the lift |
| R-K | verify and the bake use the view `out/libviews/s12-gaius`; promotion lands with the merge | no (until the merge) |
| R-L | the selftest dodges the whole slam (every row cleared by overlap); `s12-hit` from its own exposure | **yes** — what the proof shows: a clean dodge is possible |
| R-M | the blade's `carryFraction` 0.5 instead of §13.4's 0.4; margin 0.05 at the close | **yes** — where on the blade the ride starts |

## 12 · What's next

- **The merge**: promote the knight bake and `behaviour/KNIGHT.graph.json` into the base `out/library`
  (`make bake-knight behaviour` with the base `SOTC_LIB`), as S11's A16; until then main's ctest fails
  `test_knight` (R-K).
- **Quadratus onto the interpreter** (his exported MAMMOTH graph: 21/55/105/172), and **Valus from his own
  tables** (the user: "Fix later"); both need a deliberate re-bless.
- **The exporter's two known gaps** (S13 recon): the hard-coded `MOTION_ORIENT` (V/Q/K only) and undecoded
  predicate/action arguments dropped silently.
- **S13** (the systems milestone, design after this merge): the additive layer, `.path` routes, terrain
  zones, all-16 predicates; Phaedra as the proof.

## 13 · Evidence files (all gitignored)

- `out/s12-acceptance-run.log`, `out/s12-s11-regression.log` (in the worktree `.claude/worktrees/s12-gaius`).
- `out/s12/s12-selftest.txt`, `out/s12/s12-e2e.txt`, `out/s12/stills/{blade,head,belly,death}.png`.
- The runner's: `out/verify/s12-gaius-<hash>/` (logs, candidates).
- Task reports and reviews: `.superpowers/sdd/2026-10-23-s12-gaius/task-{1..10}-*.md`.
