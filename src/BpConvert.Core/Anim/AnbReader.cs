using System.Buffers.Binary;
using System.Numerics;

namespace BpConvert.Core.Anim;

/// <summary>
/// Reader for the flag-clear variant of BluePoint <c>.anb</c> motion banks (version-word bit 16
/// clear). This is the C# port of the proven RE-2 decode (<c>tools/re/anb/parse2.py</c>;
/// per-type formats pinned in <c>docs/notes/re2-keys.md</c> §1): the structure/track pointers,
/// the key formats for types 0, 2, 3–6, 0xC, 0xD and 0xE, and the root position series.
/// </summary>
/// <remarks>
/// <para>
/// All offsets are data-region-relative; the data region starts at file <c>0x110</c>. The result
/// is raw game convention: no track→joint mapping, no bridge math, no glTF conversion.
/// </para>
/// <para>
/// Only the flag-clear variant is in scope. Malformed files and the flag-set variant throw
/// <see cref="InvalidDataException"/> whose message starts with the input path.
/// </para>
/// </remarks>
public static class AnbReader
{
    private const int DataBase = 0x110;
    private const uint FlagSetBit = 1u << 16;
    private const int MaxTracks = 4096;

    // The root series scale (docs/notes/re2-keys.md §2.1); the per-key scales live in AnbKeyMath.
    private const double Int16Scale = AnbKeyMath.Int16;

    private static readonly byte[] XffMagic = { 0x78, 0x66, 0x66, 0x00 };

    // The per-type blob size in bytes (floats + the key-pointer word; measured).
    private static readonly Dictionary<byte, int> BlobSize = new()
    {
        [0x00] = 16,
        [0x02] = 16,
        [0x03] = 36,
        [0x04] = 36,
        [0x05] = 36,
        [0x06] = 36,
        [0x0C] = 28,
        [0x0D] = 24,
        [0x0E] = 28,
    };

    // The per-type key-pointer field inside the blob, the key stride, and how many descriptor
    // floats the decode reads (FUN_0066dec0's re-based field; docs/notes/re2-keys.md §1).
    private static readonly Dictionary<byte, (int KeyPtrField, int Stride, int FloatCount)> Formats = new()
    {
        [0x02] = (0x0C, 4, 3),
        [0x03] = (0x20, 4, 8),
        [0x04] = (0x20, 4, 8),
        [0x05] = (0x20, 4, 8),
        [0x06] = (0x20, 4, 8),
        [0x0C] = (0x18, 6, 6),
        [0x0D] = (0x14, 2, 5),
        [0x0E] = (0x18, 4, 6),
    };

