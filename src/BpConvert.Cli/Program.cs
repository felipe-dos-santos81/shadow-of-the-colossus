using System.Text;
using BpConvert.Core;
using BpConvert.Core.Pipeline;

if (args.Length == 0)
{
    PrintUsage();
    return 2;
}

switch (args[0])
{
    case "inspect":
        return InspectCommand(args);
    case "convert":
        return ConvertCommand(args);
    case "report":
        return ReportCommand(args);
    default:
        Console.Error.WriteLine($"bp-convert: unknown verb '{args[0]}'");
        PrintUsage();
        return 2;
}

static void PrintUsage()
{
    Console.Error.WriteLine("usage:");
    Console.Error.WriteLine("  bp-convert inspect <file>    print size, first 32 header bytes and a library parse summary");
    Console.Error.WriteLine("  bp-convert convert --root <game> --out <lib> [--under <dir|file>]... [--ext cmsh] [--clips <name,...>] [--jobs N] [--force]");
    Console.Error.WriteLine("                               convert every matching source; outcomes recorded in <lib>/catalog.json");
    Console.Error.WriteLine("                               --clips bakes <game>/cp3shadow/anim/<name>.anb into each glb");
    Console.Error.WriteLine("  bp-convert report --out <lib> [--top N]");
    Console.Error.WriteLine("                               print catalog counts and the top-N failure reasons");
}

static int InspectCommand(string[] args)
{
    if (args.Length != 2)
    {
        Console.Error.WriteLine("usage: bp-convert inspect <file>");
        return 2;
    }

    var path = args[1];
    if (!File.Exists(path))
    {
        Console.Error.WriteLine($"bp-convert inspect: file not found: {path}");
        return 2;
    }

    Console.WriteLine(path);
    Console.WriteLine($"  size:  {new FileInfo(path).Length} bytes");
    Console.WriteLine($"  head:  {HeadHex(path, 32)}");

    var detail = BpConvert.Core.Inspect.Describe(path);
    if (detail.Parsed)
    {
        var note = detail.Error is null ? "" : $" — {detail.Error}";
        Console.WriteLine($"  parse: meshes={detail.MeshCount} vertices={detail.VertexCount}{note}");
        return 0;
    }

    Console.WriteLine($"  parse: FAILED — {detail.Error}");
    return 1;
}

/// <summary>
/// <c>convert --root &lt;game&gt; --out &lt;lib&gt; [--under &lt;dir|file&gt;]... [--ext cmsh]
/// [--clips &lt;name,...&gt;] [--jobs N] [--force]</c>.
/// Exit codes: 0 = batch completed (per-file failures are recorded in the catalog, not fatal),
/// 1 = batch-level failure (unreadable catalog, I/O), 2 = bad invocation.
/// </summary>
static int ConvertCommand(string[] args)
{
    string? root = null, outLib = null, ext = null, jobsText = null, clipsText = null;
    var under = new List<string>();
    var force = false;

    for (var i = 1; i < args.Length; i++)
    {
        switch (args[i])
        {
            case "--root":
                if (!TryValue(args, ref i, out root)) return MissingValue("--root");
                break;
            case "--out":
                if (!TryValue(args, ref i, out outLib)) return MissingValue("--out");
                break;
            case "--under":
                if (!TryValue(args, ref i, out var underDir)) return MissingValue("--under");
                under.Add(underDir!);
                break;
            case "--ext":
                if (!TryValue(args, ref i, out ext)) return MissingValue("--ext");
                break;
            case "--clips":
                if (!TryValue(args, ref i, out clipsText)) return MissingValue("--clips");
                break;
            case "--jobs":
                if (!TryValue(args, ref i, out jobsText)) return MissingValue("--jobs");
                break;
            case "--force":
                force = true;
                break;
            default:
                Console.Error.WriteLine($"bp-convert convert: unknown argument '{args[i]}'");
                PrintUsage();
                return 2;
        }
    }

    if (root is null)
    {
        Console.Error.WriteLine("bp-convert convert: --root <game> is required");
        PrintUsage();
        return 2;
    }
    if (outLib is null)
    {
        Console.Error.WriteLine("bp-convert convert: --out <lib> is required");
        PrintUsage();
        return 2;
    }

    var jobs = Environment.ProcessorCount;
    if (jobsText is not null && (!int.TryParse(jobsText, out jobs) || jobs < 1 || jobs > 1024))
    {
        Console.Error.WriteLine($"bp-convert convert: --jobs must be an integer in [1, 1024] (got '{jobsText}')");
        return 2;
    }

    var options = new BatchOptions
    {
        GameRoot = root,
        OutLibDir = outLib,
        Under = under,
        Jobs = jobs,
        Force = force,
    };
    if (ext is not null)
    {
        options = options with { Extensions = ext.Split(',') };
    }
    if (clipsText is not null)
    {
        var clips = clipsText.Split(',', StringSplitOptions.TrimEntries);
        if (clips.Any(clip => clip.Length == 0))
        {
            Console.Error.WriteLine($"bp-convert convert: --clips contains an empty clip name (got '{clipsText}')");
            return 2;
        }
        options = options with { Clips = clips };
    }

    var underLabel = under.Count == 0 ? "[whole tree]" : string.Join(",", under);
    var clipsLabel = options.Clips.Count == 0 ? "" : $" clips={string.Join(",", options.Clips)}";
    Console.WriteLine($"convert: root={root} out={outLib} under={underLabel} "
        + $"ext={string.Join(",", options.Extensions)}{clipsLabel} jobs={options.Jobs} "
        + $"force={force.ToString().ToLowerInvariant()}");

    try
    {
        var summary = BatchRunner.Run(options, Console.WriteLine);
        Console.WriteLine();
        Console.WriteLine($"in scope: {summary.InScope}  skipped: {summary.Skipped}  converted: {summary.Converted}  "
            + $"failed: {summary.Failed}  pending: {summary.Pending}  elapsed: {FormatElapsed(summary.Elapsed)}");
        Console.WriteLine($"catalog: {Catalog.FilePath(outLib)}");
        return 0;
    }
    catch (ArgumentException e)
    {
        Console.Error.WriteLine($"bp-convert convert: {e.Message}");
        return 2;
    }
    catch (InvalidDataException e)
    {
        Console.Error.WriteLine($"bp-convert convert: {e.Message}");
        return 1;
    }
    catch (Exception e) when (e is IOException or UnauthorizedAccessException or AggregateException)
    {
        var cause = (e as AggregateException)?.InnerException ?? e;
        Console.Error.WriteLine($"bp-convert convert: {cause.GetType().Name}: {cause.Message}");
        return 1;
    }
}

