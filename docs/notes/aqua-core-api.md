# AquaModelLibrary — API notes for BpConvert

Submodule: `third_party/PSO2-Aqua-Library` @ **`5dcf37a815bf683852bbfd283834c1e8d23f38ad`**
(`git -C third_party/PSO2-Aqua-Library rev-parse HEAD`; branch HEAD at clone time, 2026-10-15)
Machine: macOS arm64, .NET SDK 10.0.401. All line numbers refer to that commit.

> **This file is the specification for Task 4's adapter.** Everything the adapter needs to
> load `.cmsh`/`.cmdl` files and produce meshes, vertices, materials, texture references and
> skeleton data is documented here, from the library sources only. Line numbers pin the
> exact members so later tasks do not need to re-read the library.
>
> **Gate status: resolved 2026-10-15** — the parse path runs on this machine, including
> Oodle-compressed files; see §0 for the decisions (link target, vendored decoder, semantics).

---

## 0. GATE RESOLVED (2026-10-15) — decision record; original failure history at the end of this section

Task 0's spike now passes end-to-end on this machine (macOS arm64, .NET SDK 10.0.401). The three
rulings that resolved the original gate, and what they mean for later tasks:

### R-A — Link target is `AquaModelLibrary.Data`, not `Core`

`AquaModelLibrary.Core` cannot build on macOS (Windows-only C++/CLI FBX project, MSB4278). All of the
BluePoint *parse* layer lives in `AquaModelLibrary.Data` (`net9.0`, consumable from our `net10.0`
projects) and builds clean on arm64 via `third_party/Directory.Build.props` (the `NvTriStripDotNet`
net48 shim). **Every later task links `Data`.** `Core` remains useful only as a *reference
implementation* to read (`Core/BluePoint/BluePointConvert.cs`).

### R-B — Oodle decoder: vendored official RAD Oodle 2.9.16 macOS dylib

- File: `tools/native/liboo2core_8_win64_.dylib` — macOS universal binary (x86_64 + arm64), 1,206,880 bytes.
- sha256: `47269bc3c9f7ef7948775be2c018ce674d1ab8a8526b6935e9d1033b0b47e8b7`.
- Provenance: `WorkingRobot/OodleUE` mirror of the Oodle SDK shipped in Epic's Unreal Engine.
  **Oodle is proprietary (RAD Game Tools / Epic Games), UE EULA, personal use only — never
  redistribute.** See `tools/native/NOTICE.md`.
- Resolution mechanism: `src/BpConvert.Core/BpConvert.Core.csproj` carries the file as a `Content`
  item (`Link="liboo2core_8_win64_.dylib"`, `CopyToOutputDirectory="PreserveNewest"`), so it is
  copied beside every consumer's binaries — CLI (`dotnet run`), test host, and `dotnet publish`
  output (verified in a scratch publish). The library's
  `[DllImport("oo2core_8_win64_")]` then resolves through the default .NET probing rules
  (`<appdir>/liboo2core_8_win64_.dylib`). Submodule and P/Invoke string untouched; no
  `SetDllImportResolver` hook needed.
- Proof the decoder is actually exercised: with the dylib removed from the test output dir,
  `dotnet test --no-build --filter Inspect_RunsOnWanderCmsh` fails with
  `DllNotFoundException: Unable to load shared library 'oo2core_8_win64_'` (exit 1); restored, green.

### R-C — Decode semantics: `OodleLZ_Decompress` needs the exact `rawLen`

A larger output buffer than the true decompressed size **fails**. `CompressionHandler.CheckCompression`
already passes exactly the size read from the container's 12-byte footer trailer
(`last4Bytes - 0x60000000` / `0x80000000`), so the library is compatible as-is — do not "fix" it by
padding buffers.

### Verified sample results (env-gated integration tests, run in-process by `dotnet test`)

| sample (relative to `$SOTC_GAME_ROOT`) | magic | result |
|---|---|---|
| `models/shadow/characters/wander/_cmn/wander.cmsh` | `8c0a` | `Parsed`, **meshes=12**, vertices=62,466 |
| `models/objects/props/_cmn/obj_plank_01.cmsh` | `8c0a` | `Parsed`, **meshes=1**, vertices=980 |
| `models/shadow/objects/actors/statues/colossus_01_minotaur_a/_cmn/mino_a_statue_broke.cmsh` | `8c0a` | `Parsed`, **meshes=3**, vertices=44,333 |
| `models/shadow/objects/home/entrance_pillars_01/_cmn/entrance_pillar_01_b_7e761f48.cmdl` | `8c0a` | `Parsed` (3 cmsh references) |
| `models/test/mak/_cmn/rock_slide_01.cmsh` | `8c06` | `Parsed`, meshes=1, vertices=389 |
| `models/test/mthrush/_cmn/unit_uv_quad.cmsh` | `0002` | `Parsed`, meshes=1, vertices=4 |
| `models/test/mthrush/_cmn/unit_uv_quad_400fbd6c.cmsh` | `0005` | reference-mesh stub: `Parsed`, 0 meshes, note |

*Colossus filename note:* `colossus_01_minotaur_a.cmsh` does not exist in the game tree; per the
Task 0 brief the **largest `*.cmsh` in that directory** is used and pinned:
`mino_a_statue_broke.cmsh` (589,291 bytes, next largest 160,646).

### Environment quirk (Homebrew .NET)

Bare apphost execution (`./bin/.../bp-convert`, `./out/bin/bp-convert`) fails on this machine with
`.NET app-launch-failed` (exit 131) unless `DOTNET_ROOT=/opt/homebrew/Cellar/dotnet/10.0.401_1/libexec`
is exported. `dotnet run`, `dotnet exec <dll>` and `dotnet test` are unaffected. Worth remembering
for Task 7's published `out/bin/bp-convert`.

### History — original gate record (superseded; kept for the trail)

#### H1. `AquaModelLibrary.Core` does not build on macOS (any CPU)

```
$ dotnet build third_party/PSO2-Aqua-Library/AquaModelLibrary.Core/AquaModelLibrary.Core.csproj
.../AquaModelLibrary.Native/AquaModelLibrary.Native.vcxproj(57,3): error MSB4278: the imported file
"$(VCTargetsPath)/Microsoft.Cpp.Default.props" does not exist and appears to be part of a Visual Studio
component. It may require MSBuild.exe to be imported successfully and therefore may not be able to be
built on the dotnet CLI.
```

`AquaModelLibrary.Core.csproj` (`net9.0`) has a hard `ProjectReference` to
`AquaModelLibrary.Native.vcxproj` — a **C++/CLI / MSVC** project (`PlatformToolset v145`,
`CLRSupport NetCore`) that wraps RAD's FBX SDK. It cannot build without Windows + Visual Studio
(FBX SDK headers must also be installed). The managed code that uses it
(`AquaModelLibrary.Core/General/FbxExporter.cs`'s `FbxExporterNative` → `AquaModelLibrary.Objects.Processing.Fbx.FbxExporterCore`,
used by `BluePointConvert.cs:178`, `SoulsConvert.cs`, `MSBModelExtractor.cs`, …) is **FBX export only**.

A second error appears on a clean clone —
`NvTriStrip.Net/NvTriStripDotNet/NvTriStripDotNet.csproj` (old-style csproj, `TargetFrameworkVersion v4.8`)
→ `error MSB3644: reference assemblies for .NETFramework,Version=v4.8 were not found` — this one is
**fixed in-repo** by `third_party/Directory.Build.props` (points MSBuild at the net48 reference
assemblies NuGet already vendors in `~/.nuget/packages/microsoft.netframework.referenceassemblies.net48/1.0.3/build`).
The shim fires only for that one project (`Condition="'$(MSBuildProjectName)' == 'NvTriStripDotNet'"`),
so nothing else in the graph changes.

#### H2. The BluePoint parse layer lives in `AquaModelLibrary.Data` — and it builds clean

```
$ dotnet build third_party/PSO2-Aqua-Library/AquaModelLibrary.Data/AquaModelLibrary.Data.csproj
    … 0 Error(s)          # from clean bin/obj; warnings only, count varies by incremental state
```

