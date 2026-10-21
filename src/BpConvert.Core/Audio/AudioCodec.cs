using System.Buffers.Binary;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace BpConvert.Core.Audio;

/// <summary>
/// One decoded clip: interleaved float samples (a frame is <see cref="Channels"/> consecutive
/// samples) at <see cref="Rate"/> Hz.
/// </summary>
/// <param name="Samples">
/// Interleaved samples in <c>[-1, 1]</c> after <see cref="AudioCodec.Decode"/> normalizes; the
/// raw path (<c>normalize: false</c>) may overshoot the way an AT9 overlap-add does.
/// </param>
/// <param name="Channels">1 or 2: 5.1 sources are downmixed at decode time (ruling R6).</param>
/// <param name="Rate">Sample rate in Hz (48 000 on every shipped wave).</param>
public sealed record DecodedAudio(float[] Samples, int Channels, int Rate)
{
    /// <summary>
    /// The attenuation <see cref="AudioCodec.Normalize"/> applied to the peak, in dB (0 when the
    /// clip was already at/below the ceiling). The manifest records it as <c>gain_db</c>.
    /// </summary>
    public double GainDb { get; init; }

    /// <summary>Per-channel frame count.</summary>
    public int Frames => Channels > 0 ? Samples.Length / Channels : 0;

    /// <summary>Measured duration in seconds.</summary>
    public double Seconds => Rate > 0 ? (double)Frames / Rate : 0;

    /// <summary>The absolute sample peak.</summary>
    public float Peak
    {
        get
        {
            var peak = 0f;
            foreach (var sample in Samples)
            {
                var value = MathF.Abs(sample);
                if (value > peak)
                    peak = value;
            }

            return peak;
        }
    }

    /// <summary>The root-mean-square across every interleaved sample.</summary>
    public double Rms
    {
        get
        {
            if (Samples.Length == 0)
                return 0;
            double sum = 0;
            foreach (var sample in Samples)
                sum += (double)sample * sample;
            return Math.Sqrt(sum / Samples.Length);
        }
    }
}

/// <summary>
/// The one decode entry point for the S5 audio slice (recon §4-§8, the C# counterpart of the
/// approved <c>s5_decode_gate.py</c> paths):
/// <list type="bullet">
/// <item><c>0x21</c> (and its <c>0x00FF____</c> forms) — PS-ADPCM, 16-byte blocks of 28
/// samples, the gate's 8-filter predictor table (filters 5-7 fitted, see <see cref="VagCoefficients"/>).</item>
/// <item><c>0x42</c>/<c>0x0142</c>/<c>0x0242</c>/<c>0x0642</c> — ATRAC9 through <c>ffmpeg</c>
/// with the record's own big-endian config word as extradata.</item>
/// </list>
/// A 6-channel clip is downmixed to stereo and every normalized clip peaks at ≤ 0.99. A rejected
/// format/config or a non-finite sample throws <see cref="InvalidDataException"/>; there is no
/// silent fallback.
/// </summary>
/// <remarks>
/// <para>
/// <c>ffmpeg</c> (with its built-in <c>atrac9</c> decoder) is a converter-time subprocess, never
/// a runtime dependency of the viewer. Override its path with <c>FFMPEG</c>; the default is
/// <c>ffmpeg</c> on <c>PATH</c>. The wrapper is the gate's exact
/// <c>WAVEFORMATEXTENSIBLE</c> layout: AT9 GUID, <c>block_align = frame_bytes &lt;&lt;
/// superframe</c>, and 12 bytes of extradata (<c>LE32 0</c> + the 4 config bytes + 4 zeros).
/// </para>
/// <para>
/// The PS-ADPCM predictor table is the standard VAG table for filters 0-4 plus the coefficients
/// fitted in Task 1 for filters 5-7 (they live in the NGS2 firmware, not in <c>eboot.elf</c>).
/// The fitted entries are a documented fidelity compromise, not a firmware proof: the user's ear
/// is the final identity/fidelity check for the <c>0x21</c> cues (recon §7.2).
/// </para>
/// </remarks>
public static class AudioCodec
{
    /// <summary>The normalized peak ceiling (ruling R6: the mixer can never clip).</summary>
    public const double PeakCeiling = 0.99;

