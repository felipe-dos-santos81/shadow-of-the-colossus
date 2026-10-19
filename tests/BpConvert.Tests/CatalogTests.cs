using System.Text.Json;
using BpConvert.Core.Pipeline;

/// <summary>
/// Task 6 pinned cases: the batch runner and its resumable catalog. Fixtures are private temp
/// directories; wherever a real conversion is needed the small plank model is copied from
/// <c>$SOTC_GAME_ROOT</c> through <see cref="TestAssets"/> (fails fast when the env var is unset —
/// never skips silently). The failure case is a 4-byte junk file: cheap, deterministic and
/// independent of library error behaviour. Deliberately no large real-tree conversions — those
/// live in the texture/model/skinning suites.
/// </summary>
public class CatalogTests : IDisposable
{
    private const string PlankRel = "models/objects/props/_cmn/obj_plank_01.cmsh";

    // xunit creates one test-class instance per test, so each test gets a private temp directory.
    private readonly string _dir = Directory.CreateTempSubdirectory("bpconvert-catalog-").FullName;

    public void Dispose() => Directory.Delete(_dir, recursive: true);

    private static string Under(string root, string rel) => Path.Combine(root, rel.Replace('/', Path.DirectorySeparatorChar));

    /// <summary>Copies the real plank model (18,747 bytes, one mesh, three chain textures).</summary>
    private static void CopyPlank(string root, string rel)
    {
        var target = Under(root, rel);
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        File.Copy(TestAssets.Sample(PlankRel), target);
    }

    /// <summary>Writes a 4-byte junk file the converter can never parse, whatever path it takes.</summary>
    private static void WriteCorrupt(string root, string rel, string content = "junk")
    {
        var target = Under(root, rel);
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        File.WriteAllText(target, content);
    }

    /// <summary>Copies one real clip and its sibling bank into the fixture's cp3shadow tree.</summary>
    private static void CopyClipAssets(string root)
    {
        foreach (var rel in new[] { "cp3shadow/anim/player_a_stand.anb", "cp3shadow/skel/player_a.skb" })
        {
            var target = Under(root, rel);
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(TestAssets.Sample(rel), target);
        }
    }

