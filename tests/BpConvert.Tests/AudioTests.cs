using System.Buffers.Binary;
using System.Text;
using BpConvert.Core.Audio;

/// <summary>
/// Pinned cases for the SXDF container and its index tables (S5 Task 2;
/// <c>tools/re/audio/s5_decode_gate.py</c> and <c>docs/notes/s5-audio-recon.md</c> are the
/// parsing reference): the 0x60-byte header, the chunk chain from <c>0x60</c>, the shared
/// <c>count/offsets[]</c> table (<c>record_i = body + offsets[i] + 4*i</c>, never a fixed
/// stride), the absolute music starts, and the loader-derived resident payload formula
/// <c>record + 0x24 + start</c> (recon §7.1). Task 3 adds the codec-config field, the WAV
/// writer, the R1 manifest, and the decode/mix pipeline. Real-tree cases are env-gated
/// through <see cref="TestAssets"/> (fail fast when <c>SOTC_GAME_ROOT</c> is unset — never
/// skips); the AT9 cases need <c>ffmpeg</c> with its built-in <c>atrac9</c> decoder, exactly
/// like the gate.
/// </summary>
public class AudioTests
{
    private static string Bank(string fileName) =>
        TestAssets.Sample(Path.Combine("sound", "shadow", fileName));

    [Fact]
    public void Read_Boss01_HasTheChunkChainAndData()
    {
        var path = Bank("boss01.sxd");
        var bank = SxdContainer.Read(path);

        Assert.Equal("boss01", bank.Name);
        Assert.Equal(4, bank.Version);
        Assert.Equal("3817adac981f914ca9b4146794f91bb1", bank.HashHex);

        // The chunk chain opens with TONE at 0x60 and every table the reader consumes is present.
        Assert.Equal("TONE", bank.Chunks[0].Magic);
        Assert.Equal(0x60, bank.Chunks[0].Offset);
        foreach (var magic in new[] { "REQD", "NAME", "WAVE", "DATA" })
            Assert.Contains(bank.Chunks, chunk => chunk.Magic == magic);

        var data = Assert.Single(bank.Chunks, chunk => chunk.Magic == "DATA");
        Assert.Equal(0x14f10, data.Offset);
        Assert.True(bank.HasData);
        Assert.Equal(data.Offset, bank.DataOffset);
        Assert.Equal(data.Size, bank.DataSize);

        // DATA is the last chunk and reaches EOF. `size` counts the body only, so the file end
        // is offset + 8-byte chunk header + size (the gate's `DATA + 8 + size`, recon §3/§7).
        Assert.Equal(new FileInfo(path).Length, bank.DataOffset + 8 + bank.DataSize);
        var last = bank.Chunks[^1];
        Assert.Equal(new FileInfo(path).Length, last.Offset + 8 + last.Size);
    }

    [Fact]
    public void Read_MusicSxd1_HasNoData_AndMatchesItsSxd2Hash()
    {
        var index = SxdContainer.Read(Bank("music.sxd1"));
        var payload = SxdContainer.Read(Bank("music.sxd2"));

        Assert.DoesNotContain(index.Chunks, chunk => chunk.Magic == "DATA");
        Assert.False(index.HasData);
        Assert.Equal(0, index.DataSize);

        // The GUID is identical inside the pair, so the index's hash is the payload's hash.
        Assert.Equal(payload.HashHex, index.HashHex);
        Assert.Equal(32, index.HashHex.Length);
        Assert.Equal(new FileInfo(Bank("music.sxd2")).Length, index.CompanionSize);
        Assert.Equal(0xa461d20L, index.CompanionSize);

        // An SXDS is the stream body itself: no chunk chain, the payload starts at 0x60.
        Assert.Empty(payload.Chunks);
        Assert.Equal(0x60, payload.DataOffset);
        Assert.Equal(new FileInfo(Bank("music.sxd2")).Length, payload.DataOffset + payload.DataSize);
    }

