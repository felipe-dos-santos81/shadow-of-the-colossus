# RE-3 stage composition — the `.fs` family + the inventory (2026-10-18)

RE-3 (S3 "the arena" design §5): the composition family — `cp3shadow/data/fs/stage/*.fs`,
`cp3shadow/data/fs/stagetexseg_def/*.fs`, `cp3shadow/data/fs/rootseg.fs`,
`workspaces/_ps4/shadow.cwks`, `cp3shadow/path/**/*.path`, `cp3shadow/camdata/*.csv` — mined
from measured structure, not binary RE. This file grows with the workstream: **§1 (this
commit)** is the `.fs` family + the inventory; §2 the workspace string pool; §3–§4 the arena
manifest and the escalation decision (W2.2/W2.3).

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
| Probe | **`tools/re/stage/probe.py`** — `fs <file> [--json out]`, `classify`, `check-fs` |

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
python3 tools/re/stage/probe.py check-fs                 # 10/10 gating assertions pass (exit 0)
python3 tools/re/stage/probe.py classify                 # one line per family (verbatim below)
python3 tools/re/stage/probe.py fs 1010.fs --json out/re/stage/1010.fs.json
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
