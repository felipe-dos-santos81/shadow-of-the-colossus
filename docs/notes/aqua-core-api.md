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

**Sharp edges found (and how the converter handles them)**

* The library's own `DDS.GetDXGIFormat()` resolves the `BC4U` fourCC it writes itself to
  `R8G8B8A8UNORM` (silently wrong pixels); `CtxrConverter.ParseDdsHeader` parses the DDS header
  itself and maps `BC4U`/`ATI1` → BC4, `BC5U`/`ATI2` → BC5, `DXT1/2/3/4/5`, DX10 and the masked
  layouts, and rejects anything else **by name** (BC6H/BC7/SNORM/float).
* `8C 06` `gradient_mask_alpha.ctxr` writes `dwMipMapCount = 0` while carrying a full mip chain —
  harmless, only mip 0 is decoded.
* `textures/shadow/ui/_ps4/hdr_calibration_image.ctxr` declares `-16000x692` (its SOTC
  width/height bytes decode to a negative width) → the converter refuses with that message.
* **External mip chunks are unusable on macOS**: `GetDDSBytes` builds chunk paths with
  `.Replace("/", "\\")` (`CTXR.cs:1272`), and on macOS a backslash is a literal filename
  character, so `File.Exists` never matches; the assembled DDS then silently contains only the
  *internal* mips while claiming the full resolution (observed directly: `arena_02_flow_nml.ctxr`
  with a fake `eboot.bin` root → 87,408-byte payload for a declared 2048×2048 BC5 top mip).
  Independently, `PSUtility.GetPSRootPath` needs an `eboot.bin` ancestor and this extraction keeps
  `eboot.bin` in `extracted/uroot/` while the assets live in `extracted/game/` (no `textures/`
  under `uroot`), so `$`-chunk references cannot resolve at all here. `CtxrConverter` therefore
  **rejects any container with `externalMipCount > 0`** (loud, path-named) instead of writing a
  wrong mip level. That is 3,682 of the 6,901 Oodle `8C 0A` files (53 %). Remaining convertible
  set: 3,095 (BC1 2,192 · RGBA8 748 · BC3 123 · BC5 19 · BC4 13), plus the 13 PNG + 94
  uncompressed-variant files; a 30-file random sample of the convertible set converted 30/30 OK.

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
`CC 0A`: `OodleLZ_Decompress` fails outright). BC6H support (or an HDR-aware path) is an explicit
scope decision for the M0 controller, not something to guess at.
