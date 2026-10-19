# RE-2 T2 — the flag-set `.anb` variant: `{type, p1, p2}` records + the sampler's per-type dispatch (2026-10-19)

T2 of the RE-2 cycle (recon memo `docs/notes/re2-recon.md` §6, T2): pin the **flag-set** variant's
record semantics (`{type, p1, p2}`) and the **keyframe sampler's per-type dispatch** (types `1…0x30`),
then decode a type-13 track end-to-end. Everything below is measured with the committed tool
(`tools/re/anb/records.py`) against the read-only game tree and with Ghidra 12.1.4 headless against
the copied RE-0 project; the recon memo is the foundation and its §1.6 / §2 / §3.3 readings are
re-verified here where they touch this task. The memo was under verification while this pass ran;
the review (VERIFIED, with corrections; fix commit `d05aec4`) was checked before finalizing — the
one premise that touches T2 is corrected further below (§2.2).

| | |
|---|---|
| Date | 2026-10-19 (run stamps UTC) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64; `python3` 3.12 (stdlib only) |
| Repo / branch | `sotc-native`, `re2-flagset` (base `b3bf813` = main at branch time) |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Tool | **`tools/re/anb/records.py`** — `dump <file>`, `decode <file> --track N`, `scan <pat>... [--limit N]`, `verify` (pinned assertions, exit 0/1) |
| Ghidra project (copy) | `out/re/ghidra` (copy of the recon's project), program `eboot.elf`, image base 0, 38,845 functions; VA→file = `+0x4000` |
| Decompile logs (this worktree) | `out/re/log-flagset-1.log` (`FUN_005fd900`, the three accessors, `FUN_00586060`, `FUN_005fc5d0`), `log-flagset-2.log` (`FUN_0066f9a0`, `FUN_0066fe60`, `FUN_0060b720`, `FUN_00600740`), `log-flagset-3.log` (`FUN_005fc6b0`, `FUN_00600810`, `FUN_00600d70`, `FUN_0066ecd0`, `FUN_0066e0d0`, `FUN_0066dfc0`, `FUN_0066e1b0`) |
| Tool outputs (gitignored) | `out/re/flagset-scan.json`, `flagset-verify.json`, `decode-cerberus-t0.json` |
| Samples | `objanim_leo_a_jump_fall.anb` (v8, 21 f, 15×t13 — the recon's sample), `objanim_cerberus_a_atk_near_l.anb`, `objanim_ath_damage.anb`, `objanim_tree_down_xm.anb`, the 453 `camera_*.anb`, + the full 4,455-file population |

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
python3 tools/re/anb/records.py verify                                    # 19/19 PASS, exit 0
python3 tools/re/anb/records.py scan 'cp3shadow/anim/*.anb' --json out/re/flagset-scan.json
python3 tools/re/anb/records.py dump cp3shadow/anim/objanim_leo_a_jump_fall.anb
python3 tools/re/anb/records.py decode cp3shadow/anim/objanim_cerberus_a_atk_near_l.anb --track 0
```

## 1 · The flag-set record array (MEASURED)

The variant is bit 16 of the motion structure's version word (`motion = file[u32@0xB4 + u32@0x10]`,
data base `u32@0xB4` = `0x110` in every shipped file — recon §1.4/§2.1, re-derived). Full population:
**4,455/4,455 `.anb` parse; 1,461 flag-set (32.8%), 2,994 flag-clear, 0 parse errors**
(`flagset-scan.json`; the memo's 1,461/32.8% reproduced exactly).

For flag-set files the loader's structure walk (`FUN_0066fe60`) dispatches on that bit and walks
**`motion+0x14` records at `motion+0x10`, stride 0xC**, each through `FUN_0066f9a0`
(decompile: `out/re/log-flagset-2.log`; recon §1.5's step 4 re-verified verbatim):

```c
/* FUN_0066f9a0(record, frames, version) — the flag-set per-record fixup */
uVar2 = param_1[1];            /* record+0x04 = p1 */
if ((p1 >= 0) && ((p1 & 0x7fffffff) != 0) && (p1 + base < limit)) param_1[1] = p1 | 0x80000000;
uVar2 = param_1[2];            /* record+0x08 = p2 */
if (same) param_1[2] = p2 | 0x80000000;
switch (*param_1) { … }        /* the record type selects a per-type blob walk */
```

**Both record pointers are re-based as 31-bit pointers, for every type** — the record is a
`{u32 type+flags, u32 p1, u32 p2}` group at the file level, and file offsets resolve as
`data_base + (v & 0x7fffffff)` (bit 31 is only the load-time "was base-relative" marker).

Two loader details measured this pass that refine the memo's §1.5 (the memo's wording predates the
flag gate — recorded here as a refinement, not a contradiction):

- the **version > 3 `+0x24`** and **version > 8 `+0x3C`** passes are gated on
  `(version_word & 0x10000) == 0` — i.e. **flag-clear only** (decompile of `FUN_0066fe60`:
  `if ((3 < (byte)uVar3) && ((uVar3 & 0x10000) == 0) && …)`). Flag-set files skip them;
- the **version > 7 `FUN_0066e1b0(&structure+0x2C)`** pass runs for **both** variants. On a
  flag-set file the structure is only `0x20` bytes (it abuts tail table 1: `0x6DC..0x6FC` for
  `objanim_leo_a_jump_fall`), so this pass reads the first tail-table words as a `{word0, ptr, ptr}`
  block. Measured consequence: for the leo file `FUN_0066e1b0` would take its `byte0 ∈ {2,3}` branch
  (`+0x2C` = `0x102` → byte 2) and re-base `resolve(+0x34)[0x18]` — i.e. it walks *table* data.
  Recorded as **measured mechanism, open consequence** (the file-level reader never depends on it).

**The `+0x08` auxiliary array (position and length MEASURED; meaning OPEN).** For the 872/1,461
flag-set files with `motion+0x08 != 0` the field points at `records_end` exactly
(`+0x08 == +0x10 + 12·tracks` in **872/872** measured) and holds `tracks` u32s; the motion structure
follows immediately after (`+0x08 + 4·tracks`). Observed values: `0xFFFFFFFF` (789 keys in 536
files, of 3,928 aux keys over 872 files),
`0x00010004`/`0x00010005`/`0x00010007` (camera files), and small values (`0..20`, `25..29`, e.g. the
leo file's `27, 26, 25, 29, 28, 27, 27, 26, 26, 29, 29, 28, 28, 25, 25`). No runtime reader was
found that consumes it as u32-per-track (the two readers of `motion+0x08` found — `FUN_0060b720`
path B and `FUN_00600810`'s fallback — read it as a **byte** per track, which is the flag-clear
"type byte array" shape). Meaning OPEN; the reader records position/length only. The
`+0x04` high u16 is likewise flag-ish (`0` in 1,005/1,461; `0x8000` 270×; `0x4000` 34×; other values
≤12×) and is never read (the sampler reads only the low u16 through `FUN_005fc5d0`).

## 2 · The record-role table (MEASURED)

| field | width | role | evidence |
|---|---|---|---|
| `+0x00` | u32 | **type** (low byte, `1..0x30`; types `8` and `>0x30` fall to the sampler's no-op default) + flag bits: bits 16..19 / 8..11 / 0..3 are the **per-channel format selectors** of the p2 blob's three accessor calls (read out per case body, §4); bits 24..29 are flags some bodies read back (e.g. types 1/5: `out[0x11] = (word0>>0x18)&1`) | `FUN_005fd900` switch on `*out - 1`; `FUN_0066f9a0` switches on `*record`; `FUN_0066ecd0` reads `(word0 & 0xf0000) == 0x20000` and `word0 & 0xf` |
| `+0x04` | u32 | **p1 — a 31-bit data-region pointer to a NUL-terminated name string** (for the object types; `p1 = 0` = the string at data offset 0). The **sampler never reads p1** — it is presumably consumed by the object-animation caller side (the name of the effect/object the track drives), but **no reader was located: INFERRED** (the review decompiled all nine `FUN_005fd900` callers; none reads `record+4` in the sampled paths — `FUN_006694c0` iterates records at stride 0xC but reads only the type word) | corpus: **198/198 type-13 tracks resolve to a printable NUL-terminated string**; the leo file's 15 strings tile the data region from 0 and end exactly where the key data begins; `FUN_0066f9a0` re-bases it; a full grep of `FUN_005fd900`'s decompile has no `record+4` read |
| `+0x08` | u32 | **p2 — a 31-bit data-region pointer to the track's key blob** (`{format word, A, B, C}`, §3). This is the sampler's only per-record input | `FUN_005fd900`: `*out = *(int *)(records + track*0xC)`, then `records + 8 + track*0xC` for every case body; `FUN_0066ecd0` (the type-13 loader walk) re-bases words 1 and 3 and the descriptors' pointers |

### 2.1 The sampler's read of the record (measured, verbatim from the decompile)

```c
uVar19 = (sign(motion+0x18)) ? DAT_02f3b8c0 : 0;      /* the load base */
lVar24 = uVar19 + (*(uint *)(motion + 0x18) & 0x7fffffff);   /* runtime +0x18 = disk +0x10 */
*param_3 = *(int *)(lVar24 + param_5 * 0xc);          /* the record type */
switch (*param_3 + -1) { … }                          /* the dispatch of §4 */
```

(runtime `motion+0x18` = disk `+0x10` through `FUN_0066fe60`'s `{+0x10,+0x14,+0x18}` rotation —
recon §1.5 step 1, re-derived; the frame count is the low u16 at `motion+4` via `FUN_005fc5d0`.)

### 2.2 The p1 discrepancy — the memo is corrected further (RECORDED)

The recon memo's original `p1 = k × frames` ("key-index base") is **refuted** — the verification
review corrected it to "a monotone key-index base whose increments vary per track" (`d05aec4`).
This pass measures the actual role: **p1 is the data-region offset of a NUL-terminated name
string**. The "increments" are exactly the string lengths (+1), the strings tile the data region
from 0, and `objanim_leo_a_jump_fall`'s first six values coincide with `k × 21` only because its
first five names are 21 bytes long (`"CERBERUS_RUSH_IMPACT\0"`); from k=6 the "base" diverges
(124 = 105+19, i.e. `"SIRIUS_RUSH_DAMAGE\0"`). The review's own example reproduces it:
`objanim_tree_down_xm.anb` → p1 `0, 10, 20, 31` = `"tree_base"` (10 B), `"TREE_DOWN"` (10 B),
`"TREE_SWING"` (11 B), `"rootnode_tree_lws"` — string lengths, not key indices. The `p1 = k×frames`
pattern "fits" a file only when the names happen to be frame-count-sized (the review's 44/92
statistic). **Corpus-wide: 198/198 type-13 tracks have a printable NUL-terminated string at p1
(including `p1 = 0` → the string at data offset 0).**

Caveat, measured: the p1-name role is **type-specific**, with a measured exception set. The camera
types (36/37, 453 files) carry `p1 = 0` (the 453 type-36 records) / `p1 = 1` (the 8 type-37
records) and their data offsets 0/1 are binary (floats/zeros) — `c_string()` returns nothing there,
so the tool reports no name. Beyond the camera types, **types 1/2/11 have 33 non-resolving p1
records** (type 1: 6/2,960; type 2: 24/1,056; type 11: 3/847), in two classes: names in the game's
**font encoding** (high-bit bytes, e.g. `objanim_boss_a_boot` t4 p1 = 0x24 → `proto_` + high-bit
bytes; `objanim_leo_a_jump` t0 → `97 8e 89 ba …`), and the **yamori cluster**
(`objanim_yamori_b_back_walk1` / `_pre_struggle_1` / `_2`: type-2 p1 = 0, 1, 2, 3 pointing into NUL
padding while the strings `YAM_FOOT` start at offset 4 — recorded as an open item, §7). Type 13
(and most `objanim_*` object types) is where the name reading holds corpus-wide (198/198). One
detail is deliberately left unsettled: `p1 = 0` is the first string's offset (the tiling starts at
0), but the loader's re-base condition excludes `(v & 0x7fffffff) == 0` (the value stays 0), so a
generic `(sign ? base : 0) + v` consumer would read it as **null** — whether the name consumer
treats 0 as "the first name" or as "none" is not settled here (the sampler never reads p1 either
way; the tool resolves 0 to the string at offset 0 because that is what the measured offsets say).

## 3 · The key blob and the three accessors (MEASURED)

`p2` → the blob; the loader (`FUN_0066ecd0`, the types 10/13/14/17/22/23/32/39–42/46 family) walks it:

```c
/* FUN_0066ecd0(blob, frames) — the caseD_9-family loader walk */
if (re-basable(blob[1])) {                       /* A */
  blob[1] |= 0x80000000;
  if (re-basable(blob[3])) {                     /* C */
    blob[3] |= 0x80000000;
    if ((blob[0] & 0xf0000) == 0x20000)          /* e0c0 format 2 → A[6] is a pointer */
      re-base(resolve(A) + 0x18);
    FUN_0066e0d0(blob[0] >> 8 & 0xf, blob + 2, frames);   /* the B channel walk */
    if ((byte)blob[0] & 0xf == 2 || == 3)        /* e640 format 2/3 → C[6] is a pointer */
      re-base(resolve(C) + 0x18);
  }
}
```

So the blob is `{u32 format word, u32 A, u32 B, u32 C}` with the format nibbles assigned
**A = bits 16..19 (FUN_0060e0c0), B = bits 8..11 (FUN_0060bba0), C = bits 0..3 (FUN_0060e640)** —
exactly what `FUN_00600ef0` (the type-13 case body) reads. The three accessors (exact arithmetic
from the decompiles; scale constants read from the binary at the quoted VAs):

| accessor | channel | formats (measured vocabulary) |
|---|---|---|
| `FUN_0060e0c0` | vector (w := 1) | `0` raw 3 floats at the descriptor; `2` 6 offset/scale floats + int16 triplets at `desc[6]` (6-byte stride, `1/32767` = `0x0126b2ac`/`0x0126b2dc`); else float triplets at `desc + key*12` |
| `FUN_0060bba0` | quaternion | `0` raw 4 floats; `1` 4 floats at 16-byte stride + derived `w` (sign = byte 1's MSB); `2` axis `desc[0..2]` × (cos,sin) of the float at `desc[3]` + `key*4`; `3..6` packed u32 at `desc[8]` (9-bit fields, 4-bit residual × `1/7` = `0x0126b2c4`, derived slot = `fmt-3`); `0xb` int16 triplets at the descriptor (6-byte stride) + derived `w` (sign = bit 0 of int16₀); `0xc` like `0xb` but at `desc[6]` with offset/scale; `0xd` scalar angle at `desc[5]` (2-byte stride, ×`1/32767`, offset/scale, then ×`0.5` = `0x0126b2b0`) → axis-angle; `0xe` packed u32 at `desc[6]` (11-bit x × `1/1023` = `0x0126b2b4`, 10-bit y/z × `1/511` = `0x0126b2b8`, **arithmetic shifts**, sign = bit 0); `>0xe` zeros |
| `FUN_0060e640` | vector | `0` raw 3 floats; `2` packed u32 at `desc[6]` (11/10/10 bits, `1/2047` = `0x0126b2e4`, `1/1023` = `0x0126b2e8`); `3` packed u16 at `desc[6]` (5/6/5 bits, `1/31` = `0x0126b2ec`); else float triplets at `desc + key*12` |

Two measured pins worth recording:

- **`FUN_00586060` returns `(cos, sin)`** — the calls are `f(1,0,x) → out[0]`, `f(0,0,x) → out[1]`
  (both target the same PLT stub; `out[0]` lands in the quaternion's `w` slot). Data evidence: on
  the near-zero-angle tracks (`objanim_ruins_polec_swing.anb` t12, angle range
  `[0.0000024, 0.003959]` rad — the first ~6 keys are ≤ 0.0003) the
  `out[0]=cos` reading gives `w ≈ 1.0000` (an identity-ish quaternion) while `out[0]=sin` would give
  `w ≈ 0.0000` (a 180° flip). So the 0xd/2 quaternions are the standard `(axis·sin(θ/2), cos(θ/2))`.
- **Derived components are `±sqrt(|1−|v|²|)` with the engine's own guard**: when `1−|v|² < 0` the
  derived part is **0** (not NaN, not normalized) — measured in all packed formats.

## 4 · The sampler's per-type dispatch (MEASURED)

`FUN_005fd900` ("GetObjectAnimationFloatFrame") — the full switch on `type − 1`, from
`out/re/log-flagset-1.log`. All case bodies read **only `p2`**; none reads `p1`. The table is
committed as `DISPATCH` in `tools/re/anb/records.py` (same content):

| type(s) | case | handler / body (measured) |
|---|---|---|
| 1 | `case 0` | `FUN_0060ce90(t, out+0x20, out+0x10, out+0x30, p2, frames)`; flags from `blob word0 >> 0x18` (`OBJANIMTYPE_OBJECT`) |
| 2, 11 | `caseD_1` | `FUN_00602040(t, out, out+0x10, p2, frames)`; byte `p2+3` → the `{2,0,3,0}` table at `0x0126be70` |
| 3 | `caseD_2` | `FUN_0060e0c0(out+0x10, blob_w0>>8 & 0xff, p2+4, key, key+1)`; `FUN_0060e640(out+0x20, blob byte0, p2+8)` |
| 4 | `caseD_3` | `FUN_006011f0(t, out, out+0x10, p2, frames)` |
| 5 | `case 4` | `FUN_00600810(t, motion, 0, out+0x20, out+0x10, out+0x30, track)`; dissolve byte `[frame]` at `p2+0x10`; flags (`OBJANIMTYPE_OBJECT_WITH_DISSOLVE`) |
| 6, 27, 28 | `caseD_5` | `FUN_00601590(t, out+0x10, p2, frames)` |
| 7 | `caseD_6` | `FUN_00602040(t, out, out+0x10, p2, frames)` |
| **8** | **default** | **no-op** (falls through to the return) |
| 9, 38 | `caseD_8` | `FUN_0060e0c0(out+0x10, byte p2+2, p2+4, key, key+1)`; `FUN_0060bba0(out+0x20, w0>>8 & 0xff, p2+8, key)`; `FUN_0060e370(t, t2, out+0x30, byte0, p2+12)` |
| 10, 13, 14, 17, 22, 23, 32, 39, 40, 41, 42, 46 | `caseD_9` | **`FUN_00600ef0(t, out+0x10, p2, frames)`** — the blob `{formats, A, B, C}` path of §3 |
| 12 | `caseD_b` | `FUN_0060e0c0(out+0x10, w0>>4 & 0xff, p2+4, key, key+1)`; `FUN_0060e370(t, t2, out+0x20, byte0, p2+8)` |
| 15, 16, 18, 19, 20, 21, 25 | `caseD_e` | `FUN_00601930(t, out+0x10, p2, frames)` |
| 24 | `caseD_17` | e0c0/bba0/e640 with 4-bit formats; `out[0x10] = *(int *)(p2+0x10)` |
| 26 | `caseD_19` | bba0 + e640; trailing bytes `p2+0x10..0x13` → `out[0xb..0xd]`, `out[0x35]` |
| 29 | `caseD_1c` | two branches on the motion version `< 0xb` vs `>= 0xb` |
| 30, 31 | `caseD_1d` | e0c0/bba0 with **signed-byte** formats at `p2+1`/`p2+2` |
| 33, 34, 35 | `caseD_20` | `FUN_00601c80(t, out+0x10, p2, frames)` |
| 36 | `case 0x23` | `FUN_00600810(…)`; camera id `*(int *)(motion+8 + track*4)`; flag `0xffffff < *(u32 *)p2`; `FUN_00600d70(t, 0, motion, "camera_0", viewAngle/aspectRatio/timeScale)` (`OBJANIMTYPE_CAMERA`) |
| 37 | `case 0x24` | `FUN_00600810(…)`; camera id (`OBJANIMTYPE_CAMERA_TARGET`) |
| 43, 44, 47 | `caseD_2a` | e0c0/bba0/e640/e640 with **5-bit** formats packed in word0; `out[0x14] = w0>>0x14` |
| 45 | `caseD_2c` | e0c0/bba0/e370 with 4-bit formats; dissolve `[frame]` at `p2+0x10`; byte `p2+3` → the flag table |
| 48 | `caseD_2f` | `FUN_006011f0(…)`; trailing ints `p2+0x14..0x1c`; `out[0x17] = 1.0f` |
| ≥ 49 | default | no-op |

The corpus's flag-set type histogram (measured over 4,455 files, record counts): `1:2960, 2:1056,
3:221, 4:114, 5:953, 7:163, 9:351, 10:82, 11:847, 12:11, 13:198, 14:105, 15:12, 16:130, 17:142,
22:12, 23:171, 24:183, 25:183, 26:247, 27:23, 28:25, 29:1591, 30:3, 31:2, 32:142, 33:2, 36:453,
37:8, 40:138, 41:62, 43:7, 44:203, 45:185, 46:91, 47:11, 48:8` — no type outside `1..48`, no
unmapped type except `8` (which the engine itself leaves as a no-op).

**Sampler-path gating (MEASURED check, interpretation OPEN).** `FUN_0060b720` (a sampler-loop
caller, reached `FUN_005fc6b0 → FUN_0060b720 → FUN_005fd900`) branches on **`motion+0x08 == 0`**:
that path loops `FUN_005fd900` over the tracks (the flag-set/sampler read); otherwise it loops
`FUN_0060bba0` directly over the **flag-clear** read (type byte at `motion+0x08 + track`, u32 blob
pointer at `runtime+0x18 + track*4`). 589/1,461 flag-set files have `motion+0x08 == 0` and 872 do
not (the leo/cerberus/camera samples are in the non-zero group). Which of the nine `FUN_005fd900`
callers serves the non-zero flag-set files was not resolved this pass — recorded as open (§7).

## 5 · The type-13 decode, end-to-end (MEASURED)

The type-13 path is `caseD_9 → FUN_00600ef0(t, out, blob, frames)`: wrap the frame time by
`frames−1`, split into key index + fraction, then write the **vector (e0c0) into `out+0x10..0x1c`,
the quaternion (bba0, blended between key i and i+1) into `out+0x20..0x2c`, and the vector (e640)
into `out+0x30..0x3c`**. The tool's `decode` mode implements exactly that (integer frames read key
i alone; the blend is `frac·k₁ + (1−frac)·k₀` per component).

**The recon sample (`objanim_leo_a_jump_fall`, v8, 21 f, 15 tracks, all type 13):** all 15 tracks
decode; every B key is a unit quaternion (`max |1−|q|| = 2.3e-08`); all three channels are
**constant across the 21 frames** — every blob's format word is 0 (all channels static: A =
`(0.06281, −0.10228, −0.34586)`, B = `(0.22070, 0, 0, 0.97534)`, C = `(0.27600, 0.24400, 0.43400)`
for track 0). So this file is a **static effect-placement bank** (15 named impact/damage objects);
its "smoothness" is trivial, which is why the animated decode below is the load-bearing one.

**Animated decodes (the load-bearing smoothness checks):**

- `objanim_ath_damage.anb` track 4 (formats `0/0/1`): the **C channel is a 60-key float-triplet
  series** (`fmt 1`; `desc + key*12`): `max adjacent |Δ| / component range = 0.0444` (per-component
  `0.3109/7.0, 0.0444/1.0, 0.1332/3.0`), 59/60 keys change — a smooth, non-constant curve.
- `objanim_cerberus_a_atk_near_l.anb` track 0 (formats `2/0/3`): the A channel is a 25-key int16
  triplet series (`fmt 2`, `desc[6]` array, 6-byte stride): keys 1..24 are smooth
  (`max ratio 0.269` on the steady segment) while the **key-0 → key-1 transition jumps the full
  range** (`z: −0.201 → −0.487`) — a measured property of this clip (the effect snaps at spawn),
  not a decode artifact (the raw int16 triplets show the same: `(16383, −16383, 16383)` then
  `(−15660, 15660, −16383)`). The C channel (`fmt 3`, packed u16) decodes to the full 25-key
  series `0 → 1.0625 → 0.993952` (held for keys 2–8) `→ 0.479839` (key 9) `→ 0` (keys 10–24) — a
  measured spawn/flash/hold/clear profile, not a static value.

**Corpus decode (198 type-13 tracks in 82 files):** every track decodes without exceptions; the
16 format combos observed are `(0,0,0):97, (2,0,3):28, (0,0,3):18, (2,5,3):13, (2,3,3):11,
(2,6,3):7, (2,4,3):6, (2,0,0):4, (2,3,0):3, (2,13,3):3, (2,4,0):2, (1,11,1):2, (0,0,1), (0,2,1),
(0,3,3), (0,13,0)`. **Every B-channel format decodes to unit quaternions** — the strongest single
validation of the accessor arithmetic:

| B format | keys | max \|1 − \|q\|\| |
|---|---|---|
| `0x0` (raw floats) | 4,028 | 2.96e-08 |
| `0x2` (axis-angle) | 60 | 1.11e-16 |
| `0x3` (packed) | 1,076 | 3.55e-03 |
| `0x4` (packed) | 553 | 1.65e-03 |
| `0x5` (packed) | 659 | 3.22e-04 |
| `0x6` (packed) | 320 | 1.33e-03 |
| `0xb` (int16 triplets) | 202 | 1.11e-16 |
| `0xd` (scalar angle) | 119 | 1.11e-16 |

(The packed formats' ~0.4% deviations are the engine's own residual term (`±4 bits × desc[7]/7`);
the engine never normalizes — C3 §6.3, re-observed.)

## 6 · Verification (pinned assertions — `records.py verify`, exit 0/1)

19 assertions, **19/19 PASS, exit 0**, full run ≈ 1.1 s including the 4,455-file corpus scan
(`out/re/flagset-verify.json`):

1. **leo structure/roles**: flag-set bit, 15 tracks / 21 frames, all records type 13; every `p1`
   resolves to a printable NUL-terminated string (15/15); the strings tile the data region from 0
   (last ends at `0x118`); the memo's `p1 == k×frames` holds only for k ≤ 5 and fails at k = 6
   (`p1[6] = 0x7C` vs `6·21 = 0x7E`) — the refutation is pinned as a passing check;
2. **leo decode**: all 15 tracks decode; every B key unit (`2.3e-08`); format-0 tracks static
   across all 21 frames;
3. **animated decode + smoothness**: `ath_damage` t4 C channel = 60 keys, `max ratio 0.0444 ≤ 0.25`,
   ≥50 changing steps (59); `cerberus_a_atk_near_l` t0 A channel = 25 keys, steady segment
   `max ratio 0.269 ≤ 0.5` (the key-0 jump recorded in the assertion name), B unit, C animated;
4. **`0xe` self-check** (in-memory, no game input — the review's demo vector): for the all-ones
   packed word with unit descriptors the decode is the unit quaternion
   `(−0.000978, −0.001957, −0.001957, −0.999996)`, pinning the SAR field extraction and the
   `1/511` (`0x0126b2b8`) y/z scale (the pre-fix logical-shift form gives `|q| = 3.32`);
5. **corpus pointer roles**: 198/198 type-13 `p1` resolve to strings; 198/198 `p2`/A/B/C land inside
   the data region `[data_base, t1p)`; every blob format nibble ≤ `0xe` (16 combos);
6. **corpus robustness**: all 4,455 `.anb` parse (1,461 flag-set / 2,994 flag-clear, 0 parse
   errors); all 198 type-13 tracks decode end-to-end with the failures **counted** (0 decode
   failures; each failure is reported as its own failing assertion).

The camera sample is covered by the same machinery: `scan 'cp3shadow/anim/camera_*.anb'` → 453
files, all flag-set, type histogram `36:453, 37:8`, 0 parse errors (their p2 blobs are the camera
descriptors read by the inline `case 0x23/0x24` bodies, §4 — not decoded by the type-13 path).

## 7 · Open items (INFERRED / unresolved)

- **The `+0x08` u32 array's meaning** (§1) — position/length measured, no u32-per-track reader found.
- **The p1 exception set** (§2.2) — 33 non-resolving p1 records on types 1/2/11: the font-encoded
  names (high-bit byte strings, e.g. `proto_` + font bytes) suggest a second name encoding, and the
  yamori cluster (`p1 = 0..3` into NUL padding with the strings starting at offset 4) suggests a
  per-track name offset that can point at padding — neither consumer/encoding is decoded.
- **The `FUN_0060b720` path split** (§4) — the `motion+0x08 == 0` gate is measured; which caller
  serves the 872 non-zero flag-set files is not.
- **The version>7 `+0x2C` pass on flag-set files** (§1) — measured to read tail-table words; its
  runtime effect (and whether the tail tables are meant to be read through the same 31-bit
  convention) is not established.
- **Semantics of the decoded channels** — the tool reproduces the engine's arithmetic exactly, but
  what A/B/C *mean* for type 13 (position/rotation/scale-of-effect, in which space) is not claimed.
- The packed formats' decode is validated by unit-norm and the loader's own re-base slots, not by a
  capture; the residual term is the engine's.

## 8 · What this changes

The flag-set variant is now decoded to the same level the flag-clear variant was after the recon:
the record roles (`{type, p1 = name, p2 = key blob}`), the full per-type dispatch (all 48 mapped
types), the blob/accessor vocabulary, and an end-to-end type-13 decode that passes pinned
smoothness/unit-norm assertions over the whole population. This is the T2 input the converter side
needs; the remaining conversion work (all types, not just 13) is mechanical given the dispatch
table. The T2 verification brief's `p1 = k × frames` premise is **refuted** and replaced by the
measured name-string role (§2.2) — recorded as a memo discrepancy, with the corrected assertion
pinned in `records.py verify`.
