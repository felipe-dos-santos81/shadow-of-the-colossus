using BpConvert.Core.Anim;
using BpConvert.Core.Pipeline;
using SharpGLTF.Schema2;

/// <summary>
/// Task 4 pinned cases: bare <c>.cmsh</c> → mirrored <c>.glb</c> + mirrored texture PNGs, chain
/// <c>cmsh.meshList[i].matName</c> → sibling <c>.cmdl</c> <c>GetCMATMaterialMap()</c> → <c>.cmat</c>
/// → <c>.ctxr</c> references. Real-tree cases are env-gated through <see cref="TestAssets"/>
/// (fails fast when <c>SOTC_GAME_ROOT</c> is unset — never skips silently).
/// </summary>
public class ModelConverterTests : IDisposable
{
    // xunit creates one test-class instance per test, so each test gets a private temp directory.
    private readonly string _dir = Directory.CreateTempSubdirectory("bpconvert-model-").FullName;

    public void Dispose() => Directory.Delete(_dir, recursive: true);

    private const string PlankRel = "models/objects/props/_cmn/obj_plank_01.cmsh";
    private const string WanderRel = "models/shadow/characters/wander/_cmn/wander.cmsh";
    private const string AgroRel = "models/shadow/characters/agro/_cmn/agro.cmsh";

    private static readonly string[] WanderClips = ["player_a_stand", "player_a_walk", "player_a_run"];

    private static string Mirror(string lib, string rel) =>
        Path.Combine(lib, rel.Replace('/', Path.DirectorySeparatorChar));

    private static string SourcePath(MaterialChannel? channel) =>
        channel!.Value.Texture.PrimaryImage.Content.SourcePath.Replace('\\', '/');

    [Fact]
    public void Convert_Plank_ProducesLoadableGlb()
    {
        var lib = Path.Combine(_dir, "lib");
        var result = ModelConverter.Convert(TestAssets.Root, PlankRel, lib);

        // Pinned output mapping: the source tree path is mirrored under the output library.
        Assert.Equal(Mirror(lib, "models/objects/props/_cmn/obj_plank_01.glb"), result.GlbPath);
        Assert.True(File.Exists(result.GlbPath));

        var gltf = ModelRoot.Load(result.GlbPath);
        Assert.True(gltf.LogicalMeshes.Count >= 1);
        var position = gltf.LogicalMeshes[0].Primitives[0].GetVertexAccessor("POSITION");
        Assert.True(position.Count > 0);
        Assert.Equal(position.Count, result.TotalVertices);
        Assert.Equal(gltf.LogicalMeshes.Count, result.MeshCount);

        // Normals come from the raw NRM0 stream SOTC ships (see the adapter notes): unit length,
        // and — the plank being a thin board — Y is the most common normal axis (a wrong bit order
        // in the decode would move that dominance elsewhere).
        var normals = gltf.LogicalMeshes[0].Primitives[0].GetVertexAccessor("NORMAL").AsVector3Array();
        Assert.Equal(position.Count, normals.Count);
        Assert.All(normals, normal => Assert.InRange(normal.Length(), 0.999f, 1.001f));
        int yFaces = normals.Count(normal => Math.Abs(normal.Y) > 0.9f);
        Assert.True(yFaces > normals.Count(normal => Math.Abs(normal.X) > 0.9f)
            && yFaces > normals.Count(normal => Math.Abs(normal.Z) > 0.9f)
            && yFaces > normals.Count * 0.4f, "Y should be the plank's most common normal axis");

        // Chain: obj_plank_01.cmsh -> sibling obj_plank_01_52f8cc91.cmdl ('bpShader1' ->
        // $/materials/common/debris/****/planks_arena01.cmat) -> the three .ctxr references.
        var material = Assert.Single(gltf.LogicalMaterials);
        Assert.Equal("bpShader1", material.Name);
        Assert.EndsWith("textures/common/debris/_ps4/planks_arena01_col.png", SourcePath(material.FindChannel("BaseColor")));
        Assert.EndsWith("textures/common/debris/_ps4/planks_arena01_nml.png", SourcePath(material.FindChannel("Normal")));
        // The image uri is relative to the .glb's own directory (4 levels up from _cmn/).
        Assert.StartsWith("../../../../textures/", SourcePath(material.FindChannel("BaseColor")));

        // Every resolved texture is emitted, one mirrored PNG per .ctxr, deduped.
        Assert.Equal(3, result.PngPaths.Count);
        Assert.Equal(result.PngPaths.Count, result.PngPaths.Distinct().Count());
        Assert.All(result.PngPaths, png => Assert.True(File.Exists(png), png));
        Assert.Contains(result.PngPaths, png => png.EndsWith("textures/common/debris/_ps4/planks_arena01_col.png"));
    }

