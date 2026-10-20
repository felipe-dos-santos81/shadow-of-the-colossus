using System.Text.Json;
using BpConvert.Core.Anim;
using BpConvert.Core.Pipeline;

/// <summary>
/// The sidecar clip record written beside a converted <c>.glb</c> (S4.1): one entry per baked
/// clip, carrying what the app needs to select clips by name without hard-coding frame counts.
/// The record is written only for conversions that actually baked clips (ruling R2): an M0
/// batch run stays sidecar-free.
/// </summary>
public class ClipManifestTests
{
    [Fact]
    public void Write_emits_one_entry_per_clip()
    {
        var dir = Directory.CreateTempSubdirectory("bp-clips-");
        var glb = Path.Combine(dir.FullName, "wander.glb");
        var clips = new[]
        {
            new AnimClip { Name = "player_a_stand", Frames = 76, Rate = 0.5, Tracks = [], Root = [] },
            new AnimClip { Name = "player_a_atk1",  Frames = 12, Rate = 1.0, Tracks = [], Root = [] },
        };

        var path = ClipManifest.Write(glb, "models/shadow/characters/wander/_cmn/wander.cmsh", clips);

        Assert.Equal(Path.Combine(dir.FullName, "wander.clips.json"), path);
        using var doc = JsonDocument.Parse(File.ReadAllText(path));
        var root = doc.RootElement;
        Assert.Equal(1, root.GetProperty("version").GetInt32());
        Assert.Equal("models/shadow/characters/wander/_cmn/wander.cmsh", root.GetProperty("source").GetString());
        var list = root.GetProperty("clips");
        Assert.Equal(2, list.GetArrayLength());
        Assert.Equal("player_a_stand", list[0].GetProperty("name").GetString());
        Assert.Equal(76, list[0].GetProperty("frames").GetInt32());
        Assert.Equal(15.0, list[0].GetProperty("fps").GetDouble(), 3);
        Assert.Equal("player_a_atk1", list[1].GetProperty("name").GetString());
    }

    [Fact]
    public void Write_records_root_motion_and_deletes_a_stale_record()
    {
        var dir = Directory.CreateTempSubdirectory("bp-clips-");
        var glb = Path.Combine(dir.FullName, "wander.glb");
        var clips = new[] { new AnimClip { Name = "player_a_damage_h_fly1", Frames = 9, Rate = 0.5, Tracks = [], Root = [] } };
        var path = ClipManifest.Write(glb, "models/.../wander.cmsh", clips);
        using var doc = JsonDocument.Parse(File.ReadAllText(path));
        Assert.Equal("body-offset", doc.RootElement.GetProperty("clips")[0].GetProperty("rootMotion").GetString());

        ClipManifest.Delete(glb);
        Assert.False(File.Exists(path));
    }

    [Theory]
    // S4.2's prerequisite: every clip the two fight models bake carries a classification, so a
    // later task never has to guess how a root series relates to the ground.
    [InlineData("player_a_stand", "ground-height")]
    [InlineData("player_a_walk", "ground-height")]
    [InlineData("player_a_run", "ground-height")]
    [InlineData("player_a_grip_stand_crouch", "ground-height")]
    [InlineData("player_a_grip_loop", "ground-height")]
    [InlineData("player_a_grip_move_f1", "ground-height")]
    [InlineData("player_a_grip_move_f2", "ground-height")]
    [InlineData("player_a_grip_float_u_hi_attack", "ground-height")]
    [InlineData("player_a_atk1", "body-offset")]
    [InlineData("player_a_damage_m_fly2", "body-offset")]
    [InlineData("player_a_fall_damage_to_death", "ground-height")]
    [InlineData("player_a_damage_h_fly1", "body-offset")]
    [InlineData("player_a_damage_h_fly_end", "body-offset")]
    [InlineData("player_a_fall_damage_start", "ground-height")]
    [InlineData("player_a_fall_damage_end", "ground-height")]
    [InlineData("minotaur_a_stand", "body-offset")]
    [InlineData("minotaur_a_add_head_damage", "body-offset")]
    [InlineData("minotaur_a_add_ude_r_damage", "body-offset")]
    [InlineData("minotaur_a_add_mune_r_damage", "body-offset")]
    [InlineData("minotaur_a_death_front", "body-offset")]
    [InlineData("minotaur_a_atk_leg_l", "body-offset")]
    [InlineData("minotaur_a_atk_leg_r", "body-offset")]
    [InlineData("minotaur_a_atk_weapon2_a", "body-offset")]
    [InlineData("horse_a_add_horse_kick", "unclassified")]
    public void For_classifies_every_baked_clip(string name, string expected)
    {
        Assert.Equal(expected, ClipMotion.For(name));
    }

