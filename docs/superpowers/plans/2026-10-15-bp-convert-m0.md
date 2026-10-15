# bp-convert (M0 — Converter Toolchain) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `bp-convert`, the headless macOS toolchain that converts SOTC (2018) models, materials and textures from the extracted game tree into a glTF 2.0 + PNG asset library with a resumable catalog.

**Architecture:** A .NET 10 solution — a thin CLI over a core library. All AquaModelLibrary.Core touchpoints are isolated behind one adapter (`Aqua/`); the rest of the codebase works only on our own DTOs. We write `.glb` ourselves (minimal in-house glTF writer) and convert `.ctxr → .png` via the library's decode path (portable Oodle decoder if needed). The game tree is read-only; the output library mirrors the source tree so texture references stay relative and collision-free.

**Tech Stack:** C# / .NET 10 (arm64), xUnit, SharpGLTF (test-only, NuGet), AquaModelLibrary.Core (GPL-3.0, git submodule, parse-only), minimal in-house glTF writer.

**Spec:** `docs/superpowers/specs/2026-10-15-sotc-native-rebuild-design.md`

## Global Constraints

- Target framework `net10.0`; must build and run on macOS arm64. No Windows/x64 requirement in the product path (spec A3).
- Game tree is **read-only** at `$SOTC_GAME_ROOT`; default for this machine:
  `/Users/felipe.dos.santos/Downloads/SOTC-extract/extracted/game`. Tests fail fast with a clear message if the env var is unset.
- Repo `~/Downloads/sotc-native`; game data (and anything derived from it) is **never committed** (spec A4).
- License: GPL-3.0 (we link GPL-3.0 AquaModelLibrary.Core). `LICENSE` file at repo root.
- Offline-first: nothing in this plan parses game containers at runtime; this is a build-time tool (design §8).
- Output layout: `<out>/<source-relative-dir>/<basename>.glb` plus mirrored `<out>/<texture-relative-path>.png`; default `<out>` = `out/library/` (gitignored).
- Any NuGet dependency added to the product must be arm64-macOS-safe and MIT/Apache-2.0 (or GPL-compatible), and recorded in the commit message. SharpGLTF is test-only.
- Test command convention (all tasks): `SOTC_GAME_ROOT=$SOTC_GAME_ROOT dotnet test tests/BpConvert.Tests -v q`
- Commits are small and typed (`feat:`, `test:`, `chore:`, `docs:`), one per task at minimum.

## Review Focus

Failure modes the spec implies but no single feature test covers — each line has its test pinned to a task below:

1. **Case-mismatched references** (game authored on Windows) — resolver must find files on case-sensitive APFS. → Task 2.
2. **Duplicate source basenames across directories** (`_cmn` folders everywhere) — mirrored output layout must keep every file addressable. → Task 4.
3. **`.ctxr` container variants** (PNG-payload, `8C0A` Oodle, `CC0A`) — texture path must not crash on any variant. → Task 3.
4. **Very large models** (some colossus `cmsh` files are large) — conversion must complete without exhausting memory. → Task 7.
5. **Interrupted batch runs** — catalog must be atomic and resume without duplicates or partial outputs. → Task 6.

---

### Task 0: Repository scaffold + AquaModelLibrary.Core build spike (gate)

**Files:**
- Create: `BpConvert.sln`, `src/BpConvert.Core/BpConvert.Core.csproj`, `src/BpConvert.Cli/BpConvert.Cli.csproj`, `tests/BpConvert.Tests/BpConvert.Tests.csproj`, `tests/BpConvert.Tests/TestAssets.cs`, `tests/BpConvert.Tests/InspectTests.cs`, `docs/notes/aqua-core-api.md`, `LICENSE` (download GPL-3.0 text)
- Modify: `.gitmodules` (via `git submodule add`), `README.md`

**Interfaces:**
- Consumes: nothing (first task).
- Produces: the solution/projects every later task builds in; `TestAssets.Root` / `TestAssets.Sample(string rel)` (test helper); `docs/notes/aqua-core-api.md` — the exact library types/methods used by Task 4's adapter; CLI verb `inspect`; and this pinned core API:

