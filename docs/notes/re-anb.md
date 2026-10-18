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

- **Tagged pointers.** A pointer is `tag << 16 | offset`, where `tag` is the u16 at `0x22` and
  `offset` is a file offset. Verified for all 8 pointer fields in all 4,512 files (the tag field
  varies per file — e.g. `0x0A1D` here, `0x0A25` for `player_a_stand.anb` — and is the same value in
  every pointer of a file). The `0x50` mirror table always reproduces the offsets.
- **`0x10` (last section).** Measured ≤ size in all 4,512 files; the section it starts is ≥ 396 B —
  the four smallest are the 556-byte banks `gextail_a.skb`, `guard_c_head.skb`, `guard_d_head.skb`,
  `guard_e_head.skb` (396 B each; the next smallest is 412 B).
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
  entries for `player_a_stand.anb`), so treat `.anb` tail counts as approximate until B2 decodes
  that bank.
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
