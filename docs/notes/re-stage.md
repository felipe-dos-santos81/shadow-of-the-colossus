# RE-3 stage composition — the `.fs` family + the inventory (2026-10-18)

RE-3 (S3 "the arena" design §5): the composition family — `cp3shadow/data/fs/stage/*.fs`,
`cp3shadow/data/fs/stagetexseg_def/*.fs`, `cp3shadow/data/fs/rootseg.fs`,
`workspaces/_ps4/shadow.cwks`, `cp3shadow/path/**/*.path`, `cp3shadow/camdata/*.csv` — mined
from measured structure, not binary RE. This file grows with the workstream: **§1 (commit 1)** is
the `.fs` family + the inventory; **§2 (commit 2)** the workspace dependencies file
(`shadow.cwks`) + the arena path list; **§3–§4 (this commit)** the arena manifest (the
composition) + the escalation decision.

Every value below is measured with `tools/re/stage/probe.py`; the 2026-10-18 recon's starting
facts were **re-verified, not trusted** — the two deltas found are recorded in place (§1.2).
Inputs stay read-only, derived dumps land in the gitignored `out/re/stage/`, and only counts,
offsets, sizes, and identifiers appear here — no game content.

| | |
|---|---|
| Date | 2026-10-18 |
| Machine | Apple M3 Max, macOS 27.0.1, arm64; `python3` 3.12.12 (stdlib only) |
| Repo / branch | `sotc-native`, `re3-stage` |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Converted library | `/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library` (the main checkout's `out/library`, ~12 GB, gitignored — a worktree has none) |
| Probe | **`tools/re/stage/probe.py`** — `fs <file> [--json out]`, `classify`, `check-fs`, `cwks [<file>] [--json out] [--arena-out out] [--match substr]`, `check-cwks`, `compose --library <dir> --out <file> [--paths <file>]`, `check-manifest [<file>] --library <dir>` |

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
python3 tools/re/stage/probe.py check-fs                 # 10/10 gating assertions pass (exit 0)
python3 tools/re/stage/probe.py classify                 # one line per family (verbatim below)
python3 tools/re/stage/probe.py fs 1010.fs --json out/re/stage/1010.fs.json
python3 tools/re/stage/probe.py check-cwks               # 13/13 gating assertions pass (exit 0)
python3 tools/re/stage/probe.py compose --library /Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library \
    --out out/re/stage/valus.manifest
python3 tools/re/stage/probe.py check-manifest --library /Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library \
    out/re/stage/valus.manifest                          # 11/11 gating assertions pass (exit 0)
```

Files resolve against the game tree; a bare `.fs` name also resolves inside the family's
directories (`fs 1010.fs`, `fs 650005.fs`, `fs rootseg.fs` all work from anywhere).

## 1 · The `.fs` family

### 1.1 The layout (byte-exact)

Measured on all **1,896** `cp3shadow/data/fs/stage/*.fs` files (and the two sibling `.fs`
families — §1.2); `check-fs` re-verifies it on every run:

| offset | width | value (verified across all 1,896 stage files) | example (`1010.fs`) |
|---|---|---|---|
| `0x00` | u32 | entry count `n` | `50` |
| `0x04 + 8i` | u32 | `v` — a small integer (family range `[1, 257]`, 202 distinct) | `0x95` |
| `0x08 + 8i` | u32 | `hash32` — a 32-bit hash (namespace unresolved — §1.3) | `0x4FBE65EE` |
| — | — | **`size == 4 + 8·n` exactly** (no trailing bytes) | `404 == 4 + 8·50` |

- **The 4+8·n identity holds on 1,896/1,896** files (also on all 112 `stagetexseg_def` files
  and `rootseg.fs`, §1.2); file sizes span **20..6,340 B** (675,048 B total), `n` spans
  **2..792**.
- **Family totals** (measured, all 1,896 files): **83,433 entries**; `v ∈ [1, 257]`, **202
  distinct** values; **17,686 distinct `hash32`** values. Distinct `(v, hash32)` pairs also
  number 17,686 — i.e. **each hash always carries the same `v`** (measured; whether `v` is
  derived from the hash or paired independently is not decoded).
- **`v` histogram, top values** (entry counts): `1`×25,877 · `2`×7,215 · `97`×5,906 ·
  `117`×5,724 · `21`×4,225.
- **Entry order is not sorted** (neither by `v` nor by `hash32`); the leading run often descends
  by 1 — `874.fs` is `(0x5D, …), (0x5C, …)`, `1010.fs` opens `0x95…0x90` before breaking.
  329/1,896 files are strictly descending-by-1 throughout.
- **Pinned samples** (`check-fs`):
  - `1010.fs` — 404 B, `n = 50`; first five entries verbatim:
    `(0x95, 0x4FBE65EE) (0x94, 0x03BF0117) (0x93, 0x2E3D3989) (0x92, 0xFBAF84B4) (0x91, 0x20D6F2C2)`.
  - `874.fs` — 20 B, `n = 2`: `(0x5D, 0x3C80C90C) (0x5C, 0xA60BDE96)`.
- **Hypothesis (marked, not decoded):** `.fs` = a per-stage segment/file list — the location
  (`data/fs/stage/<id>.fs`) and the plan's "stage composition" framing support it, but the
  semantics of `v` and `hash32` are open (§1.3). Nothing beyond the layout above is claimed.

### 1.2 The inventory (measured; `classify` output verbatim)

```text
classify: SOTC stage-composition families under /Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
  cp3shadow/data/fs/stage/*.fs            1896 files size 20..6340 (675048 B)  4+8n 1896/1896  names 100023.fs..9916.fs
  cp3shadow/data/fs/stagetexseg_def/*.fs   112 files size 12..564 (15928 B)  4+8n 112/112  names 650005.fs..672584.fs
  cp3shadow/data/fs/rootseg.fs               1 file  size 244 (244 B)  4+8n 1/1
  workspaces/**/*.cwks                       1 file  size 29398788 (29398788 B)
  cp3shadow/path/**/*.path                 144 files size 48..30832 (1008640 B)
  cp3shadow/camdata/*.csv                   20 files size 129..24057 (74341 B)
  total: 2174 files in 6 families
```

Tree-wide cross-checks (measured): `.fs` files number **2,009** = 1,896 + 112 + **1**; `.cwks`
1; `.path` 144 (all under `cp3shadow/path/`: 3 top-level + 141 in `pathdata/`); `.csv` 22 (the
camdata 20 + 2 NICO build lists under `cp3shadow/`, not this family).

**Two deltas against the recon's starting facts** (re-measured, recorded, not forced):

1. **`rootseg.fs` is a third `.fs` family member** the inventory lists did not name:
   `cp3shadow/data/fs/rootseg.fs`, 244 B, `n = 30`, same `4+8·n` layout, all `v` ≤ 0xFF
   (first entries `(0xFF, 0x13372745) (0xF8, 0xF7365721) (0xF4, 0xD555402C) (0xF3, 0xCF4286C6)`,
   descending). `classify` reports it as its own group; `check-fs` stays scoped to the 1,896
   stage files (per the plan's pins). Hypothesis (marked): the family's root list.
2. **`stagetexseg_def` names span prefixes `65`–`67`** (`650005.fs` … `672584.fs`), not only
   `65…`; the count (112) and the layout match. All 112 also satisfy `4+8·n` (sizes 12..564 B).
   (`classify` prints the sorted name range.)

### 1.3 The hash-namespace open item (unresolved)

The `hash32` field's namespace is **not decoded, and no mapping is claimed**. The plan's recon
recorded 0 hits over its variant set; this pass re-ran a superset scan and records exactly what
it measured (evidence: `out/re/stage/hashscan.txt` + `hashscan.py`, both gitignored):

- **Targets:** the family's 17,686 distinct `hash32` values.
- **Corpus A** — every game-tree file (130,158 files): relative path / basename, with / without
  extension, each as-is / lower / upper → 943,751 unique candidates.
- **Corpus B** — the workspace pool's raw `$…\0` ASCII strings from `shadow.cwks` (408,587
  strings; the structured pool parse is §2's): with `$`, without `$/`, without `$`, basename
  with / without extension, each as-is / lower / upper → 765,669 unique candidates.
- **Union:** 1,265,721 unique candidates × 8 conventions = **10,125,768 computations**.
- **Conventions:** FNV-1a-32, FNV-1-32, CRC32, Murmur3-x86-32 (seed 0), xxh32 (seed 0), djb2,
  sdbm, java31 — each verified against canonical test vectors before the scan.
- **Result: no convention found.** 34 hits total (fnv1a32 7, fnv1_32 8, crc32 3, murmur3 4,
  xxh32 3, djb2 6, sdbm 2, java31 1) — but a control pass over the XOR-shifted target set
  (`targets ^ 0xA5A5A5A5`; same corpus, same functions) hit **43**, and the expected
  coincidence count for a random function at this scan size is **41.7**. The hits are birthday
  collisions, not matches: **no convention covers more than 8 of the 17,686 targets**, where a
  true convention would cover thousands (the family's 83,433 entries reference 17,686 distinct
  hashes). The recon's 0 hits and this run's 34 are the same arithmetic at different corpus
  sizes (0 is the expected outcome below ≈2.4×10⁵ probes at this target count).
- **Reusable near-miss (verified; matches CTAL, not these hashes):** the vendored library's
  `CTALEntryHash` (`third_party/PSO2-Aqua-Library/AquaModelLibrary.Data/BluePoint/CTAL/CTALEntryHash.cs`)
  documents BluePoint CTAL entries as "32bit FNV-1a hashes … corresponding to
  their original name"; verified against the vendored pinned values — FNV-1a-32 of the
  **UPPERCASE name** (`TEX_HEADER_CHARCREATOR_ICON` → `0xADF4288B`) reproduces CTAL exactly.
  FNV-1a over UPPERCASE names is among the variants tested above (fnv1a32 hit 7 targets — the
  coincidence rate), so the studio's CTAL convention does not carry over to the `.fs` hashes.
- **Not tested (recorded for later targets):** NUL-terminated inputs, byte-swapped output,
  salted/chained forms, and any convention over strings absent from both corpora (e.g. names
  assembled at runtime). The structured pool parse (§2) is the natural next corpus.

## 2 · The workspace dependencies file (`shadow.cwks`)

### 2.1 What it is (measured; refined by the shipped eboot)

`workspaces/_ps4/shadow.cwks` — **29,398,788 B**, the tree's only `.cwks`. The recon framed it as a
"workspace string pool"; the shipped `eboot.bin` (`SOTC-extract/extracted/uroot/`, plain strings,
read-only) refines that: it is the **resource dependency file** the engine loads at startup —

- `+ResourceDependenciesFile=$/workspaces/_ps4/shadow.cwks` (the ini line naming it),
- `Bad fourcc/version combo %8.8x/%d for workspace dependencies file: %s` (the header check),
- `Failed to open the workspace dependencies file: %s` / `Resource dependency file loaded: %s`,
- `ResourcePool::LoadResourceDependencies(const bpe::String &)` and
  `ResourcePool::GetResourceDependencies(const ResourceId &, std::vector<ResourceRef> &)`.

So: a resource→dependencies map (path strings + per-record index), not a plain string pool. The
parse below is **structural only**; the record semantics stay open (§2.4) and nothing about the
engine's lookup is claimed beyond what the strings say.

### 2.2 The measured PSKW structure

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
python3 tools/re/stage/probe.py check-cwks                 # 13/13 gating assertions pass (exit 0)
python3 tools/re/stage/probe.py cwks                       # structure dump (verbatim below)
python3 tools/re/stage/probe.py cwks --json out/re/stage/shadow.cwks.json
python3 tools/re/stage/probe.py cwks --arena-out out/re/stage/arena_paths.txt
```

`cwks` with no file argument defaults to `workspaces/_ps4/shadow.cwks`; a bare name also resolves
under `workspaces/_ps4/`. Every value below is re-measured 2026-10-18 with `probe.py` (never
forced from the recon):

| offset | width | value (measured on the one file) |
|---|---|---|
| `0x00` | 4 B | `PSKW` — fourcc (the eboot's "fourcc/version combo" check) |
| `0x04` | u32 | `6` — version |
| `0x08` | u32 | `130234` — semantics open |
| `0x0C` | u32 | `67045` — index record count (verified: `0x10 + 67045·16 == 0x105E60`, the pool start) |
| `0x10` | 16 B × 67045 | index records, ending exactly at the pool start |
| `0x105E60` | 28,326,052 B | the string pool, to EOF (`0x1C09704`) |

**Index records** — four u32 fields each; measured, *not decoded*: field[0] is **non-decreasing**
across all 67,045 records (20,669 → 4,294,747,458 = `0xFFFCA542`; 0 decreases, 2 equal adjacent
pairs at records 23589/42109); fields[1] and [3] point into the string pool for **63,664 / 63,663**
of the records (the rest point into the record region). First and last records verbatim:
`(0x50BD, 0xFD20B6, 1, 0xFD2076)` … `(0xFFFCA542, 0x21134A, 8, 0x2111B3)`.

**Two deltas against the recon's starting facts** (re-measured, recorded, not forced):

1. **The index table starts at `0x10`, not `0x08`** — the `0x08` pair (`130234, 67045`) is the
   header's tail; treating it as a first record leaves a half-record before the pool
   (`0x105E60 − 0x08` is not a multiple of 16). With the table at `0x10` the records end
   **exactly** at the pool start.
2. **The record count is `67045`** (the header u32 at `0x0C`), not re-derived from a scan.

### 2.3 The string pool + the arena path list

The pool (from `0x105E60` to EOF) is a flat sequence of NUL-terminated entries in two measured
forms; every path starts with `/` and the walk is validated strictly (any other shape → loud
failure):

- **tagged** `[u8 L]$path\0` — `L` equals the whole entry length (measured on all **340,920**
  tagged entries);
- **bare** `$path\0` — no length byte (**67,045**; 60,660 of them duplicate a tagged entry's
  payload — e.g. a `$/coredata/...` copy next to the `5$/coredata/...` original; the
  record pointers land inside these copies, §2.4).

**Total: 407,965 entries** — the recon's "~408,308" was an estimate 343 high; the measured count
is pinned. Note the 0x24 ambiguity: a bare entry whose path is 35 B long has entry length 36 =
`0x24` = `$`, so the disambiguator is **byte 1** (`$` → tagged), never byte 0 alone (measured:
67,045 bare vs 340,920 tagged).

**The arena path list** — every entry matching `levels/arena_01/` (the level-dir pattern): **142
entries, 72 distinct**; the list's sha256 (pool order, one path per line, trailing newline) is
**`13e3bb60daf3dc91bbf36818b3bc6dd50dc59c27e9730f341d2c4b24d77e5706`** — pinned by `check-cwks`
and saved verbatim to `out/re/stage/arena_paths.txt`. Pattern note (re-measured): the brief's
literal `models/shadow/levels/arena_01/` matches 32 entries (20 distinct), the same without the
trailing `/` 85 (53 distinct, including `arena_01_entrance/`); the recon's pinned **142**
reproduces with the level-dir pattern `levels/arena_01/` (raw occurrences in the file: 142).

The list verbatim (`out/re/stage/arena_paths.txt`, pool order; duplicates preserved as
extracted — the 72 distinct paths are the `_cmn`/`_ps4`/particle set of the arena):

```text
/materials/shadow/levels/arena_01/_ps4/level_01_ground_ruintiles.cmat
/materials/shadow/levels/arena_01/_ps4/level_01_ground_ruintiles.cmat
/models/shadow/levels/arena_01/_cmn/arena_01_hero_rock_3e6a4f11.cmdl
/models/shadow/levels/arena_01/_cmn/arena_01_ground_0c992b51.cmdl
/models/shadow/levels/arena_01/_cmn/arena_01_shared-source_0ef814a3.cmdl
/models/shadow/levels/arena_01/_cmn/arena_01_shadow_caster.cmdl
/models/shadow/levels/arena_01/_cmn/arena01_occluder1.cmdl
/materials/shadow/levels/arena_01/_ps4/arena01_hero_rock_219d5d74.cmat
/models/shadow/levels/arena_01/_cmn/arena_01_hero_rock_b912a496.cmsh
/models/shadow/levels/arena_01/_cmn/arena_01_hero_rock_3e6a4f11.cmdl
/textures/shadow/levels/arena_01/_ps4/arena01_hero_rock_col.ctxr
/textures/shadow/levels/arena_01/_ps4/arena01_hero_rock_rgh_b444ba4e.ctxr
/textures/shadow/levels/arena_01/_ps4/arena01_hero_rock_nml.ctxr
/materials/shadow/levels/arena_01/_ps4/arena01_hero_rock_219d5d74.cmat
/models/shadow/levels/arena_01/_cmn/arena_01_hero_rock.cmsh
/models/shadow/levels/arena_01/_cmn/arena_01_hero_rock_b912a496.cmsh
/materials/shadow/levels/arena_01/_ps4/level_01_ground_ruintiles.cmat
/models/shadow/levels/arena_01/_cmn/arena_01_ground_7391f55a.cmsh
/models/shadow/levels/arena_01/_cmn/arena_01_ground_0c992b51.cmdl
/models/shadow/levels/arena_01/_cmn/arena_01_ground.cmsh
/models/shadow/levels/arena_01/_cmn/arena_01_ground_7391f55a.cmsh
/models/shadow/levels/arena_01/_cmn/arena_01_shared-source_4a9b2556.cmsh
/models/shadow/levels/arena_01/_cmn/arena_01_shared-source_0ef814a3.cmdl
/models/shadow/levels/arena_01/_cmn/arena_01_shared-source.cmsh
/models/shadow/levels/arena_01/_cmn/arena_01_shared-source_4a9b2556.cmsh
/models/shadow/levels/arena_01/_cmn/arena_01_shadow_caster.cmsh
/models/shadow/levels/arena_01/_cmn/arena_01_shadow_caster.cmdl
/models/shadow/levels/arena_01/_cmn/arena01_occluder1.cmsh
/models/shadow/levels/arena_01/_cmn/arena01_occluder1.cmdl
/particles/shadow/levels/arena_01/_ps4/arena_01_dustmotes_01.cprt
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small.cprt
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_dust_small.cprt
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large.cprt
/particles/shadow/levels/arena_01/_ps4/arena_01_dustmotes_01/emitter_0_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_dustmotes_01/emitter_0_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_dustmotes_01/emitter_0_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_dustmotes_01.cprt
/particles/shadow/levels/arena_01/_ps4/arena_01_dustmotes_01/emitter_0_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_dustmotes_01/emitter_0_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_dustmotes_01/emitter_0_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_0_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_0_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_0_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_1_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_1_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_1_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_2_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_2_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_2_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_3_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_3_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_3_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small.cprt
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_0_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_0_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_0_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_1_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_1_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_1_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_2_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_2_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_2_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_3_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_3_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small/emitter_3_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_dust_small/emitter_0_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_dust_small/emitter_0_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_dust_small/emitter_0_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_dust_small/emitter_1_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_dust_small/emitter_1_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_dust_small/emitter_1_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_dust_small.cprt
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_dust_small/emitter_0_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_dust_small/emitter_0_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_dust_small/emitter_0_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_dust_small/emitter_1_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_dust_small/emitter_1_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_dust_small/emitter_1_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_0_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_0_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_0_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_1_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_1_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_1_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_2_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_2_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_2_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large.cprt
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_0_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_0_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_0_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_1_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_1_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_1_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_2_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_2_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_large/emitter_2_update.csdr
/models/shadow/levels/arena_01/_cmn/arena_01_skirt_1d2ff767.cmdl
/models/shadow/levels/arena_01/_cmn/arena_01_6a543ab4.cmdl
/models/shadow/levels/arena_01/_cmn/arena_01_skirt_66158196.cmsh
/models/shadow/levels/arena_01/_cmn/arena_01_skirt_1d2ff767.cmdl
/models/shadow/levels/arena_01/_cmn/arena_01_skirt.cmsh
/models/shadow/levels/arena_01/_cmn/arena_01_skirt_66158196.cmsh
/models/shadow/levels/arena_01/_cmn/arena_01_aee5e9a9.cmsh
/models/shadow/levels/arena_01/_cmn/arena_01.cclm
/models/shadow/levels/arena_01/_cmn/arena_01_6a543ab4.cmdl
/models/shadow/levels/arena_01/_cmn/arena_01.cmsh
/models/shadow/levels/arena_01/_cmn/arena_01_aee5e9a9.cmsh
/materials/shadow/levels/arena_01/_ps4/level_01_ground_ruintiles2.cmat
/materials/shadow/levels/arena_01/_ps4/level_01_ground_ruintiles2.cmat
/materials/shadow/levels/arena_01/_ps4/level_01_ground_ruintiles2.cmat
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_flow.cprt
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_flow/emitter_0_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_flow/emitter_0_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_flow/emitter_0_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_flow.cprt
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_flow/emitter_0_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_flow/emitter_0_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_flow/emitter_0_update.csdr
/materials/shadow/levels/arena_01/_ps4/level_01_ground_ruintiles2.cmat
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_dust_small.cprt
/particles/shadow/levels/arena_01/_ps4/arena_01_falling_rocks_small.cprt
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_fall.cprt
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_fall/emitter_0_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_fall/emitter_0_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_fall/emitter_0_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_fall/emitter_1_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_fall/emitter_1_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_fall/emitter_1_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_fall.cprt
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_fall/emitter_0_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_fall/emitter_0_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_fall/emitter_0_update.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_fall/emitter_1_emit.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_fall/emitter_1_spawn.csdr
/particles/shadow/levels/arena_01/_ps4/arena_01_ent_dust_fall/emitter_1_update.csdr
/materials/shadow/levels/arena_01/_ps4/level_01_ground_ruintiles_b490955e.cmat
/materials/shadow/levels/arena_01/_ps4/level_01_ground_ruintiles_b490955e.cmat
/materials/shadow/levels/arena_01/_ps4/level_01_ground_ruintiles.cmat
/materials/shadow/levels/arena_01/_ps4/level_01_ground_ruintiles.cmat
/materials/shadow/levels/arena_01/_ps4/level_01_ground_ruintiles.cmat
/materials/shadow/levels/arena_01/_ps4/level_01_ground_ruintiles.cmat
```

### 2.4 The record-semantics open item (unresolved)

The four record fields are **not decoded**, and no mapping is claimed. What is measured:

- field[0] is non-decreasing over all 67,045 records (20,669 … 4,294,747,458, just below 2³²).
  Hypothesis (marked, not confirmed): a cumulative byte quantity of the resource payload — the
  deltas are file-size-shaped (26 KB–364 KB on the first records).
- fields[1] and [3] point into the string pool for ~95% of the records; **the pointers land
  mid-string as often as on entry starts** (measured on field[1]: ~2.6k land on a tagged/bare
  entry boundary, ~61k land inside a path, ~3.4k land in the record region outside the pool) —
  so a naive "name pointer" reading is **wrong** and is not adopted here.
- Consecutive records interlock: under the table-at-`0x10` alignment, field[3] of record *i* ≈
  field[1] of the same record (differences 0–1,745 B measured on the first records), i.e. the
  two offsets bracket a small pool region (64–1,745 B) that holds a handful of NUL-terminated
  strings (the record's field[2] is 0–24 on the first records) — consistent with *"a resource
  and its dependency list live in one pool region"*, but the exact field roles (which offset is
  the name, which the list; whether field[2] is a count; why the region boundaries cut strings)
  are **open**.
- The recon's field framing `(u32 count, u32 dataOff, u32 size, u32 nameOff)` is **not
  confirmed**: no field assignment reproduces a clean count/offset/size reading on a sample;
  it stays a candidate to test, never a fact.

The engine side confirms what the file is *for* (§2.1) but not the byte roles. If a future
milestone needs the dependency graph, the bounded Ghidra pass on
`ResourcePool::LoadResourceDependencies` in the shipped eboot is the escalation (design §5);
it was **not** triggered for W2.2.

### 2.5 The tail: the stage-family paths

The pool's last stage-family strings — **13 paths, verbatim**, the first at `0x1C07319`
(matching the recon's ≈`0x1C07318` NUL); they are the **only** entries matching
`cp3shadow/data/fs/stage` anywhere in the pool (measured), i.e. the workspace names its 10
stage segment lists + 3 texture-segment defs exactly once, at the end:

```text
/cp3shadow/data/fs/stage/132391.fs
/cp3shadow/data/fs/stage/136811.fs
/cp3shadow/data/fs/stage/136867.fs
/cp3shadow/data/fs/stage/144144.fs
/cp3shadow/data/fs/stage/145969.fs
/cp3shadow/data/fs/stage/316283.fs
/cp3shadow/data/fs/stage/317197.fs
/cp3shadow/data/fs/stage/346875.fs
/cp3shadow/data/fs/stage/350578.fs
/cp3shadow/data/fs/stage/908.fs
/cp3shadow/data/fs/stagetexseg_def/657159.fs
/cp3shadow/data/fs/stagetexseg_def/659959.fs
/cp3shadow/data/fs/stagetexseg_def/660533.fs
```

All 13 are `[u8 L]$path` tagged entries; the 10 stage ids are among the 1,896 stage `.fs` files
(§1), the 3 defs among the 112 `stagetexseg_def` files. Evidence dumps (gitignored):
`out/re/stage/cwks.dump.txt`, `check-cwks.dump.txt`, `shadow.cwks.json`, `arena_paths.txt`.

## 3 · The arena manifest (the composition)

### 3.1 The instance set — the two level sets, minus the collision glbs

W2's deliverable to W1 is **`out/re/stage/valus.manifest`** (gitignored, regenerable at any
time): the Valus arena composed from the converted library, in the W1↔W2 schema
(`docs/notes/scene-manifest.md`, W1's committed doc). The instance set is the library's two
arena level sets (the spec §1 extent), collision geometry excluded:

| set (under the library) | glb files | `*_collision*` | instances |
|---|---|---|---|
| `models/shadow/levels/arena_01/` | 13 | 1 | **12** |
| `models/shadow/levels/arena_01_entrance/` | 22 | 4 | **18** |
| total | **35** | **5** | **30** |

**The delta against the plan's pin** (re-measured, recorded, not forced): the plan's
`check-manifest` pin says "35 instances (13 + 22)"; the two directories hold 35 glb *files*,
of which 5 are `*_collision*` — under the plan's own set definition ("every non-`_collision`
`.glb`") the instance count is **30**, and `check-manifest` pins the derivation
(35 files − 5 collision = 30) instead of forcing 35.

**Why 30 is the data-supported reading — the cross-check against W2.2's path list.** The
pool answers it twice:

- **`arena_01`** — the saved 142-entry list (`out/re/stage/arena_paths.txt`, sha256
  `13e3bb60…`, §2.3) references **12/12** non-collision `cmsh` and **0** collision `cmsh`
  (the arena's collision *definition* is the sibling `arena_01.cclm`); `compose`
  cross-checks both directions and fails loudly on any mismatch.
- **`arena_01_entrance`** — outside the saved list's pattern (`levels/arena_01/`); the
  pool's own entrance entries (extracted with `cwks --match arena_01_entrance --arena-out
  out/re/stage/entrance_paths.txt`: **187 entries, 43 distinct**) contain exactly the
  **18/18** non-collision `cmsh` and **no** collision `cmsh` — the pool names their
  collision *definitions* instead (4 distinct `.cclm` + 11 distinct `.cmdl` entrance
  entries, none a `.cmsh`).

I.e. the workspace's dependency list references exactly the 30 non-collision meshes and
none of the 5 collision ones — the collision `cmsh` are collision geometry (what the
workspace names are their `.cclm` definitions), not render dependencies; consistent with
spec §1 ("no wall/prop collision; collision is S4's RE-5 pull"). The 4 entrance collision
`cmsh` sources exist in the game tree but are not part of the render composition.

### 3.2 Transforms — identity, the glbs are already world-positioned

Every instance carries the identity TRS (`0 0 0 0 0 0 1 1 1 1`): the converted arena glbs
are authored in world coordinates, so there is no placement transform to apply. The bounds
evidence (re-derived from the accessors on every `check-manifest` run; the plan's numbers
are these rounded — x/z to integers, the entrance's y to 1 dp):

| level glb | x | y | z |
|---|---|---|---|
| `arena_01.glb` | `[237.37, 431.39]` | `[27.87, 69.85]` | `[297.56, 390.04]` |
| `arena_01_entrance.glb` | `[258.60, 345.35]` | `[-1.68, 81.49]` | `[249.30, 306.56]` |

The sets meet near `z ≈ 300` (the arena starts at `z = 297.56`, the entrance reaches
`z = 306.56`). The union bounds over each whole set are larger than its level glb's (the
occluders/shadow-casters/skirts extend past it — the arena set's union is
`x[186.1, 490.4] y[13.5, 110.9] z[286.2, 502.3]`), and the per-glb positions are world
coordinates (e.g. the hero rock sits at `x[295.50, 298.13] z[303.43, 306.51]`, the
entrance occluder at `x[264.96, 345.03]`), which is what makes identity correct for every
instance.

### 3.3 The spawn — a labeled builder pick

The manifest's spawn is a **builder pick, not data-derived** — the manifest's header
comment says so, and this section records why and what the candidates were:

- **The pick** — `spawn 309.312 4.394 285.403 0`: the `arena_01_entrance_ground.glb`
  footprint's centre `(309.3118, 285.4028)` at its surface height `4.3940 m` (ray-cast on
  the mesh's triangles), facing `+Z` (yaw `0` — the app's convention is yaw 0 = +Z). The
  passage floor climbs from `y ≈ 4.4` at `z ≈ 285` to the bowl floor at `y ≈ 30` where
  the sets meet, so the player starts at the entrance floor and walks into the arena. The
  pick lies well inside the entrance bounds; `check-manifest` re-verifies it against the
  re-derived bounds.
- **Candidate 1, not usable as measured** — `cp3shadow/path/pathdata/f4_small.path`: the
  first float4 at offset `0x40` is `(-0.307, 10.457, 20.034, 1.0)` (re-verified), but its
  space does not match the arena's world (|x| ≈ 0.3 vs the arena's `x[237, 431]`; `z ≈ 20`
  vs `[249, 391]`), so no spawn point can be placed from it as measured. The file opens
  `qath` + `u32 3376` (= its own size) + `u32 48 …` — the `.path` format and its
  coordinate space stay unresolved (recorded for later targets, §4).
- **Candidate 2, not usable as measured** — the camdata camera tables: the MINO stages
  `16 / 55 / 63` (re-measured: `stage16` holds `MINO_BOOT_CAMERA1/2` + `MINO_C_*` names,
  `stage55` `MINO_A_*` incl. `st17a_mino_A_start_camera`, `stage63` `MINO_B_*`) are the
  minotaur-family (MINO A/B/C) candidates for the Valus arena, but the tables hold camera
  *parameters* (`Camera, Frame, Focal Distance, Parallax, FOVAdjust, NearPlaneAdj,
  CharMotionBlur`) — no position or facing column — so no spawn follows from them as
  measured.

### 3.4 The manifest + its checks

```bash
python3 tools/re/stage/probe.py compose --library <out/library> --out out/re/stage/valus.manifest
python3 tools/re/stage/probe.py check-manifest --library <out/library> out/re/stage/valus.manifest
```

As emitted (2026-10-18; 31 directive lines = 30 `instance` + 1 `spawn`, plus a 10-line
provenance header; sha256 `088e621894ecd38312526bdac11916fb355a645ecef4d0bace91e53b2440c1d0`):
one `instance` per line, sorted (the `arena_01` block, then `arena_01_entrance`), every
transform the identity, then the labeled spawn. `compose` writes nothing unless the
instance set, the count pins, and the path-list cross-check all hold, and round-trips the
text through the schema before writing. `check-manifest` (11/11 gating assertions, exit 0)
re-derives the library-side facts rather than trusting the manifest: the 13 + 22 file
counts and the 1 + 4 collision split, the exact instance set (both directions, duplicates
rejected), every glb's existence, the identity transforms, one spawn, the spawn inside the
re-derived entrance bounds, and both bounds-evidence matches above. A malformed manifest
(unknown directive, wrong token count, `nan`/`inf`/`1e999`/`1.5x`) renders as structured
FAILs with `<file>:<line>: <reason>` and exit 1 — the schema's own failure shape; a
missing library or path list is a one-line cause, exit 2. The manifest is gitignored and
never committed; W1.5's acceptance consumes exactly this path (the schema doc, §Output
path).

## 4 · The escalation decision (Ghidra: not triggered)

Spec §5 allows one escalation — "if reference resolution stalls, a bounded Ghidra pass on
the `.fs`/workspace loader" — and pins that a curated composition may appear only as a
**labeled demo aid**. **It was not triggered, and it was not needed**: the manifest's
instance set is resolved from data (the converted library's level sets, cross-checked
against the workspace pool, §3.1), and the one non-data element — the spawn — is labeled
as a builder pick (§3.3), not presented as data-derived.

What remains open, recorded for later targets (not on S3's critical path):

1. **The `.fs` hash32 namespace** (§1.3) — unresolved; no convention covers more than 8 of
   the 17,686 targets, and the near-misses are birthday collisions.
2. **The stage↔set mapping** — which stage id names the arena's composition. The
   workspace's own stage-family paths (the cwks tail's 13, §2.5) are the only stage lists
   it names; the mapping stage-id ↔ level-set (and thus a per-stage instance set) is
   unresolved. The arena's set did not need it: the level directories + the pool's arena
   paths gave the set.
3. **The `.path` space** — `f4_small.path`'s float4 lives in a space that does not match
   the arena's world (§3.3), so the `.path` family's coordinate frame and role stay
   unresolved; a data-derived spawn needs it.
4. **The `.cwks` record semantics** (§2.4) — the four record fields remain undecoded.

If a future milestone needs these (a data-derived spawn, per-stage composition, the
dependency graph), the escalation is the same bounded Ghidra pass on the shipped eboot's
stage/workspace loader (`ResourcePool::LoadResourceDependencies` and the `.fs`/stage
loader) — RE-1's "data mining first; Ghidra as needed" pattern. For S3 it stayed unused:
W1 consumes the manifest only, and the manifest is complete without them.