```csharp
namespace BpConvert.Core;
public static class Inspect { public static InspectSummary Summarize(string absolutePath); }
public sealed record InspectSummary(bool Parsed, int MeshCount, string? Error);
```

- [ ] **Step 1: Add the library submodule and scaffold projects**

```bash
cd ~/Downloads/sotc-native
git submodule add https://github.com/Shadowth117/PSO2-Aqua-Library third_party/PSO2-Aqua-Library
git submodule update --init --recursive
git -C third_party/PSO2-Aqua-Library rev-parse HEAD   # record this commit in docs/notes/aqua-core-api.md
dotnet new sln -n BpConvert
dotnet new classlib -o src/BpConvert.Core -n BpConvert.Core -f net10.0
dotnet new console  -o src/BpConvert.Cli  -n BpConvert.Cli  -f net10.0
dotnet new xunit    -o tests/BpConvert.Tests -n BpConvert.Tests -f net10.0
dotnet sln add src/BpConvert.Core src/BpConvert.Cli tests/BpConvert.Tests
dotnet add src/BpConvert.Cli reference src/BpConvert.Core
dotnet add tests/BpConvert.Tests reference src/BpConvert.Core
dotnet add tests/BpConvert.Tests package SharpGLTF.Core   # record resolved version
curl -s https://www.gnu.org/licenses/gpl-3.0.txt -o LICENSE
```

Set `<AssemblyName>bp-convert</AssemblyName>` in `BpConvert.Cli.csproj`, delete the template `Class1.cs`/`UnitTest1.cs`, and `dotnet build` until clean.

- [ ] **Step 2: Build AquaModelLibrary.Core on arm64 and record the result**

```bash
dotnet build third_party/PSO2-Aqua-Library/AquaModelLibrary.Core/AquaModelLibrary.Core.csproj
```

Expected: succeeds as managed code. If it fails on arm64: record the exact error in `docs/notes/aqua-core-api.md` and **STOP — report back; do not proceed** (every later task depends on this).

- [ ] **Step 3: Write the `inspect` verb (header hex + best-effort library parse)**

In `src/BpConvert.Cli/Program.cs`: `inspect <file>` prints size, first 32 bytes hex, and — by reading the library's `BluePoint` sources — a parse summary (mesh count / vertex count if the library exposes it; otherwise the exception type). Document every exact type/namespace/method you touched in `docs/notes/aqua-core-api.md` with a one-line code excerpt each (this file is Task 4's specification).

- [ ] **Step 4: Run it on the three pinned samples and record output in the notes**

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/SOTC-extract/extracted/game
for f in \
  models/shadow/characters/wander/_cmn/wander.cmsh \
  models/objects/props/_cmn/obj_plank_01.cmsh \
  models/shadow/objects/actors/statues/colossus_01_minotaur_a/_cmn/colossus_01_minotaur_a.cmsh \
; do dotnet run --project src/BpConvert.Cli -- inspect "$SOTC_GAME_ROOT/$f"; done
```

(If the exact colossus filename differs, use the largest `*.cmsh` in that directory and pin its name in the notes.)

- [ ] **Step 5: Test helper + first integration test**

`tests/BpConvert.Tests/TestAssets.cs`:

```csharp
public static class TestAssets
{
    public static string Root =>
        Environment.GetEnvironmentVariable("SOTC_GAME_ROOT")
        ?? throw new InvalidOperationException("Set SOTC_GAME_ROOT to the extracted game tree.");
    public static string Sample(string rel) => Path.Combine(Root, rel);
}
```

`tests/BpConvert.Tests/InspectTests.cs`:

```csharp
[Fact]
public void Inspect_RunsOnWanderCmsh()
{
    var file = TestAssets.Sample("models/shadow/characters/wander/_cmn/wander.cmsh");
    Assert.True(File.Exists(file));
    var summary = Core.Inspect.Summarize(file);   // thin call into BpConvert.Core
    Assert.True(summary.MeshCount >= 1);
}
```

(If the library cannot yet give a mesh count on this file, `Summarize` may report 0 meshes with a recorded reason; the assertion then moves to `summary.Parsed == true` — record the decision in the notes.)

- [ ] **Step 6: Run test + commit**

```bash
SOTC_GAME_ROOT=$SOTC_GAME_ROOT dotnet test tests/BpConvert.Tests -v q
git add -A && git commit -m "chore: scaffold solution; spike AquaModelLibrary.Core on arm64; inspect verb + API notes"
```

---

### Task 1: DTOs + glTF writer (synthetic)

**Files:**
- Create: `src/BpConvert.Core/Model/ModelData.cs`, `src/BpConvert.Core/Gltf/GlbWriter.cs`, `tests/BpConvert.Tests/GltfWriterTests.cs`

**Interfaces:**
- Consumes: nothing (pure). Can be built in parallel with Tasks 2–3.
- Produces (exact — later tasks use these verbatim):

```csharp
namespace BpConvert.Core.Model;

