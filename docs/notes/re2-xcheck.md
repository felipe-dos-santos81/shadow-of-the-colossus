# RE-2 X1 — second-engine cross-check of the sampler claims (Hopper via REA) (2026-10-19)

X1 of the RE-2 cycle: independently re-check the sampler memo's (`docs/notes/re2-sampler.md`, T3)
load-bearing claims with a **different RE engine** — Hopper 6.6.0, driven through REA
(`rea-agents` 6.4.0, Hopper provider) — and record what that engine can and cannot see of the claim
addresses. **No Ghidra was used** (the point of the exercise; REA's Ghidra provider candidate is not
configured in this environment either — `GHIDRA_INSTALL_DIR` unset). Every value below is either an
**engine observation** (REA/Hopper evidence IDs quoted) or, where labeled **supporting**, a
raw-byte/`llvm-objdump` read over the sha256-verified ELF copy — the two are kept separate on purpose.
Verdict labels: **CONFIRMED / CONTRADICTED / UNSEEN**; UNSEEN means *the engine could not reach the
address*, not that the claim failed.

The three claims (from the X1 brief / `re2-sampler.md`):

1. `0x0126b184` holds the 30.0 constant and has exactly 2 references, both in `FUN_005fc6b0`.
2. `FUN_005fc6b0` computes `fps = FUN_0060b610(...) × 30.0` (the debug readout).
3. The dispatch selector `motion+0x08 == 0` (instruction level, `FUN_0060b720` head) picks the
   flag-set path.

| | |
|---|---|
| Date | 2026-10-19 (run stamps UTC) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64 |
| Repo / branch | `sotc-native`, `re2-xcheck` (base `8cc3ba7` = main) |
| Engine | **Hopper Disassembler 6.6.0** (`launcher-sha256:b6eb364804103063c8d25b12d10a74c149519f319abdfe842729fbc10309416f`), via REA `rea-agents` 6.4.0 Hopper provider; document **`eboot.hop`** — subject `sha256 d0723afbdbd4cb3775c683a2b7f09534d371ce6d0b18146fc35b4d50c25861d3` (the same ELF as the sampler's `out/re/eboot.elf`) |
| ELF copy (read-only) | `out/re/eboot.elf` — copied from `re2-sampler`'s `out/re/eboot.elf`; `shasum -a 256` identical (`d0723afb…`) |
| Mapping | `PT_LOAD#0 off=0x4000 vaddr=0 filesz=0x1593978` → VA→file = **+0x4000** (re-verified); Hopper Segment 0 = `0x0..0x1593978`, Segment 2 = `0x1604000..0x3956aa4` |
| Scratch (gitignored) | `out/re/xcheck/` — `rea-session-bundle.json` (REA evidence export, 32 records), `refs-126b184.txt`, `hopper-launcher-usage.txt` |

## 0 · How the check ran (REA lane + labeled supporting reads)

REA tool calls against the bound Hopper document (all read-only; no `set_*`/`goto_*` mutations):
`list_documents`, `binary_session`, `binary_overview`, `list_segments`, `list_procedures`,
`list_names`, `read_bytes` (at `0x0126b184`, `0x005fc84f`, `0x005fcb20`, `0x0060b76b`),
`xrefs` (claim addresses + analyzed-region controls), `inspect_address_context`,
`resolve_containing_procedure`, and the procedure-gated operations (all fail for the claim
addresses — §4). The session evidence bundle was exported to `out/re/xcheck/rea-session-bundle.json`.

Supporting byte-level (clearly labeled; not engine results): `xxd` and `llvm-objdump` over the
sha256-verified copy — window disassembly at the claim sites and one full-image linear sweep
(`llvm-objdump -d --no-show-raw-insn eboot.elf | grep 126b184` → `refs-126b184.txt`, 2 lines).

```bash
# supporting, over the sha256-verified copy (VA→file = +0x4000)
xxd -s 0x126f184 -l 32 out/re/eboot.elf
llvm-objdump -d --start-address=0x5fc84f --stop-address=0x5fc8b0 --no-show-raw-insn out/re/eboot.elf
llvm-objdump -d --start-address=0x60b76b --stop-address=0x60b795 --no-show-raw-insn out/re/eboot.elf
llvm-objdump -d --no-show-raw-insn out/re/eboot.elf | grep 126b184   # → 2 lines
```

## 1 · Claim 1 — `0x0126b184` = 30.0, exactly 2 references in `FUN_005fc6b0`

**Value — CONFIRMED (engine).** `read_bytes` at `0x0126b184` (ev `ev_23b2b896c7399274770e677bcbb1820b9bb45f779d0d100adb7eb747354f7134`,
re-run at 8 bytes: ev `ev_55bea17faed1bfa06f41727f70b3b9381f1a046afdf96b0701cd67743457eb44`) returns:

```
0000f041 000080bf 0000003f 0000803f 8180803b 0000803f 000080bf 0000803f
```

First word `00 00 f0 41` = **`0x41F00000` = float32 30.0**; the next three words decode to −1.0,
0.5, 1.0 — matching `re2-sampler.md` §3.3's constant table (`0x0126b184` / `0x0126b188` /
`0x0126b18c` / `0x0126b190`). Precision note: the constant is a **single-precision float32**, not a
double — the memo reads it as `dword ptr` (`VMULSS`), and an IEEE-754 double 30.0 would be
`00 00 00 00 00 00 3E 40`. The "double" phrasing in the X1 brief is imprecise; the substance
(30.0 at that address) is engine-confirmed. `xxd` over the copy at file offset `0x126f184` is
byte-identical to the engine read (supporting; mapping +0x4000 re-verified from `PT_LOAD#0`).