    /// <summary>Reads and decodes one flag-clear <c>.anb</c> file.</summary>
    /// <param name="path">Path to the <c>.anb</c> file.</param>
    /// <returns>The decoded clip, raw game convention; <see cref="AnimClip.Name"/> is the file stem.</returns>
    /// <exception cref="InvalidDataException">
    /// The file is not a well-formed flag-clear <c>.anb</c> (bad magic/size, the flag-set variant,
    /// out-of-range pointers, or an unhandled track type).
    /// </exception>
    public static AnimClip Read(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        var data = File.ReadAllBytes(path);
        var name = Path.GetFileNameWithoutExtension(path);

        if (data.Length < XffMagic.Length || !data.AsSpan(0, XffMagic.Length).SequenceEqual(XffMagic))
            throw Invalid(path, "not an xff container (magic mismatch)");
        if (data.Length < DataBase + 0x40)
            throw Invalid(path, "shorter than the data region");
        var sizeField = ReadU32(data, 0x14, path);
        if (sizeField != (uint)data.Length)
            throw Invalid(path, $"size field @0x14 = {sizeField} != file size {data.Length}");

        var structOffset = ReadU32(data, 0x10, path);
        if (structOffset == 0 || structOffset >= data.Length - DataBase)
            throw Invalid(path, $"structure offset 0x{structOffset:X} out of range");
        var structure = DataBase + (long)structOffset;

        var versionWord = ReadU32(data, structure, path);
        if ((versionWord & FlagSetBit) != 0)
            throw Invalid(path, $"flag-set variant (version word 0x{versionWord:X8} bit 16 set) is out of scope");

        var frames = ReadU16(data, structure + 4, path);

        // The rate lives in the high u16 of the same word: a Q12 value; zero means the 1.0 default.
        var rateWord = ReadU16(data, structure + 6, path);
        var rate = rateWord >> 4;
        var rateValue = rate == 0 ? 1.0 : rate / 4096.0;

        var trackCountRaw = ReadU32(data, structure + 0x14, path);
        if (trackCountRaw == 0 || trackCountRaw > MaxTracks)
            throw Invalid(path, $"implausible track count {trackCountRaw}");
        var trackCount = (int)trackCountRaw;

        var typesOffset = ReadU32(data, structure + 0x08, path);
        var blobsOffset = ReadU32(data, structure + 0x10, path);
        var descriptorOffset = ReadU32(data, structure + 0x0C, path);
        if (DataBase + (long)typesOffset + trackCount > data.Length)
            throw Invalid(path, "per-track type array out of range");
        if (DataBase + (long)blobsOffset + 4L * trackCount > data.Length)
            throw Invalid(path, "per-track blob pointer array out of range");

        var tracks = new List<AnimTrack>(trackCount);
        for (var i = 0; i < trackCount; i++)
        {
            var type = data[DataBase + (int)typesOffset + i];
            var blobOffset = ReadU32(data, DataBase + (long)blobsOffset + 4L * i, path);
            tracks.Add(DecodeTrack(data, path, i, type, blobOffset));
        }

        return new AnimClip
        {
            Name = name,
            Frames = frames,
            Rate = rateValue,
            Tracks = tracks,
            Root = DecodeRoot(data, path, descriptorOffset),
        };
    }

    private static AnimTrack DecodeTrack(byte[] data, string path, int index, byte type, uint blobOffset)
    {
        var blob = DataBase + (long)blobOffset;
        if (!BlobSize.TryGetValue(type, out var blobSize))
            throw Invalid(path, $"track {index}: unhandled type 0x{type:X2}");
        if (blob + blobSize > data.Length)
            throw Invalid(path, $"track {index}: blob out of range");

        if (type == 0x00)
        {
            // A static track: one unit quaternion, no keys, no pointer field.
            var q = new Quaternion(
                ReadF32(data, blob, path), ReadF32(data, blob + 4, path),
                ReadF32(data, blob + 8, path), ReadF32(data, blob + 12, path));
            return new AnimTrack { Index = index, IsStatic = true, Keys = new List<Quaternion> { q } };
        }

        var format = Formats[type];
        var descriptor = ReadFloats(data, blob, format.FloatCount, path);
        var keyPtr = ReadU32(data, blob + format.KeyPtrField, path);
        // Extent = blob - keyptr. The count floors: type 0xC key blocks may carry 0 or 2 bytes of
        // 4-byte alignment pad before the blob (docs/notes/re2-keys.md §1).
        var extent = blobOffset - (long)keyPtr;
        if (extent <= 0)
            throw Invalid(path, $"track {index}: no key data (extent {extent})");
        var keyCount = (int)(extent / format.Stride);
        var keyBase = DataBase + (long)keyPtr;

        var keys = new List<Quaternion>(keyCount);
        for (var k = 0; k < keyCount; k++)
            keys.Add(DecodeKey(data, path, type, descriptor, keyBase, k));

        return new AnimTrack { Index = index, IsStatic = false, Keys = keys };
    }