    [Fact]
    public void Waves_ResidentFormula_TilesBoss01()
    {
        var bank = SxdContainer.Read(Bank("boss01.sxd"));
        var waves = SxdTables.Waves(bank);

        Assert.Equal(138, waves.Count);
        // The table's offsets[] absorb the stride: the real records are 48/56/68/76 bytes.
        Assert.Equal(new[] { 48, 56, 68, 76 },
            waves.Select(wave => wave.RecordSize).Distinct().OrderBy(size => size).ToArray());
        for (var i = 0; i < waves.Count; i++)
        {
            Assert.Equal(i, waves[i].Index);
            Assert.InRange(waves[i].RecordSize, 0x28, 0x100);
        }

        // The fixed field slots: 48 kHz and the four codec/channel words of the bank.
        Assert.All(waves, wave => Assert.Equal(48000u, wave.SampleRate));
        Assert.Equal(new[] { 0x00FF0121u, 0x00FF0142u, 0x00FF0221u, 0x00FF0242u },
            waves.Select(wave => wave.FormatWord).Distinct().OrderBy(word => word).ToArray());

        // The loader formula (recon §7.1): payload = record + 0x24 + start, length = bytes.
        var payloadStart = bank.DataOffset + 0x10;
        var payloadEnd = bank.DataOffset + 8 + bank.DataSize;
        Assert.Equal((payloadStart, payloadEnd), MergedUnion(waves
            .Select(wave => (Offset: wave.RecordOffset + 0x24 + wave.Start, End: wave.RecordOffset + 0x24 + wave.Start + wave.ByteLength))));

        // No duplicates on the union: the ranges cover the payload with zero gaps.
        Assert.Equal(payloadEnd - payloadStart, waves
            .Select(wave => (wave.RecordOffset + 0x24 + wave.Start, wave.ByteLength))
            .Distinct()
            .Sum(range => (long)range.ByteLength));
    }

    [Fact]
    public void Waves_Music_StartsAreAbsolute()
    {
        var bank = SxdContainer.Read(Bank("music.sxd1"));
        var waves = SxdTables.Waves(bank);

        Assert.Equal(33, waves.Count);
        Assert.All(waves, wave => Assert.Equal(76, wave.RecordSize));
        Assert.All(waves, wave => Assert.Equal(48000u, wave.SampleRate));

        // `start` is an absolute byte offset in music.sxd2, not relative to the payload base.
        var ordered = waves.OrderBy(wave => wave.Start).ToList();
        Assert.Equal(0x60, ordered[0].Start);
        for (var i = 0; i + 1 < ordered.Count; i++)
            Assert.Equal(ordered[i].Start + ordered[i].ByteLength, ordered[i + 1].Start);

        // 960-byte ATRAC9 superframes (4 frames of 240 B / 256 samples), and the ranges tile
        // the whole 0x60 .. companion-size payload.
        Assert.All(waves, wave => Assert.Equal(0, wave.ByteLength % 960));
        Assert.Equal(bank.CompanionSize, ordered[^1].Start + ordered[^1].ByteLength);
    }

    [Fact]
    public void Waves_ComSys_PartitionTheDataPayload()
    {
        var bank = SxdContainer.Read(Bank("com_sys.sxd"));
        var waves = SxdTables.Waves(bank);

        Assert.Equal(16, waves.Count);
        var payloadStart = bank.DataOffset + 0x10;
        var payloadEnd = bank.DataOffset + 8 + bank.DataSize;
        Assert.Equal(16, waves.Select(wave => wave.Start).Distinct().Count());
        Assert.Equal((payloadStart, payloadEnd), MergedUnion(waves
            .Select(wave => (Offset: wave.RecordOffset + 0x24 + wave.Start, End: wave.RecordOffset + 0x24 + wave.Start + wave.ByteLength))));

        // com_sys has no duplicate ranges, so the declared lengths sum to the payload exactly.
        Assert.Equal(bank.DataSize - 8, waves.Sum(wave => wave.ByteLength));
    }

