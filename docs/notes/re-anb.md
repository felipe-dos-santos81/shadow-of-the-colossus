# RE-1 animation containers — `xff` family (`.anb` / `.skb`) + `.cani` (2026-10-18)

RE-1 (first-playable plan, "decode the game's animation container family"): **"container family +
skeleton bank (`.skb`) + the probe"**. Deliverables: `tools/re/anb/probe.py` and §1–§2 of this file.
Every value below is measured with that probe (or quoted from the spike, marked as such); inputs
stay read-only, derived artifacts land in the gitignored `out/re/anb/`, and only counts, offsets,
tags, and identifiers appear here — no game content.

| | |
|---|---|
| Date | 2026-10-18 |
| Machine | Apple M3 Max, macOS 27.0.1, arm64; `python3` 3.12.12 (stdlib only) |
| Repo / branch | `sotc-native`, `re1-decode` (base `2aa1d89`) |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Probe | **`tools/re/anb/probe.py`** — `skb <file> [--json out]`, `classify <pat>... [--limit N] [--json out]`, `check-skb [--file F] [--glb G] [--json out] [--strict-plan]` |
| Spike (read-only) | `sotc-native/scratch/anb-probe/{report.txt,Program.cs}` (throwaway C# probe, 2026-10-18) |

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
python3 tools/re/anb/probe.py check-skb                       # 16/16 gating pass, 3 plan deltas (exit 0)
python3 tools/re/anb/probe.py skb cp3shadow/skel/player_a.skb --json out/re/anb/player_a.skb.json
python3 tools/re/anb/probe.py classify 'cp3shadow/anim/*.anb' --json out/re/anb/classify-anb.json
```

Patterns are expanded **against the game tree** (quote them: the shell cannot match them from the
repo root), so the probe runs from anywhere; `--limit` caps the files taken per pattern (that is how
`.cani` gets sampled). `check-skb` reads `wander.glb`'s JSON chunk directly (`--glb`, `$SOTC_GLB`, or
`out/library/...` — a linked worktree follows its `gitdir` into the main checkout).

## 1 · Container family

The family is the **`xff` container**: little-endian, magic `78 66 66 00` (`"xff\0"`), shared by six
extensions in the tree (measured, whole tree): `.nmo` 5,297 · `.anb` 4,455 · `.nto` 1,723 ·
`.clb` 1,042 · **`.skb` 57** · `.cls` 45. The banks are grouped by extension and directory —
`.anb` only in `cp3shadow/anim/` (4,455), `.skb` + `.weightinfo` only in `cp3shadow/skel/`
(57 + 57). The `.cani` family (2,869 files, 52 `_cmn` directories — 51 of them under `animation/`,
the other `objects/actors/arena_14/polec_wallc/_cmn`) is a **separate container** with its own magic
but the same little-endian field style; see §1.2.

### 1.1 `xff` header (measured on all 4,512 `.anb` + `.skb` files)

| offset | width | value (verified across 4,512 files) | example (`player_a.skb`) |
|---|---|---|---|
| `0x00` | u32 | magic `78 66 66 00` | `78666600` |
| `0x04` | u32 | 0 (all files) | `0` |
| `0x08` | u32 | 0 (all files) | `0` |
| `0x0C` | u16 | **bank variant flag**: 1 for 4,150 `.anb`, 0 for 305 `.anb` and all `.skb` | `0` |
| `0x0E` | u16 | 0 (all files) | `0` |
| `0x10` | u32 | offset of the file's **last section** (≤ size in all files; the section is ≥ 396 B) | `0x1060` |
| `0x14` | u32 | **file size**, exact in all 4,512 files | `0x139C` = 5,020 |
| `0x18` | 8 B | opaque: all zero in 3,451 files; otherwise 4 bytes of packer text + 4 zeros (of the 1,061 non-zero fields: 647 fully printable, 162 with control bytes, 252 with high-bit bytes) | `"yubi\0\0\0\0"` |
| `0x1C` | u32 | 0 (all files) | `0` |
| `0x20` | u32 | tagged pointer | `0x0A1D00DC` |
| `0x22` | u16 | **the file's 16-bit tag** — the high half of every pointer | `0x0A1D` |
| `0x24` | u32 | 2 (all files) | `2` |
| `0x28`,`0x2C`,`0x30`,`0x34` | u32 | tagged pointers | `0x0A1D00DC`, `0x0A1D0104`, `0x0A1D0078`, `0x0A1D00B8` |
| `0x38` | u32 | 1 (all files) | `1` |
| `0x3C` | u32 | tagged pointer | `0x0A1D00C0` |
| `0x40` | u32 | 2 (all files) | `2` |
| `0x44`,`0x48` | u32 | tagged pointers | `0x0A1D0070`, `0x0A1D00FC` |
| `0x4C` | u32 | duplicate of `0x10` (all files) | `0x1060` |
| `0x50..0x70` | 8 × u32 | the **untagged offsets of the 8 pointers, in the same order** | `DC DC 104 78 B8 C0 70 FC` |
| `0x70..0x110` | 0xA0 B | opaque descriptor region (measured shape, semantics not decoded) | — |

- **Tagged pointers.** A pointer is `(tag << 16) + offset`, where `tag` is the u16 at `0x22` and
  `offset` is a file offset. Verified for all 8 pointer fields in all 4,512 files (the tag field
  varies per file — e.g. `0x0A1D` here, `0x0A25` for `player_a_stand.anb` — and is the same value in
  every pointer of a file). The `0x50` mirror table always reproduces the offsets.
  **B2 correction (§3.1):** the form is *addition with carry*, not `tag<<16 | offset16` — an offset
  `≥ 0x10000` carries into the tag half (measured on the `.anb` table pointers: 83 files for table 1,
  86 for table 2, deltas up to `0xB0000`). Every one of these 8 header pointers has an offset
  `< 0x10000` in all 4,455 `.anb`, so B1's bitwise-or reading coincides with the arithmetic here.
- **`0x10` (last section).** Measured ≤ size in all 4,512 files; the section it starts is ≥ 396 B —
  the four smallest are the 556-byte banks `gextail_a.skb`, `guard_c_head.skb`, `guard_d_head.skb`,
  `guard_e_head.skb` (396 B each; among `.skb` only, the next smallest is 412 B, while across the
  full 4,512-file scope it is 400 B — 104 `.anb`, first `camera_debug_camera.anb`).
- **`0x18` is opaque.** The 4 text bytes differ per file, never relate to that file's own names or
  to its siblings, and mostly consist of fragments of unrelated text (`"yubi"`, `"nigh"`, `"wpos"`,
  `"ssio"`, `" 100"`, …); they are fully printable in 647 of the 1,061 non-zero fields, contain
  control bytes in 162, and bytes ≥ `0x80` in 252. Working hypothesis: a packer buffer leftover
  (uninitialized field). Treat as opaque; do not read as a pointer.
- **Tail tables.** Every container ends with one or two adjacent tables of 8-byte
  `{u32 offset, u32 tag}` entries, tags `0x102` (measured: `.skb` bone records / `.anb` descriptor
  fields) and `0x000`. Offsets are **relative to the container's string/data base `0x110`**
  (verified for `.skb` by resolving names; the same base resolves the `.anb` descriptor fields).
  Reading the tables back-to-front from EOF is exact for `.skb` (measured 64 = 32 + 32 entries in
  `player_a.skb`: 30 of the 32 `0x000` entries resolve to names — the 29 core offsets plus the
  `OptBone_*` run start — while the other two are section starts, `0x180` record array and `0xAA0`
  bind region); for `.anb` the same walk can absorb leading zero padding (measured 119 vs ~116
  entries for `player_a_stand.anb`) — **B2 resolved this: the exact counts come from the header
  (§3.1); the walk's extra entries are the data region's trailing zero bytes.**
- The vendored `CANI` parser (`third_party/PSO2-Aqua-Library`) **does not fit** this family: it
  reads big-endian while these containers are little-endian (and it expects `int_14 // Usually 0x30`
  where the `xff` files store the file size), so every parse throws — the spike recorded
  `System.OverflowException` for each `.anb`/`.cani` it tried (`scratch/anb-probe/report.txt`).
  Nothing in the port depends on that parser for this family.