    /// <summary>The standard VAG predictor table plus the fitted filters 5-7 (recon §7.2).</summary>
    public static readonly IReadOnlyList<(int C1, int C2)> VagCoefficients = new (int, int)[]
    {
        (0, 0), (60, 0), (115, -52), (98, -55), (122, -60),
        (126, -62), (128, -64), (126, -63),
    };

    /// <summary>AT9 <c>fmt</c>-chunk GUID (<c>d242e147-ba36-8d4d-88fc-61654f8c836c</c>).</summary>
    private static readonly byte[] At9Guid =
        Convert.FromHexString("d242e147ba368d4d88fc61654f8c836c");

    private const int PsAdpcmBlockBytes = 16;
    private const int PsAdpcmSamplesPerBlock = 28;
    private const int PsAdpcmCoefficientShift = 6;
    private const float DownmixCenter = 0.707f;
    private const float DownmixSurround = 0.5f;
    private const int FfmpegTimeoutMs = 120_000;

    /// <summary>
    /// Decodes one carved wave payload.
    /// </summary>
    /// <param name="payload">Exactly the record's <c>+0x20</c> byte range (no padding, no header).</param>
    /// <param name="formatWord">
    /// The record's <c>+0x04</c> word: low byte = codec id, byte 1 = channel count.
    /// </param>
    /// <param name="codecConfig">
    /// The record's AT9 config word (<c>WaveRecord.CodecConfig</c>); required for the <c>0x42</c>
    /// family, ignored by PS-ADPCM.
    /// </param>
    /// <param name="sampleRate">The record's <c>+0x08</c> rate (48 000 on every shipped wave).</param>
    /// <param name="normalize">
    /// True (the public contract) scales the clip to peak ≤ 0.99 and records the gain. The audio
    /// verb passes false so that layers are mixed at their decoded level and normalized once.
    /// </param>
    /// <exception cref="InvalidDataException">
    /// The format word names an unsupported codec/channel count, the AT9 config is missing or
    /// disagrees with the record, <c>ffmpeg</c> fails, or the decoded PCM is empty/non-finite.
    /// </exception>
    public static DecodedAudio Decode(
        ReadOnlySpan<byte> payload, uint formatWord, uint codecConfig, uint sampleRate,
        bool normalize = true)
    {
        if (payload.Length == 0)
            throw Reject("empty payload");
        if (sampleRate == 0 || sampleRate > int.MaxValue)
            throw Reject($"sample rate {sampleRate} is not a valid rate");

        var codec = formatWord & 0xFF;
        var channels = (int)((formatWord >> 8) & 0xFF);
        var rate = (int)sampleRate;

        var decoded = codec switch
        {
            0x21 => DecodePsAdpcm(payload, channels, rate),
            0x42 => DecodeAt9(payload, channels, rate, codecConfig),
            _ => throw Reject($"format 0x{formatWord:X8} has codec 0x{codec:X2}, which has no decoder"),
        };

        ValidateFinite(decoded);
        return normalize ? Normalize(decoded) : decoded;
    }

    /// <summary>
    /// Scales a clip to peak ≤ <see cref="PeakCeiling"/> and records the applied gain in dB. A
    /// clip already at/below the ceiling is returned unchanged with <c>GainDb = 0</c>. The scaled
    /// output is clamped to [-1, 1] as a final safety, which is a no-op after the ceiling scale.
    /// </summary>
    /// <exception cref="InvalidDataException">The clip contains a NaN/Inf sample.</exception>
    public static DecodedAudio Normalize(DecodedAudio audio)
    {
        ArgumentNullException.ThrowIfNull(audio);
        ValidateFinite(audio);

        var peak = audio.Peak;
        if (peak <= (float)PeakCeiling)
            return audio with { GainDb = 0 };

        var gain = PeakCeiling / peak;
        var samples = new float[audio.Samples.Length];
        for (var i = 0; i < samples.Length; i++)
            samples[i] = Math.Clamp((float)(audio.Samples[i] * gain), -1f, 1f);

        return audio with { Samples = samples, GainDb = 20.0 * Math.Log10(gain) };
    }

