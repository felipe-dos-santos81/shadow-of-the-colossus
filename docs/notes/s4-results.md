# S4.0 acceptance results (2026-10-19) — the encounter

Spec milestone S4.0 (`docs/superpowers/specs/2026-10-19-s4-encounter-design.md` §2): **"Valus
stands in the Valus arena — converted from his own `.cmsh`/`.cskl`, alive with his decoded
`minotaur_a_stand` clip — and you can walk up to him."** Presence, placement, approach; nothing
here claims a fight, a climb, or colossus AI (S4.1/S4.2).

**Status: the app/converter acceptance is PASS (script) — the user's walk is PENDING.**
`scripts/s4-acceptance.sh` ran green end to end: build, both suites, the 30-instance arena gate,
the encounter manifest (30 instances + 1 entity + 1 spawn), the self-test (`feet=ok`, collision
stop, no pass-through), five stills, the entity-region and idle-pair pixel guards, the perf
measurement, and the unchanged S3 regression. The milestone's done-line is **not claimed here**:
the feel/scale/cursor/fps items are the user's walk (§9), and the placement is a **labeled builder
pick**, not game data (§2).

| | |
|---|---|
| Date | 2026-10-19 (local −03; the recorded run is stamped `2026-10-20T00:46:24Z`–`00:53:52Z`) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64; 1280×720 points → 2560×1440 px captures |
| Repo / branch | `sotc-native`, `s4-encounter`; acceptance at `b9d64cc` (the perf-gate fix), this note after it |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Library (gitignored) | `SOTC_LIB=/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library` |
| S3 manifest (gitignored) | `out/re/stage/valus.manifest` — sha256 `088e621894ecd38312526bdac11916fb355a645ecef4d0bace91e53b2440c1d0`, 30 instances |
| Encounter manifest (gitignored) | `out/re/stage/encounter.manifest` — **4,360 B**, sha256 `a94a43819d443c16cddc0d554dc4a0d798420f7c5221e64b3d305ee9653a748a`, 30 instances + 1 entity + 1 spawn |
| Valus glb (gitignored) | `out/library/models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb` — 38,090,040 B, sha256 `398b6814fbb99bf6ef8c94578d7e2185af5ffcaa3be583c178adc08743943275` |
| Acceptance script | **`scripts/s4-acceptance.sh`** (bash 3.2; the recorded run used `/bin/bash` 3.2.57) |
| Run log (gitignored) | `out/s4-acceptance-run.log` — 137 lines, ends `s4-acceptance: PASS` |
| Captures (gitignored) | `out/re/capture/s4/` (+ `control/`) |

## 1 · The acceptance run

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export SOTC_LIB=/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library
./scripts/s4-acceptance.sh            # exit 0; everything below is from out/s4-acceptance-run.log
```

The script builds `BpConvert.sln` and `app/`, runs both suites, re-checks S3's arena manifest
(`check-manifest`), composes and gates the encounter manifest (`compose-encounter`,
`check-encounter`), runs `--s4-selftest`, takes the stills, runs the pixel guards, measures
`--frames 1` vs `--frames 240`, and finally runs `scripts/s3-acceptance.sh` unchanged. Each capture
file is deleted before its run, so a stale PNG cannot mask a failed write; the perf gate requires
both timed runs to exit 0 and write a capture (`b9d64cc`'s fix — see §6).

The individual steps, for reproduction (the script is the source of truth):

```bash
python3 tools/re/stage/probe.py check-manifest --library "$SOTC_LIB"
python3 tools/re/stage/probe.py compose-encounter --library out/library \
    --s3-manifest out/re/stage/valus.manifest --out out/re/stage/encounter.manifest
python3 tools/re/stage/probe.py check-encounter --library out/library
./app/build/sotc-viewer --library "$SOTC_LIB" --manifest out/re/stage/encounter.manifest \
    --model models/shadow/characters/wander/_cmn/wander.glb --s4-selftest \
    --screenshot out/re/capture/s4/s4-approach.png
