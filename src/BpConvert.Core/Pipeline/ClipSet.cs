using BpConvert.Core.Anim;

namespace BpConvert.Core.Pipeline;

/// <summary>One <c>--clips</c> entry resolved against the game tree: the clip and its bank.</summary>
/// <param name="Name">Clip stem as passed on the command line, e.g. <c>player_a_stand</c>.</param>
/// <param name="AnbPath">Absolute path of the clip (<c>&lt;game&gt;/cp3shadow/anim/&lt;name&gt;.anb</c>).</param>
/// <param name="BankPath">Absolute path of the sibling part bank, by the <c>probe.py</c> rule.</param>
public sealed record ClipSource(string Name, string AnbPath, string BankPath);

/// <summary>
/// The <c>--clips</c> scope: turns clip stems into concrete <c>.anb</c>/<c>.skb</c> pairs once per
/// batch, then loads a fresh pair per source (the track→joint map mutates the clip's tracks, so
/// clips are never shared between concurrent conversions).
/// </summary>
/// <remarks>
/// <para>
/// The clip lives at <c>&lt;game root&gt;/cp3shadow/anim/&lt;name&gt;.anb</c>. Its bank follows the
/// reference rule (<c>tools/re/anb/probe.py</c>'s <c>find_sibling_skb</c>): the sibling
/// <c>skel</c> directory of the clip's parent, holding the longest <c>.skb</c> stem that prefixes
/// the clip's (<c>clip == stem</c> or <c>clip starts with "stem_"</c>). No other mapping is
/// invented.
/// </para>
/// <para>
/// Resolution is a batch-level input: an unknown name (or a clip without a bank) is an invocation
/// error naming the expected file, raised before any source is converted. Mapping against a
/// model's joints is per source and loud (see <see cref="ModelConverter"/>).
/// </para>
/// </remarks>
public static class ClipSet
{
    /// <summary>Game-root-relative directory every <c>--clips</c> name resolves under.</summary>
    public const string AnimDirectory = "cp3shadow/anim";

    /// <summary>Game-root-relative bank directory (the clip's grandparent + <c>skel</c>).</summary>
    public const string SkeletonDirectory = "cp3shadow/skel";

    /// <summary>
    /// Resolves every clip name to its <c>.anb</c> and sibling <c>.skb</c>.
    /// </summary>
    /// <param name="gameRoot">Game tree root (read-only).</param>
    /// <param name="clipNames">Clip stems, in emission order; duplicates are dropped.</param>
    /// <exception cref="ArgumentException">
    /// A name is empty, carries a path separator, names no <c>.anb</c> (the message carries the
    /// expected path) or has no sibling bank.
    /// </exception>
    public static IReadOnlyList<ClipSource> Resolve(string gameRoot, IReadOnlyList<string> clipNames)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(gameRoot);
        ArgumentNullException.ThrowIfNull(clipNames);

        var root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(gameRoot));
        var resolver = new AssetResolver(root);   // lazily indexes the tree on first Resolve
        var sources = new List<ClipSource>(clipNames.Count);
        var seen = new HashSet<string>(StringComparer.Ordinal);

        foreach (var raw in clipNames)
        {
            var name = raw?.Trim() ?? "";
            if (name.Length == 0)
                throw new ArgumentException("--clips contains an empty clip name");

            // A clip is a bare file stem under the anim directory; a separator would let the name
            // escape it (and AssetResolver's `..` handling would silently honour that).
            if (name.IndexOfAny(['/', '\\']) >= 0)
                throw new ArgumentException($"clip '{name}' must be a bare stem (no path separators)");

            if (!seen.Add(name))
                continue;

            var reference = $"{AnimDirectory}/{name}.anb";
            var anbPath = resolver.Resolve(reference, reference)
                ?? throw new ArgumentException(
                    $"clip '{name}' not found: expected {DisplayPath(root, reference)}");

            var bankPath = FindSiblingBank(anbPath)
                ?? throw new ArgumentException(
                    $"clip '{name}': no sibling .skb part bank for '{Path.GetFileName(anbPath)}' "
                    + $"under {DisplayPath(root, SkeletonDirectory)}");

            sources.Add(new ClipSource(name, anbPath, bankPath));
        }

        return sources;
    }

    /// <summary>
    /// The bank for a clip: the longest <c>.skb</c> stem in the clip's grandparent <c>skel</c>
    /// directory that prefixes the clip's name (<c>tools/re/anb/probe.py</c>'s
    /// <c>find_sibling_skb</c>). Returns <c>null</c> when the directory or a match is absent.
    /// </summary>
    public static string? FindSiblingBank(string anbPath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(anbPath);

        var clip = Path.GetFileNameWithoutExtension(anbPath);
        var animDirectory = Path.GetDirectoryName(anbPath);
        if (animDirectory is null)
            return null;
        var skeletonDirectory = Path.Combine(Path.GetDirectoryName(animDirectory) ?? "", "skel");
        if (!Directory.Exists(skeletonDirectory))
            return null;

        string? best = null;
        var bestStemLength = 0;
        foreach (var candidate in Directory.EnumerateFiles(skeletonDirectory, "*", SearchOption.TopDirectoryOnly)
                     .Where(path => string.Equals(Path.GetExtension(path), ".skb", StringComparison.OrdinalIgnoreCase))
                     .OrderBy(path => path, StringComparer.Ordinal))
        {
            var stem = Path.GetFileNameWithoutExtension(candidate);
            if (clip != stem && !clip.StartsWith(stem + "_", StringComparison.Ordinal))
                continue;
            if (stem.Length > bestStemLength)
            {
                best = candidate;
                bestStemLength = stem.Length;
            }
        }

        return best;
    }

    /// <summary>Reads a fresh clip/bank pair (the track map mutates the clip, so never share them).</summary>
    public static (AnimClip Clip, SkbBank Bank) Load(ClipSource source)
    {
        ArgumentNullException.ThrowIfNull(source);
        return (AnbReader.Read(source.AnbPath), SkbReader.Read(source.BankPath));
    }

    /// <summary>Host path for a game-root-relative reference, for error messages.</summary>
    private static string DisplayPath(string root, string reference) =>
        Path.Combine(root, reference.Replace('/', Path.DirectorySeparatorChar));
}
