using System.Diagnostics;

namespace BpConvert.Core.Pipeline;

/// <summary>
/// Batch knobs for <see cref="BatchRunner.Run"/> (mirrors the pinned CLI surface
/// <c>convert --root … --out … [--under …]… [--ext …] [--jobs N] [--force]</c>).
/// </summary>
public sealed record BatchOptions
{
    /// <summary>Game tree root (read-only).</summary>
    public required string GameRoot { get; init; }

    /// <summary>Output library root (the catalog lives here; outputs mirror the source tree below it).</summary>
    public required string OutLibDir { get; init; }

    /// <summary>Game-root-relative directories to walk; empty = the whole tree.</summary>
    public IReadOnlyList<string> Under { get; init; } = [];

    /// <summary>Source extensions to attempt (no dots, case-insensitive); default = the one v1 converts.</summary>
    public IReadOnlyList<string> Extensions { get; init; } = ["cmsh"];

    /// <summary>Conversion parallelism; must be ≥ 1.</summary>
    public int Jobs { get; init; } = Environment.ProcessorCount;

    /// <summary>Reconvert every in-scope source, including ones already recorded <c>ok</c>.</summary>
    public bool Force { get; init; }
}

/// <summary>Outcome of one <see cref="BatchRunner.Run"/>; <c>Skipped + Converted + Failed == InScope</c>.</summary>
/// <param name="InScope">Sources matched by the <c>--under</c>/<c>--ext</c> filters.</param>
/// <param name="Skipped">Recorded-ok sources left untouched (no <c>--force</c>).</param>
/// <param name="Converted">Sources converted ok in this run.</param>
/// <param name="Failed">Sources whose attempt failed in this run (recorded, batch continued).</param>
public sealed record BatchSummary(int InScope, int Skipped, int Converted, int Failed)
{
    /// <summary>Wall-clock time of the run, final catalog flush included.</summary>
    public TimeSpan Elapsed { get; init; }
}

/// <summary>
/// The resumable batch engine over a game tree (Task 6): walk the <c>--under</c> directories,
/// filter by <c>--ext</c>, convert with <paramref name="options"/>' parallelism, and record every
/// outcome in <see cref="Catalog"/> — atomically flushed every 25 completed conversions, so an
/// interrupted run resumes where it stopped.
/// </summary>
/// <remarks>
/// <para><b>Skip/retry policy</b> (Task 6 rulings). Entries recorded <c>ok</c> are skipped — their
/// outputs are assumed complete — unless <c>--force</c>, which reconverts everything in scope.
/// Entries recorded <c>failed</c> are <b>re-attempted on every run</b>: a failure is cheap to
/// retry, and a converter improvement must be able to pick failures up without a special flag.</para>
/// <para><b>Failure containment.</b> Per-file exceptions are caught and recorded as
/// <c>TypeName: message</c> (single line); they never abort the batch. The batch itself only
/// throws for unusable invocation (missing root/under, bad jobs/ext) or persistence failures —
/// a corrupt catalog deliberately fails loudly instead of masquerading as a first run.</para>
/// <para><b>Determinism.</b> The source list is fixed (and ordinally sorted) before the parallel
/// loop; every source produces exactly one entry, so <c>--jobs 1</c> and <c>--jobs N</c> yield the
/// same catalog key set — only completion order can differ.</para>
/// <para><b>Resume guarantees.</b> An entry is recorded only after its conversion returned, so an
/// <c>ok</c> entry always has complete outputs; a crash between writing outputs and recording the
/// entry simply reconverts that source next run (outputs are overwritten).</para>
/// </remarks>
public static class BatchRunner
{
    /// <summary>Pinned flush cadence: whole-catalog atomic rewrite every 25 completed conversions.</summary>
    private const int FlushEvery = 25;

    /// <summary>
    /// Runs the batch. <paramref name="log"/> (optional) receives one line per source — <c>ok</c>,
    /// <c>skip</c> or <c>fail</c>, with timing — from the worker threads.
    /// </summary>
    /// <exception cref="ArgumentException">Root/under directories missing or escaping the root, or bad ext/jobs.</exception>
    /// <exception cref="InvalidDataException">The existing catalog is unreadable or of a foreign version.</exception>
    public static BatchSummary Run(BatchOptions options, Action<string>? log = null)
    {
        ArgumentNullException.ThrowIfNull(options);

        var root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(options.GameRoot));
        var outLib = Path.TrimEndingDirectorySeparator(Path.GetFullPath(options.OutLibDir));
        if (!Directory.Exists(root))
        {
            throw new ArgumentException($"--root directory not found: {options.GameRoot}");
        }
        if (options.Jobs < 1)
        {
            throw new ArgumentException($"--jobs must be at least 1 (got {options.Jobs})");
        }

        var extensions = NormalizeExtensions(options.Extensions);
        var underDirs = ResolveUnderDirs(root, options.Under);

        var watch = Stopwatch.StartNew();
        var sources = EnumerateSources(root, underDirs, extensions, outLib);
        var data = Catalog.Load(outLib);

