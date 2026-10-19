# RE-2 T1 — flag-clear `.anb` key formats end-to-end (2026-10-19)

The flag-clear layout variant (version word bit 16 clear) of BluePoint `.anb` motion banks,
decoded end to end: the file→structure→track→blob→key mapping, the per-type key format
(stride/packing/arithmetic), the descriptor application, and the key counts. This file is
**new**; it does not touch `re-anb.md` or `re2-recon.md`.

| | |
|---|---|
| Date | 2026-10-19 (run stamps UTC) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64 |
| Repo / branch | `sotc-native`, `re2-keys` (base `b3bf813` = the RE-2 recon tip) |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Ghidra | `$HOME/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC`, headless; project copy `out/re/ghidra` (`-noanalysis`, dot-free symlink invocation, memo §0) |
| Inputs | `cp3shadow/anim/player_a_stand.anb` (8,424 B), `cp3shadow/anim/horse_a_add_horse_kick.anb` (4,276 B), **all 4,455** `.anb` (2,994 flag-clear / 1,461 flag-set) + the recon's 300-file random sample (seed `20261018`: 209 flag-clear / 91 flag-set) |
| Tool | **`tools/re/anb/parse2.py`** — the flag-clear reader + the pinned assertions (`verify`) |
| Ghidra logs (gitignored) | `out/re/log-keys-decompile.log` (12 functions), `out/re/log-keys-sincos.log` (`FUN_00586060` disasm), `out/re/log-keys-sincos2.log` (import thunks) |
| Raw-byte scripts (gitignored) | `out/re/scratch-*.py` (the exploration that led here) |

