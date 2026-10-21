using System.Buffers.Binary;
using System.Text;

namespace BpConvert.Core.Audio;

/// <summary>
/// One chunk of the SXDF chunk chain: a four-character tag followed by a little-endian u32 body
/// size. The next chunk starts at <see cref="Offset"/> + 8 + <see cref="Size"/>.
/// </summary>
/// <param name="Magic">The chunk tag, e.g. <c>TONE</c>, <c>REQD</c>, <c>NAME</c>, <c>WAVE</c>, <c>DATA</c>.</param>
/// <param name="Offset">File offset of the chunk header (the tag byte).</param>
/// <param name="Size">Body size in bytes, i.e. everything after the 8-byte chunk header.</param>
public sealed record SxdChunk(string Magic, long Offset, int Size);

/// <summary>
/// One parsed container: an <c>SXDF</c> bank/index (<c>.sxd</c>, <c>.sxd1</c>) or an <c>SXDS</c>
/// stream payload (<c>.sxd2</c>). Both share the same 0x60-byte header (recon §1).
/// </summary>
/// <param name="Path">The file the bank was read from; <see cref="SxdTables"/> re-reads it.</param>
/// <param name="Version">The u16 at 0x04; 4 on every shipped container.</param>
/// <param name="HashHex">The 16-byte bank GUID at 0x10 as lowercase hex; identical inside a pair.</param>
/// <param name="Name">The NUL-padded bank name at 0x20.</param>
/// <param name="Chunks">
/// The chunk chain from 0x60, in file order. Empty for an <c>SXDS</c>: its body is raw payload,
/// not a chunk chain (the +0x08 field is a stream count there).
/// </param>
/// <param name="DataOffset">
/// The <c>DATA</c> chunk header offset on an <c>SXDF</c> (the payload starts at
/// <c>DataOffset + 0x10</c>: 8 header + 8 zero bytes, recon §7.1), or 0x60 on an
/// <c>SXDS</c>, where the body itself is the payload.
/// </param>
/// <param name="DataSize">
/// The <c>DATA</c> chunk body size on an <c>SXDF</c>, or the <c>SXDS</c> payload size from +0x0C.
/// </param>
/// <param name="HasData">True when the file carries wave payload bytes (a <c>DATA</c> chunk or an <c>SXDS</c> body).</param>
/// <param name="CompanionSize">
/// The u32 at 0x0C: the companion <c>.sxd2</c> file size on an <c>SXDF</c> index (0 when the bank
/// is self-contained), or the payload size on an <c>SXDS</c>.
/// </param>
public sealed record SxdBank(
    string Path, ushort Version, string HashHex, string Name, IReadOnlyList<SxdChunk> Chunks,
    long DataOffset, int DataSize, bool HasData, uint CompanionSize);

/// <summary>
/// Reader for the <c>SXDF</c>/<c>SXDS</c> container (recon §1; the C# counterpart of the
/// <c>s5_decode_gate.py</c> header/chunk walk): the 0x60-byte header, the chunk chain from 0x60,
/// and the <c>DATA</c> region the resident tables address. Malformed files throw
/// <see cref="InvalidDataException"/> whose message starts with the input path.
/// </summary>
/// <remarks>
/// The chunk walk must land exactly on EOF: on every shipped container the last chunk ends at the
/// file size, and the <c>+0x08</c> own-size field equals it. A truncated or padded chain therefore
/// fails here rather than in a table reader.
/// </remarks>
public static class SxdContainer
{
    /// <summary>Header length shared by both magics; the first chunk starts here.</summary>
    public const int HeaderLength = 0x60;

    private const int ChunkHeaderLength = 8;
    private const int VersionOffset = 0x04;
    private const int SizeOrStreamsOffset = 0x08;
    private const int CompanionSizeOffset = 0x0C;
    private const int HashOffset = 0x10;
    private const int HashLength = 16;
    private const int NameOffset = 0x20;
    private const int NameLength = 32;

    // The gate's tag rule: every byte of a four-character tag is in 0x30..0x5A ([0-9A-Z]).
    private const byte TagMin = 0x30;
    private const byte TagMax = 0x5A;

