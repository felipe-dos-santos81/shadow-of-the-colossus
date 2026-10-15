public class InspectTests
{
    // Compressed (Oodle 8C 0A) samples — require the vendored decoder (tools/native/NOTICE.md).
    [Fact]
    public void Inspect_RunsOnWanderCmsh()
    {
        var file = TestAssets.Sample("models/shadow/characters/wander/_cmn/wander.cmsh");
        Assert.True(File.Exists(file));
        AssertMagic(file, "8c0a");

        var summary = BpConvert.Core.Inspect.Summarize(file);

        Assert.True(summary.Parsed, summary.Error);
        Assert.Equal(12, summary.MeshCount);
    }

    [Fact]
    public void Inspect_RunsOnPlankCmsh()
    {
        var file = TestAssets.Sample("models/objects/props/_cmn/obj_plank_01.cmsh");
        Assert.True(File.Exists(file));
        AssertMagic(file, "8c0a");

        var summary = BpConvert.Core.Inspect.Summarize(file);

        Assert.True(summary.Parsed, summary.Error);
        Assert.Equal(1, summary.MeshCount);
    }

    [Fact]
    public void Inspect_RunsOnColossusCmsh()
    {
        // Largest *.cmsh in the directory (the brief's pinned colossus file name does not exist).
        var file = TestAssets.Sample(
            "models/shadow/objects/actors/statues/colossus_01_minotaur_a/_cmn/mino_a_statue_broke.cmsh");
        Assert.True(File.Exists(file));
        AssertMagic(file, "8c0a");

        var summary = BpConvert.Core.Inspect.Summarize(file);

        Assert.True(summary.Parsed, summary.Error);
        Assert.Equal(3, summary.MeshCount);
    }

    [Fact]
    public void Inspect_OodleCompressedCmdl_Parses()
    {
        var file = TestAssets.Sample(
            "models/shadow/objects/home/entrance_pillars_01/_cmn/entrance_pillar_01_b_7e761f48.cmdl");
        Assert.True(File.Exists(file));
        AssertMagic(file, "8c0a");

        var summary = BpConvert.Core.Inspect.Summarize(file);

        Assert.True(summary.Parsed, summary.Error);
    }

    [Fact]
    public void Inspect_OodleCompressedCmsh_8C06_Parses()
    {
        var file = TestAssets.Sample("models/test/mak/_cmn/rock_slide_01.cmsh");
        Assert.True(File.Exists(file));
        AssertMagic(file, "8c06");

        var summary = BpConvert.Core.Inspect.Summarize(file);

        Assert.True(summary.Parsed, summary.Error);
    }

    // Uncompressed samples (magic 00 02 / 00 05) — no Oodle involved.
    [Fact]
    public void Inspect_UncompressedCmsh()
    {
        var file = TestAssets.Sample("models/test/mthrush/_cmn/unit_uv_quad.cmsh");
        Assert.True(File.Exists(file));
        AssertMagic(file, "0002");

        var summary = BpConvert.Core.Inspect.Summarize(file);

        Assert.True(summary.Parsed, summary.Error);
        Assert.Equal(1, summary.MeshCount);
    }

    [Fact]
    public void Inspect_ReferenceMeshStub_ReportsNoGeometry()
    {
        var file = TestAssets.Sample("models/test/mthrush/_cmn/unit_uv_quad_400fbd6c.cmsh");
        Assert.True(File.Exists(file));
        AssertMagic(file, "0005");

        var summary = BpConvert.Core.Inspect.Summarize(file);

        // The library recognizes this variant but skips it by design: no geometry to report.
        Assert.True(summary.Parsed);
        Assert.Equal(0, summary.MeshCount);
        Assert.Contains("reference-mesh", summary.Error);
    }

    private static void AssertMagic(string file, string expectedHex)
    {
        using var stream = File.OpenRead(file);
        Span<byte> head = stackalloc byte[2];
        stream.ReadExactly(head);
        Assert.Equal(expectedHex, Convert.ToHexString(head).ToLowerInvariant());
    }
}
