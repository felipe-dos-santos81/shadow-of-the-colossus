# M0 acceptance results (2026-10-16)

Design gate (§9 of the rebuild design): **"Wander and ≥9 other characters convert reproducibly;
catalog covers `characters/` + `models/`."** — **PASS** (evidence below; §6 records which path
produced which evidence and §7 the deferred classes).

| | |
|---|---|
| Machine / toolchain | macOS arm64; .NET SDK 10.0.401 (Homebrew); SharpGLTF.Core 1.0.7 (checker + tests) |
| Repo / branch | `sotc-native`, `m0/bp-convert` (base `3c765c8`; results committed on top) |
| Game root (read-only) | `$SOTC_GAME_ROOT` = `/Users/felipe.dos.santos/Downloads/SOTC-extract/extracted/game` |
| Library (gitignored) | `out/library/` — catalog: **`out/library/catalog.json`** (schema v1) |
| Batch log (gitignored) | `out/m0-run.log` |
| Acceptance script | **`scripts/m0-acceptance.sh`** (re-runnable: steps 1–3 + the convergence re-run) |

All counts in this file are read from the catalog/report/log produced by the commands shown — no
numbers are estimated except where explicitly marked "≈".

## 1 · Pre-flight: 10 pinned characters

Pinned set (`scripts/m0-acceptance.sh`): `wander` + `agro`, `mono`, `girl_b`, `hawk`,
`colossus_01_minotaur_a`, `colossus_04_kirin`, `colossus_09_desert_tortoise`,
`colossus_14_cerberus`, `colossus_16_mega_maid` — a humanoid mix, a horse, a bird and six colossi
variants. Each sub-directory is converted (`cmsh` only), then every produced `.glb` is loaded with
SharpGLTF and cross-checked against the converter-reported mesh/vertex counts.

```bash
dotnet publish src/BpConvert.Cli -c Release -o out/bin
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models/shadow/characters/wander … --under models/shadow/characters/colossus_16_mega_maid \
  --ext cmsh --jobs 8
#   in scope: 121  skipped: 0  converted: 121  failed: 0  elapsed: 02:40.765
#   catalog: out/library/catalog.json
```

Then the SharpGLTF assertion (the checker is generated into `out/glbcheck/` by the script; it reads
the catalog, loads each glb, compares counts, and asserts every material carries an explicit
`metallicFactor` of 0):

```bash
dotnet exec out/glbcheck/bin/Release/net10.0/glbcheck.dll --lib out/library --require-meshes \
  --prefix models/shadow/characters/wander … --prefix models/shadow/characters/colossus_16_mega_maid
#   summary files=121 loaded=121 failed=0 mismatches=0 materials=389 badMaterials=0 bytes=205847616
#           meshes=1080 vertices=3314835 groups=10 failures=0
```

Numbers below are re-measured after the post-review fix wave (§4a; +19 bytes per material of JSON).

Per-character result (aggregated over the character's `cmsh` set; `mismatches=0` everywhere means
the glb's own mesh/vertex counts equal the catalog's for all 121 files):

| character (`models/shadow/characters/…`) | files | glb bytes | meshes | vertices | load |
|---|---:|---:|---:|---:|---|
| `wander` | 30 | 19,440,248 | 62 | 302,340 | ✓ |
| `agro` | 10 | 12,558,704 | 36 | 154,674 | ✓ |
| `mono` | 3 | 8,251,212 | 18 | 101,820 | ✓ |
| `girl_b` | 2 | 3,610,552 | 7 | 51,729 | ✓ |
| `hawk` | 1 | 487,196 | 1 | 10,264 | ✓ |
| `colossus_01_minotaur_a` | 7 | 41,228,248 | 246 | 523,865 | ✓ |
| `colossus_04_kirin` | 8 | 38,416,772 | 214 | 588,391 | ✓ |
| `colossus_09_desert_tortoise` | 4 | 11,627,076 | 245 | 128,318 | ✓ |
| `colossus_14_cerberus` | 25 | 11,484,904 | 34 | 198,921 | ✓ |
| `colossus_16_mega_maid` | 31 | 58,742,704 | 217 | 1,254,513 | ✓ |
| **total** | **121** | **205,847,616** | **1,080** | **3,314,835** | **10/10** |

