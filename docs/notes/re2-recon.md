# RE-2 recon — the `.anb` parse chain (`FUN_00588900`) + environment map (2026-10-18)

Recon pass for the next RE cycle: the **file→runtime parse chain** that RE-1/C3 recorded as the remaining
gap (`docs/notes/re-anb.md` §6.7). Everything below is measured with Ghidra 12.1.4 headless against the
RE-0 baseline project (`eboot.elf`, `x86:LE:64:default`, image base 0, 38,845 functions) and with raw-byte
scripts over the read-only game tree; the raw-byte scripts live under the gitignored `out/re/` of the
`re2-recon` worktree. This file is **new** — `re-anb.md` is untouched.

| | |
|---|---|
| Date | 2026-10-18 (run stamps UTC) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64 |
| Repo / branch | `sotc-native`, `re2-recon` (base `a50891a` = main) |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Ghidra project (copy) | `out/re/ghidra` (project `sotc`, program `eboot.elf`) — a copy of the capture-ghidra project (§0) |
| Raw input copy | `out/re/eboot.elf` (the copy of capture-ghidra's C3-baseline artifact — its `re0-acceptance` run PASSed the digest cross-check against the extraction manifest; VA→file = **+0x4000** for both PT_LOADs, verified below) |
| Samples | `cp3shadow/anim/player_a_stand.anb` (8,424 B), `cp3shadow/anim/horse_a_add_horse_kick.anb` (4,276 B), + a 300-file random sample of the 4,455 `.anb` |

## 0 · Environment map

**The port.** From `docs/notes/re-capture.md` (capture-decode worktree): port repo
`~/code/mine/ShadowOfTheColossus-PC` at commit `cdce18b` (2026-10-15), probe binary `out/bb-probe`
(`bash build.sh` / `bash build.sh --test`), staged game dir `<port>/out/CUSA08809` →
`<port>/out/pkg-extract/uroot`. The port is not needed for this pass (static only).

**The Ghidra baseline.** `scripts/re0-acceptance.sh` (main) rebuilds `out/re/` from scratch: it stages
`$SOTC_UROOT`, unwraps `eboot.bin` with the port's `scripts/prepare.py` (→ `out/re/eboot.elf`,
`out/re/analysis.json`), then imports + auto-analyzes into project `out/re/ghidra` (`sotc`) and runs
`PrintSummary.java`. `GHIDRA_HOME` defaults to `$HOME/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC`.
The capture-ghidra worktree holds the analyzed project (334 MB, 38,845 functions); this pass **copied** it
to the worktree's `out/re/ghidra` (read-only use of capture-ghidra — nothing there was modified).

**Headless invocation** (the RE-0 dot-free-location workaround; the worktree path under `~/.local` cannot
be passed to `ProjectLocator` verbatim):

```bash
GHIDRA_HOME=$HOME/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC
proj_link_dir=$(mktemp -d); ln -s "$PWD/out/re/ghidra" "$proj_link_dir/ghidra"
"$GHIDRA_HOME/support/analyzeHeadless" "$proj_link_dir/ghidra" sotc -process eboot.elf \
  -noanalysis -scriptPath "$PWD/out/re/scripts" -postScript FindSampler.java <mode...> \
  > out/re/<log> 2>&1; rm -rf "$proj_link_dir"
```

**Scripts used** (scratch copies under `out/re/scripts/`, gitignored):
`FindSampler.java` (copy of the capture-ghidra script — modes `walk`/`decompile`/`callers`/`callees`/`refs`),
`Disasm.java` (new scratch: `Disasm.java <start-end> …` prints listing instructions). Logs:
`out/re/log-parsechain-1.log` (decompile `FUN_00588900`/`FUN_00588e50`/`FUN_00588e80`/`FUN_00589310`),
`log-parsechain-2.log` (`FUN_005896a0`, `FUN_00588830`, `FUN_00588dd0`, `FUN_0058a030`, `FUN_0058a0a0`,
`FUN_0058a110`, `FUN_00b23a50`, `FUN_00b23b10`, `FUN_00b23c10`, `FUN_00b24130`), `log-fsload.log`
(`FUN_006abe10`, `FUN_005fc5d0`), `log-refs-1.log`, `log-disasm-1/2/3.log`. Re-run determinism is inherited
from the RE-0/C3 scripts (sorted outputs, `-noanalysis`).

