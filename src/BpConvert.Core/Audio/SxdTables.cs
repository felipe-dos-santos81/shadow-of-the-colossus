using System.Buffers.Binary;
using System.Text;

namespace BpConvert.Core.Audio;

/// <summary>
/// One <c>WAVE</c> table record. The field slots are the same for music and resident banks
/// (recon §2/§7): <c>+0x04</c> format word, <c>+0x08</c> sample rate, <c>+0x14</c> per-channel
/// frame count, <c>+0x20</c> byte length, <c>+0x24</c> start.
/// </summary>
/// <param name="Index">Record index in the table.</param>
/// <param name="RecordOffset">File offset of the record itself (needed by the resident payload formula).</param>
/// <param name="RecordSize">
/// Bytes from this record to the next one (or to the end of the <c>WAVE</c> chunk): the stride is
/// variable (48/56/68/76 on the resident banks, 76 for music).
/// </param>
/// <param name="FormatWord">The <c>+0x04</c> word; byte 1 is the channel count, the low byte the codec id.</param>
/// <param name="SampleRate">The <c>+0x08</c> rate (48 000 on every shipped record).</param>
/// <param name="Frames">The <c>+0x14</c> per-channel sample count.</param>
/// <param name="Start">
/// The <c>+0x24</c> value. It is <b>absolute in the companion <c>.sxd2</c></b> for a music bank
/// (no <c>DATA</c> chunk), and <b>relative to this record</b> for a resident bank, where the
/// payload is at <see cref="RecordOffset"/> + 0x24 + <see cref="Start"/> for
/// <see cref="ByteLength"/> bytes (the loader formula, <c>FUN_00983220</c>, recon §7.1).
/// </param>
/// <param name="ByteLength">The <c>+0x20</c> payload length in bytes.</param>
public sealed record WaveRecord(
    int Index, long RecordOffset, int RecordSize, uint FormatWord, uint SampleRate, uint Frames,
    long Start, int ByteLength);

/// <summary>One <c>NAME</c> table entry: the request name in table (and string-pool) order.</summary>
/// <param name="Index">Entry index; also the request id the <c>REQD</c> table is keyed by.</param>
/// <param name="Name">The NUL-terminated request name, decoded one byte per char (Latin-1).</param>
public sealed record NameRecord(int Index, string Name);

/// <summary>
/// Reader for the <c>WAVE</c> and <c>NAME</c> index tables (recon §1-§3; the C# counterpart of
/// <c>s5_decode_gate.py</c>'s <c>table_records</c>/<c>read_wave_record</c> and
/// <c>s5_audition.py</c>'s <c>bank_names</c>).
/// </summary>
/// <remarks>
/// <para>
/// <c>REQD</c>, <c>NAME</c> (offsets only — see below), <c>WAVE</c> and <c>SEQD</c> share the
/// generic table shape <c>{+0x04 size, +0x08 reserved, +0x0C count, +0x10 offsets[count]}</c>
/// with <c>record_i = chunk + 0x10 + offsets[i] + 4*i</c>. The <c>+ 4*i</c> term is the format,
/// not slack: <c>offsets[]</c> advance by <c>stride - 4</c>, so the same rule addresses a
/// variable-stride table. No stride is assumed anywhere in this reader.
/// </para>
/// <para>
/// <c>NAME</c> is a 12-byte entry table <c>{u32 string_offset, u32 hash, u32 reqd_id}</c>, where
/// <c>string_offset</c> is relative to the entry itself (<c>name = entry + string_offset</c>); the
/// entries are followed by the NUL-terminated string pool they point into. The engine's hash is
/// <c>u = byte + u*0x83</c> (<c>FUN_00987a80</c>). When every entry resolves and every ASCII name
/// matches that hash the table wins; otherwise (e.g. a different stride in an unreleased bank)
/// the pool is read in order and paired with the entries by index, which is the same order on
/// every shipped bank.
/// </para>
/// </remarks>
public static class SxdTables
{
    private const int TableCountOffset = 0x0C;
    private const int TableBodyOffset = 0x10;
    private const int WavePrefixLength = 0x28;
    private const int WaveFormatOffset = 0x04;
    private const int WaveRateOffset = 0x08;
    private const int WaveFramesOffset = 0x14;
    private const int WaveByteLengthOffset = 0x20;
    private const int WaveStartOffset = 0x24;
    private const int NameEntryLength = 12;
    private const uint NameHashMultiplier = 0x83;