    [Fact]
    public void Waves_ComObj_PartitionTheDataPayload()
    {
        // The curated player bank (swing/hit cues), 241 records with 80/264-byte tail records:
        // the same loader formula must tile it with zero gaps, duplicates and all.
        var bank = SxdContainer.Read(Bank("com_obj.sxd"));
        var waves = SxdTables.Waves(bank);

        Assert.Equal(241, waves.Count);
        var payloadStart = bank.DataOffset + 0x10;
        var payloadEnd = bank.DataOffset + 8 + bank.DataSize;
        Assert.Equal((payloadStart, payloadEnd), MergedUnion(waves
            .Select(wave => (Offset: wave.RecordOffset + 0x24 + wave.Start, End: wave.RecordOffset + 0x24 + wave.Start + wave.ByteLength))));
    }

    [Fact]
    public void Names_Boss01_AreReadableAndUnique()
    {
        var names = SxdTables.Names(SxdContainer.Read(Bank("boss01.sxd")));

        Assert.Equal(343, names.Count);
        for (var i = 0; i < names.Count; i++)
        {
            Assert.Equal(i, names[i].Index);
            Assert.NotEmpty(names[i].Name);
            Assert.All(names[i].Name, c => Assert.InRange(c, (char)0x20, (char)0x7E));
        }

        Assert.Equal(names.Count, names.Select(name => name.Name).Distinct(StringComparer.Ordinal).Count());
        Assert.Contains("boss01", names[0].Name);
        // Entry 0 is boss01_atk_hit_ground1; its third word is the REQD key (303), not the entry
        // index and not a WAVE index (attribution: REQD 303 -> tone 43 -> wave 26).
        Assert.Equal("boss01_atk_hit_ground1", names[0].Name);
        Assert.Equal(0, names[0].Index);
        Assert.Equal(303u, names[0].ReqdId);
    }

