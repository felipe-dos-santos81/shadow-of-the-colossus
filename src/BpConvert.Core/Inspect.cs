using AquaModelLibrary.Data.BluePoint.CMDL;
using AquaModelLibrary.Data.BluePoint.CMSH;

namespace BpConvert.Core;

/// <summary>
/// Best-effort inspection of a BluePoint model file through AquaModelLibrary.Data (parse-only).
/// Thin wrapper used by the CLI's <c>inspect</c> verb and by the integration tests; it is the only
/// file outside <c>Aqua/</c> allowed to touch library types (Task 4 adds the adapter itself).
/// Compressed containers decompress via the vendored Oodle dylib — see <c>tools/native/NOTICE.md</c>.
/// </summary>
public static class Inspect
{
    /// <summary>Pinned Task 0 API. See <see cref="Describe"/> for the vertex count.</summary>
    public static InspectSummary Summarize(string absolutePath)
    {
        var detail = Describe(absolutePath);
        return new InspectSummary(detail.Parsed, detail.MeshCount, detail.Error);
    }

    /// <summary>Same as <see cref="Summarize"/>, plus the vertex count the CLI prints.</summary>
    public static InspectDetail Describe(string absolutePath)
    {
        try
        {
            var bytes = File.ReadAllBytes(absolutePath);
            return Path.GetExtension(absolutePath).ToLowerInvariant() switch
            {
                ".cmsh" => DescribeCmsh(bytes),
                ".cmdl" => DescribeCmdl(bytes),
                var ext => new InspectDetail(false, 0, 0, $"unsupported extension '{ext}'"),
            };
        }
        catch (Exception ex)
        {
            return new InspectDetail(false, 0, 0, $"{ex.GetType().Name}: {FirstLine(ex.Message)}");
        }
    }

    private static InspectDetail DescribeCmsh(byte[] bytes)
    {
        var cmsh = new CMSH(bytes);
        if (cmsh.header == null)
        {
            // Reference-mesh/LOD variants: the library's constructor returns early by design
            // (CMSH.cs:28-37). The file is recognized, but there is no geometry to report.
            return new InspectDetail(true, 0, 0, "reference-mesh variant skipped by the library (no geometry)");
        }

        return new InspectDetail(true, cmsh.header.meshCount, cmsh.vertData?.positionList.Count ?? 0, null);
    }

    private static InspectDetail DescribeCmdl(byte[] bytes)
    {
        var cmdl = new CMDL(bytes);
        // A cmdl has no geometry of its own; its "mesh count" is the number of cmsh references.
        return new InspectDetail(true, cmdl.GetCMeshReferences().Count, 0, null);
    }

    private static string FirstLine(string message)
    {
        var i = message.IndexOf('\n');
        return i < 0 ? message : message[..i].TrimEnd();
    }
}

/// <summary>
/// Pinned Task 0 API. <c>Error</c> carries the failure reason when <c>Parsed</c> is false, and a
/// parse note otherwise (e.g. reference-mesh variants that carry no geometry).
/// </summary>
public sealed record InspectSummary(bool Parsed, int MeshCount, string? Error);

/// <summary>Inspection result including the vertex count (used by the CLI; not part of the pin).</summary>
public sealed record InspectDetail(bool Parsed, int MeshCount, int VertexCount, string? Error);
