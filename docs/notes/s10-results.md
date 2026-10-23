# S10 results — "the fast loop"

S10 is tooling and a refactor: no feature, no frozen output moved. The goals (design §2) and where each is
proved:

| goal | result | evidence |
|---|---|---|
| G1 the regression runner (`scripts/verify`) | blessed; PASS cold 285.7 s, warm 1.4 s; agrees with `--long` | §Task 1 |
| G2 the library view (`make libview`) | clone 4.3 s, ~7 MB; a re-bake left all 19,295 base files unchanged | §Task 3 |
| G3 the colossus census (`make census`) | 16 rows, ~2 s; 5,505 shipped hashes checked | §Task 2 |
| G4 the `app.cpp` split | 9,275 → 151 lines over 24 files; 469/469 units per commit; verify PASS per step; `--long` PASS | §Task 6 |
| D4 GPU width | stays 1 (concurrent marginals up to +2.05 ms over the 8.22 ms solo mean) | §Task 4 |
| D7 step 1, `run_flags` | 3,145,728 exhaustive cases match the five old truth tables | §Task 5 |

The user's walk is not part of S10 (nothing in the game changed).

## Task 1 — the runner (`scripts/verify`, `tools/verify/`)

What exists: `scripts/verify` (bash 3.2 wrapper, runs under `caffeinate -dimsu`), `tools/verify/run.py`
(stamps, sections, goldens, `--only sN`, `--quick`, `--dry-run`, `--long`, `--bless`), `masks.py`
(11 timing fields masked), `stamps.py` (content hashes), one adapter `tools/verify/s3.sh` … `s9.sh`
reproducing each frozen script's viewer runs and bash thresholds, and 83 goldens + `PIN` in
`tools/verify/golden/` (blessed at `PIN` `9008475`; `make verify` / `make verify-long` in the Makefile).
The runner refuses to run while a frozen `scripts/*-acceptance.sh` that existed at `PIN` differs from it.

Gate coverage (adapter dry-run; the frozen scripts are unmodified):

| scope | measured |
|---|---|
| s3–s8 adapters | 0 of 711 frozen gate conditions missing (650 verbatim, 61 excluded with reasons) |
| s9 adapter | 140/140 s9 gates present, 5 excluded |
| all nine milestones (`--dry-run`) | 34 viewer runs, 712 gates |

Bless: `scripts/verify --bless` ran every section twice (18 sections, 34 viewer runs each pass), 542.5 s
for both; the normalised outputs were byte-identical, so no `UNSTABLE` line and no mask beyond the
timing table was added. 27 `<masked>` values, all timings; no absolute path survives normalisation.

Loop time per change class (primary checkout, `caffeinate -dimsu`, nothing else running):

