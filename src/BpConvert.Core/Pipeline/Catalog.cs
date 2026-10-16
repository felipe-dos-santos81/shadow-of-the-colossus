using System.Diagnostics.CodeAnalysis;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace BpConvert.Core.Pipeline;

/// <summary>
/// The resumable batch catalog (pinned Task 6 schema): one JSON document per output library
/// recording, for every source the batch runner has attempted, whether it converted
/// (<c>ok</c>) or not (<c>failed</c>) — plus the outcome's assets and metrics:
/// <code>
/// { "version": 1, "entries": {
///   "&lt;source rel path&gt;": { "status": "ok|failed", "glb": "&lt;rel path|null&gt;",
///     "pngs": ["&lt;rel&gt;..."], "meshes": 0, "vertices": 0, "error": null } } }
/// </code>
/// </summary>
/// <remarks>
/// <para>
/// Keys are game-root-relative, '/'-separated source paths; <c>glb</c>/<c>pngs</c> are
/// output-library-relative, '/'-separated written assets. Persistence is atomic — serialise to
/// <see cref="TempFileName"/>, flush to disk, rename over <see cref="FileName"/> — so a crash at
/// any point leaves a complete, loadable catalog of everything finished up to the last flush, and
/// a resume never sees a torn document.
/// </para>
/// <para>
/// <see cref="CatalogData"/> is safe for the batch runner's concurrent writers; reads
/// (<see cref="CatalogData.Entries"/>) return a sorted copy.
/// </para>
/// </remarks>
public static class Catalog
{
    public const int SchemaVersion = 1;
    public const string FileName = "catalog.json";
    public const string TempFileName = "catalog.json.tmp";
    public const string StatusOk = "ok";
    public const string StatusFailed = "failed";

    /// <summary>Serialises whole-catalog writes; one writer at a time keeps the tmp file single-use.</summary>
    private static readonly object WriteGate = new();

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = false,
        // Same document, just not \u-escaping the non-ASCII in converter messages — this file is
        // written to disk and read back by us, never embedded in HTML.
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    /// <summary>Absolute path of the catalog inside <paramref name="outLibDir"/>.</summary>
    public static string FilePath(string outLibDir) => Path.Combine(outLibDir, FileName);

    /// <summary>
    /// Loads the catalog at <paramref name="outLibDir"/>. A missing file is an empty catalog (a
    /// first run); a file that is not valid JSON or not version <see cref="SchemaVersion"/> throws
    /// <see cref="InvalidDataException"/> — never silently reconvert the whole tree over a
    /// damaged catalog.
    /// </summary>
    public static CatalogData Load(string outLibDir)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(outLibDir);
        var path = FilePath(outLibDir);
        var data = new CatalogData();
        if (!File.Exists(path))
        {
            return data;
        }

        CatalogDocument? document;
        try
        {
            document = JsonSerializer.Deserialize<CatalogDocument>(File.ReadAllText(path), JsonOptions);
        }
        catch (JsonException e)
        {
            throw new InvalidDataException($"catalog is not valid JSON: {path}: {e.Message}", e);
        }

        if (document is null)
        {
            throw new InvalidDataException($"catalog is empty: {path}");
        }
        if (document.Version != SchemaVersion)
        {
            throw new InvalidDataException(
                $"catalog version {document.Version} is not supported (expected {SchemaVersion}): {path}");
        }