    /// <summary>
    /// PS-ADPCM: 16-byte blocks (1 header byte <c>shift|filter&lt;&lt;4</c>, 1 flags byte, 14
    /// nibble bytes → 28 samples). Stereo banks alternate the channels block by block; each
    /// channel keeps its own predictor state. Exact port of the gate's <c>ps_adpcm_decode</c>.
    /// </summary>
    private static DecodedAudio DecodePsAdpcm(ReadOnlySpan<byte> data, int channels, int rate)
    {
        if (channels is not (1 or 2))
            throw Reject($"PS-ADPCM supports 1 or 2 channels, the record says {channels}");

        var blocks = data.Length / PsAdpcmBlockBytes;
        var framesPerChannel = blocks / channels;
        if (framesPerChannel == 0)
            throw Reject(
                $"PS-ADPCM payload {data.Length} B holds no complete {channels}-channel block");

        var frames = framesPerChannel * PsAdpcmSamplesPerBlock;
        var samples = new float[frames * channels];
        var previous1 = new int[channels];
        var previous2 = new int[channels];

        for (var block = 0; block < framesPerChannel * channels; block++)
        {
            var offset = block * PsAdpcmBlockBytes;
            var header = data[offset];
            var shift = header & 0x0F;
            var filter = (header >> 4) & 0x07;
            var (c1, c2) = VagCoefficients[filter];
            var channel = block % channels;
            var s1 = previous1[channel];
            var s2 = previous2[channel];
            var frame = (block / channels) * PsAdpcmSamplesPerBlock;

            for (var i = 2; i < PsAdpcmBlockBytes; i++)
            {
                var value = data[offset + i];
                for (var half = 0; half < 2; half++)
                {
                    var nibble = half == 0 ? value & 0x0F : value >> 4;
                    var v = nibble < 8 ? nibble << 12 : (nibble - 16) << 12;
                    v >>= shift;
                    var s = v + ((s1 * c1 + s2 * c2) >> PsAdpcmCoefficientShift);
                    s = s < short.MinValue ? short.MinValue : (s > short.MaxValue ? short.MaxValue : s);
                    s2 = s1;
                    s1 = s;
                    samples[(frame++) * channels + channel] = s / 32768f;
                }
            }

            previous1[channel] = s1;
            previous2[channel] = s2;
        }

        return new DecodedAudio(samples, channels, rate);
    }

