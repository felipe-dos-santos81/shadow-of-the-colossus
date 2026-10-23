using BpConvert.Core.Aqua;

/// <summary>
/// The cmsh → sibling <c>.cmdl</c> pick (aqua-core-api.md §10.1): exact stem, then a
/// <c>&lt;stem&gt;_&lt;8 hex&gt;</c> unique-id sibling, then the first <c>&lt;stem&gt;_*</c> in ordinal order.
/// </summary>
public class SiblingCmdlTests : IDisposable
{
    private readonly string _dir = Directory.CreateTempSubdirectory("bpconvert-cmdl-").FullName;

    public void Dispose() => Directory.Delete(_dir, recursive: true);

    private static string? Pick(string rel) =>
        Path.GetFileName(AquaModelAdapter.FindSiblingCmdl(TestAssets.Sample(rel)));

    [Fact]
    public void Skirt_PicksItsOwnHexSibling_NotSkirt02s()
    {
        // arena_01_entrance_skirt_02_6cb02344.cmdl sorts first but maps one material; the skirt's
        // own _ec63e197.cmdl maps all four (the S9 white slab).
        Assert.Equal("arena_01_entrance_skirt_ec63e197.cmdl",
            Pick("models/shadow/levels/arena_01_entrance/_cmn/arena_01_entrance_skirt.cmsh"));
    }

    [Fact]
    public void GroupOwner_KeepsFirstMatch()
    {
        // No exactly-8-hex sibling: the non-hex `_group` owners keep the ordinal first match.
        Assert.Equal("lion_a_statue_broke_group.cmdl",
            Pick("models/shadow/objects/actors/statues/colossus_11_lion/_cmn/lion_a_statue_broke.cmsh"));
        Assert.Equal("hall_08_water_group_38bd1fbf.cmdl",
            Pick("models/shadow/levels/hall_08/_cmn/hall_08_water.cmsh"));
    }

    [Fact]
    public void OnlyNonHexSiblings_KeepFirstMatch()
    {
        foreach (var name in new[] { "x.cmsh", "x_lod.cmdl", "x_group.cmdl", "x_1234567.cmdl", "y_abcdef01.cmdl" })
            File.WriteAllBytes(Path.Combine(_dir, name), []);

        Assert.Equal("x_1234567.cmdl", Path.GetFileName(AquaModelAdapter.FindSiblingCmdl(Path.Combine(_dir, "x.cmsh"))));
    }

    private string PickIn(string cmsh, params string[] siblings)
    {
        foreach (var name in siblings.Append(cmsh))
            File.WriteAllBytes(Path.Combine(_dir, name), []);
        return Path.GetFileName(AquaModelAdapter.FindSiblingCmdl(Path.Combine(_dir, cmsh)))!;
    }

    [Fact]
    public void HexSibling_BeatsAnEarlierSortingNeighbour()
    {
        // x_02_cafebabe sorts before x_deadbeef but its suffix is not exactly 8 hex.
        Assert.Equal("x_deadbeef.cmdl", PickIn("x.cmsh", "x_02_cafebabe.cmdl", "x_deadbeef.cmdl"));
    }

    [Fact]
    public void NineHexSuffix_IsNotAUniqueId()
    {
        // 9 hex digits fail the exactly-8 rule: the earlier-sorting non-hex sibling (first match) wins.
        Assert.Equal("x_0_group.cmdl", PickIn("x.cmsh", "x_0_group.cmdl", "x_1deadbeef.cmdl"));
    }
}