**Link target for all later tasks must be `AquaModelLibrary.Data`, not `AquaModelLibrary.Core`.**
`Data` (`net9.0`, consumable from our `net10.0` projects) contains the complete
`AquaModelLibrary.Data.BluePoint.*` parse layer (CMSH/CMDL/CMAT/CTXR/CSKL/CANI/CGPR/…).
`Core` only adds converters-to-other-formats (`BluePointConvert`, FBX export) and is unavailable
on macOS; it remains useful as a *reference implementation* to read (file paths and line numbers
cited below are from `Core/BluePoint/BluePointConvert.cs`).

#### H3. Parsing the pinned samples was blocked by a Windows-only Oodle decoder

All three pinned `.cmsh` files start with the Oodle-compressed container marker `8C 0A`:

| sample (relative to `$SOTC_GAME_ROOT`) | size | first 32 bytes | decompressed size (from trailer) | parse result |
|---|---|---|---|---|
| `models/shadow/characters/wander/_cmn/wander.cmsh` | 1,588,667 | `8c0a 0236 3a89 2e6a 010a 0000 000c 0000 00e5 3000 4e32 2abd baec c83e 9012 453d` | 3,569,857 | `DllNotFoundException: oo2core_8_win64_` |
| `models/objects/props/_cmn/obj_plank_01.cmsh` | 18,747 | `8c0a 0049 2988 4927 0002 0000 0001 0000 003b 9000 694f 17c0 53f6 1fbe be16 6a3e` | 28,248 | `DllNotFoundException: oo2core_8_win64_` |
| `models/shadow/objects/actors/statues/colossus_01_minotaur_a/_cmn/mino_a_statue_broke.cmsh` *(see note)* | 589,291 | `8c0a 012a 5b88 91d5 0002 0000 0003 0000 005e 46da b532 c0a9 1347 bf2b ed18 78f2` | 1,340,403 | `DllNotFoundException: oo2core_8_win64_` |

*Colossus filename note:* `colossus_01_minotaur_a.cmsh` does not exist in the game tree; per the
Task 0 brief the **largest `*.cmsh` in that directory** is used and pinned:
`mino_a_statue_broke.cmsh` (589,291 bytes, next largest 160,646).

Exactly:

```
ERR models/shadow/characters/wander/_cmn/wander.cmsh
    System.DllNotFoundException: Unable to load shared library 'oo2core_8_win64_' or one of its dependencies.
ERR models/objects/props/_cmn/obj_plank_01.cmsh  (same)
ERR models/shadow/objects/actors/statues/colossus_01_minotaur_a/_cmn/mino_a_statue_broke.cmsh  (same)
```

Cause chain: every BluePoint container constructor calls
`CompressionHandler.CheckCompression(file, out wasCompressed)` (`Data/BluePoint/CompressionHandler.cs:9`);
for `8C 0A` / `8C 06` headers it calls `Zamboni.Oodle.OodleDecompress(file, decompLength)`
(`CompressionHandler.cs:31`), which P/Invokes **`oo2core_8_win64_`** —
`ZamboniLib/ZamboniLib/Oodle.cs:215+` (`[DllImport("oo2core_8_win64_", EntryPoint = "OodleLZ_Decompress")]`) —
RAD's proprietary, Windows-only Oodle runtime. The copies shipped in the repo
(`Data/Libraries/oo2core_6_win64.dll` and `ooz.x64.dll`) are Windows x64 PE files.

Compression is **the norm, not the exception**: over the first 300 `*.cmsh` files under
`models/`, first-two-byte magic counts were `8c0a` ×235, `0002` ×52, `8c06` ×8, `0005` ×5 —
i.e. ~81% carry an Oodle marker.

**The managed parse path itself is arm64-clean.** Images with `0002`/`0005` magic (no Oodle call)
parse correctly on this machine:

```
OK  models/test/mthrush/_cmn/obj_annual_01.cmsh                        meshes=1 verts=41 faces=25 bones=
OK  models/test/mthrush/_cmn/obj_grass_clump_prairiegreen_short.cmsh   meshes=1 verts=18 faces=6  bones=
OK  models/test/mthrush/_cmn/unit_uv_quad.cmsh                         meshes=1 verts=4  faces=2  bones=
OK  models/objects/props/_cmn/obj_plank_01_52f8cc91.cmdl               meshRefs=1 mats=1 cpid=
```

#### H4. Original consequences list (all items now resolved)

