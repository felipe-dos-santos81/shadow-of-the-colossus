# RE-1 — animation container decode (`.anb` / `.cani` / `.skb`) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decode the game's animation container family — `.skb` skeleton banks, `.anb` actor clips (first target: `player_a_stand.anb`), `.cani` object clips — from data mining plus a Ghidra front on the Bluepoint animation runtime in `eboot.elf`, ending in `docs/notes/re-anb.md` with the format spec **and a verified extraction of `player_a_stand`'s joint curves** — the gate for S2's converter (Plan C) or for the capture-derived fallback.

**Architecture:** A committed Python probe (`tools/re/anb/probe.py`) dumps container structure and, at the gate, extracts per-joint curves from a clip into `out/re/anb/`; a headless-Ghidra pass (committed Java script) on the RE-0 baseline locates the loader/runtime in `eboot.elf`; the notes consolidate both fronts. Inputs are read-only; derived artifacts live under `out/re/anb/` (gitignored).

**Tech Stack:** Python 3 (probe) · Ghidra 12.1.4 headless via the portable bundle (`~/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC`) + Java scripts · `scripts/re0-acceptance.sh` for the Ghidra baseline · the vendored library's Oodle `CompressionHandler` (documented path for compressed variants; out of the probe's scope).

**Spec:** `docs/superpowers/specs/2026-10-18-first-playable-design.md` §3 (spike outcome: the vendored `CANI` parser does not fit — 0/900; decode from mapped structure + the Ghidra lever) + the inherited `docs/superpowers/specs/2026-10-16-sdl-rebuild-design.md` §5 (RE-1: "data mining first; Ghidra as needed"; expected output: format spec + offline converter — the converter half is Plan C, gated on this plan's result).

## Global Constraints

- Inputs are **read-only**: the game tree (`/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game`), the port repo, and the Ghidra bundle. Nothing under them is modified.
- Derived artifacts live under `out/re/anb/` and `out/re/ghidra-anim.log` (gitignored). Committed: `tools/re/anb/probe.py`, `tools/re/ghidra/FindAnim.java`, `docs/notes/re-anb.md`.
- Python 3 (`python3` on PATH); scripts stay simple, fail loudly with a one-line cause, and exit non-zero on assertion failure.
- Ghidra home default `~/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC`; headless entry `support/analyzeHeadless`; the RE-0 project lives at `out/re/ghidra` (project `sotc`, program `eboot.elf`).
- RE notes are `docs/notes/re-<target>.md`; findings that would change a converter are proposals against the spec, never app-side parsing.
- Personal use only; nothing from the dump is committed.

## Review Focus

Failure modes this decode will meet that the happy path does not exercise; each gets its handling in the task noted.

1. The 305 of 4,455 `.anb` files with `0x0C == 0` (an odd variant) — classified by the probe, never a crash. → B1/B2.
2. Files with high-bit garbage (the ~16/300 class that overflows naive readers) — detected and reported, never a crash. → B1/B2.
3. Oodle-compressed `.cani`/`.skb` — classified as compressed (the probe reports; the decompression path — the .NET `CompressionHandler` — is documented, not re-implemented). → B1.
4. Clip ↔ skeleton pairing: clips carry no skeleton id; the binding is by naming (`player_a_*` ↔ `player_a.skb`) and the joint count must cross-check against the skeleton bank (348 for player). The notes state this rule explicitly. → B1/B2.
5. Non-character banks (`camera_*` 453, `objanim_*` 1,008) — classified; out of S2's scope. → B1.

---

### Task B1: Container family + skeleton bank (`.skb`) + the probe

**Files:**
- Create: `tools/re/anb/probe.py`
- Create: `docs/notes/re-anb.md` (§1 container family, §2 skeleton bank)

**Interfaces:**
- Consumes: the game tree (read-only); the spike's recorded facts (this plan's context).
- Produces:
  - `python3 tools/re/anb/probe.py skb <file> [--json <out>]` — structure dump of a `.skb` (header fields, tables, name blob) as text + JSON.
  - `python3 tools/re/anb/probe.py classify <file>...` — one line per file: container magic, size field, `0x0C` value, compressed/plaintext, classified bank (`skb`/`anb`/`cani`/other).
  - `python3 tools/re/anb/probe.py check-skb` — pinned assertions for `cp3shadow/skel/player_a.skb` (exit 0/1).