/// <summary>
/// <c>report --out &lt;lib&gt; [--top N]</c>: counts per status plus the top-N failure reasons
/// (message → count, one sample source each). Exit codes: 0 = ok, 1 = unreadable catalog,
/// 2 = bad invocation or no catalog yet.
/// </summary>
static int ReportCommand(string[] args)
{
    string? outLib = null, topText = null;
    for (var i = 1; i < args.Length; i++)
    {
        switch (args[i])
        {
            case "--out":
                if (!TryValue(args, ref i, out outLib)) return MissingValue("--out");
                break;
            case "--top":
                if (!TryValue(args, ref i, out topText)) return MissingValue("--top");
                break;
            default:
                Console.Error.WriteLine($"bp-convert report: unknown argument '{args[i]}'");
                PrintUsage();
                return 2;
        }
    }

    if (outLib is null)
    {
        Console.Error.WriteLine("bp-convert report: --out <lib> is required");
        PrintUsage();
        return 2;
    }

    var top = 20;
    if (topText is not null && (!int.TryParse(topText, out top) || top < 1))
    {
        Console.Error.WriteLine($"bp-convert report: --top must be a positive integer (got '{topText}')");
        return 2;
    }

    var catalogPath = Catalog.FilePath(outLib);
    if (!File.Exists(catalogPath))
    {
        Console.Error.WriteLine($"bp-convert report: no catalog at {catalogPath} — run 'bp-convert convert' first");
        return 2;
    }

    try
    {
        var summary = Catalog.Summarize(Catalog.Load(outLib), top);
        Console.WriteLine($"catalog: {catalogPath}");
        Console.WriteLine($"entries: {summary.Total}");
        var width = summary.StatusCounts.Count == 0
            ? 0
            : summary.StatusCounts.Max(pair => pair.Key.Length);
        foreach (var (status, count) in summary.StatusCounts)
        {
            Console.WriteLine($"  {status.PadRight(width)}: {count}");
        }
        if (summary.TopFailures.Count > 0)
        {
            Console.WriteLine($"top {summary.TopFailures.Count} failure reasons:");
            for (var i = 0; i < summary.TopFailures.Count; i++)
            {
                var reason = summary.TopFailures[i];
                Console.WriteLine($"  {i + 1,3}. {reason.Count} × {reason.Error}");
                Console.WriteLine($"       e.g. {reason.SampleSource}");
            }
        }
        return 0;
    }
    catch (InvalidDataException e)
    {
        Console.Error.WriteLine($"bp-convert report: {e.Message}");
        return 1;
    }
    catch (Exception e) when (e is IOException or UnauthorizedAccessException or AggregateException)
    {
        // Same filter and exit code as `convert`: a catalog that exists but cannot be read
        // (permissions, I/O) reports a one-line cause, never a raw stack trace.
        var cause = (e as AggregateException)?.InnerException ?? e;
        Console.Error.WriteLine($"bp-convert report: {cause.GetType().Name}: {cause.Message}");
        return 1;
    }
}

static bool TryValue(string[] args, ref int i, out string? value)
{
    if (i + 1 >= args.Length)
    {
        value = null;
        return false;
    }
    value = args[++i];
    return true;
}

static int MissingValue(string flag)
{
    Console.Error.WriteLine($"bp-convert: {flag} needs a value");
    PrintUsage();
    return 2;
}

static string FormatElapsed(TimeSpan elapsed) =>
    elapsed.TotalHours >= 1 ? elapsed.ToString(@"hh\:mm\:ss") : elapsed.ToString(@"mm\:ss\.fff");

static string HeadHex(string path, int maxBytes)
{
    using var stream = File.OpenRead(path);
    var buffer = new byte[Math.Min(maxBytes, stream.Length)];
    stream.ReadExactly(buffer);

    var sb = new StringBuilder(buffer.Length * 3);
    for (var i = 0; i < buffer.Length; i++)
    {
        if (i > 0)
        {
            sb.Append(' ');
        }
        sb.Append(buffer[i].ToString("x2"));
    }
    return sb.ToString();
}
