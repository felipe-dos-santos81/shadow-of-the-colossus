using System.Numerics;
using BpConvert.Core.Anim;
using BpConvert.Core.Aqua;
using BpConvert.Core.Gltf;
using BpConvert.Core.Model;
using BpConvert.Core.Pipeline;
using SharpGLTF.Schema2;

/// <summary>
/// Task 3 pins: <see cref="GlbWriter"/> emits one glTF animation per <see cref="AnimClip"/>.
/// The stand pin runs the real decode chain (AnbReader + SkbReader + TrackMap) against wander's
/// real 348-joint skeleton and reloads the produced .glb with SharpGLTF (the
/// <see cref="SkinningTests"/> pattern); the no-animation pin keeps the M0 shape.
/// </summary>
public class AnimWriterTests : IDisposable
{
    private readonly string _dir = Directory.CreateTempSubdirectory("bpconvert-animwriter-").FullName;

    public void Dispose() => Directory.Delete(_dir, recursive: true);

    private const string WanderRel = "models/shadow/characters/wander/_cmn/wander.cmsh";
    private const string StandAnbRel = "cp3shadow/anim/player_a_stand.anb";
    private const string PlayerBankRel = "cp3shadow/skel/player_a.skb";

    [Fact]
    public void Stand_animation_reloads_with_pinned_shape()
    {
        // Real skeleton (the same 348-joint CSKL wander converts with), meshes dropped: this pin
        // is about the animation nodes, not about re-writing 62k vertices.
        var model = LoadWanderSkeleton();
        var clip = AnbReader.Read(TestAssets.Sample(StandAnbRel));
        var bank = SkbReader.Read(TestAssets.Sample(PlayerBankRel));
        var joints = model.Skeleton!.Joints.Select(joint => joint.Name).ToList();

        var map = TrackMap.Map(clip, bank, joints, out var unresolved);
        Assert.Equal(0, unresolved);
        for (var track = 0; track < map.Length; track++) clip.Tracks[track].JointIndex = map[track];
        model.Animations.Add(clip);

        var gltf = ModelRoot.Load(GlbWriter.Write(model, _dir, "stand", new Dictionary<string, string>()).GlbPath);

        // One animation, named after the clip's file stem.
        var animation = Assert.Single(gltf.LogicalAnimations);
        Assert.Equal("player_a_stand", animation.Name);

        // 29 rotation channels: every track, keyed and static. The 10 static tracks are constant
        // single-key LINEAR channels at t = 0; the 19 keyed tracks carry the clip's 76 frames.
        var rotations = animation.Channels.Where(channel => channel.TargetNodePath == PropertyPath.rotation).ToList();
        Assert.Equal(29, rotations.Count);
        var rotationKeys = rotations.Select(channel => channel.GetRotationSampler().GetLinearKeys().ToList()).ToList();
        Assert.Equal(10, rotationKeys.Count(keys => keys.Count == 1));
        Assert.All(rotationKeys.Where(keys => keys.Count > 1), keys => Assert.Equal(clip.Frames, keys.Count));

        // Statics sit at t = 0 (a single LINEAR key).
        for (var index = 0; index < rotations.Count; index++)
            if (rotationKeys[index].Count == 1) Assert.Equal(0f, rotationKeys[index][0].Key);

        // Pin the bridge direction, not just unit length: (x, -y, -z, w) on the decoded key.
        var track0 = clip.Tracks[0];
        Assert.False(track0.IsStatic);
        var raw = track0.Keys[0];
        var bridged = Quaternion.Normalize(AnimBridge.ToGlb(new Vector3(raw.X, raw.Y, raw.Z), raw.W));
        var track0Channel = Assert.Single(rotations, channel => channel.TargetNode.LogicalIndex == track0.JointIndex);
        AssertQuaternionClose(bridged, track0Channel.GetRotationSampler().GetLinearKeys().First().Value);

        // The root series is one translation channel on the joint named exactly "Bone_root"; x and
        // z are zeroed (the controller owns horizontal position), y is the raw game value.
        var translationChannel = Assert.Single(animation.Channels,
            channel => channel.TargetNodePath == PropertyPath.translation);
        Assert.Equal("Bone_root", translationChannel.TargetNode.Name);
        var translations = translationChannel.GetTranslationSampler().GetLinearKeys().ToList();
        Assert.Equal(clip.Frames, translations.Count);
        Assert.Equal(joints.IndexOf("Bone_root"), translationChannel.TargetNode.LogicalIndex);
        for (var key = 0; key < translations.Count; key++)
        {
            Assert.Equal(0f, translations[key].Value.X);
            Assert.Equal(0f, translations[key].Value.Z);
            Assert.InRange(Math.Abs(translations[key].Value.Y - clip.Root[key].Y), 0f, 1e-5f);
        }

        // Engine time model: i / fps for i = 0..frames-1; the clip lasts 75/15 = 5.0 s.
        Assert.InRange(Math.Abs(animation.Duration - 5f), 0f, 1e-6f);
        for (var key = 0; key < translations.Count; key++)
            Assert.InRange(Math.Abs(translations[key].Key - key / clip.Fps), 0f, 1e-6f);

        // LINEAR everywhere; every emitted rotation is unit length; the stand loop closes.
        foreach (var channel in animation.Channels)
        {
            if (channel.TargetNodePath == PropertyPath.rotation)
            {
                var sampler = channel.GetRotationSampler();
                Assert.Equal(AnimationInterpolationMode.LINEAR, sampler.InterpolationMode);
                foreach (var (_, value) in sampler.GetLinearKeys())
                    Assert.InRange(Math.Abs(value.Length() - 1f), 0f, 1e-5f);
            }
            else
            {
                Assert.Equal(AnimationInterpolationMode.LINEAR, channel.GetTranslationSampler().InterpolationMode);
            }
        }
        foreach (var keys in rotationKeys.Where(keys => keys.Count > 1))
            Assert.Equal(keys[0].Value, keys[^1].Value);

        // TRS scope: only the 29 rotation targets + the root translation target leave matrix form;
        // every other joint keeps its bind-pose matrix.
        var rootIndex = joints.IndexOf("Bone_root");
        var animated = clip.Tracks.Select(track => track.JointIndex).Append(rootIndex).ToHashSet();
        var jointNodes = gltf.LogicalNodes.Where(node => node.IsSkinJoint).ToList();
        Assert.Equal(joints.Count, jointNodes.Count);
        foreach (var node in jointNodes)
        {
            var isAnimated = animated.Contains(node.LogicalIndex);
            Assert.Equal(isAnimated, node.LocalTransform.IsSRT);
            Assert.Equal(!isAnimated, node.LocalTransform.IsMatrix);
        }
        Assert.Equal(30, jointNodes.Count(node => node.LocalTransform.IsSRT));
        Assert.Equal(joints.Count - 30, jointNodes.Count(node => node.LocalTransform.IsMatrix));

        // The TRS decomposition is lossless: the animated nodes still satisfy the bind-pose
        // identity joint.WorldMatrix × IBM = I (the SkinningTests acceptance).
        var skin = Assert.Single(gltf.LogicalSkins);
        var inverseBind = skin.GetInverseBindMatricesAccessor()!.AsMatrix4x4Array();
        foreach (var node in jointNodes.Where(node => node.LocalTransform.IsSRT))
        {
            var product = node.WorldMatrix * inverseBind[node.LogicalIndex];
            var maxResidual = MaxAbsDifference(product, Matrix4x4.Identity);
            Assert.InRange(maxResidual, 0f, 1e-3f);
        }
    }