**Memo status note.** This worktree's `docs/notes/re2-recon.md` is the `b3bf813` copy. The recon's
independent review (`task-recon-review.md`, re2-recon worktree) verified the memo and required four
corrections, applied in `d05aec4` on the `re2-recon` branch (not in this branch's history). The
corrected figures are used here and re-verified against the raw bytes; §5 lists the places where the
`b3bf813` copy is stale.

## 0 · The model (MEASURED)

All offsets below are **data-region-relative** (the data region starts at file `0x110`); file
offset = data offset + `0x110`.

```
file[0x110 + u32@0x10]        the structure (the loader's §1.4 patch makes this the motion struct)
  +0x00  version word (byte 0 = version; bit 16 = the flag-set variant marker)
  +0x04  low u16 = frame count        +0x08 -> per-track type byte array
  +0x0C  -> root descriptor (6 floats) +0x10 -> per-track blob pointer array (u32 x tracks)
  +0x14  track count                  +0x44 -> the first track block's keyptr slot
root region  [0x000, +0x0C): [frames x 6 B int16 triplets][6 float32]  — the root series
per track i  [keys_i][blob_i], contiguous;  keys_i = [keyptr_i, blob_i)
  keyptr_i = the pointer field inside blob_i (offset depends on the type; §1)
  count_i  = (blob_i - keyptr_i) / stride_i   (see the per-type count rules, §1)
```

Measured layout facts (full corpus, `parse2.py`-derived; 2,994 flag-clear files):

- **The tail tables `t1`/`t2` are the loader's relocation list**: `count` = `u32@0xC4` entries of
  `(location, value)` 8-byte pairs (`t1` = location, `t2` = value), and **the word at every location
  equals the value — 2,994/2,994 files**. Composition for `player_a_stand` (58 entries): 1 root
  descriptor pointer slot + 19 keyed-track keyptr words + 29 blob-array entries + 9 structure
  pointer fields (the `+0x0C`/`+0x10`/`+0x1C`/`+0x24`/`+0x28` targets and the type-array base).
- **The blob's keyptr word is the next block's first word** (the same 4 bytes serve as blob *i*'s
  keyptr and as the following block's "back pointer"): measured on both samples and re-confirmed by
  the corpus adjacency rules below.
- **Block adjacency (corpus-wide, 0 exceptions; classified by the following block)**:
  - → keyed (any predecessor): `blob_i + blob_size(type_i) == keyptr_{i+1}` — 68,113/68,113
    (62,960 keyed→keyed + 5,153 stub→keyed);
  - → stub (any predecessor): `blob_{i+1} == align16(blob_i + blob_size(type_i))` — 42,982/42,982
    (5,880 keyed→stub + 37,102 stub→stub);
  - stub (type 0) blobs are **16-byte aligned, 43,812/43,812** (zero pad words inserted as needed;
    e.g. `player_a_stand` file `0x11AC` = one pad word, `0x1518..0x1520` = two);
  - keyed blobs are *not* aligned (only 14,590/70,277 are 16-aligned) — no alignment rule.
- **Type-0xC key blocks carry up to 2 bytes of pad** before the blob (the blob is 4-aligned):
  `extent − 6 x count ∈ {0, 2}` (measured `{0: 6706, 2: 11498}` over the corpus; all other types
  `{0: all}`).
- The `.anb` version word bit 16 split: **2,994 flag-clear / 1,461 flag-set** of 4,455
  (32.8 % flag-set — matches the review's population count).

## 1 · The per-type format table (the deliverable)

`blob` = the per-track descriptor at `file 0x110 + blob_ptr[i]`; `keyptr` = the pointer field
inside it; `keys` = `[keyptr, blob)` (data-relative). Counts are corpus track counts
(2,994 flag-clear files, 70,277 keyed + 43,812 static tracks). All arithmetic below is the
engine's (`FUN_0060bba0` = the rotation accessor, `FUN_0060e0c0` = the vector accessor; §2 quotes
it verbatim).

| type | tracks | blob layout (size) | key stride | key format | descriptor application | key count |
|---|---|---|---|---|---|---|
| `0x00` | 43,812 | 4 x float32 (16 B) | — | none (static) | — | none |
| `0x02` | 0 | 3 x float32 + keyptr@`+0x0C` (16 B) | 4 B | float32 angle | axis = floats[0..2] | *unobserved*; INFERRED = frames |
| `0x03` | 16,005 | 8 x float32 + keyptr@`+0x20` (36 B) | 4 B | u32 packed, derived = **x** | offset/scale pairs f0/f1, f2/f3, f4/f5 (field order) + f6/f7 for the derived | = frames |
| `0x04` | 10,518 | 8 x float32 + keyptr@`+0x20` (36 B) | 4 B | u32 packed, derived = **y** | as above | = frames |
| `0x05` | 6,836 | 8 x float32 + keyptr@`+0x20` (36 B) | 4 B | u32 packed, derived = **z** | as above | = frames |
| `0x06` | 4,793 | 8 x float32 + keyptr@`+0x20` (36 B) | 4 B | u32 packed, derived = **w** | as above | = frames |
| `0x0C` | 18,204 | 6 x float32 + keyptr@`+0x18` (28 B) | 6 B | 3 x int16 | per-component `i16/32767*scale + offset` (f0/f1, f2/f3, f4/f5) | = frames (extent + 0/2 B pad) |
| `0x0D` | 13,887 | 5 x float32 + keyptr@`+0x14` (24 B) | 2 B | int16 scalar | `s = i16/32767*scale + offset` (f0/f1); axis = f2,f3,f4 | = frames, or frames+1 (last key = 0) |
| `0x0E` | 34 | 6 x float32 + keyptr@`+0x18` (28 B) | 4 B | u32 packed 11/10/10 + sign | `x = bits31..21/1023*scale+offset`, `y = bits20..11/511*...`, `z = bits10..1/511*...` (f0/f1, f2/f3, f4/f5) | = frames |
| `0x0B` | 0 | — | — | (only the root series has this shape; it is a *vector* series, see below) | | |

**Types 3–6 — the packed 4-component family (MEASURED).** Each key is one u32 holding three
*signed 9-bit* fields (scale `1/255`), a *signed 4-bit* residual and one sign bit; the bit windows
rotate with the case so that the three stored fields occupy the quaternion components **other than
the case's derived index** (3→x, 4→y, 5→z, 6→w), packed from the top of the word:

| case | stored fields (bit window → component) | residual bits | sign bit | derived component |
|---|---|---|---|---|
| `0x03` | bits 26..18 → y, 17..9 → z, 8..0 → w | 27..30 | 31 | `x = f6 + f7*r/7 + sign*sqrt(|1-\|v\|^2|)` |
| `0x04` | bits 31..23 → x, 17..9 → z, 8..0 → w | 18..21 | 22 | `y = ...` |
| `0x05` | bits 31..23 → x, 22..14 → y, 8..0 → w | 9..12 | 13 | `z = ...` |
| `0x06` | bits 31..23 → x, 22..14 → y, 13..5 → z | 0..3 | 4 | `w = ...` |

- The **descriptor pairs apply in *field* order**, not component order: the first stored field
  (topmost bit window) uses f0/f1, the second f2/f3, the third f4/f5, and the derived uses f6/f7.
  This is the decisive difference from the naive "pair = component index" reading: on `player`
  track 24 (case 4) the two readings differ at component w, and only the field-order reading is
  continuous (dot min 0.9997 vs −0.42). The derived index also means the descriptor's *derived
  pair* is the one that goes with the case's omitted component.
- The derived term is `f6 + f7*residual/7 +/- sqrt(|1-|v_stored|^2|)`; the sqrt term is **skipped**
  when `|v_stored|^2 > 1` (the engine's guard — the absolute-value root is computed but not used);
  the sign bit set → negative (the sign table at `0x0126b8e0` = `{+1.0, -1.0}`).
- The residual refines the derived component: e.g. `player` track 0 key 0 (`0x033A4127`: residual 0,
  sign 0) decodes to x = +0.2260 (`f6` 3.4e-6 + `sqrt(1-|v|^2)` 0.225947) — the full key is
  (0.225950, -0.643605, -0.717296, 0.142150); the residual/7 term is bounded by `f7` (typically
  ~1e-3).

**Type `0x0C` (MEASURED).** `v = (i16_x/32767*f1 + f0, i16_y/32767*f3 + f2, i16_z/32767*f5 + f4)`,
`w = ±sqrt(|1-|v|^2|)` with the sign = the **LSB of the first int16** (set → negative; the LSB is
part of the sign encoding, not of x — the values are otherwise byte-identical). The stored `|v|`
spans the full range (median 0.519, p90 0.992; only 22.1 % of sampled keys have `|v| > 0.9`) and the
derived `|w|` median is 0.855 — the derived component is typically the *largest* (measured; there is
no fixed "omit the smallest component" convention).

**Type `0x0D` (MEASURED).** `s = i16/32767*f1 + f0`; `theta = s * 0.5`; quaternion =
`(f2,f3,f4)*sin(theta)` with `w = cos(theta)`. The axis is a **unit vector in 13,886/13,887
corpus tracks** (one degenerate zero-axis track). `s` spans the full angle circle (`|f0|` median
4.77, max 6.27; per-track `max|s|` median 5.02 rad, only 23.4 % of the 13,887 tracks ≤ π/2), and
continuity does not discriminate sin from cos (both readings give identical dots — verified on
`player_a_stand` t2), so the assignment is **INFERRED** (see §3): the disassembly pins the helper
structure (`f(x,1)` → w, `f(x,0)` → vector) but not its name, and the reading rests on the
standard sincos-quaternion form — the alternative is the nonstandard π − s mapping.

**Type `0x0E` (MEASURED, 34 tracks).** One u32: `x = sext(bits 31..21, 11)/1023*scale + offset`,
`y = sext(bits 20..11, 10)/511*...`, `z = sext(bits 10..1, 10)/511*...`, bit 0 = the w sign.
All 34 tracks are the `yamori_b_add_tail_{left,right}_{start,loop,end}` family; `|v|` ∈
[0.0145, 0.3787] (w down to ≈0.925) and every adjacent dot ≥ 0.9992. The sign bit is 0 in all 374
keys of the corpus, so the sign convention is INFERRED from the decompile (set → negative).

**Type `0x00` (MEASURED).** 4 x float32 = a **static unit quaternion** (x,y,z,w); norm within
4.1e-08 of 1 across all 43,812 tracks. No keys, no pointer field (the engine's `FUN_0066dec0` has
no case for it).

**Type `0x02` (INFERRED — no flag-clear track-type instance in all 2,994 flag-clear files).** From
`FUN_0060bba0` case 2 and `FUN_0066dec0` case 2 only: blob = 3 x float32 (axis) + keyptr@`+0x0C`;
keys = float32 angle at 4-byte stride; quaternion = `axis*sin(angle)`, `w = cos(angle)` (the same
helper as 0xD). The per-type key count is expected to be `frames` (unverifiable — no data). Flag-set
files *do* carry record types 0x02/0x0B (e.g. `objanim_arena_env.anb`,
`objanim_arrow_eff_hit_water.anb`) — they have no track-type array (T2 scope).

**The root series (MEASURED values + INFERRED consumer).** When structure`+0x0C` ≠ 0, the
pre-region `[frames x 6 B int16 triplets][6 float32]` decodes as a **vector (position) series**,
not a rotation: `value_c = i16/32767 * scale_c + offset_c` with the pairs (f0,f1), (f2,f3), (f4,f5)
— the arithmetic of the engine's *vector* accessor `FUN_0060e0c0` format 2 (6-byte int16 triplets,
§2). Corpus-wide (2,994 flag-clear files): **1,613 carry a root series, 1,381 have descriptor
offset 0** (no root series); for every present one, `descriptor_offset = 6 x frames + pad` with
`pad ∈ {0, 2}` (the same 4-byte alignment pad as the 0xC key blocks) — 0 violations. Evidence for
the reading: the descriptor's shape is that accessor's `param_4` (floats[0..5] + a pointer slot at
`+0x18` — the structure's `+0x0C` target, whose `+0x18` the fixup re-bases), and the *rotation*
reading fails on the horse (`|v|` = 1.37–1.40 > 1 → the derived w would be invalid) while the
position reading gives plausible roots (player ≈ (0.001..0.017, 0.743..0.747, −0.058..0.008),
horse ≈ (0, 1.370..1.402, 0.0593) with x/z constant — a root at hip height). The descriptor's
pointer slot is **0 in both samples** (the keys are inline before the descriptor; the relocation
list still carries the slot, entry `(0x1E0, 0)`), so the engine's sample base for the root is
**open** — recorded as decoded-but-consumer-open.

## 2 · Verbatim evidence

### 2.1 The engine constants (read from `out/re/eboot.elf`, VA→file `+0x4000`)

| VA | value | role |
|---|---|---|
| `0x0126b2ac` | `3.051850945e-05` = 1/32767 | int16 scale (0xB/0xC/0xD) |
| `0x0126b2b0` | `0.5` | 0xD scalar → angle (`s * 0.5`) |
| `0x0126b2b4` | `9.775171056e-04` = 1/1023 | 0xE x scale (11-bit) |
| `0x0126b2b8` | `1.956947148e-03` = 1/511 | 0xE y/z scale (10-bit) |
| `0x0126b2bc` | `1.0` | the "1" in `1 - |v|^2` |
| `0x0126b2c0` | `3.921568859e-03` = 1/255 | the 9-bit field scale (3–6) |
| `0x0126b2c4` | `0.1428571492` = 1/7 | the residual scale (3–6) |
| `0x0126b8e0/e4` | `{+1.0, -1.0}` | the derived-component sign table |
| `0x0126b2dc` | 1/32767 | vector accessor int16 scale (root) |
| `0x0126b2e4/e8/ec` | 1/2047, 1/1023, 1/31 | vector accessor packed scales |

### 2.2 `FUN_0060bba0` — the rotation accessor (decompiled, `out/re/log-keys-decompile.log`)

Signature: `long FUN_0060bba0(float *out, uint format, float *desc, int key)`.

Case `3` (verbatim, condensed to the arithmetic):

```c
lVar12 = (base if desc[8] < 0) + ABS(desc[8]);            /* the key array */
fVar15 = (float)((*(int *)(lVar12 + key * 4) << 5) >> 0x17) * desc[1] * 1/255 + desc[0];
param_1[1] = fVar15;                                       /* y = bits 26..18 */
fVar21 = (float)((*(int *)(lVar12 + key * 4) << 0xe) >> 0x17) * desc[3] * 1/255 + desc[2];
param_1[2] = fVar21;                                       /* z = bits 17..9 */
fVar5  = (float)((*(int *)(lVar12 + key * 4) << 0x17) >> 0x17) * desc[5] * 1/255 + desc[4];
param_1[3] = fVar5;                                        /* w = bits 8..0 */
fVar21 = 1.0 - (fVar15*fVar15 + fVar21*fVar21 + fVar5*fVar5);
... blendvps/abs ... DAT_02f11400 = SQRT(|1 - |v|^2|);
if (0.0 <= fVar21) fVar15 = DAT_02f11400 * table[(iVar4 < 0)];   /* table = {+1,-1}, bit 31 */
*param_1 = (float)(iVar4 * 2 >> 0x1c) * desc[7] * 1/7 + desc[6] + fVar15;
```

Case `4` (bits 31..23 → out[0], 17..9 → out[2], 8..0 → out[3], residual `(word<<10)>>28`,
sign bit 22, derived → `out[1]`); case `5` (31..23, 22..14, 8..0; residual `(word<<19)>>28`,
sign bit 13, derived → `out[2]`); case `6` (31..23, 22..14, 13..5; residual `(word<<28)>>28`,
sign bit 4, derived → `out[3]`). The three stored fields always take descriptor pairs
`(desc[0],desc[1])`, `(desc[2],desc[3])`, `(desc[4],desc[5])` in bit-window order and the derived
component `desc[6] + desc[7]*residual/7 ± sqrt`.

Case `0xc` (verbatim):

```c
uVar3 = *(ushort *)(lVar12 + key * 6);
fVar21 = (float)(int)(short)uVar3 * desc[1] * 1/32767 + desc[0];
*param_1 = fVar21;
fVar5  = (float)(int)*(short *)(lVar12 + 2 + key * 6) * desc[3] * 1/32767 + desc[2];
param_1[1] = fVar5;
fVar15 = (float)(int)*(short *)(lVar12 + 4 + key * 6) * desc[5] * 1/32767 + desc[4];
param_1[2] = fVar15;
...
param_1[3] = SQRT(|1 - |v|^2|) * (float)(int)(((uVar3 & 1) * 2 ^ 2) - 1);   /* LSB -> ±1 */
```

Case `0xd` (verbatim):

```c
FUN_00586060(((float)(int)*(short *)(lVar12 + ABS(desc[5]) + key * 2) * desc[1] * 1/32767
              + desc[0]) * 0.5, &local_38);
fVar15 = desc[2]; fVar21 = desc[3]; fVar5 = desc[4];
*param_1 = fVar15; param_1[1] = fVar21; param_1[2] = fVar5;
if (local_34 != 0.0) { *param_1 = local_34*fVar15; param_1[1] = local_34*fVar21; param_1[2] = local_34*fVar5; }
param_1[3] = local_38;                     /* w = local_38 (out[0] of the helper) */
```

Case `0xe` (verbatim):

```c
fVar15 = (float)(*(int *)(lVar12 + key * 4) >> 0x15) * desc[1] * 1/1023 + desc[0];
*param_1 = fVar15;                                          /* x = bits 31..21 */
fVar5  = (float)((*(int *)(lVar12 + key * 4) << 0xb) >> 0x16) * desc[3] * 1/511 + desc[2];
param_1[1] = fVar5;                                         /* y = bits 20..11 */
fVar21 = (float)((*(int *)(lVar12 + key * 4) << 0x15) >> 0x16) * desc[5] * 1/511 + desc[4];
param_1[2] = fVar21;                                        /* z = bits 10..1 */
uVar9 = ~(word * 2) & 2;   /* bit 0 of the word */  ... fVar16 = sqrt(|1-|v|^2|) * (uVar9 - 1);
```

Case `2` (verbatim): `FUN_00586060(*(undefined4 *)(lVar12 + ABS(desc[3]) + key * 4), &local_38);`
then `out[0..2] = desc[0..2] * local_34`, `out[3] = local_38`.

**`FUN_00586060` (the helper used by cases 2 and 0xD)** — disassembly (`0x00586060`):

```
MOV RBX,RDI; MOV EDI,0x1; XOR ESI,ESI; VMOVSS [RBP-0xc],XMM0; CALL 0x00f8fd10; VMOVSS [RBX],XMM0
XOR EDI,EDI; XOR ESI,ESI; VMOVSS XMM0,[RBP-0xc]; CALL 0x00f8fd10; VMOVSS [RBX+0x4],XMM0
```

i.e. `out[0] = f(x, 1)`, `out[1] = f(x, 0)` for an imported math helper `f(float, int)` (the import
thunks at `0xf8fd10..0xf8fd30` all resolve through `DAT_015fffb0`). Since case 0xD puts `out[1]`
on the vector and `out[0]` on w, the reading **vector = sin, w = cos** requires `f(x,0) = sin`,
`f(x,1) = cos` (see §3).

### 2.3 `FUN_0066dec0` — the flag-clear per-track pointer re-base (verbatim switch)

```c
switch (param_1) {          /* param_1 = the track type */
  case 2:   uVar1 = *(uint *)(lVar2 + 0xc);  ... *(uint *)(lVar2 + 0xc) = uVar1 | 0x80000000;
  case 3: case 4: case 5: case 6:  uVar1 = *(uint *)(lVar2 + 0x20); ... + 0x20 ...
  case 0xc: case 0xe:  uVar1 = *(uint *)(lVar2 + 0x18); ... + 0x18 ...
  case 0xd: uVar1 = *(uint *)(lVar2 + 0x14); ... + 0x14 ...
}
```

This is the evidence for the per-type **keyptr field offsets** (2 → `+0x0C`, 3–6 → `+0x20`,
0xC/0xE → `+0x18`, 0xD → `+0x14`) and for the *absence* of a case 0/0xB (no pointer to re-base).

### 2.4 `FUN_0060e0c0` — the vector accessor, format 2 (the root descriptor application)

```c
else if (param_3 == 2) {
  lVar4 = (base if param_4[6] < 0) + ABS(param_4[6]);
  *param_2 = (float)(int)*(short *)(lVar4 + key * 6) * param_4[1] * 1/32767 + *param_4;
  param_2[1] = (float)(int)*(short *)(lVar4 + 2 + key * 6) * param_4[3] * 1/32767 + param_4[2];
  param_2[2] = (float)(int)*(short *)(lVar4 + 4 + key * 6) * param_4[5] * 1/32767 + param_4[4];
}
```

(The accessor also blends two keys — `param_1` = fraction — and writes `out[3] = 1.0`; the root
series' samples base `param_4[6]` is the open item above.)

## 3 · MEASURED vs INFERRED

**MEASURED (re-derivable from the bytes + the decompiles):**

- the structure mapping, the block layout, the relocation tables, the padding rules (§0) — corpus
  counts 2,994/2,994 and the adjacency rules with 0 exceptions;
- the per-type blob layouts, keyptr offsets (decompile §2.3 + corpus adjacency), strides, and key
  counts (§1) — 0 exceptions over the corpus;
- the arithmetic for types 3–6 (field windows, field-order descriptor pairs, residual, sqrt guard),
  0xC, 0xD, 0xE — the decompile + the smoothness/continuity checks below;
- the type-0 static quaternion (unit norm, 43,812 tracks);
- the count rules: `count = frames` for 3–6/0xC/0xE (100 %); `0xD ∈ {frames, frames+1}` with the
  frames+1 last key **= 0** (7,398/7,398); `0xC` extent pad ∈ {0,2}.

**INFERRED (recorded, not data-testable on this corpus):**

- **type 0x02** — no flag-clear track-type instance (all 2,994 flag-clear files); layout/format
  from the decompile only (flag-set files do carry record type 0x02 — T2 scope);
- the **0xD / 0x2 sin-vs-cos assignment** — the disassembly pins the *structure* (`f(x,1)` → w,
  `f(x,0)` → vector, θ = s/2) but not the helper's name (an import thunk); `s` spans the full
  circle (per-track max&#124;s&#124; median 5.02 rad; only 23.4 % ≤ π/2) and both readings give
  identical dots, so continuity does not discriminate. The *vector = sin* reading rests on the
  standard sincos-quaternion form (the alternative is the nonstandard π − s mapping); a
  rotation-magnitude naturalness check does **not** settle it — the sample is mixed (8 of the 13
  sample 0xD tracks sit near 0/2π, where sin gives a 4–46° rotation, but
  `horse_a_add_horse_kick` t1 sits at `s` ≈ π, where the alternative gives ≈ 0, and t4/t5/t15/t19
  give 71–84° under sin);
- the **sign conventions** (`set → negative` for 3–6/0xC/0xE) — decompile + the sign table
  constant; **not discriminable from continuity alone**: the sign bit flips mark zero *crossings* of
  the derived component between frames (measured on `player_a_jump_run` tracks 1/5: the flips sit
  at `sqrt(1-|v|^2)` values 0.000/0.233 and 0.041/0.420/0.206/0.055 — up to 0.42, not ≈ 0), and the
  two conventions differ only by a global mirror of the derived component about the tiny base term,
  so convention-sensitive adjacent pairs differ by ≤ 0.004 dot (e.g. t1 8→9: 0.9377 vs 0.9413) and
  both give continuous tracks;
- the **root series' consumer** — the vector-accessor shape and arithmetic are pinned; the sample
  base (the `[6]` slot = 0) is open;
- the **0xE sign bit** — 0 in all 374 corpus keys.

## 4 · Verification (the tool's pinned assertions)

`python3 tools/re/anb/parse2.py verify [--full]` — exit non-zero on failure; `--full` scans all
4,455 `.anb`, the default scans the 300-file sample (seed `20261018`) + the two pinned samples.
Measured on `--full` (all assertions PASS, 30/30; log re-runnable):

| assertion | scope | measured |
|---|---|---|
| adjacent-key dots > 0.9 (all pairs) | `player_a_stand` (76-key tracks) | **min 0.9800**, 0/1425 pairs below |
| adjacent-key dots > 0.9 (≥ 95 % of pairs) | `horse_a_add_horse_kick` | 98.33 % (7/420 below; min 0.6711 — the kick's strike wraps) |
| adjacent-key dots > 0.9 (≥ 95 % of long-track pairs) | corpus, per type, tracks ≥ 64 keys | 03: 99.18 %, 04: 99.38 %, 05: 96.56 %, 06: 98.78 %, 0C: 99.18 %, 0D: 99.69 %; medians 0.9999–1.0000 |
| 0xE every adjacent dot > 0.9 | corpus (34 tracks) | 0/340 below |
| first == last on looped tracks | corpus | 14,082 looped tracks (identical first/last key bytes) decode to equal keys — 0 violations; `player_a_stand`: every keyed track looped |
| type-0xD scalar smoothness | `player_a_stand` | mean&#124;Δ&#124;/range ≤ 0.0507 |
| type-0xD scalar smoothness | corpus median / p90 | 0.0812 / 0.2222 |
| descriptor-normalized norms ≤ 1 | corpus (3–6/0xC/0xE stored parts) | max overshoot **2.67e-03** (quantization; the derived term's sqrt is skipped past 1) |
| type-0 static quaternion norms | corpus | max &#124;norm−1&#124; = 4.08e-08 |
| key count / extent / sentinel rules | corpus | §1; 0 exceptions |
| root series present => descriptor offset = 6 x frames (+0/2 pad) | corpus | 1,613 present / 1,381 absent; 0 violations |

**The dot-outlier classes (measured, not gate failures):** pairs below 0.9 cluster in
(a) *fast/wrapped segments* — e.g. `horse_a_add_horse_kick` tracks 28/29 (the kick's strike: the
stored 9-bit fields swing to their −256..255 limits and wrap within the field window between
frames, so the decoded quaternions jump 20–48°/frame for a few frames, then settle);
(b) *cut clips* — `obj_bird_*_demo_end08_cut0{20,22}.anb` alone account for the majority of the
corpus's type-5 outliers (a cut teleports the pose by design). The long-track gates keep ≥ 95 %
because these are localized bursts, not random noise.

## 5 · Discrepancies with the recon memo (`b3bf813` copy)

1. **The pre-region reading** (memo §2.3: "int16 triplets, the §6.3 `0xB` shape … dots
   0.9734..1.0000"): the triplets are the root's **position** series (vector accessor, §1), not a
   rotation. The 0xB-style quaternion reading reproduces the memo's dot numbers (0.9734 with
   `w = +sqrt`) but **fails on the horse** (`|v|` = 1.37–1.40 > 1 → the derived w is invalid under
   the engine's guard), while the position reading fits both samples and the descriptor's shape.
   The review's note (b) asked T1 to absorb or record this — it is decoded here, with the sample
   base open.
2. **The type-3 example series** (memo §2.2, `b3bf813`): the memo's lo16 list had 3 wrong values of
   16; the file's series is `16679 16166 15139 13087 11034 8468 6414 4361 2309 1282 769 1282 2822
   5901 10520 16679` (re-derived here; matches the review's correction, applied in `d05aec4`).
3. **The "high half carries the other components"** (memo §2.2): the other components are **not**
   the high int16 — they are the 9-bit windows crossing the int16 boundary (bits 26..18 and 17..9);
   the raw hi16 words are not smooth (`826 2878 31566 4962 …`) while the 9-bit fields are.
4. The memo's §2.2 "the extent between consecutive key pointers is the current best reading" — the
   count rule is now pinned per type (§1), including the 0xC pad and the 0xD sentinel.

## 6 · Reproduction

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
python3 tools/re/anb/parse2.py dump  cp3shadow/anim/player_a_stand.anb
python3 tools/re/anb/parse2.py keys  cp3shadow/anim/horse_a_add_horse_kick.anb --json out/re/keys-horse.json
python3 tools/re/anb/parse2.py root  cp3shadow/anim/player_a_stand.anb
python3 tools/re/anb/parse2.py verify            # 300-file sample + the two samples (~1 s)
python3 tools/re/anb/parse2.py verify --full     # all 4,455 .anb (~10 s)
```

The Ghidra evidence (verbatim decompiles/disassembly of `FUN_0060bba0`, `FUN_0060e0c0`,
`FUN_0066dec0`, `FUN_00586060`) is reproducible with the memo §0 headless invocation
(`FindSampler.java decompile 0x0060bba0 0x0060e0c0 0x0066dec0 0x00586060`, `Disasm.java 0x00586060
0x005860a0`).
