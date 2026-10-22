using System.Buffers.Binary;
using System.Numerics;
using System.Text.Json;
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
    public void Write_Skinned_JointNodesCarryBindPoseTransforms()
    {
        // A non-trivial bind pose: j0 is rotated and translated, j1 hangs 0.4 below it in j0's
        // own frame (row-vector composition). The inverse bind matrices are produced exactly as
        // the adapter produces them (the DTO's 16 floats are glTF's column-major listing).
        var world0 = Matrix4x4.CreateRotationZ(0.6f) * Matrix4x4.CreateTranslation(0.25f, 1f, 0f);
        var local1 = Matrix4x4.CreateTranslation(0f, 0.4f, 0f);
        var world1 = local1 * world0;
        var skeleton = new SkeletonData("sk", new()
        {
            new SkeletonJoint("j0", -1, InverseBindMatrix(world0)),
            new SkeletonJoint("j1", 0, InverseBindMatrix(world1)),
        });
        var m = new ModelData("x.cmsh", new() { new MeshData("m", Positions, Normals, new[] { Uv0 }, Indices,
            new int[] { 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0 }, new float[] { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 }, -1) },
            new(), skeleton, new());

        var gltf = ModelRoot.Load(GlbWriter.Write(m, _dir, "bindpose", new Dictionary<string, string>()).GlbPath);

        // glTF: at bind pose joint.WorldMatrix × IBM = Identity — the definition of "renders
        // undeformed". SharpGLTF decodes both sides in System.Numerics convention, where the same
        // product holds (the column-vector identity transposes as a whole).
        var skin = Assert.Single(gltf.LogicalSkins);
        var ibm = skin.GetInverseBindMatricesAccessor()!.AsMatrix4x4Array();
        for (int i = 0; i < skin.JointsCount; i++)
            AssertBindPoseIdentity(skin.Joints[i].WorldMatrix * ibm[i]);

        // The nodes carry the bind pose itself (a root local is its world; a child's is its world
        // relative to its parent), so the emitted `matrix` values pin the derivation order too.
        AssertClose(world0, skin.Joints[0].LocalMatrix);
        AssertClose(local1, skin.Joints[1].LocalMatrix);
        AssertClose(world1, skin.Joints[1].WorldMatrix);
    }

    /// <summary>
    /// Inverse bind matrix in the DTO's float order (glTF column-major — i.e. the row-vector
    /// matrix's rows in order; see the adapter notes §11): <c>inverse(world)</c> for a bind-pose
    /// world transform.
    /// </summary>
    private static float[] InverseBindMatrix(Matrix4x4 world)
    {
        Assert.True(Matrix4x4.Invert(world, out var ibm));
        return new[]
        {
            ibm.M11, ibm.M12, ibm.M13, ibm.M14,
            ibm.M21, ibm.M22, ibm.M23, ibm.M24,
            ibm.M31, ibm.M32, ibm.M33, ibm.M34,
            ibm.M41, ibm.M42, ibm.M43, ibm.M44,
        };
    }

    /// <summary>Element-wise max-abs-diff ≤ 1e-3 (float32 accumulation through the glb).</summary>
    private static void AssertClose(Matrix4x4 expected, Matrix4x4 actual)
    {
        var residual = expected - actual;
        float max = 0;
        for (int row = 1; row <= 4; row++)
            for (int column = 1; column <= 4; column++)
                max = Math.Max(max, Math.Abs(Get(residual, row, column)));
        Assert.InRange(max, 0f, 1e-3f);
    }

    private static void AssertBindPoseIdentity(Matrix4x4 product) => AssertClose(Matrix4x4.Identity, product);

    private static float Get(Matrix4x4 m, int row, int column) => (row, column) switch
    {
        (1, 1) => m.M11,
        (1, 2) => m.M12,
        (1, 3) => m.M13,
        (1, 4) => m.M14,
        (2, 1) => m.M21,
        (2, 2) => m.M22,
        (2, 3) => m.M23,
        (2, 4) => m.M24,
        (3, 1) => m.M31,
        (3, 2) => m.M32,
        (3, 3) => m.M33,
        (3, 4) => m.M34,
        (4, 1) => m.M41,
        (4, 2) => m.M42,
        (4, 3) => m.M43,
        _ => m.M44,
    };

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

        // glTF's default metallicFactor is 1.0 (fully metallic in conformant loaders); the writer
        // must pin every material to dielectric (0) — the BluePoint pipeline has no metallic input.
        // Asserted through SharpGLTF (the same loader the acceptance checker uses): a defaulted
        // factor would read back as 1.0, so 0 proves the JSON carries it explicitly.
        var metallic = mat.FindChannel("MetallicRoughness")!.Value.Parameters.Single(p => p.Name == "MetallicFactor");
        Assert.Equal(0f, Convert.ToSingle(metallic.Value));
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

    // The veins layer is carried as `emissiveTexture`: the task requires the material JSON to hold
    // the index (and the image), not just a SharpGLTF channel that a factor could synthesize.
    [Fact]
    public void Write_EmissiveTexture_EmitsEmissiveTextureIndex()
    {
        Directory.CreateDirectory(Path.Combine(_dir, "tex"));
        File.WriteAllBytes(Path.Combine(_dir, "tex", "t_veins.png"), Convert.FromBase64String(OnePixelPngBase64));

        var material = new MaterialData("mat", null, null, null,
            new float[] { 1, 1, 1, 1 }, Array.Empty<float>(), "textures/t_veins.ctxr");
        var m = new ModelData("x.cmsh", new() { new MeshData("m", Positions, Normals, new[] { Uv0 }, Indices,
            Array.Empty<int>(), Array.Empty<float>(), 0) }, new() { material }, null, new());
        var map = new Dictionary<string, string> { ["textures/t_veins.ctxr"] = "tex/t_veins.png" };

        using var json = ReadGlbJson(GlbWriter.Write(m, _dir, "emissive", map).GlbPath);
        var written = json.RootElement.GetProperty("materials")[0];

        Assert.True(written.GetProperty("emissiveTexture").GetProperty("index").GetInt32() >= 0);
        Assert.Equal("tex/t_veins.png", json.RootElement.GetProperty("images")[0].GetProperty("uri").GetString());

        // SharpGLTF (the acceptance checker's loader) reads the same binding as the Emissive channel.
        var gltf = ModelRoot.Load(Path.Combine(_dir, "emissive.glb"));
        var emissive = gltf.LogicalMaterials[0].FindChannel("Emissive");
        Assert.NotNull(emissive);
        Assert.Equal("tex/t_veins.png", emissive!.Value.Texture!.PrimaryImage.Content.SourcePath);
    }

    [Fact]
    public void Write_NullEmissiveTexture_OmitsEmissiveTexture()
    {
        var material = new MaterialData("mat", null, null, null,
            new float[] { 1, 1, 1, 1 }, Array.Empty<float>());
        var m = new ModelData("x.cmsh", new() { new MeshData("m", Positions, Normals, new[] { Uv0 }, Indices,
            Array.Empty<int>(), Array.Empty<float>(), 0) }, new() { material }, null, new());

        using var json = ReadGlbJson(GlbWriter.Write(m, _dir, "plain", new Dictionary<string, string>()).GlbPath);
        var written = json.RootElement.GetProperty("materials")[0];

        Assert.False(written.TryGetProperty("emissiveTexture", out _));
        Assert.False(json.RootElement.TryGetProperty("images", out _));
    }

    /// <summary>The JSON chunk of a .glb (header 12 B, then a chunk length/type pair).</summary>
    private static JsonDocument ReadGlbJson(string path)
    {
        var bytes = File.ReadAllBytes(path);
        int jsonLength = BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(12));
        return JsonDocument.Parse(bytes.AsMemory(20, jsonLength));
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