    [Fact]
    public void No_animations_keeps_todays_output()
    {
        // M0/M1 pin: an empty Animations list changes nothing — no `animations` array and every
        // joint node keeps its bind-pose `matrix` (no TRS).
        var skeleton = IdentitySkeleton("j0", "j1");
        var model = new ModelData("x.cmsh", new(), new(), skeleton, new());

        var glbPath = GlbWriter.Write(model, _dir, "noanim", new Dictionary<string, string>()).GlbPath;
        var gltf = ModelRoot.Load(glbPath);

        Assert.Empty(gltf.LogicalAnimations);
        var jointNodes = gltf.LogicalNodes.Where(node => node.IsSkinJoint).ToList();
        Assert.Equal(2, jointNodes.Count);
        Assert.All(jointNodes, node => Assert.True(node.LocalTransform.IsMatrix));
        Assert.All(jointNodes, node => Assert.False(node.LocalTransform.IsSRT));

        var json = File.ReadAllText(glbPath, System.Text.Encoding.ASCII);
        Assert.DoesNotContain("\"animations\"", json);
        Assert.Contains("\"matrix\"", json);
    }

    [Fact]
    public void Unmapped_track_throws()
    {
        var model = WithClip(IdentitySkeleton("Bone_root", "Bone_A"), Clip("unmapped", 2,
            new AnimTrack { Index = 0, IsStatic = true, Keys = new List<Quaternion> { Quaternion.Identity } }));

        var error = Assert.Throws<InvalidDataException>(() => Write(model, "unmapped"));
        Assert.Contains("unmapped", error.Message);
        Assert.Contains("JointIndex", error.Message);
    }

