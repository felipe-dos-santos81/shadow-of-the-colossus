namespace BpConvert.Core.Pipeline;

/// <summary>
/// Tree-mirrored, case-insensitive resolver for game asset references (cmat/cskl/ctxr/...). The game
/// was authored on Windows, so references keep its casing; lookups go through a lowercase index and
/// never depend on the host volume's case behavior.
/// <para>
/// <see cref="Resolve"/> takes an already-substituted reference (Task 4 owns <c>$</c>-rooting and
/// <c>****</c> wildcard substitution) and:
/// </para>
/// <list type="bullet">
///   <item>normalizes both separator styles (<c>/</c> and <c>\</c>);</item>
///   <item>treats the reference as tree-relative — rooted at the game root — unless its first
///   segment is <c>.</c>/<c>..</c> or it contains a <c>..</c> segment, in which case the referencing
///   file's directory (from <c>relativeToFile</c>) is its base and <c>..</c> walks up;</item>
///   <item>returns the path as spelled on disk, preferring a byte-exact match when the tree holds two
///   spellings that differ only by case (possible on case-sensitive volumes);</item>
///   <item>returns <c>null</c> for every miss, including references that escape the game root; it
///   never throws for one.</item>
/// </list>
/// <para>
/// The index is built lazily on first use (one walk of the ~130,155-file tree; ≈0.34 s warm on the
/// dev machine) by the first <see cref="Resolve"/> or <see cref="Count"/> call; the constructor does
/// no I/O. The game root must exist — the first use throws <see cref="DirectoryNotFoundException"/>
/// otherwise.
/// </para>
/// </summary>
public sealed class AssetResolver
{
    private readonly string _gameRoot;
    private readonly Lock _gate = new();

    // Lowercase tree-relative key ('/' separators) -> absolute path on disk. _caseVariants is
    // populated only where several spellings fold to one key and is published before _index, so a
    // reader that observes a non-null _index also observes a consistent _caseVariants.
    private volatile Dictionary<string, string>? _index;
    private Dictionary<string, List<string>>? _caseVariants;

    public AssetResolver(string gameRoot)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(gameRoot);
        _gameRoot = Path.TrimEndingDirectorySeparator(Path.GetFullPath(gameRoot));
    }

    /// <summary>
    /// Number of files indexed over the game tree. Like <see cref="Resolve"/>, it builds the lazy
    /// index on first access, so it always reports the full tree.
    /// </summary>
    public int Count => Index.Count;

    /// <summary>
    /// Resolves <paramref name="reference"/> (a plain tree-relative path, either separator style) to
    /// the absolute path of the file on disk, or <c>null</c> when no such file exists.
    /// <paramref name="relativeToFile"/> is the path of the file that contained the reference; it
    /// supplies the base directory for references containing <c>..</c> segments.
    /// </summary>
    public string? Resolve(string reference, string relativeToFile)
    {
        if (string.IsNullOrEmpty(reference))
        {
            return null;
        }

        var relative = Normalize(reference, relativeToFile);
        if (relative is null)
        {
            return null;
        }

        var folded = relative.ToLowerInvariant();
        if (!Index.TryGetValue(folded, out var path))
        {
            return null;
        }

        if (_caseVariants is not null && _caseVariants.TryGetValue(folded, out var spellings))
        {
            foreach (var spelling in spellings)
            {
                if (string.Equals(RelativeToRoot(spelling), relative, StringComparison.Ordinal))
                {
                    return spelling;
                }
            }
        }

        return path;
    }

    private Dictionary<string, string> Index
    {
        get
        {
            var index = _index;
            if (index is null)
            {
                lock (_gate)
                {
                    index = _index ??= BuildIndex();
                }
            }
            return index;
        }
    }

    private Dictionary<string, string> BuildIndex()
    {
        var index = new Dictionary<string, string>(StringComparer.Ordinal);
        var variants = new Dictionary<string, List<string>>(StringComparer.Ordinal);

        foreach (var path in Directory.EnumerateFiles(_gameRoot, "*", SearchOption.AllDirectories))
        {
            var fullPath = Path.GetFullPath(path);
            var folded = RelativeToRoot(fullPath).ToLowerInvariant();
            if (index.TryAdd(folded, fullPath))
            {
                continue;
            }

            // Two spellings of one tree position (only possible on a case-sensitive volume): keep
            // both so Resolve can honour an exact-case reference; the first seen stays the fallback.
            if (!variants.TryGetValue(folded, out var spellings))
            {
                variants[folded] = spellings = new List<string> { index[folded] };
            }
            spellings.Add(fullPath);
        }

        _caseVariants = variants.Count == 0 ? null : variants;
        return index;
    }

    /// <summary>
    /// Maps a reference to a tree-relative, '/'-separated path, or <c>null</c> when it cannot be one
    /// (it escapes the game root, or carries characters the host path APIs reject).
    /// </summary>
    private string? Normalize(string reference, string relativeToFile)
    {
        var path = reference.Replace('\\', '/');

        // Convenience: a reference already holding an absolute path inside the tree is accepted as
        // such; a rooted game reference such as "/materials/x" (e.g. after stripping '$') is
        // tree-rooted too — TrimStart below reduces both to the tree-relative form.
        if (path.StartsWith(_gameRoot, StringComparison.Ordinal)
            && (path.Length == _gameRoot.Length || path[_gameRoot.Length] == '/'))
        {
            path = path[_gameRoot.Length..];
        }
        path = path.TrimStart('/');

        var segments = path.Split('/');
        var referrerRelative = segments[0] == ".";
        foreach (var segment in segments)
        {
            referrerRelative |= segment == "..";
        }

        try
        {
            var baseDir = _gameRoot;
            if (referrerRelative)
            {
                baseDir = Path.GetDirectoryName(ToFullPath(relativeToFile)) ?? _gameRoot;
            }

            var relative = RelativeToRoot(Path.GetFullPath(Path.Combine(baseDir, path)));
            return relative == ".." || relative.StartsWith("../", StringComparison.Ordinal) ? null : relative;
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return null; // A corrupt reference is a miss, not a crash.
        }
    }

    private string ToFullPath(string path) =>
        Path.IsPathRooted(path) ? Path.GetFullPath(path) : Path.GetFullPath(Path.Combine(_gameRoot, path));

    private string RelativeToRoot(string fullPath) => Path.GetRelativePath(_gameRoot, fullPath).Replace('\\', '/');
}
