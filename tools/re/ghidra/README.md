# tools/re/ghidra — headless Ghidra scripts (RE-0 baseline, RE-1/RE-2 walks)

Java scripts run inside `analyzeHeadless` against the RE-0 baseline project: program `eboot.elf`
(the unwrapped retail eboot), `x86:LE:64:default`, image base 0, 38,845 functions. The scripts are
read-only over the project and sort every list, so runs are deterministic (last section).
RE-2 T6 committed the parse-chain scripts here (`Disasm.java`, `FindParseChain.java`) so the recon
in `docs/notes/re2-recon.md` is reproducible from the repo alone.

## Rebuild the baseline

`scripts/re0-acceptance.sh` rebuilds `out/re/` from scratch: it stages `$SOTC_UROOT`, unwraps
`eboot.bin` with the port's `scripts/prepare.py` (→ `out/re/eboot.elf`, `out/re/analysis.json`),
imports + auto-analyzes the ELF into the project `out/re/ghidra` (`sotc`), and runs
`PrintSummary.java`. `GHIDRA_HOME` defaults to
`$HOME/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC`; analyzing a game eboot takes minutes.

## Headless invocation (the dot-free-location workaround)

Ghidra's `ProjectLocator` rejects `.`-prefixed path elements, and the worktree checkouts live under
`~/.local` — so the project location is passed as a dot-free temp symlink (the RE-0 workaround in
`scripts/re0-acceptance.sh`). Run from the repo root:

```bash
GHIDRA_HOME=$HOME/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC
proj_link_dir=$(mktemp -d)
ln -s "$PWD/out/re/ghidra" "$proj_link_dir/ghidra"
"$GHIDRA_HOME/support/analyzeHeadless" "$proj_link_dir/ghidra" sotc -process eboot.elf \
  -noanalysis -scriptPath "$PWD/tools/re/ghidra" \
  -postScript FindParseChain.java \
  > out/re/log-parsechain.log 2>&1
rm -rf "$proj_link_dir"
```

- `-process eboot.elf` opens the already-analyzed program; `-noanalysis` keeps runs deterministic.
- Several `-postScript` flags run in one invocation, in order, over one project open.
- Each worktree keeps its own copy of `out/re/ghidra`; never point two headless runs at one project
  directory (Ghidra locks it). `out/re/` is gitignored — logs land there too.

## VA → file mapping (`out/re/eboot.elf`)

Ghidra addresses are virtual addresses (image base 0). For every initialized byte of the unwrapped
ELF, **file offset = VA + 0x4000** — both PT_LOAD segments carry the same delta (measured
2026-10-19 from the program headers):

| segment | file offset | vaddr | filesz | delta |
|---|---|---|---|---|
| PT_LOAD[0] | 0x4000 | 0x0 | 0x1593978 | +0x4000 |
| PT_LOAD[2] | 0x1608000 | 0x1604000 | 0x17d3100 | +0x4000 |

Example — the attach tail at VA `0x00588d0e` (`node+0x48 = (sign ? base : 0) + (v & 0x7fffffff)`,
`re2-recon.md` §1.3):

```bash
xxd -s $((0x588d0e + 0x4000)) -l 16 out/re/eboot.elf   # 418b 7510 … = movl 0x10(%r13), %esi
llvm-objdump -d --start-address=0x588d0e --stop-address=0x588d3d out/re/eboot.elf
```

## Scripts

| script | purpose | modes |
|---|---|---|
| `PrintSummary.java` | RE-0 acceptance: one `summary program=… functions=…` line | — |
| `FindAnim.java` | RE-1: scan for `xff`-family constants/strings, follow refs, decompile | scan (default), decompile, follow |
| `FindSampler.java` | C3/RE-1: call-graph walk + decompile of the loader chain and the sampler | walk (default), decompile, callers, callees, refs |
| `FindParseChain.java` | RE-2: the `.anb` parse-chain seed set (18 functions, attach path) | decompile (default), refs, callers, callees |
| `Disasm.java` | print listing instructions for `<start-end>` ranges | — |

The scripts' headers carry their full usage; every mode prints addresses in sorted order.

## Re-running the RE-2 parse chain

The chain's decode is `docs/notes/re2-recon.md` §1; the seed set (each function with its memo
section) is the `SEEDS` table in `FindParseChain.java`. In the commands below, `…` abbreviates the
`analyzeHeadless` invocation above.

```bash
… -postScript FindParseChain.java > out/re/log-parsechain.log 2>&1  # decompile all 18 seeds
… -postScript FindParseChain.java callers 0x00589310 1             # the 9 attach callers (§1.1)
… -postScript FindParseChain.java refs 0x0066fe60
… -postScript Disasm.java 0x0067086b-0x00670941 0x005896a0-0x005899f0 0x00588d0e-0x00588d3d
```

## Re-run determinism

Every script sorts its lists and the runs use `-noanalysis`, so a fixed project + Ghidra build
reproduces byte-identical script output. Recorded check (2026-10-19, RE-2 T6) — the full
invocation

```bash
… -postScript PrintSummary.java
… -postScript FindParseChain.java
… -postScript FindParseChain.java refs
… -postScript FindParseChain.java callers 0x00589310 1
… -postScript FindSampler.java decompile 0x005896a0 0x00588900
… -postScript Disasm.java 0x0067086b-0x00670941 0x005896a0-0x005899f0 0x00588d0e-0x00588d3d
```

was run twice into `out/re/log-t6-run1.log` / `out/re/log-t6-run2.log`; the script output
(`grep '.java> '`) differs in **0 lines** (3,083 vs 3,083). The only full-log differences are the
per-run `mktemp` symlink path and the headless startup timing lines — not script output.