    [Fact]
    public void Catalog_RoundTrips()
    {
        var lib = Path.Combine(_dir, "lib");
        var data = new CatalogData();
        data.Set("models/objects/props/_cmn/obj_plank_01.cmsh",
            CatalogEntry.Ok("models/objects/props/_cmn/obj_plank_01.glb",
                new[]
                {
                    "textures/common/debris/_ps4/planks_arena01_col.png",
                    "textures/common/debris/_ps4/planks_arena01_nml.png",
                },
                meshes: 1, vertices: 980));
        data.Set("models/shadow/gone.cmsh",
            CatalogEntry.Failed("FileNotFoundException: model not found in the game tree: 'models/shadow/gone.cmsh'"));
        Catalog.Save(lib, data);

        // Atomic-write policy: the pinned tmp name is renamed away, never left behind.
        var catalogPath = Path.Combine(lib, Catalog.FileName);
        Assert.True(File.Exists(catalogPath));
        Assert.False(File.Exists(Path.Combine(lib, Catalog.TempFileName)));

        // The bytes on disk are the pinned schema, field for field.
        using (var document = JsonDocument.Parse(File.ReadAllText(catalogPath)))
        {
            var jsonRoot = document.RootElement;
            Assert.Equal(1, jsonRoot.GetProperty("version").GetInt32());
            var entries = jsonRoot.GetProperty("entries");
            Assert.Equal(2, entries.EnumerateObject().Count());

            var ok = entries.GetProperty("models/objects/props/_cmn/obj_plank_01.cmsh");
            Assert.Equal("ok", ok.GetProperty("status").GetString());
            Assert.Equal("models/objects/props/_cmn/obj_plank_01.glb", ok.GetProperty("glb").GetString());
            Assert.Equal(
                new[]
                {
                    "textures/common/debris/_ps4/planks_arena01_col.png",
                    "textures/common/debris/_ps4/planks_arena01_nml.png",
                },
                ok.GetProperty("pngs").EnumerateArray().Select(png => png.GetString()!));
            Assert.Equal(1, ok.GetProperty("meshes").GetInt32());
            Assert.Equal(980, ok.GetProperty("vertices").GetInt64());
            Assert.Equal(JsonValueKind.Null, ok.GetProperty("error").ValueKind);

            var failed = entries.GetProperty("models/shadow/gone.cmsh");
            Assert.Equal("failed", failed.GetProperty("status").GetString());
            Assert.Equal(JsonValueKind.Null, failed.GetProperty("glb").ValueKind);
            Assert.Empty(failed.GetProperty("pngs").EnumerateArray());
            Assert.Equal(0, failed.GetProperty("meshes").GetInt32());
            Assert.Equal(0, failed.GetProperty("vertices").GetInt64());
            Assert.StartsWith("FileNotFoundException: ", failed.GetProperty("error").GetString()!);
        }

        // ...and it loads back into the same entries.
        var loaded = Catalog.Load(lib);
        Assert.Equal(2, loaded.Count);
        Assert.Equal(
            new[] { "models/objects/props/_cmn/obj_plank_01.cmsh", "models/shadow/gone.cmsh" },
            loaded.Entries.Select(entry => entry.Key));

        Assert.True(loaded.TryGet("models/objects/props/_cmn/obj_plank_01.cmsh", out var plank));
        Assert.Equal("ok", plank.Status);
        Assert.Equal("models/objects/props/_cmn/obj_plank_01.glb", plank.Glb);
        Assert.Equal(
            new[]
            {
                "textures/common/debris/_ps4/planks_arena01_col.png",
                "textures/common/debris/_ps4/planks_arena01_nml.png",
            },
            plank.Pngs);
        Assert.Equal(1, plank.Meshes);
        Assert.Equal(980, plank.Vertices);
        Assert.Null(plank.Error);

        Assert.True(loaded.TryGet("models/shadow/gone.cmsh", out var gone));
        Assert.Equal("failed", gone.Status);
        Assert.Null(gone.Glb);
        Assert.Empty(gone.Pngs);
        Assert.StartsWith("FileNotFoundException: ", gone.Error!);
    }

    [Fact]
    public void Convert_SkipsCompletedOnRerun()
    {
        // The source exists but is junk, so any conversion attempt would fail loudly. The seeded
        // 'ok' entry surviving intact (placeholder metrics included) with no output on disk is
        // therefore proof the file was skipped, not reconverted.
        var root = Path.Combine(_dir, "game");
        WriteCorrupt(root, "characters/shadow/_cmn/hero.cmsh");

        var lib = Path.Combine(_dir, "lib");
        var seeded = new CatalogData();
        seeded.Set("characters/shadow/_cmn/hero.cmsh",
            CatalogEntry.Ok("characters/shadow/_cmn/hero.glb", new[] { "textures/hero_col.png" },
                meshes: 7, vertices: 4242));
        Catalog.Save(lib, seeded);

        var summary = BatchRunner.Run(new BatchOptions
        {
            GameRoot = root, OutLibDir = lib, Under = ["characters"], Extensions = ["cmsh"], Jobs = 1,
        });

        Assert.Equal(1, summary.InScope);
        Assert.Equal(1, summary.Skipped);
        Assert.Equal(0, summary.Converted);
        Assert.Equal(0, summary.Failed);

        Assert.True(Catalog.Load(lib).TryGet("characters/shadow/_cmn/hero.cmsh", out var entry));
        Assert.Equal("ok", entry.Status);
        Assert.Equal("characters/shadow/_cmn/hero.glb", entry.Glb);
        Assert.Equal(new[] { "textures/hero_col.png" }, entry.Pngs);
        Assert.Equal(7, entry.Meshes);
        Assert.Equal(4242, entry.Vertices);
        Assert.Null(entry.Error);
        Assert.False(File.Exists(Under(lib, "characters/shadow/_cmn/hero.glb")));
    }

