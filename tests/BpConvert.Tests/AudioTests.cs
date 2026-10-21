using System.Buffers.Binary;
using System.Text;
using BpConvert.Core.Audio;

/// <summary>
/// Pinned cases for the SXDF container and its index tables (S5 Task 2;
/// <c>tools/re/audio/s5_decode_gate.py</c> and <c>docs/notes/s5-audio-recon.md</c> are the
/// parsing reference): the 0x60-byte header, the chunk chain from <c>0x60</c>, the shared
/// <c>count/offsets[]</c> table (<c>record_i = body + offsets[i] + 4*i</c>, never a fixed
/// stride), the absolute music starts, and the loader-derived resident payload formula
/// <c>record + 0x24 + start</c> (recon §7.1). Real-tree cases are env-gated through
/// <see cref="TestAssets"/> (fail fast when <c>SOTC_GAME_ROOT</c> is unset — never skips).
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
}
