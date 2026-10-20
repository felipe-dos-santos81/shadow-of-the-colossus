# S4.1 acceptance results (2026-10-20) — the first colossus

Spec milestone S4.1 (`docs/superpowers/specs/2026-10-20-s4-1-first-colossus-design.md` §2):
**"grab Valus's fur, climb him under stamina, reach a data-derived weakpoint, strike it, survive
the shake-off, and bring him down."** The loop is player-side: Valus reacts and dies; he does not
pursue or attack, and Wander has no health (a builder call recorded in the design's decision table,
§10). Nothing here claims colossus AI, Valus attacks, player damage, cloth, audio, or a reference
silhouette match (S4.2+ / non-goals, design §3).

**Status: the app/converter acceptance is PASS (script) — the user's walk is PENDING.**
`scripts/s41-acceptance.sh` ran green end to end: both builds, both suites, the deterministic
`s41-*` timeline parsed field by field, five stills with PNG/size/span guards, the defeat presence
and HUD pixel guards, the perf measurement, and the unchanged S4 regression (which runs S3 itself).
The milestone's done-line is **not claimed here**: feel, scale, stamina pressure, the HUD, the
gamepad, cursor freedom and fps are the user's walk (§9). The R10 head-band fix is in and the
strike/reaction stills now show Wander (§5); the user's walk still judges the head phase.

| | |
|---|---|
| Date | 2026-10-20 (local −03; the recorded run is stamped `2026-10-20T21:00:26Z`–`21:05:36Z`) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64; 1280×720 points → 2560×1440 px captures |
| Repo / branch | `sotc-native`, worktree `s4-1-first-colossus`; base `2c49107`, this note after it |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Library (gitignored) | `SOTC_LIB=/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library` |
| Encounter manifest (gitignored) | `out/re/stage/encounter.manifest` — **4,391 B**, sha256 `1e4315078995eb6710bcabc7ab3a40dfc19a7cfcdb1698ced83830446f3d3e69`, 30 instances + 1 entity + 1 spawn |
| Wander glb (gitignored) | `out/library/models/shadow/characters/wander/_cmn/wander.glb` — **5,512,768 B**, sha256 `d2c8093d1f8223ad57f0421971e6fffbf0e9b3e521070801cf89e2c21c53f540`, **11 clips** |
| Valus glb (gitignored) | `out/library/models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb` — **38,352,216 B**, sha256 `98b6f7125a8de80dccac3cdd16f4bb8dd1d56dddc2e08037a46f381da55ead86`, **5 clips** |
| Acceptance script | **`scripts/s41-acceptance.sh`** (bash 3.2; the recorded run used `/bin/bash` 3.2.57) |
| Run log (gitignored) | `out/s41-acceptance-run.log` — 128 lines, ends `s41-acceptance: PASS` |
| Captures (gitignored) | `out/s41/` (five stills + `control/`) |

## 1 · The acceptance run

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export SOTC_LIB=/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library
./scripts/s41-acceptance.sh            # exit 0; everything below is from the recorded logs
                                       # (out/s41-acceptance-run.log; the S3/S4 lines from
                                       # out/s4-acceptance-regression.log)
```

The script builds `BpConvert.sln` and `app/`, runs both suites, deletes and recreates `out/s41/`
(so no stale PNG can mask a failed write), runs `--s41-selftest` with `--s41-stills out/s41` and
`--screenshot out/s41/defeat.png`, parses every `s41-*` line field by field, checks the five
stills, runs the stdlib pixel guards, measures `--frames 1` vs `--frames 240`, and finally runs
`scripts/s4-acceptance.sh` unchanged — the S4 script runs `scripts/s3-acceptance.sh` itself, so S3
is not nested twice. The S4 regression recomposes `out/re/stage/encounter.manifest` from the same
S3 source; the recorded sha above is the post-run file.

The individual steps, for reproduction (the script is the source of truth):

```bash
dotnet build BpConvert.sln -v q
cmake -S app -B app/build && cmake --build app/build -j
dotnet test tests/BpConvert.Tests -v q
ctest --test-dir app/build --output-on-failure
./app/build/sotc-viewer --library "$SOTC_LIB" --manifest out/re/stage/encounter.manifest \
    --model models/shadow/characters/wander/_cmn/wander.glb --s41-selftest \
    --s41-stills out/s41 --screenshot out/s41/defeat.png
