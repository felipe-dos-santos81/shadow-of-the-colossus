using System.Globalization;
using System.Text;

namespace BpConvert.Core.Audio;

/// <summary>
/// One line of <c>audio.manifest</c> (ruling R1): the decoded clip's identity, its WAV path
/// relative to the library root, and the measured PCM facts. The viewer loads the manifest and
/// never opens a <c>.sxd</c>.
/// </summary>
/// <param name="Bank">Source bank name, e.g. <c>boss01</c> or <c>music</c>.</param>
/// <param name="Name">Clip name — the curated event key, e.g. <c>valus.impact</c>.</param>
/// <param name="File">
/// WAV path relative to the library root with forward slashes, e.g.
/// <c>audio/boss01/valus.impact.wav</c>.
/// </param>
/// <param name="Rate">Measured sample rate (48 000).</param>
/// <param name="Channels">1 or 2 (5.1 was downmixed at conversion time, ruling R6).</param>
/// <param name="Frames">Measured per-channel frame count (not the record's declared count).</param>
/// <param name="Seconds">Measured duration, <paramref name="Frames"/> / <paramref name="Rate"/>.</param>
/// <param name="Codec">Source codec: <c>at9</c>, <c>psadpcm</c>, or <c>mixed</c> for a layer mix.</param>
/// <param name="Loop">True for the streamed music tracks.</param>
/// <param name="GainDb">
/// The attenuation <see cref="AudioCodec.Normalize"/> applied when bringing the clip to peak
/// ≤ 0.99 (0 when no scaling was needed). Informational: the WAV is already normalized.
/// </param>
public sealed record AudioEntry(
    string Bank, string Name, string File, int Rate, int Channels, long Frames, double Seconds,
    string Codec, bool Loop, double GainDb);

/// <summary>
/// Reads and writes the line-based <c>audio.manifest</c> (ruling R1):
/// <code>
/// version=1
/// name=valus.impact bank=boss01 file=audio/boss01/valus.impact.wav rate=48000 channels=2 frames=362068 seconds=7.543083 codec=psadpcm loop=0 gain_db=-0.0864
/// </code>
/// One entry per line, keys in that order, sorted by <c>(bank, name)</c>. Values are
/// space-free: the clip name is the curated event key, so the <c>key=value</c> pairs stay
/// unambiguous for the viewer's simple parser. The first line is <c>version=1</c>; the path key is
/// relative to the library root (<c>out/library</c>), which is exactly what
/// <c>AudioEngine::load_manifest(manifestPath, libraryRoot)</c> receives.
/// </summary>
public static class AudioManifest
{
    /// <summary>The only manifest schema version this converter writes/reads.</summary>
    public const int Version = 1;

    private static readonly string[] Keys =
    {
        "name", "bank", "file", "rate", "channels", "frames", "seconds", "codec", "loop", "gain_db",
    };

    /// <summary>
    /// Writes <paramref name="entries"/> sorted by <c>(bank, name)</c>, creating the parent
    /// directory. Returns <paramref name="path"/> so a caller can log it directly.
    /// </summary>
    /// <exception cref="InvalidDataException">
    /// A field is empty/out of range, a value contains whitespace (the line format is
    /// space-separated), or two entries share a <c>(bank, name)</c> key.
    /// </exception>
    public static string Write(string path, IReadOnlyList<AudioEntry> entries)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        ArgumentNullException.ThrowIfNull(entries);

        var seen = new HashSet<(string Bank, string Name)>();
        foreach (var entry in entries)
        {
            Validate(path, null, entry);
            if (!seen.Add((entry.Bank, entry.Name)))
                throw Invalid(path, $"duplicate entry {entry.Bank}/{entry.Name}");
        }

        var builder = new StringBuilder();
        builder.Append("version=").Append(Version).Append('\n');
        foreach (var entry in entries.OrderBy(entry => entry.Bank, StringComparer.Ordinal)
                     .ThenBy(entry => entry.Name, StringComparer.Ordinal))
        {
            builder.Append("name=").Append(entry.Name)
                .Append(" bank=").Append(entry.Bank)
                .Append(" file=").Append(entry.File)
                .Append(" rate=").Append(entry.Rate.ToString(CultureInfo.InvariantCulture))
                .Append(" channels=").Append(entry.Channels.ToString(CultureInfo.InvariantCulture))
                .Append(" frames=").Append(entry.Frames.ToString(CultureInfo.InvariantCulture))
                .Append(" seconds=").Append(entry.Seconds.ToString("F6", CultureInfo.InvariantCulture))
                .Append(" codec=").Append(entry.Codec)
                .Append(" loop=").Append(entry.Loop ? '1' : '0')
                .Append(" gain_db=").Append(entry.GainDb.ToString("0.####", CultureInfo.InvariantCulture))
                .Append('\n');
        }

        var directory = Path.GetDirectoryName(Path.GetFullPath(path));
        if (!string.IsNullOrEmpty(directory))
            Directory.CreateDirectory(directory);
        File.WriteAllText(path, builder.ToString(), new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
        return path;
    }

