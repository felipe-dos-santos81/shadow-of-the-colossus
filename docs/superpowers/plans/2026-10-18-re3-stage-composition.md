# RE-3 stage composition (W2) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** S3's data half — the Valus arena's composition from data: a committed measured spec (`docs/notes/re-stage.md`) and the arena manifest (`out/re/stage/valus.manifest`) that the app (W1) consumes.

**Architecture:** A committed Python probe mines the composition families (`data/fs/stage/*.fs`, `workspaces/_ps4/shadow.cwks`, `path/*.path`, `camdata/*`) from measured structure; the manifest's instance set comes from the converted library's already-world-positioned arena sets (cross-checked against the workspace's arena path list), with the spawn's provenance recorded honestly. A bounded Ghidra pass (spec §5) is the allowed escalation only if reference resolution stalls — the milestone does not require it.

**Tech Stack:** Python 3 (stdlib only) · the game tree (read-only) · the converted library at `out/library` · Ghidra 12.1.4 headless only if escalated.

**Spec:** `docs/superpowers/specs/2026-10-18-s3-arena-design.md` — workstream W2, §5 · `docs/superpowers/specs/2026-10-16-sdl-rebuild-design.md` §5 (RE-3 row).

## Global Constraints

- Inputs read-only: the game tree and the port repo; derived artifacts under `out/re/stage/` (gitignored); committed: `tools/re/stage/probe.py`, `docs/notes/re-stage.md`.
- The probe: stdlib-only, fail loudly with a one-line cause, exit non-zero on assertion failure; measured values quoted verbatim in the notes; hypotheses never stated as facts.
- **The manifest format is the W1↔W2 interface** (pinned identically in the W1 plan): line-oriented, `#` comments, blank lines skipped; `instance <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz>`; `spawn <px> <py> <pz> <yawDeg>`; numbers are strict full-string finite floats. Output path: `out/re/stage/valus.manifest`. The committed schema doc (`docs/notes/scene-manifest.md`) is W1's artifact; this plan conforms to it verbatim.
- Small typed commits; nothing from the dump is committed.

## Review Focus

1. The unresolved hash namespace stays honestly unresolved (measured near-misses listed; never a guessed mapping).
2. A manifest naming a glb that does not exist in `out/library` → the probe fails loudly listing it.
3. The spawn's provenance is labeled (data-derived vs a builder pick) — the done-line's "from data" claim must not be overstated.
4. The notes quote measured values verbatim (counts, offsets, magic bytes) — hypotheses are marked as such.
5. Re-running the probe on the committed state reproduces every pinned number (`check-*` exit 0).

---

### Task W2.1: Stage mining probe — the `.fs` family + the inventory

**Files:**
- Create: `tools/re/stage/probe.py`
- Create: `docs/notes/re-stage.md` (§1)
- Outputs (gitignored): `out/re/stage/*.dump.txt`

**Interfaces:**
- Produces:
  - `python3 tools/re/stage/probe.py fs <file> [--json <out>]` — dump: `u32 n @0x00`, then `n × (u32 v, u32 hash32)` at `0x04+8i`; prints entries + the distinct-v histogram.
  - `python3 tools/re/stage/probe.py classify` — walk the families and print one line per group: `.fs` (1,896 files, size range), `stagetexseg_def/*.fs` (112, `65…` names), `workspaces/*.cwks` (1), `path/*.path` (144), `camdata/*.csv` (20).
  - `python3 tools/re/stage/probe.py check-fs` — pinned assertions (exit 0/1): every one of the 1,896 `.fs` files satisfies `size == 4 + 8·n`; `1010.fs` has `n == 50` and its first three entries verbatim; `874.fs` has `n == 2`.
- Starting facts (re-verify, don't trust): the measured values in this plan's context came from a 2026-10-18 recon — the `4+8n` structure verified on all 1,896 files; `v ∈ [1,257]`, 202 distinct; 17,686 distinct hashes; `1010.fs` = 404 B, first entries `(0x95, 0x4fbe65ee)`, `(0x94, 0x03bf0117)`.

- [ ] **Step 1: Write `fs` + `classify` modes.**
- [ ] **Step 2: Run `check-fs`** — exit 0 with the pinned counts; any mismatch is a finding for the notes (re-measure, don't force).
- [ ] **Step 3: Write notes §1** — the measured `.fs` spec (byte-exact), the inventory table, and the **hash-namespace open item**: list the conventions tested with 0 hits (FNV-1a, CRC32, FNV-1, Murmur3, xxh32, djb2, sdbm, java31 — over name/path/case/ext variants) and the reusable near-miss (the vendored CTAL convention: FNV-1a-32 of the UPPERCASE name — matches CTAL, not these hashes).
- [ ] **Step 4: Commit**

```bash
git add tools/re/stage/probe.py docs/notes/re-stage.md
git commit -m "feat(re): stage fs family probe + measured spec"
```

---

### Task W2.2: The workspace string pool — the arena's path list

**Files:**
- Modify: `tools/re/stage/probe.py` (add `cwks` + `check-cwks`)
- Modify: `docs/notes/re-stage.md` (§2)

**Interfaces:**
- Produces:
  - `python3 tools/re/stage/probe.py cwks <file> [--json <out>]` — parse: magic `PSKW`, `u32 6 @0x04`, 16-B index records from `0x08` (pattern `(u32 count, u32 dataOff, u32 size, u32 nameOff)` — characterize as far as measured), string pool from `0x105E60`: strings as `[u8 tag][0x24]['/…path'][NUL]`; dump counts + a sample; extract every string matching `models/shadow/levels/arena_01`.
  - `check-cwks` — pinned: magic + `u32 6`; pool start `0x105E60`; ~408,308 strings (pin the measured count); exactly 142 `models/shadow/levels/arena_01/` paths (pin the list's sha256); the tail's 13 stage paths verbatim.
- Starting facts: `workspaces/_ps4/shadow.cwks` = 29,398,788 B; pool start `0x105E60` (1,072,736); tail at ≈`0x1C07318` lists `132391,136811,136867,144144,145969,316283,317197,346875,350578,908.fs` + `stagetexseg_def/{657159,659959,660533}.fs`.

- [ ] **Step 1: Implement `cwks` + `check-cwks`** (bounded reads; fail loudly on structure mismatch).
- [ ] **Step 2: Run `check-cwks`** — exit 0; the arena path list saved to `out/re/stage/arena_paths.txt`.
- [ ] **Step 3: Write notes §2** — the measured PSKW structure, the pool format, the arena path list verbatim, and the record-semantics open item.
- [ ] **Step 4: Commit**

```bash
git add tools/re/stage/probe.py docs/notes/re-stage.md
git commit -m "feat(re): workspace string pool + the arena path list"
```

---

### Task W2.3: Emit the arena manifest + the composition notes

**Files:**
- Modify: `tools/re/stage/probe.py` (add `compose` + `check-manifest`)
- Modify: `docs/notes/re-stage.md` (§3 composition + §4 the escalation decision)
- Output (gitignored): `out/re/stage/valus.manifest`

**Interfaces:**
- Produces:
  - `python3 tools/re/stage/probe.py compose --library <out/library> --out out/re/stage/valus.manifest` — instance set = every non-`_collision` `.glb` under `models/shadow/levels/arena_01/` and `models/shadow/levels/arena_01_entrance/` (the set cross-checked against W2.2's path list); transforms = identity (the glbs are already world-positioned — record the bounds evidence: arena_01 `x[237,431] y[28,70] z[298,390]`, entrance `x[259,345] y[−1.7,81.5] z[249,307]`, sets meeting near `z≈300`); spawn = a point inside the entrance set's bounds at its ground height, **labeled as a builder pick** until the `.path`/`camdata` mining resolves a data-derived one (record the candidates: `path/pathdata/f4_small.path` first float4 `(−0.307, 10.457, 20.034, 1.0)` — in a space that does not match the arena bounds; camdata's stage numbers 16/55/63 = MINO A/B/C are the Valus candidates).
  - `check-manifest` — pinned: parses; 35 instances (13 + 22); every `glbRelPath` exists under the library; the spawn lies inside the entrance bounds; identity transforms on every instance.
- Manifest format: exactly the Global Constraints' line-oriented schema (comments allowed; emit a provenance header comment).

- [ ] **Step 1: Implement `compose` + `check-manifest`.**
- [ ] **Step 2: Run + verify** — `check-manifest` exit 0; the manifest loads by eye (one instance per line).
- [ ] **Step 3: Write notes §3** (the composition: what is data-derived vs the labeled spawn pick; the bounds evidence) **+ §4** (the escalation decision: the Ghidra pass was **not triggered** — reference resolution for the *instance set* came from the workspace pool + library; the hash namespace and stage↔set mapping remain open and are recorded for later targets; if a future milestone needs them, the bounded Ghidra pass on the stage/workspace loader is the escalation).
- [ ] **Step 4: Commit**

```bash
git add tools/re/stage/probe.py docs/notes/re-stage.md
git commit -m "feat(re): arena manifest emitted + composition notes (spawn pick labeled)"
```

---

## Execution notes

- W2 runs independently of W1/W3; W1's last task waits for this plan's `out/re/stage/valus.manifest`.
- The manifest is regenerable at any time (`compose`); it is gitignored, never committed (derived from the game tree).
- The full `.fs`/stage-id resolution (the hash namespace) is explicitly **not** on S3's critical path — it is recorded open work; escalating to Ghidra requires the stall condition in spec §5, which did not occur.