./scripts/s4-acceptance.sh             # the S4.0 + S3 regressions, unchanged
```

## 2 · The clip record and the two glbs

Task 1 baked the fight clips through S2's existing `--clips` path (no new emission code) and wrote a
sidecar clip record beside each glb. The app resolves clips by **glTF animation name**
(`player_a_grip_loop`, `minotaur_a_death_front`, …), which is authoritative; the sidecar is the
provenance cross-check (Task 1 report). Both artifacts are deterministic: a re-conversion produced
byte-identical glbs.

**Wander — 11 clips:**

| # | name | frames | fps |
|---|---|---|---:|
| 0 | `player_a_stand` | 76 | 15 |
| 1 | `player_a_walk` | 21 | 15 |
| 2 | `player_a_run` | 18 | 30 |
| 3 | `player_a_grip_stand_crouch` | 81 | 15 |
| 4 | `player_a_grip_loop` | 39 | 15.19775390625 |
| 5 | `player_a_grip_move_f1` | 19 | 15 |
| 6 | `player_a_grip_move_f2` | 19 | 15 |
| 7 | `player_a_grip_float_u_hi_attack` | 21 | 15 |
| 8 | `player_a_atk1` | 28 | 30 |
| 9 | `player_a_damage_m_fly2` | 6 | 15 |
| 10 | `player_a_fall_damage_to_death` | 18 | 15.4541015625 |

**Valus (`c01_minotaur_a`) — 5 clips:**

| # | name | frames | fps |
|---|---|---|---:|
| 0 | `minotaur_a_stand` | 26 | 15 |
| 1 | `minotaur_a_add_head_damage` | 36 | 15 |
| 2 | `minotaur_a_add_ude_r_damage` | 31 | 15 |
| 3 | `minotaur_a_add_mune_r_damage` | 31 | 15 |
| 4 | `minotaur_a_death_front` | 94 | 15.08056640625 |

The two sha256s the acceptance records verbatim:

```text
d2c8093d1f8223ad57f0421971e6fffbf0e9b3e521070801cf89e2c21c53f540  wander.glb
98b6f7125a8de80dccac3cdd16f4bb8dd1d56dddc2e08037a46f381da55ead86  c01_minotaur_a.glb
```

The clip names used by the loop are exactly the design's: grip `player_a_grip_loop`, climb
`player_a_grip_move_f1`, strike `player_a_grip_float_u_hi_attack`, fall/stagger
`player_a_damage_m_fly2`; Valus's idle `minotaur_a_stand`, head reaction
`minotaur_a_add_head_damage`, and death `minotaur_a_death_front`. A missing name is a load error
(exit 2) naming the glb and the clip — never a silent fallback to the first clip.

## 3 · Provenance: what is data, what is builder-tuned

**Weakpoints — `derived-mesh`.** The two strike targets are shells in Valus's own converted glb,
selected by material name and weighted to the head/arm bones; the strike proxy is an invented
sphere at the **posed** centroid with a radius from the shell's bbox, labeled `builder-tuned`
because the glb carries no hurt volume.

| Weakpoint | Shell | Verts | Weights | Frame-0 centroid | Route |
|---|---|---|---|---|---|
| forehead | `weakpoint_03` (Mesh_8) | 730 | `Bone_KAO` 98.4 % | (−0.91, 15.79, 3.90) | leg → torso → neck → head |
| right upper arm | `weakpoint_04` (Mesh_4) | 204 | `Bone_JOWA_R` 87 % | (−2.76, 10.61, −3.40) | leg → torso → right arm, from behind |

`trigger_02` (left shin) is treated as a climb trigger; `minotaur_a_add_leg_l_weakpoint_damage` is
recorded as an unexplained data hint, not a third weakpoint. The game's
`cp3shadow/col/minotaur_a_weak_coli.clb` (11,288 B, two objects) is still undecoded; it is the named
upgrade path, not a dependency.

**Constants — `builder-tuned`.** The stamina math was rescaled until one bar closes with margin at
the forehead; the design records the numbers and the builder check (`design §7.2`, §11.1).

| Field | Value | Unit |
|---|---:|---|
| `grabRange` | 0.9 | m |
| `climbSpeed` | 1.2 | m/s |
| `staminaHang` | 2.5 | /s |
| `staminaClimb` | 4.0 | /s |
| `staminaStrike` | 8.0 | /s |
| `staminaRegen` | 25.0 | /s |
| `staminaRegenDelay` | 1.0 | s |
| `chargeSeconds` | 0.9 | s |
| `fullChargeFraction` | 0.75 | — |
| `catchSeconds` | 0.6 | s |
| `catchRange` | 2.5 | m |
| `staggerSeconds` | 1.2 | s |
| `damageWeak` | 20.0 | HP |
| `damageFull` | 34.0 | HP |
| Valus HP | 100.0 | HP |

**R10 band retune.** The climb band table's neck radius is **1.6 m** (was 1.4) and the head
**1.75 m** (was 1.2); `app/tests/test_grab.cpp` pins both, and `app/tests/test_player_state.cpp` pins
the head surface point at `1.75 + 0.35 = 2.10 m`. The head teleport's hint points at the head's
front (the forehead side) rather than the last shin position.

**The eboot stat pass was a closed negative (Task 1, Step 8).** The bounded pass over `eboot.elf`
found no base HP/stamina/grip value: `WanderHealthBarMax` / `WanderGripBarMax` registration blocks
carry no numeric field, and the only numbers near them are the debug-menu bounds
(100 / 570 / 700) the spec explicitly excludes. The builder-tuned pool and drains above therefore
stand as declared, not decoded.

## 4 · The fight timeline and the `s41-status` line

`--s41-selftest` runs the S4 approach unchanged, then a fixed 1/60 s, no-input-device timeline:
grab, a 240-frame climb, a full-charge forehead strike, a catch, a deliberately missed catch, a
forced zero-stamina fall, the killing blow, and the post-kill settle. The status line, verbatim:

```text
s41-status grab=left_foot climb=4.77 strikes=3 entity_health=0 mode=grounded defeated=1
```

The event lines, verbatim:

```text
s41-grab frames=522 band=left_foot pos=380.454,28.425,369.175
s41-climb frames=240 from_y=28.425 to_y=33.199 band=left_shin
s41-strike player_px=1147,661,273,93
s41-strike swing=1 damage=34 health=100->66 weakpoint=0
s41-catch band=head
s41-strike swing=2 damage=20 health=66->46 weakpoint=0
s41-fall frames=102 landed=1
s41-stamina fall=1
s41-strike swing=3 damage=34 health=34->0 weakpoint=0
s41-death clip=minotaur_a_death_front defeated=1
s41-settle frames=177 grounded=1
s41-defeat bbox=325,188,2559,1317 screen=2560x1440
```

- **Grab.** The approach walks 522 frames in two legs (the S4 stop at the knee, then the fixed aim
  at the nearest band surface) and attaches to `left_foot` at `(380.454, 28.425, 369.175)`. The
  grab is a real `player_step` attach: the position is the band surface, not the spawn.
- **Climb.** The 240 fixed frames rise **4.77 m** (28.425 → 33.199) with no backwards step, ending
  on `left_shin`. This is the real, continuous-grip climb the acceptance proves; it is **not** the
  whole 17.7 m route. The strike phases use the ruled test scaffolding that re-attaches the player
  on the head band (the user's walk does the full route by hand). The climb line's `band=left_shin`
  is the band at the end of the frame window; `s41-grab` records the initial `left_foot`.
- **Strike 1.** Full charge (0.9 s held) deals **34**, `100 → 66`, `weakpoint=0` (forehead). The hit
  shakes the player off and opens the 0.6 s catch window. The `s41-strike player_px` line is the
  player's own projected bounds in the strike still — `1147,661,273,93` (x, y, w, h, inclusive) —
  and the acceptance asserts it is non-empty, on-screen and inside the 2560×1440 frame. **R10**
  raises the head band from 1.2 → **1.75 m** (the forehead shell reaches ~1.73 m from `Bone_KAO` in
  z) and the neck from 1.4 → **1.6 m**; the head teleport's surface hint now points from the head's
  front (the side the forehead faces), not from the last shin position, so Wander is visible on the
  front of the head in both the strike and reaction stills instead of clipped inside the shell.
- **Catch.** A fresh grab press catches the **`head`** band. R10's larger head band is the nearest
  band at the shake-off point (the pre-R10 catch was the neck); the 0.6 s window is unchanged.
- **Strike 2.** A one-third charge deals the weak damage **20**, `66 → 46`. The catch is the head,
  so this weak strike is on the forehead weakpoint too (`weakpoint=0`); it exercises the weak-damage
  path. (The pre-R10 neck catch's `weakpoint=-1` is no longer the geometry.)
- **Missed catch.** The next press is deliberately withheld for 0.7 s, so the window expires; the
  fall runs **102** frames to the ground, `landed=1`.
- **Stamina.** The pool is forced to zero while attached; the grip breaks **exactly once**
  (`s41-stamina fall=1`; the selftest asserts the next held step reports no second break). A
  head-height fall lands on the foot/leg axis, where the leg capsules block a scripted straight walk
  to a band; the re-grab is therefore placed on the nearest band's surface (the same ruled
  scaffolding class as the head teleport) and grabs `left_foot`. The user's walk does the real
  re-climb.
- **Kill.** The selftest sets HP to exactly one plunge (34) before the blow, proving the death path
  in one hit; the kill's own line is `s41-strike swing=3 damage=34 health=34->0 weakpoint=0` (the
  34 HP set-up is source-derived from `damageFull`). The reported full sequence is 34 (100→66), 20
  (66→46), then the set-up 34 (34→0). The natural fight is three clean plunges (100→66→32→0) or a
  messier mix — the selftest's weak hit makes its own sequence four — and is the user's walk. Valus
  plays `minotaur_a_death_front` once, freezes on its last frame, and `defeated=1`; the player is
  thrown clear.
- **Settle (ruling R9a).** The death blow leaves the player `Falling`; the final phase advances the
  same no-input step until the fall lands and the 1.2 s stagger resolves. **177 frames** later the
  player is `Grounded`, on the heightmap, and the defeat still is rendered with the player landed —
  not suspended at the head.
- **Status.** `strikes=3` counts the full, weak and killing blows; `entity_health=0`;
  `mode=grounded` (the R9a settle); `defeated=1`.

The timeline's event/status lines reproduced identically across the repeated runs of this session
(the still bytes differ in the last digits because the HUD label and GPU readback are frame-state
artifacts, not timeline state). The selftest is deterministic because every step is 1/60 s and no
window or gamepad input is read.

## 5 · The stills

All captures are 2560×1440. The four mid-fight stills are written by `--s41-stills` at the phase
boundary; the defeat still is the selftest's final frame. Each asserts `\x89PNG` magic, > 5000 B,
**2560×1440**, and the viewer's own non-uniformity span (largest channel span ≥ 30 from
`s1-pixels`); the strike still also asserts the `s41-strike player_px` box is non-empty, on-screen
and inside the frame. Stale PNGs are deleted with `out/s41/` before the run.

| file (`out/s41/`) | bytes | span | what it shows |
|---|---:|---:|---|
| `grab.png` | 9,584,304 | 254 | Wander crouched at the left foot ring in the grip clip; stamina 100 |
| `mid-climb.png` | 9,472,150 | 255 | Wander on Valus's left shin, climb camera; stamina 92 |
| `strike.png` | 9,391,869 | 255 | the forehead weakpoint rune at full charge, with Wander at the head's front (`player_px=1147,661,273,93`); HUD stamina 76, health 100 |
| `reaction.png` | 9,974,148 | 255 | Wander re-gripped on the front of the head while the head reaction plays; HUD health 66 (two-thirds bar), stamina 76 |
| `defeat.png` | 9,497,981 | 254 | Valus on the ground and Wander landed at the corpse; stamina 92, no health bar (0 HP) |

The defeat still pulls the follow camera to its 12 m wheel ceiling so both the landed player and the
17.7 m corpse fit the frame; the interactive default remains 4.5 m (the selftest ends at the still).

**R10 fixed the head clipping.** The head band is now 1.75 m (+0.35 m player = 2.10 m from the
`Bone_KUBI` → `Bone_KAO` axis) and the neck 1.6 m, and the head teleport's surface hint points from
the head's front towards the spawn — the side the forehead shell faces. Wander is visible at the
head's front in the strike still (the legs/body at the right of the measured box), and the reaction
still (captured after the catch, while the head reaction is still playing) shows him re-gripped on
the front of the head. The acceptance now asserts the strike still's `player_px` is non-empty,
on-screen and inside 2560×1440, so a future radius regression cannot pass with him invisible; the
pixels-plus-bbox guard is the check, and the user's walk is the visual judge.

### The defeat presence and localization guard

The same final frame is rendered twice more: with the entity's meshes translated out of view
(`control/defeat-noentity.png`) and with the HUD hidden (`control/defeat-nohud.png`). The no-entity
diff proves the entity is drawn and localizes it; the no-HUD diff proves the overlay is drawn. The
recorded lines:

```text
s41-defeat-region pixels=2525550 span=238 changed=1066514 changed_pct=42.229 outside=0 bbox=326,188,2559,1254
s41-defeat-hud changed=14005 outside=0 stamina_changed=14005 health_changed=0
s41-reaction-hud changed=24733 outside=0 stamina_changed=12061 health_changed=12672
s41-hud-bars stamina_green=11520 health_red=19200 stamina_pixels=12025 health_pixels=20417
```

- **Presence/localization.** **42.229 %** of the `s41-defeat` bbox region differs from the
  no-entity control (need ≥ 5 %); **0** pixels differ outside the bbox (+16 px). The projected
  entity bbox is `325,188,2559,1317`; the differing pixels are `326,188,2559,1254`. The corpse
  still reaches the right frame edge, but the box localizes both axes (x 325…2559, y 188…1317) and
  every differing pixel is inside it.
- **Defeat HUD.** The only HUD difference from the no-HUD control is the bottom-left stamina
  bar/label (`stamina_changed=14005`, `health_changed=0`): at **0 HP** the 600-px health bar has zero
  width, so no health pixels are drawn. That is the arithmetic the design asks the HUD to show.
- **Reaction HUD.** The 66 HP reaction frame differs from its no-HUD control in **both** bands:
  `stamina_changed=12061`, `health_changed=12672`, all inside the two HUD rects (`outside=0`).
- **Bar colours.** The grab still's stamina bar is green in **11,520** of its 12,025 bar pixels; the
  strike still's full health bar is red in **19,200** of its 20,417. The bars are drawn, not just
  present as geometry.

## 6 · Performance

`--frames 1` vs `--frames 240`, same encounter manifest, player and capture path:

| run | wall time |
|---|---:|
| `--frames 1` (load-dominated) | **10.058 s** |
| `--frames 240` | **11.625 s** |
| marginal (239 frames) | **6.56 ms/frame ≈ 152.4 fps** |

The gate is **< 16.667 ms** (60 fps); the 240-frame run also printed
`s1-pixels min=0,0,0 max=254,254,254 mean=103.0,97.1,88.9` and wrote a 2560×1440 capture, so the
marginal number is a real 240-frame run, not a load-only success. The S4 regression measured
**8.51 ms/frame** in the same session. The loop is vsync-paced; the load is the fixed 30-glb arena
+ 152 textures + the 38 MB Valus glb, a per-run startup cost. The ~2 ms difference between the two
runs is machine noise, not scene cost.

## 7 · Suites and the regressions

- `dotnet test tests/BpConvert.Tests -v q`: **93 passed**, 0 failed (50 s).
- `ctest --test-dir app/build --output-on-failure`: **2/2 passed** (unit 9.43 s, windowed 0.19 s;
  total 9.62 s).
- `scripts/s4-acceptance.sh` (unchanged, run inside the S4.1 script): **exit 0, PASS**.
  The S4 script runs `scripts/s3-acceptance.sh` itself, so S3 was not run twice.

```text
s4-status instances=30 rigs=2 entity=minotaur_a_stand placement=builder-pick spawn=data-position+builder-yaw feet=ok entity_bbox=1123,237,1485,751 pos=378.187,28.430,369.028 frames=423
s3-status instances=30 spawn=309.312,4.394,285.403 pos=309.312,29.802,305.405 ground=ok frames=240
s3-acceptance: PASS — build, ctest, 30-instance composed scene (ground=ok), spawn PNG (\x89PNG magic, >5000 B, non-uniform pixels), wave guard (<2 % delta, border untouched)
```

The S4/S3 manifests, scripts and acceptance paths are untouched and remain valid. Their stills now
carry the S4.1 HUD (it is a manifest-mode overlay), and their pixel guards stayed green:
`s4-region ... outside=0` and `s4-idle-pixels ... outside=0` in the recorded regression. This re-run
does **not** close either human walk: S3's arena entrance/ramp feel and S4.0's bowl approach remain
the user's.

## 8 · Known limitations and fidelity gaps

- **No AI, no Valus attacks, no player health.** Valus reacts to a hit and dies; he never pursues,
  stomps, or damages Wander. This is the design's builder call (§10), not an unfinished loop: the
  milestone ends at "he falls".
- **The head-band fix is a band-radius fix, not a mesh-surface rule.** R10 sets the head band to
  1.75 m and the neck to 1.6 m; the hit stays band/weakpoint-routed, not a mesh-surface proximity
  test. The strike still is a close-up of the head (Wander's `player_px` box is 273×93 at the
  head's front); he is visible there and clearly visible in the reaction still after the catch, but
  the still is not a full-body portrait. The head is wider at its sides than the 2.10 m surface; the
  stills use the front (the natural route from the neck), and a side orbit may still read as a
  float/clip at 1.75 m. The user's walk judges whether the head phase reads.
- **Catch lands on the head band.** R10's larger head band is the nearest band at the shake-off
  point, so `s41-catch band=head`; the weak strike is therefore on the forehead weakpoint
  (`weakpoint=0`), not the pre-R10 neck's `-1`. The post-landing re-grab is placed on the nearest
  band's surface (`left_foot`) as test scaffolding because a head-height fall lands on the leg axis
  and the leg capsules block a scripted straight walk; the user's walk does the real re-climb.
- **The selftest's kill is scaffolded.** HP is set to exactly one plunge (34) before the killing
  blow so the death path is proven in one hit; the kill's own `s41-strike swing=3 … 34->0` line is
  printed, and the 34 HP set-up is source-derived from `damageFull`. The natural three-plunge fight
  is the user's walk.
- **Gamepad hardware unverified.** The mapping (left stick move, right stick camera, RT grip, X
  strike, Start pause, hot-plug) is unit-tested headlessly; no physical pad was available in this
  run. If the user has one, the walk records it; otherwise it stays "hardware unverified", never
  claimed.
- **HUD in every manifest still.** The stamina/health overlay is visible for all `--manifest` runs,
  so the S3/S4 stills now show full bars as well as the S4.1 stills. Their pixel guards stayed green;
  if the user wants HUD-free S3 stills, gating `set_hud` on `!manifest.entities.empty()` is a
  one-line change (Task 6 report).
- **No reference capture / no silhouette match.** S4.0 excluded the dev port and no Valus capture
  exists; the pose validation is decode-vs-bind plus internal geometry. These stills are a rendered
  view, not a game-reference comparison.
- **Feet delta is 7 cm.** The S4.0 entity's lowest posed vertex is −0.070 m from the sampled ground
  (inside the 0.10 m gate); the walk is the visual judge. Inherited, unchanged.
- **No ragdoll.** Valus's death is the `minotaur_a_death_front` clip played once and frozen on its
  last frame; the player is thrown clear, not simulated against the collapse.
- **No cloth, shell fur, or eye glow.** The banner is static/opaque, the fur layer is absent, and the
  weakpoint/eye decals draw flat/opaque (S4.0 gaps, design §3 non-goals).
- **No animation blending.** Stand/walk/run and the mode clips switch by hard cuts at t = 0; S2's
  foot-sliding limit remains.
- **The 4.77 m climb is not the whole route.** The selftest proves the real grip climb from the foot
  band to the shin, then uses the ruled head teleport for the strike phases. The user's walk does
  the full leg→torso→neck→head route by hand.
- **Defeat still uses a 12 m capture camera.** The follow camera is pulled to its wheel ceiling only
  for the final still so the corpse and the landed player share the frame; interactive play keeps
  the 4.5 m default.

## 9 · The user's walk — PENDING

The scripted pass cannot judge feel, stamina pressure, scale, cursor freedom, HUD legibility, the
gamepad, or the title-bar fps. Run the interactive loop from `sotc-native/` and fill the result
cells:

```bash
export SOTC_LIB=/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library
./app/build/sotc-viewer --library "$SOTC_LIB" --manifest out/re/stage/encounter.manifest \
    --model models/shadow/characters/wander/_cmn/wander.glb