    /// <summary>
    /// Reads a manifest written by <see cref="Write"/> and returns its entries sorted by
    /// <c>(bank, name)</c> (the on-disk order is already sorted; sorting here makes a hand-edited
    /// file stable too).
    /// </summary>
    /// <exception cref="InvalidDataException">
    /// The version line is missing/unknown, a line has an unknown/duplicate/missing key or an
    /// unparsable value, or two entries share a <c>(bank, name)</c> key.
    /// </exception>
    public static IReadOnlyList<AudioEntry> Read(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        var lines = File.ReadAllLines(path);
        var entries = new List<AudioEntry>(Math.Max(0, lines.Length - 1));
        var seen = new HashSet<(string Bank, string Name)>();
        var sawVersion = false;

        for (var i = 0; i < lines.Length; i++)
        {
            var line = lines[i].Trim();
            if (line.Length == 0)
                continue;

            if (!sawVersion)
            {
                if (line != $"version={Version}")
                    throw Invalid(path, $"line {i + 1}: expected version={Version}, got \"{line}\"");
                sawVersion = true;
                continue;
            }

            var values = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var token in line.Split(' ', StringSplitOptions.RemoveEmptyEntries))
            {
                var equals = token.IndexOf('=');
                if (equals <= 0 || equals == token.Length - 1)
                    throw Invalid(path, $"line {i + 1}: \"{token}\" is not key=value");
                var key = token[..equals];
                if (Array.IndexOf(Keys, key) < 0)
                    throw Invalid(path, $"line {i + 1}: unknown key \"{key}\"");
                if (!values.TryAdd(key, token[(equals + 1)..]))
                    throw Invalid(path, $"line {i + 1}: duplicate key \"{key}\"");
            }

            foreach (var key in Keys)
                if (!values.ContainsKey(key))
                    throw Invalid(path, $"line {i + 1}: missing key \"{key}\"");

            var entry = new AudioEntry(
                values["bank"], values["name"], values["file"],
                ParseInt(path, i, "rate", values["rate"]),
                ParseInt(path, i, "channels", values["channels"]),
                ParseLong(path, i, "frames", values["frames"]),
                ParseDouble(path, i, "seconds", values["seconds"]),
                values["codec"],
                ParseLoop(path, i, values["loop"]),
                ParseDouble(path, i, "gain_db", values["gain_db"]));
            Validate(path, $"line {i + 1}: ", entry);
            if (!seen.Add((entry.Bank, entry.Name)))
                throw Invalid(path, $"line {i + 1}: duplicate entry {entry.Bank}/{entry.Name}");
            entries.Add(entry);
        }

        if (!sawVersion)
            throw Invalid(path, "missing version=1 line");
        return entries.OrderBy(entry => entry.Bank, StringComparer.Ordinal)
            .ThenBy(entry => entry.Name, StringComparer.Ordinal)
            .ToList();
    }

    private static void Validate(string path, string? location, AudioEntry entry)
    {
        var at = $"{location}{entry.Bank}/{entry.Name}";
        if (entry.Bank.Length == 0 || entry.Name.Length == 0 || entry.File.Length == 0
            || entry.Codec.Length == 0)
            throw Invalid(path, $"{location}entry has an empty bank/name/file/codec");
        if (ContainsWhitespace(entry.Bank) || ContainsWhitespace(entry.Name)
            || ContainsWhitespace(entry.File) || ContainsWhitespace(entry.Codec))
            throw Invalid(path,
                $"{at} has a value with whitespace (the line format is space-separated)");
        if (entry.Rate <= 0)
            throw Invalid(path, $"{at} has rate {entry.Rate}");
        if (entry.Channels is not (1 or 2))
            throw Invalid(path, $"{at} has {entry.Channels} channels (the runtime is stereo)");
        if (entry.Frames < 0)
            throw Invalid(path, $"{at} has {entry.Frames} frames");
        if (!double.IsFinite(entry.Seconds) || entry.Seconds < 0)
            throw Invalid(path, $"{at} has seconds {entry.Seconds}");
        if (!double.IsFinite(entry.GainDb) || entry.GainDb > 0)
            throw Invalid(path, $"{at} has gain {entry.GainDb} dB (normalization only attenuates)");
    }

    private static bool ContainsWhitespace(string value) =>
        value.Any(char.IsWhiteSpace);

    private static int ParseInt(string path, int line, string key, string value)
    {
        if (!int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed))
            throw Invalid(path, $"line {line + 1}: {key}=\"{value}\" is not an integer");
        return parsed;
    }

    private static long ParseLong(string path, int line, string key, string value)
    {
        if (!long.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed))
            throw Invalid(path, $"line {line + 1}: {key}=\"{value}\" is not an integer");
        return parsed;
    }

    private static double ParseDouble(string path, int line, string key, string value)
    {
        if (!double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed)
            || !double.IsFinite(parsed))
            throw Invalid(path, $"line {line + 1}: {key}=\"{value}\" is not a finite number");
        return parsed;
    }

    private static bool ParseLoop(string path, int line, string value) => value switch
    {
        "0" => false,
        "1" => true,
        _ => throw Invalid(path, $"line {line + 1}: loop=\"{value}\" is not 0/1"),
    };

    private static InvalidDataException Invalid(string path, string detail) => new($"{path}: {detail}");
}