    /// <summary>Reads the <c>WAVE</c> table in table order.</summary>
    /// <param name="bank">A bank from <see cref="SxdContainer.Read"/>; <see cref="SxdBank.Path"/> is re-read.</param>
    /// <returns>All records, or an empty list when the bank has no <c>WAVE</c> chunk.</returns>
    /// <exception cref="InvalidDataException">
    /// The table header, an <c>offsets[]</c> entry or a record prefix is out of bounds, or the
    /// records are not strictly increasing (an overlap).
    /// </exception>
    public static IReadOnlyList<WaveRecord> Waves(SxdBank bank)
    {
        ArgumentNullException.ThrowIfNull(bank);

        var chunk = FindChunk(bank, "WAVE");
        if (chunk is null)
            return Array.Empty<WaveRecord>();

        var path = bank.Path;
        var data = File.ReadAllBytes(path);
        var count = ReadCount(path, data, chunk);
        var body = chunk.Offset + TableBodyOffset;
        var chunkEnd = chunk.Offset + ChunkLength(chunk);
        if (body + 4L * count > chunkEnd)
            throw Invalid(path, $"WAVE count {count} overruns the chunk");

        var offsets = new long[count];
        for (var i = 0; i < count; i++)
        {
            var relative = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan((int)(body + 4L * i)));
            var record = body + relative + 4L * i;
            if (record + WavePrefixLength > chunkEnd)
                throw Invalid(path, $"WAVE record {i} at 0x{record:X} escapes the chunk");
            if (i > 0 && record <= offsets[i - 1])
                throw Invalid(path, $"WAVE record {i} at 0x{record:X} overlaps record {i - 1} at 0x{offsets[i - 1]:X}");
            offsets[i] = record;
        }

        var waves = new List<WaveRecord>((int)count);
        for (var i = 0; i < count; i++)
        {
            var record = offsets[i];
            var size = (int)((i + 1 < count ? offsets[i + 1] : chunkEnd) - record);
            var byteLength = ReadU32(path, data, record + WaveByteLengthOffset);
            if (byteLength > int.MaxValue)
                throw Invalid(path, $"WAVE record {i} byte length {byteLength} exceeds int.MaxValue");
            waves.Add(new WaveRecord(
                i, record, size,
                ReadU32(path, data, record + WaveFormatOffset),
                ReadU32(path, data, record + WaveRateOffset),
                ReadU32(path, data, record + WaveFramesOffset),
                ReadU32(path, data, record + WaveStartOffset),
                (int)byteLength));
        }

