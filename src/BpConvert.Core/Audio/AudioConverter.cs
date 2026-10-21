using System.Globalization;
using System.Text.Json;

namespace BpConvert.Core.Audio;

/// <summary>One curated cue from <c>tools/re/audio/s5_map.json</c>.</summary>
/// <param name="Event">The sound-request event key, e.g. <c>valus.impact</c> or <c>music.boss</c>.</param>
/// <param name="Bank">The bank file stem (<c>boss01</c>, <c>music</c>, …).</param>
/// <param name="Name">
/// The request name (<c>NAME</c>/<c>REQD</c> namespace) for resident cues, or the <c>REQD</c>
/// track name for music (<c>"Boss 1"</c>). Validated against the bank's own table.
/// </param>
/// <param name="Wave">The primary <c>WAVE</c> table index.</param>
/// <param name="Layers">
/// Every wave in the sound, primary included; <c>Layers[0] == Wave</c>. Mixed with equal gains
/// from t=0 (the gate's <c>valus.impact</c> mix).
/// </param>
/// <param name="Loop">True for the streamed music track.</param>
public sealed record AudioCue(
    string Event, string Bank, string Name, int Wave, IReadOnlyList<int> Layers, bool Loop);

/// <summary>
/// The <c>audio</c> verb's inputs. <paramref name="Events"/> empty and <paramref name="Music"/>
/// null means "the whole curated map".
/// </summary>
public sealed record AudioConvertOptions(
    string Root, string OutLibrary, string MapPath, IReadOnlyList<string> Events, string? Music,
    bool Force);

/// <summary>What one run produced: the merged manifest entries, plus which cues were decoded.</summary>
public sealed record AudioConvertResult(
    IReadOnlyList<AudioEntry> Entries, IReadOnlyList<string> Written, IReadOnlyList<string> Reused,
    string ManifestPath);

/// <summary>
/// The converter pipeline behind <c>bp-convert audio</c> (plan Task 3): read the curated map,
/// resolve each cue's bank and <c>WAVE</c> record(s), carve with the loader formula, decode,
/// mix layers, normalize, write <c>out/library/audio/&lt;bank&gt;/&lt;event&gt;.wav</c>, and merge the R1
/// <c>audio.manifest</c>. Every failure is an <see cref="InvalidDataException"/> naming the event
/// (the CLI turns it into exit 1).
/// </summary>
/// <remarks>
/// <para>
/// Resident payload: <c>wave_record_offset + 0x24 + start</c>, length <c>bytes</c>, inside the
/// <c>DATA</c> body (recon §7.1, ruling R8). Music payload: <c>start</c> absolute in the
/// companion <c>.sxd2</c> (recon §2). A range outside those regions is rejected as a non-tiling
/// range, never clamped.
/// </para>
/// <para>
/// Layers are decoded with <c>normalize: false</c> and summed with <c>1/N</c> gain from t=0,
/// padded to the longest layer (the gate's mix), then downmixed (already done per layer) and
/// normalized once, so the manifest's <c>gain_db</c> is the level the mixer actually sees.
/// </para>
/// </remarks>
public static class AudioConverter
{
    private const int ResidentPayloadOffset = 0x24;
    private const int StreamPayloadBase = 0x60;
    private const double SilenceFloorRms = 1e-3; // the gate's -60 dBFS
    private const string ManifestFileName = "audio.manifest";

    /// <summary>Runs the conversion; idempotent unless <see cref="AudioConvertOptions.Force"/>.</summary>
    /// <param name="options">Roots and scope.</param>
    /// <param name="log">Optional one-line progress sink (the CLI passes <c>Console.WriteLine</c>).</param>
    /// <exception cref="InvalidDataException">
    /// The map is unreadable/incomplete, a selected event/music name is not in the map, a bank,
    /// wave, name or payload is missing, a range does not tile, or a decode fails/silences.
    /// </exception>
    public static AudioConvertResult Run(AudioConvertOptions options, Action<string>? log = null)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentException.ThrowIfNullOrWhiteSpace(options.Root);
        ArgumentException.ThrowIfNullOrWhiteSpace(options.OutLibrary);
        ArgumentException.ThrowIfNullOrWhiteSpace(options.MapPath);
        ArgumentNullException.ThrowIfNull(options.Events);

        var cues = Select(LoadMap(options.MapPath), options);
        var manifestPath = Path.Combine(options.OutLibrary, "audio", ManifestFileName);

