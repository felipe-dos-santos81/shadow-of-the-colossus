using BpConvert.Core.Pipeline;

public class AssetResolverTests
{
    private const string WanderCmsh = "models/shadow/characters/wander/_cmn/wander.cmsh";
    private const string WanderCmdl = "models/shadow/characters/wander/_cmn/wander.cmdl";

    // The game tree is read-only and identical for every test; one lazily-built index is shared so
    // the tree walk happens once for the whole class. Count_ReportsIndexedFiles uses its own
    // instance on purpose (it pins Count's first-use build).
    private static readonly AssetResolver Resolver = new(TestAssets.Root);

    [Fact]
    public void Resolve_ExactRelativePath_Finds()
    {
        Assert.Equal(TestAssets.Sample(WanderCmsh), Resolver.Resolve(WanderCmsh, TestAssets.Sample(WanderCmdl)));
    }

    [Fact]
    public void Resolve_WrongCase_Finds()
    {
        // The game was authored on Windows, so references carry its casing. The lookup goes through
        // the resolver's own cache (must not depend on the host volume's case behavior) and returns
        // the path as it is spelled on disk.
        var resolved = Resolver.Resolve(
            "Models/Shadow/Characters/Wander/_cmn/Wander.CMSH", TestAssets.Sample(WanderCmdl));

        Assert.Equal(TestAssets.Sample(WanderCmsh), resolved);
        Assert.True(File.Exists(resolved));
    }

    [Fact]
    public void Resolve_RelativeToReferrer_WalksDots()
    {
        // Real pair: wander.cmdl references $/materials/shadow/characters/wander/****/wander_skin.cmat
        // (**** = the _ps4 platform variant). Expressed relative to the referencing file, the same
        // target walks five levels up out of models/ and back down into materials/.
        var resolved = Resolver.Resolve(
            "../../../../../materials/shadow/characters/wander/_ps4/wander_skin.cmat",
            TestAssets.Sample(WanderCmdl));

        Assert.Equal(TestAssets.Sample("materials/shadow/characters/wander/_ps4/wander_skin.cmat"), resolved);
        Assert.True(File.Exists(resolved));
    }

    [Fact]
    public void Resolve_Missing_ReturnsNull()
    {
        Assert.Null(Resolver.Resolve(
            "models/shadow/characters/wander/_cmn/no_such_asset.cmsh", TestAssets.Sample(WanderCmdl)));
    }

    [Fact]
    public void Resolve_BackslashSeparators_Finds()
    {
        // References may use either separator; the game data mixes both.
        var resolved = Resolver.Resolve(
            @"Models\Shadow\Characters\Wander\_cmn\Wander.CMSH", TestAssets.Sample(WanderCmdl));

        Assert.Equal(TestAssets.Sample(WanderCmsh), resolved);
    }

    [Fact]
    public void Resolve_EscapesRoot_ReturnsNull()
    {
        // Six ".." segments from models/.../ _cmn land above the game root: a miss, never an escape.
        Assert.Null(Resolver.Resolve(
            "../../../../../../materials/shadow/characters/wander/_ps4/wander_skin.cmat",
            TestAssets.Sample(WanderCmdl)));
    }

    [Fact]
    public void Count_ReportsIndexedFiles()
    {
        // 130,155 is the tree's file count (`find "$SOTC_GAME_ROOT" -type f | wc -l`); the index is
        // built lazily on first use, not in the constructor.
        var resolver = new AssetResolver(TestAssets.Root);

        Assert.Equal(130_155, resolver.Count);
    }

    [Fact]
    public void Count_SkipsAppleMetadataFiles()
    {
        // Finder drops .DS_Store beside real content; it is not game data and must not be indexed.
        var dir = Directory.CreateTempSubdirectory("bpconvert-resolver-").FullName;
        try
        {
            File.WriteAllText(Path.Combine(dir, "wander.cmsh"), "not a real model");
            File.WriteAllText(Path.Combine(dir, ".DS_Store"), "junk");

            Assert.Equal(1, new AssetResolver(dir).Count);
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }
}