public sealed record MeshData(
    string Name, float[] Positions, float[] Normals, float[][] UvChannels, uint[] Indices,
    int[] BoneIndices, float[] BoneWeights, int MaterialIndex);              // -1 = none

public sealed record MaterialData(
    string Name, string? BaseColorTexture, string? NormalTexture, string? SpecularTexture,
    float[] BaseColorFactor, float[] EmissiveFactor);                        // rgba / rgb

public sealed record SkeletonJoint(string Name, int ParentIndex, float[] InverseBindMatrix16);
public sealed record SkeletonData(string Name, List<SkeletonJoint> Joints);

public sealed record ModelData(
    string SourceRelativePath, List<MeshData> Meshes, List<MaterialData> Materials,
    SkeletonData? Skeleton, List<string> ReferencedTexturePaths);

// BpConvert.Core.Gltf
public static class GlbWriter
{
    /// textureUriMap: source .ctxr relative path -> output .png relative URI (from the .glb's directory)
    public static GlbWriteResult Write(ModelData model, string outDir, string glbName,
        IReadOnlyDictionary<string,string> textureUriMap);
}
public sealed record GlbWriteResult(string GlbPath, long Bytes);
```

- [ ] **Step 1: Write failing tests** (`GltfWriterTests.cs`)

```csharp
[Fact] public void Write_SingleTriangle_RoundTrips()
{
    var m = new ModelData("x.cmsh", new(){ new MeshData("m", new float[]{0,0,0, 1,0,0, 0,1,0},
        new float[]{0,0,1, 0,0,1, 0,0,1}, new[]{ new float[]{0,0, 1,0, 0,1} }, new uint[]{0,1,2},
        Array.Empty<int>(), Array.Empty<float>(), -1) }, new(), null, new());
    var r = GlbWriter.Write(m, _dir, "x", new Dictionary<string,string>());
    var gltf = SharpGLTF.Schema2.ModelRoot.Load(r.GlbPath);
    Assert.Equal(1, gltf.LogicalMeshes.Count);
    Assert.Equal(3, gltf.LogicalMeshes[0].Primitives[0].GetVertexAccessor("POSITION").Count);
}
[Fact] public void Write_MultiChannelUv_KeepsChannels()
[Fact] public void Write_Skinned_UsesSkin()              // synthetic 1-bone skin; asserts gltf.Skins count == 1
[Fact] public void Write_BaseColorTexture_EmitsUri()      // textureUriMap entry appears in material channels
```

- [ ] **Step 2: Run to verify they fail** — `dotnet test ... --filter GltfWriter` → FAIL (`GlbWriter` missing).
- [ ] **Step 3: Implement `ModelData` exactly as pinned above; implement `GlbWriter` as a minimal GLB 2.0 binary writer** (JSON chunk + BIN chunk; positions/normals/UV0..n/indices; skin joints+IBM from `SkeletonData`; materials with base-color/normal/specular texture URIs and factors). No axis conversion in v1 (game and glTF are both Y-up; verified visually in Task 4).
- [ ] **Step 4: Run tests** — all pass.
- [ ] **Step 5: Commit** — `git add -A && git commit -m "feat: model DTOs + minimal glTF (.glb) writer"`

---

### Task 2: Asset resolver (case-insensitive, tree-mirrored)

**Files:**
- Create: `src/BpConvert.Core/Pipeline/AssetResolver.cs`, `tests/BpConvert.Tests/AssetResolverTests.cs`

**Interfaces:**
- Consumes: nothing.
- Produces:

```csharp
namespace BpConvert.Core.Pipeline;
public sealed class AssetResolver
{
    public AssetResolver(string gameRoot);                       // lazy case-insensitive index
    public string? Resolve(string reference, string relativeToFile);  // absolute path or null
    public int Count { get; }
}
```

- [ ] **Step 1: Failing tests**

```csharp
[Fact] public void Resolve_ExactRelativePath_Finds()
    // "models/shadow/characters/wander/_cmn/wander.cmsh" from root