**References — UNSEEN (engine).** `xrefs 0x0126b184` → `[]` (ev `ev_12d2559eacd31a54db75653fc040f0c7d9625388b7c6b6ff399b98cdbd455c84`).
The empty result **cannot be read as "zero references"**: Hopper's analysis never reached the code
that references the constant (§4 — the whole sampler region is outside the analyzed set), so the
engine has no reference edges to report either way. Control (same tool, analyzed regions):
`xrefs 0x53` → `["0x35"]`; `xrefs 0x2dd7100` → `["0x27","0x37"]`; `xrefs 0x57d590` → `["0x53678f"]` —
the mechanism works where analysis exists.

**Supporting byte-level (labeled):** the full-image `llvm-objdump` sweep finds **exactly 2**
references to `0x126b184` in the entire image (`refs-126b184.txt`):

```
5fc858:  vmulss 0xc6e924(%rip), %xmm0, %xmm3  # 0x126b184
5fcb34:  vmulss 0xc6e648(%rip), %xmm0, %xmm4  # 0x126b184
```

— the two sites the memo names (`0x005fc858`, `0x005fcb34`), both after the claimed entry
`0x5fc6b0`. Caveats stated: linear sweep (can miss references whose address is computed rather than
encoded; the standard RIP-relative form for a float constant load is what it catches); "both inside
`FUN_005fc6b0`" is the Ghidra-side function-membership claim and is not independently checkable by
this engine (Hopper has no procedure there).

**Verdict:** value **CONFIRMED** (engine); ref-count **UNSEEN** (engine) / consistent — exactly the
2 claimed sites — supporting.

## 2 · Claim 2 — `fps = FUN_0060b610(...) × 30.0` in `FUN_005fc6b0`

**UNSEEN (engine).** Hopper has **no procedure at `0x005fc6b0`**:
`inspect_address_context 0x005fc6b0` → `found: false`, `reason: "not_in_procedure"` (name/comment
`null`); `resolve_containing_procedure` → same. Every procedure-gated operation fails with
`"Hopper <op> failed (-32000, invalid_request): No procedure exists at the requested address"`:
`read_function_instructions`, `procedure_assembly`, `procedure_pseudo_code`, `analyze_function`,
`procedure_info`, `procedure_callers`, `procedure_callees`, `procedure_address`.
`inspect_native_instruction` (the instruction-level decoder) is **not declared by the Hopper
provider** at all: `capability_unavailable`, `"operation is not declared by this provider"`.
So the engine can neither decompile nor disassemble the function.