    [Fact]
    public void Convert_WithoutClips_DeletesAStaleClipRecord()
    {
        // The stale-delete path (S4.2): a re-conversion that bakes no clips must remove the
        // previous pair's sidecar, or the app would select clips the glb no longer carries.
        var lib = Directory.CreateTempSubdirectory("bp-clips-stale-").FullName;
        try
        {
            var clips = ClipSet.Resolve(TestAssets.Root, ["player_a_stand"]);
            var baked = ModelConverter.Convert(
                TestAssets.Root, "models/shadow/characters/wander/_cmn/wander.cmsh", lib, clips: clips);
            var sidecar = Path.ChangeExtension(baked.GlbPath, ".clips.json");
            Assert.True(File.Exists(sidecar), sidecar);

            var plain = ModelConverter.Convert(
                TestAssets.Root, "models/shadow/characters/wander/_cmn/wander.cmsh", lib);

            Assert.Equal(baked.GlbPath, plain.GlbPath);
            Assert.True(File.Exists(plain.GlbPath));
            Assert.False(File.Exists(sidecar), sidecar);
        }
        finally
        {
            Directory.Delete(lib, recursive: true);
        }
    }

    [Fact]
    public void Convert_WithoutClips_WritesNoClipRecord()
    {
        // Ruling R2: the sidecar exists only when the conversion baked at least one clip. Without
        // --clips the output is the M0 shape, and a 16k-file batch must not gain 16k empty records.
        var lib = Directory.CreateTempSubdirectory("bp-clips-noclip-").FullName;
        try
        {
            var result = ModelConverter.Convert(TestAssets.Root, "models/objects/props/_cmn/obj_plank_01.cmsh", lib);

            Assert.True(File.Exists(result.GlbPath));
            Assert.False(File.Exists(Path.ChangeExtension(result.GlbPath, ".clips.json")));
        }
        finally
        {
            Directory.Delete(lib, recursive: true);
        }
    }

    [Fact]
    public void Convert_WithClips_WritesTheRecordBesideTheGlb()
    {
        // The wiring: a conversion with a clip scope must leave its record next to the glb it
        // just wrote, carrying the clip's real decoded shape and the source spelling.
        var lib = Directory.CreateTempSubdirectory("bp-clips-wired-").FullName;
        try
        {
            var clips = ClipSet.Resolve(TestAssets.Root, ["player_a_stand"]);
            var result = ModelConverter.Convert(
                TestAssets.Root, "models/shadow/characters/wander/_cmn/wander.cmsh", lib, clips: clips);

            var path = Path.ChangeExtension(result.GlbPath, ".clips.json");
            Assert.True(File.Exists(path), path);

            using var doc = JsonDocument.Parse(File.ReadAllText(path));
            var root = doc.RootElement;
            Assert.Equal(1, root.GetProperty("version").GetInt32());
            Assert.Equal("models/shadow/characters/wander/_cmn/wander.cmsh", root.GetProperty("source").GetString());
            var entry = Assert.Single(root.GetProperty("clips").EnumerateArray());
            Assert.Equal("player_a_stand", entry.GetProperty("name").GetString());
            Assert.Equal(76, entry.GetProperty("frames").GetInt32());
            Assert.Equal(15.0, entry.GetProperty("fps").GetDouble(), 3);
            Assert.True(entry.GetProperty("tracks").GetInt32() > 0);
            Assert.True(entry.GetProperty("root").GetBoolean());
        }
        finally
        {
            Directory.Delete(lib, recursive: true);
        }
    }
}
