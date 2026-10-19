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

    // The engine constants (docs/notes/re2-keys.md §2.1).
    private const double Int16Scale = 1.0 / 32767.0; // types 0xC/0xD and the root series
    private const double Packed9Scale = 1.0 / 255.0; // the signed 9-bit fields of types 3–6
    private const double Packed11Scale = 1.0 / 1023.0; // type 0xE x
    private const double Packed10Scale = 1.0 / 511.0; // type 0xE y/z
    private const double ResidualScale = 1.0 / 7.0; // the 4-bit residual of types 3–6

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

    // Types 3–6: the three stored 9-bit fields occupy the components other than the derived one,
    // packed from the top of the word. Descriptor pairs apply in field order (1st field f0/f1,
    // 2nd f2/f3, 3rd f4/f5; the derived component uses f6/f7).
    private static readonly Dictionary<byte, PackedLayout> PackedLayouts = new()
    {
        [0x03] = new PackedLayout(
            0, new PackedField[] { new(1, 18, 0), new(2, 9, 1), new(3, 0, 2) }, 27, 31),
        [0x04] = new PackedLayout(
            1, new PackedField[] { new(0, 23, 0), new(2, 9, 1), new(3, 0, 2) }, 18, 22),
        [0x05] = new PackedLayout(
            2, new PackedField[] { new(0, 23, 0), new(1, 14, 1), new(3, 0, 2) }, 9, 13),
        [0x06] = new PackedLayout(
            3, new PackedField[] { new(0, 23, 0), new(1, 14, 1), new(2, 5, 2) }, 0, 4),
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
        if (PackedLayouts.TryGetValue(type, out var layout))
        {
            var word = ReadU32(data, keyBase + 4L * key, path);
            var comp = new double[4];
            foreach (var field in layout.Fields)
            {
                comp[field.Component] = Signed9((int)((word >> field.Shift) & 0x1FF))
                    * Packed9Scale * f[2 * field.Pair + 1] + f[2 * field.Pair];
            }

            var norm2 = comp[0] * comp[0] + comp[1] * comp[1] + comp[2] * comp[2] + comp[3] * comp[3];
            var residual = Signed4((int)((word >> layout.ResidualShift) & 0xF));
            var sign = ((word >> layout.SignBit) & 1) != 0 ? -1.0 : 1.0;
            var sqrt = norm2 <= 1.0 ? Math.Sqrt(Math.Abs(1.0 - norm2)) : 0.0;
            comp[layout.Derived] = f[6] + f[7] * (residual * ResidualScale) + sign * sqrt;
            return ToQuaternion(comp);
        }

        switch (type)
        {
            case 0x0C:
            {
                var offset = keyBase + 6L * key;
                var firstWord = ReadU16(data, offset, path);
                double x = ReadI16(data, offset, path) * Int16Scale * f[1] + f[0];
                double y = ReadI16(data, offset + 2, path) * Int16Scale * f[3] + f[2];
                double z = ReadI16(data, offset + 4, path) * Int16Scale * f[5] + f[4];
                var norm2 = x * x + y * y + z * z;
                var sqrt = norm2 <= 1.0 ? Math.Sqrt(Math.Abs(1.0 - norm2)) : 0.0;
                var sign = (firstWord & 1) != 0 ? -1.0 : 1.0; // LSB of the first int16
                return new Quaternion((float)x, (float)y, (float)z, (float)(sign * sqrt));
            }

            case 0x0D:
            {
                var s = ReadI16(data, keyBase + 2L * key, path) * Int16Scale * f[1] + f[0];
                var theta = s * 0.5;
                var sin = Math.Sin(theta);
                return new Quaternion(
                    (float)(f[2] * sin), (float)(f[3] * sin), (float)(f[4] * sin),
                    (float)Math.Cos(theta));
            }

            case 0x0E:
            {
                var word = ReadU32(data, keyBase + 4L * key, path);
                double x = Sext((int)(word >> 21), 11) * Packed11Scale * f[1] + f[0];
                double y = Sext((int)((word >> 11) & 0x3FF), 10) * Packed10Scale * f[3] + f[2];
                double z = Sext((int)((word >> 1) & 0x3FF), 10) * Packed10Scale * f[5] + f[4];
                var norm2 = x * x + y * y + z * z;
                var sqrt = norm2 <= 1.0 ? Math.Sqrt(Math.Abs(1.0 - norm2)) : 0.0;
                var sign = (word & 1) == 0 ? 1.0 : -1.0; // bit 0: 0 -> +, 1 -> -
                return new Quaternion((float)x, (float)y, (float)z, (float)(sign * sqrt));
            }

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

    private static Quaternion ToQuaternion(double[] components) => new(
        (float)components[0], (float)components[1], (float)components[2], (float)components[3]);

    private static int Signed9(int raw) => raw >= 256 ? raw - 512 : raw;

    private static int Signed4(int raw) => raw >= 8 ? raw - 16 : raw;

    private static int Sext(int value, int bits)
    {
        var mask = 1 << (bits - 1);
        return (value & (mask - 1)) - (value & mask);
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

    private readonly record struct PackedField(int Component, int Shift, int Pair);

    private readonly record struct PackedLayout(
        int Derived, PackedField[] Fields, int ResidualShift, int SignBit);
}