```

## 2 · The encounter manifest and its provenance

The composer reads S3's 30-instance composition, carries those instance lines verbatim (identity
transforms, one source for the arena), re-samples both ground heights on
`arena_01_ground.glb`, writes a **new** provenance header (S3's header is not inherited), appends
one `entity`, and rewrites `spawn`. Composition is deterministic for identical arguments
(byte-identical re-run). The header and the two directives, verbatim:

```text
# placement=builder-pick entity=models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb clip=minotaur_a_stand
#   bowl floor (382, 370), measured ground 28.369 m, yaw -130.763605 deg (q 0.000000 -0.909104 0.000000 0.416570),
#   facing the spawn.  Builder pick, not data: no shipped record satisfies the
#   (a)+(b)+(c) bar (docs/notes/re4-placement.md §7-8).
# spawn=data-position+builder-yaw position (353, 345), measured ground 27.951 m, yaw 49.236395 deg;
#   x/z from misc/playerstartpositions.txt:1 (`353 29 345`, y=29 is not the feet
#   height), y re-sampled on arena_01_ground.glb, yaw builder-chosen to face the entity.
entity models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb 382 28.369 370 0 -0.909104 0 0.416570 1 1 1 minotaur_a_stand
spawn 353 27.951 345 49.236395
```

**The placement pull was negative.** No shipped record satisfies the design's three-part bar
(named/typed as the MinA spawn, inside the bowl near the floor, x/z + yaw without an unknown frame
origin): the 27 MinA-named arena records are sensors/relays 17–25 m above the floor; no live
colossus instance exists in the `.csml` lists; the entity's own `.cgpr` is identity at the origin;
the `.path` file has no frame anchor; and the `.cwks`/`.fs` families carry dependencies and hashes,
not placements. Evidence and every rejected candidate: `docs/notes/re4-placement.md` §2–§8. **The
entity's transform is therefore a labeled builder pick, never presented as game-derived.**

| field | value | provenance |
|---|---|---|
| entity placement | `builder-pick` | chosen on the open bowl floor, facing the spawn |
| entity x / z | **382.000 / 370.000** | builder pick |
| entity y (manifest) | **28.369 m** | ground sample at the column (not the model origin) |
| entity yaw | **−130.763605°** (q `0, −0.909104, 0, 0.416570`) | faces the spawn |
| entity clip | `minotaur_a_stand` | baked into `c01_minotaur_a.glb` (Task 1) |
| spawn x / z | **353.000 / 345.000** | **data**: `misc/playerstartpositions.txt:1` (`353 29 345`) |
| spawn y | **27.951 m** | ground sample; the file's `y=29` is not the feet height |
| spawn yaw | **+49.236395°** | builder-chosen to face the entity |
| spawn provenance | `data-position+builder-yaw` | position data, yaw builder |

The gates, verbatim from the log:

```text
PASS  measured  spawn y == the sampled ground (27.951)                     expected ±0.001, measured manifest 27.951, ground 27.951
PASS  measured  entity y == the sampled ground (28.369)                    expected ±0.001, measured manifest 28.369, ground 28.369
PASS  measured  entity faces the spawn (quaternion yaw)                    expected ±0.01 deg, measured -130.763565 (expected -130.763605, delta 0.000040)
PASS  measured  spawn faces the entity (yaw)                               expected ±0.01 deg, measured 49.236395 (expected 49.236395, delta 0.000000)
PASS  measured  entity yawed bind footprint inside the bowl                expected inside, measured x[366.521,396.776] z[353.255,386.139]
PASS  measured  spawn-entity distance < 100 m                              expected < 100, measured 38.288 m
```

`check-encounter`: **22/22** gating assertions, gate MET. S3's `check-manifest` re-run first:
**11/11**, gate MET. The entity's yawed bind footprint is fully inside the bowl (north margin
3.90 m); the spawn is outside it. The spawn-to-entity distance is 38.288 m; the spawn's clearance
to the nearest **collision capsule** is separately measured at 34.810 m (§4).

The header records the absolute S3 source path, so the encounter manifest's sha256 pins this
checkout's file; the coordinates and the composition are path-independent, and same-argument runs
are byte-identical (`docs/notes/re4-placement.md`, Task 6 report §7).

## 3 · Valus's clip and pose validation

Task 1 re-converted the colossus through S2's existing `--clips` path (no new emission code) and
validated the mapping and pose; the full record is `docs/notes/s4-pose.md`.

| property | value |
|---|---|
| glb | 38,090,040 B; 12 meshes; 1 skin / **238 joints**; 476,666 vertices in the composed scene |
| animation | **1** — `minotaur_a_stand` |
| clip shape | **26 frames**, 15 fps, **1.6667 s** (25/15); **58 rotation tracks** (21 static + 37 keyed) + `Bone_root`'s translation = **59 channels** |
| mapping | **58/58** tracks resolve to **58 distinct joints** of the 238-joint rig; 0 unresolved; the 13 type-`0x0D` tracks included |
| bridge residual | max **12.90°** (`Bone_KO_A_L`); 54/58 records ≤ 1°; composed bind vs the glb world matrices **1.07e-14** |
| loop closure | worst first-to-last rotation **0.0286°** (`Bone_KUBI`); 31/37 keyed tracks byte-identical first/last |

**Vertex count reconciliation.** The main glb's 12 meshes carry **476,666** vertices; the
**523,865** figure in `docs/notes/m0-results.md` is the **seven-file directory total** (246 meshes
across the main, LOD1, hiza, weapon, collision and related files), not this glb.

Frame-0 world heights (the values the C# world-pose test pins, ±0.02 m):

| joint | y (m) |
|---|---:|
| `Bone_KOSI` (pelvis) | **7.9896** |
| `Bone_MUNE` | **10.9251** |
| `Bone_KUBI` | **14.6764** |
| `Bone_KAO` (head) | **15.1153** |
| `Bone_TUMASAKI_R` (lowest body joint) | **−0.0031** |

Head above the pelvis by **7.1257 m**; total height (`KAO` − lowest foot) **15.1184 m**; feet
lowest among the `Bone_*` body joints; the loop keeps the head above the pelvis
(6.9763–7.1257 m) and the feet at −0.0032–0.0566 m. The pose is upright, asymmetric in the
authored way, and **not folded or mirrored**. The RE tool's generic verify passes **9/9**; the
player regression still passes **17/17**; the full converter suite is **90/90**.

**What this establishes:** the mapping and the bridge convention hold cross-file on Valus, and
the written glb's frame-0 pose is upright at the decoded heights. **What it does not:** a
silhouette comparison — **no Valus capture exists** (the S2 xval was player-only). The acceptance
stills (§5) are a rendered view of the pose and show it upright, but they are not matched against
a game reference.

## 4 · `s4-status` — the self-test

`--s4-selftest` composes the encounter manifest, pins the entity's clock at **t = 0**, asserts the
spawn is outside every reachable entity capsule, measures the entity's lowest posed vertex against
the ground, computes the entity's projected screen bbox under the spawn-follow camera, walks the
player forward+run at a fixed 1/60 s toward the nearest leg capsule until the collision stops it,
renders the final frame (saved as `s4-approach.png` when `--screenshot` is given), and exits 0. The
status line, verbatim:

```text
s4-status instances=30 rigs=2 entity=minotaur_a_stand placement=builder-pick spawn=data-position+builder-yaw feet=ok entity_bbox=1123,237,1485,751 pos=378.187,28.430,369.028 frames=423
```

Supporting lines, verbatim:

```text
s4-spawn capsules=8 clearance=34.810 spawn=353.000,27.951,345.000
s4-entity clip=minotaur_a_stand joints=238 lowest=28.297 ground=28.368 delta=-0.070 bbox=1123,237,1485,751 screen=2560x1440
s4-walk frames=423 stop=yes aim=379.600,0.000,370.372 pos=378.187,28.430,369.028 clearance=-0.000 penetration=0.000 stop_before=1.950 grounded=0.0000
```

- **Two rigs.** The composed scene is 134 meshes / 931,601 vertices / 128 materials / 163
  textures, with the player (348 joints) and the entity (238 joints) on independent clocks. Against
  the no-entity control (122 meshes / 454,935 vertices / 116 materials / 152 textures), Valus adds
  **12 meshes / 476,666 vertices / 12 materials / 11 textures**.
- **Spawn guard.** 8 reachable capsules; the spawn is **34.810 m** clear of the nearest one (foot
  radius + player radius included). The self-test exits 1 if any reachable capsule contains it.
- **Feet.** Lowest posed vertex **28.297 m**, the app heightmap at the entity column **28.368 m**,
  delta **−0.070 m** — 7 cm on a 17.7 m body (0.4 % of height), inside the pinned **0.10 m**
  tolerance → `feet=ok`. The manifest's own ground sample is 28.369 m (the composer's ray-cast);
  the app's heightmap sample at the entity column is 28.368 m, 1 mm lower.
- **Collision.** The fixed-step walk stops after **423** frames at final clearance **0.000 m**,
  penetration **0.000** (bound 0.02 m), **1.950 m** before the aim point on the nearest leg
  capsule axis; no pass-through; maximum ground deviation **0.0000 m**. `pos` is the **player's**
  post-walk position; the aim `(379.600, 0.000, 370.372)` is the closest XZ point on the capsule
  axis. The push-out is opposite the motion — a stop, not a slide.

The self-test is deterministic (entity t = 0, fixed 1/60 s steps), so the status numbers above are
reproducible without the interactive path.

## 5 · The stills and the pixel guards

All captures are 2560×1440. The four `capture` stills are taken with `--frames 1 --anim-time 0`;
the approach frame is the self-test's final frame. Stale PNGs are deleted before the run.

| file (`out/re/capture/s4/`) | bytes | what it shows |
|---|---:|---|
| `s4-wide.png` | 10,794,441 | spawn view, `--camera-target player`, entity t=0: Wander's back, Valus ahead on the bowl floor |
| `s4-approach.png` | 9,606,345 | the self-test's final frame: Wander at Valus's near foot, stopped by the proxy |
| `s4-portrait.png` | 11,278,843 | `--camera-target entity`, full body, face-on from the spawn side; feet on the floor |
| `s4-idle-t0.png` | 10,794,441 | idle pair t=0 (byte-identical to the wide still: same camera and time) |
| `s4-idle-t08.png` | 10,792,569 | idle pair t=0.8, same spawn-follow camera |

The four `capture` stills assert `\x89PNG` magic, > 5000 B, and the viewer's own non-uniformity
(largest channel span ≥ 30 from `s1-pixels`). The approach still asserts magic and > 5000 B; its
run printed `s1-pixels min=0,0,0 max=242,242,242`, and its content is the self-test's stop frame
(the bbox/stop assertions in §4 are what cover it).

**Entity presence / non-background.** The same manifest with its `entity` line removed is rendered
with the same camera (`out/re/capture/s4/control/s4-noentity.png`, 122 meshes / 454,935 vertices —
the S3 arena). Against `s4-wide`:

```text
s4-region pixels=186945 span=235 changed=123830 changed_pct=66.239 outside=0 bbox=1123,237,1484,748
```

**66.239 %** of the entity's screen region differs from the no-entity control (need ≥ 5 %), the
region's largest channel span is **235** (need ≥ 30), and **0** pixels change outside the bbox
expanded by 16 px. The control proves the bbox really is Valus's screen region, not a background
patch. The differing-pixel box is `1123,237,1484,748`; the projection in `s4-status` is the
slightly larger `1123,237,1485,751`, so the guard's region contains every differing pixel.

**Idle pair (the wave-guard analog).** `--entity-time 0` vs `0.8`, same spawn-follow camera:

```text
s4-idle-pixels differing=132528 total=3686400 pct=3.595 outside=0 margin=16 bbox=1112,237,1484,748
```

**132,528 of 3,686,400 pixels differ (3.595 %)**, all inside the `s4-status` bbox expanded by
**16 px**; **0** pixels differ outside it, so the player and the world are unchanged. The x range
`[1112,1484]` reproduces the Task-4 measurement; the t=0.8 pose travels 11 px further left than
t=0 (`1112` vs `1123`), and the right/bottom edges move 1–3 px. The 16 px margin is what makes the
guard valid for both poses.

The idle pair uses the **spawn-follow** camera, not the portrait camera, on purpose: with
`--camera-target entity` the orbit point is the entity's chest sampled at the pinned time, so
`--entity-time 0.8` moves the camera target itself and the whole frame shifts (measured 98.8 %
differing, full-frame bbox in Task 6). The spawn-follow camera does not depend on the entity's
pose. The portrait remains a separate still; the acceptance never combines `--s4-selftest` with
`--camera-target entity`.

## 6 · Performance (the S3 pattern)

`--frames 1` vs `--frames 240`, same encounter manifest and capture path:

| run | wall time |
|---|---:|
| `--frames 1` (load-dominated) | **25.649 s** |
| `--frames 240` | **27.788 s** |
| marginal (239 frames) | **8.95 ms/frame ≈ 111.7 fps** |

The loop is vsync-paced; nothing measured approaches the 16.667 ms / 60 fps budget. Across the
recorded runs the marginal cost was 8.60–10.43 ms (≈96–116 fps); the variation is machine noise,
not scene cost. The load is the fixed 30-glb arena + 152 textures + the 38 MB Valus glb; it is a
per-run startup cost, not a frame-loop cost.

The perf gate was hardened in `b9d64cc` (review finding): `timed()` now returns the viewer's own
status, both runs are called under a failure check, and each must write a non-empty capture (the
240-frame run must also print its `s1-pixels`/`s1-screenshot` lines) before the `many > one` and
`< 16.667 ms` checks run. A failed 240-frame run can no longer pass as a load-time success. The
recorded 240-frame run printed `s1-pixels min=0,0,0 max=248,248,248 mean=102.3,96.8,88.9` and
`s1-screenshot ... width=2560 height=1440`.

## 7 · Suites and the S3 regression

- `dotnet test tests/BpConvert.Tests -v q`: **90 passed**, 0 failed (49 s).
- `ctest --test-dir app/build --output-on-failure`: **2/2 passed** (unit 34.85 s, windowed 0.16 s;
  total 35.02 s).
- `scripts/s3-acceptance.sh` (unchanged, run inside the S4 script): **exit 0, PASS**.

```text
s3-status instances=30 spawn=309.312,4.394,285.403 pos=309.312,29.802,305.405 ground=ok frames=240
s3-wave-pixels differing=5487 total=3686400 margin=0 bbox=1218,766,1342,964
s3-acceptance: PASS — build, ctest, 30-instance composed scene (ground=ok), spawn PNG (\x89PNG magic, >5000 B, non-uniform pixels), wave guard (<2 % delta, border untouched)
```

The S3 manifest, script, and acceptance path are untouched and remain valid. This re-run does
**not** close S3's own human walk (the arena's entrance/ramp feel), which is separate from S4's
bowl walk.

## 8 · Known limitations and fidelity gaps

- **Placement is a builder pick.** The pull was negative; the entity's transform is not the game's
  spawn transform. The manifest header, `s4-status placement=builder-pick`, and §2 say so. The
  user's walk judges whether the spot reads right.
- **No club or knee meshes.** The club (`_weapon`) and knee (`_hiza`) are separate unskinned glbs
  (613,576 B / 78,212 B converted); the club's hierarchy/attachment is undecoded, and neither is
  loaded or attached. Deferred to S4.2's attack work (design §3).
- **No shell fur.** The fur textures and shader strings exist in the game data, but there is no fur
  mesh and no converted fur PNGs emitted, so the body renders without the shell-fur layer.
- **The banner is opaque.** The banner mesh is weighted to 38 banner joints, but the 58-track body
  clips animate none of them; the glb writer emits no `alphaMode`/`doubleSided`, so it renders as a
  static opaque, single-sided sheet (no cloth simulation, no alpha).
- **No encounter/event camera.** Camera clip types 36/37 remain undecoded; `T` is an app-side
  camera-only framing aid (chest target, 22–30 m, pitch clamp ±80°, 10° framing pitch), not the
  game's `st17` camera, and there is no cutscene camera.
- **No AI, combat, or climb.** No patrol, attacks, weak points, damage, death, climbing, grab, or
  stamina. S4.0 is presence, placement, and approach; S4.1/S4.2 carry the rest.
- **No blending.** Player stand/walk/run switch by hard cuts at t = 0; the entity's idle loops
  independently. S2's foot-sliding limit remains (the controller speed is not matched to the clip).
- **Between-feet collision edge (accepted).** The resolver is capped at two iterations. At the
  ~2.7 m foot separation the player+foot exclusion zones (radius 2.05 m per foot) overlap, so a
  player placed exactly at the midpoint stays inside the union; it is **not reachable by walking**
  (the spawn is 34.810 m clear and approaches come from open ground). The limitation is named in
  `collision.cpp` and pinned by a test; the torso capsules (2.0 m) are coarse and unreachable from
  the floor.
- **Feet delta is 7 cm, not zero.** −0.070 m on a 17.7 m body, inside the 0.10 m gate; the idle
  clip's root series and the converter's feet pose do not land the lowest vertex exactly on the
  sampled ground. The walk is the visual judge.
- **The target camera has no occlusion test.** Orbiting `T` can put the camera through arena
  geometry; camera occlusion is out of scope.
- **No Valus reference capture.** The pose validation is decode-vs-bind plus internal geometry; the
  acceptance stills are a rendered view, not a silhouette match against the game.
- **Load ≈25 s.** The arena + Valus load is a fixed per-run cost, not a frame cost.

## 9 · The user's walk — PENDING

The scripted pass cannot judge feel, scale, cursor freedom, or the title-bar fps. Run the
interactive encounter and fill the result cells:

```bash
export SOTC_LIB=/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library
./app/build/sotc-viewer --library "$SOTC_LIB" --manifest out/re/stage/encounter.manifest \
    --model models/shadow/characters/wander/_cmn/wander.glb
