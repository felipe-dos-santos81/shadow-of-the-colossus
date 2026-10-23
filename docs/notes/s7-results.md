# S7 acceptance results (2026-10-22) — return to the shrine

Spec milestone S7 (`docs/superpowers/specs/2026-10-22-s7-return-to-the-shrine-design.md`): after
Valus's death the game does what the original does next — the screen fades out, the shrine of
worship loads, Wander wakes in front of the altar among the sixteen idols and takes control; `R`
goes back to the arena. The S6 end state (the corpse, the control) is no longer the end of the
interactive game.

**Status: the app/converter acceptance is PASS (script) — the user's walk is PENDING.**
`scripts/s7-acceptance.sh` runs green end to end (exit 0): both builds, both suites, the **frozen
S6 acceptance first** (which runs S5 → S4.2 → S4.1 → S4 → S3), the arena compose against its
pinned hash, the shrine compose byte-identical to the manifest, the four probe checks, the
deterministic `s7-*` timeline (25 lines) parsed field by field, the JSON cross-check, and five
2560×1440 stills. Whether the shrine reads as the game's, whether the wake feels right and whether
the walls and the floor feel solid are the user's eye and hands (§11).

| | |
|---|---|
| Date | 2026-10-22 local (−03); the recorded run is stamped `2026-10-23T00:06:32Z`–`00:18:57Z` (12 min 25 s) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64; 1280×720 points → 2560×1440 px captures |
| Repo / branch | `sotc-native`, branch `s7-shrine`; the code base is `ee1fe08` (the return, wired and proven) |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Library (gitignored) | `SOTC_LIB=<repo>/out/library` (shared; Wander re-baked with 17 clips, Mono baked with 1) |
| Arena compose (scratch) | `out/s7/arena-compose.manifest`, sha256 `088e621894ecd38312526bdac11916fb355a645ecef4d0bace91e53b2440c1d0` (the pinned S3 value; `valus.manifest` and `encounter.manifest` untouched) |
| Shrine manifest (gitignored) | `out/re/stage/shrine.manifest`, sha256 `6bacc017ab2349786b5fcb126dd2a931ccc863ec7fea0f5b70f4e6d3d9bc97d8`; 52 instance lines (34 shrine-set glbs + 17 statues + 1 posed Mono), 3,551 meshes, 2,229,907 vertices; the scratch re-compose is byte-identical |
| Acceptance script | **`scripts/s7-acceptance.sh`** (bash 3.2) |
| Run log (gitignored) | `out/s7-acceptance-run.log` — ends `s7-acceptance: PASS —` |
| S6 regression log (gitignored) | `out/s7-s6-regression.log` — ends `s6-acceptance: PASS —` |
| Captures (gitignored) | `out/s7/stills/`, `out/s7/s7-selftest.txt` |

## 1 · The acceptance run

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export SOTC_LIB=<repo>/out/library
caffeinate -dimsu ./scripts/s7-acceptance.sh   # exit 0; everything below is from
                                                # out/s7-acceptance-run.log