    private static Quaternion DecodeKey(
        byte[] data, string path, byte type, float[] f, long keyBase, int key)
    {
        if (AnbKeyMath.IsPacked(type))
            return AnbKeyMath.Packed(type, ReadU32(data, keyBase + 4L * key, path), f);

        switch (type)
        {
            case 0x0C:
            {
                var offset = keyBase + 6L * key;
                return AnbKeyMath.Int16Triple(
                    ReadI16(data, offset, path), ReadI16(data, offset + 2, path),
                    ReadI16(data, offset + 4, path), ReadU16(data, offset, path), f);
            }

            case 0x0D:
                return AnbKeyMath.Angle(ReadI16(data, keyBase + 2L * key, path), f);

            case 0x0E:
                return AnbKeyMath.Packed11(ReadU32(data, keyBase + 4L * key, path), f);

            case 0x02:
            {
                // No flag-clear track of this type exists in the corpus; the format is from the
                // accessor only (docs/notes/re2-keys.md §1).
                var angle = ReadF32(data, keyBase + 4L * key, path);
                var sin = Math.Sin(angle);
                return new Quaternion(
                    (float)(f[0] * sin), (float)(f[1] * sin), (float)(f[2] * sin),
                    (float)Math.Cos(angle));
            }

            default:
                throw Invalid(path, $"unhandled track type 0x{type:X2}");
        }
    }

    private static List<Vector3> DecodeRoot(byte[] data, string path, uint descriptorOffset)
    {
        if (descriptorOffset == 0)
            return new List<Vector3>();

        if (DataBase + (long)descriptorOffset + 0x1C > data.Length)
            throw Invalid(path, "root descriptor out of range");
        var descriptor = ReadFloats(data, DataBase + (long)descriptorOffset, 6, path);

        // The key count is encoded by the descriptor offset: 6 bytes per key, 0 or 2 pad bytes
        // (measured for every flag-clear file that carries a root series).
        var remainder = descriptorOffset % 6;
        var keyCount = remainder switch
        {
            0 => descriptorOffset / 6L,
            2 => (descriptorOffset - 2L) / 6L,
            _ => throw Invalid(path, $"root descriptor offset 0x{descriptorOffset:X} is not 6*frames (+pad)"),
        };
        if (DataBase + 6L * keyCount > data.Length)
            throw Invalid(path, "root series out of range");

        var root = new List<Vector3>((int)keyCount);
        for (long k = 0; k < keyCount; k++)
        {
            var offset = DataBase + 6L * k;
            root.Add(new Vector3(
                (float)(ReadI16(data, offset, path) * Int16Scale * descriptor[1] + descriptor[0]),
                (float)(ReadI16(data, offset + 2, path) * Int16Scale * descriptor[3] + descriptor[2]),
                (float)(ReadI16(data, offset + 4, path) * Int16Scale * descriptor[5] + descriptor[4])));
        }

        return root;
    }

    private static ushort ReadU16(byte[] data, long offset, string path) =>
        BinaryPrimitives.ReadUInt16LittleEndian(Check(data, offset, 2, path));

    private static uint ReadU32(byte[] data, long offset, string path) =>
        BinaryPrimitives.ReadUInt32LittleEndian(Check(data, offset, 4, path));

    private static short ReadI16(byte[] data, long offset, string path) =>
        BinaryPrimitives.ReadInt16LittleEndian(Check(data, offset, 2, path));

    private static float ReadF32(byte[] data, long offset, string path) =>
        BinaryPrimitives.ReadSingleLittleEndian(Check(data, offset, 4, path));

    private static float[] ReadFloats(byte[] data, long offset, int count, string path)
    {
        var values = new float[count];
        for (var i = 0; i < count; i++)
            values[i] = ReadF32(data, offset + 4L * i, path);
        return values;
    }

    private static ReadOnlySpan<byte> Check(byte[] data, long offset, int size, string path)
    {
        if (offset < 0 || offset + size > data.Length)
            throw Invalid(path, $"read past end of file at 0x{offset:X}");
        return data.AsSpan((int)offset, size);
    }

    private static InvalidDataException Invalid(string path, string message) =>
        new($"{path}: {message}");
}