```

Controls: `W`/`A`/`S`/`D` walk, `Shift` runs, `E` grabs (hold), `J` charges a strike (hold; release
to plunge), left-drag turns the view, wheel zooms, `T` toggles the entity framing, `F` recenters
behind the player (or re-seeds the climb orbit), `Space` pauses, `R` restarts, `Escape` quits.
Gamepad: left stick moves, right stick turns the camera, RT grips, X/square strikes, Start pauses.
The cursor is never captured.

| # | check | do this | expected | result |
|---|---|---|---|---|
| 1 | approach and scale | walk (then run) to Valus; stand at a foot and orbit | the feet sit on the bowl floor; the body reads ~17.7 m (~14× Wander), not a toy or a giant | ☐ |
| 2 | grab | hold `E` at the foot/leg | the grip clip plays; the stamina bar starts draining; Wander is on the surface, not floating or inside the leg | ☐ |
| 3 | climb | climb the leg → torso → neck → head with `W`; watch stamina | the climb camera stays outside the body; the route is readable; the bar has margin at the forehead; releasing `E` or emptying the bar drops you | ☐ |
| 4 | forehead strike | at the sigil, hold `J` to full charge, release | the blow lands on the forehead, Valus reacts, Wander is shaken off; a fresh `E` inside 0.6 s re-grips | ☐ |
| 5 | catch and missed catch | re-grip after the shake-off; then deliberately miss the next catch | a catch lands on the head (R10's nearest band); a miss means the ground, a stagger, and a walk back to a band | ☐ |
| 6 | forced stamina fall | hold on until the pool empties | the grip breaks once, Wander falls, lands and recovers; no repeated break while `E` is still held | ☐ |
| 7 | kill and death | land the remaining plunges; watch Valus | the death clip plays once and freezes; the HUD reads defeated; Wander lands clear; the camera returns to follow | ☐ |
| 8 | fps, cursor, HUD, quit | walk while reading the title; move the mouse; read the bars; press `Escape` | the title's fps readout is in the ≥60 class (this Mac's vsync rate); the cursor stays visible and free; the HUD is legible; Escape quits cleanly | ☐ |
| 9 | gamepad (if a pad is available) | connect a standard SDL3 gamepad; use both sticks, RT and X | the mapping matches the controls above; hot-plug opens/closes it; removal clears its inputs | ☐ |

**Verdict: PENDING — the user's.** S4.1's done-line closes only when this table is filled and
passes; nothing in §1–§8 claims it. Item 9 stays "hardware unverified" if no pad is available; that
is a recorded result, not a pass.

## 10 · Evidence files (all gitignored)

- `out/s41-acceptance-run.log` — the recorded run's full output (128 lines; quoted above).
- `out/re/stage/encounter.manifest` — 4,391 B, sha256 `1e431507…3d3e69` (30 + 1 + 1).
- `out/s41/` — `grab.png`, `mid-climb.png`, `strike.png`, `reaction.png`, `defeat.png`;
  `control/defeat-noentity.png` (presence control), `control/defeat-nohud.png` (HUD control),
  `control/reaction-nohud.png` (HUD control), and `control/s41-pixels.py` (the stdlib guard). The
  script deletes `control/perf.png` after each timed run, leaving `control/perf-1.log` and
  `control/perf-240.log`.
- `out/library/.../wander.glb` — 5,512,768 B, sha256 `d2c8093d…c53f540`; `c01_minotaur_a.glb` —
  38,352,216 B, sha256 `98b6f712…55ead86`.
- `out/s4-acceptance-regression.log`, `out/s3-acceptance-regression.log` — the unchanged S4/S3
  regression outputs.
- `out/re/capture/s4/`, `out/re/capture/s3/` — the regression captures (now HUD-bearing).

## 11 · Cross-references

- Design: `docs/superpowers/specs/2026-10-20-s4-1-first-colossus-design.md` (§2 goals, §3
  non-goals, §7.2 climb/stamina, §7.3 damage/provenance, §7.4 acceptance, §10 decisions, §11
  builder resolutions).
- Plan: `docs/superpowers/plans/2026-10-20-s4-1-first-colossus.md`; recon: `docs/notes/s4-recon.md`.
- Task records: `.superpowers/sdd/2026-10-20-s4-1-first-colossus/task-1-report.md` (clips, shas,
  eboot negative), `task-2-report.md` (band table), `task-3-report.md` (state machine), `task-6-report.md`
  (wiring, selftest, the one-frame clip lag and head-still note).
- S4.0 results: `docs/notes/s4-results.md`; S3 arena: `docs/notes/s3-results.md`; S2 clips:
  `docs/notes/s2-results.md`; pose validation: `docs/notes/s4-pose.md`.
- Script: `scripts/s41-acceptance.sh` (this pass); the recorded run's log is
  `out/s41-acceptance-run.log` (§10).
