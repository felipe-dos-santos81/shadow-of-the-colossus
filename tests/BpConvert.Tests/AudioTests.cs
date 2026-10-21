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
        // The table resolves to the engine hash: NAME 0 is REQD 303 (recon attribution §1).
        Assert.Equal("boss01_atk_hit_ground1", names[0].Name);
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
    public void Read_Bridge_AcceptsToneOnly()
    {
        var bank = SxdContainer.Read(Bank("bridge.sxd"));

        var chunk = Assert.Single(bank.Chunks);
        Assert.Equal("TONE", chunk.Magic);
        Assert.Equal(0x60, chunk.Offset);
        Assert.False(bank.HasData);
        Assert.Equal(0, bank.DataSize);
        Assert.Empty(SxdTables.Waves(bank)); // no WAVE table in this bank
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