    [Fact]
    public void Root_length_mismatch_throws()
    {
        var clip = Clip("bad_root", 3,
            new AnimTrack { Index = 0, IsStatic = true, Keys = new List<Quaternion> { Quaternion.Identity }, JointIndex = 0 });
        clip.Root.Add(Vector3.Zero); // 1 entry for 3 frames
        var model = WithClip(IdentitySkeleton("Bone_root"), clip);

        var error = Assert.Throws<InvalidDataException>(() => Write(model, "badroot"));
        Assert.Contains("bad_root", error.Message);
        Assert.Contains("root", error.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void Root_without_Bone_root_throws()
    {
        var clip = Clip("no_bone_root", 2,
            new AnimTrack { Index = 0, IsStatic = true, Keys = new List<Quaternion> { Quaternion.Identity }, JointIndex = 0 });
        clip.Root.Add(Vector3.Zero);
        clip.Root.Add(Vector3.Zero);
        var model = WithClip(IdentitySkeleton("Bone_A"), clip);

        var error = Assert.Throws<InvalidDataException>(() => Write(model, "norootbone"));
        Assert.Contains("no_bone_root", error.Message);
        Assert.Contains("Bone_root", error.Message);
    }

    /// <summary>Wander's real skeleton as the adapter builds it, without meshes/materials.</summary>
    private static ModelData LoadWanderSkeleton()
    {
        var model = new AquaModelAdapter(new AssetResolver(TestAssets.Root)).Load(TestAssets.Sample(WanderRel));
        Assert.NotNull(model.Skeleton);
        return model with { Meshes = new(), Materials = new(), ReferencedTexturePaths = new() };
    }

    private static AnimClip Clip(string name, int frames, params AnimTrack[] tracks) => new()
    {
        Name = name,
        Frames = frames,
        Rate = 0.5,
        Tracks = tracks.ToList(),
        Root = new List<Vector3>(),
    };

    private static ModelData WithClip(SkeletonData skeleton, AnimClip clip)
    {
        var model = new ModelData("x.cmsh", new(), new(), skeleton, new());
        model.Animations.Add(clip);
        return model;
    }

    private static SkeletonData IdentitySkeleton(params string[] names) => new("sk", names
        .Select((name, index) => new SkeletonJoint(name, index == 0 ? -1 : index - 1,
            new float[] { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }))
        .ToList());

    private string Write(ModelData model, string name) =>
        GlbWriter.Write(model, _dir, name, new Dictionary<string, string>()).GlbPath;

    private static void AssertQuaternionClose(Quaternion expected, Quaternion actual)
    {
        Assert.InRange(Math.Abs(expected.X - actual.X), 0f, 1e-6f);
        Assert.InRange(Math.Abs(expected.Y - actual.Y), 0f, 1e-6f);
        Assert.InRange(Math.Abs(expected.Z - actual.Z), 0f, 1e-6f);
        Assert.InRange(Math.Abs(expected.W - actual.W), 0f, 1e-6f);
    }

    /// <summary>Largest absolute element difference (float32 accumulation through the glb).</summary>
    private static float MaxAbsDifference(Matrix4x4 a, Matrix4x4 b)
    {
        var d = a - b;
        return new[]
        {
            d.M11, d.M12, d.M13, d.M14, d.M21, d.M22, d.M23, d.M24,
            d.M31, d.M32, d.M33, d.M34, d.M41, d.M42, d.M43, d.M44,
        }.Max(Math.Abs);
    }
}