```

- **Builds.** `dotnet build BpConvert.sln`, `dotnet publish src/BpConvert.Cli`, app `cmake` Release.
- **Suites.** `dotnet test` **146 passed, 0 failed** (49 s); `ctest` **2/2** (unit 11.64 s,
  windowed 0.19 s).
- **The frozen S6 acceptance** ran unchanged and printed its own `PASS —` (the unchanged S4.2 render
  path at **8.52 ms/frame**, marginal, ~117.4 fps); it took 9 min 53 s with its nested chain. Its
  own s6 outputs are not re-asserted.
- **Manifests.** The arena compose into the scratch path hashes to the pinned
  `088e6218…c1d0`; the shrine compose is byte-identical to `out/re/stage/shrine.manifest`;
  `check-cwks` 13/13, `check-manifest` (arena) pass, `check-encounter` 22/22,
  `check-manifest --stage shrine` 12/12 (`gate: MET` on each).
- **The timeline.** `--s7-selftest --s7-stills out/s7/stills` exited 0 in 80 s with exactly **25**
  `s7-*` lines (10 phase, 4 load, 2 reset, 4 ground, wall, idols, mono, perf, status), §2–§6.
- **The JSON cross-check** (ruling R1): the printed wall segment equals the JSON's probe; the
  `app/src/shrine_points.h` constants equal the JSON's four probes, the three flow timings and the
  fade colour.
- **The stills.** Five 2560×1440 PNGs, §7.

The individual steps, for reproduction (the script is the source of truth):

```bash
dotnet build BpConvert.sln -v q
dotnet publish src/BpConvert.Cli -c Release -o out/bin
cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release && cmake --build app/build -j
dotnet test tests/BpConvert.Tests -v q
ctest --test-dir app/build --output-on-failure
caffeinate -dimsu ./scripts/s6-acceptance.sh        # the frozen S6 + S5 + S4.2 + S4.1 + S4 + S3
python3 tools/re/stage/probe.py compose --library "$SOTC_LIB" --out out/s7/arena-compose.manifest
python3 tools/re/stage/probe.py compose --library "$SOTC_LIB" --stage shrine --out out/s7/shrine-compose.manifest
python3 tools/re/stage/probe.py check-cwks
python3 tools/re/stage/probe.py check-manifest out/s7/arena-compose.manifest --library "$SOTC_LIB"
python3 tools/re/stage/probe.py check-encounter out/re/stage/encounter.manifest --library "$SOTC_LIB"
python3 tools/re/stage/probe.py check-manifest out/re/stage/shrine.manifest --library "$SOTC_LIB" --stage shrine
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
  --manifest out/re/stage/encounter.manifest --shrine-manifest out/re/stage/shrine.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --s7-selftest --s7-stills out/s7/stills
```

Every viewer and acceptance run is wrapped in `caffeinate -dimsu`; `compose-encounter` is never
run (the encounter manifest is frozen).

## 2 · The RE gates (`docs/notes/s7-recon.md`)

| gate | outcome | what S7 adopts |
|---|---|---|
| **R1** idol placement | **PASS** — from data (`cathedral_int.cgpr`), not code | 16 positions + yaws + model refs; slot 1 = `mino_a_statue_broke` + `_skirt` (`source: data`) |
| **R2** post-defeat flow | **FALLBACK** — no timings in the binary | hold 2.0 / fade-out 1.0 / fade-in 1.5 s, white — **authored** |
| **R3** wake / spawn / probes | **PASS** (wake clips, spawn position) + authored (spawn yaw, Mono, probes) | wake = `player_a_home_boss01_cut04` + `cut05`; spawn = the `cathedral_int.sgpr` start, (0, 8.998, 1) |

## 3 · The return timeline

The `s7-phase` lines, in the order printed (the selftest seeds Valus dead, steps the return, injects
one `R` during the first `FadeOut`, re-seeds, then completes):

```text
s7-phase phase=Idle t=0.000 fade=0.000 lock=0
s7-phase phase=Hold t=0.000 fade=0.000 lock=0
s7-phase phase=FadeOut t=0.017 fade=0.017 lock=1
  (R at fade 0.6 -> s7-reset, the fade.png still)
