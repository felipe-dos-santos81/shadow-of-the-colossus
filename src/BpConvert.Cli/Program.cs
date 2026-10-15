using System.Text;
using BpConvert.Core;

if (args.Length == 0)
{
    PrintUsage();
    return 2;
}

switch (args[0])
{
    case "inspect":
        return InspectCommand(args);
    default:
        Console.Error.WriteLine($"bp-convert: unknown verb '{args[0]}'");
        PrintUsage();
        return 2;
}

static void PrintUsage()
{
    Console.Error.WriteLine("usage:");
    Console.Error.WriteLine("  bp-convert inspect <file>    print size, first 32 header bytes and a library parse summary");
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
