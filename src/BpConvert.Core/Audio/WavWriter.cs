using System.Text;

namespace BpConvert.Core.Audio;

/// <summary>
/// Writes the converter's deliverable WAV: float32 RIFF/WAVE (<c>fmt</c> tag 3), 48 kHz,
/// mono or stereo. The format is pinned in <c>AudioTests.Wav_RoundTrips_Float32</c> and read by
/// the viewer's <c>wav_io</c>; PCM16 would clamp the normalized mixes a second time, which is why
/// the converter keeps IEEE float end-to-end.
/// </summary>
public static class WavWriter
{
    private const int HeaderLength = 44;
    private const int FmtChunkLength = 16;
    private const int FloatBytes = 4;
    private const ushort IeeeFloat = 3;

    /// <summary>
    /// Writes <paramref name="samples"/> (interleaved) to <paramref name="path"/>, creating the
    /// parent directory if needed.
    /// </summary>
    /// <param name="path">Destination <c>.wav</c>.</param>
    /// <param name="samples">Interleaved float samples; length must be a multiple of <paramref name="channels"/>.</param>
    /// <param name="channels">1 (mono) or 2 (stereo).</param>
    /// <param name="rate">Sample rate in Hz; the converter always writes 48 000.</param>
    /// <exception cref="ArgumentException">
    /// An empty path, no samples, a length that is not whole frames, or a rate that does not
    /// fit the RIFF fields.
    /// </exception>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="channels"/> is not 1 or 2.</exception>
    public static void Write(string path, ReadOnlySpan<float> samples, int channels, int rate)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        if (channels is not (1 or 2))
            throw new ArgumentOutOfRangeException(
                nameof(channels), channels, "the converter writes mono or stereo only");
        if (rate <= 0)
            throw new ArgumentOutOfRangeException(nameof(rate), rate, "rate must be positive");
        if (samples.Length == 0)
            throw new ArgumentException("no samples to write", nameof(samples));
        if (samples.Length % channels != 0)
            throw new ArgumentException(
                $"{samples.Length} samples do not form whole {channels}-channel frames", nameof(samples));

        var dataLength = checked(samples.Length * FloatBytes);
        var riffLength = HeaderLength - 8L + dataLength;
        if (riffLength > uint.MaxValue)
            throw new ArgumentException($"data is too long for a RIFF file ({dataLength} B)", nameof(samples));

        var directory = Path.GetDirectoryName(Path.GetFullPath(path));
        if (!string.IsNullOrEmpty(directory))
            Directory.CreateDirectory(directory);

        using var stream = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.None);
        using var writer = new BinaryWriter(stream, Encoding.ASCII, leaveOpen: false);

        writer.Write("RIFF"u8);
        writer.Write((uint)riffLength);
        writer.Write("WAVE"u8);
        writer.Write("fmt "u8);
        writer.Write((uint)FmtChunkLength);
        writer.Write(IeeeFloat);
        writer.Write((ushort)channels);
        writer.Write((uint)rate);
        writer.Write((uint)(rate * channels * FloatBytes)); // byte rate
        writer.Write((ushort)(channels * FloatBytes)); // block align
        writer.Write((ushort)(FloatBytes * 8)); // bits per sample
        writer.Write("data"u8);
        writer.Write((uint)dataLength);

        foreach (var sample in samples)
            writer.Write(sample);
    }
}
