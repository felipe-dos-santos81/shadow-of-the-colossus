using System.Text.Json;
using BpConvert.Core.Anim;

namespace BpConvert.Core.Pipeline;

/// <summary>
/// The provenance record written beside a converted <c>.glb</c> when the conversion baked clips:
/// <c>&lt;stem&gt;.clips.json</c>, one entry per baked clip (name, engine frame count, playback fps,
/// track count, whether the clip carries a root-position series, and how that series relates to
/// the ground).
/// </summary>
/// <remarks>
/// The record exists so the app selects clips by name and never hard-codes a frame count. It is
/// written <b>only</b> for conversions that baked at least one clip (S4.1 ruling R2): the caller —
/// <see cref="ModelConverter.Convert"/> — owns that scope, so an M0 batch run stays sidecar-free,
/// and removes a previous record when a re-conversion bakes none.
/// </remarks>
public static class ClipManifest
{
    /// <summary>Record version; a consumer refuses anything else.</summary>
    public const int Version = 1;

    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    };

    /// <summary>
    /// Writes the record for <paramref name="clips"/> next to <paramref name="glbPath"/>
    /// (<see cref="Path.ChangeExtension(string, string)"/> to <c>.clips.json</c>) and returns its
    /// path.
    /// </summary>
    /// <param name="glbPath">The written <c>.glb</c>; the record sits in the same directory.</param>
    /// <param name="sourceRelPath">Game-root-relative source spelling, recorded for provenance.</param>
    /// <param name="clips">The clips the conversion baked, in emission order.</param>
    public static string Write(string glbPath, string sourceRelPath, IReadOnlyList<AnimClip> clips)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(glbPath);
        ArgumentException.ThrowIfNullOrWhiteSpace(sourceRelPath);
        ArgumentNullException.ThrowIfNull(clips);

        var path = Path.ChangeExtension(glbPath, ".clips.json");
        var record = new ClipRecord(
            Version,
            sourceRelPath,
            clips.Select(clip => new ClipEntry(
                clip.Name,
                clip.Frames,
                clip.Fps,
                clip.Tracks.Count,
                clip.Root.Count > 0,
                ClipMotion.For(clip.Name))).ToList());

        File.WriteAllText(path, JsonSerializer.Serialize(record, Options));
        return path;
    }

    /// <summary>
    /// Removes the record beside <paramref name="glbPath"/> (<c>&lt;stem&gt;.clips.json</c>), if any.
    /// </summary>
    /// <remarks>
    /// Called by a re-conversion that bakes no clips: the fresh <c>.glb</c> and the previous
    /// record would otherwise disagree about the clips. <see cref="File.Delete(string)"/> is a
    /// no-op when the file is absent, so an M0 batch stays free of this call's effects.
    /// </remarks>
    public static void Delete(string glbPath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(glbPath);

        File.Delete(Path.ChangeExtension(glbPath, ".clips.json"));
    }

    private sealed record ClipRecord(int Version, string Source, IReadOnlyList<ClipEntry> Clips);

    private sealed record ClipEntry(
        string Name, int Frames, double Fps, int Tracks, bool Root, string RootMotion);
}