    [Fact]
    public void Convert_Wander_ProducesMeshAndTextureRefs()
    {
        var lib = Path.Combine(_dir, "lib");
        var result = ModelConverter.Convert(TestAssets.Root, WanderRel, lib);

        Assert.Equal(Mirror(lib, "models/shadow/characters/wander/_cmn/wander.glb"), result.GlbPath);
        Assert.True(File.Exists(result.GlbPath));
        Assert.True(result.MeshCount >= 1);

        Assert.True(result.PngPaths.Count >= 1);
        Assert.Equal(result.PngPaths.Count, result.PngPaths.Distinct().Count());
        Assert.All(result.PngPaths, png => Assert.True(File.Exists(png), png));

        // SharpGLTF reads every image uri from disk relative to the .glb, so this load also proves
        // the uri mapping: the PNGs exist where the glb expects them. (They do by now.)
        var gltf = ModelRoot.Load(result.GlbPath);
        Assert.True(gltf.LogicalMeshes.Count >= 1);
        Assert.True(gltf.LogicalMeshes.Sum(m => m.Primitives.Sum(p => p.GetVertexAccessor("POSITION").Count)) > 0);
        // Every emitted vertex carries a normal (NRM0 decode) and its UV set(s).
        foreach (var primitive in gltf.LogicalMeshes.Select(m => m.Primitives[0]))
        {
            Assert.Equal(primitive.GetVertexAccessor("POSITION").Count, primitive.GetVertexAccessor("NORMAL").Count);
            Assert.True(primitive.VertexAccessors.ContainsKey("TEXCOORD_0"));
        }

        // Slot rule: '_col' wins the base-color slot, the plain '_nml' (not '_spec_nml') the normal slot.
        var skin = gltf.LogicalMaterials.Single(m => m.Name == "wander_body_skin");
        Assert.EndsWith("textures/shadow/characters/wander/_ps4/wander_skin_col.png", SourcePath(skin.FindChannel("BaseColor")));
        Assert.EndsWith("textures/shadow/characters/wander/_ps4/wander_skin_nml.png", SourcePath(skin.FindChannel("Normal")));

        // The hair has no plain normal map: its tangent-space normal is the documented fallback.
        var hair = gltf.LogicalMaterials.Single(m => m.Name == "wander_hair_03");
        Assert.EndsWith("textures/shadow/characters/wander/_ps4/wander_hair_02_tangent_nml.png", SourcePath(hair.FindChannel("Normal")));

        // Unmapped map types are still emitted as PNGs (e.g. the block-compressed roughness map).
        Assert.Contains(result.PngPaths, png => png.EndsWith("wander_skin_rgh_436a37c6.png"));
    }

    [Fact]
    public void Convert_WanderWithClips_EmitsTheThreeDemoAnimations()
    {
        // Task 4's demo pin: the clip scope is resolved under cp3shadow/anim, the sibling bank by
        // the probe.py rule, and every track maps into wander's real skeleton (unresolved == 0 —
        // otherwise the conversion throws instead of writing a partial clip).
        var lib = Path.Combine(_dir, "lib");
        var clips = ClipSet.Resolve(TestAssets.Root, WanderClips);

        var result = ModelConverter.Convert(TestAssets.Root, WanderRel, lib, clips: clips);

        var gltf = ModelRoot.Load(result.GlbPath);
        Assert.Equal(WanderClips, gltf.LogicalAnimations.Select(animation => animation.Name));

        // One rotation channel per .anb track, plus the in-place root translation series.
        for (var index = 0; index < WanderClips.Length; index++)
        {
            var animation = gltf.LogicalAnimations[index];
            var decoded = AnbReader.Read(TestAssets.Sample($"cp3shadow/anim/{WanderClips[index]}.anb"));
            Assert.Equal(decoded.Tracks.Count,
                animation.Channels.Count(channel => channel.TargetNodePath == PropertyPath.rotation));
            Assert.Contains(animation.Channels,
                channel => channel.TargetNodePath == PropertyPath.translation && channel.TargetNode.Name == "Bone_root");
        }
    }