**The ELF mapping** (measured, `out/re/eboot.elf`): PT_LOAD[0] `off=0x4000 vaddr=0 filesz=0x1593978`,
PT_LOAD[2] `off=0x1608000 vaddr=0x1604000 filesz=0x17d3100` → **file offset = VA + 0x4000** for every
initialized byte (the RE-1 reviewer's mapping, re-verified here).

## 1 · The parse chain (measured)

### 1.1 The call graph

`loadCdMotionFile` (`FUN_00670680`, the `.anb` loader of the RE-1 registry) is one of **nine** callers of
the same attach pair (measured, `log-refs-1.log`):

| function | role | callers (measured) |
|---|---|---|
| `FUN_006abe10` | FS load — returns the raw file bytes (either the FS buffer `*(file+0x68)` or a copy via `FUN_00579ad0` when `DAT_030178f0 != 0`); writes the size to its 4th arg | `FUN_00670680` (and the other loaders) |
| `FUN_005896a0` | **header fixup pass** (854 B, no calls — self-contained) | `FUN_00670680`, `FUN_00670ce0`, `FUN_00671350`, `FUN_006717f0`, `FUN_00671b20`, `FUN_00671e70`, `FUN_00672910`, `FUN_007f0770` |
| `FUN_00589310` | **attach wrapper**: allocates the 0x88 node, calls `FUN_00588900`, falls back to `FUN_00588e80` | the same 8 + `FUN_00788d10` |
| `FUN_00588900` | **attach** (1,210 B): re-base +0x3C, `FUN_00b24130`, the section-size loop, the "launchElfInfo" pool, `FUN_00b23c10`, node+0x40/+0x48 | `FUN_00588830`, `FUN_00589310` |
| `FUN_0066fe60` | **motion-structure fixup** (the RE-1 §4.4 pass): rotation + 31-bit re-bases + the per-element walks | `FUN_00670680` (and the other loaders) |

`FUN_00670680`'s measured order (decompile + disasm `0x0067086b`–`0x00670941`):
`uVar3 = FUN_006abe10(path, …)` → `FUN_005896a0(uVar3)` → `lVar4 = FUN_00589310(uVar3, 0, &DAT_02f35858, 1, 0, 1, &DAT_02f35858, 0x48)`
→ `lVar2 = *(long *)(lVar4 + 0x48)` → `FUN_0066fe60(lVar2)` → allocate the 0x88 runtime object
(`FUN_00017070(0x88)`, two sub-objects at +0x18/+0x50 via `FUN_00295a90`) → copy the 8 bytes at `lVar2+0x10`
into object+0x00/+0x04 → store the object at `lVar2+0x10` → `FUN_006c17f0(path, lVar2)`.

### 1.2 `FUN_005896a0` — the header fixup (the "tagged pointer" mystery, resolved)

`FUN_005896a0(file)` rewrites the header **in place**. For each of the eight tagged pointer fields it
**ignores the tag half entirely** and rebuilds the value from the **mirror table** `+0x50..+0x6C`:

```
uVar6 = *(uint *)(file + MIRROR) + file;          /* the target address */
stored = (uVar6 >= DAT_02f3b8c0)                  /* in the load region? */
       ? 0x80000000 | ((uVar6 - base) & 0x7fffffff)   /* base-relative, bit31 set */
       : (uVar6 & 0x7fffffff);                         /* absolute, bit31 clear */
```

Measured conversions (disasm `0x005896a0`–`0x005899f0`; the same set appears in `FUN_00b24130`, the twin
called from `FUN_00588900`):

| field | ← mirror | | field | ← mirror |
|---|---|---|---|---|
| `+0x30` | `+0x5C` | | `+0x20` | `+0x50` |
| `+0x28` | `+0x54` | | `+0x3C` | `+0x64` |
| `+0x34` | `+0x60` | | `+0x48` | `+0x6C` |
| `+0x2C` | `+0x58` | | `+0x44` | `+0x68` |
| `+0x18` | `+0x14` (the file size — i.e. EOF) | | | |

Plus two nested walks:

- the section table (`+0x30` target, count `*(int*)(file+0x40)`): for entries `0 … count−2` (stride `0x20`),
  field `[target+0x24+0x20k]` ← mirror `[target+0x3C+0x20k]`;
- the table-descriptor array (`+0x3C` target, count `*(int*)(file+0x38)`, stride `0x1C`): per entry,
  `+0x0C` ← `+0x14` and `+0x10` ← `+0x18`.

**The mirror table is the loader's input, not a copy**: on this loader/fixup path the raw `tag<<16 + offset`
form is never read — the untagged mirror at `+0x50..0x6C` is what the loader consumes (RE-1 §1.1's tag half
is a packer artifact *for this path*; a whole-binary absence is not claimed here). The RE-1 §3.1 correction
("addition with carry") stays true as a *property of the raw bytes* but is moot at runtime.

### 1.3 `FUN_00588900` — the attach

Measured behaviour for a real `.anb`/`.skb` (`file+0x1C == 0`, `param_6 != 0` → the "big branch"):

1. re-bases `+0x3C` from its mirror `+0x64` (the same conversion as §1.2 — idempotent);
2. `FUN_00b24130(file)` — the same fixup pass again (a re-run for moved buffers);
3. **the section-size loop** over the section table (`file+0x30` target, count `file+0x40`): per record
   (stride `0x20`, `count−1` records), sets `+0x34 = 1` (a "needs materialization" flag) and, for the leaf
   types `{1, 0x7ffff420, 8}` at `+0x30`, accumulates `0xa0 + size(+0x28)`;
4. `DAT_02f4e420 = FUN_0057b110(param_6, iVar12 + 0x110 + iVar10, "launchElfInfo", {src,line})` — creates the
   **"launchElfInfo" pool** (debug-tagged allocator; `iVar10 = 0` for both variants since
   `file+0x24 − file+0x1C == (file+0xC ? file+0xC+1 : 2)` holds for `file+0xC ∈ {0,1}`);
5. allocates the **0x70 attach object** from the pool (`FUN_00579ad0(pool, 0x70, 4, …)`);
6. `FUN_00b23c10(file, FUN_0058a030, FUN_0058a0a0, &LAB_00588e70, param_7)` — **the section decoder**
   (§1.4);
7. zeroes the section records' `+0x38` state fields;
8. **`node+0x48 = (sign(file+0x10) ? base : 0) + (file+0x10 & 0x7fffffff)`** — the pointer `FUN_00670680`
   passes to `FUN_0066fe60` (assembly `0x00588d0e`–`0x00588d3d`);
9. `node+0x40 = file`; frees the +0x30-array entries whose `+0x38` state is set (`FUN_0057aa50`) and the file.

`FUN_00589310` (the wrapper): allocates the node (`FUN_00577840(pool, 1, 0x88, extra, …)` with
`extra = (iVar7 ? iVar7*0x20 + 0xa0 : 0) + (iVar2 ? iVar2*0x20 + 0xa0 : 0)`, `iVar7 = 1 − max(file+0xC, 1)`,
`iVar2 = file+0x1C` — both terms are 0 for `.anb`/`.skb`, so the node is exactly 0x88 B), calls
`FUN_00588900`; on success `node+0x40 = *returned` (the 0x70 object), `node+0x48 = **returned`
(its first 8 bytes = the re-based `file+0x10`); on failure `FUN_00588e80(node, file, re-based(file+0x10), …)`.
`FUN_00588830` is the alternate attach (pre-allocated node) and calls `FUN_00588e80` in both branches.

### 1.4 `FUN_00b23c10` — the section decoder ("`ld: decode section`")

Walks the section table's records (`count−1`, stride `0x20`) and **materializes** each: type
`0x7ffff420`/`1` → the "overlay data" path (dest = src in place when `(align−1) & src == 0`, else allocate +
memcpy); type `8` → allocate + memcpy + zero; other types → allocate + memcpy. It writes the destination
31-bit pointer to record `+0x20`, the state (`1`/`2`) to `+0x38`, and remembers the **first materialized
block's absolute address** (`lVar15`). Then the decisive tail (decompile lines 819–833, confirmed by the
record `+0x20`/`+0x38` semantics):

