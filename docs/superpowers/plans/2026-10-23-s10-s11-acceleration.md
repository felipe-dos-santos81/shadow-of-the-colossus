# S10–S11 Acceleration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut the verification loop from ≈ 8.5 min to ≈ 1.5 min, stop branch re-bakes from touching the shared library, census all 16 colossi, split `app.cpp` for parallel work (S10), then make the colossus fight data-driven and add Quadratus (S11).

**Architecture:** S10 adds a bash/stdlib-Python regression runner with golden outputs and content-hash stamps over the unchanged frozen scripts, a clone-and-symlink library view per branch, a checked-in census tool, and a five-step behaviour-preserving split of `app.cpp`. S11 adds a `constexpr EncounterDesc` per colossus (Valus round-tripped byte-for-byte), five generic entity mechanics, and Quadratus in `arena_02`.

**Tech Stack:** C++20, SDL3/SDL_GPU/Metal, doctest, bash 3.2, Python 3 stdlib, .NET 10 (BpConvert), APFS clones.

**Spec:** `docs/superpowers/specs/2026-10-23-s10-s11-acceleration-design.md` — its §13 review rulings are binding and override the plan text below where they differ.

## Global Constraints

- Frozen and never modified: `scripts/s3…s9-acceptance.sh`; the arena/encounter/shrine/plain/road manifests and their probe pins; the output of every existing selftest/e2e (timing fields masked).
- The runner refuses to run if `git diff --quiet <pin> -- scripts/s*-acceptance.sh` fails; the frozen chain (`--long`) remains each milestone's final gate.
- No full library copies (disk ~96 % full); the base `out/library` is never deleted or rewritten by a view.
- Bash 3.2; stdlib Python; C++20; no new dependency; `caffeinate -dimsu` around viewer/acceptance runs; the game tree read-only.
- Until Task 6 (the split) completes, one agent writes `app.cpp` at a time.
- Valus's encounter tables copy today's literals in today's order; the `Sfx`/band tokens in frozen logs never change.

## Review Focus

1. **A golden that hides a regression.** A masked field must never be one a frozen script asserts exactly. Test: `tools/verify/test_masks.py` — every field the frozen scripts compare exactly survives masking (fixture lines from each script).
2. **A stale stamp.** An input change the stamp misses would skip a needed run. Test: `tools/verify/test_stamps.py` — touching each listed input class changes its stamp; `--long` agrees with `make verify` on the same tree (Task 4).
3. **Textures through the libview symlink.** The loader's canonical/`path_is_within` checks must accept them. Test: Task 3's check loads `wander.glb` from a view and compares its decoded textures with the base's.
4. **A split step that changes a selftest.** Test: each Task 6 step runs `make verify` (all goldens) and is reviewed on the masked diff being empty.
5. **Valus after the encounter table.** Every frozen line, still and perf gate unchanged. Test: Task 8 runs the long chain; `test_encounter.cpp` compares the table against the pre-refactor literals.

---

## File Structure

- `scripts/verify`, `tools/verify/{run.py,masks.py,stamps.py,test_masks.py,test_stamps.py}`, `tools/verify/sN.sh` (one adapter per milestone: its viewer runs, heredoc arguments and bash thresholds), `tools/verify/golden/sN-*.txt` — the runner.
- `tools/verify/libview.sh`, `Makefile` (`verify`, `verify-long`, `libview`, `census`) — the view and targets.
- `tools/re/census/census.py`, `tools/re/census/census.json`, `docs/notes/colossi-census.md` — the census.
- `app/src/run_flags.h/.cpp`, `app/src/encounter_rt.*`, `capture.*`, `audio_seam.*`, `gamepad_input.*`, `stage_ctx.h`, `stage_setup.cpp`, `selftest_s3.cpp` … `selftest_s42.cpp`, `selftest_s6.cpp`, `selftest_s7.cpp`, `selftest_s8.cpp`, `selftest_s9.cpp`, `interactive.cpp` — the split.
- `app/src/encounter_desc.h`, `app/src/encounters/{valus.h,quadratus.h}`, `app/src/behaviour.h/.cpp`, `app/src/grab.*` (surface walk), `app/src/locomotion.h/.cpp` — S11.
- `tools/re/stage/probe.py` (`STAGES["arena_02"]`), `scripts/s11-acceptance.sh`, `docs/notes/s10-results.md`, `docs/notes/s11-results.md`, `AGENTS.md`.

