using System.Numerics;
using BpConvert.Core.Anim;

public class AnbReaderTests
{
    // Pinned shape of the flag-clear decode (docs/notes/re2-keys.md §1; the RE-2 T1
    // verify assertions). The game tree is read-only; TestAssets fails fast without
    // SOTC_GAME_ROOT instead of skipping.
    [Fact]
    public void Stand_clip_decodes_to_the_pinned_shape()
    {
        var clip = AnbReader.Read(TestAssets.Sample("cp3shadow/anim/player_a_stand.anb"));

        Assert.Equal("player_a_stand", clip.Name);
        Assert.Equal(76, clip.Frames);
        Assert.Equal(0.5, clip.Rate, 3);
        Assert.Equal(15.0, clip.Fps, 3);
        Assert.Equal(29, clip.Tracks.Count);
        Assert.Equal(19, clip.Tracks.Count(t => !t.IsStatic));
        Assert.Equal(10, clip.Tracks.Count(t => t.IsStatic));
        // 1,444 keyed quaternions + one quaternion for each of the 10 static tracks.
        Assert.Equal(1454, clip.Tracks.Sum(t => t.Keys.Count));
        Assert.All(clip.Tracks.Where(t => t.IsStatic), t =>
        {
            Assert.Single(t.Keys);
            Assert.Equal(1.0, Norm(t.Keys[0]), 4); // type 0 is a unit quaternion
        });
        Assert.Equal(76, clip.Root.Count);

        // Every adjacent-key pair on the keyed tracks is continuous (measured min 0.980021).
        foreach (var track in clip.Tracks.Where(t => !t.IsStatic))
        {
            for (var k = 1; k < track.Keys.Count; k++)
            {
                var dot = NormalizedDot(track.Keys[k - 1], track.Keys[k]);
                Assert.True(dot >= 0.98, $"track {track.Index} key {k}: normalized dot {dot:F6} < 0.98");
            }
        }
    }

    [Fact]
    public void Walk_and_run_decode_with_their_frame_counts()
    {
        var walk = AnbReader.Read(TestAssets.Sample("cp3shadow/anim/player_a_walk.anb"));
        var run = AnbReader.Read(TestAssets.Sample("cp3shadow/anim/player_a_run.anb"));

        Assert.Equal("player_a_walk", walk.Name);
        Assert.Equal(21, walk.Frames);
        Assert.Equal(0.5, walk.Rate, 3); // measured Q12 rate field
        Assert.Equal(15.0, walk.Fps, 3);

        Assert.Equal("player_a_run", run.Name);
        Assert.Equal(18, run.Frames);
        Assert.Equal(1.0, run.Rate, 3); // measured: zero Q12 field -> the 1.0 default
        Assert.Equal(30.0, run.Fps, 3);

        Assert.True(walk.Rate > 0);
        Assert.True(run.Rate > 0);
    }

    [Fact]
    public void Horse_kick_decodes()
    {
        var clip = AnbReader.Read(TestAssets.Sample("cp3shadow/anim/horse_a_add_horse_kick.anb"));

        Assert.Equal("horse_a_add_horse_kick", clip.Name);
        Assert.Equal(16, clip.Frames);
        Assert.Equal(0.5, clip.Rate, 3);
        Assert.Equal(16, clip.Root.Count); // the clip carries the root position series
    }

    [Fact]
    public void Wrong_magic_throws()
    {
        var path = Path.Combine(Path.GetTempPath(), $"bpconvert-anb-{Guid.NewGuid():N}.anb");
        try
        {
            File.WriteAllBytes(path, new byte[] { 0xDE, 0xAD, 0xBE, 0xEF });

            Assert.Throws<InvalidDataException>(() => AnbReader.Read(path));
        }
        finally
        {
            File.Delete(path);
        }
    }

    private static double NormalizedDot(Quaternion a, Quaternion b)
    {
        static double Dot(Quaternion p, Quaternion q) =>
            (double)p.X * q.X + (double)p.Y * q.Y + (double)p.Z * q.Z + (double)p.W * q.W;

        return Dot(a, b) / (Math.Sqrt(Dot(a, a)) * Math.Sqrt(Dot(b, b)));
    }

    private static double Norm(Quaternion q) =>
        Math.Sqrt((double)q.X * q.X + (double)q.Y * q.Y + (double)q.Z * q.Z + (double)q.W * q.W);
}
