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
