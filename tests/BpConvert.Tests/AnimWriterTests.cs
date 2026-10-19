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

        // Pin the bridge direction, not just unit length: (-x, y, z, w) on the decoded key —
        // the conjugate of the RE tool's row-vector bridge, i.e. what a glTF loader's
        // column-vector node rotation needs (see AnimBridge.ToGlb).
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
    public void Bridge_negates_the_vector_x_and_keeps_y_z_w()
    {
        // The decoded quaternion is in the RE tool's row-vector convention; a glTF node
        // rotation is applied as a column-vector local transform, so the emitted value is
        // the inverse/conjugate of the tool's bridged quaternion: raw (x, y, z, w) becomes
        // (-x, y, z, w). Pinned on a raw value with four distinct components, so any sign
        // slip in the map is visible independently of the writer.
        var bridged = AnimBridge.ToGlb(new Vector3(0.1f, 0.2f, 0.3f), 0.4f);
        Assert.Equal(-0.1f, bridged.X);
        Assert.Equal(0.2f, bridged.Y);
        Assert.Equal(0.3f, bridged.Z);
        Assert.Equal(0.4f, bridged.W);
    }

    [Fact]
    public void Stand_rotation_keys_stay_inside_the_xval_bands_of_their_bind_locals()
    {
        // The semantic pose pin: emit the real stand clip, reload the glb, and compare
        // every rotation channel's key 0 with the target joint's bind local rotation (the
        // node's TRS before animation). The bands are the decode's own cross-validation
        // bands (docs/notes/re2-xval.md §4): the idle pose is within 40° of the bind on the
        // core chain, 60° on the arm chain, and the relaxed-hand finger statics sit inside
        // 30–110°.
        //
        // This is the pin Task 7's acceptance was missing. With the old bridge
        // (x, -y, -z, w) the same angles read ~156° on HARA and ~176° on HAND_A_L — a
        // vertical mirror of the pose — so the assertion catches the exact defect.
        var gltf = LoadStandGltf();
        var animation = Assert.Single(gltf.LogicalAnimations);

        var observed = new List<(string Category, string Name, double Angle)>();
        foreach (var channel in animation.Channels.Where(c => c.TargetNodePath == PropertyPath.rotation))
        {
            var name = channel.TargetNode.Name;
            var bind = BindLocal(channel.TargetNode).Rotation;
            var key0 = channel.GetRotationSampler().GetLinearKeys().First().Value;
            observed.Add((CategoryOf(name), name, QuaternionAngleDegrees(bind, key0)));
        }

        // The stand clip's 29 tracks: 13 core, 8 arm, 8 finger statics — no track may be
        // dropped or left unclassified.
        Assert.Equal(29, observed.Count);
        Assert.Equal(13, observed.Count(o => o.Category == "core"));
        Assert.Equal(8, observed.Count(o => o.Category == "arm"));
        Assert.Equal(8, observed.Count(o => o.Category == "finger"));

        // One aggregated assertion so a failing run lists every offending channel.
        var offenders = observed
            .Where(o => o.Angle < Band(o.Category).Lower || o.Angle > Band(o.Category).Upper)
            .Select(o => $"{o.Name} {o.Angle:F1}° (band {Band(o.Category).Lower}–{Band(o.Category).Upper}°)")
            .ToList();
        Assert.True(offenders.Count == 0,
            "rotation keys outside the xval band of their bind local: " + string.Join(", ", offenders));
    }

    [Fact]
    public void Stand_world_pose_puts_the_head_and_hands_at_the_decoded_heights()
    {
        // The world-pose pin: substitute key 0 of every channel (rotation keys, root
        // translation) into the reloaded node locals and compose down the tree, in glTF's
        // column-vector semantics (System.Numerics' row-vector matrices: world = local ×
        // parent world). The decoder's frame-0 world heights are Bone_KAO y 1.1165 and
        // Bone_HAND_A_L y 0.6269 (tools/re/anb/pose_dump.py pose). With the old bridge the
        // head reads y 0.3758 — below the pelvis at 0.7429 — which is Task 7 §5.1.
        var gltf = LoadStandGltf();
        var animation = Assert.Single(gltf.LogicalAnimations);

        var local = gltf.LogicalNodes
            .Where(node => node.IsSkinJoint)
            .ToDictionary(node => node, BindLocal);
        foreach (var channel in animation.Channels)
        {
            if (!local.TryGetValue(channel.TargetNode, out var current)) continue;
            if (channel.TargetNodePath == PropertyPath.rotation)
                current.Rotation = channel.GetRotationSampler().GetLinearKeys().First().Value;
            else if (channel.TargetNodePath == PropertyPath.translation)
                current.Translation = channel.GetTranslationSampler().GetLinearKeys().First().Value;
            local[channel.TargetNode] = current;
        }

        var world = new Dictionary<Node, Matrix4x4>();
        Matrix4x4 WorldOf(Node node)
        {
            if (world.TryGetValue(node, out var cached)) return cached;
            var (translation, rotation, scale) = local[node];
            var matrix = Matrix4x4.CreateScale(scale) * Matrix4x4.CreateFromQuaternion(rotation) *
                         Matrix4x4.CreateTranslation(translation);
            if (node.VisualParent != null) matrix *= WorldOf(node.VisualParent);
            world[node] = matrix;
            return matrix;
        }

        var byName = gltf.LogicalNodes.Where(node => node.IsSkinJoint).ToDictionary(node => node.Name);
        var head = WorldOf(byName["Bone_KAO"]);
        var leftHand = WorldOf(byName["Bone_HAND_A_L"]);
        Assert.InRange(head.Translation.Y, 1.1065f, 1.1265f);
        Assert.InRange(leftHand.Translation.Y, 0.6169f, 0.6369f);

        // The head must sit above the pelvis (the fold's signature is the reverse), and
        // the standard glTF evaluator must agree with the manual composition.
        var pelvis = WorldOf(byName["Bone_KOSI"]);
        Assert.True(head.Translation.Y > pelvis.Translation.Y + 0.3f);
        var evaluated = byName["Bone_KAO"].GetWorldMatrix(animation, 0f);
        Assert.InRange(Math.Abs(evaluated.Translation.Y - head.Translation.Y), 0f, 1e-3f);
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

    /// <summary>The stand clip through the real decode chain, written and reloaded as glTF.</summary>
    private ModelRoot LoadStandGltf()
    {
        var model = LoadWanderSkeleton();
        var clip = AnbReader.Read(TestAssets.Sample(StandAnbRel));
        var bank = SkbReader.Read(TestAssets.Sample(PlayerBankRel));
        var joints = model.Skeleton!.Joints.Select(joint => joint.Name).ToList();
        var map = TrackMap.Map(clip, bank, joints, out var unresolved);
        Assert.Equal(0, unresolved);
        for (var track = 0; track < map.Length; track++) clip.Tracks[track].JointIndex = map[track];
        model.Animations.Add(clip);
        return ModelRoot.Load(GlbWriter.Write(model, _dir, "stand-pose", new Dictionary<string, string>()).GlbPath);
    }

    /// <summary>A node's bind local TRS, from its SRT transform or its bind-pose matrix.</summary>
    private static (Vector3 Translation, Quaternion Rotation, Vector3 Scale) BindLocal(Node node)
    {
        var transform = node.LocalTransform;
        if (transform.IsSRT)
            return (transform.Translation, Quaternion.Normalize(transform.Rotation), transform.Scale);
        if (!Matrix4x4.Decompose(transform.Matrix, out var scale, out var rotation, out var translation))
            throw new InvalidOperationException($"Node '{node.Name}': the bind matrix does not decompose.");
        return (translation, Quaternion.Normalize(rotation), scale);
    }

    /// <summary>The track-name family and the xval band its bind-local residual must meet.</summary>
    private static string CategoryOf(string name)
    {
        // The glb's joint names carry the "Bone_" prefix the brief's xval families omit.
        var bone = name.StartsWith("Bone_", StringComparison.Ordinal) ? name["Bone_".Length..] : name;
        if (bone.StartsWith("YUBI_", StringComparison.Ordinal) ||
            bone.StartsWith("OYAYUBI_", StringComparison.Ordinal))
            return "finger";
        if (bone.StartsWith("KATA_", StringComparison.Ordinal) ||
            bone.StartsWith("JOWA_", StringComparison.Ordinal) ||
            bone.StartsWith("UDE_", StringComparison.Ordinal) ||
            bone.StartsWith("HAND_", StringComparison.Ordinal))
            return "arm";
        if (bone is "KOSI" or "HARA" or "MUNE" or "KUBI" or "KAO" ||
            bone.StartsWith("MOMO_", StringComparison.Ordinal) ||
            bone.StartsWith("SUNE_", StringComparison.Ordinal) ||
            bone.StartsWith("TOE_", StringComparison.Ordinal) ||
            bone.StartsWith("TUMASAKI_", StringComparison.Ordinal))
            return "core";
        throw new InvalidOperationException($"Track '{name}' is outside the xval's core/arm/finger families.");
    }

    private static (double Lower, double Upper) Band(string category) => category switch
    {
        "core" => (0.0, 40.0),
        "arm" => (0.0, 60.0),
        "finger" => (30.0, 110.0),
        _ => throw new ArgumentOutOfRangeException(nameof(category)),
    };

    /// <summary>
    /// The rotation angle between two unit quaternions, in degrees, taking q and −q as the
    /// same rotation (the sign-insensitive glTF reading).
    /// </summary>
    private static double QuaternionAngleDegrees(Quaternion a, Quaternion b)
    {
        var dot = Math.Abs(Quaternion.Dot(Quaternion.Normalize(a), Quaternion.Normalize(b)));
        return 2.0 * Math.Acos(Math.Clamp(dot, 0.0, 1.0)) * 180.0 / Math.PI;
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
