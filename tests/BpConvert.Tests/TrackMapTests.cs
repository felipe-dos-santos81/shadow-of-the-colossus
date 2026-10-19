using System.Numerics;
using BpConvert.Core.Anim;
using BpConvert.Core.Pipeline;
using SharpGLTF.Schema2;

/// <summary>
/// The <c>.anb</c> track → glb joint map (<c>tools/re/anb/pose_dump.py</c>'s linkage): track i is
/// the sibling <c>.skb</c>'s record i, its name is matched against the produced glb's joint names,
/// and an absent name is unresolved (-1). The glb side converts the pinned samples the same way
/// <see cref="SkinningTests"/> does and reloads them with SharpGLTF — never the local library.
/// </summary>
public class TrackMapTests : IDisposable
{
    private readonly string _dir = Directory.CreateTempSubdirectory("bpconvert-trackmap-").FullName;

    public void Dispose() => Directory.Delete(_dir, recursive: true);

    private const string WanderRel = "models/shadow/characters/wander/_cmn/wander.cmsh";
    private const string AgroRel = "models/shadow/characters/agro/_cmn/agro.cmsh";
    private const string ValusRel = "models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.cmsh";

    [Fact]
    public void Stand_tracks_all_resolve_into_wander_glb()
    {
        var clip = AnbReader.Read(TestAssets.Sample("cp3shadow/anim/player_a_stand.anb"));
        var bank = SkbReader.Read(TestAssets.Sample("cp3shadow/skel/player_a.skb"));
        var joints = JointNames(ConvertAndLoad(WanderRel));

        var map = TrackMap.Map(clip, bank, joints, out var unresolved);

        Assert.Equal(29, clip.Tracks.Count);
        Assert.Equal(clip.Tracks.Count, map.Length);
        Assert.Equal(0, unresolved);
        Assert.Equal(29, map.Count(index => index >= 0)); // 29/29
        Assert.All(map, index => Assert.InRange(index, 0, joints.Count - 1));

        // Track i is the bank's record i, in file order (pose_dump's mapping).
        Assert.Equal(Array.IndexOf(joints.ToArray(), "Bone_KOSI"), map[0]);
        Assert.Equal(Array.IndexOf(joints.ToArray(), "Bone_MOMO_L"), map[1]);
        Assert.Equal(Array.IndexOf(joints.ToArray(), "Bone_OYAYUBI_B_R"), map[28]);
    }

    [Fact]
    public void Horse_clip_resolves_into_agro_glb()
    {
        var clip = AnbReader.Read(TestAssets.Sample("cp3shadow/anim/horse_a_add_horse_kick.anb"));
        var bank = SkbReader.Read(TestAssets.Sample("cp3shadow/skel/horse_a.skb"));
        var joints = JointNames(ConvertAndLoad(AgroRel));

        var map = TrackMap.Map(clip, bank, joints, out var unresolved);

        Assert.Equal(30, clip.Tracks.Count);
        Assert.Equal(clip.Tracks.Count, map.Length);
        Assert.Equal(0, unresolved);
        Assert.Equal(30, map.Count(index => index >= 0));
        Assert.All(map, index => Assert.InRange(index, 0, joints.Count - 1));
        Assert.Equal(Array.IndexOf(joints.ToArray(), "Bone_KOSI"), map[0]);
        Assert.Equal(Array.IndexOf(joints.ToArray(), "Bone_MIMI_R"), map[29]);
    }

    [Fact]
    public void Minotaur_stand_tracks_all_resolve_into_valus_glb()
    {
        // S4's central mapping risk: Valus's 58-track idle against the converted 238-joint
        // colossus glb. The read-only recon emulated 58/58; this pin runs the real readers,
        // the real converter and a SharpGLTF reload of the result.
        var clip = AnbReader.Read(TestAssets.Sample("cp3shadow/anim/minotaur_a_stand.anb"));
        var bank = SkbReader.Read(TestAssets.Sample("cp3shadow/skel/minotaur_a.skb"));
        var joints = JointNames(ConvertAndLoad(ValusRel));

        var map = TrackMap.Map(clip, bank, joints, out var unresolved);

        Assert.Equal(238, joints.Count);
        Assert.Equal(58, clip.Tracks.Count);
        Assert.Equal(clip.Tracks.Count, map.Length);
        Assert.Equal(0, unresolved);
        Assert.Equal(58, map.Count(index => index >= 0));
        Assert.Equal(58, map.Distinct().Count()); // 58 tracks -> 58 distinct joints, no collision
        Assert.All(map, index => Assert.InRange(index, 0, joints.Count - 1));

        // Track i is the bank's record i, in file order (pose_dump's rule).
        Assert.Equal(Array.IndexOf(joints.ToArray(), "Bone_KOSI"), map[0]);
        Assert.Equal(Array.IndexOf(joints.ToArray(), "Bone_KAO"), map[12]);
        Assert.Equal(Array.IndexOf(joints.ToArray(), "Bone_OYA_C_R"), map[57]);

        // The clip's 13 type-0xD tracks (angle+axis records; indices measured by the RE
        // decode) must be part of the mapped set, not dropped as an unhandled variant.
        var type0D = new[] { 2, 6, 15, 21, 29, 30, 32, 33, 35, 36, 38, 39, 42 };
        Assert.Equal(13, type0D.Length);
        foreach (var track in type0D)
            Assert.Equal(Array.IndexOf(joints.ToArray(), bank.Records[track].Name), map[track]);
    }

    [Fact]
    public void Missing_names_and_short_banks_are_unresolved()
    {
        var clip = new AnimClip
        {
            Name = "synthetic",
            Frames = 1,
            Rate = 1.0,
            Tracks =
            [
                new AnimTrack { Index = 0, IsStatic = true, Keys = [Quaternion.Identity] },
                new AnimTrack { Index = 1, IsStatic = true, Keys = [Quaternion.Identity] },
                new AnimTrack { Index = 2, IsStatic = true, Keys = [Quaternion.Identity] },
            ],
            Root = [],
        };
        var bank = new SkbBank
        {
            Name = "synthetic",
            Records =
            [
                new SkbRecord { Name = "Bone_A", Parent = -1 },
                new SkbRecord { Name = "Bone_Missing", Parent = -1 },
            ],
        };

        var map = TrackMap.Map(clip, bank, new[] { "Bone_root", "Bone_A" }, out var unresolved);

        Assert.Equal(new[] { 1, -1, -1 }, map); // exact match; absent name and past-the-bank track
        Assert.Equal(2, unresolved);
    }

    private ModelRoot ConvertAndLoad(string relativePath) =>
        ModelRoot.Load(ModelConverter.Convert(TestAssets.Root, relativePath, Path.Combine(_dir, "lib")).GlbPath);

    private static IReadOnlyList<string> JointNames(ModelRoot gltf)
    {
        var skin = Assert.Single(gltf.LogicalSkins);
        return skin.Joints.Select(joint => joint.Name!).ToList();
    }
}