    [Fact]
    public void Read_Truncated_Throws()
    {
        var source = Bank("boss01.sxd");
        var path = Path.Combine(Path.GetTempPath(), $"bpconvert-sxd-{Guid.NewGuid():N}.sxd");
        try
        {
            File.WriteAllBytes(path, File.ReadAllBytes(source)[..0x80]);

            Assert.Throws<InvalidDataException>(() => SxdContainer.Read(path));
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void Read_ChunkBodyOverrun_Throws()
    {
        // Patch the +0x08 own-size field to the truncated length so that guard passes: the chunk
        // walk itself must catch the TONE body running past EOF.
        var path = Path.Combine(Path.GetTempPath(), $"bpconvert-sxd-{Guid.NewGuid():N}.sxd");
        try
        {
            var bytes = File.ReadAllBytes(Bank("boss01.sxd"))[..0x80];
            BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(0x08), (uint)bytes.Length);
            File.WriteAllBytes(path, bytes);

            var error = Assert.Throws<InvalidDataException>(() => SxdContainer.Read(path));
            Assert.Contains("overruns the file", error.Message);
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void Read_Bridge_AcceptsToneOnly()
    {
        var bank = SxdContainer.Read(Bank("bridge.sxd"));

        var chunk = Assert.Single(bank.Chunks);
        Assert.Equal("TONE", chunk.Magic);
        Assert.Equal(0x60, chunk.Offset);
        Assert.False(bank.HasData);
        Assert.Equal(0, bank.DataOffset);
        Assert.Equal(0, bank.DataSize);
        Assert.Empty(SxdTables.Waves(bank)); // no WAVE table in this bank
    }

    [Fact]
    public void Names_FallBackToThePool_WhenTheEntryStrideDoesNotResolve()
    {
        // A synthetic NAME table whose 12-byte entries carry a zero string offset: the entry
        // layout cannot resolve (the "name" would start on its own offset word), so the reader
        // must read the NUL-terminated pool after the entries and keep the entry order.
        var path = Path.Combine(Path.GetTempPath(), $"bpconvert-name-{Guid.NewGuid():N}.sxd");
        try
        {
            File.WriteAllBytes(path, BuildNameOnlyBank("alpha\0beta\0", new uint[] { 7, 9 }));

            var names = SxdTables.Names(SxdContainer.Read(path));

            Assert.Equal(2, names.Count);
            Assert.Equal("alpha", names[0].Name);
            Assert.Equal(7u, names[0].ReqdId);
            Assert.Equal("beta", names[1].Name);
            Assert.Equal(9u, names[1].ReqdId);
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void Waves_CodecConfig_Pinned()
    {
        // Music records carry the AT9 config word verbatim at +0x40 (recon §2); resident 0x42
        // records carry it at +0x2C (56-byte) or +0x40 (76-byte) (recon §5.0/§7.3); the 0x21
        // PS-ADPCM family has no AT9 config and reads 0.
        var music = SxdTables.Waves(SxdContainer.Read(Bank("music.sxd1")));
        Assert.All(music, wave => Assert.Equal(0xFE761DF0u, wave.CodecConfig));

        var boss = SxdTables.Waves(SxdContainer.Read(Bank("boss01.sxd")));
        Assert.All(boss.Where(wave => (wave.FormatWord & 0xFF) == 0x21),
            wave => Assert.Equal(0u, wave.CodecConfig));
        Assert.All(boss.Where(wave => wave.FormatWord == 0x00FF0242),
            wave => Assert.Equal(0xFE740FE0u, wave.CodecConfig));
        Assert.All(boss.Where(wave => wave.FormatWord == 0x00FF0142),
            wave => Assert.Equal(0xFE7007E0u, wave.CodecConfig));

        // Both record strides appear on the 0x42 family, and both carry the same config.
        Assert.Contains(boss, wave => wave.FormatWord == 0x00FF0242 && wave.RecordSize == 56);
        Assert.Contains(boss, wave => wave.FormatWord == 0x00FF0242 && wave.RecordSize == 76);
    }

    [Fact]
    public void Wav_RoundTrips_Float32()
    {
        var dir = TempDir();
        try
        {
            var path = Path.Combine(dir, "tone.wav");
            var samples = new float[480]; // 240 stereo frames
            for (var i = 0; i < samples.Length; i++)
                samples[i] = (float)(Math.Sin(2 * Math.PI * 1000 * i / 48000.0) * 0.5);

            WavWriter.Write(path, samples, 2, 48000);

            var bytes = File.ReadAllBytes(path);
            Assert.Equal("RIFF", Encoding.ASCII.GetString(bytes, 0, 4));
            Assert.Equal((uint)(bytes.Length - 8), BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(4)));
            Assert.Equal("WAVE", Encoding.ASCII.GetString(bytes, 8, 4));
            Assert.Equal("fmt ", Encoding.ASCII.GetString(bytes, 12, 4));
            Assert.Equal(16u, BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(16)));
            Assert.Equal(3, BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(20))); // IEEE float
            Assert.Equal(2, BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(22)));
            Assert.Equal(48000u, BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(24)));
            Assert.Equal(48000u * 2 * 4, BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(28)));
            Assert.Equal(8, BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(32)));
            Assert.Equal(32, BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(34)));
            Assert.Equal("data", Encoding.ASCII.GetString(bytes, 36, 4));
            Assert.Equal(1920u, BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(40)));
            Assert.Equal(44 + 1920, bytes.Length);

            var (channels, rate, frames, roundTrip) = ReadFloatWav(path);
            Assert.Equal(2, channels);
            Assert.Equal(48000, rate);
            Assert.Equal(240, frames);
            Assert.Equal(samples, roundTrip);
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }

    [Fact]
    public void Manifest_RoundTrips_StableSorted()
    {
        var dir = TempDir();
        try
        {
            var path = Path.Combine(dir, "audio.manifest");
            var music = new AudioEntry("music", "music.boss", "audio/music/music.boss.wav",
                48000, 2, 6044367, 125.924312, "at9", true, -12.3456);
            var impact = new AudioEntry("boss01", "valus.impact", "audio/boss01/valus.impact.wav",
                48000, 2, 362068, 7.543083, "psadpcm", false, 0);

            // Written in reverse bank order: the file is sorted by (bank, name).
            Assert.Equal(path, AudioManifest.Write(path, new[] { music, impact }));
            var lines = File.ReadAllLines(path);
            Assert.Equal("version=1", lines[0]);
            Assert.StartsWith("name=valus.impact bank=boss01 file=audio/boss01/valus.impact.wav",
                lines[1]);
            Assert.StartsWith("name=music.boss bank=music file=audio/music/music.boss.wav",
                lines[2]);
            Assert.Contains("codec=psadpcm loop=0 gain_db=0", lines[1]);
            Assert.Contains("codec=at9 loop=1 gain_db=-12.3456", lines[2]);

            var read = AudioManifest.Read(path);
            Assert.Equal(2, read.Count);
            Assert.Equal(impact, read[0]);
            Assert.Equal(music, read[1]);
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }

    [Fact]
    public void Manifest_DuplicateBankName_Throws()
    {
        var dir = TempDir();
        try
        {
            var path = Path.Combine(dir, "audio.manifest");
            var entry = new AudioEntry("boss01", "valus.impact", "audio/boss01/valus.impact.wav",
                48000, 2, 100, 100 / 48000.0, "psadpcm", false, 0);
            Assert.Throws<InvalidDataException>(() => AudioManifest.Write(path, new[] { entry, entry }));
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }

    [Fact]
    public void Decode_0x42_Is48kNonSilent()
    {
        var bank = SxdContainer.Read(Bank("boss01.sxd"));
        var waves = SxdTables.Waves(bank);

        // The 0x42 family is ATRAC9 at the record's own config (recon §5.0/§7.3): record 108 is
        // the stereo/mono pair 108/109 at 232 800 frames (4.8587 s).
        foreach (var index in new[] { 108, 109 })
        {
            var wave = waves[index];
            Assert.Equal(0x42u, wave.FormatWord & 0xFF);
            Assert.NotEqual(0u, wave.CodecConfig);

            var audio = AudioCodec.Decode(CarveResident(bank, wave), wave.FormatWord,
                wave.CodecConfig, wave.SampleRate);

            Assert.Equal(48000, audio.Rate);
            Assert.Equal((int)((wave.FormatWord >> 8) & 0xFF), audio.Channels);
            Assert.True(audio.Frames > 0);
            Assert.True(audio.Rms > 1e-3, $"record {index} RMS {audio.Rms}");
            Assert.InRange(audio.Peak, 0.1f, 0.99f);
            var error = Math.Abs(audio.Frames - wave.Frames) / 48000.0;
            Assert.InRange(error, 0, 0.050); // the gate's 50 ms tolerance
        }
    }

    [Fact]
    public void Decode_RejectedConfigAndNonFinite_Throw()
    {
        var bank = SxdContainer.Read(Bank("boss01.sxd"));
        var wave = SxdTables.Waves(bank)[108]; // 0x0242, 2ch ATRAC9
        var payload = CarveResident(bank, wave);

        // No silent fallback: a 0x42 record without its config, a config whose channel layout
        // disagrees with the record, and an unsupported codec all reject.
        Assert.Throws<InvalidDataException>(() =>
            AudioCodec.Decode(payload, wave.FormatWord, 0, wave.SampleRate));
        Assert.Throws<InvalidDataException>(() =>
            AudioCodec.Decode(payload, wave.FormatWord, 0xFE761DF0, wave.SampleRate));
        Assert.Throws<InvalidDataException>(() =>
            AudioCodec.Decode(payload, 0x00FF0104, 0, wave.SampleRate));

        // A non-finite sample never reaches a WAV.
        Assert.Throws<InvalidDataException>(() =>
            AudioCodec.Normalize(new DecodedAudio(new[] { float.NaN }, 1, 48000)));
    }

    [Fact]
    public void Decode_0x21_Is48kNonSilent()
    {
        var bank = SxdContainer.Read(Bank("boss01.sxd"));
        var waves = SxdTables.Waves(bank);

        foreach (var index in new[] { 3, 26 }) // 0x0121 mono / 0x0221 stereo
        {
            var wave = waves[index];
            Assert.Equal(0x21u, wave.FormatWord & 0xFF);

            var audio = AudioCodec.Decode(CarveResident(bank, wave), wave.FormatWord,
                wave.CodecConfig, wave.SampleRate);

            Assert.Equal(48000, audio.Rate);
            Assert.Equal((int)((wave.FormatWord >> 8) & 0xFF), audio.Channels);
            Assert.True(audio.Rms > 1e-3, $"wave {index} RMS {audio.Rms}");
            Assert.InRange(audio.Peak, 0.1f, 0.99f);
            // 16-byte blocks of 28 samples: the decoded length trails the declared count by at
            // most one block per channel (the gate allows 1 ms for this family).
            var error = Math.Abs(audio.Frames - wave.Frames) / 48000.0;
            Assert.InRange(error, 0, 0.001);
        }
    }

    [Fact]
    public void Decode_Music_IsStereo48k()
    {
        var bank = SxdContainer.Read(Bank("music.sxd1"));
        var waves = SxdTables.Waves(bank);
        var wave = waves[6];
        Assert.Equal("Boss 1", SxdTables.TrackNames(bank)[6]);
        Assert.Equal(0x00FF0642u, wave.FormatWord); // 6 channels, AT9
        Assert.Equal(0xFE761DF0u, wave.CodecConfig);

        var payload = File.ReadAllBytes(Bank("music.sxd2"));
        var slice = payload.AsSpan((int)wave.Start, wave.ByteLength).ToArray();
        var audio = AudioCodec.Decode(slice, wave.FormatWord, wave.CodecConfig, wave.SampleRate);

        Assert.Equal(2, audio.Channels); // 5.1 downmixed (R6)
        Assert.Equal(48000, audio.Rate);
        Assert.InRange(audio.Seconds, 125.8, 126.1); // measured 125.93 s (recon §6)
        Assert.True(audio.Rms > 1e-3);
        Assert.InRange(audio.Peak, 0.98f, 0.99f);
        // The unclipped stereo downmix peaks ~3.16 (the 5.1 master overshoots unity), so the
        // normalization must attenuate by ~10 dB; clipping the sum first would report ~0 dB.
        Assert.InRange(audio.GainDb, -12, -8);
    }

    [Fact]
    public void Verb_WritesManifestAndFiles()
    {
        var outLib = TempDir();
        try
        {
            var options = new AudioConvertOptions(TestAssets.Root, outLib,
                RepoFile(Path.Combine("tools", "re", "audio", "s5_map.json")),
                new[] { "player.swing" }, null, Force: true);

            var result = AudioConverter.Run(options);
            var entry = Assert.Single(result.Entries);
            Assert.Single(result.Written);
            Assert.Empty(result.Reused);
            Assert.Equal("com_obj", entry.Bank);
            Assert.Equal("player.swing", entry.Name);
            Assert.Equal("audio/com_obj/player.swing.wav", entry.File);
            Assert.Equal(48000, entry.Rate);
            Assert.Equal(1, entry.Channels);
            Assert.Equal("psadpcm", entry.Codec);
            Assert.False(entry.Loop);

            var wavPath = Path.Combine(outLib, entry.File);
            Assert.True(File.Exists(wavPath), wavPath);
            var (channels, rate, frames, samples) = ReadFloatWav(wavPath);
            Assert.Equal(entry.Channels, channels);
            Assert.Equal(entry.Rate, rate);
            Assert.Equal(entry.Frames, frames);
            Assert.True(Peak(samples) > 0.1f);

            var manifestPath = Path.Combine(outLib, "audio", "audio.manifest");
            Assert.Equal(manifestPath, result.ManifestPath);
            var read = AudioManifest.Read(manifestPath);
            Assert.Single(read);
            Assert.Equal(entry, read[0]);

            // Without --force the existing WAV and manifest entry are reused, not re-decoded.
            var again = AudioConverter.Run(options with { Force = false });
            Assert.Single(again.Reused);
            Assert.Empty(again.Written);
            Assert.Equal(entry, Assert.Single(again.Entries));
        }
        finally
        {
            Directory.Delete(outLib, true);
        }
    }

    [Fact]
    public void Layers_Mix_NonSilent()
    {
        var outLib = TempDir();
        try
        {
            var options = new AudioConvertOptions(TestAssets.Root, outLib,
                RepoFile(Path.Combine("tools", "re", "audio", "s5_map.json")),
                new[] { "valus.impact" }, null, Force: true);

            var result = AudioConverter.Run(options);
            var entry = Assert.Single(result.Entries);
            Assert.Equal(2, entry.Channels);

            var (channels, rate, frames, samples) = ReadFloatWav(Path.Combine(outLib, entry.File));
            Assert.Equal(2, channels);
            Assert.Equal(48000, rate);
            Assert.True(Peak(samples) <= 0.99f + 1e-6f);
            Assert.True(Rms(samples) > 1e-3);

            // The gate mixes from t=0 and pads to the longest layer, so the mix is exactly the
            // longest decoded layer (no layer is longer than it).
            var waves = SxdTables.Waves(SxdContainer.Read(Bank("boss01.sxd")));
            var maxDeclared = waves.Skip(26).Take(5).Max(wave => (int)wave.Frames);
            Assert.InRange(frames, maxDeclared, maxDeclared + 64);
        }
        finally
        {
            Directory.Delete(outLib, true);
        }
    }

    /// <summary>
    /// Builds a minimal SXDF with one NAME chunk: count entries at +0x0C whose 12-byte records
    /// all carry a zero string offset, followed by <paramref name="pool"/>. The REQD key is the
    /// entry's third word, so it survives the pool fallback.
    /// </summary>
    private static byte[] BuildNameOnlyBank(string pool, uint[] reqdIds)
    {
        var poolBytes = Encoding.Latin1.GetBytes(pool);
        var body = new byte[0x08 + 12 * reqdIds.Length + poolBytes.Length]; // body = chunk + 8
        BinaryPrimitives.WriteUInt32LittleEndian(body.AsSpan(0x04), (uint)reqdIds.Length); // chunk + 0x0C
        for (var i = 0; i < reqdIds.Length; i++)
        {
            var entry = 0x08 + 12 * i; // chunk + 0x10
            BinaryPrimitives.WriteUInt32LittleEndian(body.AsSpan(entry), 0); // unresolvable offset
            BinaryPrimitives.WriteUInt32LittleEndian(body.AsSpan(entry + 4), 0);
            BinaryPrimitives.WriteUInt32LittleEndian(body.AsSpan(entry + 8), reqdIds[i]);
        }

        poolBytes.CopyTo(body, 0x08 + 12 * reqdIds.Length);

        var file = new byte[0x60 + 8 + body.Length];
        Encoding.ASCII.GetBytes("SXDF").CopyTo(file, 0);
        BinaryPrimitives.WriteUInt16LittleEndian(file.AsSpan(0x04), 4);
        BinaryPrimitives.WriteUInt32LittleEndian(file.AsSpan(0x08), (uint)file.Length);
        Encoding.ASCII.GetBytes("test").CopyTo(file, 0x20);
        Encoding.ASCII.GetBytes("NAME").CopyTo(file, 0x60);
        BinaryPrimitives.WriteUInt32LittleEndian(file.AsSpan(0x64), (uint)body.Length);
        body.CopyTo(file, 0x68);
        return file;
    }

    /// <summary>Merges the ranges and returns them as one tuple (the tiling assertion shape).</summary>
    private static (long Offset, long End) MergedUnion(
        IEnumerable<(long Offset, long End)> ranges)
    {
        var merged = new List<(long Offset, long End)>();
        foreach (var (offset, end) in ranges.Distinct().OrderBy(range => range.Offset))
        {
            if (merged.Count > 0 && offset <= merged[^1].End)
                merged[^1] = (merged[^1].Offset, Math.Max(merged[^1].End, end));
            else
                merged.Add((offset, end));
        }

        Assert.Single(merged);
        return merged[0];
    }

    /// <summary>A unique temporary directory, removed by the caller.</summary>
    private static string TempDir()
    {
        var path = Path.Combine(Path.GetTempPath(), $"bpconvert-audio-{Guid.NewGuid():N}");
        Directory.CreateDirectory(path);
        return path;
    }

    /// <summary>Resolves a repository-relative fixture (the cue map) from the test bin directory.</summary>
    private static string RepoFile(string relative)
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null)
        {
            var candidate = Path.Combine(dir.FullName, relative);
            if (File.Exists(candidate))
                return candidate;
            if (File.Exists(Path.Combine(dir.FullName, "BpConvert.sln")))
                throw new FileNotFoundException($"fixture {relative} is missing", candidate);
            dir = dir.Parent;
        }

        throw new FileNotFoundException($"repository root not found for {relative}");
    }

    /// <summary>The loader formula (recon §7.1): payload = record + 0x24 + start, length = bytes.</summary>
    private static byte[] CarveResident(SxdBank bank, WaveRecord wave)
    {
        var data = File.ReadAllBytes(bank.Path);
        var start = (int)(wave.RecordOffset + 0x24 + wave.Start);
        return data.AsSpan(start, wave.ByteLength).ToArray();
    }

    /// <summary>Reads the float32 RIFF/WAVE that <see cref="WavWriter"/> writes.</summary>
    private static (int Channels, int Rate, int Frames, float[] Samples) ReadFloatWav(string path)
    {
        var bytes = File.ReadAllBytes(path);
        Assert.Equal("RIFF", Encoding.ASCII.GetString(bytes, 0, 4));
        Assert.Equal("WAVE", Encoding.ASCII.GetString(bytes, 8, 4));

        int channels = 0, rate = 0, bits = 0, format = 0;
        var dataStart = -1;
        var dataLength = 0;
        var position = 12;
        while (position + 8 <= bytes.Length)
        {
            var id = Encoding.ASCII.GetString(bytes, position, 4);
            var size = (int)BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(position + 4));
            var body = position + 8;
            if (id == "fmt ")
            {
                format = BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(body));
                channels = BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(body + 2));
                rate = (int)BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(body + 4));
                bits = BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(body + 14));
            }
            else if (id == "data")
            {
                dataStart = body;
                dataLength = size;
            }

            position = body + size + (size & 1);
        }

        Assert.Equal(3, format); // IEEE float
        Assert.Equal(32, bits);
        Assert.True(dataStart >= 0);
        var samples = new float[dataLength / 4];
        for (var i = 0; i < samples.Length; i++)
            samples[i] = BinaryPrimitives.ReadSingleLittleEndian(bytes.AsSpan(dataStart + 4 * i));

        Assert.Equal(0, samples.Length % channels);
        return (channels, rate, samples.Length / channels, samples);
    }

    private static float Peak(IEnumerable<float> samples) =>
        samples.Select(Math.Abs).DefaultIfEmpty(0).Max();

    private static double Rms(IEnumerable<float> samples)
    {
        double sum = 0;
        var count = 0;
        foreach (var sample in samples)
        {
            sum += (double)sample * sample;
            count++;
        }

        return count == 0 ? 0 : Math.Sqrt(sum / count);
    }
}