**Supporting byte-level (labeled):** the engine's own byte read at the readout window —
`read_bytes 0x005fc84f` len 96 (ev `ev_db922a0c578bfec3e2b33e91acd9df8c13b594694b3742c859313dcddbd8aa90`),
`0f bf 5f 04 e8 b8 ed 00 00 c5 fa 59 1d 24 e9 c6 00 …` — decodes (`llvm-objdump`, supporting) to
the memo §3.1 sequence line-for-line:

```
5fc84f:  movswl 0x4(%rdi), %ebx                       ; frames = motion+4
5fc853:  callq 0x60b610                               ; rate = FUN_0060b610(motion)
5fc858:  vmulss 0xc6e924(%rip), %xmm0, %xmm3 # 0x126b184   ; fps = rate × 30.0
5fc872:  vcvtsi2ss …                                  ; frames−1
5fc876:  vdivss  %xmm3, %xmm0, %xmm2                  ; duration_s
5fc888:  vaddss  0xc6e8f8(%rip), %xmm0, %xmm0 # 0x126b188   ; frames + (−1.0)
5fc890:  vdivss  %xmm0, %xmm4, %xmm0                  ; progress
5fc894:  vmulss  %xmm2, %xmm0, %xmm1                  ; seconds = progress × duration_s
```

The second site is the same shape: `callq 0x60b610` at `0x5fcb2f` and
`vmulss 0xc6e648(%rip), %xmm0, %xmm4 # 0x126b184` at `0x5fcb34`. The `call` target arithmetic was
checked by hand (rel32 = `0xedb8`, next-IP `0x5fc858` → `0x60b610`) and the constant operand
target too (`0x5fc860 + 0xc6e924` → `0x126b184`; `0x5fcb3c + 0xc6e648` → `0x126b184`).

**Verdict:** **UNSEEN** (engine — no procedure, no disassembly lane); the supporting bytes are
consistent with the claim's instruction listing.

## 3 · Claim 3 — the `motion+0x08 == 0` gate in `FUN_0060b720`

**UNSEEN (engine).** Hopper has **no procedure at `0x0060b720`** — same failure set as §2
(`resolve_containing_procedure` → `not_in_procedure`; every procedure-gated op →
`"No procedure exists at the requested address"`). `xrefs 0x0060b720` → `[]` (ev
`ev_2d4b517cd12316352a053cac773fc78f231b1a0557df1ef0210428d205bc3c30`); `xrefs 0x0060b610` → `[]`
(ev `ev_b502bf3ee873fe3f64770e73d6526881969dfd704a1c34b8601922d526c56187`). No instruction-level
operation is available to decode the gate.

**Supporting byte-level (labeled):** the engine's byte read at the gate —
`read_bytes 0x0060b76b` len 48 (ev `ev_3ba161eca1c48be57e5c3510df0037848e2d12ae34a37022d382402433325a03`),
`41 8b 46 08 48 f7 de 31 ff 89 c1 81 e1 ff ff ff 7f 85 c0 48 0f 48 fe 48 39 f9 0f 85 63 01 00 00 …`
— decodes to the memo §2.3 listing **verbatim**, including the branch target:

```
60b76b:  movl  0x8(%r14), %eax        ; EAX = motion+0x08
60b76f:  negq  %rsi
60b772:  xorl  %edi, %edi
60b774:  movl  %eax, %ecx
60b776:  andl  $0x7fffffff, %ecx
60b77c:  testl %eax, %eax
60b77e:  cmovsq %rsi, %rdi
60b782:  cmpq  %rdi, %rcx
60b785:  jne   0x60b8ee              ; ≠ → path B (flag-clear direct read)
```

(`jne` rel32 `0x163`, next-IP `0x60b78b` → `0x60b8ee`, checked by hand.) The *semantic* reading —
`v == 0` → the `FUN_005fd900` dispatch loop, `v != 0` → the direct `FUN_0060bba0` read — remains the
Ghidra derivation; the engine cannot confirm either path target (`0x60b8ee` is in unanalyzed code).