**Lanes:** S10 — Task 1 (runner) ‖ Task 2 (census) ‖ Task 3 (libview); Task 4 (parallel GPU measurement) after Task 1; Task 5 (run-flags, exhaustive test) after Task 1; Task 6 (the split, steps a–e, one writer) after Task 5. S11 — Task 7 (schema + Valus round-trip) after Task 6; Task 8 (the five mechanics) ‖ Task 9 (arena_02 + Quadratus bake/data) after Task 7; Task 10 (Quadratus wired + selftest/e2e) after 8–9; Task 11 (acceptance + results).

## S10 — Faster loops

### Task 1: The regression runner and goldens

**Files:** Create `scripts/verify`, `tools/verify/{run.py,masks.py,stamps.py,pixels.py,test_masks.py,test_stamps.py}`, `tests/golden/`; Modify `Makefile`.

**Interfaces:** `scripts/verify [--only sN] [--bless] [--long] [--jobs N]` → exit 0/1, a summary line per milestone `verify: sN PASS|FAIL (<seconds> s, <reason>)`; masks: a table of `(line prefix, field)` replaced by `<masked>`; stamps: `out/verify/<step>.stamp` holding the SHA-256 of the step's sorted input hashes.

- [ ] **Step 1: Failing tests** — `test_masks.py`: for each frozen script, every field it compares with `=`/`[[ … = … ]]` against a literal is NOT masked; `ms=`, `frame_ms=`, `effect_ms`, `base_ms`, `marginal_ms`, `path=` are. `test_stamps.py`: changing one file in each input class changes that step's stamp and no other's.
- [ ] **Step 2: Run** `python3 -I tools/verify/test_masks.py && python3 -I tools/verify/test_stamps.py` → fail.
- [ ] **Step 3: Implement** — per design D2/D3 and §13.1–5: one adapter per milestone (`tools/verify/sN.sh`) reproducing every viewer run of its frozen script (with env and heredoc arguments) and its bash thresholds, the relational checks, the audio/probe checks; output to `out/verify/<worktree>/`; paths normalised to `<OUT>`, timings masked; the frozen scripts' `<<'PY'` heredocs extracted at runtime; perf gates kept; the `scripts/` pin check against the scripts existing at the pin; `--long` = the highest existing `scripts/sN-acceptance.sh`; `--quick` skips e2e runs.
- [ ] **Step 4: Bless from fresh runs** — on the pinned commit (main after S9), run every adapter twice, diff the two runs to find unstable fields (add them to the mask table with a comment), bless `tools/verify/golden/`; then `scripts/verify` → PASS on the unchanged tree; time it cold and warm; then `scripts/verify --long` on the same tree and record that both verdicts agree.
- [ ] **Step 5: Agreement** — introduce a one-line deliberate change to a selftest print in a scratch branch: `scripts/verify` fails naming the line; revert.
- [ ] **Step 6: Commit** — `git commit -m "feat: a regression runner over the frozen selftests"`

### Task 2: The colossus census

**Files:** Create `tools/re/census/census.py`, `tools/re/census/census.json`, `docs/notes/colossi-census.md` (from the recon prototype in the session scratchpad `s11-census/`).

- [ ] **Step 1:** port the prototype (stdlib + `tools/re/stage/spawn_probe.py` + `tools/re/anb/parse2.py`) with the game tree, library and output paths as arguments; a `--check` mode asserting 16 rows, every stem's skeleton coverage = 1.0, every arena dir present, and the shipped hashes (from `logs/game.sha256`) of the colossus model dirs and `cp3shadow/anim` — reporting mismatches (the known Valus clobber) without failing on them; `make census` in the Makefile.
- [ ] **Step 2:** run it; write the note (the table, the mechanics ranking, the RE/audition-only columns, the bank-by-cue-stem mapping and the Valus `boss01`/`boss15` conflict, the clobbered shipped files with their paths and expected hashes from `logs/game.sha256`).
- [ ] **Step 3: Commit** — `git commit -m "chore(re): census all sixteen colossi"`

### Task 3: The library view

**Files:** Create `tools/verify/libview.sh`; Modify `Makefile` (`libview`), `AGENTS.md` (how branches use it).

**Interfaces:** `make libview BRANCH=<name>` → `out/libviews/<name>/` with `cp -cR` clones of `models/`, `textures/`, `audio/`, `coredata/`, `catalog.json` (no symlinks — §13.6); prints the path to export as `SOTC_LIB`; the Makefile's `LIB ?= $(or $(SOTC_LIB),$(CURDIR)/out/library)`.

