# RE-0 toolchain notes — `eboot.elf` + Ghidra baseline (2026-10-17)

RE-0 (spec §5, D-S6/D-S9): **"Tool-up: unwrap `eboot.bin` SELF→ELF; executable inventory; Ghidra
project + MCP wiring; REA inventory of the payloads"** — deliverable **"loadable ELF, named
functions baseline, `docs/notes/re-toolchain.md`"** (this file). One re-runnable script drives it
all, `scripts/re0-acceptance.sh`; every value below is quoted from its runs and the artifacts it
writes under `out/re/` (gitignored). Inputs stay read-only; personal use only — the committed
tooling (script + Java) and these notes carry no game content: hashes, sizes, counts, and
identifiers only.

| | |
|---|---|
| Date | 2026-10-17 (local −03; run stamps are UTC) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64 |
| Repo / branch | `sotc-native`, `s1-sdl-re0`; RE-0 code `121035b` (script) + `2eb798d` (Ghidra stage); this notes file is the RE-0 close-out commit |
| Input tree | `SOTC_UROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/uroot` (E1) |
| Acceptance script | **`scripts/re0-acceptance.sh`** — preflight → stage + unwrap → verify → negative check → headless Ghidra baseline |
| Run log (gitignored) | `out/re-acceptance-run.log` |

## 1 · Tool inventory

| tool | version / path | role |
|---|---|---|
| Ghidra | **12.1.4**, portable bundle `~/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC` (`Ghidra/application.properties`: `application.version=12.1.4`) | headless import + auto-analysis (`support/analyzeHeadless`) |
| JDK | bundle run: **Temurin 21.0.12.1** (`openjdk version "21.0.12.1" 2026-08-18 LTS`, first line of `out/re/ghidra.log`); PATH `java` is 27 (preflight checks existence only) | runs `analyzeHeadless` + the Java script |
| `prepare.py` (port, read-only use) | `/Users/felipe.dos.santos/code/mine/ShadowOfTheColossus-PC/scripts/prepare.py`, 13,962 B, sha256 `9459d439056e9ca61758ad8f3023f0c1e46b085dd1524644446af00374856890` | SELF→ELF unwrap + `analysis.json` |
| Ghidra-MCP bridge | bundle venv `bridge-mcp-ghidra` (port 8089); GUI launch: `make -C ~/code/mine/ghidra-bundle run` | interactive follow-up (§6) |
| REA | **6.3.0** (bundle `.rea/rea`, npm `rea-agents@6.3.0`) | artifact interrogation; analyzes through the same Ghidra without the GUI |

Input: `eboot.bin` 65,314,052 B, sha256
`21982727c6f97fd415b3af5e99c5e071619c4dcc2248787a701a31a45ab0a4e0` — matches the `uroot/eboot.bin`
line of `SOTC-extract/logs/uroot.sha256` (the script fails on any mismatch, so a different dump is
visible — Review Focus #5).

## 2 · Commands + artifacts

The acceptance run (E1; the worktree-relative default does not resolve from here):

```bash
SOTC_UROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/uroot ./scripts/re0-acceptance.sh
```

Steps: preflight (inputs, `python3`, `java`, `analyzeHeadless`, `PrintSummary.java`) → stage
`out/re/uroot` (a prepare.py-consumable view; §4) and unwrap with the port's `prepare.py` → verify
the artifacts + digest cross-check → negative check (junk input must fail with `expected PS4
SELF`; silent on success) → headless Ghidra import + analysis, asserting a `summary … functions=…`
line with a floor of 1,000.

The Ghidra stage's invocation (from the script — wrapped in `if ! …; then` there for the failure
path; `$proj_link_dir` is a dot-free temp symlink to `out/re/ghidra` — §4, R-E6 — and the project
name is `sotc`):

```bash
"$GHIDRA_HOME/support/analyzeHeadless" "$proj_link_dir/ghidra" sotc \
     -import "$out/eboot.elf" \
     -scriptPath "$repo_root/tools/re/ghidra" \
     -postScript PrintSummary.java \
     -analysisTimeoutPerFile 3600 > "$out/ghidra.log" 2>&1
```

Artifacts (all under the gitignored `out/re/`, rebuilt from scratch every run):