        // The manifest is cumulative: a run scoped to one event must not drop the other entries.
        var entries = new Dictionary<(string Bank, string Name), AudioEntry>();
        if (File.Exists(manifestPath))
            foreach (var entry in AudioManifest.Read(manifestPath))
                entries[(entry.Bank, entry.Name)] = entry;

        var banks = new Dictionary<string, BankContext>(StringComparer.Ordinal);
        var written = new List<string>();
        var reused = new List<string>();

        foreach (var cue in cues)
        {
            var key = (cue.Bank, cue.Event);
            if (!options.Force && entries.TryGetValue(key, out var existing)
                && File.Exists(Path.Combine(options.OutLibrary,
                    existing.File.Replace('/', Path.DirectorySeparatorChar))))
            {
                reused.Add(cue.Event);
                log?.Invoke($"  reuse {cue.Bank}/{cue.Event}  {existing.Seconds:F3} s  "
                    + $"{existing.Rate} Hz  gain {existing.GainDb:F2} dB");
                continue;
            }

            var entry = ConvertCue(options, cue, banks);
            entries[key] = entry;
            written.Add(cue.Event);
            log?.Invoke($"  write {cue.Bank}/{cue.Event}  {entry.Seconds:F3} s  "
                + $"{entry.Rate} Hz {entry.Channels} ch  gain {entry.GainDb:F2} dB  ({entry.Codec})");
        }