    [Fact]
    public void Convert_AgroWithKick_EmitsOneFullyMappedAnimation()
    {
        var lib = Path.Combine(_dir, "lib");
        var clips = ClipSet.Resolve(TestAssets.Root, ["horse_a_add_horse_kick"]);

        var result = ModelConverter.Convert(TestAssets.Root, AgroRel, lib, clips: clips);

        var gltf = ModelRoot.Load(result.GlbPath);
        var animation = Assert.Single(gltf.LogicalAnimations);
        Assert.Equal("horse_a_add_horse_kick", animation.Name);

        // A rotation channel per track proves 0 unresolved tracks: a partial map would have
        // failed the conversion (the writer/mapper never drop channels).
        var decoded = AnbReader.Read(TestAssets.Sample("cp3shadow/anim/horse_a_add_horse_kick.anb"));
        Assert.Equal(decoded.Tracks.Count,
            animation.Channels.Count(channel => channel.TargetNodePath == PropertyPath.rotation));
    }

    [Fact]
    public void Convert_UnmappableClip_FailsLoudlyAndWritesNothing()
    {
        // The plank has no skeleton: every track is unresolved. The conversion must fail with the
        // clip name, the unresolved count and a sample of the missing joint names — and must not
        // write a glb with the clip partially attached.
        var lib = Path.Combine(_dir, "lib");
        var clips = ClipSet.Resolve(TestAssets.Root, ["player_a_stand"]);

        var error = Assert.Throws<InvalidDataException>(
            () => ModelConverter.Convert(TestAssets.Root, PlankRel, lib, clips: clips));

        Assert.Contains("Clip 'player_a_stand'", error.Message);
        Assert.Contains("tracks unresolved", error.Message);
        Assert.Matches(@"\d+ of \d+ tracks unresolved", error.Message);
        Assert.Contains("Bone_", error.Message);
        Assert.Contains("refusing to attach a partially mapped clip", error.Message);
        Assert.False(File.Exists(Mirror(lib, "models/objects/props/_cmn/obj_plank_01.glb")));
    }

    [Fact]
    public void Convert_TwoSameBasenames_DistinctOutputs()
    {
        // Review Focus #2 — the same basename at two tree positions must stay addressable. Fixture
        // game root holds only the two .cmsh copies (no .cmdl sibling), which also exercises the
        // default-material fallback.
        var fixture = Path.Combine(_dir, "game");
        Directory.CreateDirectory(Path.Combine(fixture, "a"));
        Directory.CreateDirectory(Path.Combine(fixture, "b"));
        var source = TestAssets.Sample(PlankRel);
        File.Copy(source, Path.Combine(fixture, "a", "x.cmsh"));
        File.Copy(source, Path.Combine(fixture, "b", "x.cmsh"));

        var lib = Path.Combine(_dir, "lib");
        var notes = new List<string>();
        var a = ModelConverter.Convert(fixture, "a/x.cmsh", lib, notes.Add);
        var b = ModelConverter.Convert(fixture, "b/x.cmsh", lib, notes.Add);

        Assert.Equal(Mirror(lib, "a/x.glb"), a.GlbPath);
        Assert.Equal(Mirror(lib, "b/x.glb"), b.GlbPath);
        Assert.NotEqual(a.GlbPath, b.GlbPath);
        Assert.True(File.Exists(a.GlbPath));
        Assert.True(File.Exists(b.GlbPath));
        // A basename-keyed writer would have collapsed both into <lib>/x.glb.
        Assert.False(File.Exists(Path.Combine(lib, "x.glb")));

        // Both outputs are independently loadable conversions of the same source. (The bytes are
        // identical by construction — identical inputs, no path-dependent content — so the
        // distinctness under test is the mirrored location, not the payload; the byte-equality
        // assertion doubles as a determinism check.)
        var glbA = ModelRoot.Load(a.GlbPath);
        var glbB = ModelRoot.Load(b.GlbPath);
        Assert.Equal(glbA.LogicalMeshes[0].Primitives[0].GetVertexAccessor("POSITION").Count,
            glbB.LogicalMeshes[0].Primitives[0].GetVertexAccessor("POSITION").Count);
        Assert.Equal(File.ReadAllBytes(a.GlbPath), File.ReadAllBytes(b.GlbPath));

        // No .cmdl sibling -> single default material, no texture references — and the degradation
        // is recorded on the diagnostic sink (the brief's "degrade to a default material and record it").
        var material = Assert.Single(glbA.LogicalMaterials);
        Assert.Equal("default", material.Name);
        Assert.Empty(glbA.LogicalImages);
        Assert.Null(material.FindChannel("BaseColor")!.Value.Texture);
        Assert.Empty(a.PngPaths);
        Assert.Contains(notes, note => note.Contains("no sibling .cmdl beside x.cmsh", StringComparison.Ordinal));
        Assert.Contains(notes, note => note.Contains("single default material", StringComparison.Ordinal));
    }