| path | what |
|---|---|
| `out/re/eboot.elf` | the unwrapped loadable ELF (65,214,308 B; `\x7fELF` magic + ≥ 1 MiB asserted) |
| `out/re/analysis.json` | prepare.py's recorded inventory (entry, image bytes, headers, imports, relocations, resources) |
| `out/re/boot.bin`, `out/re/entry.bin` | prepare.py's side outputs |
| `out/re/uroot/` | the staged prepare.py-consumable view of the read-only tree (symlinks + the `param.sfo` copy, §4) |
| `out/re/ghidra/` | the Ghidra project `sotc` (`sotc.gpr` + `sotc.rep/`); rebuilt every run |
| `out/re/ghidra.log` | the full headless log (45,970 B / 482 lines) |

Committed tooling: `scripts/re0-acceptance.sh` + `tools/re/ghidra/PrintSummary.java` (its one line:
`summary program=… language=… functions=… entry=… imageBase=…`).

## 3 · Measured results

prepare.py's stdout (verbatim):

```text
SHADOW OF THE COLOSSUS™ | entry=0xf120 | image=60,123,812 bytes
588 imported symbols; 712,333 relocations; 49 required modules
Unavailable non-loadable metadata headers: [10]; not a byte-exact ELF reconstruction
Output: /Users/felipe.dos.santos/.local/share/opencode/worktree/05966d/s1-sdl-re0/out/re
libc _init_env verified RET: True
21982727c6f97fd415b3af5e99c5e071619c4dcc2248787a701a31a45ab0a4e0  /Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/uroot/eboot.bin
```

`analysis.json` (prepare.py's recorded values): `entry: "0xf120"`, `image_bytes: 60123812`,
`source_sha256` = the `eboot.bin` hash above; `import_count: 588`; relocation counts by type
`7: 571, 8: 709,058, 1: 2,704` (total 712,333); 49 `needed` modules; `bundled_modules`:
`libSceFios2.prx`, `libc.prx`; `unavailable_metadata_headers: [10]` (§4); `resources: {}`,
`resource_bytes: 0` (§4); `status: "Prepared only; execution and Vulkan are tested separately."`

Ghidra's summary line (verbatim; headless prefixes it with `INFO  PrintSummary.java> ` and appends
` (GhidraScript)` + two spaces):

```text
INFO  PrintSummary.java> summary program=eboot.elf language=x86:LE:64:default functions=38845 entry=none imageBase=00000000 (GhidraScript)  
```

**`functions=38845`** (non-external; floor 1,000 asserted). **`ERROR` grep count = 185**
(informational only: 182 × `function body repair failed due to overlap with another function`,
3 × `Failed to create function at … since its body contains referring thunk at …` — analyzer
chatter, never a run failure).

Runs (all exit 0; the script `rm -rf`s `out/re` and rebuilds the project every run, so identical
counts are re-run determinism, not a cache):