[Fact] public void Resolve_WrongCase_Finds()                       // "Models/Shadow/..." → resolves (Review Focus #1)
[Fact] public void Resolve_RelativeToReferrer_WalksDots()
    // resolver called with relativeToFile = ".../wander.cmdl", reference = "../_cmn/x.cmat"
[Fact] public void Resolve_Missing_ReturnsNull()
```

- [ ] **Step 2: Verify fail.**  
- [ ] **Step 3: Implement** — build a lowercase→actual-path dictionary over the tree once (130k files; ~1–2 s); `Resolve` normalizes separators, joins `relativeToFile`'s directory when the reference is relative, and looks up case-insensitively.
- [ ] **Step 4: Pass + commit** — `git commit -m "feat: case-insensitive game-tree asset resolver"`

---

### Task 3: Texture converter (`ctxr → png`)

**Files:**
- Create: `src/BpConvert.Core/Textures/CtxrConverter.cs`, `tests/BpConvert.Tests/CtxrConverterTests.cs`

**Interfaces:**
- Consumes: nothing. (Can run in parallel with Tasks 1–2.)
- Produces:

```csharp
namespace BpConvert.Core.Textures;
public static class CtxrConverter
{
    public static CtxrResult Convert(string ctxrAbsPath, string outPngAbsPath);
}
public sealed record CtxrResult(int Width, int Height, string Variant);  // "png" | "8c0a" | "cc0a" | "other"
```

- [ ] **Step 1: Failing tests (env-gated real samples; Review Focus #3)**

```csharp
[Fact] public void Convert_PngVariant_WritesImage()
    // misc/boot/_ps4/saveshadow_scee_tr.ctxr  (payload magic 89 50 4E 47)
    // assert: file exists, result.Variant == "png", Width > 0
[Fact] public void Convert_OodleVariant_WritesImage()
    // textures/ocean/flowmaps/_ps4/neutral_flowmap_nml.ctxr  (header 8C 0A)
    // assert: file exists, Variant == "8c0a", Width > 0, and bytes are not all-identical pixels
[Fact] public void Convert_UnknownVariant_ThrowsWithPath()   // synthesised 4-byte file
```

- [ ] **Step 2: Verify fail.**  
- [ ] **Step 3: Implement** — route by container header: PNG payload → strip to file; `8C 0A`/`CC 0A` → the library's decode path (per `docs/notes/aqua-core-api.md`); encode w/ alpha to PNG. **Oodle decision point:** if the library shells out to a Windows DLL for Mermaid blocks, integrate a portable decoder that builds from source on macOS arm64 with a permissive license (verify the license text first; e.g. powzix/ooz if its terms allow), pin its commit, and note the choice in the commit message; if no compliant decoder is found, **stop and escalate** (this is a spec-listed high risk).
- [ ] **Step 4: Find + record a `CC 0A` sample for the notes**

```bash
cd "$SOTC_GAME_ROOT" && for f in $(find . -name '*.ctxr' | head -2000); do head -c2 "$f" | xxd -p | grep -q '^cc0a' && echo "$f" && break; done
```

Record the found path in `docs/notes/aqua-core-api.md` (no test pinned: sample set not guaranteed stable in first 2000; implementer converts it once via a scratch run instead).

- [ ] **Step 5: Pass + commit** — `git commit -m "feat: ctxr → png converter (png/8c0a/cc0a variants)"`

---

### Task 4: Model converter v1 — static meshes, materials, textures

**Files:**
- Create: `src/BpConvert.Core/Aqua/AquaModelAdapter.cs`, `src/BpConvert.Core/Pipeline/ModelConverter.cs`, `tests/BpConvert.Tests/ModelConverterTests.cs`
- Modify: `docs/notes/aqua-core-api.md` (append adapter notes)

**Interfaces:**
- Consumes: `ModelData`/`MeshData`/`MaterialData` (Task 1), `GlbWriter.Write` (Task 1), `AssetResolver` (Task 2), `CtxrConverter.Convert` (Task 3).
- Produces:

```csharp
namespace BpConvert.Core.Aqua;
public sealed class AquaModelAdapter
{
    public AquaModelAdapter(AssetResolver resolver);
    public ModelData Load(string absolutePath);   // maps library types → DTOs per docs/notes/aqua-core-api.md
}