```c
file+0x10 = 31-bit(lVar15 + file+0x4C);   /* lVar15 = the first materialized block */
```

**So the runtime patches `file+0x10` (using the duplicate `+0x4C`) to point at
`materialized_data_region + (+0x4C)` — the motion structure lives *inside the materialized copy of the data
region*.** For both samples the single section record is the data region (`src = file+0x110`,
`size = 7224` (player) / `2900` (horse) — the data size = `table1_start − 0x110`, the value at `@0xA0`,
`type = 1`, `align = 0x10`), so:

```
motion structure (file) = file[0x110 + *(u32)@0x10]
```

(MEASURED: `player_a_stand` → file `0x1D04`; `horse_a_add_horse_kick` → file `0xC20`; both look like
structures, §2.1 — while the raw `+0x10` value read as a *file* offset lands in key data, which is why
RE-1/C3's scans missed it.)

### 1.5 `FUN_0066fe60` — the motion-structure fixup (RE-1 §4.4, re-read)

On the structure (all offsets data-region-relative, §2):

1. **rotation**: `{+0x10: X, +0x14: Y, +0x18: Z, +0x1C: W}` → `{+0x10: Y, +0x14: Z, +0x18: X, +0x1C: W}`;
2. re-bases `+0x08`, `+0x0C`, `+0x18`, `+0x1C`, `+0x28`, `+0x40`, `+0x44` (bit31 if `v+base < DAT_02f08000`),
   and (version > 5 with flags) `+0x18` of the structure at `+0x0C`;
3. `+0x01 = 0`;
4. the track walk: count = runtime `+0x10` (= disk `+0x14`); per element:
   - `*structure & 0x10000 == 0` → `FUN_0066dec0(type byte from `+0x08` array + i, u32 from the `+0x18` array + 4i, frames)` — the **flag-clear** variant;
   - else → `FUN_0066f9a0(record + 0xC·i, frames, version)` — the **flag-set** variant (12-byte records);
5. the `+0x1C` array: count = runtime `+0x14` (= disk `+0x18`), 16-byte elements, `FUN_0066e460` each;
6. version > 3: `+0x24` re-based + `FUN_0066dec0`; version > 7: `FUN_0066e1b0(+0x2C, frames)`;
   version > 8: `+0x3C` re-based + `FUN_0066dec0` (all skipped/zero for both samples).

`FUN_0066dec0(byte, ptr)` re-bases one field of the per-type blob at `base+ptr`: type 2 → `+0x0C`;
types 3–6 → `+0x20`; `0xC`/`0xE` → `+0x18`; `0xD` → `+0x14`. `FUN_0066f9a0(record)` re-bases `record+0x04`
and `record+0x08` and switches on the type word (cases 1, 2/7/9/0xB/0x26, 3, 4/0x30, 5, 6/0x1A/0x1B/0x1C, …).

### 1.6 What the sampler reads (C3 §6.1, re-checked)

`FUN_005fd900(t, t2, out, motion, track)`:
`records = (sign(motion+0x18) ? base : 0) + (motion+0x18 & 0x7fffffff)`; `*out = *(int *)(records + track*0xC)`
(the record **type**); `lVar18 = *(long *)(motion + 0x10)` (the 0x88 object pointer stored by the loader);
the frame count via `FUN_005fc5d0` → `(int)*(short *)(motion + 4)`. So the sampler's record array =
**runtime `motion+0x18`** — which per §2 is the *blob-pointer array* for flag-clear files (stride 4) and the
*12-byte record array* for flag-set files (§3.3 validates the latter on real data).

## 2 · The `.anb` motion structure and track model (measured)

### 2.1 The structure layout

`motion = file[0x110 + *(u32)@0x10]`. Measured words (both samples):

| offset | `player_a_stand` | `horse_a_add_horse_kick` | meaning (measured/inferred) |
|---|---|---|---|
| `+0x00` | `0x0000000C` | `0x0000000C` | version word: byte 0 = version (12 here; full population 4…12 — v4:3, v5:6, all flag-set `objanim_*`), bit 16 = the **12-byte-record flag** (clear here) |
| `+0x04` | `0x8002004C` | `0x80020010` | low u16 = **frame count** (76 / 16) — read by `FUN_005fc5d0`/sampler |
| `+0x08` | `0x000019B4` | `0x00000A7C` | offset → the per-track **type byte array** |
| `+0x0C` | `0x000001C8` | `0x00000060` | offset → the **pre-region descriptor** (§2.3); the version>5 nested fixup re-bases its `+0x18` ([6] slot) — the condition holds for both samples (version 12, bits 48–51 nibble = `0x2` in both `0x8002xxxx` words); the slot is 0 in both → no-op |
| `+0x10` | `0x00001940` | `0x00000A04` | offset → the per-track **blob pointer array** (u32 × tracks) — runtime `+0x18` |
| `+0x14` | `0x0000001D` | `0x0000001E` | **track count** (29 / 30) — runtime `+0x10` |
| `+0x18` | `0x00000001` | `0x00000001` | count of the `+0x1C` array — runtime `+0x14` |
| `+0x1C` | `0x00001BDC` | `0x00000ACC` | offset → a 16-byte-element array |
| `+0x24` | `0x00000000` | `0x00000AE0` | offset (version>3 fixup) |
| `+0x28` | `0x00001BF0` | `0x00000AF0` | offset → one word (`5`) immediately before the structure (`player`: file `0x1D00` = `0x00000005`; the next word is the structure's own `+0x00`); horse: `{0, 0}` |
| `+0x3C` | `0x00000000` | `0x00000B00` | offset (version>8 fixup) |
| `+0x44` | `0x000001E0` | `0x00000078` | offset → **t1[0]** — the first track region (§2.3), confirmed by the region's first word = `t2[0] = 0` |

All offsets are **data-region-relative** (data region = file `0x110 .. t1p`; `t1p = *(u32)@0xD4`). The
`+0x44` → t1[0] cross-check is the decisive one (both samples).

### 2.2 Tracks, blobs, key data

- **Track types** (29 / 30 bytes at `0x110 + (+0x08)`): `player` =
  `03 03 0d 03 00 03 0d 03 03 04 03 03 05 00 05 0d 04 00 00 00 00 06 04 0d 04 00 00 00 00`;
  `horse` = `0d 0d 03 03 0d 0d 03 03 03 03 03 03 00 00 04 0d 03 0d 04 0d 03 0d 03 0d 03 03 03 05 03 04`.
  Type `0` = a stub track (no pointer field).
- **Blob pointers** (u32 × tracks at `0x110 + (+0x10)`): each points at a per-type **blob** (8 floats +
  a pointer field; e.g. `player` track 0 → data-region `0x314` = file `0x424`: 8 floats then `+0x20 = 0x1E4`).
- **Key-data pointer** per blob (the field `FUN_0066dec0` re-bases): types 3–6 → `+0x20`; type `0xD` → `+0x14`;
  type 2 → `+0x0C`; `0xC`/`0xE` → `+0x18`. It points (data-region-relative) at the track's **key data**:
  - **type `0xD` (scalar)**: a 2-byte int16 series. MEASURED smooth: `player` track 2 at file `0x59C`
    (`32766×8 → 32392×4 → 32766×5 → 32392 → 31269 28647 24902 19285 12919 6178` — a plateau then a
    smooth descent); `horse` track 0 at file `0x18C` (`32767 23238 4039 −16637 −30832 −32766 −31121 … 21256`
    — a sine-like curve). Matches §6.3 case `0xD` ("1 × int16 per key").
  - **types 3–6 (quaternion-ish)**: a u32 series whose **low int16 is a smooth symmetric component**
    (e.g. `player` track 0 at file `0x2F4`: lo16 `16679 16166 15139 13087 11034 8468 6414 4361 2309 1282
    769 1282 2822 5901 10520 16679` — U-shaped, **first == last**), while the high half carries the other
    components. The exact 4-component packing is **open** (§4) — the §6.3 packed formulas (cases 3–6, 0xE) do
    not reproduce smooth quaternions on these words (tested, dots ≫ 1).
- **The leading-run relation** (RE-1 §3.2's "t2[i] = t1[i−1]+4", now explained): the region tables t1/t2 are
  **data-region-relative**; `t1[i]` = track *i*'s region start, `t2[i+1]` = track *i*'s key-data offset
  (= `t1[i]+4` for the leading run; for later tracks the blob's key pointer is authoritative and equals
  `t2[k]`). Region *i* = `[t1[i], t1[i+1])` (the last to `t1p`) and its **first word = `t2[i]`** (a back
  pointer to the previous track's key start; `0` for track 0).

### 2.3 The pre-region — the per-frame "root" series

The data region starts with a **per-frame key series**: file `0x110` up to the first region
(`0x110 + t1[0]`). MEASURED:

- `player_a_stand`: **76 keys** at 6-byte stride (int16 triplets, the §6.3 `0xB` shape) = exactly the frame
  count, dots `0.9734..1.0000`, ending at file `0x2D8`; then **6 float32s** (24 B) = the §6.2 sample-descriptor
  offset/scale pairs (`0x3c11aa3e 0x3c8167b3 0x3f3eb8b4 0x3b9639fe 0xbccd3e19 0x3d88a329`).
- `horse_a_add_horse_kick`: 16 keys = the frame count, 6-byte stride, same layout (its int16 triplets are
  *not* in [−1,1] as raw values — norms² ≈ 2.0–2.25 on the first keys; that the descriptor's offset/scale
  normalizes them is **INFERRED**, not yet verified); 6 floats follow.
- Layout: `[frames × 6 B keys][6 × float32]` = the whole pre-region (`0x1E0` B for the player, `0x78` for the
  horse — both exact). The structure's `+0x0C` points at the 6-float tail: i.e. the tail **is** the start of
  the §6.2 sample descriptor (`floats[0..5]` = the offset/scale pairs; its `[6]` pointer slot at descriptor
  `+0x18` = the first region's first word = `t2[0] = 0` for both samples, so no pointer is re-based —
  INFERRED: the keys are inline, hence no sample-array pointer).

This is the **only** per-frame series found; the per-track key data (§2.2) is sparse/heterogeneous.

## 3 · Cross-checks against the C3 leads (measured)

### 3.1 The "record 0 (0x1E0) 41-key series" — a phase-shifted reading, reproduced and explained

Re-derived under the C3 rule (6-byte int16 triplets, `|v| ≤ 1`, dots of derived-`w` quaternions):
**42 keys from file `0x1E0`, dots `0.9727..0.9996`, first break at the 41st transition, dot `−0.0643`**
(C3 recorded `−0.0642`; the reviewer's dot-min adjudication stands). Under the §2 mapping, file `0x1E0` is
`0xD0` into the data region — i.e. **4 bytes into key 34 of the pre-region series** (0xD0 = 34·6 + 4). The
"series" is the tail of the real pre-region series read at a 4-byte phase shift; the recorded break lands
where the series ends and the descriptor floats begin (~file `0x2D8`). **The real series is at file `0x110`
with 76 keys (§2.3) — the C3 lead is a phase artifact of it, not a record's key data.** (The dot figures use
the unsigned derived-`w` variant, as C3's recorded numbers do; the §6.3 `0xB` sign bit is not applied —
applying it makes the series non-smooth — a precision note from the verification.) The other C3 leads
(record 48's run at `0x1A3A`; the 178-window cluster at `0x1CB8`; record 57's two runs at `0x1C1C`/`0x1CB8`)
fall inside track regions under the §2 mapping (measured: `0x1A3A` → region 19; `0x1CB8`/`0x1C1C` → region
48); that they are the same class of shifted readings is **INFERRED** (not re-derived) — they are recorded as
**superseded by the structure** (the key data is int16-ish streams that pass the norm test in shifted
alignments).

### 3.2 t1/t2 tables (RE-1 §3.2's open relation, now resolved)

`t1[i]` and `t2[i]` are data-region-relative; `t1p` (the table *location*, file `0x1D48`) is a file offset
from the header pointer `+0xD4`. The region extents `[0x110+t1[i], 0x110+t1[i+1])` reproduce RE-1 §3.2's
size histograms exactly (e.g. `player`: 340, 340, 176, 340, 356, 176 … and the last region `0x1D2C..0x1D48`
= 28 B with first word `t2[57] = 0x1BF0`), and the first region's first word = `t2[0] = 0` (both samples).

### 3.3 The flag-set variant (the 12-byte records) — validated on real data

Population scan (300 random `.anb` of 4,455, `out/re/`): **92/300 (31%) have bit 16 of the version word set**
(version byte histogram 7:3, 8:1, 9:2, 10:3, 11:16, 12:275; frame counts 1…851). Full population (4,455 files,
0 unreadable): **1,461 (32.8%) flag-set**; versions 4:3, 5:6, 7:30, 8:5, 9:23, 10:93, 11:145, 12:4,150 — the
v4/v5 files are all flag-set `objanim_*`. For flag-set files the records at `0x110 + (disk +0x10)` are
12-byte `{type, p1, p2}` groups with types in `1..0x30` (all 1,461 flag-set files type-checked: range 1..0x30,
0 violations) — e.g. `objanim_leo_a_jump_fall.anb` (v8, 21 frames, 15 tracks): 15 records of type 13 with
`p1 = 0, 0x15, 0x2A, 0x3F, 0x54, 0x69, 0x7C, 0x8C, 0x9F, 0xAF, 0xC2, 0xD2, 0xE5, 0xF5, 0x108` — a
**monotone key-index base** whose first six values coincide with `k × frames` (21) but then diverge (124 vs
126 … 264 vs 294); only **44/92** flag-set sample files fit `k × frames` at all (e.g. `objanim_tree_down_xm`:
0, 10, 20, 31 with 24 frames) — and `p2 = 0x14C, 0x18C, 0x1CC, 0x20C …` (+0x40 per track — key-data offsets);
`camera_demo_end07_cut007.anb` (v12, 481 frames, 1 track): type 36, `p1 = 0`, `p2 = 0x2214` (=
`struct+0x10 − 0x14`; the record array sits at file `0x2338` = `struct − 0x10`, the structure at file `0x2348`).
**The C3 sampler's 12-byte record layout is confirmed** — for the flag-set variant, whose records the sampler
reads at stride `0xC` (§1.6).

## 4 · What this changes / open questions

- **The parse chain is decoded to the file→runtime mapping.** The `.anb` structure, tracks, blobs and key-data
  pointers are located and cross-checked on both samples; the C3 leads are explained (phase artifacts /
  superseded); the two layout variants are identified by the version word's bit 16.
- **Open (bounded) questions**:
  1. **Key encoding per type** — type `0xD` is a 2-byte int16 scalar series (smooth, measured); types 3–6 are
     u32-ish with a smooth low int16 (the full 4-component packing + the descriptor's offset/scale semantics
     are not yet pinned); types 2/`0xC`/`0xE` blobs not yet sampled; the **key counts** per track (the extent
     between consecutive key pointers is the current best reading).
  2. **The flag-set variant's record semantics**: `p1`/`p2` roles (index base / key offset) and the per-type
     key formats for the sampler's dispatch (types 1…0x30) — the `objanim_*`/camera corpus is available.
  3. **The pool-base relation**: the structure's raw offsets resolve as `base + v`; for them to address the
     materialized copy, the copy must sit at `DAT_02f3b8c0` (INFERRED — the loader's patch of `file+0x10`
     encodes the copy's address but no patch of the structure's own fields was found; the file-level mapping
     §2 does not depend on it).
  4. **The descriptor** (§6.2's `floats[0..5]` = offsets/scales) — the 6 floats after the pre-region series
     are measured; their application to the type-`0xD`/3–6 key values is not yet verified.
  5. **`FUN_0058a110`/`FUN_00b23a50`** (the name/object and table walks) — decoded at the call-shape level
     only; not needed for the key decode.
  6. **The pre-region "root" series' role** — decoded (the per-frame 6-byte keys + the descriptor) but the
     code path that samples it is open; folded into T1.

## 5 · Feasibility verdict

**A complete parse is achievable; the gap is now a bounded encoding problem, not a discovery problem.** The
loader chain, the file→structure mapping, the track/blob/key-data model and the two variants are measured;
what remains is the per-type key packing + counts, testable directly against the raw bytes (norm/dot/first==last
checks) and against the sampler's format vocabulary (RE-1 §6.3, already read out with exact constants). No
blockers were found; Ghidra is fully usable (headless, deterministic, project copy in this worktree).

## 6 · Proposed task breakdown (next RE cycle)

| # | goal | inputs | outputs | verification idea |
|---|---|---|---|---|
| T1 | **Flag-clear key formats end-to-end**: for each blob type (0, 2, 3, 4, 5, 6, `0xC`, `0xD`, `0xE`) pin the blob layout, key-data format (stride/packing), the descriptor application, the key count, and the pre-region root series' role (consumer/application) | `player_a_stand`, `horse_a_add_horse_kick`, a flag-clear corpus (208/300 of the sample) | `tools/re/anb/parse2.py` (or an extended `probe.py` mode) emitting per-track key arrays | adjacent-key dots > 0.9 on long tracks; first == last on looped tracks; type-`0xD` scalar smoothness; descriptor-normalized norms ≤ 1 |
| T2 | **Flag-set variant**: the `{type, p1, p2}` semantics (key-index base / key offset) and the sampler's per-type dispatch for types 1…0x30 | `objanim_leo_a_jump_fall.anb`, `camera_*` (92/300 flag-set sample) | the record-role table + a per-type sample reader | `p1` = a monotone key-index base with per-track increments (measured: `objanim_leo_a_jump_fall` = 0, 21, 42, 63, 84, 105, 124, 140, 159, 175, 194, 210, 229, 245, 264; only 44/92 sample files fit `k × frames`); decode one type-13 track and check smoothness |
| T3 | **Sampler loop closed**: connect `FUN_005fd900`'s dispatch to both variants; pin the seconds→frames conversion (above `FUN_005fc6b0`) and confirm the frame count source (`motion+4`) | Ghidra project, C3 logs | notes §7 addition + the conversion chain | `FUN_005fc6b0`'s callers walked; the 30.0 constant cross-checked |
| T4 | **Re-run the gate**: update the curve extraction to the §2 model and re-run `check-curves`; flip RE-1 §5's verdict if it passes | `probe.py`, the new reading | `check-curves` output + `docs/notes/re-anb.md` §7 (append) | the gate's own assertions (norms, dots, timing) |
| T5 | **Capture cross-validation** (if the capture workstream is ready): compare a decoded pose at a known frame with a captured reference | the port rig (capture-decode) | a pose-diff report | semantic ground truth for one record |
| T6 | **Environment consolidation**: commit the parse-chain scripts (`Disasm.java`, a `FindParseChain` seed set or an extended `FindSampler.java`) and document the VA→file `+0x4000` mapping | this worktree's `out/re/scripts` | committed scripts + notes §0 | re-run determinism |