    [Fact]
    public void Convert_RefusedTexture_SkipsItAndStillWrites()
    {
        // Fixture chain (mirrors the real $ references): the plank cmdl is found beside the cmsh,
        // the cmat under _cmn, and the base-colour texture slot is occupied by
        // hdr_calibration_image.ctxr — a container Task 3 permanently refuses (negative dimensions).
        // The other two referenced textures are absent on disk. The model must still convert.
        var fixture = Path.Combine(_dir, "game");
        var props = Path.Combine(fixture, "models/objects/props/_cmn");
        Directory.CreateDirectory(props);
        File.Copy(TestAssets.Sample(PlankRel), Path.Combine(props, "obj_plank_01.cmsh"));
        File.Copy(TestAssets.Sample("models/objects/props/_cmn/obj_plank_01_52f8cc91.cmdl"),
            Path.Combine(props, "obj_plank_01_52f8cc91.cmdl"));

        var matDir = Path.Combine(fixture, "materials/common/debris/_cmn");
        Directory.CreateDirectory(matDir);
        File.Copy(TestAssets.Sample("materials/common/debris/_ps4/planks_arena01.cmat"),
            Path.Combine(matDir, "planks_arena01.cmat"));

        var texDir = Path.Combine(fixture, "textures/common/debris/_ps4");
        Directory.CreateDirectory(texDir);
        File.Copy(TestAssets.Sample("textures/shadow/ui/_ps4/hdr_calibration_image.ctxr"),
            Path.Combine(texDir, "planks_arena01_col.ctxr"));

        var lib = Path.Combine(_dir, "lib");
        var notes = new List<string>();
        var result = ModelConverter.Convert(fixture, PlankRel, lib, notes.Add);

        Assert.True(File.Exists(result.GlbPath));
        Assert.Equal(1, result.MeshCount);
        Assert.Empty(result.PngPaths);   // the one referenced texture refused; nothing was written
        Assert.Contains(notes, note => note.Contains("texture skipped: textures/common/debris/_ps4/planks_arena01_col.ctxr", StringComparison.Ordinal));
        Assert.Contains(notes, note => note.Contains("unresolved texture reference", StringComparison.Ordinal));   // rgh/nml are absent

        var gltf = ModelRoot.Load(result.GlbPath);
        var material = Assert.Single(gltf.LogicalMaterials);
        Assert.Equal("bpShader1", material.Name);      // the chain resolved; only the pixels refused
        Assert.Empty(gltf.LogicalImages);
        Assert.Null(material.FindChannel("BaseColor")!.Value.Texture);
    }

    [Fact]
    public void Convert_MissingSource_Throws()
    {
        var gameRoot = Directory.CreateDirectory(Path.Combine(_dir, "empty")).FullName;
        Assert.Throws<FileNotFoundException>(() => ModelConverter.Convert(gameRoot, "nope/missing.cmsh", Path.Combine(_dir, "lib")));
    }

    [Fact]
    public void Core_LibraryTypes_StayInsideTheAquaFolder()
    {
        // The adapter is the only Task 4 file that may reference AquaModelLibrary types. Two
        // pre-existing wrappers predate the rule (Task 0's Inspect, Task 3's CtxrConverter) and are
        // allowlisted; everything else in src/ must stay library-free.
        var allowed = new[] { "BpConvert.Core/Inspect.cs", "BpConvert.Core/Textures/CtxrConverter.cs" };
        var src = Path.Combine(RepoRoot(), "src");

        var offenders = Directory.EnumerateFiles(src, "*.cs", SearchOption.AllDirectories)
            .Where(path => !path.Replace('\\', '/').Contains("/obj/"))
            .Where(path => File.ReadLines(path).Any(line => line.TrimStart().StartsWith("using AquaModelLibrary", StringComparison.Ordinal)))
            .Select(path => Path.GetRelativePath(src, path).Replace('\\', '/'))
            .Where(relative => !relative.StartsWith("BpConvert.Core/Aqua/", StringComparison.Ordinal))
            .Where(relative => !allowed.Contains(relative))
            .ToList();

        Assert.Empty(offenders);
    }

    /// <summary>Repository root, walking up from the test assembly's output directory.</summary>
    private static string RepoRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null && !Directory.Exists(Path.Combine(directory.FullName, "src", "BpConvert.Core")))
            directory = directory.Parent;
        Assert.NotNull(directory);
        return directory!.FullName;
    }
}