        var merged = entries.Values
            .OrderBy(entry => entry.Bank, StringComparer.Ordinal)
            .ThenBy(entry => entry.Name, StringComparer.Ordinal)
            .ToList();
        AudioManifest.Write(manifestPath, merged);
        log?.Invoke($"manifest: {manifestPath} ({merged.Count} entries)");
        return new AudioConvertResult(merged, written, reused, manifestPath);
    }

    private static AudioEntry ConvertCue(
        AudioConvertOptions options, AudioCue cue, Dictionary<string, BankContext> banks)
    {
        try
        {
            if (!banks.TryGetValue(cue.Bank, out var bank))
            {
                bank = LoadBank(options.Root, cue.Bank);
                banks[cue.Bank] = bank;
            }

            ValidateName(bank, cue);

            var layers = new List<DecodedAudio>(cue.Layers.Count);
            var codecs = new HashSet<byte>();
            foreach (var layer in cue.Layers)
            {
                if (layer < 0 || layer >= bank.Waves.Count)
                    throw new InvalidDataException(
                        $"wave {layer} is not in {bank.Bank.Path} (0..{bank.Waves.Count - 1})");
                var wave = bank.Waves[layer];
                var codec = (byte)(wave.FormatWord & 0xFF);
                codecs.Add(codec);
                if (codec == 0x42 && wave.CodecConfig == 0)
                    throw new InvalidDataException(
                        $"wave {layer} is ATRAC9 but has no config word at its record");
                var payload = Carve(bank, wave);
                layers.Add(AudioCodec.Decode(
                    payload, wave.FormatWord, wave.CodecConfig, wave.SampleRate,
                    normalize: false));
            }

            var rate = layers[0].Rate;
            if (layers.Any(layer => layer.Rate != rate))
                throw new InvalidDataException(
                    "the layers disagree on sample rate: "
                    + string.Join(", ", layers.Select(layer => layer.Rate).Distinct()));
            var channels = layers.Max(layer => layer.Channels);
            if (channels is not (1 or 2))
                throw new InvalidDataException($"the mix has {channels} channels (the runtime is stereo)");

            var mix = Mix(layers, channels);
            var normalized = AudioCodec.Normalize(new DecodedAudio(mix, channels, rate));
            if (normalized.Rms <= SilenceFloorRms)
                throw new InvalidDataException(
                    $"the decoded clip is silent (RMS {normalized.Rms:0.######})");

            var entry = new AudioEntry(
                cue.Bank, cue.Event, $"audio/{cue.Bank}/{cue.Event}.wav", rate, channels,
                normalized.Frames, Canonical(normalized.Seconds, "F6"), CodecName(codecs),
                cue.Loop, Canonical(normalized.GainDb, "0.####"));
            var path = Path.Combine(options.OutLibrary,
                entry.File.Replace('/', Path.DirectorySeparatorChar));
            WavWriter.Write(path, normalized.Samples, normalized.Channels, normalized.Rate);
            return entry;
        }
        catch (InvalidDataException e)
        {
            throw new InvalidDataException($"{cue.Event}: {e.Message}", e);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            throw new InvalidDataException($"{cue.Event}: {e.Message}", e);
        }
    }

    /// <summary>
    /// Rounds to exactly what <see cref="AudioManifest"/> prints, so the in-memory entry and the
    /// manifest line are the same value (the round-trip test compares records).
    /// </summary>
    private static double Canonical(double value, string format) =>
        double.Parse(value.ToString(format, CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);

    /// <summary>Equal-gain mix from t=0, padded to the longest layer.</summary>
    private static float[] Mix(IReadOnlyList<DecodedAudio> layers, int channels)
    {
        var frames = layers.Max(layer => layer.Frames);
        var mix = new float[frames * channels];
        var gain = 1f / layers.Count;
        foreach (var layer in layers)
        {
            for (var frame = 0; frame < layer.Frames; frame++)
            {
                if (layer.Channels == channels)
                {
                    for (var channel = 0; channel < channels; channel++)
                        mix[frame * channels + channel] +=
                            layer.Samples[frame * layer.Channels + channel] * gain;
                }
                else
                {
                    // A mono layer feeds both channels of a stereo mix.
                    var sample = layer.Samples[frame] * gain;
                    for (var channel = 0; channel < channels; channel++)
                        mix[frame * channels + channel] += sample;
                }
            }
        }

        return mix;
    }

    private static string CodecName(HashSet<byte> codecs) => codecs.Count == 1
        ? codecs.Single() switch
        {
            0x21 => "psadpcm",
            0x42 => "at9",
            var other => $"0x{other:X2}",
        }
        : "mixed";

    /// <summary>Carves one record's exact payload, rejecting a range outside the bank's region.</summary>
    private static byte[] Carve(BankContext context, WaveRecord wave)
    {
        long start;
        byte[] source;
        long limitStart;
        long limitEnd;
        if (context.Bank.HasData)
        {
            start = wave.RecordOffset + ResidentPayloadOffset + wave.Start;
            source = context.IndexBytes;
            limitStart = context.Bank.DataOffset + 0x10;
            limitEnd = context.Bank.DataOffset + 8 + context.Bank.DataSize;
        }
        else
        {
            start = wave.Start;
            source = context.PayloadBytes
                ?? throw new InvalidDataException(
                    $"bank {context.Bank.Name} has no companion .sxd2 payload");
            limitStart = StreamPayloadBase;
            limitEnd = source.Length;
        }

        if (start < limitStart || start + wave.ByteLength > limitEnd)
            throw new InvalidDataException(
                $"wave {wave.Index} payload [{start:#x}, +{wave.ByteLength}) escapes "
                + $"[{limitStart:#x}, {limitEnd:#x}) — the record does not tile the payload");
        return source.AsSpan((int)start, wave.ByteLength).ToArray();
    }

    /// <summary>Reads a bank and validates the cue's name against its own naming table.</summary>
    private static BankContext LoadBank(string root, string bankName)
    {
        var soundDir = Path.Combine(root, "sound", "shadow");
        var residentPath = Path.Combine(soundDir, bankName + ".sxd");
        var indexPath = Path.Combine(soundDir, bankName + ".sxd1");
        string path;
        if (File.Exists(residentPath))
            path = residentPath;
        else if (File.Exists(indexPath))
            path = indexPath;
        else
            throw new InvalidDataException(
                $"bank file not found: {residentPath} or {indexPath}");

        var bank = SxdContainer.Read(path);
        var waves = SxdTables.Waves(bank);
        if (waves.Count == 0)
            throw new InvalidDataException($"{path} has no WAVE records");

        var context = new BankContext(path, bank, waves, File.ReadAllBytes(path));
        if (bank.HasData)
            context.Names = SxdTables.Names(bank)
                .Select(record => record.Name)
                .ToHashSet(StringComparer.Ordinal);
        else
            context.PayloadBytes = ReadCompanion(path);
        return context;
    }

    private static byte[] ReadCompanion(string indexPath)
    {
        var payloadPath = Path.ChangeExtension(indexPath, ".sxd2");
        if (!File.Exists(payloadPath))
            throw new InvalidDataException($"companion payload not found: {payloadPath}");
        return File.ReadAllBytes(payloadPath);
    }

    /// <summary>Resolves and cross-checks the cue's name against the bank's table.</summary>
    private static void ValidateName(BankContext context, AudioCue cue)
    {
        if (context.Bank.HasData)
        {
            if (context.Names is null || !context.Names.Contains(cue.Name))
                throw new InvalidDataException(
                    $"name \"{cue.Name}\" is not in {context.Path}'s NAME table");
            return;
        }

        context.TrackNames ??= SxdTables.TrackNames(context.Bank);
        var tracks = context.TrackNames;        if (cue.Wave >= tracks.Count)
            throw new InvalidDataException(
                $"wave {cue.Wave} has no REQD track name (the bank has {tracks.Count})");
        if (!string.Equals(tracks[cue.Wave], cue.Name, StringComparison.Ordinal))
            throw new InvalidDataException(
                $"wave {cue.Wave} is the track \"{tracks[cue.Wave]}\", not \"{cue.Name}\"");
    }

    private static List<AudioCue> Select(List<AudioCue> cues, AudioConvertOptions options)
    {
        if (options.Events.Count == 0 && options.Music is null)
            return cues;

        var selected = new HashSet<string>(StringComparer.Ordinal);
        foreach (var eventName in options.Events)
        {
            var cue = cues.FirstOrDefault(candidate => candidate.Event == eventName)
                ?? throw new InvalidDataException(
                    $"event '{eventName}' is not in {options.MapPath}");
            selected.Add(cue.Event);
        }

        if (options.Music is not null)
        {
            var cue = cues.FirstOrDefault(candidate =>
                    candidate.Bank == "music" && candidate.Name == options.Music)
                ?? throw new InvalidDataException(
                    $"music track '{options.Music}' is not in {options.MapPath}");
            selected.Add(cue.Event);
        }

        return cues.Where(cue => selected.Contains(cue.Event)).ToList();
    }

    /// <summary>Reads the curated map (the Task 1 JSON schema, ruling R2).</summary>
    private static List<AudioCue> LoadMap(string path)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        var root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object
            || !root.TryGetProperty("cues", out var cues)
            || cues.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException($"{path}: no \"cues\" array");

        var result = new List<AudioCue>();
        var events = new HashSet<string>(StringComparer.Ordinal);
        foreach (var element in cues.EnumerateArray())
        {
            var eventName = RequiredString(element, "event", path);
            var bank = RequiredString(element, "bank", path);
            var name = RequiredString(element, "name", path);
            if (!element.TryGetProperty("wave", out var waveElement)
                || !waveElement.TryGetInt32(out var wave) || wave < 0)
                throw new InvalidDataException($"{path}: cue '{eventName}' has no valid wave index");
            if (!events.Add(eventName))
                throw new InvalidDataException($"{path}: duplicate cue event '{eventName}'");

            var layers = new List<int>();
            if (element.TryGetProperty("layers", out var layerElement))
            {
                if (layerElement.ValueKind != JsonValueKind.Array)
                    throw new InvalidDataException($"{path}: cue '{eventName}' has a non-array layers");
                foreach (var layer in layerElement.EnumerateArray())
                {
                    if (!layer.TryGetInt32(out var index) || index < 0)
                        throw new InvalidDataException(
                            $"{path}: cue '{eventName}' has a bad layer index");
                    layers.Add(index);
                }
            }

            if (layers.Count == 0)
                layers.Add(wave);
            if (layers[0] != wave)
                throw new InvalidDataException(
                    $"{path}: cue '{eventName}' must list its primary wave first in layers");
            if (layers.Distinct().Count() != layers.Count)
                throw new InvalidDataException($"{path}: cue '{eventName}' repeats a layer");

            var loop = element.TryGetProperty("loop", out var loopElement)
                && loopElement.ValueKind is JsonValueKind.True or JsonValueKind.False
                    ? loopElement.GetBoolean()
                    : bank == "music";
            result.Add(new AudioCue(eventName, bank, name, wave, layers, loop));
        }

        return result;
    }

    private static string RequiredString(JsonElement element, string key, string path)
    {
        if (!element.TryGetProperty(key, out var value)
            || value.ValueKind != JsonValueKind.String
            || string.IsNullOrWhiteSpace(value.GetString()))
            throw new InvalidDataException($"{path}: a cue is missing a non-empty \"{key}\"");
        return value.GetString()!;
    }

    /// <summary>A bank opened once per run, with its payload and name table cached.</summary>
    private sealed class BankContext
    {
        public BankContext(string path, SxdBank bank, IReadOnlyList<WaveRecord> waves, byte[] indexBytes)
        {
            Path = path;
            Bank = bank;
            Waves = waves;
            IndexBytes = indexBytes;
        }

        public string Path { get; }
        public SxdBank Bank { get; }
        public IReadOnlyList<WaveRecord> Waves { get; }
        public byte[] IndexBytes { get; }
        public byte[]? PayloadBytes { get; set; }
        public HashSet<string>? Names { get; set; }
        public IReadOnlyList<string>? TrackNames { get; set; }
    }
}
