# S10–S11 — Accelerating the rebuild: faster loops, then colossi as data

**Status:** Proposed — builder call (the user asked for a plan to speed up completion and for it to be
written up, folding in the two next milestones; 2026-10-23). Starts after S8 and S9 merge (§13's
rulings, from a builder check and a code fact-check on 2026-10-23, override the sections above where they
differ). Every frozen acceptance (S3…S9) stays green and unchanged.

## 1 · Intent

After S8 the rebuild is roughly 10–15 % of the full game. The groundwork (converter, RE tooling, stage
system, acceptance discipline) is largely built; what remains is mostly content — 15 colossi, the
world, the story — and the current pace (one ~11-task milestone per feature, each re-verified through a
growing nested acceptance chain) would take far too long. Two levers carry most of the gain:

1. **Faster loops (S10).** 63 % of every nested acceptance run is the same 146-test `dotnet test`
   repeated six times, 12 % is `ctest` repeated seven times, and only ~22 % is unique content; the
   shared 12 GB library forces test-only commits on `main` every time a branch re-bakes. A single
   regression runner with golden outputs and content-hash stamps takes an app-only change from ≈ 8.5 min
   to ≈ 1.5 min of verification; a per-branch library view ends the pin juggling; a census of all 16
   colossi replaces per-milestone recon; and splitting `app.cpp` lets several agents build the app at
   once.
2. **Colossi as data (S11).** The fight is wired to Valus in ~70 places across 10 groups. An encounter
   description per colossus — Valus first, byte-for-byte, then Quadratus with the five mechanics he
   adds — turns each later colossus into data plus its unique mechanics. The census shows three colossi
   reuse the Valus system (two share his skeleton), seven reuse the quadruped walker Quadratus brings,
   and four need new systems (flight, swimming, a long body, the bow).

**Order.** S10 before S11 (swapped from the first proposal): every S11 step must prove that the frozen
outputs did not move, and S10's runner makes that proof ~5× cheaper.

## 2 · Goals

- **G1 — The regression runner** (`scripts/verify`): runs each milestone's selftest once against golden
  files (volatile fields masked), keeps the pixel guards and perf gates, skips work whose inputs did not
  change (content-hash stamps), runs headless suites in parallel with the GPU runs; the frozen
  `scripts/s*-acceptance.sh` stay untouched and remain the milestone-end long run.
- **G2 — The library view:** a per-branch view of `out/library` (APFS clones of models/audio/coredata/
  catalog, a symlink for textures) that `SOTC_LIB` and the converter's `--out` point at, so branch
  re-bakes and the S5/S6 audio clobber never touch the shared library or `main`.
- **G3 — The colossus census:** `tools/re/census/census.py` → `docs/notes/colossi-census.md` + JSON for
  all 16 colossi (model, joints, dead bodies, clip stem and count, death clips, weakpoint records and
  materials, arena dir and spawn, audio bank by cue stem, mechanics hints), with the columns that need
  RE or audition marked.
- **G4 — The app split:** `app.cpp` (6.5k lines) split into modules with zero behaviour change: one
  run-flags helper, the stateless helpers moved, a `StageCtx`, one file per selftest, the interactive
  loop with a `frame_step` seam.
- **G5 — The encounter description (S11):** a `constexpr EncounterDesc` per colossus selected by a
  manifest key; Valus round-trips through it with every frozen output byte-identical.
- **G6 — Quadratus (S11):** the second colossus, defeatable: `arena_02`, his clips and weakpoints, and the
  five new mechanics (a behaviour state machine, state-gated weakpoints, walking on a horizontal back,
  shake-off, entity locomotion for the charge).

## 3 · Non-goals

- A third colossus, flight/swimming/long-body/bow systems (the census ranks them; later packs),
  world streaming, cutscenes, menus/save.
- Replacing the frozen acceptance scripts (they stay as the long proof), a CI service, a JSON encounter
  format (a C++ table until something other than code authors encounters).