        return waves;
    }

    /// <summary>Reads the <c>NAME</c> table (request-name namespace) in table order.</summary>
    /// <param name="bank">A bank from <see cref="SxdContainer.Read"/>; <see cref="SxdBank.Path"/> is re-read.</param>
    /// <returns>All entries, or an empty list when the bank has no <c>NAME</c> chunk.</returns>
    /// <exception cref="InvalidDataException">
    /// The entry array is out of bounds, neither the 12-byte entries nor the string pool resolve,
    /// or a name is empty, non-printable or duplicated inside the bank.
    /// </exception>
    public static IReadOnlyList<NameRecord> Names(SxdBank bank)
    {
        ArgumentNullException.ThrowIfNull(bank);

        var chunk = FindChunk(bank, "NAME");
        if (chunk is null)
            return Array.Empty<NameRecord>();

        var path = bank.Path;
        var data = File.ReadAllBytes(path);
        var count = ReadCount(path, data, chunk);
        var body = chunk.Offset + TableBodyOffset;
        var chunkEnd = chunk.Offset + ChunkLength(chunk);
        if (body + NameEntryLength * (long)count > chunkEnd)
            throw Invalid(path, $"NAME count {count} overruns the chunk");

        var names = TryReadEntryTable(data, body, count, chunkEnd);
        if (names is null)
        {
            var pool = ReadPool(data, body + NameEntryLength * (long)count, chunkEnd);
            if (pool.Count < count)
                throw Invalid(path, $"NAME string pool holds {pool.Count} names, expected {count}");
            names = pool.GetRange(0, (int)count);
        }

        var records = new List<NameRecord>(names.Count);
        var seen = new HashSet<string>(StringComparer.Ordinal);
        for (var i = 0; i < names.Count; i++)
        {
            if (names[i].Length == 0)
                throw Invalid(path, $"NAME entry {i} is empty");
            foreach (var c in names[i])
                if (c is < ' ' or '\x7F')
                    throw Invalid(path, $"NAME entry {i} is not printable");
            if (!seen.Add(names[i]))
                throw Invalid(path, $"NAME entry {i} duplicates \"{names[i]}\"");
            records.Add(new NameRecord(i, names[i]));
        }

        return records;
    }

    /// <summary>
    /// Resolves the 12-byte NAME entries (<c>name = entry + string_offset</c>) and verifies the
    /// engine hash on every ASCII name. Returns null when the layout does not resolve, which sends
    /// the caller to the string pool.
    /// </summary>
    private static List<string>? TryReadEntryTable(byte[] data, long body, uint count, long chunkEnd)
    {
        var names = new List<string>((int)count);
        for (var i = 0L; i < count; i++)
        {
            var entry = body + NameEntryLength * i;
            var stringOffset = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan((int)entry));
            var hash = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan((int)entry + 4));
            var start = entry + stringOffset;
            if (start < body || start >= chunkEnd)
                return null;

            var end = start;
            while (end < chunkEnd && data[end] != 0)
                end++;
            if (end == start || end >= chunkEnd)
                return null;

            var bytes = data.AsSpan((int)start, (int)(end - start));
            if (IsAscii(bytes) && EngineHash(bytes) != hash)
                return null;
            names.Add(Encoding.Latin1.GetString(bytes));
        }

        return names;
    }

    /// <summary>The NUL-terminated string pool after the NAME entries, empty strings dropped.</summary>
    private static List<string> ReadPool(byte[] data, long start, long end)
    {
        var names = new List<string>();
        var position = start;
        while (position < end)
        {
            var nul = position;
            while (nul < end && data[nul] != 0)
                nul++;
            if (nul >= end)
                break;
            if (nul > position)
                names.Add(Encoding.Latin1.GetString(data, (int)position, (int)(nul - position)));
            position = nul + 1;
        }

        return names;
    }

    /// <summary>True when every byte is ASCII (the hash is only verified for those names).</summary>
    private static bool IsAscii(ReadOnlySpan<byte> bytes)
    {
        foreach (var b in bytes)
            if (b >= 0x80)
                return false;
        return true;
    }

    /// <summary>The engine's name hash (<c>FUN_00987a80</c>): <c>u = byte + u * 0x83</c>, u32 wrap.</summary>
    private static uint EngineHash(ReadOnlySpan<byte> bytes)
    {
        var hash = 0u;
        foreach (var b in bytes)
            hash = unchecked(b + hash * NameHashMultiplier);
        return hash;
    }

    private static SxdChunk? FindChunk(SxdBank bank, string magic)
    {
        foreach (var chunk in bank.Chunks)
            if (chunk.Magic == magic)
                return chunk;
        return null;
    }

    /// <summary>A chunk's total length: 8-byte header plus body.</summary>
    private static int ChunkLength(SxdChunk chunk) => 8 + chunk.Size;

    private static uint ReadCount(string path, byte[] data, SxdChunk chunk)
    {
        if (chunk.Size < TableBodyOffset)
            throw Invalid(path, $"{chunk.Magic} chunk at 0x{chunk.Offset:X} is too small for a table header");
        return BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan((int)(chunk.Offset + TableCountOffset)));
    }

    private static uint ReadU32(string path, byte[] data, long offset)
    {
        if (offset + 4 > data.Length)
            throw Invalid(path, $"field at 0x{offset:X} is beyond EOF");
        return BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan((int)offset));
    }

    private static InvalidDataException Invalid(string path, string detail) => new($"{path}: {detail}");
}