| run | wall | `functions=` | `ERROR` lines |
|---|---:|---:|---:|
| Task 2, run 1 | 432 s | 38845 | 185 |
| Task 2, run 2 (determinism) | 433 s | 38845 | 185 |
| Task 2, run 3 (final state) | 435 s | 38845 | 185 |
| RE-0 close-out (this file's run) | 452 s | 38845 | 185 |

The close-out run's PASS line (verbatim):

```text
re0-acceptance: PASS — SELF unwrapped via the port prepare.py; ELF + analysis.json verified; digest matches the extraction manifest; junk input rejected; Ghidra baseline functions=38845 >= 1000
```

## 4 · Caveats

1. **`eboot.elf` is a probe reconstruction, not byte-exact.** prepare.py reports
   `Unavailable non-loadable metadata headers: [10]; not a byte-exact ELF reconstruction` and
   records `unavailable_metadata_headers: [10]` in `analysis.json`. The 65,314,052-byte SELF
   yields a 60,123,812-byte image. Use it for analysis, not as a runnable artifact.
2. **Staged uroot + `param.sfo` provenance (R-E4).** The extraction keeps PKG-level metadata in
   `extracted/pkg-meta/`; its `uroot/sce_sys/` holds only `about/` + `keystone`, no `param.sfo`
   (which prepare.py requires). The script stages `out/re/uroot` — symlinks to every top-level and
   `sce_sys/` entry, plus a copy of `pkg-meta/param.sfo` when the tree lacks one — so the read-only
   trees are never written. The staged `param.sfo` sha256 is
   `317b9716ed6c62a8e5fc0f8ccfe9265f7e72b4cb95f8b33ad2332222669b73b3`; it is **not listed in
   `uroot.sha256`** — that manifest records the PkgTool-only extraction. The drift anchor is the
   `eboot.bin` line, which does match.
3. **No `dvdroot_ps4`.** The extracted uroot has no `dvdroot_ps4/`, so prepare.py's resource
   inventory is empty for this extraction (`resources: {}`, `resource_bytes: 0`); resource
   discovery needs the extraction's other outputs or a different pass, not this tree.
4. **`entry=none` in the Ghidra line is the honest report (R-E7).** Ghidra's loader registers no
   external entry point for this probe ELF (`entry=none`, `imageBase=00000000`), while prepare.py
   records the ELF-header entry `0xf120` — which the summary block prints directly above the
   Ghidra line. No consumer parses `entry=` from the Java line.
5. **Dot-prefixed path elements are rejected by Ghidra (R-E6).** `ProjectLocator` refuses any path
   element starting with `.` (no bypass property), and this worktree lives under `~/.local/…` — so
   both headless and GUI/MCP project opens must reach `out/re/ghidra/sotc.gpr` through a dot-free
   alias (the script uses a temp symlink; §6's checklist does the same). Canonical artifacts stay
   at `out/re/ghidra`.
6. **Headless prefixes script output (R-E5).** The log line is
   `INFO  PrintSummary.java> summary … (GhidraScript)` (two trailing spaces), so the script anchors
   on the message body (`grep -m1 'summary program='`), not the line start.
7. **`ERROR` lines are informational.** The floor check and the run's exit are independent of them.

## 5 · RE conventions for later targets

- RE notes are `docs/notes/re-<target>.md` — one per target: RE-1 `.anb` animation containers,
  RE-2 foliage (`virtualgeometry` `.cvmg` + `vs_*` scenes), RE-3 stage composition
  (`stage/*.fs`, `workspaces/*.cwks`, `path/*.path`, `camdata/*`), RE-4 behavior (entity scripts
  `*.cgpr` + movement/camera constants in eboot/PRX functions). (Spec §5 is the source; RE-5
  collision and RE-6 shaders are conditional/deferred rows.)
- Findings that change a converter are **proposals against the spec**, not app-side parsing: the
  app never parses original game containers at runtime, only converted assets (spec §2) — RE
  outputs feed the offline converter pipeline, and anything else is a spec change to propose, not
  a shortcut to take.
- Keep this file's discipline: measured values quoted verbatim from artifacts, inputs read-only,
  derived artifacts under `out/` (gitignored), personal use only.

## 6 · Interactive MCP workflow — **to be verified**

**Status: TO BE VERIFIED — not performed.** As of 2026-10-17 the interactive path cannot be
exercised in this environment: port 8089 is held by an unrelated running Ghidra 12.2_DEV instance
(pid 12094), and the bundle's `make run` refuses to launch while the port is occupied (its own
check fails with `port 8089 is already in use`). The Ghidra-MCP bridge itself is reachable from
OpenCode — `list_instances` responds with that instance (pid 12094, `project: "unknown"`,
`program_count: 0`, `connected: true`) — but it has no open program and the bridge exposes no
project-open tool, so the RE-0 project cannot be attached without the GUI step below. Nothing was
launched; the running instance was not touched. Per the plan: record the flow, do not claim it.

Checklist to verify (free the port → GUI → MCP):

1. Free port 8089 (quit the unrelated Ghidra instance).
2. `make -C ~/code/mine/ghidra-bundle run`.
3. Open the project through a **dot-free alias** of `out/re/ghidra/sotc.gpr` (R-E6; e.g. `ln -s`
   into a temp dir — the GUI checks paths the same way headless does).
4. From OpenCode: `list_instances` → `connect_instance` → `get_current_program_info` reports
   `eboot.elf` (imported + analyzed).
5. Record the instance/project names here (and spot-check `functions=38845`).

Note: **REA 6.3.0** (bundle) can analyze through the same Ghidra **without the GUI** — the
interactive path is for follow-up/human inspection, not a prerequisite for headless analysis.