### 1.2 `.cani` — the sibling container (measured on 2,869 files)

- Magic `38 30 41 45` (`"80AE"`), **little-endian** like the `xff` family (measured: `fps` at
  `0x08` and both counts at `0x0C`/`0x0E` decode correctly as LE; the vendored parser reads
  big-endian, which is one reason it misfires on this family). 1,745 files plaintext.
- 1,124 files are **Oodle-compressed** with a block header of `8C 0A` (955 files, `u16` LE
  `0x0A8C`) or `8C 06` (169 files, `0x068C`); the decompressed size is stored in the file's last
  4 bytes as `0x60000000 | size` (8C0A) or `0x80000000 | size` (8C06) — the same rule as the
  vendored `CompressionHandler` (which decompresses those blocks). Measured example:
  `horse_a_demo01_cut01.cani` (360,259 B) ends `… B0 3B 09 60` → `decomp=605104`.
- **12-byte `MINA` footer** at the end of every file: magic `MINA`, version byte `9`, then the
  uncompressed size (`size - 12` for plaintext files). For compressed files the footer is carried
  verbatim in the compressed file and re-appended after decompression.
- Plaintext header fields (measured on the sample): `float fps` at `0x08` — bits `0x41EFFFFF`
  = 29.999998 (30 − 1 ULP) in all 1,745 plain files (the probe prints `fps=30` through `%g`);
  `u16` count at `0x0C` (e.g. 720, 1136, 256 — key/frame-ish); **`u16` joint count at `0x0E`** (348
  for every wander file, 132 for agro, 35 for `kame_a`, 34 for the bull, 1 for camera files) — the
  joint-count cross-check against the sibling skeleton is the spike's `5/5` agreement and is
  re-observed here (wander 348 = `wander.cskl` boneCount 348 = `wander.glb` 348 skin joints).
- Compressed files have **no readable header**: the `0x14` read has the high bit set in 45% of the
  1,124 compressed files and the `0x0C` read yields float-looking garbage (e.g. `0x4144CCCD`).

### 1.3 Bank taxonomy + classify classes

| class | rule (content-derived) | measured in the sample |
|---|---|---|
| `skb` | `xff` + a `Bone_*`/`OptBone_*` name blob | 57 / 57 `.skb` |
| `anb` | `xff`, no name blob, `0x0C == 1` | 4,150 / 4,455 `.anb` |
| `anb-v0` | `xff`, no name blob, `0x0C == 0` | 305 / 4,455 `.anb` |
| `cani` | `80AE` magic (plain) or Oodle + `MINA` footer | 60 / 60 sampled (32 plain, 28 Oodle) |
| `other` | anything else (`.weightinfo` text, `.cskl`, …) | — |

The rule is content-derived, not extension-derived: all 4,455 `.anb` contain no `Bone_` string and
all 57 `.skb` do (measured), so the name blob alone separates the two banks. The `0x0C` flag does
**not** change the header shape or the tail tags (both variants show `{0x000, 0x102}`) — it is
recorded as a variant marker only; its meaning is still open.

**Delta from the plan's Step-3 expectation.** The plan expected three distinct classes: the
`0x0C == 0` variant, a "high-bit-garbage class", and compressed `.cani`. The `0x0C == 0` variant and
compressed `.cani` are real (305 and 1,124 files); a **"high-bit-garbage" content class does not
exist** in this family — measured, every `xff` file has `0x0C ∈ {0, 1}`, a valid `0x10`, and an exact
`0x14` size (4,512/4,512). The only garbage-header class is compressed files (above), which
`classify` reports as `comp=oodle-*` with the two fields marked `(garbage)`.

## 2 · Skeleton bank (`.skb`) — measured

`cp3shadow/skel/player_a.skb` (5,020 B) is the probe's pinned reference; the same walk was run over
all 57 banks (0 failures).

### 2.1 Layout

```
0x000 .. 0x070   header (§1.1)
0x070 .. 0x110   opaque descriptor region
0x110 ..         name blob + records + bind data
0x1060 .. EOF    last section (header 0x10): bind data + the two tail tables
```

- **Name blob.** Starts at `0x110` in **57/57** files. Names are NUL-terminated ASCII, `Bone_*` /
  `OptBone_*`, in two runs: `player_a.skb` holds **52 names** — 29 core (`Bone_KOSI` … 
  `Bone_OYAYUBI_B_R`, blob `0x110..0x284`) then 23 optional (`OptBone_sword` … `OptBone_swordcase3`,
  blob `0x9CC..0xBA3`). Extraction rule used by the probe: every NUL-terminated printable string
  whose text starts with `Bone_`/`OptBone_`, in file order.