1. A **portable Oodle decompressor** is now on the critical path for *models* as well as textures
   (the M0 plan anticipated this only inside Task 3's "Oodle decision point").
   → **Resolved by R-B** (vendored RAD 2.9.16 dylib); note it also covers Task 3's texture path.
2. The plan's candidate `powzix/ooz` (C++; Kraken/Mermaid/Selkie/Leviathan/LZNA/Bitknit) has
   **no LICENSE file and no license statement in its README** (checked 2026-10-15) → it does not
   meet the plan's "permissive license" bar without the author's permission. Per the plan's T3
   rule ("if no compliant decoder is found, stop and escalate"), this needs an explicit decision:
   e.g. (a) obtain written permission / find a licensed fork of ooz; (b) RAD Oodle SDK (proprietary,
   local-use only); (c) a managed C# decoder written/adapted for this project.
   → **Moot**: the partner approved option (b)-equivalent — the official RAD 2.9.16 macOS binary,
   personal use only (R-B).
3. **Integration seam (cheap option):** the existing P/Invoke name is the *string* `"oo2core_8_win64_"`.
   A permissively-licensed decoder built as `liboo2core_8_win64_.dylib` exporting `OodleLZ_Decompress`
   and placed next to the app (or in `runtimes/osx-arm64/native/`) would satisfy the library's
   P/Invoke unchanged — no fork or patch of the submodule needed.
   → **Adopted**: side-by-side copy via the `Content` item (R-B); no resolver hook required.
4. `AquaModelLibrary.Data` is otherwise fully usable: build with `third_party/Directory.Build.props`
   in place. Manual override if the NuGet cache ever moves:
   `dotnet build … -p:TargetFrameworkRootPath=~/.nuget/packages/microsoft.netframework.referenceassemblies.net48/1.0.3/build`.
   → Still accurate; the shim is committed.
5. Task 0 Steps 3–6 (the pinned `Inspect` API, `inspect` verb, integration test) were **not
   implemented** — the brief's Step 2 stop condition is met. The pinned API design remains valid;
   it will report `Parsed=false` for compressed inputs until (3) above is in place.
   → **Superseded**: all implemented during the resumed fix round (see §2.1).

---

## 1. Format/era cheat sheet (SOTC vs Demon's Souls)

`CFooter` = last 12 bytes of every BluePoint container: `int magic; byte version; byte×3 unk; int uncompressedSize`
(`Data/BluePoint/CFooter.cs`). `version` selects the era-specific layout:

| container | SOTC (Shadow of the Colossus 2018) | Demon's Souls | notes |
|---|---|---|---|
| CMSH | footer `version != 0x5B` | `0x5B` | `CMSHHeader.isDeSR = version == 0x5B` (`CMSHHeader.cs:51`); `header.era` → `BPEra.SOTC` |
| CMAT | `0x39` | `0x63` | `CMAT.cs:60/81` |
| CTXR | `0x25` | `0x6E` (also PS3 variant `0x01010002`/`0xFF000002`) | `CTXR.cs:417/480/388` |
| CSKL | `0x9` | `0x19` | `BluePointConvert.cs:832/856` |
| CMDL | `CMDLMagic.SOTC` / `CMDLMagic.DeSR` | | `CMDL.cs:41` |

Container compression markers: first 2 bytes LE `0x0A8C`/`0x0ACC` → decompressed length =
`last4Bytes - 0x60000000`; `0x068C`/`0x06CC` → `last4Bytes - 0x80000000`
(`CompressionHandler.cs:9-26`). `$`-rooted references use `****` as the platform wildcard;
local paths substitute `_cmn` / `_ps5` / `_ps4` (the game root anchor is "the directory containing
`eboot.bin`", found by `PSUtility.GetPSRootPath(path)` — `Data/Utility/PSUtility.cs:5`).

## 2. Loading files (entry points)

All constructors take the **whole file as `byte[]`**, run `CheckCompression` themselves, and read
footer-first. All types are in `AquaModelLibrary.Data`:

```csharp
// Data/BluePoint/CMSH/CMSH.cs:25 — header, vertData, faceData, boneData, morph*, footerData
public CMSH(byte[] file)
// Early-return leaves everything null for LOD/reference-mesh variants:
//   ushort magic ∈ {0x1500, 0x500, 0xD01, 0x4100, 0x1100, 0x4901, 0x5100} (CMSH.cs:28-37)
//   → ALWAYS null-check `cmsh.header` before use.
// `wasCompressed` tells whether the Oodle path ran.
```

```csharp
// Data/BluePoint/CMDL/CMDL.cs:20 — CMDLMagic magic; CGPRObject mainObject, secondObject; CFooter footer
public CMDL(byte[] file)
// Data/BluePoint/CMAT/CMAT.cs:32 — int hash; List<string> shaderNames, texNames; CFooter footerData
public CMAT(byte[] file)
// Data/BluePoint/CSKL/CSKL.cs:29 — CSKLHeader header; List<CSKLTransform> transforms;
//   List<Matrix4x4> invWorldTransforms; CSKLMetaData metadata; CSKLNames names; partitions; CFooter footerData
public CSKL(byte[] file)
// Data/BluePoint/CTXR/CTXR.cs:45 / :63 — second overload skips texture-buffer reads (header-only, fast)
public CTXR(byte[] file)                 //  public CTXR(byte[] file, bool readTexBuffers)
```

The CMSH body is conditional (`CMSH.cs:125-149`): `footerData` → `new CMSHHeader(sr, footerData.version)`,
then `if (header.variantFlag2 != 0x41)` reads `vertData` + `faceData` (+ morph data if
`variantFlag2 & 0x20`, + `boneData` if `variantFlag & 0x1`).

### 2.1 `Inspect` wrapper + `inspect` verb (Task 0 Steps 3–5) — implemented

`src/BpConvert.Core/Inspect.cs` is the thin wrapper the integration tests and CLI use. It is the
only file outside `Aqua/` that touches library types (Task 4 adds the adapter itself):

```csharp
var bytes = File.ReadAllBytes(absolutePath);        // Inspect.Describe
var cmsh  = new CMSH(bytes);                        // decompresses 8C0A/8C06 via the vendored dylib (R-B)
cmsh.header.meshCount                               // → InspectSummary.MeshCount (0 for skipped variants)
cmsh.vertData?.positionList.Count                   // → InspectDetail.VertexCount
var cmdl  = new CMDL(bytes);                        // .cmdl: MeshCount = cmdl.GetCMeshReferences().Count
```

Exact surface (the first two lines are the Task 0 pin; `Describe` is additive for the CLI):

```csharp
public static InspectSummary Summarize(string absolutePath);
public static InspectDetail  Describe (string absolutePath);
public sealed record InspectSummary(bool Parsed, int MeshCount, string? Error);        // pinned
public sealed record InspectDetail (bool Parsed, int MeshCount, int VertexCount, string? Error);
```

Semantics: `Parsed=false` only when reading/parsing threw, or the extension is unsupported; `Error`
then holds `"<ExceptionType>: <first line of message>"`. Reference-mesh/LOD variants (the
`CMSH.cs:28-37` early return, e.g. `00 05` stubs) report `Parsed=true, MeshCount=0` with an
explanatory `Error` note — the brief's "0 meshes with a recorded reason" shape.

CLI (`src/BpConvert.Cli/Program.cs`, assembly `bp-convert`): `bp-convert inspect <file>` prints the
absolute path, size, the first 32 bytes hex, and the parse summary; exit `0` = parsed (notes still
printed), `1` = parse failure, `2` = usage error / file not found.



Per-file mesh list: `cmsh.header.meshCount` (`CMSHHeader.cs:29`) and
`cmsh.header.meshList : List<CMSHMeshReference>` (`CMSHMeshReference.cs`):

```csharp
public string matName;           // → material (see §4)
public int startingFaceIndex;    // DeSR: face range into faceData.faceList (×3)
public int faceIndexCount;       // DeSR
public int startingVertexIndex;  // SOTC: vertex range
public int vertexIndicesUsed;    // SOTC
public int startingFaceVertIndex;  // SOTC: face range actually used
public int faceVertIndicesUsed;    // SOTC
public Vector3 minBounding, maxBounding;
```

SOTC sub-mesh face ranges — the reference implementation (`BluePointConvert.cs:660-690`) does:

```csharp
startFace  = mesh.header.meshList[m].startingFaceVertIndex / 3;
faceCount  = mesh.header.meshList[m].faceVertIndicesUsed / 3;
// failsafe: if the range is empty, take faceList.Count - currentFace (rest of the buffer)
```

Vertex data (`cmsh.vertData`, `Data/BluePoint/CMSH/CMSHVertexData.cs`), all streams indexed by the
same vertex id:

```csharp
public List<Vector3> positionList;                       // POS0
public Dictionary<VertexMagic, List<Vector2>> uvDict;    // TEX0..TEX3, half-float pairs (line 155-165)
public List<uint> qut0List;                              // QUT0 packed TBN quaternion (line 128)
public List<byte[]> normalTemp;                          // NRM0/TAN0 raw 4-byte entries — NOT decoded by the library
public Dictionary<VertexMagic, List<byte[]>> colorDict;  // COL0..COL2 (line 134-154)
public List<int[]> vertWeightIndices;                    // BONI (line 166)
public List<Vector4> vertWeights;                        // BONW, /255 (line 185)
```

* Vertex count = `vertDefs[0].dataSize / 0xC` — POS0 is always first (`CMSHVertexData.cs:101`).
* Stream layout: `vertDefs : List<CMSHVertexDataDefinition>` (`dataMagic`, `dataFormat`, `dataStart`,
  `dataSize`; `CMSHVertexDataDefinition.cs`) — each stream is seeked to `vertexDataStart + dataStart`
  (`CMSHVertexData.cs:104-105`).
* Normals: unpack `qut0List` — `UnpackQUT0(uint, out normal, out tangent, out bitangent)` or the batch
  `GetQut0Data(out List<Vector3> normals, out tangents, out bitangents)` (`CMSHVertexData.cs:386/414`);
  QUT0 is a 10/10/10/2 packed TBN quaternion. Raw NRM0 (`normalTemp`) has no library decoder.
* Faces: `cmsh.faceData.faceList : List<Vector3Int.Vec3Int>` — 3 indices per face; stored as
  `ushort` unless `positionList.Count > 0xFFFF` (`CMSHFaceData.cs:17-35`).
* SOTC-only extras: `sotcUnk0List`/`sotcUnk1List` (per-region bounds/index ranges;
  `CMSHSOTCUnkData0.minBounding/maxBounding`, `CMSHSOTCUnkData1.startIndex/length`),
  `satValues` (per-face surface-area table). `header.sizeFloat` = total surface area.

## 4. Materials and texture references

The chain is **CMDL → CMAT → CTXR**, plus per-mesh `matName` inside CMSH:

```csharp
// Data/BluePoint/CMDL/CMDL.cs:71
public Dictionary<string, string> GetCMATMaterialMap()   // material name → "$/…/x.cmat"
// Data/BluePoint/CMAT/CMAT.cs:28-30 (SOTC, footer version 0x39)
public List<string> shaderNames;   // e.g. "shadow_map"
public List<string> texNames;      // "$/…/x.ctxr" references, one per texture slot
```

* `cmsh.header.meshList[i].matName` is the lookup key into `GetCMATMaterialMap()`
  (reference impl: `BluePointConvert.cs:692-730`).
* Resolve `$`-refs by stripping the leading `"$/"`, replacing `****` with `_cmn`, `_ps5`, `_ps4`
  (try in that order — `BluePointConvert.cs:79-96` for cmat, `:137-161` for ctxr), against the
  game root. Prefer `_cmn` first.
* Mesh-level geometry comes from `cmdl.GetCMeshReferences()` (`CMDL.cs:38`) — `"$/models/…/x.cmsh"`
  strings (`BluePointConvert.cs:116-120` substitutes `****`→`_cmn`).
* Optional CMDL extras: `GetCPIDPath()` (`CMDL.cs:121`, physics collision file — many cmdls lack it),
  `GetCCLMs(out string cclmPath, out string hqCclmPath)` (`CMDL.cs:138`).
* **Note for Task 4:** CMAT color metadata is *not parsed by the library* ("TODO - Material color
  metadata", `CMAT.cs:79/103`) — `MaterialData.BaseColorFactor`/`EmissiveFactor` must default
  (1,1,1,1 / 0,0,0) unless we decode it ourselves later.

## 5. Skeleton and skinning

CMSH-side (`cmsh.boneData`, only present when `header.variantFlag & 0x1`) —
`Data/BluePoint/CMSH/CMSHBoneData.cs`:

```csharp
public string skeletonPath;                    // "$/…/x.cskl" or a filename
public int skeletonHash;
public List<string> boneNames;                 // local to this mesh
public List<Vector4> boneBoundingSpheres;
```

Weights: `vertData.vertWeightIndices` (4 bone indices per vertex; read as `ushort` when
`dataSize/4 != vertCount`, else as `byte` — `CMSHVertexData.cs:166-183`) and
`vertData.vertWeights` (4 weights/255 → [0,1]). Bone indices index the CSKL bone list.

CSKL (`Data/BluePoint/CSKL/CSKL.cs`, `CSKLHeader.cs`, `CSKLTransform.cs`) —
the reference node build is `BluePointConvert.CSKLToAquaNode` (`Core/BluePoint/BluePointConvert.cs:807+`):

```csharp
public CSKLHeader header;                          // boneCount (CSKLHeader.cs:18)
public List<CSKLTransform> transforms;             // scale, rotation, position; ComputeLocalTransform() (:64)
public List<Matrix4x4> invWorldTransforms;         // DeSR path: IBM directly (footer v0x19)
public CSKLMetaData metadata;                      // familyIds[i].parentId
public CSKLNames names;                            // names.skeletonNames.names[i], pipe-separated ("root|…"); take Split('|').Last()
```

Inverse-bind recipe for **SOTC** (`footerData.version == 0x9`, `BluePointConvert.cs:832-854`):
walk bones in order; `mat = local_i`; if `parentId != -1` then `mat = local_i × inverse(IBM_parent)` (the
IBM already computed for the parent); `IBM_i = inverse(mat)`. DeSR (0x19) uses
`invWorldTransforms[i]` directly.

CSKL resolution: `Path.GetFileName(boneData.skeletonPath)` beside the model first, else the `$`-ref
route with `****`→`_cmn` against the game root (`BluePointConvert.cs:499-515`).

## 6. Texture decode (CTXR) entry point

```csharp
// Data/BluePoint/CTXR/CTXR.cs:45 — parse; isPng ⇒ raw PNG payload, no decode needed
var ctxr = new CTXR(File.ReadAllBytes(ctxrPath));
if (ctxr.isPng) pngBytes = ctxr.mipMapsList[0][0];   // magic 0x474E5089 (":PNG"); textureFormat == -1
```

Otherwise the library decodes containers (SOTC footer `0x25`, DeSR `0x6E`; PS3 variant handled
at `CTXR.cs:388`) down to **DDS**, never PNG:

```csharp
public List<byte[]> GetDDSBytes(string ctxrPath);     // CTXR.cs:1232 — resolves external mip chunks
public void WriteToDDS(string ctxrPath, string outPath); // CTXR.cs:1195 — writes .dds (or .png passthrough)
public int GetWidth();  public int GetHeight();          // CTXR.cs:1062/1076
```

* Deswizzling is fully managed (DrSwizzler `DeSwizzler.PS4DeSwizzle`/`PS5DeSwizzle`).
* External mip levels live in sibling `.ctxc` chunk files referenced by
  `ctxr.mipPaths[i].externalMipReference` (`$`-rooted, `****` wildcard) — `GetDDSBytes` reads them
  from disk relative to the `eboot.bin` root (`CTXR.cs:1266-1294`).
* `CTXR.cs` compression check runs first — `8C 0A` ctxr (per the plan's T3 samples) hits the same
  Windows-only Oodle wall as §0.3.
* **Task 3 done (2026-10-15):** `BpConvert.Core.Textures.CtxrConverter` now produces real PNGs —
  hand-written BC1/BC2/BC3/BC4/BC5 + masked RGBA8/BGRA8 decoders and an in-house PNG writer (no new
  packages). See §9 for the reconnaissance and the sharp edges found there.
* Useful header-only path for cheap scans: `new CTXR(bytes, readTexBuffers: false)`.

## 7. Other BluePoint containers present in `Data` (not needed for M0 v1)

`CANI` (animations — plan's M3 fallback), `CGPR`/`CGPRObject` tree used by CMDL parsing, `CAWS`,
`CTAL`, `CRMP`, `CCLM`, `LICM`-era helpers, `CSKL` variants for PS3 SOTC/Ico. `CLength`/`BPString`
handle demon's-souls-style length-prefixed strings.

## 8. Reproduce (commands used in Task 0)

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/SOTC-extract/extracted/game
dotnet build BpConvert.sln                                    # 0 warnings, 0 errors
dotnet test tests/BpConvert.Tests -v q                        # all 7 integration tests run + pass
dotnet run --project src/BpConvert.Cli -- inspect "$SOTC_GAME_ROOT/models/objects/props/_cmn/obj_plank_01.cmsh"
dotnet publish src/BpConvert.Cli -c Release -o out/bin        # dylib lands beside bp-convert.dll (Task 7)
```

Notes: `dotnet test` resolves the vendored dylib from the test output directory; if you run a bare
apphost (`./out/bin/bp-convert`) on this machine, export
`DOTNET_ROOT=/opt/homebrew/Cellar/dotnet/10.0.401_1/libexec` first (see §0, environment quirk).

Resolved tool versions: .NET SDK 10.0.401 (arm64), SharpGLTF.Core **1.0.7** (test-only),
submodule commit **5dcf37a815bf683852bbfd283834c1e8d23f38ad**, vendored Oodle dylib sha256
**47269bc3c9f7ef7948775be2c018ce674d1ab8a8526b6935e9d1033b0b47e8b7**.

## 9. T3 reconnaissance — texture formats, decoder and CC0A findings (2026-10-15)

Reconnaissance for `BpConvert.Core.Textures.CtxrConverter` (`ctxr → png`), from the extracted tree
(`$SOTC_GAME_ROOT` = `…/extracted/game`, all **7,334** `*.ctxr` files header-scanned).

**Variant/format inventory** (first two bytes → count; `0x25` footer textures unless noted):

| variant | files | notes |
|---|---|---|
| `8C 0A` | 6,901 | Oodle; formats below |
| `CC 0A` | 325 | Oodle; **100 % textureFormat `0x10` (BC6H_UF16)** — every IBL/sky texture |
| `0B 00` | 86 | uncompressed, format `0xB` (BC1), legacy fourCC `DXT1` |
| `89 50` (`PNG`) | 13 | raw PNG payloads (`textureFormat == -1`), e.g. `misc/boot/_ps4/saveshadow_scee_tr.ctxr` |
| `00 00` | 7 | uncompressed, format `0x0` (R8G8B8A8_UNORM), masked `R=0xFF G=0xFF00 B=0xFF0000 A=0xFF000000` |
| `8C 06` | 1 | `textures/shadow/characters/mono/_ps4/gradient_mask_alpha.ctxr`, format `0xE` (BC4) |
| `0F 00` | 1 | uncompressed, format `0xF` (BC5) |

`8C 0A` format histogram: BC1 `0xB` 4,715 · BC5 `0xF` 907 · RGBA8 `0x0` 753 · BC3 `0xD` 263 ·
BC4 `0xE` 138 · BC6H `0x10` 124 · BC2 `0xC` 1. **No BC7 anywhere.** 3,682 of the 6,901 `8C 0A`
files carry **external `.ctxc` mip chunks**.

**Pinned samples**: `misc/boot/_ps4/saveshadow_scee_tr.ctxr` = raw PNG (IHDR 228×128, copy
verbatim). `textures/ocean/flowmaps/_ps4/neutral_flowmap_nml.ctxr` = `8C 0A`, 16×16 BC5UNORM,
5 mips, **no external mips**; its top mip is uniformly `80 80` — a *neutral* normal, so the file
decodes to the single RGBA value (128, 128, 0, 255) by design (BCnEncoder.Net agrees byte-for-byte;
the "not all-identical pixels" check therefore uses a detailed sample instead).

**Decoder chosen** (product code, no new NuGet packages): hand-written BC1/BC2/BC3/BC4/BC5 block
decoders plus the 32-bit masked (RGBA8/BGRA8/RGBX/BGRX) layouts in
`src/BpConvert.Core/Textures/BcnDecoder.cs`; PNG encoding by an in-house writer (RGBA8, filter 0,
`System.IO.Compression.ZLibStream` + table CRC-32) in `CtxrConverter.cs`. Cross-checked against the
in-graph, test-only `BCnEncoder.Net` 2.1.0: BC1 and BC2 are byte-identical, BC3/BC4/BC5 differ only
on interpolated palette entries by ±1 (BCnEncoder rounds; we truncate, matching the D3D/Vulkan
integer reference). BC4/BC5 keep the single/two channels where they are (red, red+green) with
alpha 255, matching BCnEncoder.Net and texconv. Verified visually on decoded UI/logo/leaf textures.
(`SixLabors.ImageSharp` 3.1.6, used to decode written PNGs, is transitive through the GPL-3.0
submodule — a direct `PackageReference` of `AquaModelLibrary.Data` — and is present in the
**product** graph: its DLL ships in the published CLI output. It is under the
**Six Labors Split License** — not Apache-2.0 — hence personal use only, no redistribution;
see `tools/native/NOTICE.md`.)

**Sharp edges found (and how the converter handles them)**

* The library's own `DDS.GetDXGIFormat()` resolves the `BC4U` fourCC it writes itself to
  `R8G8B8A8UNORM` (silently wrong pixels); `CtxrConverter.ParseDdsHeader` parses the DDS header
  itself and maps `BC4U`/`ATI1` → BC4, `BC5U`/`ATI2` → BC5, `DXT1/2/3/4/5`, DX10 and the masked
  layouts, and rejects anything else **by name** (BC6H/BC7/SNORM/float).
* `8C 06` `gradient_mask_alpha.ctxr` writes `dwMipMapCount = 0` while carrying a full mip chain —
  harmless, only mip 0 is decoded.
* `textures/shadow/ui/_ps4/hdr_calibration_image.ctxr` declares `-16000x692` (its SOTC
  width/height bytes decode to a negative width) → the converter refuses with that message.
* **External mip chunks are unusable through the library on macOS** — `GetDDSBytes` builds chunk
  paths with `.Replace("/", "\\")` (`CTXR.cs:1272`), and on macOS a backslash is a literal filename
  character, so `File.Exists` never matches; the assembled DDS then silently contains only the
  *internal* mips while claiming the full resolution (observed directly: `arena_02_flow_nml.ctxr`
  with a fake `eboot.bin` root → 87,408-byte payload for a declared 2048×2048 BC5 top mip).
  Independently, `PSUtility.GetPSRootPath` needs an `eboot.bin` ancestor and this extraction keeps
  `eboot.bin` in `extracted/uroot/` while the assets live in `extracted/game/` (no `textures/`
  under `uroot`), so `$`-chunk references cannot resolve at all here.
  **Resolution (D2, 2026-10-15): we assemble mip 0 ourselves.** `CtxrConverter.AssembleExternalMip0`
  resolves the level-0 `mipPaths` reference by walking the *ancestors of the container's own
  directory* (no environment variables, no globals): the first ancestor under which the reference
  exists wins, with `****` tried as `_cmn`, then `_ps4`, then `_ps5` (the game's shared-first order;
  the library's own order is `_cmn`/`_ps5`/`_ps4`). The chunk file is read, decompressed via
  `CompressionHandler.CheckCompression` (`.ctxc` chunks carry their own Oodle marker), sized with
  `CTXR.GetDeSwizzleSize`, de-swizzled with `DeSwizzler.PS4DeSwizzle`/`PS5DeSwizzle` and cropped by
  `DeSwizzler.ExtractTile` — mirroring `ReadAndDeSwizzleCTexChunk` (`CTXR.cs:1411-1432`) exactly, but
  with `/` separators. **Only mip 0 is assembled; lower levels are skipped** (consumers re-mip),
  which is also what the DDS blob we hand to `BcnDecoder` declares (`mipCount: 1`). Verified: 35/35
  random files from the previously-refused class convert (0 failures, ~0.4 s/file), the minotaur
  statue and Wander head/skin/poncho textures render correctly, and
  `arena_02_flow_nml.ctxr` goes from the library's 87,408-byte internal-only payload (wrong mip) to
  a 4,194,304-byte mip 0 (= the level-0 reference's own `bufferSize`).
* All 3,682 external-mip containers are `textureType Standard`, `sliceCount 1`, formats
  BC1 2,523 / BC5 888 / BC3 140 / BC4 125 / RGBA8 5 / BC2 1, every one with a level-0 reference.
  Cube/volume containers with external mips are not present; the assembler refuses them by name.

**Convertible set after D2** (whole-tree picture): 3,095 containers without external mips + 3,682
with external mips now assembled = **6,777 of 6,901 `8C 0A` files (98.2 %)**, plus the 13 PNG and
94 uncompressed-variant files. Only BC6H (124 `8C 0A` + all 325 `CC 0A`) and
`hdr_calibration_image.ctxr` remain refused, by name.

**CC0A path**. Found via the brief's scan: the first `cc0a` file is
`textures/shadow/sky/_ps4/sky_op2_storm__col.ctxr` (30,455,533 bytes, textureFormat `0x10`), and a
full scan shows **all 325 CC0A containers are BC6H_UF16** (8192×4096 sky 14 mips + 256²–512² IBL
cubes — HDR). The converter routes them as variant `cc0a`, decompresses through the vendored dylib
(0x0ACC works exactly like 0x0A8C), de-swizzles and assembles the DDS, then **fails by name**:
`cc0a container: unsupported DDS DXGI format 95 (BC6H_UF16)` — no BC6H decode was attempted.
CC0A can therefore not succeed anywhere in this tree, and no decodable CC0A sample exists to pin a
test on. A synthetic substitute is not available either: `Zamboni.Oodle.OodleCompress` only emits
`8C 06` stream headers, and re-labelling such a stream as `CC 0A` breaks decompression (the marker
bytes are the Oodle stream header itself — byte-0-only edit `CC 06`: payload no longer round-trips;
`CC 0A`: `OodleLZ_Decompress` fails outright). **Controller ruling (D1, 2026-10-15):** accepted as a
*named M0 limitation* — the `cc0a` route stays verified up to the named refusal, no BC6H work in M0;
revisit at M1+ with an HDR-capable container (EXR/KTX2 + tonemap), since PNG is the wrong container
for HDR.

---

## 10. Adapter notes (Task 4, 2026-10-15) — derived rules for `AquaModelAdapter` / `ModelConverter`

Everything below was derived by reading `BluePointConvert.cs` (§0 R-A: it is a *reference* only) and
by scanning the extracted tree; it is the contract the adapter implements. Where nothing was
derivable, the behavior is a named, tested degradation.

### 10.1 Material chain for a bare `.cmsh` (cmsh → cmdl → cmat → ctxr)

The CMSH carries no material data of its own: `header.meshList[i].matName` is a key, resolved as

```
matName → sibling .cmdl .GetCMATMaterialMap()[matName]  →  $/…/x.cmat  →  CMAT.texNames  →  $/…/x.ctxr
```

* **Sibling `.cmdl` selection** (the brief's pinned cases are `obj_plank_01.cmsh` →
  `obj_plank_01_52f8cc91.cmdl` and `wander.cmsh` → `wander.cmdl`): exact stem first, then a stem
  extension (`<stem>_…`, the game's unique-id suffix). Among several extensions, one whose suffix
  after `<stem>_` is **exactly 8 hex digits** wins (the mesh's own unique-id cmdl); otherwise the
  first candidate in ordinal path order. *Why (S9, 2026-10-23):* plain first-match let a longer-named
  neighbour's cmdl win — `arena_01_entrance_skirt.cmsh` took `arena_01_entrance_skirt_02_6cb02344.cmdl`
  (skirt_02's, maps one of its four materials) over its own `arena_01_entrance_skirt_ec63e197.cmdl`
  (maps all four), so three materials fell back to the viewer's white 1×1 (the S9 "white slab").
  The hex preference changes **10** picks over the tree (the entrance skirt, `arena_02_gbskirt`,
  `arena_02_landbridge`, `arena_11_skirts`, `hall_09_skirts`, `hall_15_water`, `level_02`,
  `level_10`, plus `hall_16_skirts` and `cathedral_ext_fur`, which re-convert byte-identical: same map)
  and loses none; a strict hex-only rule would lose 18 non-hex owners (17 `_group` + `collectibles_corrupt_cloak`: `lion_a_statue_broke` →
  `lion_a_statue_broke_group.cmdl`, `hall_08_water` → `hall_08_water_group_38bd1fbf.cmdl`, …), hence
  the first-match fallback. Pinned by `SiblingCmdlTests`. The tree holds **22,012** `*.cmsh` under `models/` (the S9
  white-slab simulation's `cmdlpick.txt` counted 21,510; 22,012 is the recount). The figures below
  **predate B7** (measured under the first-match rule): B7 changes which extension wins for 10 cmsh,
  not how many find one, so the exact/extension counts stand; the all-`matName`s percentages may have
  moved for those 10. Measured over the tree:
  **5,610 exact + 1,292 extension-only = 6,902 (31 %)**; of those, 97.9 % / 97.6 % have *all* their
  `matName`s present in the chosen cmdl's map. The remaining 15,110 are UDIM/LOD tiles whose owner
  cmdl references *them* (20,005 have such a referencing cmdl) — that search is **not** implemented
  in v1 and is a named M1 candidate (the plank pin rules it out for the plank itself:
  `obj_plank_01_52f8cc91.cmdl` references `obj_plank_01_f04cfcce.cmsh`, not `obj_plank_01.cmsh`).
* **No sibling `.cmdl`** → *single default material* (`"default"`, no textures, all sub-meshes
  share it) — the pinned degradation; this is what the fixture tests (plank copies without their
  cmdl) exercise. A `matName` missing from a *found* cmdl's map degrades per-material (material
  keeps the name, no textures).
* The reference's additional bruteforce (`<parent>/materials/_cmn/<name>.cmat`,
  `<modelDir>/<name>.cmat`, `BluePointConvert.cs:702-722`) is *not* implemented: for the plank it
  finds nothing (`bpShader1.cmat`), and it would be a guess.
* `GetCMATMaterialMap()` can return `null` keys (library `cmatMap.Add(matName, …)` with a nameless
  set) — such cmdls throw inside the library; not worked around.

### 10.2 CMAT `texNames` → material slots

Slot order in `CMAT.texNames` is **not** reliable (`_col` sits at index 0 in 7,883 of 8,765
materials that have one, but 370 at index 1, 281 at index 2, …; the plain `_nml` is at index 2 in
1,545 and index 3 in 6,476). The shader list (`shaderNames`) is a cooked-pipeline list
(`depth`/`shadow`/`gbuffer…`/`alpha` `.csdr` stages) and carries no slot information. The rule is
therefore **name-based**:

| slot | rule | evidence |
|---|---|---|
| base color | first `texName` with a `col` stem token, else `texNames[0]` | the reference's albedo heuristic: `tex.Contains("_col.")` (`BluePointConvert.cs:987-1001`), token-wise so `ocean_col_02` / `vineleaf01_col_alpha` are caught too. 8,765/8,920 materials have `_col`; the 121 without are terrain decals (`_msk`) and non-render shadow casters |
| normal | first `texName` whose last stem token is `nml`/`nrm` and whose family token is *not* `spec`/`specular`/`flow`/`flowmap`; else the first `tangent_nml` | 8,712/8,920 materials have a plain `_nml`. `spec_nml`/`specular_nml` (18 materials) and `flow(map)_nml` (22+22) are different maps; `tangent_nml` is a tangent-space normal (what a glTF `normalTexture` is) and is the only normal in 6 hair materials (LOD copies of `wander_hair_03`) |
| specular | **never set** (null) | no SOTC CMAT references a specular *colour* texture; the only specular-named maps are `*_spec_nml` BC5 two-channel normal-detail maps, which do not fit `KHR_materials_specular.specularTexture` semantics (specular colour F0 + strength in alpha) |
| roughness/height/alpha/transmission/AO/mask | not mappable in M0's `MaterialData` | still converted: **every resolved `texNames` entry becomes a PNG** in `PngPaths`, mapped or not |
| `BaseColorFactor` / `EmissiveFactor` | `(1,1,1,1)` / `(0,0,0)` | CMAT colours are not parsed by the library (`CMAT.cs:79/103` TODO) |

### 10.3 Sub-mesh ranges, geometry and winding

* SOTC sub-mesh range = `startingFaceVertIndex/3` + `faceVertIndicesUsed/3`; DeSR uses
  `startingFaceIndex`/`faceIndexCount` (§3, `BluePointConvert.cs:660-690`). The reference's
  degenerate marker (`faceCount == 0 && faceIndexCount > 1` — SOTC never sets that field, it stays
  −1) and the all-zero failsafe ("take the rest of the buffer", `currentFace` advancing by the
  range taken) are implemented as written. A sub-mesh that is empty *after* range computation is
  skipped; ranges are clamped to the face list and faces with out-of-range indices are dropped
  (the writer has no index range check). Measured: **0 degenerate sub-meshes** in the pinned
  samples (12/12 wander, 3/3 statue, 1/1 plank) and in a 150-file tree slice (236/236 emitted).
* Vertices are compacted per sub-mesh (the reference's per-material vertex remap) in face order;
  positions are written unchanged (**no axis conversion** — both sides are Y-up; verified: the
  plank's thickness sits in Y, wander is 1.26 tall in Y vs 1.23 wide in X arm-span and 0.21 deep,
  statue pieces within ±8). Winding is **already CCW-outward** (glTF's front-face convention), so
  no triangle reordering: face geometry beat the vertex normals on 732/756 plank, 736/736
  rock_slide, 11,440/11,446 wander-body and 28,930/28,930 wander-hair faces (mean dot 0.91–0.999).
* UVs: TEX0..TEX3 channels whose length matches the vertex count, written unchanged (half-float
  pairs); `TEX3` is wander's second channel. No V-flip: the decoded PNGs are top-down and glTF's
  UV origin is top-left, like the game's DirectX-style textures.

### 10.4 Normals: NRM0 is decodable (the library only documents QUT0)

SOTC `.cmsh` files do **not** carry QUT0: every sample's `vertDefs` holds `NRM0`+`TAN0`
(4 bytes/vertex; `CMSHVertexData.normalTemp` receives both streams in `vertDefs` order, NRM0
first). The library has no decoder ("These are a similar idea, but NOT the same as QUT0",
`CMSHVertexData.cs:120`), so the adapter implements one:

```
x = (packed[0:10]  − 511.5)/511.5      packed = LE uint32 of the 4-byte entry
y = (packed[10:20] − 511.5)/511.5      then normalized (defensively; entries are not always
z = (packed[20:30] − 511.5)/511.5      unit — rock_slide_01 decodes to 0.377)
```

Same packing the library uses for a QUT0's vector part (`UnpackQUT0`, `CMSHVertexData.cs:414-421`).
Verified on the pinned samples: decoded lengths 0.998–1.001 (plank, wander), direction agrees with
the face winding as above, and the plank's dominant normal axis is Y (its 0.31-thick side).
QUT0 is used when present (other BluePoint titles); a zero-length entry falls back to Y-up.

### 10.5 Reference resolution, output mapping and skip policy (`ModelConverter`)

* `$`-refs: strip `$/`, then `****` → `_cmn`, `_ps4`, `_ps5` (first existing wins — the same order
  Task 3 pinned for `.ctxc`; the library's own is `_cmn`/`_ps5`/`_ps4`). Unresolved references are
  **not fatal**: a missing `.cmat`/`.ctxr` leaves its slot null (material keeps its name) and a
  missing texture is simply not converted.
* Output: `<outLib>/<source tree path>.glb` and `<outLib>/<ctxr tree path>.png` (pinned mapping);
  `textureUriMap` = source `.ctxr` tree path → PNG uri **relative to the `.glb`'s directory**,
  `/`-separated. `PngPaths` is the deduped list of PNGs actually written (absolute).
* Refused textures (BC6H/HDR, negative dimensions, …) are skipped, not fatal:
  `CtxrConverter.Convert`'s `InvalidDataException`/`NotSupportedException` are caught, the model
  still writes its `.glb`, and the unmapped channel is omitted. Measured: 0 refusals among the
  pinned samples (3 + 41 + 12 textures) and the 150-file slice (557 textures, all written).
* Sources: `.cmsh` only in v1 (`NotSupportedException` otherwise — a `.cmdl` merely lists meshes);
  a missing source throws `FileNotFoundException`. Reference-mesh/LOD stubs (`cmsh.header == null`)
  produce a valid empty `.glb` (0 meshes) instead of failing.
* **Diagnostics** (the brief's "keep a diagnostic" / "record it"): `Load` and `Convert` take an
  optional `Action<string>? note` (additive to the pinned 1-/3-argument call shapes; the pinned
  signatures keep working). Reported: unresolved `$`/`****` references (material and texture),
  the no-sibling-cmdl default-material fallback, skipped sub-meshes and dropped
  out-of-range faces, and refused textures. No other channel exists — degradations are otherwise
  observable only structurally (null slots, absent PNGs, 0 meshes).
* Library parse limits are **not** worked around: e.g. `models/objects/foliage/grass/*.cmsh`
  (8 of the first 150 tree files) throw `OverflowException` in
  `CMSHVertexData.ReadVertDefData` (the 16-bit-colour `dataFormat == 7` hack seeks past the
  stream). Those files fail loudly; the batch runner records them (Task 6).
* `ModelData`/`MeshData` come from the adapter with **absolute** resolved texture paths
  (`SourceRelativePath` = file name) because `AquaModelAdapter` has no game root of its own;
  `ModelConverter` normalises both to game-root-relative paths before writing. Skin fields are
  filled and `Skeleton` is set by Task 5 — see §11.

## 11. Skinning (Task 5, 2026-10-15) — adapter append

Implements the adapter's skeleton/skinning half from §5. Records what the real tree showed and the
decisions taken (CSKL routing, IBM element order, index/weight policies); §11.2 also covers the
writer-side bind-pose joint transforms that closed the initial "nodes carry no transforms" gap
(2026-10-16 addendum).

### 11.1 CSKL routing (measured)

SOTC meshes do **not** spell their skeleton as `$/…`: all 128 skinned `.cmsh` under
`models/shadow/characters` store a **rooted** `/models/…/x.cskl` path (0 `$/`-prefixed, 0 bare),
and a file of that name exists beside every one of them. The adapter therefore tries
`Path.GetFileName(skeletonPath)` next to the mesh first (the reference's first route,
`BluePointConvert.cs:499-515`), then the Task 4 `$`-reference route (`$/` strip, `****` →
`_cmn`/`_ps4`/`_ps5`; `AssetResolver` already treats a leading `/` as tree-relative), and records a
miss like every other unresolved reference. Tree figures: 341 `.cmsh` under
`models/shadow/characters` (128 skinned); `models/shadow/objects` + `models/shadow/characters`
together 160 skinned meshes / 10.86M skinned vertices — the actor subtrees (colossi, horses) matter
as much as the characters. All 128 character meshes resolve beside the model; a 1,353-file adapter
sweep over both subtrees built 160 skeletons (21,106 joints) with 0 conversion failures.

### 11.2 Inverse bind matrices — recipe, and the element-order (transpose) decision

**Recipe** (SOTC `footerData.version == 0x9`, `BluePointConvert.cs:832-854`): per bone in order,
`mat = ComputeLocalTransform()`; with a parent, `mat = mat × inverse(IBM_parent)` — i.e. the
parent's bind world transform, so `mat` becomes the bone's own — then `IBM = inverse(mat)`. All of
it in `System.Numerics`' row-vector convention (translation in `M41..M43`). The adapter resolves a
parent on demand (files list parents first; recursion keeps that true for any order) and rejects
cycles/singular transforms with a note instead of emitting NaNs. `0x19` (DeSR) files ship the
matrices and the library reads them out of the file's layout into the row-vector convention
(`CSKL.cs:117`); the adapter uses them when complete, and refuses the file (note → model
unskinned) when they are not.

**Element order.** `SkeletonJoint.InverseBindMatrix16` is written verbatim by the writer into a
glTF MAT4 accessor, so the adapter must pick the sequence. It writes the matrix's own rows in
order: `M11 M12 M13 M14 M21 … M44`. A glTF loader reads those 16 floats column by column and
applies them as a column-vector matrix, so the pairing row-vector/System-Numerics (row order) with
column-major storage is the *same geometric transform*: a translation in `M41..M43` lands in
glTF's last column (offsets 12..14). Three independent supports:

1. **Conventions.** A row-vector matrix with the same element values as a column-vector matrix
   applies the transposed map, and glTF's `inverseBindMatrices` are column-vector matrices stored
   column-major. Canonical check: `CreateTranslation(0,2,0)` has raw order
   `[1,0,0,0, 0,1,0,0, 0,0,1,0, 0,2,0,1]`, which is exactly glTF's documented column-major array
   for that transform.
2. **The library's own file reader/writer.** The `.cskl` files store IBMs in the *other* element
   order (`M11 M21 M31 M41 M12 …`, "Column-major 3x4", `CSKL.cs:486-492`) and the library
   **transposes** when reading them into `System.Numerics` (`CSKL.cs:117`) — i.e. the game's
   layout is the column-vector one, and glTF's layout is that layout, not the `System.Numerics`
   one.
3. **The library's own skinning export.** `AssimpModelExporter.cs:308-311` builds
   `aiBone.OffsetMatrix` — assimp's inverse bind matrix — by reading the AQN node's `m1..m4` rows
   **transposed** (`new Matrix4x4(m1.X, m2.X, m3.X, m4.X, m1.Y, …)`), the same conversion the
   adapter performs for glTF.

Experiment on the pinned sample: writing wander's IBM accessor with the elements in the file's
order makes SharpGLTF **reject the file at load** (`Accessor[0] Matrices: Invalid MatrixModel`),
while the chosen order decodes to the derived translation (`Bone_EYELID_Bot_L`:
`M41..M43 ≈ (0.1066, −0.3910, 0.0188)`); the pinned `Wander_HasSkin_WithJoints` test asserts that
translation, so the layout is pinned, not just the counts.

**Joint node transforms (2026-10-16 addendum).** The writer (`GlbWriter`) now derives each joint
node's bind-pose local transform from the data it already receives — no DTO change, no pin change:

```
world_i  = inverse(IBM_i)                      (glTF column-vector, or row-vector with the
local_i  = world_i × IBM_parent                  same elements — the identity transposes)
local_i  = world_i                            (roots, ParentIndex == -1)
```

In the row-vector terms this project uses (`local = world × IBM_parent`, `Matrix4x4.Multiply` =
"apply left, then right"), that is the transpose of the column-vector
`local = inverse(world_parent) × world` — i.e. the same node matrix on the wire once written in
glTF's element order, which is also the DTO's own order (`M11 M12 M13 M14 M21 … M44`). The reason
the node matrices are derivable at all: glTF defines the skin by
`jointMatrix = globalTransform(joint) × IBM`, and at bind pose `jointMatrix` must be the identity,
so `globalTransform(joint)` *is* `inverse(IBM)` and the node locals follow from the hierarchy.
Shape of the derivation: `IBM_i` and `IBM_parent` are both in the data, so no traversal order is
assumed (the parent's world transform is `IBM_parent` — that is what an inverse bind matrix is).

Acceptance (`Wander_Joints_IdentityAtBindPose`, `Write_Skinned_JointNodesCarryBindPoseTransforms`):
for every joint of the produced glb, `joint.WorldMatrix × IBM` must be the identity with
max-abs-diff ≤ 1e-3. Measured on wander: **max residual 5.96e-7 over all 348 joints** (worst joint
346), i.e. float32 accumulation only — the mesh now renders undeformed at bind pose with a
conformant loader. Models without a skeleton emit no node `matrix` and stay byte-identical
(verified: plank `sha256[:16] 3E09FF76CB5679A1`, minotaur statue `A443CD480FAA20D1`, before and
after the change). These `matrix` nodes are an M0/M1 representation: when animation lands (M3) the
joints should carry TRS (and animation channels) instead, which is a mechanical substitution
(`matrix` ↔ translation/rotation/scale) — the local values stay the bind pose either way.

### 11.3 Weights and indices (policies, measured)

`vertData.vertWeightIndices` (4 per vertex; SOTC ships both widths — 10 ushort / 118 byte across
the character tree, the library reads either) and `vertWeights` (4 × byte/255 → `[0,1]`) are read
only when both cover every vertex; otherwise the model is left unskinned with a note.

* **Normalisation is ours**: the quantised file weights often sum below 1 (wander: 28,404 of 62,466
  vertices non-unit, minimum 0.988), so each vertex is renormalized to exactly 1 — verified over
  the pinned sample (all 62,466 sums at `[1,1]` in float32) and a 6-model smoke pass; the pinned
  `Wander_Weights_Normalized` test samples the first 100 vertices of every primitive.
* **Index safety**: the writer validates *every* index element, even a zero-weight one, so an
  out-of-range/negative index is always dropped to `(0, 0)` (weight 0), counted and noted per
  model. Measured: 0 such slots in the whole shadow tree — the policy is purely defensive. The
  vertex's remaining weights are renormalised.
* **Unweighted vertices**: a vertex left with no weight (all-zero, or everything dropped) is
  marked unweighted. If a sub-mesh has no weighted vertex at all it is emitted **unskinned**
  (empty `BoneIndices`/`BoneWeights`, so `JOINTS_0`/`WEIGHTS_0` are omitted — the plank behaves
  exactly as in Task 4). Inside a skinned sub-mesh such a vertex is bound to the root joint
  (index 0, weight 1) instead of an all-zero quadruple, which keeps every emitted vertex
  normalised and puts it where the root's IBM places it; noted per model. Measured: 0 all-zero
  models and 491 zero-weight vertices across the 341 character meshes (the fallback fires; the
  Task 4 plank pin is unaffected).
* **Weight indices index the CSKL's bone list** (as §5 states and the reference assumes): the
  mesh-local `boneData.boneNames` list matches the CSKL's last-name segments in count and order
  for all 124 parseable character skeletons (0 mismatches), and no weight index exceeds
  `boneCount` (0 of 124).

### 11.4 Degradations and known limits

* **A CSKL that fails to parse degrades, unlike a failing *source***: 4 bird meshes
  (`hawklores/seagull_lores.cmsh`, `hawklores/hawk_lores.cmsh`, `hawk/obj_bird_b.cmsh`,
  `hawklores/dove_lores.cmsh`) reference skeletons the library cannot read — `KeyNotFoundException`
  at `CSKL.cs:73` (missing parent rows in `hawk_lores.cskl` / `obj_bird_b.cskl`). The adapter
  catches that, records `skeleton '…' failed to parse (…); model left unskinned` and still converts
  the mesh. Rationale: the skeleton is an optional reference (the Task 4 treatment of unresolved
  materials/textures), whereas a parse failure of the *source* `.cmsh` stays loud (Task 4's grass
  files). When the library is fixed upstream the skins appear without further changes.
* **The reference's synthetic skeleton is not implemented.** When a mesh has `boneData` but no
  CSKL, `BluePointConvert.cs:526-551` fabricates a chain from `boneNames` with identity IBMs.
  Without transforms that rig cannot animate and the identity matrices only reproduce a bind pose
  whose nodes are also identity — an unskinned mesh is the same picture with fewer parts, so v1
  degrades (note) instead.
* **The shipped `boneBoundingSpheres` do not corroborate the documented generator.** Checking a
  bone's sphere center against `IBM · average(influenced vertices)` (the recipe
  `CMSHBoneData.cs` describes) misses by ≈0.42 on average against a mean radius of 0.036
  (3/150 wander bones inside their radius), so the spheres were not used to validate anything and
  are not converted. Worth a look if the bounding volumes are ever needed.
* Unsupported CSKL versions (neither `0x9` nor a complete `0x19`) leave the model unskinned with a
  note rather than guessing identity bind matrices.

## 12. Objanim event tracks and the `<glb stem>.events.json` sidecar (S12 Task 3)

Not an `AquaModelLibrary` surface: `objanim_*.anb` are the flag-set `.anb` variant (version-word bit 16
set, a 12-byte `{type, name, blob}` record array), which `AnbReader` still refuses. They carry the
per-clip volumes and events the fights run on. Decoders: `src/BpConvert.Core/Anim/ObjAnimReader.cs`
(`ObjAnimReader.Read(ReadOnlySpan<byte>)`, a port of the recon's `objanim/py/objanim.py` over
`tools/re/anb/records.py`), sidecar writer `EventsSidecar.cs`, curated link `tools/re/objanim/<colossus>_map.json`.

* **Tracks kept:** 3 (sphere), 10 (cylinder), 13 (box) damage; 29 effect triggers (event windows
  `[start, start + len]`, the engine's start-crossing); 44 (cylinder/ellipsoid sector, kind `w0 >> 20`)
  and 46 (box) AI judge volumes. Type 11 (armour-debris emitters, 612 in Gaius's clips) and every other
  type are skipped. `ObjAnimReader` returns the game frame; `Parent` is the raw aux word (≥ 0 skb record,
  `0x10000|k` OptBone k, −1 entity, −2 entity root, −3 skelton).
* **Refusals:** a non-xff input (e.g. `objanim_minotaur_a_death_front.anb`, a JSON text file) and the
  flag-clear variant throw `InvalidDataException` (`ObjAnimReader.IsRefusal`); the sidecar writer skips
  those with a note. Malformed flag-set input throws the same type without the refusal flag.
* **Shared decode:** the packed quaternion math (types/formats 3–6, 0xC, 0xD, 0xE) lives in
  `AnbKeyMath` (internal) and is used by both readers; `AnbReader`'s public contract is unchanged.
* **Sidecar:** `{"format": "sotc-objanim-events/1", "skeleton", "frame": "glb", "judgeDefaults": [...],
  "clips": {"<clip>": {"fps", "frames", "judge", "damage", "effects"}}, "spawned": {"<objanim>": {"frames",
  "fps", "damage"}}}`, row shapes as the recon `objanim/schema.json` `$defs` (the schema's top-level
  array/`version` form became this keyed form). Glb frame: positions x → −x, quaternions
  `(−x, y, z, w)`; sizes untouched; times are the objanim's own frames at its own fps. Static rows
  collapse to `static`; damage rows keep keys only inside their windows; effects keep one key per window
  start. Each row keeps its parent kind and joint name (R4: bone = glb joint of that name, offset (−x, y, z);
  entity (−x, y, z); skelton (x, hipY + y, −z)); posing is the app's.
* **Written:** by `convert --clips` beside the glb, only when a baked clip has an `objanim_<clip>.anb`
  (or the bank has `objanim_<bank>_skelton.anb`, the default judge volumes); a conversion that bakes
  nothing removes a stale one. `--objanim-maps <dir>` (default `tools/re/objanim` when present) picks the
  map whose `skeleton` matches. Measured: knight_a, 26 clips + 15 default judge volumes + 2 spawned
  objanims, 41.8 KB compact. Derived game data: never committed.
* **The map** (`knight_map.json`): `spawns` (effect name → spawned objanim stems; the spawned files are
  not `knight_a_*` clips, so the writer reads them by name) and `damage` (volume name → `amount`
  = `damage_def+0x1c`, `reaction` = `+0x18`; raw, meanings a guess). The link is in the eboot's effect
  tables, which the reader cannot see.