        foreach (var (source, entry) in document.Entries ?? [])
        {
            if (entry.Status is null)
            {
                throw new InvalidDataException($"catalog entry '{source}' has no status: {path}");
            }
            data.Set(source, new CatalogEntry(entry.Status, entry.Glb, entry.Pngs ?? [],
                entry.Meshes, entry.Vertices, entry.Error));
        }
        return data;
    }

    /// <summary>
    /// Atomically replaces the catalog at <paramref name="outLibDir"/> with <paramref name="data"/>:
    /// write <see cref="TempFileName"/>, flush to disk, rename over <see cref="FileName"/>. Creates
    /// the output library directory when absent. The whole write is gated, so concurrent flushes
    /// (the runner's every-25-entries policy) never race on the tmp file.
    /// </summary>
    public static void Save(string outLibDir, CatalogData data)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(outLibDir);
        ArgumentNullException.ThrowIfNull(data);

        var path = FilePath(outLibDir);
        var tempPath = Path.Combine(outLibDir, TempFileName);

        lock (WriteGate)
        {
            // Snapshot inside the gate: with two flushes in flight, the later lock acquisition must
            // never write an older snapshot over a newer one (flushes stay monotonic).
            // Entries come back key-sorted, so the document is deterministic for equal state.
            var document = new CatalogDocument { Version = SchemaVersion };
            foreach (var (source, entry) in data.Entries)
            {
                document.Entries[source] = new CatalogEntryDocument
                {
                    Status = entry.Status,
                    Glb = entry.Glb,
                    Pngs = entry.Pngs.ToList(),
                    Meshes = entry.Meshes,
                    Vertices = entry.Vertices,
                    Error = entry.Error,
                };
            }

            Directory.CreateDirectory(outLibDir);
            using (var stream = new FileStream(tempPath, FileMode.Create, FileAccess.Write, FileShare.None))
            {
                JsonSerializer.Serialize(stream, document, JsonOptions);
                stream.Flush(flushToDisk: true);
            }
            File.Move(tempPath, path, overwrite: true);
        }
    }

    /// <summary>
    /// Counts entries per status (pinned order <c>ok</c>, <c>failed</c>, then any foreign status
    /// ordinally) and ranks failure reasons by count — ties broken by the error text — taking the
    /// first <paramref name="topFailures"/>. Each reason carries the ordinally-smallest source path
    /// that produced it, so the sample is deterministic.
    /// </summary>
    public static CatalogSummary Summarize(CatalogData data, int topFailures = 20)
    {
        ArgumentNullException.ThrowIfNull(data);
        ArgumentOutOfRangeException.ThrowIfNegative(topFailures);

        var counts = new Dictionary<string, int>(StringComparer.Ordinal);
        var failures = new Dictionary<string, (int Count, string Sample)>(StringComparer.Ordinal);
        foreach (var (source, entry) in data.Entries)
        {
            counts[entry.Status] = counts.GetValueOrDefault(entry.Status) + 1;
            if (entry.Status != StatusFailed)
            {
                continue;
            }

            var error = entry.Error ?? "(no error recorded)";
            if (failures.TryGetValue(error, out var current))
            {
                failures[error] = (current.Count + 1,
                    string.CompareOrdinal(source, current.Sample) < 0 ? source : current.Sample);
            }
            else
            {
                failures[error] = (1, source);
            }
        }

        var statusCounts = counts
            .OrderBy(pair => StatusOrder(pair.Key))
            .ThenBy(pair => pair.Key, StringComparer.Ordinal)
            .Select(pair => new KeyValuePair<string, int>(pair.Key, pair.Value))
            .ToList();
        var top = failures
            .OrderByDescending(pair => pair.Value.Count)
            .ThenBy(pair => pair.Key, StringComparer.Ordinal)
            .Take(topFailures)
            .Select(pair => new FailureReason(pair.Key, pair.Value.Count, pair.Value.Sample))
            .ToList();
        return new CatalogSummary(data.Count, statusCounts, top);
    }

    private static int StatusOrder(string status) => status switch
    {
        StatusOk => 0,
        StatusFailed => 1,
        _ => 2,
    };

    /// <summary>Keys and paths are stored '/'-separated, whatever separator the caller used.</summary>
    internal static string NormalizeKey(string sourceRelPath) => sourceRelPath.Replace('\\', '/');

    /// <summary>On-disk document, pinned schema: <c>{"version": 1, "entries": {...}}</c>.</summary>
    private sealed class CatalogDocument
    {
        [JsonPropertyName("version")] public int Version { get; set; }

        [JsonPropertyName("entries")] public Dictionary<string, CatalogEntryDocument> Entries { get; set; } = new(StringComparer.Ordinal);
    }

    /// <summary>On-disk entry, pinned schema.</summary>
    private sealed class CatalogEntryDocument
    {
        [JsonPropertyName("status")] public string? Status { get; set; }
        [JsonPropertyName("glb")] public string? Glb { get; set; }
        [JsonPropertyName("pngs")] public List<string>? Pngs { get; set; }
        [JsonPropertyName("meshes")] public int Meshes { get; set; }
        [JsonPropertyName("vertices")] public long Vertices { get; set; }
        [JsonPropertyName("error")] public string? Error { get; set; }
    }
}

