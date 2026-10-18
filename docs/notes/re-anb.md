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
(57 + 57). The `.cani` family (2,869 files, 52 `…/animation/_cmn/` directories) is a **separate
container** with its own magic but the same little-endian field style; see §1.2.

### 1.1 `xff` header (measured on all 4,512 `.anb` + `.skb` files)

| offset | width | value (verified across 4,512 files) | example (`player_a.skb`) |
|---|---|---|---|
| `0x00` | u32 | magic `78 66 66 00` | `78666600` |
| `0x04` | u32 | 0 (all files) | `0` |
| `0x08` | u32 | 0 (all files) | `0` |
| `0x0C` | u16 | **bank variant flag**: 1 for 4,150 `.anb`, 0 for 305 `.anb` and all `.skb` | `0` |
| `0x0E` | u16 | 0 (all files) | `0` |
| `0x10` | u32 | offset of the file's **last section** (≤ size in all files; the section is ≥ 400 B) | `0x1060` |
| `0x14` | u32 | **file size**, exact in all 4,512 files | `0x139C` = 5,020 |
| `0x18` | 8 B | opaque: all zero in 3,451 files, otherwise 4 ASCII bytes + 4 zeros | `"yubi\0\0\0\0"` |
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
- **`0x18` is opaque.** The 4 ASCII bytes differ per file, never relate to that file's own names or
  to its siblings, and mostly consist of fragments of unrelated text (`"yubi"`, `"nigh"`, `"wpos"`,
  `"ssio"`, `" 100"`, …). Working hypothesis: a packer buffer leftover (uninitialized field). Treat
  as opaque; do not read as a pointer.
- **Tail tables.** Every container ends with one or two adjacent tables of 8-byte
  `{u32 offset, u32 tag}` entries, tags `0x102` (measured: `.skb` bone records / `.anb` descriptor
  fields) and `0x000`. Offsets are **relative to the container's string/data base `0x110`**
  (verified for `.skb` by resolving names; the same base resolves the `.anb` descriptor fields).
  Reading the tables back-to-front from EOF is exact for `.skb` (measured 64 = 32 + 32 entries in
  `player_a.skb`, every `0x000` entry resolving into the name blob); for `.anb` the same walk can
  absorb leading zero padding (measured 119 vs ~116 entries for `player_a_stand.anb`), so treat
  `.anb` tail counts as approximate until B2 decodes that bank.
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
- Plaintext header fields (measured on the sample): `float fps = 30.0` at `0x08`; `u16` count at
  `0x0C` (e.g. 720, 1136, 256 — key/frame-ish); **`u16` joint count at `0x0E`** (348 for every
  wander file, 132 for agro, 35 for `kame_a`, 34 for the bull, 1 for camera files) — the joint-count
  cross-check against the sibling skeleton is the spike's `5/5` agreement and is re-observed here
  (wander 348 = `wander.cskl` boneCount 348 = `wander.glb` 348 skin joints).
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
  `f0` name offset (blob-relative), `f1`/`f2` = `0xFFFFFFFF`, `f3` = 0, `f4..f7` quat (w = 1.0),
  `f8..f11` second quat (the same floats the sibling `.weightinfo` carries, verified on
  `Bone_KOSI`/`Bone_MOMO_L`), `f12` 1-based index or `0xFFFFFFFF` for leaves, `f13` related index
  (unverified), `f14` **parent index** (`-1` = root; equals the parent's `f12 - 1`, verified for all
  29 records — `Bone_KOSI` and `Bone_HARA` are the two roots), `f15` 0.
- **Name-offset table** (tag `0x000`): the 29 core name offsets in blob order, then three section
  starts — `0x180` (record array), `0xAA0` (second bind region), `0x8BC` (the `OptBone_*` run).
  The 23 `OptBone_*` names have **no record** in this bank: they are joints only.

### 2.2 Linkage rule (Review Focus 4) — naming + joint-count cross-check

