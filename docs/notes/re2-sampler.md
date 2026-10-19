# RE-2 T3 — the sampler loop closed: the dispatch on both variants + the seconds→frames conversion (2026-10-19)

T3 of the RE-2 cycle (recon memo `docs/notes/re2-recon.md` §6, T3): connect `FUN_005fd900`'s
dispatch to **both** `.anb` variants — the flag-clear blob/pointer path and the flag-set 12-byte
record path (T2's dispatch table, `docs/notes/re2-flagset.md` §4) — pin the **seconds→frames
conversion** above `FUN_005fc6b0`, and confirm the **frame count source** (`motion+4`) against
T2's samples. Everything below is measured with Ghidra 12.1.4 headless against a copy of the
RE-0 baseline project (`eboot.elf`, image base 0, 38,845 functions) and with raw-byte reads over
the read-only game tree; the scratch scripts/logs live under the gitignored `out/re/` of this
worktree. This file is **new** — `re-anb.md` is untouched (T4 owns it).

| | |
|---|---|
| Date | 2026-10-19 (run stamps UTC) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64; `python3` 3.12 (stdlib only) |
| Repo / branch | `sotc-native`, `re2-sampler` (base `55a9b7b` = main after the first wave) |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Ghidra project (copy) | `out/re/ghidra` (copy of `re2-recon`'s project — read-only source), program `eboot.elf`, VA→file = `+0x4000` |
| Scripts (scratch) | `out/re/scripts/FindSampler.java` (decompile/callers/callees/refs — copied from the recon), `Disasm.java` (instruction ranges — copied), **`ScanField.java`** (new this pass: find instructions whose operands contain a given scalar displacement) |
| Logs (this worktree) | `log-t3-1.log` (callers of `FUN_005fc6b0` depth 2, callers of `FUN_005fd900`, refs of the constants, key decompiles), `log-t3-2.log` (helpers + gate disasm + callers of `FUN_0060b720`), `log-t3-3.log` (field scan `0x274/0x278/0x294`, callers of the rate/fps accessors), `log-t3-4.log` (`FUN_00636650` family + gate disasm), `log-t3-5.log` (state-machine chain), `log-t3-6.log` (slot-time setters + converters), `log-t3-7.log` (frame-delta + state-machine head disasm), `log-t3-8.log` (`FUN_0066dec0`/`FUN_0066e460` — the loader-side slot table, code-verified) |
| Samples (raw reads) | `player_a_stand`, `horse_a_add_horse_kick`, `objanim_leo_a_jump_fall`, `objanim_cerberus_a_atk_near_l`, `objanim_ath_damage`, `objanim_tree_down_xm`, `camera_demo_end07_cut007` |

```bash
GHIDRA_HOME=$HOME/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC
proj_link_dir=$(mktemp -d); ln -s "$PWD/out/re/ghidra" "$proj_link_dir/ghidra"
"$GHIDRA_HOME/support/analyzeHeadless" "$proj_link_dir/ghidra" sotc -process eboot.elf \
  -noanalysis -scriptPath "$PWD/out/re/scripts" \
  -postScript FindSampler.java callers 0x005fc6b0 2 cap=100 \
  -postScript FindSampler.java callers 0x005fd900 1 cap=20 \
  -postScript FindSampler.java refs 0x0126b184 0x0126b188 0x0126b18c 0x0126b2ac 0x0126b1b8 \
  -postScript FindSampler.java decompile 0x005fc6b0 0x005fc5d0 0x0060b720 0x0060b610 \
  > out/re/log-t3-1.log 2>&1; rm -rf "$proj_link_dir"
# further runs: log-t3-2 … log-t3-8 (same pattern; run scripts out/re/t3-run{1..8}.sh)
```

## 1 · The runtime field pair the sampler reads (MEASURED)

`FUN_0066fe60` rotates the structure's first three data fields in place (decompile lines 16–21,
`param_1` = the motion structure; the loader stores the 0x88 object at `+0x10` **after** this pass,
`FUN_00670680` step order per the recon §1.1):

```
+0x10 ← old +0x14      +0x14 ← old +0x18      +0x18 ← old +0x10      (+0x1C unchanged)
```

So at sampler time (all offsets data-region-relative, disk = file bytes; runtime = after the rotation
+ the object store):

| runtime field | disk origin | meaning |
|---|---|---|
| `motion+0x18` | `+0x10` | **the per-track array** — 12-byte records (flag-set) or u32 blob pointers (flag-clear) |
| `motion+0x08` | `+0x08` | the per-track auxiliary field — byte-per-track formats (flag-clear) or u32-per-track params (flag-set), 0 when absent |
| `motion+0x10` | object ptr | the 0x88 object; `object+0x00` = **track count** (disk `+0x14`), `object+0x04` = disk `+0x18` |
| `motion+0x04` | `+0x04` | frame count (low u16) + rate/flag word (high u16) — §4 |

Measured in the disassembly: `FUN_0060b720` clamps its track count to `**(int **)(motion+0x10)`
(= `object+0x00` = track count; `0x0060b758`–`0x0060b767`); `FUN_005fd900` resolves the array as
`resolve(motion+0x18)` (`0x005fd946`–`0x005fd95b`: `AND ECX,0x7fffffff` / `TEST EAX,EAX` /
`CMOVS` the base / `ADD`). The recon §1.5 rotation and §2.1 table are re-derived here from code.

## 2 · The dispatch connection — both variants (MEASURED)

### 2.1 The flag-set shape: 12-byte records (the dispatch)

`FUN_005fd900` (C3 §6.1; T2 §2.1/§4) reads, unconditionally:

```c
lVar24 = resolve(motion+0x18);                        /* the record array */
*out   = *(int *)(lVar24 + track*0xC);                /* record.type  → switch (type − 1) */
...every case body reads p2 as: resolve(*(u32 *)(lVar24 + 8 + track*0xC));   /* record.p2 */
```

(verbatim from the decompile; e.g. `caseD_20` `uVar17 = *(uint *)(lVar24 + 8 + lVar15 * 0xc)`).
The case bodies are T2's §4 dispatch table — they take `p2` as the record's key blob and **none
reads `p1`**. The switch is entered twice: a **no-cooked-object pre-dispatch** (when the object
chain resolves to null) with inline bodies for types 0/4/camera (which read `p2` the same way)
and `goto`s into the shared bodies, then the main dispatch. The stride-0xC/offset-8 read is the
**flag-set** shape: `FUN_0066fe60` walks the same array at stride `0xC` through `FUN_0066f9a0`
when bit 16 of the version word is set.

### 2.2 The flag-clear shape: format byte + blob pointer (no dispatch)

For flag-clear motions the same array (`motion+0x18`) holds **u32 blob pointers at stride 4**, and
the per-track **type byte** (`motion+8 + track`) is not a record type but the **quaternion accessor
format** — the read is a direct `FUN_0060bba0` call, no switch:

```c
/* FUN_0060b720, the non-zero branch (decompile lines 102–176) */
FUN_0060bba0(out + …,                                      /* the quaternion output slot      */
             *(byte *)(motion+8 + track),                  /* param_2 = format                */
             resolve(*(u32 *)(motion+0x18 + track*4)),     /* param_3 = the blob (descriptor) */
             frame);
```

The blob **is** the accessor descriptor, and the loader re-bases exactly the slot each format reads
(loader-side `FUN_0066dec0` per-format offsets == accessor-side reads; both re-read from code in
this pass — `FUN_0066dec0` switches on the same format byte and sets bit 31 on the slot listed
below, `log-t3-8.log`):

| format | `FUN_0066dec0` re-bases | `FUN_0060bba0` reads | sample location |
|---|---|---|---|
| 2 | `+0x0C` = `desc[3]` | `param_3[3]` as a pointer (case 2) | `resolve(desc[3]) + key*4` (float angle) |
| 3–6 | `+0x20` = `desc[8]` | `param_3[8]` as a pointer (cases 3–6) | `resolve(desc[8]) + key*4` (packed u32) |
| 0xC / 0xE | `+0x18` = `desc[6]` | `param_3[6]` (cases 0xc/0xe) | `resolve(desc[6]) + key*6` / `+ key*4` |
| 0xD | `+0x14` = `desc[5]` | `param_3[5]` (case 0xd) | `resolve(desc[5]) + key*2` (int16 scalar) |
| 1 / 0xA / 0xB | — (inline) | case 1: `desc + key*16 + 4`; case 0xa: `desc + key*12`; case 0xb: `desc + key*6` | inline |

So for flag-clear files the "type" is the format and the "blob" is the descriptor — the dispatch's
per-type vocabulary (T2 §4) is the flag-set-only mechanism. This is the **flag-clear blob/pointer
path** the T3 brief names.

### 2.3 The selector: `FUN_0060b720`'s gate (+ the dispatch's own fallback)

`FUN_0060b720` (the per-track bulk loop; its **only caller is `FUN_005fc6b0`** — measured, 1 edge)
selects between the two shapes at its head; the gate is `motion+0x08 == 0` (disasm
`0x0060b76b`–`0x0060b785`):

```
0060b76b  MOV EAX,dword ptr [R14 + 0x8]   ; EAX = motion+0x08
0060b76f  NEG RSI                         ; RSI = -load-base
0060b772  XOR EDI,EDI
0060b774  MOV ECX,EAX
0060b776  AND ECX,0x7fffffff              ; ECX = v & 0x7fffffff
0060b77c  TEST EAX,EAX
0060b77e  CMOVS RDI,RSI                   ; RDI = (v < 0) ? -base : 0
0060b782  CMP RCX,RDI
0060b785  JNZ 0x0060b8ee                  ; ≠ → path B (flag-clear direct read)
```

The compare is the compiler's form of "the resolved field is null": it is true exactly when
`v == 0` (a negative `v` makes the right side a 64-bit `−base`, unreachable by the 31-bit left
side). **Path A (`v == 0`) = the `FUN_005fd900` dispatch loop; path B (`v != 0`) = the direct
`FUN_0060bba0` loop of §2.2.** Both paths write the same per-track output block (16-byte
quaternion + identity `{0,0,0,1}` from `0x0126b5b0`).

The same flag-clear read also lives **inside** the dispatch as a fallback: `FUN_00600810` (the
case body for types 1/5) uses the resolved cooked object when available and otherwise takes
`LAB_00600974` — the identical read (`motion+8+track` format byte, `motion+0x18+4·track` blob
pointer, `FUN_0060bba0`) (decompile lines 86–106). Measured mechanism; which motions take the
fallback in practice was not enumerated.

### 2.4 Loader-side agreement (re-derived from code)

`FUN_0066fe60`'s track walk (decompile lines 77–118) branches on the **same bit** and walks the
**same array address** (`param_1[3]` = runtime `+0x18`):

```c
if ((*param_1 & 0x10000) == 0) {                       /* flag-clear */
  blob = resolve(*(u32 *)(array + track*4));           /* stride 4 */
  FUN_0066dec0(*(byte *)(typearr + track), blob, frames);
} else {                                               /* flag-set */
  FUN_0066f9a0(array + track*0xC, frames, version);    /* stride 0xC */
}
```

So the loader's per-track fix-up and the sampler's per-track read agree on: the array at `+0x18`,
the two strides (4 vs `0xC`), and the bit-16 gate. T2's §1 measurement (records at `+0x10`,
`+0x08 == records_end` for the aux-carrying files) is re-verified by a raw dump of the leo file
(records at data offset `0x4DC`, 15 × 12 B; aux at `0x590` = records end, 15 u32s
`0x1B 0x1A 0x19 0x1D 0x1C …`).