/// <summary>
/// In-memory catalog: source path → <see cref="CatalogEntry"/>, safe for the runner's concurrent
/// writers. <see cref="Entries"/> hands out a key-sorted copy (the order <see cref="Catalog.Save"/>
/// writes).
/// </summary>
public sealed class CatalogData
{
    private readonly object _gate = new();
    private readonly Dictionary<string, CatalogEntry> _entries = new(StringComparer.Ordinal);

    /// <summary>Number of recorded sources.</summary>
    public int Count
    {
        get { lock (_gate) { return _entries.Count; } }
    }

    /// <summary>Entries sorted by source path (ordinal) — the serialisation order.</summary>
    public IReadOnlyList<KeyValuePair<string, CatalogEntry>> Entries
    {
        get { lock (_gate) { return _entries.OrderBy(entry => entry.Key, StringComparer.Ordinal).ToList(); } }
    }

    /// <summary>Looks up the entry for a source path (either separator style).</summary>
    public bool TryGet(string sourceRelPath, [NotNullWhen(true)] out CatalogEntry? entry)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(sourceRelPath);
        lock (_gate) { return _entries.TryGetValue(Catalog.NormalizeKey(sourceRelPath), out entry); }
    }

    /// <summary>Records (or replaces) the entry for a source path.</summary>
    public void Set(string sourceRelPath, CatalogEntry entry)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(sourceRelPath);
        ArgumentNullException.ThrowIfNull(entry);
        lock (_gate) { _entries[Catalog.NormalizeKey(sourceRelPath)] = entry; }
    }
}

/// <summary>
/// One catalog entry (pinned Task 6 schema): <paramref name="Status"/> is
/// <see cref="Catalog.StatusOk"/> or <see cref="Catalog.StatusFailed"/>; <paramref name="Glb"/> and
/// <paramref name="Pngs"/> are output-library-relative written assets; <paramref name="Error"/> is
/// null for ok entries and a one-line <c>TypeName: message</c> for failed ones.
/// </summary>
public sealed record CatalogEntry(string Status, string? Glb, IReadOnlyList<string> Pngs,
    int Meshes, long Vertices, string? Error)
{
    /// <summary>A successful conversion of one source.</summary>
    public static CatalogEntry Ok(string glb, IReadOnlyList<string> pngs, int meshes, long vertices) =>
        new(Catalog.StatusOk, glb, pngs, meshes, vertices, null);

    /// <summary>A failed conversion attempt; <paramref name="error"/> is the recorded reason.</summary>
    public static CatalogEntry Failed(string error) =>
        new(Catalog.StatusFailed, null, [], 0, 0, error);
}

/// <summary>One ranked failure reason for <c>report</c>: <paramref name="Count"/> entries share
/// <paramref name="Error"/>; <paramref name="SampleSource"/> is one deterministic example source.</summary>
public sealed record FailureReason(string Error, int Count, string SampleSource);

/// <summary>Aggregated catalog view for <c>report</c>.</summary>
/// <param name="Total">Entries recorded.</param>
/// <param name="StatusCounts">Counts per status, pinned order <c>ok</c>, <c>failed</c>, then foreign.</param>
/// <param name="TopFailures">Failure reasons ranked by count (ties: error text ordinal).</param>
public sealed record CatalogSummary(int Total, IReadOnlyList<KeyValuePair<string, int>> StatusCounts,
    IReadOnlyList<FailureReason> TopFailures);