    /// <summary>
    /// ATRAC9 through <c>ffmpeg</c>, using the record's config word as extradata (recon §4.3).
    /// </summary>
    private static DecodedAudio DecodeAt9(
        ReadOnlySpan<byte> data, int channels, int rate, uint codecConfig)
    {
        if (codecConfig == 0)
            throw Reject("ATRAC9 record has no config word");
        if (codecConfig >> 24 != 0xFE)
            throw Reject($"ATRAC9 config 0x{codecConfig:X8} does not start with 0xFE");

        var sampleRateIndex = (int)((codecConfig >> 20) & 0x0F);
        var blockConfig = (int)((codecConfig >> 17) & 0x07);
        var frameBytes = (int)((codecConfig >> 5) & 0x7FF) + 1;
        var superframe = (int)((codecConfig >> 3) & 0x03);

        if (sampleRateIndex != 7 || rate != 48000)
            throw Reject(
                $"ATRAC9 config 0x{codecConfig:X8} is sri {sampleRateIndex}, but the record is {rate} Hz (only 48 kHz/sri 7 is shipped)");
        var configChannels = blockConfig switch
        {
            0 => 1,
            1 or 2 => 2,
            3 => 6,
            4 => 8,
            5 => 4,
            _ => 0,
        };
        if (configChannels != channels)
            throw Reject(
                $"ATRAC9 config 0x{codecConfig:X8} is {configChannels}ch, but the record says {channels}ch");
        if (superframe is not (0 or 2))
            throw Reject($"ATRAC9 config 0x{codecConfig:X8} has superframe index {superframe}");
        if (channels is not (1 or 2 or 6))
            throw Reject($"ATRAC9 supports 1, 2 or 6 channels here, the record says {channels}");

        var wrapper = BuildAt9Wrapper(data, channels, rate, frameBytes << superframe, codecConfig);
        var directory = Path.Combine(
            Path.GetTempPath(), $"bpconvert-at9-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        try
        {
            var input = Path.Combine(directory, "carved.at9.wav");
            var raw = Path.Combine(directory, "decoded.f32");
            File.WriteAllBytes(input, wrapper);

            RunFfmpeg(input, raw, channels);
            var pcm = ReadFloats(raw, channels);
            var decoded = new DecodedAudio(pcm, channels, rate);
            if (channels == 6)
                decoded = DownmixToStereo(decoded);
            return decoded;
        }
        finally
        {
            try
            {
                Directory.Delete(directory, true);
            }
            catch (IOException)
            {
                // A leftover temp file is not a decode failure.
            }
            catch (UnauthorizedAccessException)
            {
            }
        }
    }

    /// <summary>
    /// The gate's <c>at9_riff_with_config</c>: 52-byte <c>WAVEFORMATEXTENSIBLE</c> with the AT9
    /// GUID, mask, and 12-byte extradata, followed by one <c>data</c> chunk.
    /// </summary>
    private static byte[] BuildAt9Wrapper(
        ReadOnlySpan<byte> payload, int channels, int rate, int blockAlign, uint codecConfig)
    {
        var mask = channels switch
        {
            1 => 0x4u,
            2 => 0x3u,
            4 => 0x33u,
            6 => 0x3Fu,
            8 => 0x63Fu,
            _ => throw Reject($"no WAVEFORMATEXTENSIBLE mask for {channels} channels"),
        };

        const int extraDataLength = 12;
        const int formatLength = 52;
        var body = new byte[4 + 8 + formatLength + 8 + payload.Length];
        var span = body.AsSpan();

        "WAVE"u8.CopyTo(span);
        "fmt "u8.CopyTo(span[4..]);
        BinaryPrimitives.WriteUInt32LittleEndian(span[8..], formatLength);

        var fmt = span[12..];
        BinaryPrimitives.WriteUInt16LittleEndian(fmt, 0xFFFE); // WAVE_FORMAT_EXTENSIBLE
        BinaryPrimitives.WriteUInt16LittleEndian(fmt[2..], (ushort)channels);
        BinaryPrimitives.WriteUInt32LittleEndian(fmt[4..], (uint)rate);
        BinaryPrimitives.WriteUInt32LittleEndian(fmt[8..], 0); // byte rate is unused by AT9
        BinaryPrimitives.WriteUInt16LittleEndian(fmt[12..], (ushort)blockAlign);
        BinaryPrimitives.WriteUInt16LittleEndian(fmt[14..], 0); // bits per sample
        BinaryPrimitives.WriteUInt16LittleEndian(fmt[16..], 22 + extraDataLength);
        BinaryPrimitives.WriteUInt16LittleEndian(fmt[18..], 0); // valid bits
        BinaryPrimitives.WriteUInt32LittleEndian(fmt[20..], mask);
        At9Guid.CopyTo(fmt[24..]);
        // extradata: LE32 version 0 | 4 big-endian config bytes | 4 zero bytes
        BinaryPrimitives.WriteUInt32LittleEndian(fmt[40..], 0);
        BinaryPrimitives.WriteUInt32BigEndian(fmt[44..], codecConfig);
        BinaryPrimitives.WriteUInt32LittleEndian(fmt[48..], 0);

        var at = 12 + formatLength;
        "data"u8.CopyTo(span[at..]);
        BinaryPrimitives.WriteUInt32LittleEndian(span[(at + 4)..], (uint)payload.Length);
        payload.CopyTo(span[(at + 8)..]);

        // "RIFF" + the size of everything after it (the body is "WAVE" onward).
        var riff = new byte[8 + body.Length];
        "RIFF"u8.CopyTo(riff);
        BinaryPrimitives.WriteUInt32LittleEndian(riff.AsSpan(4), (uint)body.Length);
        body.CopyTo(riff, 8);
        return riff;
    }

    /// <summary>Runs <c>ffmpeg</c> over the wrapper and leaves raw little-endian f32 samples.</summary>
    private static void RunFfmpeg(string input, string output, int channels)
    {
        var executable = Environment.GetEnvironmentVariable("FFMPEG") ?? "ffmpeg";
        var start = new ProcessStartInfo
        {
            FileName = executable,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
        };
        foreach (var argument in new[]
                 {
                     "-hide_banner", "-v", "error", "-y", "-i", input,
                     "-map", "0:a", "-c:a", "pcm_f32le", "-f", "f32le", output,
                 })
            start.ArgumentList.Add(argument);

        Process process;
        try
        {
            process = Process.Start(start)
                ?? throw new InvalidDataException("ffmpeg did not start");
        }
        catch (Win32Exception e)
        {
            throw Reject(
                $"ATRAC9 needs '{executable}' on PATH (or FFMPEG); {e.Message}");
        }
        catch (InvalidOperationException e)
        {
            throw Reject($"ATRAC9 needs ffmpeg: {e.Message}");
        }

        using (process)
        {
            var error = process.StandardError.ReadToEndAsync();
            process.StandardOutput.ReadToEnd(); // drain so the pipe cannot fill
            if (!process.WaitForExit(FfmpegTimeoutMs))
            {
                try
                {
                    process.Kill(entireProcessTree: true);
                }
                catch (InvalidOperationException)
                {
                }

                throw Reject($"ffmpeg timed out after {FfmpegTimeoutMs / 1000} s");
            }

            var stderr = error.GetAwaiter().GetResult().Trim();
            if (process.ExitCode != 0)
                throw Reject($"ffmpeg failed ({process.ExitCode}): {Tail(stderr)}");
        }

        if (!File.Exists(output))
            throw Reject("ffmpeg produced no PCM");
    }

    /// <summary>Reads the raw f32le file and rejects a partial frame.</summary>
    private static float[] ReadFloats(string path, int channels)
    {
        var bytes = File.ReadAllBytes(path);
        if (bytes.Length == 0)
            throw Reject("ffmpeg produced an empty PCM stream");
        if (bytes.Length % (4 * channels) != 0)
            throw Reject(
                $"ffmpeg produced {bytes.Length} B, not whole {channels}-channel frames");

        var samples = new float[bytes.Length / 4];
        if (BitConverter.IsLittleEndian)
        {
            MemoryMarshal.Cast<byte, float>(bytes.AsSpan()).CopyTo(samples);
        }
        else
        {
            for (var i = 0; i < samples.Length; i++)
                samples[i] = BinaryPrimitives.ReadSingleLittleEndian(bytes.AsSpan(4 * i));
        }

        return samples;
    }

    /// <summary>
    /// 5.1 → stereo (ruling R6): <c>L' = L + 0.707·C + 0.5·LFE + 0.5·SL</c>,
    /// <c>R' = R + 0.707·C + 0.5·LFE + 0.5·SR</c>. The channel order is the shipped AT9 5.1
    /// layout (FL FR FC LFE SL SR).
    /// </summary>
    /// <remarks>
    /// The sum is left unclipped on purpose. Every music master overshoots unity (recon §8) and
    /// clamping the sum at [-1, 1] here hard-clips 2.36 % of Boss 1's stereo samples before
    /// <see cref="Normalize"/> can attenuate them, which is exactly the distortion R6 exists to
    /// avoid. <see cref="Normalize"/> brings the summed clip to peak 0.99 and clamps the final
    /// output, so the delivered WAV is in range without pre-normalization clipping.
    /// </remarks>
    private static DecodedAudio DownmixToStereo(DecodedAudio audio)
    {
        var frames = audio.Frames;
        var source = audio.Samples;
        var stereo = new float[frames * 2];
        for (var frame = 0; frame < frames; frame++)
        {
            var at = frame * 6;
            stereo[frame * 2] = source[at] + DownmixCenter * source[at + 2]
                + DownmixSurround * source[at + 3] + DownmixSurround * source[at + 4];
            stereo[frame * 2 + 1] = source[at + 1] + DownmixCenter * source[at + 2]
                + DownmixSurround * source[at + 3] + DownmixSurround * source[at + 5];
        }

        return new DecodedAudio(stereo, 2, audio.Rate);
    }

    private static void ValidateFinite(DecodedAudio audio)
    {
        if (audio.Channels is not (1 or 2))
            throw Reject($"{audio.Channels} channels reached the output (only mono/stereo exist)");
        if (audio.Rate <= 0)
            throw Reject($"output rate {audio.Rate} Hz is not positive");
        if (audio.Samples.Length % audio.Channels != 0)
            throw Reject(
                $"{audio.Samples.Length} samples do not form whole {audio.Channels}-channel frames");

        for (var i = 0; i < audio.Samples.Length; i++)
        {
            if (!float.IsFinite(audio.Samples[i]))
                throw Reject($"non-finite sample at frame {i / audio.Channels}, channel {i % audio.Channels}");
        }
    }

    private static string Tail(string text)
    {
        const int limit = 400;
        return text.Length <= limit ? text : text[^limit..];
    }

    private static InvalidDataException Reject(string detail) => new($"audio decode: {detail}");
}
