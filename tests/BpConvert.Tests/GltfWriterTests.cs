using System.Numerics;
using BpConvert.Core.Gltf;
using BpConvert.Core.Model;
using SharpGLTF.Schema2;

public class GltfWriterTests : IDisposable
{
    // xunit creates one test-class instance per test, so each test gets a private temp directory.
    private readonly string _dir = Directory.CreateTempSubdirectory("bpconvert-gltf-").FullName;

    // 1x1 opaque PNG. SharpGLTF reads files referenced by image `uri` at load time, so texture
    // tests must place a real (valid) image at the mapped relative path.
    private const string OnePixelPngBase64 =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==";

    private static readonly float[] Positions = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    private static readonly float[] Normals = { 0, 0, 1, 0, 0, 1, 0, 0, 1 };
    private static readonly float[] Uv0 = { 0, 0, 1, 0, 0, 1 };
    private static readonly uint[] Indices = { 0, 1, 2 };

    public void Dispose() => Directory.Delete(_dir, recursive: true);

    [Fact]
    public void Write_SingleTriangle_RoundTrips()
    {
        var m = new ModelData("x.cmsh", new() { new MeshData("m", new float[] { 0, 0, 0, 1, 0, 0, 0, 1, 0 },
            new float[] { 0, 0, 1, 0, 0, 1, 0, 0, 1 }, new[] { new float[] { 0, 0, 1, 0, 0, 1 } }, new uint[] { 0, 1, 2 },
            Array.Empty<int>(), Array.Empty<float>(), -1) }, new(), null, new());
        var r = GlbWriter.Write(m, _dir, "x", new Dictionary<string, string>());

        var gltf = ModelRoot.Load(r.GlbPath);

        Assert.Single(gltf.LogicalMeshes);
        Assert.Equal(3, gltf.LogicalMeshes[0].Primitives[0].GetVertexAccessor("POSITION").Count);
        Assert.Equal(r.GlbPath, Path.Combine(_dir, "x.glb"));
        Assert.Equal(new FileInfo(r.GlbPath).Length, r.Bytes);
    }

    [Fact]
    public void Write_MultiChannelUv_KeepsChannels()
    {
        var uv1 = new float[] { 0.5f, 0.25f, 1f, 1f, 0.25f, 0.5f };
        var m = new ModelData("x.cmsh", new() { new MeshData("m", Positions, Normals, new[] { Uv0, uv1 }, Indices,
            Array.Empty<int>(), Array.Empty<float>(), -1) }, new(), null, new());

        var gltf = ModelRoot.Load(GlbWriter.Write(m, _dir, "uv", new Dictionary<string, string>()).GlbPath);

        var prim = gltf.LogicalMeshes[0].Primitives[0];
        Assert.Equal(2, prim.VertexAccessors.Keys.Count(k => k.StartsWith("TEXCOORD_", StringComparison.Ordinal)));

        // The second channel's coordinates must survive (not silently collapse into channel 0).
        var channel1 = prim.GetVertexAccessor("TEXCOORD_1").AsVector2Array();
        Assert.Equal(3, channel1.Count);
        Assert.Equal(uv1[0], channel1[0].X);
        Assert.Equal(uv1[1], channel1[0].Y);
        Assert.Equal(uv1[2], channel1[1].X);
        Assert.Equal(uv1[3], channel1[1].Y);
    }