Notes: 121 files in 2:41 (8 jobs) — dominated by the co-skinned skirt meshes of the mega maid
(33–43 s each). A few `cmsh` in these directories are geometry-free reference-mesh stubs
(`meshes=0`, 72-byte glb — see §5, class 2); the "≥1 mesh" assertion is applied per character, not per
file, and every one of the ten characters carries real geometry.

## 2 · Largest-model check (Review Focus #4)

The pinned sample directory `models/shadow/objects/actors/statues/colossus_01_minotaur_a/_cmn/`
holds 6 `cmsh`; the largest is `mino_a_statue_broke.cmsh` (589,291 B; next largest 160,646 B).

```bash
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models/shadow/objects/actors/statues/colossus_01_minotaur_a/_cmn --ext cmsh --jobs 8
#   in scope: 6  skipped: 0  converted: 6  failed: 0  elapsed: 00:08.344

# the pinned directory …
dotnet exec out/glbcheck/bin/Release/net10.0/glbcheck.dll --lib out/library --require-meshes \
  --prefix models/shadow/objects/actors/statues/colossus_01_minotaur_a/_cmn
#   group …/_cmn files=6 loaded=6 bytes=2566972 meshes=6 vertices=59871 mismatches=0
#   summary files=6 loaded=6 failed=0 mismatches=0 materials=6 badMaterials=0 bytes=2566972
#           meshes=6 vertices=59871 groups=1 failures=0

# … then the pinned largest source itself (separate invocation: the first matching prefix owns a
# source, so nested prefixes in one call would leave the file's group empty)
dotnet exec out/glbcheck/bin/Release/net10.0/glbcheck.dll --lib out/library --require-meshes \
  --prefix models/shadow/objects/actors/statues/colossus_01_minotaur_a/_cmn/mino_a_statue_broke.cmsh
#   file …/mino_a_statue_broke.cmsh bytes=1974928 cat=3m/44333v glb=3m/44333v ok
#   summary files=1 loaded=1 failed=0 mismatches=0 materials=3 badMaterials=0 bytes=1974928
#           meshes=3 vertices=44333 groups=1 failures=0
```

(Checker outputs re-measured in the post-review fix wave, §4a; the convert transcript above is the
original acceptance run.)

`mino_a_statue_broke.cmsh` (589,291 B) → `mino_a_statue_broke.glb` 1,974,928 B, 3 meshes,
44,333 vertices, loads in SharpGLTF with counts matching the catalog and all 3 materials pinned
to an explicit `metallicFactor` of 0. **PASS.**

## 3 · Full batch

```bash
nohup dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models --under characters --ext cmsh --jobs 8 > out/m0-run.log 2>&1 &
#   launched 2026-10-16T17:12:31Z (pid recorded in out/m0-run.pid)
```

`out/bin/bp-convert` (the apphost) cannot launch on this machine without `DOTNET_ROOT`
(`.NET app-launch-failed`, Homebrew .NET — docs/notes/aqua-core-api.md §0); `dotnet exec
out/bin/bp-convert.dll` is the working invocation, and is what the script uses.