s7-phase phase=Idle t=0.000 fade=0.000 lock=0
s7-phase phase=Hold t=0.000 fade=0.000 lock=0
s7-phase phase=FadeOut t=0.017 fade=0.017 lock=1
s7-phase phase=Load t=0.000 fade=1.000 lock=1
s7-phase phase=FadeIn t=0.000 fade=1.000 lock=1
s7-phase phase=Wake t=0.017 fade=0.000 lock=1
s7-phase phase=Free t=0.000 fade=0.000 lock=0
```

The script asserts the sequence `Idle Hold FadeOut Idle Hold FadeOut Load FadeIn Wake Free` exactly,
fade 1.000 and the input lock at `Load`, fade 0.000 and the lock at `Wake`, and the lock released
at `Free`.

**The wake is 11.669 s** (ruling R5 amended): the sum of the two baked clips' sampler maxima as the
app loads them — `player_a_home_boss01_cut04` 8.168 s + `cut05` 3.501 s (124 f @ 15.059 fps and
54 f @ 15.139 fps; ≈11.80 s by frame count). `docs/notes/s7-recon.md` §3.1's 15.456 s was computed at
the `.anb`'s 10.26 fps and is corrected there. The clips carry no root series, so the app plants the
pose's lowest body joint on the floor: the selftest measured the lift against the recon's pelvis
heights (lying 0.10 m, kneeling 0.55 m at the cut04 → cut05 boundary, standing 0.72 m) within 0.05 m.

## 4 · The stage loads (four, equal shrine scenes)

| # | line | meshes | ms |
|---|---|---|---|
| 1 | `s7-load stage=arena instances=30` | 142 | 11,712.8 |
| 2 | `s7-load stage=shrine instances=51` | 3,563 | 27,196.5 |
| 3 | `s7-load stage=arena instances=30` | 142 | 9,126.7 |
| 4 | `s7-load stage=shrine instances=51` | 3,563 | 26,604.6 |

Ruling R10: four lines, not three — the selftest loads the arena, the shrine, the arena again after
the shrine's `R`, and the shrine again. The two shrine mesh counts are equal (3,563 scene meshes; the
manifest's glbs hold 3,551 glTF meshes), and so are the arena's (142). Ruling R11: the shrine
load ceiling is **60 s** (`ms ≤ 60000`); the 24–26.5 s measured earlier and the 26.6–27.2 s here
leave no margin under the design's default 30 s — about 21 s of it is `compose_scene` over 3,563
meshes / 474 textures. **Load-time work (texture/glb caching) is the next step.** The window does not
pump events during the load (§10).

## 5 · The ground, the wall, the idols, Mono

Each `s7-ground` probe is the floor a body walking there from the spawn stands on (the feet follow
`heightmap_floor` in 0.25 m steps along the straight line, as `player_update` does), against the
column's maximum (the roof, the garden above). The script asserts floor < column max − 1.0 at the
three named points and 8.5 ≤ floor ≤ 12.5 at the spawn (ruling R6: the recon's 9–11 band was an
approximation; the hall floor measures 8.999).

| probe | xz (authored point) | floor | column max |
|---|---|---|---|
| `spawn` | (0, 1) — the `sgpr` start | 8.998 | 60.391 |
| `altar_step` | (0, 20) | 10.316 | 83.170 |
| `hall` | (0, −20) | 8.999 | 109.979 |
| `stair` | (0, −68) | 9.856 | 158.834 |

- **The wall.** `s7-wall from=0.00,-13.00 to=14.00,-13.00 stopped=8.54,-13.00 blocked=1`: a forward
  walk through `player_step` from (0, −13) toward (14, −13) is stopped at x = 8.54 — the steep face
  is at x ≈ 9.33–9.39, so the coarse 1 m mask stops the body ≈0.8 m short (a known gap, §10). The
  printed segment equals the JSON's `probes.wall`.
- **The idols.** `s7-idols count=16 broken_slot=1`: sixteen statues (the broken slot-1 skirt not
  counted), slot 1 the `_broke` glb.
- **Mono.** `s7-mono clip=girl_a_death_loop present=1`: the posed `mono.glb` is in the composed
  scene at its manifest transform.
- **Layers.** `kHeightmapLayers` is 64 (ruling R12): a shrine stair column holds up to 60 surfaces;
  with 8 the stair probe read the roof. Unlayered (arena) maps are unaffected.

## 6 · Performance and status

- `s7-perf frames=240 frame_ms=8.348 drawn=170 culled=3393` — the 240-frame play view at the spawn
  in the shrine: **8.348 ms/frame** (≤ 16.7), **170 meshes drawn, 3,393 culled** of 3,563.
- `s7-status stage=shrine phase=Free lock=0`.
- `s7-reset stage=arena phase=Idle death=Idle valus=100 player=100` twice (`R` mid-fade, `R` from the
  shrine), both exact.
- `s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 frames=3` — windowed, the cursor never
  captured.
- The frozen S4.2 render path in the S6 regression: 8.52 ms/frame marginal.

## 7 · The stills

| still | bytes | channel span | what |
|---|---|---|---|
| `fade.png` | 8,625,340 | 102 | the arena at **fade 0.6** (authored) with the corpse; mean luma 192.0 (≥ 180) |
| `wake.png` | 10,910,945 | 255 | the wake at 85 % of its length (the rise, the altar ahead) |
| `altar.png` | 10,455,881 | 249 | Mono on the altar, orbit 7 m, yaw 180°, pitch 20° (authored framing) |
| `idols.png` | 11,149,828 | 246 | the hall with the idols, the broken first one among them (authored framing) |
| `hall.png` | 12,181,543 | 255 | a wide of the hall (authored framing) |

All five are 2560×1440, carry the PNG magic and are > 5000 B; the span is the viewer's own
non-uniformity line (≥ 30 required). `fade.png`'s mean luma is computed from the same `s1-pixels`
line (0.2126 R + 0.7152 G + 0.0722 B), 192.0 against the 180 floor (ruling R13: captured at 0.6,
not 0.5).

## 8 · The suites

`dotnet test tests/BpConvert.Tests`: **146 passed, 0 failed, 0 skipped** (49 s).
`ctest --test-dir app/build`: **2/2 passed** (`unit_tests` 11.64 s, `windowed_selftest` 0.19 s;
11.84 s total). The unit suite carries the S7 cases added by Tasks 2–9 (heightmap floor/wall mask, frustum
culling, the transition machine, `posed` manifest records, `camera_wake_pose`).

## 9 · Authored values and rulings

**Authored (labelled `source: authored` in the data and here):**

- the flow timings — hold 2.0 s, fade-out 1.0 s, fade-in 1.5 s — and the fade colour (white) — R2
  found none in the binary (`shrine_points.h`, transcribed from `shrine_placements.json`, ruling R1);
- Mono's placement — `mono.p = (0, 11.402, 23)` on the altar top (11.302) + 0.10 m, yaw 0, pose time
  0 s of `girl_a_death_loop`;
- the spawn yaw — 0 (facing +Z, the altar); the spawn position is data (8.998);
- the probe points — `altar_step` (0, 20), `hall` (0, −20), `stair` (0, −68), the wall segment
  (0, −13) → (14, −13);
- the wake camera's two keys — front 2.5 m at eye height 1.2 m looking at the lying body (0.3 m),
  then 3.5 m behind at 1.8 m looking at the head height, eased (`camera_wake_pose`);
- the fade still at fade 0.6, the wake still at 85 % of the wake, and the three orbit framings.

**Data, not authored:** the 16 idol positions and yaws and the broken slot-1 pair (`cathedral_int.cgpr`),
the wake clips and their pelvis heights, the spawn position (`cathedral_int.sgpr` start).

**Rulings that changed the plan:**

- **R5 (amended)** — the Wake is the baked clips' duration, 11.669 s, not Task 0's 15.456 s.
- **R6** — the spawn band is [8.5, 12.5], not 9–11 (the data spawn is 8.998).
- **R10** — four `s7-load` lines, not three.
- **R11** — the shrine load ceiling 60 s, not 30 s.
- **R12** — `kHeightmapLayers` 64, not 8.
- **R13** — `fade.png` at fade 0.6 and the probes follow the floor walked from the spawn.
- **R1** — the app parses no JSON: the timings and probes are `constexpr` in `shrine_points.h`, and
  this script cross-checks them against `shrine_placements.json`.

## 10 · Known limitations

- **Dormin's voice and light** are not reproduced; the hall's `DorminLight1` is not lit.
- **The statue break animation** is not played: slot 1 simply stands broken.
- **The cutscene cameras** are not decoded; the wake camera is the authored two-key path.
- **The bridge and the garden walk** are not part of S7; the garden above the shrine is not a
  destination.
- **The shrine is silent** — no ambience, no footsteps, no score. A recorded gap, not a placeholder.
- **The wall mask is coarse (1 m).** The wall probe stops the walk at x = 8.54 against a face at
  x ≈ 9.33, ≈0.8 m short; thin geometry between nodes can be missed. The band and steepness are
  builder knobs.
- **The shrine load is ≈25 s** (26.6–27.2 s here), the window does not pump events during it and may
  beachball. Texture/glb caching is the next step.
- **A possible few-centimetre pop at Wake → Free**: the wake plants the pose's lowest joint on the
  floor, then the standing clip takes over; the walk (§11, item 3) judges it.
- The ground probe follows the floor but ignores walls, so its label overstates the walk's physics;
  the wall probe is the wall evidence.
- The interactive path (the key press, the real defeat, the gamepad) is not driven by an automated
  run; the selftest drives the same helpers (`step_return`, `press_reset`, `release_scene`) the
  interactive loop calls.
- Interactive play without the wake clips in `wander.glb` exits with an error; `--no-return` avoids
  the return.

## 11 · The user's walk — PENDING

The scripted pass cannot judge the shrine's look, the wake's feel or whether the walls and the
floor feel solid. Run from the repo root and fill the cells (to skip the fight, start in the shrine
with `--stage shrine`, which needs `--manifest`):

```bash
export SOTC_LIB=<repo>/out/library
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
    --manifest out/re/stage/encounter.manifest \
    --model models/shadow/characters/wander/_cmn/wander.glb