- [ ] **Step 1:** build a view; time the clones; run `ctest` and a short `--selftest` with `SOTC_LIB` at the view (green).
- [ ] **Step 2:** a re-bake into the view (`--out` at the view) leaves the base's `wander.glb` **and every base texture PNG it references** byte-identical (hash before/after); `df` shows no material growth.
- [ ] **Step 3: Commit** — `git commit -m "feat: per-branch library views"`

### Task 4: Concurrent viewers (measurement)

- [ ] **Step 1:** with nothing else running on the machine, run three S4.2 perf pairs concurrently vs alone (three trials each): if every concurrent marginal stays within ±0.2 ms of the solo baseline, set the runner's GPU width to 3 (perf runs alone); else width 1. Reword AGENTS.md's concurrency rule per §13.7.
- [ ] **Step 2:** record the numbers in `docs/notes/s10-results.md`; commit — `git commit -m "chore: measure concurrent viewers and set the runner width"`

### Task 5: One run-flags helper

**Files:** Create `app/src/run_flags.h/.cpp`, `app/tests/test_run_flags.cpp`; Modify `app/src/app.cpp`, `app/src/main.cpp`, `app/CMakeLists.txt`.

**Interfaces (§13.8):** `struct RunFlags { bool frozenSelftest, interactiveRun, deathWired, audioWanted, valusBehavior, returnWired, pumpLoad; }; RunFlags run_flags(const AppOptions&, StageId, bool followMode, bool hasEntity, bool entityRigValid, bool returnAvailable);` — each field reproduces today's expression exactly (main.cpp:325; app.cpp ~2119, ~2128, ~2156, ~2460, ~2511).

- [ ] **Step 1: Failing test** — an exhaustive table over every selftest/e2e flag, `entityTimeSet`, `screenshotPath` empty/non-empty, `noReturn`, and `hasEntity`: each field equals the old inline expression (copied verbatim into the test as the oracle).
- [ ] **Step 2–4:** run → fail; implement; replace the five inline expressions; run → green.
- [ ] **Step 5:** `scripts/verify` → PASS. **Commit** — `git commit -m "refactor(app): one run-flags helper"`

### Task 6: Split `app.cpp` (steps a–e, one writer)

Each step: move code with no behaviour change, `cmake --build`, `ctest`, `scripts/verify` → PASS with an empty masked diff, commit `refactor(app): <step>`.
- [ ] **a.** Pure moves: `encounter_rt.{h,cpp}` (the stateless encounter helpers), `capture.{h,cpp}` (screenshots/stills), `audio_seam.{h,cpp}`, `gamepad_input.{h,cpp}`.
- [ ] **b.** `struct StageCtx` (`stage_ctx.h`) holding run_app's per-stage locals; `hud_state`, `soft_reset_encounter`, `press_reset` and the wake lambdas become members; `stage_setup.cpp` builds it (`std::optional<int>` exit code on failure).
- [ ] **c.** One file per selftest, smallest first: the plain `--selftest`, `selftest_s3.cpp`, `selftest_s4.cpp` (both of its blocks), `selftest_s6.cpp`, `selftest_s7.cpp`, `selftest_s41.cpp`, `selftest_s42.cpp` (with s5 woven in), and the S8/S9 selftests; each `SelftestResult run_sNN(StageCtx&, RunState&)` (§13.9: `{ optional<int> exitCode; optional<StageId> next; }`, per-run state split from per-stage state); duplicated helpers stay duplicated.
- [ ] **d.** `interactive.cpp`: the interactive loop and the e2e scripts behind `frame_step(StageCtx&, const FrameInput&)`.
- [ ] **e.** The long run: `make verify-long` → PASS. Record the line counts before/after in `s10-results.md`.

### Task S10-close: results