    [Fact]
    public void Write_Skinned_UsesSkin()
    {
        var skeleton = new SkeletonData("sk", new()
        {
            new SkeletonJoint("j0", -1, new float[] { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }),
        });
        var boneIndices = new int[] { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        var boneWeights = new float[] { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
        var m = new ModelData("x.cmsh", new() { new MeshData("m", Positions, Normals, new[] { Uv0 }, Indices,
            boneIndices, boneWeights, -1) }, new(), skeleton, new());

        var gltf = ModelRoot.Load(GlbWriter.Write(m, _dir, "skin", new Dictionary<string, string>()).GlbPath);

        Assert.Single(gltf.LogicalSkins);
        Assert.Equal(1, gltf.LogicalSkins[0].JointsCount);
        Assert.Equal("j0", gltf.LogicalSkins[0].Joints[0].Name);

        var prim = gltf.LogicalMeshes[0].Primitives[0];
        Assert.Equal(3, prim.GetVertexAccessor("JOINTS_0").Count);
        Assert.Equal(3, prim.GetVertexAccessor("WEIGHTS_0").Count);

        // The mesh node must reference the skin for the file to be meaningful.
        var node = gltf.LogicalNodes.Single(n => n.Mesh != null);
        Assert.Equal(gltf.LogicalSkins[0], node.Skin);
    }

    [Fact]
    public void Write_BaseColorTexture_EmitsUri()
    {
        Directory.CreateDirectory(Path.Combine(_dir, "tex"));
        var png = Convert.FromBase64String(OnePixelPngBase64);
        File.WriteAllBytes(Path.Combine(_dir, "tex", "t_albedo.png"), png);
        File.WriteAllBytes(Path.Combine(_dir, "tex", "t_normal.png"), png);
        File.WriteAllBytes(Path.Combine(_dir, "tex", "t_spec.png"), png);

        var material = new MaterialData("mat", "textures/t_albedo.ctxr", "textures/t_normal.ctxr", "textures/t_spec.ctxr",
            new float[] { 1, 0.5f, 0.25f, 1 }, new float[] { 0, 0, 0 });
        var m = new ModelData("x.cmsh", new() { new MeshData("m", Positions, Normals, new[] { Uv0 }, Indices,
            Array.Empty<int>(), Array.Empty<float>(), 0) }, new() { material }, null, new());
        var map = new Dictionary<string, string>
        {
            ["textures/t_albedo.ctxr"] = "tex/t_albedo.png",
            ["textures/t_normal.ctxr"] = "tex/t_normal.png",
            ["textures/t_spec.ctxr"] = "tex/t_spec.png",
        };

        var gltf = ModelRoot.Load(GlbWriter.Write(m, _dir, "mat", map).GlbPath);

        var mat = gltf.LogicalMaterials[0];
        Assert.Equal("mat", mat.Name);

        // SharpGLTF resolves each emitted uri against the .glb's directory and reads the file,
        // so SourcePath proves the uri mapping per slot.
        var baseColor = mat.FindChannel("BaseColor")!.Value;
        Assert.Equal(new Vector4(1, 0.5f, 0.25f, 1), baseColor.Color);
        Assert.Equal("tex/t_albedo.png", baseColor.Texture.PrimaryImage.Content.SourcePath);

        Assert.Equal("tex/t_normal.png", mat.FindChannel("Normal")!.Value.Texture.PrimaryImage.Content.SourcePath);

        // The shipped SharpGLTF surfaces KHR_materials_specular's specularTexture as "SpecularFactor".
        Assert.Equal("tex/t_spec.png", mat.FindChannel("SpecularFactor")!.Value.Texture.PrimaryImage.Content.SourcePath);
    }

    [Fact]
    public void Write_TextureNotInMap_OmitsChannel()
    {
        var material = new MaterialData("mat", "textures/t_albedo.ctxr", null, null,
            new float[] { 1, 1, 1, 1 }, Array.Empty<float>());
        var m = new ModelData("x.cmsh", new() { new MeshData("m", Positions, Normals, new[] { Uv0 }, Indices,
            Array.Empty<int>(), Array.Empty<float>(), 0) }, new() { material }, null, new() { "textures/t_albedo.ctxr" });

        var gltf = ModelRoot.Load(GlbWriter.Write(m, _dir, "unmapped", new Dictionary<string, string>()).GlbPath);

        // A texture whose source path is not in the map is omitted (no dangling uri in the output).
        Assert.Empty(gltf.LogicalImages);
        Assert.Null(gltf.LogicalMaterials[0].FindChannel("BaseColor")!.Value.Texture);
    }

    [Fact]
    public void Write_EmptyModel_ProducesLoadableGlb()
    {
        // Reference-mesh cmsh variants carry no geometry; the writer must still emit a valid .glb.
        var m = new ModelData("stub.cmsh", new(), new(), null, new());

        var gltf = ModelRoot.Load(GlbWriter.Write(m, _dir, "empty", new Dictionary<string, string>()).GlbPath);

        Assert.Empty(gltf.LogicalMeshes);
        Assert.Empty(gltf.LogicalSkins);
    }
}