    /// <summary>Reads one <c>SXDF</c> bank/index or <c>SXDS</c> stream payload.</summary>
    /// <param name="path">Path to the <c>.sxd</c>/<c>.sxd1</c>/<c>.sxd2</c> file.</param>
    /// <exception cref="InvalidDataException">
    /// The file is shorter than the 0x60-byte header, its magic is neither <c>SXDF</c> nor
    /// <c>SXDS</c>, the declared size disagrees with the file, or a chunk header/body is
    /// truncated or duplicated.
    /// </exception>
    public static SxdBank Read(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        var data = File.ReadAllBytes(path);
        if (data.Length < HeaderLength)
            throw Invalid(path, $"shorter than the 0x{HeaderLength:X} header ({data.Length} B)");

        var magic = Encoding.ASCII.GetString(data, 0, 4);
        if (magic is not ("SXDF" or "SXDS"))
            throw Invalid(path, $"magic \"{Escaped(magic)}\" is not SXDF/SXDS");

        var version = BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(VersionOffset));
        var sizeOrStreams = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(SizeOrStreamsOffset));
        var companionSize = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(CompanionSizeOffset));

        IReadOnlyList<SxdChunk> chunks = magic == "SXDF"
            ? WalkChunks(path, data, sizeOrStreams)
            : Array.Empty<SxdChunk>();

        long dataOffset;
        int dataSize;
        bool hasData;
        if (magic == "SXDF")
        {
            var dataChunk = chunks.FirstOrDefault(chunk => chunk.Magic == "DATA");
            dataOffset = dataChunk?.Offset ?? 0;
            dataSize = dataChunk?.Size ?? 0;
            hasData = dataChunk is not null;
        }
        else
        {
            // An SXDS has no chunk chain: +0x08 is a stream count and +0x0C the payload size,
            // with the payload starting at the end of the header.
            if (HeaderLength + (long)companionSize > data.Length)
                throw Invalid(path, $"payload size @0x0C = {companionSize} overruns the file ({data.Length} B)");
            dataOffset = HeaderLength;
            dataSize = (int)companionSize;
            hasData = true;
        }

        return new SxdBank(
            path, version, Convert.ToHexStringLower(data.AsSpan(HashOffset, HashLength)),
            ReadName(path, data), chunks, dataOffset, dataSize, hasData, companionSize);
    }

    /// <summary>
    /// Walks <c>0x60 .. EOF</c>: <c>char[4] magic + u32 body size</c>, next at
    /// <c>offset + size + 8</c>. The walk must consume the file exactly.
    /// </summary>
    private static List<SxdChunk> WalkChunks(string path, byte[] data, uint declaredSize)
    {
        if (declaredSize != data.Length)
            throw Invalid(path, $"size field @0x08 = {declaredSize} != file size {data.Length}");

        var chunks = new List<SxdChunk>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        long offset = HeaderLength;
        while (offset < data.Length)
        {
            if (offset + ChunkHeaderLength > data.Length)
                throw Invalid(path, $"chunk header at 0x{offset:X} is truncated");
            var chunkMagic = ReadTag(path, data, offset);
            if (!seen.Add(chunkMagic))
                throw Invalid(path, $"duplicate {chunkMagic} chunk at 0x{offset:X}");

            var size = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan((int)offset + 4));
            if (size > int.MaxValue || offset + ChunkHeaderLength + size > data.Length)
                throw Invalid(path,
                    $"{chunkMagic} chunk at 0x{offset:X} overruns the file ({size} B body, {data.Length - offset - ChunkHeaderLength} B left)");

            chunks.Add(new SxdChunk(chunkMagic, offset, (int)size));
            offset += ChunkHeaderLength + size;
        }

        if (chunks.Count == 0)
            throw Invalid(path, "no chunks at 0x60");
        return chunks;
    }

    private static string ReadTag(string path, byte[] data, long offset)
    {
        var tag = data.AsSpan((int)offset, 4);
        foreach (var b in tag)
            if (b is < TagMin or > TagMax)
                throw Invalid(path, $"chunk tag at 0x{offset:X} is not [0-9A-Z]");
        return Encoding.ASCII.GetString(tag);
    }

    private static string ReadName(string path, byte[] data)
    {
        var field = data.AsSpan(NameOffset, NameLength);
        var nul = field.IndexOf((byte)0);
        var name = Encoding.Latin1.GetString(nul < 0 ? field : field[..nul]);
        return name.Length > 0 ? name : throw Invalid(path, "empty bank name at 0x20");
    }

    private static string Escaped(string magic) =>
        string.Concat(magic.Select(c => c is >= ' ' and <= '~' ? c.ToString() : $"\\x{(int)c:X2}"));

    private static InvalidDataException Invalid(string path, string detail) => new($"{path}: {detail}");
}