```

Controls: `W`/`A`/`S`/`D` walk, `Shift` runs, left-drag turns the view, wheel zooms, `T` toggles
the entity framing, `F` recenters behind the player, `Space` pauses, `R` restarts, `Escape` quits.
The cursor is never captured.

| # | check | do this | expected | result |
|---|---|---|---|---|
| 1 | spawn and presence | launch; look ahead from the spawn | you stand in the bowl; Valus is visible ahead and **idling** (not a statue); the placement reads deliberate | ☐ |
| 2 | approach and collision | walk (then run) into his legs; try to push through; walk around | the collision stops you at his legs — no pass-through into the body; you can move around the feet | ☐ |
| 3 | `T` framing | press `T`; move a little; press `T` again | `T` frames the full 17.7 m body; `T` again returns to the follow view without a snap; the camera is not disorienting while moving | ☐ |
| 4 | feet and scale | stand at a foot and orbit; look at the ground contact | the feet sit on the bowl floor — no float, no sink; the body reads ~17.7 m (≈10× Wander), not a toy or a giant | ☐ |
| 5 | fps, cursor, quit | walk while reading the title; move the mouse; press `Escape` | the title's fps readout is in the ≥60 class (this Mac's vsync rate); the cursor stays visible and free; Escape quits cleanly | ☐ |

**Verdict: PENDING — the user's.** S4.0's done-line closes only when this table is filled and
passes; nothing in §1–§8 claims it.

## 10 · Evidence files (all gitignored)

- `out/s4-acceptance-run.log` — the recorded run's full output (137 lines; quoted above).
- `out/re/stage/encounter.manifest` — 4,360 B, sha256 `a94a4381…3a748a` (30 + 1 + 1).
- `out/re/capture/s4/` — `s4-wide.png`, `s4-approach.png`, `s4-portrait.png`, `s4-idle-t0.png`,
  `s4-idle-t08.png`; `control/s4-noentity.png` and `control/s4-perf.png` (+ the perf logs and the
  stdlib pixel-diff script).
- `out/re/stage/valus.manifest` — the S3 source, sha256 `088e6218…40c1d0`.
- `out/library/.../c01_minotaur_a.glb` — 38,090,040 B, sha256 `398b6814…943275`.

## 11 · Cross-references

- Design: `docs/superpowers/specs/2026-10-19-s4-encounter-design.md` (§2 goals, §3 non-goals,
  §7.2 two rigs, §7.3 placement/provenance, §7.4 acceptance).
- Plan: `docs/superpowers/plans/2026-10-19-s4-encounter.md`; recon: `docs/notes/s4-recon.md`.
- Pose validation: `docs/notes/s4-pose.md` (the bake, 58/58 mapping, residuals, frame-0 world
  heights).
- Placement pull (negative) and the builder pick: `docs/notes/re4-placement.md` §7–§8.
- Manifest schema: `docs/notes/scene-manifest.md` (`instance`/`entity`/`spawn`).
- S3 arena / S3 acceptance: `docs/notes/s3-results.md`; S2 clips/locomotion:
  `docs/notes/s2-results.md`.
- Script: `scripts/s4-acceptance.sh` (the recorded pass); the recorded run's log is
  `out/s4-acceptance-run.log` (§10).