    [Fact]
    public void Convert_Force_Reconverts()
    {
        var root = Path.Combine(_dir, "game");
        CopyPlank(root, PlankRel);

        var lib = Path.Combine(_dir, "lib");
        var seeded = new CatalogData();
        seeded.Set(PlankRel, CatalogEntry.Ok("stale/placeholder.glb", new[] { "stale.png" },
            meshes: 1234, vertices: 5678));
        Catalog.Save(lib, seeded);

        var summary = BatchRunner.Run(new BatchOptions
        {
            GameRoot = root, OutLibDir = lib, Under = ["models"], Extensions = ["cmsh"], Jobs = 1, Force = true,
        });

        Assert.Equal(1, summary.InScope);
        Assert.Equal(0, summary.Skipped);
        Assert.Equal(1, summary.Converted);

        Assert.True(Catalog.Load(lib).TryGet(PlankRel, out var entry));
        Assert.Equal("ok", entry.Status);
        Assert.Null(entry.Error);
        // The real conversion replaced the seeded placeholder metrics...
        Assert.NotEqual(1234, entry.Meshes);
        Assert.NotEqual(5678, entry.Vertices);
        Assert.True(entry.Meshes >= 1);
        Assert.True(entry.Vertices > 0);
        Assert.DoesNotContain("stale.png", entry.Pngs);
        // ...and wrote the mirrored glb.
        Assert.Equal("models/objects/props/_cmn/obj_plank_01.glb", entry.Glb);
        Assert.True(File.Exists(Under(lib, entry.Glb!)));
    }

    [Fact]
    public void Convert_ClipsOnAlreadyOkSource_PendsUntilForce()
    {
        // Ruling: a source already recorded ok without --force is never silently skipped when the
        // run carries --clips — it is reported as pending, with the way forward spelled out.
        var root = Path.Combine(_dir, "game");
        CopyPlank(root, PlankRel);
        CopyClipAssets(root);

        var lib = Path.Combine(_dir, "lib");
        var first = BatchRunner.Run(new BatchOptions
        {
            GameRoot = root, OutLibDir = lib, Under = ["models"], Extensions = ["cmsh"], Jobs = 1,
        });
        Assert.Equal(1, first.Converted);

        var log = new List<string>();
        var second = BatchRunner.Run(new BatchOptions
        {
            GameRoot = root, OutLibDir = lib, Under = ["models"], Extensions = ["cmsh"], Jobs = 1,
            Clips = ["player_a_stand"],
        }, log.Add);

        Assert.Equal(1, second.InScope);
        Assert.Equal(1, second.Pending);
        Assert.Equal(0, second.Skipped);
        Assert.Equal(0, second.Converted);
        Assert.Equal(0, second.Failed);
        Assert.Contains(log, line => line.Contains("recorded ok", StringComparison.Ordinal));
        Assert.Contains(log, line => line.Contains("re-convert with --force", StringComparison.Ordinal));

        // The ok entry (and its outputs) is untouched: nothing was re-baked without --force.
        Assert.True(Catalog.Load(lib).TryGet(PlankRel, out var entry));
        Assert.Equal("ok", entry.Status);
        Assert.Equal("models/objects/props/_cmn/obj_plank_01.glb", entry.Glb);
    }

    [Fact]
    public void Convert_UnknownClip_FailsFastNamingTheAnb()
    {
        var root = Path.Combine(_dir, "game");
        CopyPlank(root, PlankRel);

        var lib = Path.Combine(_dir, "lib");
        var expected = Path.Combine(root, "cp3shadow", "anim", "does_not_exist.anb");

        var error = Assert.Throws<ArgumentException>(() => BatchRunner.Run(new BatchOptions
        {
            GameRoot = root, OutLibDir = lib, Under = ["models"], Extensions = ["cmsh"], Jobs = 1,
            Clips = ["does_not_exist"],
        }));

        Assert.Contains("does_not_exist", error.Message);
        Assert.Contains(expected.Replace('\\', '/'), error.Message.Replace('\\', '/'));
        Assert.False(File.Exists(Catalog.FilePath(lib)));   // failed before any conversion/catalog write
    }