# or: add  --stage shrine  to start in the shrine
```

| # | check | do this | expected | result |
|---|---|---|---|---|
| 1 | the hold and the fade | defeat Valus (or watch the S6 sequence) and wait | after the death sequence the corpse holds (control kept), then the screen fades to white and the shrine loads (≈25 s, the window may stall) | ☐ |
| 2 | the fade-in | watch the load end | the shrine fades in from white | ☐ |
| 3 | the wake | watch Wander rise | Wander wakes in front of the altar (lying → kneeling → standing), the camera starts low on him and ends behind him facing the altar; no float or sink, and no visible pop when control returns | ☐ |
| 4 | Mono on the altar | look at the altar | Mono lies on the altar | ☐ |
| 5 | the idols | look along the hall | sixteen idols; the first is broken | ☐ |
| 6 | the shrine reads as the game's | walk the hall | it reads as the game's shrine of worship | ☐ |
| 7 | ground: steps and stairs | walk to the altar steps and up/down the stairs | the floor under the feet at every point: no falling through, no standing on the roof | ☐ |
| 8 | walls, pillars, altar, idols | walk into each | Wander stops or slides; never passes through, never drops to the exterior ground | ☐ |
| 9 | `R` returns | press `R` in the shrine (and mid-fade in the arena) | the arena in its opening state (Valus alive, the corpse gone, the player whole) | ☐ |
| 10 | the return repeats | defeat Valus again after `R` | the whole return plays again | ☐ |
| 11 | no clipping, windowed, cursor | move the mouse over the window; watch the title | a 1280×720 window, the cursor free, the fps in the ≥ 60 class | ☐ |

`--no-return` keeps the S6 end state for the S6 walk (the corpse and the control, no return to the
shrine); run the S6 walk (`docs/notes/s6-results.md` §11) with it.

**Deferred — do not mark as failures:** Dormin's voice and light, the statue break animation, the
cutscene cameras, the bridge, the garden walk, the shrine's sound, the coarse (1 m) wall mask, the
~25 s load.

**Verdict: PENDING — the user's eye and hands.** S7's done-line closes only when this table is
filled and passes; nothing in §1–§10 claims it.

## 12 · Evidence files (all gitignored)

- `out/s7-acceptance-run.log` — the recorded run's full output (ends with the PASS line).
- `out/s7-s6-regression.log` — the frozen S6 acceptance's full output, ending `PASS —`.
- `out/s7/s7-selftest.txt` — the captured `--s7-selftest` output (the 25 `s7-*` lines).
- `out/s7/s7-loads.txt` — the four load lines.
- `out/s7/arena-compose.manifest`, `out/s7/shrine-compose.manifest` — the scratch re-composes.
- `out/s7/stills/` — `fade.png`, `wake.png`, `altar.png`, `idols.png`, `hall.png`.
- `out/re/stage/shrine.manifest` — the shrine scene.

## 13 · Cross-references

- Design: `docs/superpowers/specs/2026-10-22-s7-return-to-the-shrine-design.md` (§2 goals, §8 the
  design, §9 the builder's calls, §11 the walk).
- Plan: `docs/superpowers/plans/2026-10-22-s7-return-to-the-shrine.md`; rulings R1–R13 in
  `.superpowers/sdd/2026-10-22-s7-return-to-the-shrine/progress.md`.
- RE gates: `docs/notes/s7-recon.md`; data: `tools/re/s7/shrine_placements.json`.