- A C++ `SOTC_LIB_OVERLAY` lookup (the clone view needs no app change; the overlay is recorded as the
  fallback).

## 4 · Anti-goals

- **A1 — A faster loop that proves less.** Every assertion of the frozen scripts keeps an equivalent in
  the runner (exact golden lines, the same pixel guards, the same perf gates); the runner refuses to run
  if `scripts/` differ from the pinned commit; the long run stays the milestone gate.
- **A2 — A refactor that moves a frozen number.** Every split step and the encounter round-trip are gated
  on byte-identical selftest output (timing fields masked) and the long run.
- **A3 — Losing the shared library.** The view is built from clones and a symlink; nothing deletes or
  rewrites the base.
- **A4 — Overclaiming the census.** Columns derived from names are labelled as such; weakpoint positions,
  state machines and cue identity stay RE/audition items.

## 5 · Constraints

- Apple Silicon, bash 3.2, stdlib Python, C++20; no new dependency; the game tree read-only; assets never
  committed; disk is ~96 % full (36 GiB free) — no full library copies.
- Frozen: `scripts/s3…s9-acceptance.sh`, the arena/encounter/shrine/plain/road manifests and their probe
  pins, every existing selftest's output.
- ≤ 4 concurrent agents; until the split lands, `app.cpp` has one writer at a time.

## 6 · What the recons found (2026-10-23)

**The chain.** s7 → s6 → s5 → s42 → s41 → s4 → s3, each level re-running its prologue: a nested S7 run
is 8 m 31 s; `dotnet test` ×6 ≈ 322 s (63 %), `ctest` ×7 ≈ 60 s (12 %), builds ~15 s, unique content
~113 s (s7 30, s6 18, s5 7, s42 11, s41 20, s4 19, s3 8 s). The 240-frame perf pair repeats in s4/s41/
s42; s42 re-runs its selftest for a purity check, s5 for audio-off identity. s5/s6 re-decode audio with
`--force`, pruning the library manifest to 8/10. Golden-able: every `sN-*` order/line check and the
hash/compose pins; masked: `ms=`, `frame_ms=`, `effect_ms`, `base_ms`, `marginal_ms`, `path=`; kept as
thresholds: floors, wall stops, load/frame ceilings; pixels: PNG magic/size/dimensions/span, the
crop-local guards (stdlib Python heredocs in s4/s41/s6), the s3 wave guard; `s1-pixels` means are
deterministic. Seeds for goldens exist on disk (`out/s7/s7-selftest.txt`, `s7-e2e.txt`,
`out/s6/s6-selftest.txt`, `out/s5/s5-selftest.txt`, `out/s42/events-with-stills.txt`, the run logs).
Every selftest opens a real window and Metal device; audio is capture-only in selftests; the
"never two acceptances at once" rule comes from shared `out/` files, not a measured GPU conflict
(untested).

**The library.** 12 GB (models 3.0 GB / 16,764 files, textures 8.9 GB, audio 55 MB); worktrees share it
through the `out` symlink. Readers: `asset_io.cpp` (glb/texture lookup, `weakly_canonical`),
`effect.cpp`, `audio.cpp` (with a `path_is_within` check), `app.cpp`, `main.cpp`, six test files'
`test_library()`, `probe.py --library`, `placements.py`. `cp -cR models` clones in 4.3 s using no space;
texture URIs resolve lexically, so a symlinked `textures/` should work (untested).

