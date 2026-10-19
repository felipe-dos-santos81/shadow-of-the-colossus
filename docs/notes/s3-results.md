# S3 acceptance results (2026-10-18) — the arena

Spec milestone S3 (S3 design §1): **"the Valus arena composed from real game data, explorable —
*you* steer Wander: keyboard + mouse, game-style third-person camera (auto-follow + drag-to-look
nudges; the cursor is never captured), hand-tuned feel … ≥60 fps class; notes + screenshots."**

**App half (W1): PASS** — evidence below. **S3's done-line is not claimed yet**: the milestone
closes when the user's sign-off walk (§6) passes; the script cannot reach the feel guarantees.

| | |
|---|---|
| Date | 2026-10-18 (local −03; the recorded acceptance run is stamped `2026-10-19T02:21:56Z`–`02:24:07Z`) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64; ProMotion display (the loop is vsync-paced, ≈120 Hz) |
| Repo / branch | `sotc-native`, `s3-app`; W1 code `8d0ff08` + this task's carried fixes (`app/src/app.cpp` Q1) with `scripts/s3-acceptance.sh` and these notes |
| Library (gitignored) | `SOTC_LIB` = `/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library` (the main checkout's converted library; this worktree has none) |
| Manifest (gitignored) | `out/re/stage/valus.manifest` — W2's RE-3 deliverable, sha256 `088e621894ecd38312526bdac11916fb355a645ecef4d0bace91e53b2440c1d0`, 30 instances + spawn |
| Acceptance script | **`scripts/s3-acceptance.sh`** (build → ctest → composed self-test → spawn PNG → wave guard → summary) |
| Run log (gitignored) | `out/s3-acceptance-run.log` |

## 1 · The acceptance run

```bash
SOTC_LIB=/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library ./scripts/s3-acceptance.sh
#   exit 0
```

The script mirrors `scripts/s1-acceptance.sh`: it builds `app/`, runs the suite, runs the W1.4
self-test **against W2's real manifest**, requires `s3-status instances=<n> … ground=ok` with `<n>`
equal to the manifest's own instance count, takes a spawn screenshot carrying the s1 PNG
assertions (`\x89PNG` magic, > 5000 bytes, largest channel span ≥ 30 in the viewer's own
`s1-pixels` stats), runs the F3 wave guard (§4), and prints the summary (library, manifest +
sha256 + instance count, status line, capture sizes, wave delta). The manifest is consumed as-is;
when it is absent the pass fails with a one-line cause naming the W2 plan
(`docs/superpowers/plans/2026-10-18-re3-stage-composition.md`) — no fallback ships (S3 design §5:
a curated composition would be a separate, user-approved, labeled step). Each capture file is
deleted before its run, so a stale PNG can never mask a failed write.

**Suite** (`ctest --test-dir app/build --output-on-failure`):

```text
1/2 Test #1: unit_tests .......................   Passed   22.23 sec
2/2 Test #2: windowed_selftest ................   Passed    0.16 sec
100% tests passed out of 2
```

**The self-test on the real manifest** (verbatim; one line per concern):

```text
s1-scene meshes=122 vertices=454935 materials=116 textures=152
s2-anim clip=procedural-wave joints=348 procedural=1 t=0.000
s3-status instances=30 spawn=309.312,4.394,285.403 pos=309.312,29.802,305.405 ground=ok frames=240
```

*(Historical S3 capture: the procedural-wave stand-in. S2 now plays `player_a_stand` by
default in the arena — see `docs/notes/s2-results.md`.)*

The walk is exact: 240 × 1/60 s at run speed 5.0 m/s = 20.00 m of +Z travel from the spawn
(z 285.403 → 305.405, x unchanged), ending on the bowl floor at y = 29.802 — the heightmap's own
sample there — with `ground=ok`.

**The composed scene**: 30 instances → 122 meshes, 454,935 vertices, 116 materials, 152 textures;
bounds min `(−0.613, −4.472, −0.099)` max `(490.403, 110.930, 502.291)`; the ground heightmap is
493 × 504 cells at 1 m. Scene load measures ≈25 s (30 glb parses + 152 texture decodes — the fixed
cost of a `--frames 1` capture); a single-glb wander load is ≈2.7 s. Each acceptance viewer run
pays that load.

**Captures** (1280×720 points → 2560×1440 px @2×):

| file | bytes | what |
|---|---:|---|
| `out/s3/spawn.png` | 9,178,366 | the manifest's spawn, camera snapped behind the player, wave pinned t=0 |
| `out/s3/wave-t0.png` | 10,600,404 | wave guard capture, t=0 (same 240-frame walk as the self-test) |
| `out/s3/wave-t1.png` | 10,601,058 | wave guard capture, t=1.0 s |

## 2 · The walk profile (measured; read this before the sign-off walk)

The self-test walks straight +Z from the spawn. Sampling the heightmap along that exact path
(x = 309.312, 1 m cells) gives the walk's shape — recorded because the sign-off walk follows it:

| z (m) | y (m) | what the column's max surface is |
|---:|---:|---|
| 285.40 (spawn) | 4.393 | `arena_01_entrance_ground.glb` (the spawn pick's surface) |
| 286.4 … 289.9 | 4.67 → 5.55 | gentle ramp up the entrance floor |
| 290.4 → 291.4 | 5.92 → 8.55 | steep rise onto the entrance structures' tops |
| 292.9 … 294.9 | 11.55 → 11.58 | plateau (`arena_01_entrance_low1.glb` top) |
| 295.9 | 8.95 | dip |
| 296.9 | 9.48 | |
| **297.4 → 297.9** | **19.20 → 28.88** | the bowl floor's footprint begins (z ≈ 297.6): the column's max jumps from the entrance structures (≈11 m) to the bowl floor |
| 298.9 → 301.4 | 29.33 → 30.41 | bowl floor |
| 305.40 (end) | 29.80 | the self-test's end point |

A heightmap grid over the seam shows the same climb spread over ≈6–10 m of travel at x ≈ 300–306
(e.g. x = 300: 4.25 m at z = 293 → 15.76 at z = 298 → 28.76 at z = 303), so the ≈19 m step is
specific to the spawn's x sitting at the eastern edge of the opening — and to the heightmap's
per-cell **max-Y** rule (the highest surface in a column wins; the walkable passage floor is not
distinguished from the structures above it). Per spec §1 (ground-follow + edge clamp, **no
wall/prop collision**) this is the pinned model, not a defect; whether the entry reads game-like
is exactly what the sign-off walk judges (§6, item 5). A possible follow-on refinement —
restricting the heightmap to ground-class meshes, or a spawn pick aligned with the ramp — is
recorded here, not adopted.

## 3 · fps class

The claim is **≥60 fps class**. This session could not read the window title programmatically
(the W1.5 constraint forbids osascript/System Events; S1's AX title read is not repeatable here),
so the number below is the app's own render loop, measured from timed capture runs — the same
loop as the interactive path minus input polling (present mode is `SDL_GPU_PRESENTMODE_VSYNC`):

- `--frames 1`: **24.82 s** → the scene load dominates (30 glbs + 152 textures);
- `--frames 120`: 25.89 s; `--frames 1200`: 35.06 s → **1,080 marginal frames in 9.17 s =
  8.49 ms/frame ≈ 118 fps**. (Against the load-only run: 1,199 marginal frames in 10.24 s =
  8.54 ms/frame — the same number.)

So the loop is vsync-paced at the display rate (the 120 Hz interval is 8.33 ms); nothing measured
is near the 60 fps floor, and the single-glb scene measures the same marginal cost. The
interactive run itself was launched briefly against the real manifest — it composed the scene,
entered the loop and held for 35 s with no errors (startup lines only in
`out/s3-interactive.log`; no input injected). **The title-bar readout while walking is the user's
checklist item** (§6, item 6) — it closes the class claim interactively. For reference, S1 read
113–121 fps in the title bar on the same presentation path and window size.

## 4 · The wave guard (F3's shader half — W1.4 review Q2)

F3's fix (`c7d6486`) makes unskinned vertices pass through untransformed (the loader writes zero
weights; the shader's `weight_sum` branch). The loader half is unit-tested; the **shader half** was
guarded only by manual evidence, so the acceptance now pins an automated guard: the composed scene
is rendered twice with the wave pinned at `--anim-time 0` and `--anim-time 1.0` (everything else
identical: same manifest, same 240-frame walk, same spawn), and the two PNGs must differ **only
inside the player's screen region** — correct rendering moves the player, never the world.

Measured (the recorded run):

```text
s3-wave-pixels differing=13139 total=3686400 margin=0 bbox=1114,760,1445,1104
```

13,139 of 3,686,400 pixels differ (**0.356 %**), all inside the frame's central area (the player),
**none** in the outer 10 % border. The guard asserts: differing pixels > 0 (non-vacuous — the pose
must actually advance), < 2 % of the frame, and 0 in the outer 10 % border.

**Guard validation against the pre-fix behaviour** (reproduced this session, scratch build only):
a copy of the binary with `mesh.metal`'s `weight_sum == 0` branch changed to select `palette[0]`
for unskinned vertices (the F3 bug's shader effect) renders the same two captures **96.008 %
differing, full-frame bbox `x[0,2559] y[0,1439]`** — the arena warps with the player's animation,
which the guard fails loudly. The same scratch build's t=0 frame is **byte-identical** to the
fixed build's t=0 frame (0 differing pixels): at t=0 the wave's palette is identity, so the bug is
invisible there — which is why the guard must compare two *different* times.

## 5 · Adjudications carried into these notes

- **The 0.76 m origin-to-feet lift** (W1.4 review, adjudication 2 — accepted). The controller's
  `position.y = heightmap_sample` (feet on the ground) is untouched; only the *rendering* transform
  lifts the player's meshes, by `player_ground_offset` — the negated lowest vertex of the player's
  own meshes. Measured on the real manifest: **0.7596 m** (wander's local bounds y ∈
  `[−0.7596, 0.5033]`). It is also what makes the pinned camera target `playerPos + (0, 1.4, 0)`
  land at head height. Recorded as the app's player-grounding convention.
- **The `yaw + 180°` chase camera** (adjudication 1 — accepted). The follow camera's target yaw is
  `playerYawDeg + yawOffsetDeg + 180°`: the orbit `Camera` stores the eye-offset direction, so the
  literal reading would put the eye in front of the player and make W walk toward the camera. With
  +180° the eye trails and W walks away — the spawn capture (`out/s3/spawn.png`) shows Wander's
  back. Pinned by a camera test and visible in the capture.
- **30 instances = 35 glb files − 5 `*_collision*`** (W2 §3.1). The two arena level sets hold 35
  glb files; 5 are collision geometry; the workspace pool references exactly the 30 non-collision
  meshes (12/12 + 18/18) and no collision `cmsh` — W2's `check-manifest` pins the derivation, and
  this acceptance re-checks the count against the status line.
- **The spawn is a labeled builder pick** (W2 §3.3), not data-derived: `spawn 309.312 4.394
  285.403 0` is the entrance ground's footprint centre at its surface height, facing +Z into the
  arena (the `.path` and camdata candidates were unusable as measured). The manifest's header says
  so; this acceptance consumes it as-is.
- **F3's fix + its guard**: `c7d6486` (unskinned pass-through) and §4's automated wave guard.
- **The Q1 error-prefix fix** (W1.4 review, Q1): `write_screenshot`'s thrown messages carried a
  `sotc-viewer: ` prefix that the caller's catch added again (`sotc-viewer: sotc-viewer: cannot
  write …`). The prefix now lives only in the catch — the same one-line class also covered
  `camera_view_proj`'s throw. Verified: `sotc-viewer: cannot write /no/such/dir/x.png` (exit 1).

## 6 · Interactive checklist — the user's sign-off walk (OPEN)

Run the app against the real manifest and walk the arena:

```bash
export SOTC_LIB=/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library
./app/build/sotc-viewer --library "$SOTC_LIB" --manifest out/re/stage/valus.manifest \
    --model models/shadow/characters/wander/_cmn/wander.glb
```

| # | check | do this | expected | result |
|---|---|---|---|---|
| 1 | cursor free | move the mouse across the window; click once; try ⌘-Tab | cursor stays visible and free — the app never captures, hides, or recenters it; ⌘-Tab switches apps | ☐ |
| 2 | drag-look | left-drag in the window | the camera turns (yaw) and tilts (pitch clamped ±60°); while walking, the view settles back behind the player | ☐ |
| 3 | walk / run | W; W+Shift; A/D strafe; S back | camera-relative movement at 2.2 m/s walk / 5.0 m/s run; the character turns toward the movement direction (540°/s) | ☐ |
| 4 | edge clamp | run to the arena's outer edge / heightmap border | the player stops ≈0.5 m inside the bounds; no falling off; the camera stays sane | ☐ |
| 5 | ground-follow | walk the entrance ramp into the bowl (note the rim step, §2) | the feet track the surface — no sinking or floating; judge whether the entry reads game-like | ☐ |
| 6 | fps class | walk while reading the window title | the rolling fps readout (expect the display rate, ≈120 on this Mac; the claim is ≥60) | ☐ |
| 7 | playback keys | Space; R; `[` / `]` | pause/resume; restart; in arena mode the controller owns the clip (still/walk/run), so `[`/`]` are ignored — pin a clip with `--anim`/`--anim-time`. The default is now the real `player_a_stand` (S2); `procedural-wave` is only the no-clip fallback | ☐ |
| 8 | F / Escape | press F; press Escape | F recenters the camera behind the player; Escape quits cleanly | ☐ |

## 7 · Known limitations

> **Superseded by S2** (`docs/notes/s2-results.md`): real decoded clips and locomotion now play.

- **No wall/prop collision — by spec §1** (ground-follow + edge clamp only; collision is S4's
  RE-5 pull; clipping is accepted). Consequence to expect in the walk: the max-Y heightmap can
  lift the player onto a surface above the walkable floor — the bowl-rim step of §2 is the
  measured case.
- **Animation is S2's procedural wave** — the labeled stand-in; no locomotion clip selection
  (spec §1). S2's sign-off stays open until a real clip exists (design §4's stand-in rule).
- **Scene load ≈25 s** for the 30-instance arena (30 glbs + 152 textures); every acceptance run
  pays it four times. Not a frame-loop cost.
- The acceptance script reaches build/test/status/captures/pixels only; the cursor, drag feel,
  and fps readout are the human walk's (§6).

## 8 · Cross-references

- **W2 — RE-3 composition**: plan `docs/superpowers/plans/2026-10-18-re3-stage-composition.md`;
  notes `docs/notes/re-stage.md` (§3.1 the 30-instance set + the collision cross-check; §3.3 the
  spawn pick and its rejected candidates; §3.4 the manifest, its checks, and the pinned sha256).
  The manifest `out/re/stage/valus.manifest` is gitignored and regenerable:
  `python3 tools/re/stage/probe.py compose --library <out/library> --out out/re/stage/valus.manifest`
  then `check-manifest` (11/11 gating assertions). This acceptance consumes it as-is and fails
  loudly when it is absent.
- **W3 — capture + decode continuation**: plan
  `docs/superpowers/plans/2026-10-18-capture-decode.md`; notes `docs/notes/re-capture.md` (as W3
  lands them). Per the design §4, the port-rig captures serve as visual validation against this
  app's render — `out/s3/spawn.png` and the wave pair are that comparison's app-side artifacts.
- **The app workstream (W1)**: plan `docs/superpowers/plans/2026-10-18-s3-arena-app.md`; the
  manifest schema `docs/notes/scene-manifest.md`; S1's results `docs/notes/s1-results.md` (the
  acceptance-script pattern this script mirrors).

## 9 · Evidence files (all gitignored)

- `out/s3/spawn.png`, `out/s3/wave-t0.png`, `out/s3/wave-t1.png` — the acceptance captures.
- `out/s3-acceptance-run.log` — the recorded script run's full output (quoted above).
- The wave-guard pre-fix validation used a scratch binary + patched `mesh.metal` under
  `scratch/w15/` (untracked; the method and numbers are in §4).