### 2.5 The `+0x08` element shape by variant (the boundary, kept open)

- **flag-clear**: byte-per-track format (player: `03 03 0d 03 …` — all ≤ `0xe`, the accessor's
  vocabulary) — consumed by path B / the fallback.
- **flag-set**: u32-per-track parameter, consumed by type-specific case bodies. Measured: the
  camera case 0x23 reads `motion+8 + 4·track` as the **camera id** (`iVar9 = *(int *)(resolve(+0x08) + track*4)`,
  default `-1` when `+0x08 == 0`; decompile lines 1310–1325); `camera_demo_end07_cut007`'s aux is
  `0xFFFFFFFF`; the corpus has `0x00010004/5/7` values (T2 §1).
- **The 872/1,461 flag-set files with a non-zero `+0x08`** (leo/cerberus/camera): the bulk loop's
  path B would read their aux bytes as formats (leo: `0x1B…` > `0xe` → the accessor's zero branch).
  Their consumers are the **direct `FUN_005fd900` callers** (the other 8: `FUN_006112c0`,
  `FUN_006694c0`, `FUN_0066b220`, `FUN_006789c0`, `FUN_0067cf00`, `FUN_0067d3e0`, `FUN_006802d0`,
  `FUN_006ddba0` — most pass a stored time pair such as `+0x274/+0x278` or `+0x3f4/+0x3f8`, some
  call sites pass literal `(0,0)`), not `FUN_005fc6b0`'s bulk loop. T2 §7's open item is narrowed
  but **not closed**: no file-level rule selects the caller; the mechanism (dispatch callers bypass
  `FUN_0060b720`) is measured, the population mapping is not.

## 3 · The time model — the seconds→frames conversion (MEASURED)

### 3.1 The readout identity

`FUN_005fc6b0`'s debug readout (disasm `0x005fc848`–`0x005fc8a4`) computes, verbatim:

```
005fc84f  MOVSX EBX,word ptr [RDI + 0x4]        ; frames = motion+4 (signed u16)
005fc853  CALL 0x0060b610                       ; rate = FUN_0060b610(motion)
005fc858  VMULSS XMM3,XMM0,dword ptr [0x0126b184] ; fps = rate × 30.0
005fc872  VCVTSI2SS XMM0,EAX  (EAX = frames−1)
005fc876  VDIVSS XMM2,XMM0,XMM3                 ; duration_s = (frames−1) / fps
005fc884  VCVTSI2SS XMM0,EBX  (EBX = frames)
005fc888  VADDSS XMM0,XMM0,[0x0126b188]         ; frames + (−1.0)
005fc890  VDIVSS XMM0,XMM4,XMM0                 ; progress = t / (frames−1)
005fc894  VMULSS XMM1,XMM0,XMM2                 ; seconds = progress × duration_s
```

i.e. **`fps = rate × 30.0`**, `seconds = frame / fps`, `duration_s = (frames−1) / fps` — the print
`"%5.2f/%d = %.2f/%.2f : %.2fFPS"` displays exactly these. The independent second site:
`FUN_0067bb30(node)` returns `(frames−1) × (1/30) / rate` (`0x01270c88 = 0x3d088889` = `1/30`) —
the same duration. **The conversion is `frame = seconds × rate × 30`** (the inverse of the readout).

### 3.2 `rate` = `FUN_0060b610(motion)` (the clip's playback multiplier)

```c
if (resolved anim asset) rate = *(f32 *)(*(obj+0x60) + 8) / 30.0;      /* 0x0126b2a0 = 30.0 */
else { n = *(u16 *)(motion + 6) >> 4;                                   /* the high u16 of motion+4 */
       rate = (n != 0) ? (float)n * (1/4096) : 1.0; }                   /* 0x0126b2a4, 0x0126b2a8 */
return rate * *DAT_0300c948;                                            /* the global time scale */
```

The raw path is the file's **Q12 rate** (`(u16@+6 >> 4)/4096`, default 1.0). Measured raw values:
player `0x8002` → 0.5, horse `0x8002` → 0.5, cerberus `0x6660` → 0.4, tree_down `0x4220` → 0.258,
leo/camera/ath `0` → 1.0. (The cooked path (`header+0x08/30`) is the same quantity cooked at load;
that the two agree is INFERRED — only the raw path is file-verifiable.)

### 3.3 Constants (raw reads, `out/re/eboot.elf`, VA→file `+0x4000`)

| VA | value | used by |
|---|---|---|
| `0x0126b184` | **30.0** (`0x41f00000`) | `FUN_005fc6b0` only — refs `0x005fc858`, `0x005fcb34` (C3's constant, cross-checked) |
| `0x0126b188` | −1.0 | `FUN_005fc6b0` readout (`frames + (−1)`) |
| `0x0126b18c` / `0x0126b190` | 0.5 / 1.0 | `FUN_005fc6b0` blend swap |
| `0x0126b1b8` | 1.0 | `FUN_005fd900` blend `(1−frac)` (18 reads) |
| `0x0126b2a0` | **30.0** | `FUN_0060b610` resolved path (`fps/30`) |
| `0x0126b2a4` | 1/4096 | `FUN_0060b610` raw rate scale |
| `0x0126b2a8` | 1.0 | `FUN_0060b610` default rate |
| `0x0126da78` | **30.0** | `FUN_00636650` (`param_1 × 30.0`) |
| `0x0126da7c` | 1/60 | `FUN_00636650` tick epsilon |
| `0x01270c88` | **1/30** | `FUN_0067bb30` duration |

C3's statement "the constant 30.0 has no other reference in the image" is **true for
`0x0126b184`** (2 refs, both in `FUN_005fc6b0`) but three further 30.0-family constants exist
above; the *conversion* is therefore not single-sited — the rate (`fps/30`) is the carrier.

### 3.4 The chain above `FUN_005fc6b0` (callers walked)

`FUN_005fc6b0(float t, motion, node, out, tracks, track)` — `t` is a **float frame index**; it
wraps/blends and calls `FUN_0060b720` at frames `⌊t⌋` and `⌊t⌋+1` (`FUN_0060c510` blends). All
**8 callers** (measured; all pass frame times and re-wrap by `frames−1` or clamp to it):

| caller | role (measured) |
|---|---|
| `FUN_005fd1b0` | clamp `t` to the frame count, call with track = −1 |
| `FUN_005fc4e0` | wrap `t` by `(frames−1)`; an exact-integer wrapped time maps to `frames−1` (`DAT_0126b180` = −1.0 — the loop-closure convention; same in `FUN_00605060`/`FUN_006086f0`/`FUN_0060f8e0`) |
| `FUN_00605060` | per-record search + sample, then apply to an object node |
| `FUN_00608480` / `FUN_006086f0` / `FUN_006089c0` | anim-slot resolution (`node+0x48`) + sample (blend-motion path in `FUN_006089c0`) |
| `FUN_0060f020` | "Applying to Bone" — per-bone sample/apply loop |
| `FUN_0060f8e0` | "GRID1 BLEND" — two samples + `FUN_0060c510` blend |

Level-2 (the frame time's home): `FUN_00631cd0` (entity update) → **`FUN_00636650`** (the character
state machine, 11,980 B; writes the entity's frame fields) → **`FUN_0063c2d0`** (pose applier;
samples at `entity+0x278`, writes transform matrices) → the wrappers above → `FUN_005fc6b0`.
`FUN_00636650` computes the frame time as `len(node+0x168) × phase(local_16c) × speeds(+0x534,
+0x288, +0x6f8, +0x290)` (line 728) and writes the pair `+0x274`/`+0x278` (lines 341/477/592/731/767/
791/816…); in the 0x20-flag branch it quantizes with **`param_1 × 30.0`** (`0x0126da78`) — `param_1`
is a time (it is differenced against `local_16c`, line 342). The anim-slot time (`slot+0x14`, slot
stride `0xe8` at `obj+0x470+8`) is advanced by `FUN_006049f0` (`time += Δ`, wrap by `frames−1`),
`FUN_006111c0` (`Δ = rate × x × slot+0x28`), remapped on motion switch by `FUN_005fd550`
(a progress-fraction rescale between the two motions' frame counts, clamped to `frames−1`), and
read back as `frames−1` by `FUN_006049e0`/`FUN_006066e0`
(`FUN_006049e0 = FUN_006066e0 − 1`, `FUN_006066e0 = FUN_005fc5d0`). The object-side sampler time
(`+0x3f4`/`+0x3f8`, used by `FUN_0067d3e0`'s `FUN_005fd900` calls) is written from the slot time
and remapped by `FUN_005fd550` the same way.

**Result:** every sampling site receives **frames**; the seconds→frames relation is the §3.1
identity (`frame = seconds × rate × 30`). The *advance* sites that consume wall-clock time are
`FUN_00636650` (`× 30.0`, quantized) and `FUN_006111c0`/`FUN_006049f0` (rate-scaled Δ); the
ultimate seconds source (the game clock feeding the entity update) was not traced to a single
function — the frame fields are already frames at every boundary this pass walked.

## 4 · The frame count source (`motion+4`) — confirmed

- `FUN_005fc5d0(motion)`: resolved path → `FUN_00a26590(obj)` = `u16 @ (*(obj+0x60)+0x10)`;
  **fallback → `(int)*(short *)(motion + 4)`** (decompile; the fallback is the file-level value).
- `FUN_005fc6b0` reads it directly: `MOVSX EBX,word ptr [RDI + 0x4]` (`0x005fc84f`) — the readout
  and every `FUN_005fc5d0` call in its body use the same field.
- `FUN_006066e0(node)` = `FUN_005fc5d0(node's motion)`; `FUN_006049e0` = that `− 1` (the wrap
  modulus used across `FUN_00636650`/`FUN_0060f020`).

Cross-check against T2's samples (raw reads; `motion = u32@0xB4 + u32@0x10`, count = low u16):

| file | variant | `u32@motion+4` | frames | rate (high u16) | T2/memo reference |
|---|---|---|---|---|---|
| `player_a_stand` | flag-clear | `0x8002004C` | 76 | `0x8002` → 0.5 | memo §2.1 (76) |
| `horse_a_add_horse_kick` | flag-clear | `0x80020010` | 16 | `0x8002` → 0.5 | memo §2.1 (16) |
| `objanim_leo_a_jump_fall` | flag-set | `0x00000015` | 21 | 0 → 1.0 | T2 §1 (v8, 21 f) |
| `objanim_cerberus_a_atk_near_l` | flag-set | `0x66600019` | 25 | `0x6660` → 0.4 | T2 §5 (25 keys) |
| `objanim_ath_damage` | flag-set | `0x0000003C` | 60 | 0 → 1.0 | T2 §5 (60 keys) |
| `objanim_tree_down_xm` | flag-set | `0x42200018` | 24 | `0x4220` → 0.258 | T2 §2.2 (24 f) |
| `camera_demo_end07_cut007` | flag-set | `0x000001E1` | 481 | 0 → 1.0 | T2 §3.3 (481 f) |

All seven match the previously recorded numbers — the frame count source is `motion+4` (low u16),
and the *rate* lives in the same word's high u16 (`>>4`, Q12; §3.2).

## 5 · Verification (what was run)

1. **`FUN_005fc6b0`'s callers walked** — 8 direct callers, 20 functions reached at depth 2
   (`log-t3-1.log`, `log-t3-5.log`); the state-machine chain `FUN_00631cd0 → FUN_00636650 →
   FUN_0063c2d0` decompiled (`log-t3-4/5/7.log`). `FUN_0060b720`'s caller set re-checked
   (1 edge: `FUN_005fc6b0`).
2. **The 30.0 fps constant cross-checked** — `0x0126b184` reads as `0x41f00000` = 30.0 and is
   referenced only from `FUN_005fc6b0` (`0x005fc858`, `0x005fcb34`); the readout disassembly quoted
   in §3.1; the other 30.0-family constants enumerated (§3.3).
3. **The frame count source confirmed against T2's samples** — §4's table (7/7 match, raw bytes).
4. **The dispatch connection** — the flag-set read (`stride 0xC`, `p2` at `+8`) and the flag-clear
   read (`motion+8+track` format, `motion+0x18+4·track` blob) both re-derived from the decompiles,
   with the selector gate quoted at instruction level (§2.3) and the loader-side agreement (§2.4).
5. **Determinism** — headless runs used `-noanalysis` on the copied project; functions decompiled
   in two different runs are byte-identical (`FUN_005fc6b0` in `log-t3-1` vs `log-t3-2`;
   `FUN_00636650` in `log-t3-4` vs `log-t3-5` — checked, bodies equal). Raw-byte reads are the
   committed stdlib script pattern (`VA→file = +0x4000`, no game runs).

## 6 · Open items (bounded, INFERRED where noted)

- **The 872 flag-set files with a `+0x08` array** — §2.5: the mechanism (direct dispatch callers
  bypass `FUN_0060b720`) is measured; the file-level rule that selects them is not (T2 §7 kept open).
- **`FUN_00636650`'s `local_16c`/`local_168`** — read (phase / frame time) but their write site was
  not exposed by the decompiler (the function is 11,980 B; the assignments are inside the body).
- **The seconds source** — the entity update's input clock was not traced; every sampler boundary
  measured already carries frames (§3.4).
- **`FUN_0067bae0`** — `(frames−1)/(x × rate)` with no direct callers found (likely indirect);
  recorded as arithmetic only.
- **The cooked/raw rate agreement** (§3.2) — INFERRED; only the raw Q12 field is file-verifiable.

## 7 · What this changes

The sampler loop is closed at the *variant* level: the flag-set 12-byte-record dispatch and the
flag-clear format-byte/blob read are both located, with their selector (`motion+0x08 == 0`), their
loader-side counterparts, and the exact element shapes per variant. The time model is pinned as
`fps = rate × 30` / `frame = seconds × rate × 30` with the rate's two branches and the constants
cross-checked; the frame count source is `motion+4` (low u16), confirmed on T2's samples. For the
converter/port the actionable statements are: sample at `frame = seconds × fps` (fps = Q12 rate ×
30, default 30), wrap by `frames−1`, blend linearly; flag-clear tracks are single quaternion curves
(format byte + descriptor), flag-set tracks dispatch on the record type with `p2` as the 3-channel
blob. `re-anb.md` remains untouched (T4).
