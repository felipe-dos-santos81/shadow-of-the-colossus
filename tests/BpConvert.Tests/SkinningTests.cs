using System.Numerics;
using BpConvert.Core.Pipeline;
using SharpGLTF.Schema2;

/// <summary>
/// Task 5 pinned cases: skeleton + skinning conversion. Wander is the skinned pin — its CMSH
/// carries bone data (<c>variantFlag &amp; 0x1</c>) and the CSKL sits beside it
/// (<c>wander.cskl</c>, 348 bones, SOTC footer version 0x9, so the inverse bind matrices are
/// derived with the reference's recipe); the plank is the unskinned regression pin (no bone data
/// at all). Real-tree cases are env-gated through <see cref="TestAssets"/> (fail fast when
/// <c>SOTC_GAME_ROOT</c> is unset — never skips silently).
/// </summary>
public class SkinningTests : IDisposable
{
    // xunit creates one test-class instance per test, so each test gets a private temp directory.
    private readonly string _dir = Directory.CreateTempSubdirectory("bpconvert-skin-").FullName;

    public void Dispose() => Directory.Delete(_dir, recursive: true);

    private const string WanderRel = "models/shadow/characters/wander/_cmn/wander.cmsh";
    private const string PlankRel = "models/objects/props/_cmn/obj_plank_01.cmsh";

    private static readonly string[] SkinnedAttributes = { "JOINTS_0", "WEIGHTS_0" };

    [Fact]
    public void Wander_HasSkin_WithJoints()
    {
        var gltf = ConvertAndLoad(WanderRel);

        // One skin for the whole model, indexing the CSKL's bone list (wander.cskl: 348 bones).
        var skin = Assert.Single(gltf.LogicalSkins);
        Assert.True(skin.JointsCount >= 10, $"expected >= 10 joints, got {skin.JointsCount}");
        Assert.Equal(348, skin.JointsCount);

        // ... and one inverse bind matrix per joint.
        var inverseBind = skin.GetInverseBindMatricesAccessor();
        Assert.NotNull(inverseBind);
        Assert.Equal(skin.JointsCount, inverseBind!.Count);

        // Every primitive of every sub-mesh carries skin attributes dimensioned by its own vertices.
        var primitives = gltf.LogicalMeshes.SelectMany(mesh => mesh.Primitives).ToList();
        Assert.NotEmpty(primitives);
        Assert.All(primitives, primitive =>
        {
            var vertexCount = primitive.GetVertexAccessor("POSITION").Count;
            Assert.Equal(vertexCount, primitive.GetVertexAccessor("JOINTS_0").Count);
            Assert.Equal(vertexCount, primitive.GetVertexAccessor("WEIGHTS_0").Count);
        });

        // Joint names are the CSKL's pipe-separated paths' last segment; parents from the CSKL's
        // family ids (bone 1 is a child of bone 0, the root).
        Assert.Equal("Bone_root", skin.Joints[0].Name);
        Assert.All(skin.Joints, joint => Assert.False(string.IsNullOrEmpty(joint.Name)));
        Assert.Equal("Bone_root", skin.Joints[1].VisualParent.Name);

        // The joint-count checks above cannot see a transposed inverse bind matrix, so pin the
        // layout too: SharpGLTF decodes the accessor into System.Numerics convention (its own
        // conformant reader), where a translation sits in M41..M43. Wander's root transform is the
        // identity, so joint 0's IBM is the identity; bone 102 (Bone_EYELID_Bot_L) is moved off
        // the origin by its bind chain, so its IBM translation must match the derived value — a
        // transposed element order would zero it (or be rejected at load).
        var matrices = inverseBind.AsMatrix4x4Array();
        AssertTranslation(Vector3.Zero, matrices[0]);
        AssertTranslation(new Vector3(0.10657571f, -0.39097184f, 0.018784743f), matrices[102]);
    }

    [Fact]
    public void Wander_Weights_Normalized()
    {
        var gltf = ConvertAndLoad(WanderRel);
        var skinned = gltf.LogicalMeshes.SelectMany(mesh => mesh.Primitives)
            .Where(primitive => primitive.VertexAccessors.ContainsKey("WEIGHTS_0"))
            .ToList();
        Assert.NotEmpty(skinned);

        // The game stores weights as k/255, so four kept weights often sum to less than 1 (the
        // samples reach 0.988): the conversion must renormalize each vertex to ~1.
        foreach (var primitive in skinned)
        {
            var weights = primitive.GetVertexAccessor("WEIGHTS_0").AsVector4Array();
            Assert.True(weights.Count >= 100, $"expected >= 100 vertices, got {weights.Count}");
            for (int vertex = 0; vertex < 100; vertex++)
            {
                var sum = weights[vertex].X + weights[vertex].Y + weights[vertex].Z + weights[vertex].W;
                Assert.InRange(Math.Abs(sum - 1f), 0f, 0.01f);
            }
        }
    }

    [Fact]
    public void Plank_StaysUnskinned()
    {
        // obj_plank_01.cmsh has no bone data (variantFlag 0x0): exactly the Task 4 output — no
        // skeleton, no skin attributes, but still a full conversion.
        var gltf = ConvertAndLoad(PlankRel);

        Assert.NotEmpty(gltf.LogicalMeshes);
        Assert.Empty(gltf.LogicalSkins);
        Assert.All(gltf.LogicalMeshes.SelectMany(mesh => mesh.Primitives), primitive =>
            Assert.All(SkinnedAttributes, attribute => Assert.False(primitive.VertexAccessors.ContainsKey(attribute))));
    }

    /// <summary>Converts a pinned sample and loads the produced .glb with SharpGLTF.</summary>
    private ModelRoot ConvertAndLoad(string relativePath)
    {
        var result = ModelConverter.Convert(TestAssets.Root, relativePath, Path.Combine(_dir, "lib"));
        Assert.True(File.Exists(result.GlbPath), result.GlbPath);
        return ModelRoot.Load(result.GlbPath);
    }

    /// <summary>
    /// Translation of a matrix SharpGLTF decoded from the IBM accessor, within float noise (the
    /// values come from the same float operations on both sides).
    /// </summary>
    private static void AssertTranslation(Vector3 expected, Matrix4x4 actual)
    {
        Assert.InRange((actual.Translation - expected).Length(), 0f, 1e-3f);
    }
}