**In scope: 16,782 `cmsh`.** The task brief anticipated 22,012 — that is the *whole-tree* `cmsh`
count; the pinned scope `--under models --under characters` contains **16,782** `cmsh` and zero
`cmsh` under the top-level `characters/` tree (character meshes live under
`models/shadow/characters/`, 341 of them). The design gate ("catalog covers `characters/` +
`models/`") is therefore evaluated over 16,782 `cmsh` entries. Range + completion evidence:

| snapshot (UTC) | log lines | catalog entries | ok | failed | skipped | library | volume free |
|---|---:|---:|---:|---:|---:|---:|---:|
| 17:12:31 (launch) | 1 | 0 | 0 | 0 | 0 | — | 46 GB |
| 17:14:50 | 11,456 | 11,552 | 11,552 | 0 | — | 3.7 GB | 43 GB |
| 17:17:38 | 12,558 | 12,552 | 12,535 | 17 | — | 7.3 GB | 39 GB |
| 17:30:27 | 15,215 | 15,152 | 15,134 | 18 | — | 10 GB | 36 GB |
| **17:45:24 (summary written)** | **16,786** | **16,782** | **16,758** | **24** | **127** | **12 GB** | **34 GB** |

The batch's own summary line (`out/m0-run.log`):

```
in scope: 16782  skipped: 127  converted: 16631  failed: 24  elapsed: 32:57.656
```

`skipped: 127` is exactly the pre-flight (121) + largest-model (6) entries the earlier steps had
already converted into the same library — i.e. the batch picked up where the pre-flight stopped.
(The library was later reconverted in place by the post-review fix wave, §4a: every count below was
reconfirmed; only glb byte sizes grew, by `+19 B` per material of JSON.)

Fastest section by far: the rocks family (`models/objects/rocks` + `models/objects/rocks_desert`,
**12,372** in-scope `cmsh`, 11,835 of them reference-mesh stubs — see §5, class 2): 11,126 were
already recorded inside the first snapshot's 11,456 log lines (2m19s in), the remaining 1,246 by
log line 14,793. Slowest: the textured level geometry (`models/shadow/levels`, ≈7.1 s/file) and
the skinned colossus/player meshes (43.7 s for one mega-maid skirt mesh).

```bash
dotnet exec out/bin/bp-convert.dll report --out out/library --top 20
```
```
catalog: out/library/catalog.json
entries: 16782
  ok    : 16758
  failed: 24
top 1 failure reasons:
    1. 24 × OverflowException: Arithmetic operation resulted in an overflow.
       e.g. models/objects/foliage/grass/_cmn/grass_01.cmsh
```

## 4 · Convergence (resumability proof)

The acceptance criterion is that rerunning the batch converts nothing new. The script's guard does
this automatically once `out/m0-run.log` carries a completed-batch summary; the second invocation
was run that way (`./scripts/m0-acceptance.sh`, exit 0) on the completed library:

```bash
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models --under characters --ext cmsh --jobs 8      # second invocation, foreground
```
```
in scope: 16782  skipped: 16758  converted: 0  failed: 24  elapsed: 00:00.801
catalog: out/library/catalog.json
```

**Result: PASS — `converted: 0`.** 16,758 entries skipped (the 16,631 conversions + the 127
pre-flight/statue entries) and the 24 failed entries re-attempted (by design — failures are retried
on every run) and failed again with the same reason in 0.8 s. The pre-flight (121) and largest-model
(6) steps of the script also converged at `converted: 0` on this re-run.

Third line of evidence: loading every produced glb in the library with SharpGLTF (the checker
without `--prefix`, 21 groups — output kept in `out/m0-glbcheck-all-fixed.log`; re-measured in the
post-review fix wave, §4a):

```
summary files=16758 loaded=16758 failed=0 mismatches=0 materials=7147 badMaterials=0 bytes=3140427688 meshes=23583 vertices=64085954 groups=21 failures=0
```

— i.e. **all 16,758 produced glbs load, every one's mesh/vertex counts match the catalog**
(0 mismatches) **and every one of the 7,147 materials carries an explicit `metallicFactor` of 0**.
13,381 of them are empty stub containers (§5, class 2); the other 3,377 carry 23,583 meshes /
64.1 M vertices / 3.14 GB of glb data (plus 23,761 PNGs on the 1,499 entries that reference
decodable textures; library total 12 GB).

## 4a · Post-review fix wave (dielectric materials, 2026-10-16)

A whole-branch review found that materials were written without `metallicFactor` — glTF's default
is **1.0**, so every output rendered fully metallic in conformant loaders. The writer now emits
`"metallicFactor": 0` (dielectric; `roughnessFactor` stays at its 1.0 default as the accepted
approximation — §7). The same wave (`600f36b`) fixed `report`'s I/O error handling, pinned
`<IsPackable>false</IsPackable>` on the core project, corrected the ImageSharp license/dependency
notes, and taught the acceptance checker to assert the explicit factor. The 12 GB library was then
reconverted in place so the artifacts match the fixed code:

```bash
dotnet publish src/BpConvert.Cli -c Release -o out/bin
nohup dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models --under characters --ext cmsh --jobs 8 --force > out/m0-force-run.log 2>&1 &
#   launched 2026-10-16T18:24:25Z (pid recorded in out/m0-force-run.pid)
#   in scope: 16782  skipped: 0  converted: 16758  failed: 24  elapsed: 33:06.194
#   (summary written ≈18:57:31Z; the same 24 grass-family OverflowException failures as §5,
#    logged as `fail  <source>: OverflowException …` lines)

dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models --under characters --ext cmsh --jobs 8        # second invocation, no --force
#   in scope: 16782  skipped: 16758  converted: 0  failed: 24  elapsed: 00:00.936
#   (log: out/m0-force-convergence.log)
```

**Reconfirmed: 16,758 `ok` / 24 failed, and `converted: 0` on the second invocation.** The
acceptance script was re-run end to end (`./scripts/m0-acceptance.sh`, exit 0; log
`out/m0-acceptance-run3.log`) — its pre-flight (121 files, `materials=389 badMaterials=0`) and
largest-model checks (6 + 1 files, `materials=6` / `materials=3`, `badMaterials=0`) now assert the
material field, and its convergence branch re-proved `converted: 0`. Whole-library re-check
(checker without `--prefix`, 16 s; `out/m0-glbcheck-all-fixed.log`, quoted in full in §4 above —
`materials=7147 badMaterials=0`, 0 mismatches). Artifact-level proof of the fix:

```bash
$ grep -o -a '"metallicFactor":0' out/library/models/shadow/characters/wander/_cmn/wander.glb | wc -l
12        # = the file's material count (12): every one carries the explicit dielectric factor
```

**What actually changed in the results sheet**: the glb byte figures in §§1–2 and §4 grew by
`+19 B` per material of JSON (`wander` 19,439,104 → 19,440,248; pre-flight total 205,840,260 →
205,847,616; whole library 3,140,291,956 → 3,140,427,688). Everything else is unchanged: 16,782
entries, 16,758 `ok` / 24 failed (0.143 %), the same 24-overflow failure class, 23,583 meshes,
64,085,954 vertices, 23,761 PNGs. §3's snapshot table and both historical command transcripts above
are the original run-of-record; their counts remain valid.

## 5 · Failure analysis

Final `report` output (verbatim):

```
catalog: out/library/catalog.json
entries: 16782
  ok    : 16758
  failed: 24
top 1 failure reasons:
    1. 24 × OverflowException: Arithmetic operation resulted in an overflow.
       e.g. models/objects/foliage/grass/_cmn/grass_01.cmsh
```

**Failure rate: 24 / 16,782 = 0.143 %** — the design gate (**≤ 5 %**) passes with a wide margin.
The gate is evaluated over the `cmsh` entries the batch was pinned to (`--ext cmsh`; the T7
amendment excludes `.cmdl`-as-source — see §7).

The 24 failures are one class, spread over four directories:

- `models/objects/foliage/grass/_cmn/` — 15 (`grass_01`, `grass_02_lod1`, `obj_grass_13a`–`13f`,
  `obj_grass_14a`–`14e`, `obj_grass_beach_02/03`)
- `models/test/mthrush/_cmn/` — 6 (`obj_clover_clump_green`, `obj_grass_17`, `obj_grass_a14_02`,
  `obj_grass_green`, `obj_grass_green_short`, `obj_grass_highlands_green_01_short`)
- `models/objects/foliage/plants/_cmn/` — 2 (`obj_perennial_05`, `plant_06`)
- `models/objects/foliage/trees/_cmn/` — 1 (`tree_27_collision`)

Observed classes:

1. **Vendored-parser crash on a grass-family vertex format** — `OverflowException: Arithmetic
   operation resulted in an overflow.` Reproduced outside the batch with a throwaway harness
   (`new CMSH(bytes)` from `AquaModelLibrary.Data`, no BpConvert code in the path):

   ```
   System.OverflowException: Arithmetic operation resulted in an overflow.
      at Reloaded.Memory.Streams.BufferedStreamReader`1.ReFillBuffer()
      at AquaModelLibrary.Data.BluePoint.CMSH.CMSHVertexData.ReadVertDefData(BufferedStreamReaderBE`1 sr, Int32 vertCount, VertexMagic dataMagic, Int64 dataSize, UInt16 dataFormat) …/CMSHVertexData.cs:line 147
      at AquaModelLibrary.Data.BluePoint.CMSH.CMSHVertexData..ctor(...) … CMSHVertexData.cs:line 105
      at AquaModelLibrary.Data.BluePoint.CMSH.CMSH.Read(...) … CMSH.cs:line 131
   ```

   Line 147 is the `COL0/COL1/COL2` + `dataFormat == 4` case:
   `colors.Add(sr.ReadBytes(sr.Position, 4));` — the stream *position* is passed as the read
   *count*, so the buffered reader is asked for an enormous span and overflows inside
   Reloaded.Memory. It is data-dependent within one file family: `…/foliage/grass/_cmn/grass_09.cmsh`
   parses fine (1 mesh, 384 vertices) while `grass_01.cmsh` (same `8C 0A` magic) throws. The
   submodule is read-only for this project ⇒ **not fixed here**; M1 candidate: handle this vertex
   case in `AquaModelAdapter`/upstream the one-line fix.

2. **Geometry-free reference-mesh `cmsh` are not failures.** Most in-scope `cmsh` are
   hash-suffixed *reference-mesh variants* (e.g.
   `models/objects/rocks/_cmn/obj_rocks_kit01_ledge_02_lod1_82e62d2a.cmsh`): the library's `CMSH`
   constructor returns early for them by design, so they convert `ok` to a 72-byte glb (header +
   empty JSON chunk) with `meshes=0` and no PNGs. The name pattern is exact — in
   `models/objects/rocks/_cmn/` (11,264 `cmsh`, 67 % of the whole in-scope set) 10,784 files carry
   the `_<8 hex>` suffix and are stubs; the remaining 480 plain-named files carry the geometry
   (629 meshes in that directory). `bp-convert inspect` confirms the stub class: `parse: meshes=0 vertices=0 — reference-mesh variant skipped by the library (no
   geometry)`. They are `ok` by definition (the container is recognized; M1's cmdl-driven assembly
   is what would resolve them). Final split over the whole batch: **13,381 stub entries
   (`meshes=0`, every one a 72-byte glb, no PNGs) vs 3,377 entries with geometry** (23,583 meshes,
   64,085,954 vertices). `models/objects/rocks/_cmn/` alone is 11,264 of the 16,782 in-scope `cmsh`
   (67 %), of which 10,784 are hash-suffixed stubs.

3. **Deferred/expected classes** (see §7): `.cmdl`-as-source is out of scope by the T7 amendment;
   HDR/BC6H textures are refused **by name** as a *skip*, not a failure, so they do not appear in
   the failure counts at all.

## 6 · Provenance: which path produced what

- Steps 1–3 were executed **manually, in the pinned order**, and their exact commands/outputs are
  quoted in §§1–3 (the batch was launched with `nohup` at 17:12:31Z and ran 32:57.656 — the
  acceptance run was underway before `scripts/m0-acceptance.sh` existed).
- The script was then run end-to-end (`./scripts/m0-acceptance.sh`, exit 0, 17:48:04–17:48:07Z,
  output kept in `out/m0-acceptance-run.log`), exercising **publish → checker build → pre-flight
  (121 skipped + 121 glb loads) → largest model (6 + 1 glb loads) → convergence re-run
  (`converted: 0`) → report** against the completed library. Its convergence branch is therefore
  live-verified; its *launch* branch was not (there was nothing left to launch) — the launch
  command it encodes is byte-identical to the one that produced the batch above.
- The `out/m0-run.pid` guard was verified live while the batch was running: the script refused with
  "a batch is already running (pid 65954); refusing to run" (exit 1) and left the catalog untouched.
- **Post-review fix wave** (2026-10-16, commit `600f36b`): CLI republished; full `--force` re-run
  (launched 18:24:25Z, `out/m0-force-run.log`), second invocation (`out/m0-force-convergence.log`),
  acceptance script re-run (exit 0, `out/m0-acceptance-run3.log`), whole-library check
  (`out/m0-glbcheck-all-fixed.log`) — details and superseded byte figures in §4a.
- The checker embedded in the script is byte-identical (`diff` clean) to the copy used for the
  manual checks and to `scratch/glbcheck/`.

## 7 · Known limitations (deliberate, carried to M1)

- **`.cmdl` as a source is not implemented** (T7 amendment, 2026-10-16): measured 12/12
  `NotSupportedException` on the pinned samples; a sibling `.cmdl` is consumed as the material map
  of its `.cmsh`. cmdl-driven assembly (`GetCMeshReferences`, composing a character from parts) is
  an M1 item. The batch therefore runs `--ext cmsh` only; the ~19.3k `cmdl` under `models/` +
  `characters/` are untouched. (Double-checked: the whole tree has 24,540 `cmdl`, of which 19,306
  are inside this scope.)
- **Texture refusals are skips by design** (`ModelConverter.cs:69–80`): a `.ctxr` whose declared
  format we do not decode is skipped, the model still converts, and the refused texture is absent
  from the entry's `pngs`. Pinned classes: **BC6H/HDR refused by name** (test:
  `CtxrConverterTests` asserts `BC6H` in the message; e.g. `CC 0A` = 325 tree-wide, all BC6H_UF16)
  and one container Task 3 permanently refuses (`hdr_calibration_image.ctxr`, negative
  dimensions). In *batch* mode nothing surfaces these skips (no note sink), so this run does not
  count them — an observability gap worth a one-line M1 fix (record skipped textures in the
  catalog/entry).
- **Geometry-free `cmsh` stubs** (§5, class 2) are `ok` by definition here; if a later milestone
  wants "every ok entry has geometry", that is a policy change, not a bug.
- **Materials carry no `alphaMode`/`alphaCutoff`/`doubleSided`** (M1 material item): the library
  does not parse the CMAT blend state, so the writer emits only `baseColorFactor`, its texture,
  `emissiveFactor`, and the normal/specular texture slots. Alpha-tested foliage/cloth will render
  opaque and single-sided until that item lands. The same M1 item revisits `metallicFactor` —
  currently pinned to a dielectric `0` (the post-review fix wave; outputs before it omitted the
  factor, which conformant loaders default to **1.0 = fully metallic**) — and `roughnessFactor`,
  left at its default 1.0 as the accepted approximation.

## 8 · Reproduce

```bash
export SOTC_GAME_ROOT=/path/to/extracted/game
./scripts/m0-acceptance.sh                       # steps 1–3, then the convergence re-run once finished
dotnet exec out/bin/bp-convert.dll report --out out/library --top 20   # counts + top failure reasons
# whole-library glb assertion (every produced glb loads via SharpGLTF; 15 s):
dotnet exec out/glbcheck/bin/Release/net10.0/glbcheck.dll --lib out/library --depth 3
dotnet test tests/BpConvert.Tests -v q           # suite: 57/57 green, 53 s
```

`out/` and `scratch/` are gitignored: the library, the batch log and the generated checker are
local artifacts, not committed. Everything committed for this task is
`scripts/m0-acceptance.sh`, this document, and the README M0 section.