- **Bone records** (tail table tag `0x102`): a leading run at **stride `0x40`** — 29 records for
  `player_a.skb` (blob `0x180 + 0x40k`, file `0x290…`) — followed by **3 trailing section
  descriptors** (measured 3 in 56/57 files, 2 in `obj_bat_a.skb`). Record fields measured:
  `f0` name offset (blob-relative), `f1`/`f2` = `0xFFFFFFFF`, `f3` = 0, `f4..f7` quat (w = 1.0) —
  `f4..f6` are the first three hex words of the bone's `.weightinfo` line (29/29 records) —
  `f8..f11` second quat (its relation to the sidecar is unresolved for 27 records: it equals the
  `.weightinfo`'s last four hex words for the two roots `Bone_KOSI` and `Bone_HARA` only),
  `f12` 1-based index or `0xFFFFFFFF` for leaves, `f13` related index (unverified), `f14`
  **parent index** (`-1` = root; equals the parent's `f12 - 1`, verified for all 29 records — the two
  roots are `Bone_KOSI` and `Bone_HARA`), `f15` = 0 for 28 of 29 records: `Bone_OYAYUBI_B_R`'s `f15`
  is the first four bytes of the `OptBone_*` run, which starts where its record ends (file `0x9CC`) —
  section data, not a field.
- **Name-offset table** (tag `0x000`): the 29 core name offsets in blob order, then three section
  starts — `0x180` (record array), `0xAA0` (second bind region), `0x8BC` (the `OptBone_*` run).
  The 23 `OptBone_*` names have **no record** in this bank: they are joints only.

### 2.2 Linkage rule (Review Focus 4) — naming + joint-count cross-check

1. **Naming.** Every `.skb` name must appear in the full skeleton's joint names. Measured for
   `player_a.skb`: 52/52 names are present in `wander.glb`'s 348 skin joints (`skb - glb = ∅`).
2. **Sibling list.** The `.skb` name set equals the sibling `.weightinfo` name set — **57/57** pairs
   (the `.weightinfo` is an ASCII sidecar: core lines are `Name` + 10 hex words + 3 ints, 14 fields,
   and optional lines are bare `#OptBone_*`).
3. **Joint count.** The animation-side count (`u16 @0x0E` in the `.cani`, spike `5/5`; re-observed
   wander 348) must equal the skeleton's bone count (`wander.cskl` 348 / `wander.glb` 348 joints).
4. **Bank granularity.** The bank is a **model part's** subset, not the whole skeleton: `player_a.skb`
   = body (52), `player_a_head.skb` = face (36, `Bone_KAO` first). The full skeleton's remaining
   joints (fringe, facial, twist, …) are carried by the same `wander.glb` (348 joints — including the
   28 `Bone_Fringe_*`); `wander_fringe.glb` is mesh-only (1 node, 0 skins). A bank's names are
   therefore always a proper subset of the full skeleton's.

### 2.3 Delta from the plan's pinned values (recorded, not forced)

The plan pinned `check-skb` on: **348 names, first name `Bone_root`, name set == `wander.glb`
joints**. Measured reality for `cp3shadow/skel/player_a.skb`:

| pinned | measured |
|---|---|
| 348 names | **52** names |
| first name `Bone_root` | first name **`Bone_KOSI`**; `Bone_root` is **absent** from the bank |
| name set == glb joints (348) | 52 names ⊂ 348 joints (symmetric difference 296) |
| `Bone_KOSI` and `Bone_MOMO_L` present | **holds** |

The 348 / `Bone_root` values describe the **full** skeleton (`wander.cskl` 348 names — first entry
`|Bone_root` — and `wander.glb`'s 348 skin joints, first joint `Bone_root`), not the part bank. So
`check-skb` **gates on the measured structure** (magic, `0x14` size, `0x0C`, blob start `0x110`, name
extraction, tail tables, records, glb-subset, `.weightinfo` equality — 16/16 pass, exit 0) and
**reports the three plan deltas** as findings, printing the pointer to this section. `--strict-plan`
turns the deltas into an exit-1 failure for anyone who wants the plan's numbers enforced verbatim.

## 3 · Actor clips (`.anb`) — the container decoded, the curve encoding open

Task B2's gate was `player_a_stand`'s per-joint curves, extracted and verified. The container is
decoded to the byte (§3.1–§3.2) and the payload's **content classes** are measured (§3.3), but the
**key encoding itself is not decoded**: the candidate reading (§3.5) fails the gate's own sanity
checks (§3.6), and the joint-count cross-check is pinned to what the data shows. Artifacts (gitignored,
under `out/re/anb/`): `player_a_stand.structure.txt`, `horse_a_add_horse_kick.structure.txt`,
`check-curves.txt`, `player_a_stand.curves.json`, plus the JSON forms of each.

| | |
|---|---|
| Probe modes (this section) | `anb <file> [--json out] [--curves out.json]`, `check-curves [--file F] [--cani F] [--json out]` |
| Gate file | `cp3shadow/anim/player_a_stand.anb` (8,424 B), second sample `horse_a_add_horse_kick.anb` (4,276 B) |
| Gate verdict input | `check-curves` → **exit 1, gate: NOT MET** (§3.6, verbatim) |

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
python3 tools/re/anb/probe.py anb cp3shadow/anim/player_a_stand.anb --json out/re/anb/player_a_stand.structure.json
python3 tools/re/anb/probe.py anb cp3shadow/anim/horse_a_add_horse_kick.anb
python3 tools/re/anb/probe.py anb cp3shadow/anim/player_a_stand.anb --curves out/re/anb/player_a_stand.curves.json
python3 tools/re/anb/probe.py check-curves                      # exit 1 (NOT MET)
```

### 3.1 Container structure (measured)

The header is B1's `xff` header (§1.1) with the fields it left opaque now named. The `0x70..0x110`
region turns out to be a fixed set of descriptor structures — the *same shape in every `.anb`*:
`{0,1,0,0}` at `0x70`, a section record at `0x78` (`{0,0,0,1,8,0,0,0}`), the motion descriptor at
`0x90`, `{0,0x110,0,0}` at `0xB0`, the table descriptor at `0xC0`, and a trailer at `0xE8`
(`{0,1,0,0}` + `0x00010003` + the literal string `.rodata` at `0xFC`) — the string is part of the
file, not a pointer target.

| offset | width | measured (both samples; full-population numbers in §3.1) | meaning |
|---|---|---|---|
| `0x90..0xC0` | 12 × u32 | `{0, X, ext_ptr, ptr, data_size, 0x10, 1, 0, 0, 0x110, 0, 0}` | "motion" descriptor: `X`/`ext_ptr` are pointers with *external* tags (0 for the character samples, non-zero in `objanim_*`), `ptr` = the data start |
| `0xA0` | u32 | 7224 (`player_a_stand`), 2900 (horse) | **data size** = `table1_start − 0xB4` (4455/4455 against the *located* table 1; it fails only against the masked pointer read, §3.1) |
| `0xB4` | u32 | `0x110` (4455/4455) | data offset — the same string/data base as `.skb` |
| `0xC0` | u32 | **9** (4455/4455) | constant, semantics open |
| `0xC4` | u32 | 58 / 69 | **count of *both* tail tables** |
| `0xC8` | u32 | **1** (4455/4455) | constant |
| `0xCC`, `0xD0` | u32 | tagged pointers | table 1 / table 2 pointers: `(tag<<16) + offset`, **addition with carry** (§3.1); `0xD4`/`0xD8` hold the exact offsets untagged |
| `0x10` | u32 | 0x1BF4 / 0xB10 | "last section" — in the character samples this is a block boundary *inside* the data region, not a separate tail (B1's §1.1 scope note); `objanim_*` samples can point into the header instead (`objanim_leo_a_run.anb`: `0x44`) |

- **The tail tables are exact now.** B1 walked back from EOF and absorbed leading zero bytes, so its
  counts were "approximate" (119 vs ~116 for `player_a_stand`). The header's pointers give the exact
  rule: **both** tables hold `count` = u32 `@0xC4` entries of 8-byte `{u32 offset, u32 tag}`; table 1
  starts at the offset in `@0xD4` (tags all `0x102`), table 2 at `@0xD8`'s (tags all `0x000`), and the
  two are **contiguous**: `t2p == t1p + 8*count`. Measured on all 4,455 `.anb`: the untagged mirrors
  locate valid contiguous tables that end exactly at EOF — **4455/4455** (plus 60/60 `anb-v0`).
  `player_a_stand`: table 1 `@0x1D48` (58 entries), table 2 `@0x1F18` (58), ends at EOF. The
  "119 entries" B1 measured for this file = 116 real entries + 3 trailing zero entries of the data
  region (the 24 bytes at `0x1D30..0x1D48`).
- **The tagged pointer is `(tag<<16) + offset` — addition with carry, not `tag<<16 | offset16`.**
  Measured on all 4,455 `.anb`: `u32@0xCC == (tag<<16) + u32@0xD4` and `u32@0xD0 == (tag<<16) +
  u32@0xD8` — **4455/4455**; the untagged `@0xD4`/`@0xD8` equal the exact offsets (4455/4455). B1's
  §1.1 read (`tag<<16 | offset16`) coincides with this while the offset is `< 0x10000` — true for
  every one of the 8 header pointers (`0x20..0x48`) in all 4,455 files — but **not** for the table
  pointers: an offset `>= 0x10000` carries into the tag half, so the low 16 bits are a truncation.
  Measured: 83 files have table 1's offset `>= 0x10000` (deltas `0x10000×{1:63, 2:14, 3:2, 4:1, 8:2,
  11:1}`, i.e. up to `0xB0000` — `objanim_demo_end07_cut001.anb`), 86 files for table 2 (table 2
  straddles the boundary in 3 more). The class is **not** a demo/cut quirk: it is exactly "the table
  lies beyond 64 KiB" and includes boot files (`bird_a_boot`, `snake_c_boot`, `eel_b_boot`) and large
  `objanim_*`. `eel_b_death.anb` is *not* an exception: `@0xD0` = `0x0B120108` → offset `0x10108`
  (its `@0xD8` mirror), the correct table 2 — my B2 draft misread it through the mask.
- **The probe's behavior is unaffected** (verified 4455/4455): it reads the low half (B1's shape) and,
  when that read cannot locate a plausible contiguous pair, falls back to the
  **walk-back-from-EOF rule *with the exact count*** — walking back stops at the first
  non-`{0x102,0x000}` tag and the **last `2*count` entries are [table 1][table 2]** (the walk's extra
  leading entries are the data region's trailing zero bytes, exactly as B1 saw). The `@0xA0`
  data-size cross-check (`data_size == table1_start − 0xB4`) confirms the fallback's table 1 start.
  The probe reports which method it used; a future pass can read `@0xD4`/`@0xD8` directly and keep
  the walk-back purely as a cross-check.
- **11 tagged pointers per file** (measured, both samples, no others anywhere): the 8 B1 fields
  (`0x20..0x48`, offsets `< 0x10000` in all files), `0x9C` (data start, offset `0x110`), and
  `0xCC`/`0xD0` (the tables).

### 3.2 The record map (measured)

- Table 1's offsets are **record starts**; a record runs to the next t1 offset (the last one to
  `t1p`). Sizes are heterogeneous but always multiples of 4 (full histograms, summing to the record
  count):
  `player_a_stand` 58 records: 32 × 4 B, 12 × 340 B, 4 × 176 B, 2 × 12 B, singles 356/360/412/72/516/44/20/300;
  horse 69 records: 35 × 4 B, 18 × 100 B, 9 × 56 B, 1 × 136 B, 1 × 88 B, 1 × 64 B, **1 × 12 B,
  1 × 8 B, 1 × 20 B, 1 × 280 B**.
- **The t2 table's relation to t1 is not decoded (sample-scoped observations only).** In the two
  samples `t2[0] == 0` and `t2[i] == t1[i-1] + 4` for the leading run (16/57 entries for
  `player_a_stand`, 27/68 for the horse). Neither is a format *rule*, but the shift does recur: over
  all 4,455 files `t2[0] == 0` holds for **1613/4455**, and the leading run is non-empty in
  **2749/4455 (62%)** while covering the whole table in **0/4455** and being empty in **1706/4455**;
  the run length spans 0…112 —
  `{0:1706, 1:335, 2:148, 3:105, 4:280, …, 32:82, …, 62:16, 76:6, 111:1, 112:1}` (files). The two
  samples made the "+4 shift" look like a rule; it is a majority pattern, not a universal one. What
  t2 means is **open**; it is *not* a per-record "data pointer".
- The horse's t1 list **starts with the three descriptor structures** (`0x78`, `0xB0`, `0xE8`);
  `player_a_stand`'s does not, although both files' `0x70..0x110` regions are structurally identical.
  Why the lists differ is open.

### 3.3 What the payload contains (measured)

Four content classes, all measured with the probe's per-record flags (`anb` mode):

1. **Unit float32 quaternions — the only *verified* quaternion data in the files.** Norms are
   1.0000000 in float32 (well inside the gate's 1e-3). The counts are **convention-dependent and
   stated as such**: the probe's flag is a per-record scan that advances **4 bytes by default and 16
   after each hit** (a literal 16-byte stride would give 12 / 7), records scanned independently —
   giving **17** quats in 9/58 records for `player_a_stand` and **14** in 10/69 for the horse. The
   same scan over the whole file gives 17 / **15**: for the horse it finds two windows the per-record
   scan lacks (`0x590`, straddling records 14/15; `0x5A0`, fully inside record 15 — a phase artifact
   of the `0x590` hit skipping it) and loses one (`0x59C`, fully inside record 15), net +1. An
   independent 4-byte-aligned *overlapping* window scan gives **19 / 27** (it also counts shifted
   windows, e.g. `0x1CCC`/`0x1CD0` inside the `0x1CC8` window). The *unit norms* are
   convention-independent; only the counts move.
   `player_a_stand`'s hits include
   blocks of 4 at `0x1520` and `0x1A10`, e.g. `(0.74762, 0.03297, 0.03722, 0.66226)`, and
   axis-aligned values like `(-1,0,0,0)`, `(0,0,0,1)`; the horse's include
   `(0.42626, 0.53636, -0.48916, -0.53976)` at `0x590`. They are **too sparse to be a curve** (no
   per-joint series), and their role (bind pose? pose snapshots? sparse keys?) is not decoded.
2. **16-bit streams** dominate the payload. They are smooth under some stride readings (the probe's
   `triplet` column: relative mean |Δ| of the stride-6 channel, 0.05–0.52 for the player's records)
   but see §3.5 — smoothness alone does not identify the encoding.
3. **Constant runs** of identical 4-byte words (e.g. `FF FE FD 2B` ×4, `FF 02 06 24` ×3,
   `02 80 02 80` ×n) — plausibly static/stub tracks, not decoded.
4. **4-byte-group blocks** at the end of many records where **three of the four stride-4 byte planes
   vary smoothly and the fourth is low-order noise** — the signature of a fixed-point value whose
   high bytes vary. Read big-endian and scaled by 2⁻³¹ the values lie in [−1, 1] and several blocks
   are *symmetric with first == last* (`player_a_stand` `0x2F4..0x334`: 0.306678058 → 0.007934333 →
   0.306678058, 16 values; 4-dp rounding gives 0.3067 → 0.0079 → 0.3067) — i.e. they look like a
   looping component series, but a single 4-byte value is not a
   quaternion and the packing is not decoded.

### 3.4 Timing model (inference and its evidence)

**No time or frame fields were found in the `.anb` payload**: no monotonically increasing 16/32-bit
field, no fps float (the `.anb` `0x08` is 0 in every file; the `.cani`'s fps lives at *its* `0x08`),
and no per-record count that matches a frame count. The only fps evidence is the **family**: the
sibling `.cani` clips (2,869 files, §1.2) carry `fps` = 29.999998 (bits `0x41EFFFFF`) in every
plaintext file, and the `.anb` is the same engine's actor-clip container. The candidate extraction
therefore **assumes** uniform key times at the family fps (one key per frame) — an assumption, not a
measurement — and the gate's two timing assertions are reported as **unverified** rather than passing
vacuously. The joint-count cross-check (from the same family): the clip's own `player_a_stand.cani`
is Oodle-compressed and has no readable header, so `check-curves` resolves the character's plaintext
sibling instead (exact stem first, else any plaintext `.cani` of the same character directory —
`player_a_add_grip_stand_crouch.cani`, **348** joints; the horse's `horse_a_add_horse_kick.cani` in
`characters/agro/`, **132** joints).

### 3.5 The extraction recipe (candidate) — and why it is unverified

The candidate the probe emits (`anb <file> --curves`):

> For each record, read `size // 6` little-endian `int16` triplets from the record start as a
> quaternion vector part `(x, y, z)` scaled by `1/32768`; derive `w = sqrt(1 − |v|²)` (keys with
> `|v| > 1` keep `w = 0` and are counted, never silently normalized); times = `k / fps`.

It is emitted **marked `UNVERIFIED`** in the JSON and on stderr, because:

- it **fails the gate's own sanity checks** on `player_a_stand` (§3.6: 428 keys with `|v| > 1`,
  693/1111 adjacent-key dots below 0.9, max norm error 0.732);
- continuity cannot verify it: **any** smooth triplet stream yields continuous derived-`w`
  quaternions (measured — a whole-file scan finds "continuous runs" even inside the tail table,
  which is not curve data), so the reading is unfalsifiable;
- the payload's semantics — which record is which joint, where a record's keys start, how the values
  are packed — are **not** established.

### 3.6 The gate run (verbatim)

`python3 tools/re/anb/probe.py check-curves` (exit 1):

```
check-curves: .../cp3shadow/anim/player_a_stand.anb
  sibling .skb: player_a.skb (52 names)
  sibling .cani: player_a_add_grip_stand_crouch.cani (348 joints) [same character, different clip]
PASS  structure   magic is 78 66 66 00                                 expected 78666600, measured 78666600
PASS  structure   size field @0x14 == file size                        expected 8424, measured 8424
PASS  structure   data size @0xA0 == table1 start - data offset        expected 7224, measured 7224
PASS  structure   table count @0xC4 matches both tables                expected 58, measured [58, 58]
PASS  finding     table location method                                expected header-pointers, measured header-pointers
PASS  structure   tables are contiguous and end at EOF                 expected True, measured (True, True)
PASS  structure   table1 tags are all 0x102                            expected 0x102, measured ['102']
PASS  structure   table2 tags are all 0x000                            expected 0x000, measured ['000']
PASS  structure   records are in range and sized                       expected >0, measured 58
PASS  structure   t2[i] == t1[i-1] + 4 (leading run)                   expected first 4 entries, measured 16/57 entries
PASS  joint-count clip track count == declared count @0xC4             expected 58, measured 58
PASS  joint-count tracks <= full skeleton joints (sibling .cani)       expected <=348, measured 58
DELTA finding     tracks vs part-bank bone names (sibling .skb)        expected 52, measured 58
PASS  curves      curve extraction produces keys                       expected >0, measured 1137
PASS  curves      no NaN quaternions                                   expected 0, measured 0
FAIL  curves      quaternion norms within 1e-3 of 1                    expected <=1e-3, measured 0.7319627110883922
FAIL  curves      adjacent-key quaternion dot > 0.9                    expected 0 pairs below, measured 693/1111
FAIL  curves      keys with |v| > 1 (w undefined)                      expected 0, measured 428
SKIP  curves      times strictly increasing (derived uniform model)    (no time fields in the payload)
SKIP  curves      timing consistent with family fps (29.999998)        (no time fields in the payload)
summary: 13/16 gating assertions pass (2 skipped, 1 finding delta(s))
gate: NOT MET — 3 curve assertion(s) fail; the container decode is
      verified but the curve encoding is not (docs/notes/re-anb.md §3).
```

The same check on the horse sample: same verdict (467 keys, 329/433 dots below 0.9, 156 keys with
`|v| > 1`), with its own sibling resolved (`horse_a_add_horse_kick.cani`, 132 joints, `agro/`) —
69 tracks ≤ 132 ✓, and 69 ≠ the 40-name `horse_a.skb` part bank (same finding shape).

**Joint-count cross-check, pinned to what the data shows** (the plan's "348, or the real count"):
`player_a_stand.anb` declares **58 tracks** (`@0xC4`, = its record count); the part bank
`player_a.skb` has **52** names (B1 §2.3); the full skeleton is **348** joints (the sibling `.cani`).
58 ≤ 348 holds (the clip animates a subset of the full skeleton); 58 ≠ 52 — the tracks are *not* the
part bank's bone list (a finding, not a gate failure). The horse: 69 tracks vs a 40-name part bank.

### 3.7 Hypotheses tested — negative results are results

| hypothesis | checkable prediction | outcome |
|---|---|---|
| int16 `(x,y,z)` Q15 triplets + derived `w` (6-byte keys) | unit norms; adjacent dots > 0.9 | **FAILS** — 428 keys `|v| > 1`; 693/1111 dots < 0.9; norm error 0.732 |
| same triplets as rotation vectors (×π, ×2π, ×1 rad) | continuous rotations | **FAILS** — 780/967, 759/967, 570/967 bad dots (per-record too) |
| float32 quaternions are the keys | unit norms; a per-joint series | quaternions **verified** (norm 1.0000000) but only 17 (player) / 14 (horse) and no series — not a curve |
| int8 quaternions (4-byte keys) | norm ≈ 127 | **FAILS** — norms 13…161 |
| 4 × int16 quaternions (8/12/16/24/32-byte keys; LE/BE; 2³¹/2³⁰/2¹⁵) | unit-norm fraction high | **FAILS** — best unit fraction 0.20 |
| `.cani` `CANIFrameData`-style records (variant codes `0x315`–`0x318`, `0x40F`–`0x41E`) | codes present | **FAILS** — 0 hits for `0x315`–`0x318` in the plaintext `.cani`; 3 incidental `0x412/0x418` hits only — the SOTC revision differs (B1 §1.2) |
| `t2[i] = t1[i] + 4` (rotation/translation block pairs) | holds for all i | not a rule, but it recurs: the leading run is non-empty in **2749/4455** files and covers the whole table in **0/4455** (run length spans 0…112; 1706 have no leading match — §3.2) — semantics open |
| record = `[240][32][u32][64]` (player 340-byte records) | the 8-word block = keys | structure **measured** (records 0/1/3/15 fit it, with a 4-byte marker before the floats in record 1); the float blocks are **not** unit quaternions (norms 0.17…0.97) |
| the "wave" blocks are component curves | monotonic time or unit data | values are smooth and symmetric (first == last) but in [−1,1] as fixed-point; not times, not quaternions as stored |
| bit-packed values (fixed N-bit fields) | N-bit extraction reproduces the smoothness | not found (no N tried made the noisy byte plane smooth) |

### 3.8 Stopping point

The decode stopped at: **container verified, payload classified, key encoding open.** What a next pass
needs is *semantic* ground truth for one record — the Ghidra front's keyframe sampler (B3 was allowed
to stop before it) or a capture-derived reference pose to match against — because the payload's
smoothness is not self-verifying. Until then the S2 converter cannot consume `.anb` clips and the
capture-derived fallback (first-playable design §5) is the armed path.

## 4. Ghidra findings — the animation runtime in eboot.elf

The RE-1 Ghidra front: where the retail runtime touches the `xff` family and the `.cani` bank.
Everything below is quoted from the committed script and the logs it writes under the gitignored
`out/re/`.

| | |
|---|---|
| Date | 2026-10-18 (run stamps UTC) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64 |
| Repo / branch | `sotc-native`, `re1-ghidra` (base `2c768cb`) |
| Baseline | RE-0 acceptance re-run in this worktree: project `out/re/ghidra`, program `eboot.elf`, `x86:LE:64:default`, image base 0, **38,845 functions** (exit 0, 451 s) |
| Script | **`tools/re/ghidra/FindAnim.java`** — mode 1 scan (default), mode 2 `decompile <addr>…`, mode 3 `follow <addr>…` (references to each address + decompile of the referencing functions) |
| Scan log | `out/re/ghidra-anim.log` (16,674 lines; re-run twice — the `FindAnim:` lines are byte-identical, so the scan is deterministic) |
| Follow / decompile logs | `out/re/ghidra-anim-follow{,2,3,4}.log`, `out/re/ghidra-anim-decompile.log`, `out/re/ghidra-anim-motion.log` |

```bash
GHIDRA_HOME=$HOME/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC
proj_link_dir=$(mktemp -d); ln -s "$PWD/out/re/ghidra" "$proj_link_dir/ghidra"
"$GHIDRA_HOME/support/analyzeHeadless" "$proj_link_dir/ghidra" sotc -process eboot.elf \
  -noanalysis -scriptPath "$PWD/tools/re/ghidra" -postScript FindAnim.java > out/re/ghidra-anim.log 2>&1
# mode 3 example (the loading-path walk): -postScript FindAnim.java follow 0x00671e70 0x00670680
```

The dot-free `proj_link_dir` is the RE-0 R-E6 workaround, not a choice: this worktree lives under
`~/.local`, and the plan's verbatim `… out/re/ghidra sotc …` form dies in `ProjectLocator` with
`Path element starting with '.' is not permitted` (verified — the failure log is
`out/re/ghidra-anim-verbatim.log`). `-scriptPath` is passed absolute for the same reason (the
verbatim relative form resolved fine, but absolute is what the record uses).

### 4.1 Scan totals (9 initialized blocks, 77,245,752 bytes)

| pattern | hits | where |
|---|---|---|
| `xff-magic` bytes `78 66 66 00` (u32 LE `0x00666678`) | **1** | data — inside the ASCII string `.xff` (§4.2) |
| `xff-magic-rev` bytes `00 66 66 78` (u32 LE `0x78666600`) | **0** | — |
| `oodle-8c0a` bytes `8c 0a` (u16 LE `0x0A8C`) | 156 | 61 code / 95 data — byte-pattern noise (§4.2) |
| `str-anb` (`anb`, ci) | 4,697 | all data — 4,696 pool names + the format string `anb %s` at `0x012D173B` (§4.6) |
| `str-dot-anb` (`.anb`, ci) | 4,696 | all data — the same pool |
| `str-skb` (`skb`, ci) | 83 | all data — 64 `skel/*.skb` names + 18 `skbush` texture names + the bare `.skb` (§4.6, §4.3) |
| `str-mina` (`MINA`, ci) | 104 | **1 code** / 103 data (§4.5) |
| `str-anim` (`ANIM`/`anim`, ci) | 6,854 | all data — mostly substrings (`ObjAnim`, `NewAnims`, `AnimLoop`); **no bare `anim` string exists** |
| `str-animdata` (`ANIMDATA`, ci) | **0** | — |
| `str-cp3shadow` (`cp3shadow`, ci) | 17 | all data — path templates (§4.6) |

Totals: **16,608 hits**, **53 distinct functions** contain a code hit, `unreadable=0`. The plan's
`ANIM` and `anim` entries are one pattern in the script (case-insensitively identical); the plan's
other nine tokens are scanned as listed. Context is the enclosing printable string for the string
patterns and the ±8 bracketed bytes for the binary ones.

### 4.2 The `xff` magic is never compared — and the Oodle marker hits are noise

- The single `78 66 66 00` hit (`0x01024F2B`) is the ASCII text `.xff` inside the module name
  `nicosys-netguicnf.xff` in the string pool — a coincidence, not a container header. Measured
  context: `…nicosys-netguicnf.` + `78 66 66 00` + `chooseLangScript…`.
- The byte-order variant `00 66 66 78` occurs **0** times. A `cmp`/`mov` immediate of the u32 magic
  (`3d 78 66 66 00` / `b8 78 66 66 00`) contains the same byte run and would have been caught by the
  same pattern, so **no code in `eboot.elf` compares the container magic** (limitation: four separate
  `cmp byte` checks would not be caught by a u32 scan).
- All 156 `8c 0a` hits are two-byte-pattern noise: the code hits sit inside instruction encodings —
  `0f 8c 0a ff ff ff` (`jl rel32`), `c5 fa 10 8c 0a …` (`vmovss` with disp32), `c7 87 8c 0a 00 00 …`
  (`mov [reg+0xA8C], imm`). No code compares `0x0A8C`. Oodle itself **is** linked into the eboot
  (strings `OodleLZ_Decompress`, `OodleLZ_Compress`, source paths `v:\devel\projects\oodle2\core\…`),
  but its block-header handling is inside that library, not an inline constant.

**Negative recorded:** the runtime does not validate the `xff` container by magic; it dispatches by
asset type/name instead (§4.3, §4.6).

### 4.3 The loading path — a named loader registry with `loadCdXffFile` and `loadCdMotionFile`

The strongest structural find: a **named-function registry** (entries at stride `0x18`:
`{const char *name, u64 0, handler}`) in the static data at VA **`0x01606270`–`0x01606D38`**
(**116 entries**, walked contiguously by the pattern "name pointer into `0x0102…`, middle word 0,
handler pointer into code"), holding the asset loaders and the script-callable functions
(`chooseLangScript`, `nico_defaultAI`, `GexInitFunc`, …). Quoted entries (entry VA → handler VA):

| name | entry | handler |
|---|---|---|
| `loadCdNtoFile` | `0x01606588` | `FUN_00671b20` |
| `loadCdXffFile` | `0x01606690` | **`FUN_00671e70`** |
| `unloadCdXffFile` | `0x016066a8` | `FUN_00672090` |
| `loadCdNmoFile` | `0x016066c0` | `FUN_00671350` |
| `loadCdColResFile` | `0x01606720` | `FUN_00672910` |
| **`loadCdMotionFile`** | `0x01606750` | **`FUN_00670680`** |
| `unloadCdMotionFile` | `0x01606768` | `FUN_00670ac0` |
| `loadCdWinTexFile` | `0x01606780` | `FUN_00671d30` |
| `load_npe_file_main` | `0x016067e0` | `FUN_00696560` |

(all 116 registry name strings are packed in `0x01024F2F`–`0x01025980`)

Evidence: `follow 0x00671e70` → data references from `0x016066A0`, `0x017EFC38`, `0x024C0E78` — the
handler pair `{load, unload}` also appears in asset-type descriptor tables at `0x017EFC38` /
`0x024C0E78` (those also carry a sample asset name; `0x024C0EA0` holds the logical name `OLD_BIRD`).
The registry has **no incoming references** (`follow 0x01606630` and `follow 0x01606750` → 0), so the
dispatcher reaches it by computing addresses — not resolved in this pass.

**`loadCdXffFile` = `FUN_00671e70`** (531 bytes) is the xff-family loader — this is where `.skb` is
recognized, by **extension string**, not by content:

```c
lVar4 = thunk_FUN_00f8fc86(param_2,&DAT_0127059d);   /* name vs ".skb" (VA 0x0127059D) */
if (lVar4 != 0) { … lVar6 = thunk_FUN_00f8f646(param_2,0x2f);   /* strrchr '/', basename */
                  pcVar7 = FUN_00a5bd20(uVar5,lVar6 + 1); … FUN_006709c0(uVar5,&local_58); }
```

The load branch itself goes through the engine FS (`FUN_006abe10` → `FUN_00589310`) and, when the
loaded object's flag byte `+0x65` is clear, prints the message at `0x012705B5`
(`objman: …`). `FUN_006709c0` is a `vector::push_back` of a 16-byte string element into the global
list `DAT_03017840` (both `loadCdXffFile` and `loadCdMotionFile` push the basename there).

### 4.4 What the runtime does with a motion file (`loadCdMotionFile` = `FUN_00670680`)

`FUN_00670680` loads the file, then hands the loaded bytes (the file object's field `+0x48`) to
**`FUN_0066fe60`** (1,160 bytes) — a **header-normalization + pointer re-base pass**:

- the u32 triple at `+0x10/+0x14/+0x18` is rotated in place (new `+0x10` = old `+0x14`,
  new `+0x14` = old `+0x18`, new `+0x18` = old `+0x10`);
- nine 32-bit fields **of the header** — `+0x08`, `+0x0C`, `+0x18`, `+0x1C`, `+0x24`, `+0x28`,
  `+0x3C`, `+0x40`, `+0x44` — are checked as offsets: bit 31 clear, low 31 bits non-zero, and
  `value + DAT_02f3b8c0 < DAT_02f08000` → bit 31 is set (`DAT_02f3b8c0` = the load base,
  `DAT_02f08000` = the region limit). The resolved pointer is `(bit31 ? base : 0) + (value & 0x7FFFFFFF)`.
  The set is re-derived from the decompile and the raw disassembly (`orl $0x80000000` followed by
  `movl …, 0xNN(%r13)` at `0x66FEB6`, `0x66FEEE`, `0x66FF1E`, `0x66FF4D`, `0x66FF81`, `0x66FFB5`,
  `0x66FFE9`, `0x670255`, `0x6702BF`); `+0x24` is re-based only when the header's version byte is > 3
  and flag `0x10000` is clear, `+0x3C` only when it is > 8 and the flag is clear;
- `+0x10`/`+0x14` are **counts**, never re-based: the arrays reached from the re-based fields are
  walked with per-element fix-ups `FUN_0066dec0`, `FUN_0066f9a0` (selected by header flag `0x10000`)
  and `FUN_0066e460`. The same predicate also hits a nested field (`+0x18` of the structure a header
  field points at, when the version is > 5 and `(header & 0xf000000000000) == 0x2000000000000`) and
  array elements (stride 4).

After the fix-up, `FUN_00670680` allocates a `0x88`-byte runtime object, copies the 64-bit pair at
data `+0x10` into its first 8 bytes, stores the path at `+0x10` of the object and registers it
(`FUN_006c17f0`). **Feeds B2:** the runtime treats the container's header fields as **31-bit
base-relative offsets with bit 31 as a state flag**, and re-bases/rotates them in place at load —
the field set above (`+0x08…+0x44`) is the list B2's record-layout hypotheses should line up against
the §1.1 header. Of the re-based fields only `0x28`, `0x3C` and `0x44` are tagged-pointer fields in
§1.1's table, while §1.1 measures `0x08`/`0x1C` as 0, `0x0C` as the bank-variant flag, `0x24`/`0x40`
as the constant 2 and `0x18` as opaque — so the re-based structure is not the raw file header (a
runtime sub-structure, or a repurposed header), to be pinned down by B2. The per-element fix-ups
(`FUN_0066dec0`, `FUN_0066f9a0`, `FUN_0066e460`) are the next pass's entry point to the record layout
and to keyframe sampling; they are not decoded here.

### 4.5 `.cani` / `MINA` — the only code reference is a dev console command

The single `MINA` code hit (`0x00A26DD2`, in `FUN_00a26d90`) is a **dev-console command
registration**:

```c
FUN_00144640(local_d8,"Animation",FUN_00a26cf0,0);   /* command group "Animation" */
local_b0 = 0x9414e494d;                              /* bytes: "MINA" 0x09 0 0 0  */
FUN_001444e0(param_1,0x63616e69,local_d8);           /* fourcc: big-endian "cani" */
```

`0x9414E494D` is exactly the `.cani` footer signature measured in §1.2 — magic `MINA` + **version
byte 9** — and `0x63616E69` is the big-endian fourcc of `cani` (the engine's short-name encoding;
memory bytes read `inac`). So the `.cani` signature exists in the binary, but in a **dev tool**
(handler `FUN_00a26cf0`), not in a runtime loader: the loader registry (§4.3) has **no `.cani`
entry**, and an exact-case scan for the string `cani` matches only once more in the image (inside
`motsyncanim_def`; a case-insensitive scan finds 8 — the other 7 inside `CAnim…`/`SyncAnim…` C++
symbol names).

### 4.6 Where the names live — the asset name pools (for B2)

- **Anim names.** 4,696 `anim/*.anb` names in one string region (first anim name start `0x01030993`,
  last `0x0105B300`) — the region holds 5,563 NUL-terminated strings, the other 867 being
  script/symbol names; **613** of the `.anb` names are immediately followed by a logical name
  (`anim/camera_home_e3_start_cam.anb` → `E3_HOME_START_CAM`), the rest sit in runs of consecutive
  `.anb` names (last: `anim/objanim_boss_blood_bow.anb`).
- **Skeleton names.** `skel/*.skb` and the matching `skel/*.weightinfo` as **64 adjacent pairs**
  (64/64 verified; from `0x011A2201`): `skel/boss_cloud_cl.skb` + `skel/boss_cloud_cl.weightinfo`, …
  Delta vs §1/§2: the tree ships **57** `.skb` (+ 57 `.weightinfo`) in `cp3shadow/skel/`, so the pool
  lists **7 skeletons with no shipped file** (`boss_cloud_cl`, `buffalo_a`, `grass_c_cl`, `ico_boy`,
  `ico_girl`, `old_bird_a`, `tree_cl` — absent tree-wide, verified case-insensitively), and the
  shipped names differ from the pool's in **case** (tree lowercase `player_a.skb`, pool `player_A.skb`
  — all 57 shipped names are present in the pool case-insensitively).
- **Bone/motion names.** `Bone_*`/`OptBone_*` names each paired with a Japanese display name from
  `0x0119EE6A`, and motion-state names just before it (`PLAYERBODY`, `strong_atk`, `mid_dam`,
  `hang`, `strong_3D`, …).
- **`follow` on a name returns 0 references** — auto-analysis created no xrefs, because the names
  are reached through **absolute 64-bit pointers in static tables it did not annotate**: the anim
  name `0x01030993` is pointed at by `0x016724B0` (a `0x40`-stride record whose `+0x30` field points
  back at the record start) and by the slot at file offset `0x2ED2168` (Ghidra
  `segment_8::000F7068` — the dynlibdata segment maps into its own address space, space = file −
  `0x2DDB100`; the file−`0x4000` pseudo-VA `0x02ECE168` used elsewhere in this note lands in
  `segment_2`'s zero-filled bss and reads as zeros), inside a `0x30`-stride index region. A raw
  u64-slot scan of the dump (rule: every 8-byte-aligned little-endian u64 in `out/re/eboot.elf` whose
  value lands in the stated range; the committed script does not scan pointer slots, so the rule is
  stated here) finds **14,177** slots into the anim-name range `[0x01030993,0x0105B320)`, **266**
  into the skel pool `[0x011A2201,0x011A2D94)` and **2,113** into the bone/motion table
  `[0x0119EE6A,0x011A2201)`; an any-offset u64 scan adds unaligned coincidences (14,407 / 266 /
  2,151). The engine's `CP3Shadow` path templates
  (`$/cp3shadow/`, `/app0/CP3Shadow/%s`, `$/cp3shadow/****/cp3shadowprecache.calt`, …) are separate
  strings; the ones followed here lead to dev tooling (a config UI registering the `.calt` files —
  `FUN_00533100`; a path-data export tool — `FUN_00689b80`; a text config parser — `FUN_007f6f90`).

### 4.7 Not reached / next pass

- The `.anb` **record layout and keyframe sampling** are not decoded here: the load path stops at
  `FUN_0066fe60` + the three per-element fix-ups (§4.4). Next: decompile `FUN_0066dec0`,
  `FUN_0066f9a0`, `FUN_0066e460`, then the motion-object methods (`FUN_00670300`,
  `FUN_006c17f0`'s callee) to reach the sampler.
- The **`.skb` container parse** (the §1.1 header/tag scheme) was not located: `loadCdXffFile` loads
  and registers by name; the object construction (`FUN_00589310`) is the next lead.
- The **registry dispatcher** (who looks a loader up by name) is not referenced from anywhere —
  next pass should follow `0x015FFC68`-style descriptors (`{table_ptr, …}` slots pointing into the
  registry) and the asset-type descriptor tables at `0x017EFC38` / `0x024C0E78`.
- `.cani` runtime loading: no evidence in this pass (§4.5); the format's runtime consumer is still
  open, and B2's `.cani` decode stands on the offline probe alone.