1. **Naming.** Every `.skb` name must appear in the full skeleton's joint names. Measured for
   `player_a.skb`: 52/52 names are present in `wander.glb`'s 348 skin joints (`skb - glb = ∅`).
2. **Sibling list.** The `.skb` name set equals the sibling `.weightinfo` name set — **57/57** pairs
   (the `.weightinfo` is an ASCII sidecar, one line per bone: `[#]Name` + 9 hex words + 3 ints).
3. **Joint count.** The animation-side count (`u16 @0x0E` in the `.cani`, spike `5/5`; re-observed
   wander 348) must equal the skeleton's bone count (`wander.cskl` 348 / `wander.glb` 348 joints).
4. **Bank granularity.** The bank is a **model part's** subset, not the whole skeleton: `player_a.skb`
   = body (52), `player_a_head.skb` = face (36, `Bone_KAO` first), `wander_fringe.glb` holds the
   fringe bones. A bank's names are therefore always a proper subset of the full skeleton's.

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
| `str-anb` (`anb`, ci) | 4,697 | all data — the anim name pool (§4.6) |
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

(the name strings themselves are packed at `0x010253C0`–`0x010254A9`)

Evidence: `follow 0x00671e70` → data references from `0x016066A0`, `0x017EFC38`, `0x024C0E78` — the
handler pair `{load, unload}` also appears in asset-type descriptor tables at `0x017EFC38` /
`0x024C0E78` (those also carry a sample asset name; `0x024C0E98` holds the logical name `OLD_BIRD`).
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
- nine 32-bit fields — `+0x08`, `+0x0C`, `+0x10`, `+0x14`, `+0x18`, `+0x1C`, `+0x28`, `+0x40`,
  `+0x44` — are checked as offsets: bit 31 clear, low 31 bits non-zero, and
  `value + DAT_02f3b8c0 < DAT_02f08000` → bit 31 is set (`DAT_02f3b8c0` = the load base,
  `DAT_02f08000` = the region limit). The resolved pointer is `(bit31 ? base : 0) + (value & 0x7FFFFFFF)`;
- arrays reached from those fields are walked with per-element fix-ups `FUN_0066dec0`,
  `FUN_0066f9a0` (selected by header flag `0x10000`) and `FUN_0066e460`, with counts read from the
  same header (`+0x10`/`+0x14`).

After the fix-up, `FUN_00670680` allocates a `0x88`-byte runtime object, copies the 64-bit pair at
data `+0x10` into its first 8 bytes, stores the path at `+0x10` of the object and registers it
(`FUN_006c17f0`). **Feeds B2:** the runtime treats the container's header fields as **31-bit
base-relative offsets with bit 31 as a state flag**, and re-bases/rotates them in place at load —
the field set above (`+0x08…+0x44`) is the list B2's record-layout hypotheses should line up against
the §1.1 header (only `0x28` and `0x44` of it are tagged-pointer fields in §1.1's table; `0x10`/`0x14`
are its last-section-offset/file-size pair — so the loader is reading a *sub-structure* of the
container, or a second header variant; to be pinned down by B2). The per-element fix-ups
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
entry**, and the string `cani` does not occur anywhere else in the image (the one other `cani`
byte match is inside `motsyncanim_def`).

### 4.6 Where the names live — the asset name pools (for B2)

- **Anim names.** 4,696 `anim/*.anb` names in one string region (first anim name `0x01030993`, last
  `0x0105B31B`) — the region holds 5,563 NUL-terminated strings, the other 867 being script/symbol
  names; **613** of the `.anb` names are immediately followed by a logical name
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
  back at the record start) and by `0x02ECE168` (a `0x30`-stride index region); a raw scan of the
  image finds **14,935** 8-byte slots pointing into the anim pool, **394** into the skel pool and
  **1,353** into the bone/motion table. The engine's `CP3Shadow` path templates
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