**The census (from data alone, ~1.5 s automated).** All 16 colossi convert `ok`; each has a dead body
(Valus a dirtmound, #4 a mound, #10 a stone); clip stems found by skeleton containment (coverage 1.0):
`minotaur_a` (Valus), `boss_a` (Quadratus), `knight_a`, `kirin_a`, `bird_a`, `minotaur_b`, `eel_b`,
`yamori_b`, `kame_a`, `narga_a`, `leo_a`, `poseidon_a`, `snake_c`, `cerberus_a`, `minotaur_c`,
`buddha_a`; all flag-clear for the S2 decoder (types 12/14 in ~15 clips are unvalidated); `minotaur_a/b/c`
share a skeleton; `leo_a`/`cerberus_a` are interchangeable. Arenas are the `arena_NN` zones
(`arena_12_south/north`), each with a converted level dir and cgpr set. Audio banks map by cue stem, not
number (2 → `boss01` `mammo_`, … ; Valus's `minotaur_*`/`mivo_*` cues are in `boss15`, which conflicts
with S5's `boss01` attribution — an audition item). **Four shipped Valus `.anb` files in the game tree
are clobbered** (`minotaur_a_death_front*.anb`, `objanim_…`, `camera_minoa_death_front_camera_01.anb` —
JSON dumps, sha mismatch; S6's "known clobber"); the library's baked copy is fine, but any re-bake from
source would fail — restoring them from `uroot/pak3.psarc` is the user's call (the tree is read-only to
the project).

**The coupling and the split.** Valus-specific: identity/assets (club `OptBone_sword`, corpse,
dirtmound; `entityRig = playerRig+1`), 7 entity clips, the skeleton map (`Bone_MUNE` chest,
`Bone_TUMASAKI_L/R` feet, `OptBone_hit` club tip), 2 weakpoints (`grab.cpp:130-139`) and their reaction
routing, the 9-band climb graph (`grab.cpp:76-87`, `ClimbBandId`), 8 capsules (`collision.cpp:127-136`),
14 behaviour constants (`valus.h:22-39`) + 3 damages + `apply_valus_impact`'s tiers, the death bundle
(5 timeline constants, 16 death-camera keys, the tendril fallback height, two roots), 7 `valus.*` cues
+ `music.boss` and the `Sfx`/`EncounterSfx` enums (tokens in the frozen s5 log), the HUD max health and
the target-camera distances. Generic already: collision resolve, the grab maths given a band table,
`player_step` (strike, stamina, catch, damage), `posed_weakpoint_proxy`, the death machine given
constants, the effect pass, the yaw turn, the audio engine. Biped-only: `climb_surface_point` (vertical
bands), the two-foot attack selection, the fixed chest, two tendril roots, a static entity. `run_app`
spans 1973–6508; its stage body is ~4,400 lines: setup ~490, s41 854, s42/s5 1,171, s7 352, s6 450,
s4 271, the interactive loop + e2e ~590; the five run-flag predicates (`frozen_selftest`,
`interactive_run`, `death_wired`, `audio_wanted`, `valus_behavior`) are five different truth tables.

## 7 · Decisions

| # | Decision | By | Why / cost if wrong |
|---|---|---|---|
| D1 | S10 = faster loops (runner, library view, census, app split); S11 = colossi as data (encounter description, Quadratus) | Builder | §1 order. Cost: Quadratus waits one milestone |
| D2 | The runner is a bash 3.2 script `scripts/verify` + stdlib Python helpers in `tools/verify/`: per milestone, run its selftest(s) once, mask volatile fields with a shared sed table, diff against `tests/golden/sN-*.txt`, run the frozen scripts' own pixel heredocs (extracted at runtime between `<<'PY'` and `PY` — one source of truth), keep the perf gates; `--bless` rewrites goldens only on request; `--only sN`; `--long` runs the frozen chain | Builder | §6. Cost: goldens to maintain |
| D3 | Stamps: content hashes of the inputs per step (dotnet: `src`, `tests`, `*.sln`, `Directory.Build.props`, the submodule SHA; ctest: `app/` + the library fingerprint; audio: the map JSON + the converter source; probes: `tools/re/stage` + the manifests) in `out/verify/`; a step runs only when its hash changed | Builder | §6. Cost: a missed input makes a stale pass — the long run is the backstop |
| D4 | Parallelism: `dotnet test` and the C++ build/ctest run concurrently with each other and with the GPU selftests; GPU selftests run up to 3-wide **only after** a measured check that two concurrent viewers keep `frame_ms` within the perf noise (±0.2 ms); the perf-bearing runs (s6/s7 perf, one perf pair) always run alone | Builder (veto) | The GPU sharing is untested. Cost: if the check fails, GPU runs stay serial (still ~2 min) |
| D5 | The library view: `make libview BRANCH=…` builds `out/libviews/<branch>/` with `cp -cR` clones of models/audio/coredata/catalog and a symlink `textures → ../../library/textures`; acceptance and bakes on that branch point `SOTC_LIB`/`--out` at it; a check that the app loads a glb's textures through the symlink comes first | Builder (veto) | Disk is 96 % full; clones cost nothing. Cost: if the symlinked textures fail the `path_is_within`/canonical checks, the fallback is the C++ overlay (§3) |
| D6 | The census is a checked-in tool (`tools/re/census/census.py`, from the recon prototype) writing `docs/notes/colossi-census.md` + `tools/re/census/census.json`; derived-from-names columns labelled; it also verifies the game tree's shipped hashes against `logs/game.sha256` and reports the clobbered files | Builder | §6. Cost: none |
| D7 | The app split order: (1) `run_flags(opts)` with named fields reproducing each of the five truth tables exactly + an exhaustive test over the flag inputs; (2) pure moves of the stateless helpers (`encounter_rt`, `capture`, `audio_seam`, `gamepad_input`); (3) `struct StageCtx` (~40 locals; `hud_state`, `soft_reset_encounter`, `press_reset`, the wake lambdas become methods) + `stage_setup.cpp`; (4) one file per selftest, smallest first (s3 → s4 → s6 → s7 → s41 → s42/s5), duplicated helpers left for later; (5) the interactive loop + e2e to `interactive.cpp` with a `frame_step(StageCtx&, FrameInput)` seam. Each step gated on byte-identical selftest output (masked) via the runner, the long run at the end | Builder | §6. Cost: a long refactor; done in S10 because S11 and every later colossus need it |
| D8 | The encounter description is a `constexpr EncounterDesc` C++ table per colossus (`app/src/encounters/valus.h`, `quadratus.h`) selected by a manifest key `encounter=<id>` (default `valus` when the key is absent, so frozen manifests load unchanged); Valus's tables copy today's literals byte-for-byte in today's order (band ties and capsule indices depend on it); `ClimbBandId`/`ValusAttack` stay as aliases over table indices; the `Sfx` and band tokens printed in frozen logs are kept | Builder | No JSON parser; tests name the enums 113 times; `static_assert` checks shape. Cost: a rebuild per colossus change |
| D9 | Quadratus's new mechanics are generic modules, not bull code: an entity behaviour state machine (states, transitions on hits/timers, clips per state), weakpoint gates per state (a hit can change state instead of dealing damage), a surface-walk climb mode for horizontal bands, shake-off (stamina drain or forced release while attached), entity locomotion with ground follow; data for the bull lives in `quadratus.h` | Builder | Seven census colossi reuse the quadruped walker. Cost: generality now, before a second user |
| D10 | Quadratus's audio waits for the census's bank mapping (`boss01` holds `mammo_*` — his stem) and an audition; until then his fight is silent except the shared player cues (a recorded gap); Valus's S5 attribution is re-checked by audition against `boss15` and corrected only if the ear agrees (a separate, recorded change) | Builder | The census found the conflict. Cost: a silent second fight for one milestone |

## 8 · The design (sketch; the plan carries the tasks)

- **S10:** the runner + goldens (seeded from existing outputs, masked, blessed once, then compared); the
  stamps; the parallel-viewer measurement; the library view; the census tool; the five split steps,
  each landing with the runner green; the long run at the milestone's end; AGENTS.md and the Makefile
  (`make verify`, `make verify-long`, `make libview`, `make census`).
- **S11:** the `EncounterDesc` schema + Valus's table (round-trip proof); the manifest key; the five
  mechanics as generic modules with unit tests; Quadratus's table, `arena_02` composed by the probe
  (`STAGES["arena_02"]`), his bake (58 clips), his weakpoints and dead body; `--s11-selftest` (a scripted
  hoof stab → kneel → back walk → back sigil → head sigil → death) and an e2e; `scripts/s11-acceptance.sh`
  through the runner's long mode.

## 9 · The builder's calls

The golden file layout and the mask table; the stamp storage; the parallel width (≤ 3); the libview
naming; the census JSON shape; the `StageCtx` member list; the `EncounterDesc` field names; Quadratus's
state timings and shake-off strength (authored where the census/RE has nothing); the S11 stills.

## 10 · Risks

| Risk | Severity | Mitigation |
|---|---|---|
| A runner that passes when a frozen script would fail | high | Same assertions, pixel code extracted from the frozen scripts, the long run per milestone |
| Concurrent viewers distort perf numbers | medium | D4's measurement gate; perf runs alone |
| The split moves a frozen number | high | Byte-identical masked outputs per step, the long run |
| The symlinked textures fail the loader's path checks | medium | D5's check first; the C++ overlay fallback |
| The quadruped mechanics leak bull specifics | medium | D9: generic modules + data; the census's reuse list as the review lens |
| The clobbered Valus `.anb` files block a Valus re-bake | medium | Not needed in S10/S11; reported by the census; the user restores them |

## 11 · The walk (the user's, PENDING after S11)

Quadratus: stab a rear hoof, he kneels, climb his fur, walk his back, strike the back and head sigils,
he shakes you, he charges, he dies; R; windowed/cursor; fps. S10 has no walk (tooling); its proof is the
runner matching the long run.

## 12 · Acceptance

S10: `make verify` green on a no-op change in seconds, on an app change in ≈ 1.5 min, matching the long
run's verdict on the same tree; the split landed with byte-identical masked outputs; the census note
committed. S11: `scripts/s11-acceptance.sh` green; Valus unchanged; the Quadratus walk added.

## 13 · Review rulings (2026-10-23; binding, override the text above)

1. **What `make verify` runs.** Every assertion class of the frozen scripts has a runner equivalent,
   each run when its stamp changes: the selftest lines, every viewer run each script makes (≈ 27 across
   s3–s7: s4 alone has six — the no-entity/no-HUD controls, the bbox capture), the bash thresholds that
   follow each `<<'PY'` heredoc (copied into a per-milestone adapter `tools/verify/sN.sh` — a second copy,
   said plainly), the relational checks (s42 stills-on vs stills-off; s5 audio-off identity), the audio
   decode counts/peaks and the s5 mixdown ffprobe/astats checks, the probe pins/`check-encounter`, and the
   S7 placements cross-check. The S8/S9 e2e runs are included by default; `--quick` skips e2e runs for the
   inner loop. The ≈ 1.5 min target becomes "measured in Task 1" (expected 2–4 min for an app change
   with e2e).
2. **Paths are normalised, not masked.** `path=` and `mixdown=` are asserted exactly by the frozen
   scripts; the runner rewrites the repo/out-dir prefix to `<OUT>` and keeps the rest. Timing fields
   (`ms=`, `frame_ms=`, `effect_ms`, `base_ms`, `marginal_ms`) are masked.
3. **Goldens come from fresh runs on the pinned commit,** run twice and diffed to find unstable fields —
   never from files under the shared `out/`, which other branches rewrite. Runner output goes to
   `out/verify/<worktree>/`. Goldens live in `tools/verify/golden/` (outside the dotnet stamp's
   `tests/`).
4. **The runner's long mode and pin** use the highest `scripts/sN-acceptance.sh` that exists at run time;
   the pin check covers the frozen scripts that existed at the pin and moves forward when a milestone
   adds its script. S10 starts after S9 merges, so s8/s9 exist.
5. **Stamps (D3) are complete:** dotnet also hashes `tools/re/audio/*.json` and `tools/native/`; probes
   also `tools/re/s7/shrine_placements.json`, `tools/re/s8/placements.json`, `tools/re/s9/*`,
   `tools/re/anb`; GPU runs hash the app tree (sources, shaders, CMake), the manifests, the library's glbs
   and `.clips.json` and `audio.manifest`, `app/src/shrine_points.h`, and the realpath of `SOTC_LIB`. The
   runner decodes the current audio map once per audio stamp (the library holds whatever the last
   frozen script left — 8 or 10 — and S8+ needs 34).
6. **The library view clones everything:** `cp -cR` of `models/`, `textures/` (2,493 files), `audio/`,
   `coredata/`, `catalog.json` — no symlinks, because the converter rewrites every referenced texture PNG
   on each bake (`ModelConverter.cs:80-87` → `CtxrConverter.cs:351-356`) and a symlinked `textures/` would
   write through into the base. The Makefile honours an exported view
   (`LIB ?= $(or $(SOTC_LIB),$(CURDIR)/out/library)`).
7. **Concurrency.** The GPU-width measurement (Task 4) runs three viewers at once, with nothing else
   running; AGENTS.md's "never two acceptances concurrently" is reworded to "never two runs sharing an
   `out/` directory; the runner parallelises within its own `out/verify/<worktree>/`".
8. **`run_flags`** (amended 2026-10-23, ruling A9: split by availability — `RunFlags run_flags(opts, stage, followMode, returnAvailable)` for the pre-load fields plus S8/S9's `s8Wired`/`s9Wired`/`road` and the folded `scriptedE2e`/`audioStrict`; `SceneFlags scene_flags(opts, stage, followMode, hasEntity, entityRigValid)` for `audioWanted`/`valusBehavior`, called after the manifest loads; `is_frozen_selftest(opts)`) reproduces seven fields exactly — `frozenSelftest` (main.cpp:325), `interactiveRun`,
   `deathWired`, `audioWanted`, `valusBehavior`, `returnWired`, `pumpLoad` — taking every input they read
   (`opts`, the stage, `followMode`, `hasEntity`, `entityRigValid`, `returnAvailable`); the exhaustive test
   covers all their boolean inputs (~32k cases).
9. **Selftests that cross stages.** The split's per-selftest functions return
   `SelftestResult { std::optional<int> exitCode; std::optional<StageId> next; }`; per-run state (the
   carry, `first_stage`, `return_available`) is split from per-stage state (`StageCtx`); s4's two blocks,
   s5 woven into s42, and the plain `--selftest` are listed explicitly.
10. **S11 data shape.** Valus has three attacks (`StompL`, `StompR`, `Club`) plus `None`; band links
    become `int` indices (−1 = none) instead of `ClimbBandId` values, with `ClimbBandId` kept as named
    Valus indices; weakpoints are their own table (material, reaction clip, gate) instead of the band
    row's last field.
11. **The encounter directive.** `encounter <id>` is a new manifest directive, accepted only when the
    caller asks (the `posed`/`horse` opt-in), in both `manifest.cpp` and `probe.py`; absent means `valus`.
12. **arena_02.** A real `STAGES["arena_02"]` branch with the shrine-style exclusions (67 glbs, 28 stubs);
    a composer for the arena_02 encounter (the arena_01 `compose-encounter` is hard-coded); Quadratus's
    placement from the `arena_02` cgpr (the gate reads it; the dead body sits at (245, −98, −818)); the
    spawn's y sampled from the ground, its yaw authored.
13. **No return to the shrine after Quadratus** in S11: `returnWired` is off in arena_02; the corpse stays;
    `R` resets. The colossus-2 loop is later.
14. **Valus's audio-bank conflict is recorded, not corrected:** `s5_map.json` is frozen; a correction is a
    separate decision for the user.
15. **The census:** paths as arguments (no hard-coded worktree/library); the sha check limited to the
    colossus model dirs and `cp3shadow/anim` (the whole tree is 130k files); `make census` added.