    [Fact]
    public void Convert_UnderFile_ScopesToOneSource()
    {
        // The demo conversion needs exactly one model out of a directory full of them; a file
        // passed to --under is that source and nothing else.
        var root = Path.Combine(_dir, "game");
        CopyPlank(root, PlankRel);
        WriteCorrupt(root, "models/objects/props/_cmn/other.cmsh");

        var lib = Path.Combine(_dir, "lib");
        var summary = BatchRunner.Run(new BatchOptions
        {
            GameRoot = root, OutLibDir = lib, Under = [PlankRel], Extensions = ["cmsh"], Jobs = 1,
        });

        Assert.Equal(1, summary.InScope);
        Assert.Equal(1, summary.Converted);
        Assert.Equal(0, summary.Failed);
        Assert.Equal(new[] { PlankRel }, Catalog.Load(lib).Entries.Select(entry => entry.Key));
        Assert.False(File.Exists(Under(lib, "models/objects/props/_cmn/other.glb")));
    }

    [Fact]
    public void Convert_UnderFile_FilteredOut_Throws()
    {
        // An explicit file scope is a deliberate one-model invocation: a filter that removes it
        // must fail the invocation loudly, never report "in scope: 0" and exit 0.
        var root = Path.Combine(_dir, "game");
        WriteCorrupt(root, "models/objects/props/_cmn/other.cmdl");

        var extensionError = Assert.Throws<ArgumentException>(() => BatchRunner.Run(new BatchOptions
        {
            GameRoot = root, OutLibDir = Path.Combine(_dir, "lib"),
            Under = ["models/objects/props/_cmn/other.cmdl"], Extensions = ["cmsh"], Jobs = 1,
        }));
        Assert.Contains("other.cmdl", extensionError.Message);
        Assert.Contains("--ext cmsh", extensionError.Message);

        // Same for a source below the output library (the library may live inside the tree).
        WriteCorrupt(root, "lib/inside.cmsh");
        var outputError = Assert.Throws<ArgumentException>(() => BatchRunner.Run(new BatchOptions
        {
            GameRoot = root, OutLibDir = Path.Combine(root, "lib"),
            Under = ["lib/inside.cmsh"], Extensions = ["cmsh"], Jobs = 1,
        }));
        Assert.Contains("inside.cmsh", outputError.Message);
        Assert.Contains("below --out", outputError.Message);
    }

    [Fact]
    public void Convert_FailedFile_RecordedAndContinues()
    {
        var root = Path.Combine(_dir, "game");
        CopyPlank(root, PlankRel);
        WriteCorrupt(root, "models/objects/props/_cmn/shredded.cmsh");

        var lib = Path.Combine(_dir, "lib");
        var summary = BatchRunner.Run(new BatchOptions
        {
            GameRoot = root, OutLibDir = lib, Under = ["models"], Extensions = ["cmsh"], Jobs = 1,
        });

        // The batch survived the corrupt file and converted the good one.
        Assert.Equal(2, summary.InScope);
        Assert.Equal(1, summary.Converted);
        Assert.Equal(1, summary.Failed);

        var entries = Catalog.Load(lib).Entries;
        var bad = entries.Single(entry => entry.Key.EndsWith("shredded.cmsh", StringComparison.Ordinal)).Value;
        Assert.Equal("failed", bad.Status);
        Assert.Null(bad.Glb);
        Assert.Empty(bad.Pngs);
        Assert.Equal(0, bad.Meshes);
        Assert.Equal(0, bad.Vertices);
        // Failures record the exception type and a single-line message.
        Assert.NotNull(bad.Error);
        Assert.Matches(@"^[A-Za-z0-9_.+]+Exception: .+", bad.Error!);
        Assert.DoesNotContain('\n', bad.Error!);

        var good = entries.Single(entry => entry.Key.EndsWith("obj_plank_01.cmsh", StringComparison.Ordinal)).Value;
        Assert.Equal("ok", good.Status);
        Assert.NotNull(good.Glb);
        Assert.True(File.Exists(Under(lib, good.Glb!)));
    }