        int skipped = 0, converted = 0, failed = 0, completed = 0;
        Parallel.ForEach(sources, new ParallelOptions { MaxDegreeOfParallelism = options.Jobs }, source =>
        {
            if (!options.Force && data.TryGet(source, out var prior) && prior.Status == Catalog.StatusOk)
            {
                Interlocked.Increment(ref skipped);
                log?.Invoke($"skip  {source}");
                return;
            }

            var fileWatch = Stopwatch.StartNew();
            CatalogEntry entry;
            try
            {
                var result = ModelConverter.Convert(root, source, outLib);
                entry = CatalogEntry.Ok(RelativeTo(outLib, result.GlbPath),
                    result.PngPaths.Select(png => RelativeTo(outLib, png)).ToList(),
                    result.MeshCount, result.TotalVertices);
                Interlocked.Increment(ref converted);
                log?.Invoke($"ok    {source} -> {entry.Glb} ({fileWatch.ElapsedMilliseconds} ms)");
            }
            catch (Exception e)
            {
                entry = CatalogEntry.Failed(Describe(e));
                Interlocked.Increment(ref failed);
                log?.Invoke($"fail  {source}: {entry.Error} ({fileWatch.ElapsedMilliseconds} ms)");
            }

            data.Set(source, entry);
            if (Interlocked.Increment(ref completed) % FlushEvery == 0)
            {
                Catalog.Save(outLib, data);
            }
        });

        // Always leave a complete catalog behind — on a fully-skipped rerun too.
        Catalog.Save(outLib, data);
        return new BatchSummary(sources.Count, skipped, converted, failed) { Elapsed = watch.Elapsed };
    }

    /// <summary>
    /// In-scope sources: every file below the <c>--under</c> directories whose extension is in
    /// <c>--ext</c> (case-insensitive), deduplicated and ordinally sorted. Files below
    /// <paramref name="outLib"/> are never sources (the library may live inside the tree).
    /// </summary>
    private static List<string> EnumerateSources(string root, List<string> underDirs, HashSet<string> extensions, string outLib)
    {
        var sources = new List<string>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var dir in underDirs)
        {
            foreach (var file in Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories))
            {
                var fullPath = Path.GetFullPath(file);
                if (IsBelow(fullPath, outLib))
                {
                    continue;
                }
                var extension = Path.GetExtension(file).TrimStart('.');
                if (!extensions.Contains(extension))
                {
                    continue;
                }
                var relative = Path.GetRelativePath(root, fullPath).Replace('\\', '/');
                if (seen.Add(relative))
                {
                    sources.Add(relative);
                }
            }
        }
        sources.Sort(StringComparer.Ordinal);
        return sources;
    }

    /// <summary>Resolves and validates <c>--under</c> entries: each must exist below the root.</summary>
    private static List<string> ResolveUnderDirs(string root, IReadOnlyList<string> under)
    {
        var dirs = new List<string>();
        if (under.Count == 0)
        {
            dirs.Add(root);
            return dirs;
        }
        foreach (var raw in under)
        {
            if (string.IsNullOrWhiteSpace(raw))
            {
                throw new ArgumentException("--under contains an empty directory");
            }
            var candidate = Path.TrimEndingDirectorySeparator(Path.GetFullPath(Path.Combine(root, raw.Replace('\\', '/'))));
            if (!Directory.Exists(candidate))
            {
                throw new ArgumentException($"--under directory not found: {raw}");
            }
            if (candidate != root && !IsBelow(candidate, root))
            {
                throw new ArgumentException($"--under directory is outside --root: {raw}");
            }
            if (!dirs.Contains(candidate, StringComparer.Ordinal))
            {
                dirs.Add(candidate);
            }
        }
        return dirs;
    }

    /// <summary>Normalises <c>--ext</c> to a dotless, case-insensitive set; rejects empty tokens.</summary>
    private static HashSet<string> NormalizeExtensions(IReadOnlyList<string> extensions)
    {
        ArgumentNullException.ThrowIfNull(extensions);
        var set = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var raw in extensions)
        {
            var token = (raw ?? "").Trim().TrimStart('.');
            if (token.Length == 0)
            {
                throw new ArgumentException("--ext contains an empty extension");
            }
            set.Add(token);
        }
        if (set.Count == 0)
        {
            throw new ArgumentException("--ext must name at least one extension");
        }
        return set;
    }

    /// <summary>True when <paramref name="path"/> is a strict descendant of <paramref name="dir"/>.</summary>
    private static bool IsBelow(string path, string dir) =>
        path.Length > dir.Length
        && path.StartsWith(dir, StringComparison.Ordinal)
        && (path[dir.Length] == Path.DirectorySeparatorChar || path[dir.Length] == '/');

    /// <summary>Output-library-relative, '/'-separated spelling of a written asset.</summary>
    private static string RelativeTo(string outLib, string written) =>
        Path.GetRelativePath(outLib, written).Replace('\\', '/');

    /// <summary>One-line failure reason: exception type + message, whitespace runs collapsed.</summary>
    private static string Describe(Exception e) =>
        $"{e.GetType().Name}: {string.Join(' ', e.Message.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries))}";
}