| change class | wall | what re-ran | status |
|---|---|---|---|
| nothing changed (warm) | **1.4 s** | nothing (mostly the incremental cmake check) | measured |
| a frozen s4 run + `make audio` (36 WAVs rewritten, manifest `# source:` line moved) | **1.6 s** | nothing: manifests hash with `# source:` normalised, WAVs by sha256 (0.026 s for 36 files) | measured |
| one milestone, comment-only edit in `app/src/sword.cpp`, `--only s9` | **94.5 s** | app rebuild + s9.gpu (93 s, e2e included); probes, audio, decode not re-run; no ctest (`--only` runs only named steps) | measured |
| any app change that reaches every gpu section | **255.7 s** (also 256.5 s) | all nine gpu sections (s3 7–8, s4 17–18, s41 20, s42 9, s5 5, s6 16, s7 27, s8 45, s9 93 s) with ctest 9–10 s concurrent; a section-sum estimate is ~240 s | measured (255.7 s and 256.5 s were runs where every section re-ran) |
| cold (stamps deleted) | **285.7 s** | everything: dotnet build/publish/test 58–59 s and ctest 10 s concurrent, decode 1 s, probes ≤ 2 s each, audio sections 2–3 s each, gpu sections as above | measured (261 s in Task 4's cold run) |
| converter-only change (`src/`, `tests/`) | dotnet step 58–59 s; the gpu sections re-run only if their own stamps moved | | the 58–59 s is measured inside the cold run; this class was not timed on its own |
| `--long` (the milestone gate, highest frozen script, s9 → … → s3) | **782 s** (784 s on the split tree) | the whole frozen chain | measured |

Against the frozen chain on the same tree: cold runner 285.7 s vs `--long` 782 s (2.7×); an unchanged tree
1.4 s. The design's estimate for an app change with e2e was 2–4 min (§13.1); measured 255.7 s if every gpu
section re-runs, 94.5 s if one milestone's does.

Agreement: on the blessed tree the runner said PASS (all nine milestones) and `--long` said PASS
(`verify: long s9 PASS (782 s …)`), with matching values (s9 lock `k 0.0000, hit at 38.0 m`, e2e gallop
455.4 m, road perf `drawn 84, culled 3549`). A full `scripts/verify` after the `--long` passed again
(256.5 s), so the runner holds after the frozen chain rewrites the shared library.
Not done: the scratch-branch mutation check of bless step 5 (a changed selftest print must make verify
FAIL); `test_run.py` covers a changed line against fake sections only.

Known limits (deferred, none affects a PASS verdict):
- The stamps hash every `out/re/stage/*.manifest`, so changing one (e.g. `arena_02.manifest`) re-runs every
  section that reads any.
- `--quick` skips the four s9 refusal runs labelled `[e2e]` from golden comparison; their bash gates still
  run.
- A git worktree needs `git submodule update --init --recursive` (nested submodules) before dotnet or
  verify.
- A mask is added only with a comment naming the evidence; none was needed on this machine on this day, and
  a re-bless on other hardware could surface one.

## Task 2 — the census (`tools/re/census/census.py`, `docs/notes/colossi-census.md`)

`make census` (about 2 s, read-only on the game tree) writes `tools/re/census/census.json`; the note is a
hand-written snapshot of that output (regenerating the json does not regenerate the note). Measured:
16 colossi, all convert `ok`, skeleton coverage 1.0 for each clip stem, every one has a dead body (#1 a
dirtmound, #4 a mound, #10 a stone), 0 clips outside the S2 decoder's track set, `minotaur_a/b/c` share a
238-joint skeleton, `leo_a`/`cerberus_a` are interchangeable. Hash check: 5,505 files checked,
4 mismatched, 0 missing; the four are the known Valus clobber (`minotaur_a_death_front`, `_loop`,
`objanim_minotaur_a_death_front`, `camera_minoa_death_front_camera_01`); a Valus re-bake from the game
tree would fail on them, restoring them from `uroot/pak3.psarc` is the user's call.

Name-derived (labelled in the note, not verified behaviour): the ranking by distance from Valus — #6, #15
(same rig) first; #2, #3, #4, #11, #14 (ground bodies on a new 34–42-joint skeleton) next; #8, #9, #12, #16
(a second system) third; #5, #7, #10, #13 (flight, swimming, serpentine) last — the mechanics hints, and
the audio-bank map by cue stem. Recorded, not corrected: Valus's S5 cues come from `boss01`, whose cue
stems are `mammo_`/`mavo_` (Quadratus); `minotaur_`/`mivo_` sit in `boss15` and `boss22`; names cannot
decide it and `s5_map.json` stays frozen (design §13.14).

## Task 3 — the library view (`make libview BRANCH=<name>`, `tools/verify/libview.sh`)

Measured on `s10-libview`: clone 4.3 s wall (models 4 s, textures 1 s, audio/catalog/coredata 0 s), 19,295
files, 0 symlinks, ~7 MB of free space used. At the view: `ctest` 2/2; the viewer `--selftest` exit 0 with
`s1-scene meshes=135 vertices=941302 materials=129 textures=86`. A re-bake of Wander's 43 clips into the
view (`convert --out <view> --force`, 13 s) wrote 44 files (~92 MB diverged blocks); path + size + mtime of
all 19,295 base files, and the sha256 of `wander.glb` and its 21 texture PNGs, were identical before and
after. The view's `wander.glb` sha256 equals the base's, so the bake is reproducible.

Deviation from the design (D5): textures are cloned, not symlinked (§13.6: the converter rewrites every
referenced texture PNG on a bake and would write through a symlink). Guards added in review: the view is
built under a temp name and moved into place only when complete; the script refuses a non-APFS volume
(clones cannot cross volumes). Deferred: two branch names that sanitise equally collide; dotfiles are not
cloned; a concurrent finish can nest the temp dir. S9 used the view `out/libviews/s9-road` and its bake
was promoted to `out/library` at S9's merge (the view was deleted); `out/libviews/s11-quadratus` is the
only view now.

## Task 4 — concurrent viewers (GPU width)

Protocol (design §13.7): the frozen S4.2 perf pair (`--frames 1` then `--frames 240`, `--anim-time 0
--entity-time 0`, encounter manifest, Wander; marginal = (t240 - t1) * 1000 / 239 ms) run alone vs three
pairs at once (the 1-frame runs together, then the 240-frame runs together), three trials each, every run
under `caffeinate -dimsu`, outputs in a scratch dir. Background: `mediaanalysisd`/`photolibraryd` were
at ~100 % CPU (macOS, nice 0) during the solo trials and trial 1 of the concurrent set; a nice-19
`sotc-app-tests` from another worktree overlapped concurrent trial 3, so that trial was discarded and
re-measured clean (trial 4).

| trial | solo marginal (ms) | concurrent marginals, pairs 1/2/3 (ms) |
|---|---|---|
| 1 | 7.74 | 10.27 / 8.21 / 8.41 |
| 2 | 8.44 | 8.54 / 8.63 / 8.73 |
| 3 (4 for conc.) | 8.47 | 8.23 / 8.28 / 8.23 |

Solo baseline (mean of 3) 8.22 ms (a warm-up run and a fourth solo trial read 8.26 and 8.50). Load time:
solo 1.27–1.44 s, concurrent 1.64–2.11 s.
Discarded confounded trial 3: 8.48 / 8.59 / 8.36 (a nice-19 `sotc-app-tests` was running).

Decision: **width 1.** Concurrent marginals reach +2.05 ms (10.27) and +0.51 ms (8.73) over the solo
mean, and the solo trials themselves span 0.73 ms, so the ±0.2 ms rule cannot be met; the load phase is
also ~55 % slower under contention. The runner keeps `--jobs` default 1 (perf-gated runs always alone).

`scripts/verify` after the change: all nine milestones PASS, 261 s wall (cold stamps; width 1 is the
unchanged default, so no golden moved and the time is the same as before).

## Task 5 — `run_flags` (D7 step 1)

`app/src/run_flags.{h,cpp}` (33 / 53 lines): `RunFlags run_flags(opts, stage, followMode, returnAvailable)`
for the fields known before the scene loads (10: the seven of §13.8 plus `s8Wired`, `s9Wired`, `road`) and
`SceneFlags scene_flags(opts, stage, followMode, hasEntity, entityRigValid)` for `audioWanted` and
`valusBehavior`, which need post-load inputs (ruling A9 split the single signature in two). The exhaustive
test (`app/tests/test_run_flags.cpp`) compares them with the old predicates over 3,145,728 cases, red
against a stub then green; 469 unit cases in the suite. Landed after `scripts/verify` re-ran all nine gpu
sections against the goldens (PASS).

## Task 6 — the app.cpp split

Behaviour-preserving, steps a–d (15 `refactor(app):` commits on `s10-split`); every step's
`make verify` re-ran all nine milestones' GPU sections and matched the goldens (PASS), and step e's
`make verify-long` is recorded below.

Before: `app/src/app.cpp` **9,275** lines (`run_app` alone ~6,940). After (24 files, 10,654 lines; the
growth is declarations, includes and the per-function reference preambles that keep the moved bodies
textually unchanged):

| file | responsibility | lines |
|---|---|---|
| `app.cpp` | `run_app`: option checks, the SDL shell, the stage loop | 151 |
| `stage_ctx.h` | `Shell`, `RunState` (per run), `PlainRide`, `FrameState`, `StageCtx` (per stage), shared constants | 331 |
| `stage_ctx.cpp` | `StageCtx` methods: `hud_state`, `soft_reset_encounter`, `press_reset`, the wake lambdas, `plain_step`, `sword_frame`, `door_frame`, … | 579 |
| `stage_setup.cpp` | `stage_setup`: the stage's composition (`std::optional<int>` exit code) | 661 |
| `selftests.h` | `SelftestResult`, the selftest declarations, `run_selftests` (the original order) | 108 |
| `selftest_s1.cpp` | the plain `--selftest`, the `--screenshot` capture | 103 |
| `selftest_s3.cpp` | the S3 selftest | 102 |
| `selftest_s4.cpp` | the S4 selftest (`s4_pin_entity` + `run_s4`) | 379 |
| `selftest_s6.cpp` | the S6 selftest | 503 |
| `selftest_s7.cpp` | the S7 selftest | 414 |
| `selftest_s41.cpp` | the S4.1 selftest | 908 |
| `selftest_s42.cpp` | the S4.2 selftest, S5 woven in | 1,235 |
| `selftest_s8.cpp` | the S8 timeline | 552 |
| `selftest_s9.cpp` | the S9 timeline | 562 |
| `interactive.h` | `FrameInput`, `FrameResult`, `frame_step`, `run_interactive` declarations | 51 |
| `interactive.cpp` | the interactive loop, `frame_step`, the s7/s8/s9 e2e scripts | 1,594 |
| `encounter_rt.h` / `.cpp` | the stateless runtime helpers (+ `camera_sword_shoulder`) | 592 / 1,085 |
| `capture.h` / `.cpp` | view-proj, s1-status, screenshots/stills, screen boxes | 109 / 192 |
| `audio_seam.h` / `.cpp` | `AudioSeam` | 96 / 240 |
| `gamepad_input.h` / `.cpp` | `GamepadInput` | 40 / 67 |

(`wc -l` on the tree at `8515158`; the 24 files above sum to 10,654. Task 5's `run_flags.h` / `.cpp`, 33 / 53,
sit beside them and are outside that count.)

Step e: `make verify-long` → `verify: long s9 PASS (784 s, scripts/s9-acceptance.sh)` (the whole frozen
chain s9 → … → s3 on the split tree; 13 m 05 s wall). Shared frames: `frame_step` is the interactive
frame only — the S8/S9 selftests' `frame` lambdas and S4.2's `step_frame` differ from it (no death/return
machines or player-sfx drain, the S9 look before the player step, a basis-yaw override, no sword facing
turn), so they stay their own; the S9 shoulder shift is one function (`camera_sword_shoulder`) both the
loop and `--s9-selftest` call just before `sword_frame`.