**Verdict:** **UNSEEN** (engine); supporting bytes match the claimed instruction listing exactly.

## 4 · Engine capability — what REA/Hopper could and could not see

**Session.** Hopper provider, one document (`eboot.hop`, subject sha256 `d0723afb…`), 54 operations
declared available; `binary_overview` (ev `ev_0aef17c2678968a44e4d8f9eeecea6d7f457769e1bef408bd62f65023aab069c`):
2 segments, **1,261 procedures**, 0 strings (the provider exposes no string objects). The earlier
sessions' "~1,261 procedures" reading is confirmed exactly (1,261).

**The analysis gap — the key capability fact.** `list_procedures` (ev
`ev_0905fbd840b8d946e2c462cfe047dd28bfcf93f2a5e23ea164d448082c0cc33b`) shows the 1,261 procedures
span `0x20..0xf90300`, all named `sub_*` except `EntryPoint` (`0xf120`) and `j_sub_f590`; **zero
procedures lie in `[0x5c0000, 0x620000]`** and, more broadly, none between the nearest analyzed
procedures `0x57d590` (below the sampler region) and `0x882d60` (above it). Every claim address
(`0x5fc6b0`, `0x5fc858`, `0x5fcb34`, `0x60b610`, `0x60b720`, `0x60b76b`, `0x60b8ee`) sits inside
that gap, so every procedure-gated operation returns
`"No procedure exists at the requested address"` (and `resolve_containing_procedure` /
`inspect_address_context` report `not_in_procedure`). `list_names` (1,678 entries) has no name at or
near the claim addresses either — the only name in `[0x580000, 0x900000)` is `sub_882d60`.
Contrast for scale: the Ghidra baseline project analyzed **38,845** functions of this same image.

**What the engine *did* resolve.** (a) Document/subject identity and the sha256 pin to the same
ELF as the sampler's copy; (b) the two segments and the VA space (`0x0..0x1593978`,
`0x1604000..0x3956aa4`); (c) **raw byte reads at any mapped VA** — `read_bytes` is not
procedure-gated, returns `complete: true` at every claim site (the bytes quoted in §1–§3 are engine
output); (d) `xrefs` inside analyzed regions (controls above); (e) procedure dossiers/assembly/
pseudo-code for the 1,261 analyzed procedures (not the claim functions).

**What the engine could *not* resolve (and why).**
- *Instruction-level decoding outside analyzed procedures*: `inspect_native_instruction` is
  `capability_unavailable` — `"operation is not declared by this provider"`. No disassemble-range
  operation exists in REA's Hopper surface.
- *Procedure-level analysis at the claim addresses*: absent from Hopper's auto-analysis (the gap
  above); the provider's own limitation text: *"Results depend on Hopper's completed static
  analysis."*
- *`address_to_file_offset`*: fails with `target_unavailable` — the document's recorded source path
  (`…/re2-recon/out/re/eboot.elf`) no longer exists (`ENOENT`), so byte-verification against the
  original source is unavailable; document-retained bytes are still readable (`read_bytes`).

**Headless/CLI Hopper path — findings (filesystem inspection; the GUI was not opened).**
- `Contents/MacOS/hopper` is the **Hopper Launcher v1.6.0** — a CLI that loads a document
  (`-e file` / `-d hop_file`) and can run Python (`-y CMD` / `-Y script.py`) *"once the initial
  analysis is ended"*, with GUI-driving switches (`-C --checkbox`, `-R --radio`, `-j --jump-to`,
  `-X --address`, `-s --string`); **no headless flag exists** (full usage captured in
  `out/re/xcheck/hopper-launcher-usage.txt`). Its job is to hand work to the Hopper application.
