# RE-0 — Reverse-Engineering Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A reproducible RE baseline: `eboot.bin` unwrapped to `eboot.elf` (via the user's port's `prepare.py`), imported and auto-analyzed into a Ghidra project, with results and conventions recorded in committed notes.

**Architecture:** One re-runnable script (`scripts/re0-acceptance.sh`) wires the pipeline: preflight checks → SELF→ELF unwrap with the port's `scripts/prepare.py` (read-only use) → verification of the artifacts → headless Ghidra import/analysis through the portable Ghidra 12.1.4 bundle with a committed Java summary script → a stable `summary` line the script asserts on. All derived artifacts live under `out/re/` (gitignored); the notes and scripts are committed.

**Tech Stack:** Python 3 (`prepare.py`, the port's, unchanged) · Ghidra 12.1.4 portable bundle (`~/code/mine/ghidra-bundle`) · `analyzeHeadless` + JDK (present: Temurin 21 / Java 27) · Ghidra-MCP bridge (`bridge-mcp-ghidra`, port 8089) for interactive follow-up · REA 6.3.0 (bundle, optional).

**Spec:** `docs/superpowers/specs/2026-10-16-sdl-rebuild-design.md` (RE-0 in §5; D-S6, D-S9).

## Global Constraints

- Inputs are **read-only**: `SOTC_UROOT` (default `../SOTC-extract/extracted/uroot`) and the port repo `SOTC_PC_PORT` (default `~/code/mine/ShadowOfTheColossus-PC`). Do not modify either; do not modify the Ghidra bundle.
- Ghidra home: `GHIDRA_HOME` (default `~/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC`); headless entry is `$GHIDRA_HOME/support/analyzeHeadless`. Interactive work goes through the bundle's Ghidra-MCP bridge.
- Derived artifacts live under `out/re/` (gitignored). Committed: `scripts/re0-acceptance.sh`, `tools/re/ghidra/PrintSummary.java`, `docs/notes/re-toolchain.md`.
- Scripts stay bash 3.2-compatible (macOS default), `set -euo pipefail`, and fail loudly with a one-line cause.
- The unwrapped `eboot.elf` is a probe reconstruction, **not a byte-exact ELF** (prepare.py reports unavailable non-loadable metadata headers) — notes must say so.
- Personal use only; nothing from the dump or the port is committed.

## Review Focus

Failure modes the spec implies but the happy-path does not exercise; each gets its check in the task noted.

1. Non-SELF input (junk/encrypted dump): the pipeline must fail loudly with the tool's message, never produce a green run. → Task 1.
2. Truncated/failed import: Ghidra must import `eboot.elf`; a project with a near-zero function count is a failure (floor asserted). → Task 2.
3. Re-runs are deterministic: the script rebuilds the Ghidra project from scratch every run and the summary line is stable across runs. → Task 2.
4. Missing toolchain (no `GHIDRA_HOME`, no JDK, missing `prepare.py`): preflight explains exactly what is missing and exits ≠ 0. → Task 1.
5. Dump drift: the script prints the `eboot.bin` SHA-256 (and prepare.py records it in `analysis.json`); notes pin it, so a different dump is visible. → Tasks 1–3.

---

### Task 1: Acceptance script, part 1 — preflight + SELF→ELF unwrap + negative check

**Files:**
- Create: `scripts/re0-acceptance.sh`

**Interfaces:**
- Consumes: the port's `$SOTC_PC_PORT/scripts/prepare.py` (args: `<game-dir> --out <dir>`; writes `eboot.elf`, `analysis.json`, `boot.bin`, `entry.bin`).
- Produces: `out/re/eboot.elf`, `out/re/analysis.json`; script exits 0 on success, ≠ 0 with a one-line cause otherwise.

- [ ] **Step 1: Write the script's preflight and unwrap sections**

Defaults and checks (bash 3.2-safe):

```bash
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${SOTC_UROOT:=$repo_root/../SOTC-extract/extracted/uroot}"
: "${SOTC_PC_PORT:=$HOME/code/mine/ShadowOfTheColossus-PC}"
: "${GHIDRA_HOME:=$HOME/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC}"
out="$repo_root/out/re"

# preflight (each failure prints what is missing and exits 1):
#   $SOTC_UROOT/eboot.bin exists; $SOTC_PC_PORT/scripts/prepare.py exists
#   python3, java available; $GHIDRA_HOME/support/analyzeHeadless is executable

rm -rf "$out"
python3 "$SOTC_PC_PORT/scripts/prepare.py" "$SOTC_UROOT" --out "$out"   # prints TITLE | entry | image + import counts
```

- [ ] **Step 2: Add artifact verification + digest output**

- `eboot.elf`: first 4 bytes are `\x7fELF` (`head -c4 | od -An -tx1`), size ≥ 1 MiB.
- `analysis.json`: valid JSON (`python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$out/analysis.json"`).
- Print `shasum -a 256 "$SOTC_UROOT/eboot.bin"` and note it must match the line for `eboot.bin` in `../SOTC-extract/logs/uroot.sha256` (cross-check the extraction manifest; a mismatch is dump drift — fail).

- [ ] **Step 3: Add the negative check (Review Focus #1)**

```bash
tmp="$(mktemp -d)"
printf 'not a self' > "$tmp/eboot.bin"
if python3 "$SOTC_PC_PORT/scripts/prepare.py" "$tmp" --out "$tmp/out" 2>"$tmp/err"; then
  echo "re0-acceptance: expected failure on junk input" >&2; exit 1
fi
grep -q 'expected PS4 SELF' "$tmp/err" || { cat "$tmp/err" >&2; exit 1; }
rm -rf "$tmp"
```

- [ ] **Step 4: Run it**

Run: `./scripts/re0-acceptance.sh`
Expected: exit 0; `out/re/eboot.elf` + `analysis.json` exist; the junk-input check passes silently. Runtime: seconds.

- [ ] **Step 5: Commit**

```bash
git add scripts/re0-acceptance.sh
git commit -m "feat: re0 acceptance script — SELF unwrap via the port's prepare.py + checks"
```

---

### Task 2: Acceptance script, part 2 — headless Ghidra baseline

**Files:**
- Create: `tools/re/ghidra/PrintSummary.java`
- Modify: `scripts/re0-acceptance.sh` (append the Ghidra stage)

**Interfaces:**
- Consumes: `out/re/eboot.elf` (Task 1).
- Produces: `out/re/ghidra/` (Ghidra project, superset of name `sotc`), `out/re/ghidra.log`; the script asserts a `summary program=… functions=…` line with `functions >= 1000`.

- [ ] **Step 1: Write `tools/re/ghidra/PrintSummary.java`**

```java
// RE-0 acceptance summary: one stable line the shell script can assert on.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class PrintSummary extends GhidraScript {
    @Override
    public void run() throws Exception {
        long functions = 0;
        var it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
            Function f = it.next();
            if (!f.isExternal()) { functions++; }
        }
        println("summary program=" + currentProgram.getName()
            + " language=" + currentProgram.getLanguageID()
            + " functions=" + functions
            + " entry=" + currentProgram.getEntryPoint()
            + " imageBase=" + currentProgram.getImageBase());
    }
}
```

- [ ] **Step 2: Append the Ghidra stage to `scripts/re0-acceptance.sh`**

```bash
rm -rf "$out/ghidra" "$out/ghidra.log"                     # deterministic re-runs (Review Focus #3)
"$GHIDRA_HOME/support/analyzeHeadless" "$out/ghidra" sotc \
  -import "$out/eboot.elf" \
  -scriptPath "$repo_root/tools/re/ghidra" \
  -postScript PrintSummary.java \
  -analysisTimeoutPerFile 3600 > "$out/ghidra.log" 2>&1
```

then: `grep -m1 '^summary ' "$out/ghidra.log"` (print it), parse `functions=`, and fail when it is `< 1000` with a tail of the log (Review Focus #2). Also `grep -c 'ERROR' ghidra.log` → print count (informational; do not fail on it alone).

- [ ] **Step 3: Add the summary printer for humans**

The script ends by printing: the prepare.py output (entry / image bytes / imports / relocations), the Ghidra summary line, the `eboot.bin` SHA-256, artifact paths, and total elapsed. This block is what the notes in Task 3 quote.

- [ ] **Step 4: Run it**

Run: `./scripts/re0-acceptance.sh`
Expected: exit 0; `summary … functions=N` with N ≥ 1000 (analysis of a game eboot takes several minutes — the log is `out/re/ghidra.log`). Re-run once: the summary's `functions=` is identical (determinism).

- [ ] **Step 5: Commit**

```bash
git add tools/re/ghidra/PrintSummary.java scripts/re0-acceptance.sh
git commit -m "feat: re0 headless Ghidra baseline (import + PrintSummary) in the acceptance script"
```

---

### Task 3: Notes, MCP verification, final acceptance

**Files:**
- Create: `docs/notes/re-toolchain.md`
- Modify: `scripts/re0-acceptance.sh` (only if Step 2 surfaces a defect)

- [ ] **Step 1: Write `docs/notes/re-toolchain.md`**

Contents (measured values quoted verbatim from the Task 2 run):

- Tool inventory + versions: Ghidra 12.1.4 (portable bundle path), JDK, `prepare.py` source path + its SHA-256, `eboot.bin` SHA-256 (matches `SOTC-extract/logs/uroot.sha256`).
- The exact commands (script path, analyzeHeadless invocation) and where artifacts live (`out/re/`).
- Results: entry point, image bytes, import/symbol counts, relocation counts, required modules, Ghidra `functions=` count, `ERROR` grep count.
- Caveats: `eboot.elf` is a probe reconstruction, not byte-exact (unavailable non-loadable metadata headers listed by prepare.py); uroot has no `dvdroot_ps4`, so prepare.py's resource inventory is empty for this extraction.
- **RE conventions for later targets:** RE notes are `docs/notes/re-<target>.md` (RE-1 `.anb`, RE-2 foliage, RE-3 composition, RE-4 behavior); findings that change a converter are proposals against the spec, not app-side parsing.
- **Interactive MCP workflow:** launch the bundle (`make -C ~/code/mine/ghidra-bundle run`), open the `sotc` project; from OpenCode the Ghidra-MCP bridge then exposes the instance (`list_instances` → `connect_instance` → `get_current_program_info` reports `eboot.elf`). Record the verified instance/program names. Note that REA 6.3.0 (bundle) can analyze through the same Ghidra without the GUI.

- [ ] **Step 2: Verify the interactive MCP path**

With a GUI session: start the bundle, open the project, then via the Ghidra-MCP tools verify `list_instances` shows it and `get_current_program_info` reports `eboot.elf` (imported + analyzed). Record the exact instance/project names in the notes. If no GUI session is available, record the flow as "to be verified" explicitly — do not claim it.

- [ ] **Step 3: Final acceptance run + commit**

Run: `./scripts/re0-acceptance.sh` (exit 0), then:

```bash
git add docs/notes/re-toolchain.md scripts/re0-acceptance.sh
git commit -m "docs: RE-0 results — toolchain notes (eboot.elf + Ghidra baseline)"
```

---

## Self-review notes (author)

- Spec coverage: RE-0's deliverable line ("loadable ELF, named functions baseline, `docs/notes/re-toolchain.md`") maps to Tasks 1/2/3; D-S9's `prepare.py` reuse is Task 1; the MCP tooling (D-S6) is Task 3.
- Type consistency: artifact paths (`out/re/eboot.elf`, `out/re/analysis.json`, `out/re/ghidra/`, `out/re/ghidra.log`) and the `summary …` line format are identical across tasks.
- Review Focus lines each carry their check (Tasks 1 ×2, Task 2 ×2, Task 3 + Task 1 digest for drift).
