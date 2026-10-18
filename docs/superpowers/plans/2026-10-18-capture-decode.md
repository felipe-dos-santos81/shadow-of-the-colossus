# S2 completion + decode continuation (W3) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The design-activated capture workflow — reference stills of Wander's idle (and the horse) from the port rig — plus the decode's unblocking step (the Ghidra keyframe sampler), so S2's sign-off can land if the decode cracks.

**Architecture:** A thin macOS capture driver in this repo drives the user's port rig (build/staging per its own run-sheet; scripted pad input via `BB_PAD_FILE`; frame dumps via `BB_FRAME_DUMP_TRIGGER`). A Ghidra pass decompiles the sampler chain around `FUN_0066fe60` and correlates it with `docs/notes/re-anb.md` §3's structure map. The plan ends with a verbatim gate decision; a confirmed decode hands off to the converter plan (Plan C), a negative keeps S2's sign-off open with the captures standing as the §5 artifact.

**Tech Stack:** Python 3 (capture driver) · Ghidra 12.1.4 headless (the RE-0 baseline) · bash 3.2 · the user's port at `~/code/mine/ShadowOfTheColossus-PC` (dev-only; nothing from it enters the product path).

**Spec:** `docs/superpowers/specs/2026-10-18-s3-arena-design.md` — workstream W3, §3/§4 · `docs/superpowers/specs/2026-10-18-first-playable-design.md` §4–§5.

## Global Constraints