// BpConvert.Core.Pipeline
public static class ModelConverter
{
    public static ConvertResult Convert(string gameRoot, string sourceRelPath, string outLibDir);
}
public sealed record ConvertResult(string GlbPath, List<string> PngPaths, int MeshCount, long TotalVertices);
```

Output mapping (pinned): `models/shadow/characters/wander/_cmn/wander.cmsh` → `<outLib>/models/shadow/characters/wander/_cmn/wander.glb`; referenced `textures/.../x.ctxr` → `<outLib>/textures/.../x.png`.

- [ ] **Step 1: Failing tests**

```csharp
[Fact] public void Convert_Plank_ProducesLoadableGlb()
    // obj_plank_01.cmsh → SharpGLTF loads; LogicalMeshes >= 1; POSITION count > 0
[Fact] public void Convert_Wander_ProducesMeshAndTextureRefs()
    // wander.cmsh → meshes >= 1; result.PngPaths.Count >= 1; each png exists
[Fact] public void Convert_TwoSameBasenames_DistinctOutputs()      // Review Focus #2
    // synthetic fixture game root (temp dir): copy obj_plank_01.cmsh to a/x.cmsh and b/x.cmsh;
    // convert both; assert both glbs exist at their mirrored paths (a/x.glb, b/x.glb) and differ
```

- [ ] **Step 2: Verify fail.**
- [ ] **Step 3: Implement the adapter strictly from `docs/notes/aqua-core-api.md`** (it is the only file allowed to reference library types; map geometry, UV channels, normals, indices, and materials — resolving `.cmat` via `AssetResolver` — plus their texture refs; skeleton deferred to Task 5 — leave `MeshData.BoneIndices/Weights` empty and `Skeleton` null for now).
- [ ] **Step 4: Implement `ModelConverter`** — resolve refs via `AssetResolver`, convert textures via `CtxrConverter`, build `textureUriMap`, write via `GlbWriter`.
- [ ] **Step 5: Pass + commit** — `git commit -m "feat: model converter v1 (static meshes + materials + textures)"`

---

### Task 5: Skinning + skeletons

**Files:**
- Modify: `src/BpConvert.Core/Aqua/AquaModelAdapter.cs`, `tests/BpConvert.Tests/SkinningTests.cs`

**Interfaces:**
- Consumes: Task 1 (`SkeletonData`, `GlbWriter` skin support), Task 4 (adapter).
- Produces: adapter now fills `MeshData.BoneIndices/BoneWeights` and `ModelData.Skeleton`.

- [ ] **Step 1: Failing tests**

```csharp
[Fact] public void Wander_HasSkin_WithJoints()
    // gltf.Skins.Count == 1; Skin.Joints.Count >= 10; IBM accessor count == joints
[Fact] public void Wander_Weights_Normalized()
    // sample first 100 vertices: |sum(weights)-1| < 0.01