    [Fact]
    public void Convert_Jobs4_MatchesJobs1_EntrySet()
    {
        var root = Path.Combine(_dir, "game");
        CopyPlank(root, PlankRel);
        WriteCorrupt(root, "models/objects/props/_cmn/bad_a.cmsh");
        WriteCorrupt(root, "models/objects/props/_cmn/bad_b.cmsh");
        WriteCorrupt(root, "models/shadow/characters/probe/bad_c.cmsh", "oops");

        var lib1 = Path.Combine(_dir, "lib-jobs1");
        var lib4 = Path.Combine(_dir, "lib-jobs4");
        var serial = BatchRunner.Run(new BatchOptions
        {
            GameRoot = root, OutLibDir = lib1, Under = ["models"], Extensions = ["cmsh"], Jobs = 1,
        });
        var parallel = BatchRunner.Run(new BatchOptions
        {
            GameRoot = root, OutLibDir = lib4, Under = ["models"], Extensions = ["cmsh"], Jobs = 4,
        });

        Assert.Equal(4, serial.InScope);
        Assert.Equal(4, parallel.InScope);
        var serialEntries = Catalog.Load(lib1).Entries;
        var parallelEntries = Catalog.Load(lib4).Entries;

        // Pinned determinism: the same key set whatever the degree of parallelism...
        Assert.Equal(
            serialEntries.Select(entry => entry.Key).OrderBy(key => key, StringComparer.Ordinal),
            parallelEntries.Select(entry => entry.Key).OrderBy(key => key, StringComparer.Ordinal));
        // ...and the same per-key outcome (a deterministic converter over identical inputs).
        Assert.Equal(
            serialEntries.ToDictionary(entry => entry.Key, entry => (entry.Value.Status, entry.Value.Error)),
            parallelEntries.ToDictionary(entry => entry.Key, entry => (entry.Value.Status, entry.Value.Error)));
        Assert.Equal(1, serialEntries.Count(entry => entry.Value.Status == "ok"));
        Assert.Equal(3, serialEntries.Count(entry => entry.Value.Status == "failed"));
    }

    [Fact]
    public void Convert_UnderAndExt_Filter()
    {
        var root = Path.Combine(_dir, "game");
        CopyPlank(root, "models/objects/props/_cmn/probe.cmsh");
        WriteCorrupt(root, "models/objects/props/_cmn/other.cmdl");     // inside --under, filtered by --ext
        WriteCorrupt(root, "models/shadow/characters/probe/bad.cmsh"); // filtered by --under

        var lib = Path.Combine(_dir, "lib");
        var summary = BatchRunner.Run(new BatchOptions
        {
            GameRoot = root, OutLibDir = lib, Under = ["models/objects/props"], Extensions = ["cmsh"], Jobs = 1,
        });

        Assert.Equal(1, summary.InScope);
        Assert.Equal(1, summary.Converted);
        Assert.Equal(0, summary.Failed);
        // Both excluded files exist on disk — they were filtered, not missing.
        Assert.True(File.Exists(Under(root, "models/objects/props/_cmn/other.cmdl")));
        Assert.True(File.Exists(Under(root, "models/shadow/characters/probe/bad.cmsh")));
        // The catalog holds exactly the one converted source...
        Assert.Equal(new[] { "models/objects/props/_cmn/probe.cmsh" },
            Catalog.Load(lib).Entries.Select(entry => entry.Key));
        // ...and its mirrored glb exists.
        Assert.True(File.Exists(Under(lib, "models/objects/props/_cmn/probe.glb")));
    }

    [Fact]
    public void Report_CountsStatuses_AndRanksFailures()
    {
        // The report Task 7 consumes: counts per status plus a top-N failure table with one
        // deterministic sample source each.
        var data = new CatalogData();
        data.Set("a/x.cmsh", CatalogEntry.Failed("EndOfStreamException: unexpected end of stream"));
        data.Set("b/y.cmsh", CatalogEntry.Failed("EndOfStreamException: unexpected end of stream"));
        data.Set("c/z.cmsh", CatalogEntry.Failed("InvalidDataException: bad magic 0x1234"));
        data.Set("d/ok.cmsh", CatalogEntry.Ok("d/ok.glb", [], meshes: 1, vertices: 10));

        var summary = Catalog.Summarize(data, topFailures: 1);

        Assert.Equal(4, summary.Total);
        Assert.Equal(
            new[] { new KeyValuePair<string, int>("ok", 1), new KeyValuePair<string, int>("failed", 3) },
            summary.StatusCounts);
        var top = Assert.Single(summary.TopFailures);
        Assert.Equal("EndOfStreamException: unexpected end of stream", top.Error);
        Assert.Equal(2, top.Count);
        Assert.Equal("a/x.cmsh", top.SampleSource);
    }
}