- **Read-only inputs**: the game tree, the port repo, the Ghidra bundle. The capture driver writes only into the port's own `out/` and its user dir; saves are never touched (use the port's own save-template discipline).
- Derived artifacts live under `out/re/capture/` and `out/re/ghidra-*.log` (gitignored). Committed: `tools/re/capture/port_capture.py`, `tools/re/ghidra/FindSampler.java`, `docs/notes/re-capture.md`, `docs/notes/re-anb.md` (§5 update).
- Ghidra home default `~/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC`; the RE-0 project is built in this worktree by `./scripts/re0-acceptance.sh` with `SOTC_UROOT=…/SOTC-extract/extracted/uroot` exported (the worktree-relative default does not resolve).
- Python 3 on PATH; scripts fail loudly with a one-line cause; the capture driver must be macOS-correct (it may replicate `tools/sotc_play.py`'s loop — that tool is Windows-shaped; do not modify the port repo).
- The port runs APP_VER **01.00**; its patch modes are EU 1.01-only (fps60/af16 and `SOTC_CVARS` will not apply) — the run-sheet notes the workarounds; all 01.00 behavior is inference until C1 proves it.
- The candidate curve data from RE-1's B2 is marked UNVERIFIED and must never be consumed as animation data.
- Small typed commits; nothing from the dump or the port is committed.

## Review Focus

1. Port boot on 01.00 fails → the gate's documented fallbacks fire (shadPS4 per D-S9, or park captures and continue decode-only); no half-claimed captures.
2. Shot timing and PNG naming are nondeterministic → every capture writes a manifest entry (pinned time, route token, file, sha256) so the comparison is reproducible.
3. A capture run that would touch saves → the save-template restore/never-write discipline holds.
4. The Ghidra pass yields a negative → documented with the exact stopping point, no overclaim.
5. The captures must never be presented as product data — dev-only, labeled as reference.

---

### Task C1: Port bring-up spike (the gate)

**Files:**
- Create: `docs/notes/re-capture.md` (§1 run-sheet-as-measured + §2 the gate verdict)
- (No repo code yet; all port-side outputs stay in the port's `out/`.)

**Interfaces:**
- Consumes: the port's own run-sheet (`README.md`, `build.sh`, `Makefile`, `tools/macos_check.py`, `sotc.py`, `scripts/prepare.py` — read-only).
- Produces: a working `out/bb-probe` build + a staged game dir + the gate verdict: **BOOT** (main loop reached, a frame presented, per `make verify`'s markers) or **NO-BOOT** (documented failure point).

- [ ] **Step 1: Build the port (long — background it)**
  `brew install cmake ninja pkgconf glslang nasm python`; provide an **x86_64** `libMoltenVK.dylib` (`BB_MOLTENVK` or `out/moltenvk`); `make driver` with `BB_KOSMICKRISP_URL` (prebuilt) or `BB_KOSMICKRISP_TARBALL`; `bash build.sh`; `bash build.sh --test`. Record every deviation in the notes.

- [ ] **Step 2: Stage the game dir**
  Assemble `out/CUSA08809` (symlink layout acceptable) with: `eboot.bin`, `sce_module/*.prx` (incl. `libc.prx`, `libSceFios2.prx`), `sce_sys/param.sfo` (from `SOTC-extract/pkg-meta/param.sfo`), `dvdroot_ps4/` (the extracted data tree). **Check first whether a 1.01 update tree exists** next to the 01.00 dump — if it does, `sotc.py` merges its `eboot`/`sce_module`/`param.sfo` and the 1.01 gates pass; record the finding either way. Set `SOTC_SYS_MODULES` to include `libSceUlt.sprx` + `libSceNgs2.sprx` (else silence).

- [ ] **Step 3: Run the gate**
  `make verify` (30 s, log markers) → then a real boot: `python3 sotc.py` (macOS env is set by it). Gate = main loop + a frame presented. If NO-BOOT: record the exact failure (libc `_init_env` gate, patch-mode mismatch, missing modules) and stop C2; C3–C4 still run.

- [ ] **Step 4: Write `docs/notes/re-capture.md` §1–§2** — the measured run-sheet (commands that worked, deviations, versions) + the gate verdict with its evidence.

- [ ] **Step 5: Commit**

```bash
git add docs/notes/re-capture.md
git commit -m "docs(re): port bring-up run-sheet + boot gate verdict"
```

---

### Task C2: Capture driver + the idle/horse stills

**Files:**
- Create: `tools/re/capture/port_capture.py`
- Modify: `docs/notes/re-capture.md` (§3 captures)
- Outputs (gitignored): `out/re/capture/*.png`, `out/re/capture/manifest.json`

**Interfaces:**
- Consumes: a booted port (C1); the port's hooks (`BB_PAD_FILE` tokens — held buttons + `lx= ly= rx= ry= 0..255`; `BB_FRAME_DUMP_TRIGGER`; screenshots land in `<BB_GPU_USER_DIR>/screenshots`).
- Produces: `python3 tools/re/capture/port_capture.py <schedule-file> --out out/re/capture` — a schedule file of `<time-seconds> <action>` lines (`action` = pad tokens joined by `,` or `shot`); the driver: sets the env the port needs on macOS (`DYLD_LIBRARY_PATH`, `VK_DRIVER_FILES`), launches `sotc.py` with `BB_PAD_FILE` + a per-shot `BB_FRAME_DUMP_TRIGGER`, waits per the schedule, copies each produced PNG into `--out`, appends a manifest entry `{time, action, file, sha256}`, and restores the save-template **before each run** (a known starting state; the run never depends on its own saves).

- [ ] **Step 1: Write the driver** (macOS-correct: do not reuse the Windows-shaped `sotc_play.py`; replicate its loop: pad-file writes, trigger files, PNG collection).
- [ ] **Step 2: Discover the route** — boot to a Wander-idle state (title → new/loaded game per the available saves; use the port's save-template). Record the working route in the schedule file. Iterate until a stable idle shot is produced.
- [ ] **Step 3: Capture** — idle: shots at t ≈ 5/10/15 s after gameplay is reached; horse: a second run if a reachable horse state exists (record honestly if not).
- [ ] **Step 4: Verify** — ≥3 idle PNGs exist, each non-uniform (largest channel span ≥ 30, the s1 style), manifest entries complete, saves untouched (restore ran).
- [ ] **Step 5: Write notes §3 + commit**

```bash
git add tools/re/capture/port_capture.py docs/notes/re-capture.md
git commit -m "feat(re): port capture driver + idle/horse reference stills"
```

---

### Task C3: Decode continuation — the Ghidra sampler

**Files:**
- Create: `tools/re/ghidra/FindSampler.java`
- Modify: `docs/notes/re-anb.md` (§5 Ghidra sampler findings — a new section; the file is owned by RE-1, this task appends)
- Outputs (gitignored): `out/re/ghidra-sampler.log`

**Interfaces:**
- Consumes: the RE-0 baseline (built in this worktree by `./scripts/re0-acceptance.sh`); RE-1's findings (`docs/notes/re-anb.md` §3–§4: the 9-field re-based record around `FUN_0066fe60`, base `DAT_02f3b8c0`, the candidate encoding marked UNVERIFIED).
- Produces: `FindSampler.java` — decompiles `FUN_0066fe60` and its callers/callees to a deterministic log; the notes section records either the cracked reading (with the exact arithmetic: field roles, time base, quaternion reconstruction) or the documented negative.

- [ ] **Step 1: Baseline** — `SOTC_UROOT=…/uroot ./scripts/re0-acceptance.sh` (~8 min, background it). Use the documented dot-free project path workaround for headless runs.
- [ ] **Step 2: Write `FindSampler.java`** — decompile `FUN_0066fe60`, its callers (who consumes the re-based fields), and the functions at the candidate's sampling sites; deterministic, sorted output; addresses in every heading.
- [ ] **Step 3: Run headless** — `$GHIDRA_HOME/support/analyzeHeadless <dot-free project path> sotc -process eboot.elf -noanalysis -scriptPath tools/re/ghidra -postScript FindSampler.java > out/re/ghidra-sampler.log 2>&1`.
- [ ] **Step 4: Read + correlate** — map the decompilation onto §3's structure map and the B2 candidate: does a field pair with the key count? does the time base explain the missing time fields (e.g. implicit 1/30 s)? is the candidate's quaternion reading confirmed or contradicted? Record the conclusion with addresses.
- [ ] **Step 5: Write notes §5 + commit**

```bash
git add tools/re/ghidra/FindSampler.java docs/notes/re-anb.md
git commit -m "feat(re): locate the keyframe sampler in eboot (FindSampler)"
```

---

### Task C4: Consolidate — gate decision + the comparison artifact

**Files:**
- Modify: `docs/notes/re-capture.md` (§4 gate decision + the comparison)
- Outputs (gitignored): `out/re/capture/compare/` (the reference stills + our app's matching renders + `README.md` pairing them)

**Interfaces:**
- Consumes: C1–C3 outputs; the app's `--screenshot`/`--anim-time 0` render path (S2, on `main`).
- Produces: the verbatim decision — **MET** (the decode is confirmed; S2's converter plan (Plan C) proceeds; the stills comparison then serves the sign-off) or **NOT MET** (the decode stays open; the reference stills stand as the §5 artifact; S2's sign-off stays open; the stand-in rule applies) — plus the labeled side-by-side set (never presented as product data).

- [ ] **Step 1: Render the app side** — `sotc-viewer --library … --model models/shadow/characters/wander/_cmn/wander.glb --anim-time 0 --screenshot out/re/capture/compare/app-t0.png --frames 3` (and the stand-in at a pinned time if a real clip is not yet in play).
- [ ] **Step 2: Pair** — copy the reference stills beside the app renders into `compare/` with a `README.md` pairing each and stating exactly what is and is not being claimed.
- [ ] **Step 3: Decision + notes §4** — write the gate decision verbatim with its evidence; update `docs/notes/re-anb.md` §5's cross-reference if the sampler changed the picture.
- [ ] **Step 4: Commit**

```bash
git add docs/notes/re-capture.md
git commit -m "docs(re): capture + sampler results — gate decision recorded"
```

---

## Execution notes

- C1 and C3 are independent (port build vs Ghidra) and can run in parallel; C2 waits for C1's BOOT; C4 waits for C1–C3.
- The port build and the Ghidra baseline are both long (~minutes) — run them as background commands inside their tasks.
- If C1 is NO-BOOT, C2 is skipped and C4 records it; the shadPS4 avenue (D-S9) is a follow-up decision for the user, not a silent substitution.
- A cracked decode (C3) does not get consumed here — it hands off to Plan C (the converter), which is written when this plan's gate says MET.