- [ ] `docs/notes/s10-results.md` (measured loop times cold/warm per change class, the width, the view check, the census summary, the split's file map); AGENTS.md (`make verify` is the per-task gate, `make verify-long` the milestone gate, `make libview` per branch); commit `docs: S10 results`.

## S11 — Colossi as data

### Task 7: The encounter description + Valus round-trip

**Files:** Create `app/src/encounter_desc.h`, `app/src/encounters/valus.h`, `app/tests/test_encounter.cpp`; Modify `app/src/grab.*`, `collision.*`, `valus.*`, `death.*`, `audio_map.h`, `camera.*`, `stage_setup.cpp`, `manifest.*` (the `encounter=<id>` key, default `valus`).

**Interfaces (design D8):** `struct EncounterDesc { const char* id; int maxHealth; JointMap joints; span<PropDesc> props; span<WeakpointDesc> weakpoints; span<BandDesc> bands; span<CapsuleDesc> capsules; float turnDegPerSec; span<AttackDesc> attacks; DeathDesc death; CueDesc cues; TargetCamDesc targetCam; };` with `ClimbBandId`/`ValusAttack` aliases over table indices.
- [ ] **Step 1: Failing test** — `test_encounter.cpp`: Valus's table equals today's literals (copied verbatim as the oracle) field by field and order by order; `static_assert`s on counts (9 bands with `int` links, 8 capsules, 2 weakpoints in their own table, 3 attacks + `None`) — §13.10.
- [ ] **Step 2–4:** run → fail; implement; thread `const EncounterDesc&` through StageCtx; run → green.
- [ ] **Step 5:** `scripts/verify` → PASS (empty masked diff), then `make verify-long` → PASS. **Commit** — `refactor(app): the encounter description, Valus first`.

### Task 8: The five generic mechanics

**Files:** Create `app/src/behaviour.h/.cpp`, `app/src/locomotion.h/.cpp`; Modify `app/src/grab.*` (surface walk), `player_state.*` (shake-off), tests.
- [ ] **Behaviour machine:** states with clips and transitions on hit-events/timers; Valus expressed as a one-state machine (unchanged outputs).
- [ ] **Weakpoint gates:** a weakpoint's hit either deals damage or fires a transition, per state.
- [ ] **Surface walk:** a climb mode on bands flagged horizontal — the player stands on the band's top surface (normal from the bone frame) and walks along/across it.
- [ ] **Shake-off:** an entity action that drains stamina or forces a release while the player is attached.
- [ ] **Locomotion:** the entity moves along its yaw with ground follow at a clip-measured speed.
Each with unit tests first; `scripts/verify` → PASS (Valus unchanged); commit per mechanic `feat(app): …`.

### Task 9: Quadratus's data

**Files:** Modify `tools/re/stage/probe.py` (a real `STAGES["arena_02"]` branch with the shrine-style exclusions, an arena_02 encounter composer, the `encounter` directive opt-in — §13.11–12), `app/src/manifest.*` (the `encounter <id>` directive, opt-in), `Makefile` (bake `colossus_02_bull_a/_cmn/colossus_02_bull_a` with its 58 clips into the branch's libview); Create `app/src/encounters/quadratus.h` (from the census row + a gate for weakpoint shells, bands and capsules on his 34-joint skeleton, his placement from the `arena_02` cgpr), `out/re/stage/arena_02.manifest` (`encounter quadratus`, spawn x/z (273, −842) with y from the ground and an authored yaw).
- [ ] compose + `check-manifest --stage arena_02`; the bake in the libview; the table's joints/bones resolve in his glb (a test).
- [ ] Commit — `feat: Quadratus's data and arena`.

### Task 10: Quadratus wired + proof

- [ ] `--s11-selftest`: in `arena_02`, scripted: stab a rear hoof → kneel → climb the fur → surface-walk the back → strike the back sigil → the head sigil → death; a shake-off survived; a charge dodged; lines `s11-*`; stills.
- [ ] `--s11-e2e`: the interactive loop from the arena_02 spawn to his death; `returnWired` off in arena_02 (§13.13) — the corpse stays, `R` resets.
- [ ] `scripts/verify` → PASS; commit — `feat(app): Quadratus, wired and proven`.

### Task 11: The S11 acceptance and results

- [ ] `scripts/s11-acceptance.sh` (bash 3.2): `make verify-long`'s chain first, then the S11 checks; `PASS —`.
- [ ] `docs/notes/s11-results.md` (measured; authored values labelled; the silent fight per D10; the Valus bank conflict recorded, not corrected — §13.14); AGENTS.md; commit — `docs: S11 acceptance and results`.

## Self-Review

- **Spec coverage:** G1 → Tasks 1, 4; G2 → Task 3; G3 → Task 2; G4 → Tasks 5, 6; G5 → Task 7; G6 → Tasks 8–10. D2/D3 → 1; D4 → 4; D5 → 3; D6 → 2; D7 → 5, 6; D8 → 7; D9 → 8; D10 → 9/11.
- **Frozen paths:** every app-touching task ends with `scripts/verify`; Tasks 6e and 7 with the long run.
- **Names:** `scripts/verify`, `RunFlags`/`run_flags`, `StageCtx`, `run_sNN`, `frame_step`, `EncounterDesc`, `encounter=<id>` — consistent.