[Fact] public void Plank_StaysUnskinned()
```

- [ ] **Step 2: Verify fail.**
- [ ] **Step 3: Implement** — read `cskl` + weight streams via the library (extend the notes doc), map to DTOs; normalize weights defensively (sum ≠ 0.0? renormalize; all-zero → leave unskinned).
- [ ] **Step 4: Pass + commit** — `git commit -m "feat: skeleton + skinning conversion"`

---

### Task 6: Batch runner + catalog + CLI

**Files:**
- Create: `src/BpConvert.Core/Pipeline/Catalog.cs`, `scripts/convert-characters.sh`, `tests/BpConvert.Tests/CatalogTests.cs`
- Modify: `src/BpConvert.Cli/Program.cs`

**Interfaces:**
- Consumes: `ModelConverter.Convert` (Task 4).
- Produces (CLI surface for Task 7 + humans):

```
bp-convert inspect <file>
bp-convert convert --root <game> --out <lib> [--under <dir>]... [--ext cmsh,cmdl] [--jobs N] [--force]
bp-convert report  --out <lib>
```

Catalog `catalog.json` (pinned schema; written atomically as `catalog.json.tmp` + rename, flushed every 25 entries):

```json
{ "version": 1, "entries": {
  "<source rel path>": { "status": "ok|failed", "glb": "<rel path|null>",
    "pngs": ["<rel>..."], "meshes": 0, "vertices": 0, "error": null } } }
```

- [ ] **Step 1: Failing tests (Review Focus #5)**

```csharp
[Fact] public void Catalog_RoundTrips()
[Fact] public void Convert_SkipsCompletedOnRerun()      // fixture catalog with one ok entry → not reconverted
[Fact] public void Convert_Force_Reconverts()
[Fact] public void Convert_FailedFile_RecordedAndContinues()   // one corrupt file → status failed, others ok
[Fact] public void Convert_Jobs4_MatchesJobs1_EntrySet()        // same key set
[Fact] public void Convert_UnderAndExt_Filter()                 // --under models/objects/props --ext cmsh
```

- [ ] **Step 2: Verify fail.**
- [ ] **Step 3: Implement** the runner (`Parallel.ForEach` over the walk), catalog, filters, and `report` (counts per status + top errors).
- [ ] **Step 4: Pass + commit** — `git commit -m "feat: batch runner, resumable catalog, CLI convert/report"`

---

### Task 7: M0 acceptance run + verification sheet

**Files:**
- Create: `scripts/m0-acceptance.sh`, `docs/notes/m0-results.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: everything above.
- Produces: the M0 acceptance evidence (design §9: "Wander and ≥9 other characters convert reproducibly; catalog covers `characters/` + `models/`").

- [ ] **Step 1: Pre-flight on the pinned 10 characters** (wander + 9 others from `models/shadow/characters/`), assert each: glb loads via SharpGLTF, meshes ≥ 1, and record sizes.
- [ ] **Step 2: Largest-model check (Review Focus #4)** — convert the largest `*.cmsh` under `models/shadow/objects/actors/statues/colossus_01_minotaur_a/_cmn/`; assert it completes and the glb loads.
- [ ] **Step 3: Publish the CLI and launch the full batch** over `--under models --under characters --ext cmsh,cmdl` (≈46k files) in the background:

```bash
dotnet publish src/BpConvert.Cli -c Release -o out/bin
nohup ./out/bin/bp-convert convert --root "$SOTC_GAME_ROOT" --out out/library --under models --under characters --ext cmsh,cmdl --jobs 8 \
  > out/m0-run.log 2>&1 &
```

Resume-able; re-running continues where it stopped. Do not block on completion — the acceptance criterion is that the run is resumable and converges (verify with a second invocation: 0 new conversions once complete).
- [ ] **Step 4: Write `docs/notes/m0-results.md`** — success/failure counts, top-20 failure reasons with sample paths, the 10-character table (name, glb bytes, meshes, vertices), catalog path, and the exact commands. Record whether any failure rate exceeded 5% (design gate).
- [ ] **Step 5: Commit** — `git add -A && git commit -m "docs: M0 acceptance results + full-batch script"`

---

## Parallelism map (for the executing crew)

- T0 is the gate; everything waits on it.
- T1, T2, T3 are mutually independent → up to 3 agents in parallel.
- T4 needs T1+T2 (+T3 for textures); T5 needs T4; T6 needs T4; run T5 and T6 as two parallel agents after T4.
- T7 is the acceptance pass, single agent, then the background batch.