**Starting facts (from the 2026-10-18 spike; re-verify, don't trust):** `.anb`/`.skb` share the plaintext `xff\0` magic (`78 66 66 00`); `.anb` has file size at `0x14` and a u32 offset table at the tail; `.cani` is often Oodle-compressed (`8C0A…` blocks) and carries fps `30.0` (float @ `0x08`), a u16 joint count (@ `0x0E`, matching the sibling `.cskl` 5/5), offset tables, and a 12-byte `MINA` ("ANIM") v9 footer; `player_a.skb` (5,020 B) holds plaintext bone names (`Bone_root`, `Bone_KOSI`, `Bone_MOMO_L`, …).

- [ ] **Step 1: Write the probe's `classify` + `skb` modes**

Container header dump first (magic, size field, `0x0C`, table offsets), then the `.skb` walk: locate the plaintext name blob, extract names (NUL- or length-prefixed — determine from the data), count them. `check-skb` asserts: 348 names; first name `Bone_root`; `Bone_KOSI` and `Bone_MOMO_L` present; the name **set** equals wander.glb's joint node names (read via a small Python glb-JSON reader inside the check).

- [ ] **Step 2: Run and verify**

Run: `python3 tools/re/anb/probe.py check-skb` (from the repo root; game tree path from `SOTC_GAME_ROOT` or the default absolute path — fail loudly when missing).
Expected: exit 0 with the pinned counts printed; if the name count or set differs, the probe reports the delta (that delta is itself a finding for the notes).

- [ ] **Step 3: Classify the family**

Run: `python3 tools/re/anb/probe.py classify cp3shadow/anim/*.anb` (a bounded subset: the 305 `0x0C==0` files + 20 others + all `cp3shadow/skel/*.skb` + a sample of `.cani`).
Expected: every file classified without a crash; the `0x0C==0` variant, the high-bit-garbage class, and compressed `.cani` each appear as their own classification.

- [ ] **Step 4: Write notes §1–§2**

`docs/notes/re-anb.md`: §1 container family (the `xff` header fields with measured values, the tail offset table, the `MINA` v9 footer, compression markers, the bank taxonomy); §2 the `.skb` structure (measured) + the **linkage rule** (naming + joint-count cross-check; Review Focus 4) + the `.weightinfo` sibling observation (one line).

- [ ] **Step 5: Commit**

```bash
git add tools/re/anb/probe.py docs/notes/re-anb.md
git commit -m "feat(re): anb container probe + skeleton bank notes"
```

---

### Task B2: `.anb` structure decode + curve extraction (the gate)

**Files:**
- Modify: `tools/re/anb/probe.py` (`anb` mode + `--curves` + `check-curves`)
- Modify: `docs/notes/re-anb.md` (§3 actor clips)

**Interfaces:**
- Produces:
  - `python3 tools/re/anb/probe.py anb <file> [--json <out>]` — full structure dump (header, tables, tagged records, tail table).
  - `python3 tools/re/anb/probe.py anb <file> --curves <out.json>` — per-joint curves: `{"clip": …, "fps": …, "joints": [{"index": n, "times": [...], "rotations": [[x,y,z,w], …], "translations": [[x,y,z], …]}], …}`.
  - `python3 tools/re/anb/probe.py check-curves` — the gate's pinned assertions (exit 0/1).

- [ ] **Step 1: Dump and map**

Dump `player_a_stand.anb` (and one more, `horse_a_add_horse_kick.anb`, as a second sample) to `out/re/anb/*.structure.txt`. Build the structure map: header fields, the `0x20…` tagged records, the tail offset table — cross-referenced against the `.cani` map (fps/joins/tables) and the spike's notes (the repeated `dc00 XXXX` pairs pattern: a small value + a constant base offset). Write the hypothesis table into the notes as you go; every hypothesis gets a checkable prediction.

- [ ] **Step 2: Identify keyframe records and extract**

Extend the probe to walk the identified records and emit curves. Extraction is complete when `--curves` produces, for every joint the clip animates, a strictly increasing `times` array and unit quaternions. Sanity checks the probe enforces: quaternion norms within 1e-3 of 1; no NaNs; adjacent-key quaternion dot > 0.9 (continuity); timing consistent with 30 fps (the `.cani`-family frame rate; state the inference and its evidence).

- [ ] **Step 3: Run the gate assertions**

Run: `python3 tools/re/anb/probe.py check-curves`
Expected (the gate): `player_a_stand`'s joint count cross-checks with `player_a.skb` (348, or the real count if the clip animates a subset — pin whatever the data shows); the idle's motion is idle-scale (pin the measured per-joint deltas; no jumps above 15°/frame); all sanity checks pass.

- [ ] **Step 4: Write notes §3**

Structure (measured), the record layout, the timing model, the extraction recipe, and the pinned check outputs — verbatim. Negative results from failed hypotheses are recorded too (they are results).

- [ ] **Step 5: Commit**

```bash
git add tools/re/anb/probe.py docs/notes/re-anb.md
git commit -m "feat(re): anb curve extraction — player_a_stand decoded"
```

---

### Task B3: Ghidra front — the animation runtime in `eboot.elf`

**Files:**
- Create: `tools/re/ghidra/FindAnim.java`
- Modify: `docs/notes/re-anb.md` (§4 Ghidra findings)

**Interfaces:**
- Consumes: the RE-0 baseline (`out/re/ghidra`, project `sotc`, program `eboot.elf`).
- Produces: `tools/re/ghidra/FindAnim.java` — a GhidraScript that scans for candidate constants and strings and prints, for each hit, the containing function and its address; `out/re/ghidra-anim.log`.

- [ ] **Step 1: Baseline (if missing)**

Run: `./scripts/re0-acceptance.sh` (from the repo root; in the canonical checkout the default `SOTC_UROOT` resolves — no export needed). Expected: exit 0, `out/re/ghidra` present (~8 min).

- [ ] **Step 2: Write `FindAnim.java`**

Scan program memory for: the `xff` magic as a u32 (`0x78666600` and byte-order variant `0x00666678`); the Oodle block marker `8C0A`; ASCII strings `anb`, `.anb`, `skb`, `MINA`, `ANIM`, `ANIMDATA` (case-insensitive, ≥ 3 chars); and any string naming the anim directory (`cp3shadow`, `anim`). For each hit: address, the containing function, and a one-line context. Output is deterministic (sorted).

- [ ] **Step 3: Run headless**

Run: `$GHIDRA_HOME/support/analyzeHeadless out/re/ghidra sotc -process eboot.elf -noanalysis -scriptPath tools/re/ghidra -postScript FindAnim.java > out/re/ghidra-anim.log 2>&1`
Expected: the log lists candidates (or a documented empty result — a negative is acceptable for this pass).

- [ ] **Step 4: Decompile the top candidates**

Follow the strongest candidates into the loading path (a second script mode dumping decompilation, or the interactive Ghidra-MCP / REA tooling alongside — the reproducible record is the committed script + log). Document what the runtime does with the container: where the `xff` magic is checked, what the record layouts mean (feed back into B2's hypotheses), where keyframes are sampled.

- [ ] **Step 5: Write notes §4 + commit**

```bash
git add tools/re/ghidra/FindAnim.java docs/notes/re-anb.md
git commit -m "feat(re): locate the animation runtime in eboot (FindAnim)"
```

---

### Task B4: Consolidate notes + the gate decision

**Files:**
- Modify: `docs/notes/re-anb.md` (§5 conventions + gate decision; final pass over all sections)

**Interfaces:**
- Consumes: B1–B3 artifacts.
- Produces: the final `docs/notes/re-anb.md` with the gate decision recorded.

- [ ] **Step 1: Final pass**

Every quoted value verbatim from the committed probe/scripts and their outputs; §5: conventions for later targets (`docs/notes/re-<target>.md`; findings → converter proposals) and the **gate decision**, verbatim one of:
- **MET** — curves extracted and verified (pin the numbers) → the S2 converter plan (Plan C) proceeds; the pipeline is the normal one.
- **NOT MET** — the decode did not reach verified extraction → the capture-derived path activates (its plan is written against the port rig); decoding continues as background work.

- [ ] **Step 2: Final acceptance**

Run: `python3 tools/re/anb/probe.py check-skb && python3 tools/re/anb/probe.py check-curves` (check-curves only when the gate is MET).
Expected: exit 0 from the committed state.

- [ ] **Step 3: Commit**

```bash
git add docs/notes/re-anb.md
git commit -m "docs: RE-1 results — animation container decoded (gate: <MET|NOT MET>)"
```