- The analysis engine lives **inside the app**: the `HopperLowLevel` Python module (found in the app
  binary) and `libLLVMServices.dylib` (the app's only non-Sparkle framework). The app (v6.6.0) is a
  regular GUI app (no `LSUIElement` / `LSBackgroundOnly`).
- The **Python SDK entry point** is `Contents/Resources/hopper_api.py` (plus `script_*.py` samples)
  — an **in-app** module (`import HopperLowLevel`); not a standalone SDK. It is what the GUI script
  editor and the launcher's `-Y` drive. Hopper's documented MCP server (`HopperMCPServer`, STDIO;
  help page `mcp.html`) likewise launches/attaches the running app (`"Trying to launch Hopper"`,
  `"Waiting for Hopper to be ready"`, `"Hopper is already launched"`).
- **Finding: there is no headless Hopper.** Every programmatic lane (in-app Python, the MCP server,
  the launcher) requires the Hopper application running; REA's Hopper provider adds raw byte reads
  and analyzed-region queries, but **no instruction decode for unanalyzed code**.
- Note: `hopper -h` (invalid option) was run once to capture the launcher usage — it printed usage
  and exited (`exit=1`); the open-document set was re-checked afterwards (`[eboot.hop]`, unchanged)
  and no new process appeared. All other REA calls were read-only; the only writes were to this
  worktree's `out/re/xcheck/`.

**What would unlock deeper engine checks.** (a) Hopper-side: run its analysis over the gap — the
in-app Python API exposes `disassembleWholeSegment` / `markAsProcedure` / `getInstructionAtAddress`
— then the procedure- and instruction-gated queries would resolve through the same REA lane (this
would mutate the shared `eboot.hop` document and/or drive the GUI, so it was not done in this
read-only pass); (b) a headless provider with instruction-level queries (REA's Ghidra provider —
which would be the *first* engine again, defeating the purpose here).

## 5 · Verdict summary

| # | claim (from `re2-sampler.md`) | engine verdict (REA/Hopper) | supporting byte-level (labeled, `llvm-objdump`) |
|---|---|---|---|
| 1a | `0x0126b184` holds 30.0 | **CONFIRMED** — `read_bytes` `00 00 f0 41 …` = float32 30.0 (single precision; memo agrees, brief says "double") | xxd over the copy identical |
| 1b | exactly 2 refs, both in `FUN_005fc6b0` | **UNSEEN** — `xrefs` = `[]`, but the referring code is outside the analyzed set; empty ≠ zero | exactly 2 refs in the full image, at the claimed `5fc858` / `5fcb34`; function membership not independently checkable |
| 2 | `fps = FUN_0060b610(…) × 30.0` | **UNSEEN** — no procedure at `0x5fc6b0`; all procedure-gated ops fail; no instruction-level op | readout bytes decode to `callq 0x60b610` + `vmulss …,0x126b184` (+ the §3.1 sequence) — consistent |
| 3 | `motion+0x08 == 0` gate | **UNSEEN** — no procedure at `0x60b720` | gate bytes decode to the §2.3 listing verbatim, incl. `jne 0x60b8ee` — consistent |

**Nothing was CONTRADICTED.** The engine confirmed one claim directly (the constant's value and
width), and could not reach the other two — a consequence of Hopper's shallow pass on this stripped
ELF (1,261 of ~38.8k functions, with the entire sampler region in the gap), not of the claims.
The supporting byte-level reads are consistent with all three claims, but they are *supporting
evidence only* — they are not engine confirmations, and this note does not present them as such.

## 6 · Reproduce

- REA lane: bind/attach the Hopper provider (document `eboot.hop`), then re-run the calls in §0;
  the exported evidence bundle (`out/re/xcheck/rea-session-bundle.json`) carries the records
  (32 records, 0 unknowns). Key evidence IDs are cited per section above.
- Supporting lane: the three bash lines in §0 over `out/re/eboot.elf` (sha256 `d0723afb…`, copied
  from the sampler's worktree); outputs land in `out/re/xcheck/` (`refs-126b184.txt` = 2 lines).
- Hopper tooling: `out/re/xcheck/hopper-launcher-usage.txt`; app bundle inspection commands
  (`strings`, `otool -L`, `PlistBuddy`) as quoted in §4.
