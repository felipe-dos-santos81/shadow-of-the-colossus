using System.Numerics;

namespace BpConvert.Core.Anim;

/// <summary>
/// The quaternion key decodes shared by the two <c>.anb</c> readers: <see cref="AnbReader"/> (the
/// flag-clear per-type blobs) and <see cref="ObjAnimReader"/> (the flag-set accessor formats,
/// where format N is the same math under a different descriptor addressing). Pure functions of
/// the descriptor floats and the raw key bits: no I/O, no behaviour of their own
/// (<c>docs/notes/re2-keys.md</c> §1, <c>tools/re/anb/records.py</c> <c>sample_bba0</c>).
/// </summary>
internal static class AnbKeyMath
{
    // The engine constants (docs/notes/re2-keys.md §2.1).
    private const double Int16Scale = 1.0 / 32767.0; // types 0xC/0xD and the root series
    private const double Packed9Scale = 1.0 / 255.0; // the signed 9-bit fields of types 3–6
    private const double Packed11Scale = 1.0 / 1023.0; // type 0xE x
    private const double Packed10Scale = 1.0 / 511.0; // type 0xE y/z
    private const double ResidualScale = 1.0 / 7.0; // the 4-bit residual of types 3–6

    /// <summary>The int16 → [-1, 1] scale (types 0xC/0xD and the root series).</summary>
    public const double Int16 = Int16Scale;

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

    public static bool IsPacked(byte type) => PackedLayouts.ContainsKey(type);

    /// <summary>Types/formats 3–6: three signed 9-bit fields + a 4-bit residual + a sign bit in one word.</summary>
    public static Quaternion Packed(byte type, uint word, float[] f)
    {
        var layout = PackedLayouts[type];
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
        return new Quaternion((float)comp[0], (float)comp[1], (float)comp[2], (float)comp[3]);
    }

    /// <summary>Type/format 0xC: three int16 (the LSB of the first is the sign of w).</summary>
    public static Quaternion Int16Triple(short a, short b, short c, ushort firstWord, float[] f)
    {
        double x = a * Int16Scale * f[1] + f[0];
        double y = b * Int16Scale * f[3] + f[2];
        double z = c * Int16Scale * f[5] + f[4];
        var norm2 = x * x + y * y + z * z;
        var sqrt = norm2 <= 1.0 ? Math.Sqrt(Math.Abs(1.0 - norm2)) : 0.0;
        var sign = (firstWord & 1) != 0 ? -1.0 : 1.0;
        return new Quaternion((float)x, (float)y, (float)z, (float)(sign * sqrt));
    }

    /// <summary>Type/format 0xD: one int16 angle about the descriptor's axis (f2..f4).</summary>
    public static Quaternion Angle(short raw, float[] f)
    {
        var s = raw * Int16Scale * f[1] + f[0];
        var theta = s * 0.5;
        var sin = Math.Sin(theta);
        return new Quaternion(
            (float)(f[2] * sin), (float)(f[3] * sin), (float)(f[4] * sin), (float)Math.Cos(theta));
    }

    /// <summary>Type/format 0xE: x 11-bit, y/z 10-bit, bit 0 the sign of w.</summary>
    public static Quaternion Packed11(uint word, float[] f)
    {
        double x = Sext((int)(word >> 21), 11) * Packed11Scale * f[1] + f[0];
        double y = Sext((int)((word >> 11) & 0x3FF), 10) * Packed10Scale * f[3] + f[2];
        double z = Sext((int)((word >> 1) & 0x3FF), 10) * Packed10Scale * f[5] + f[4];
        var norm2 = x * x + y * y + z * z;
        var sqrt = norm2 <= 1.0 ? Math.Sqrt(Math.Abs(1.0 - norm2)) : 0.0;
        var sign = (word & 1) == 0 ? 1.0 : -1.0; // bit 0: 0 -> +, 1 -> -
        return new Quaternion((float)x, (float)y, (float)z, (float)(sign * sqrt));
    }

    private static int Signed9(int raw) => raw >= 256 ? raw - 512 : raw;

    private static int Signed4(int raw) => raw >= 8 ? raw - 16 : raw;

    private static int Sext(int value, int bits)
    {
        var mask = 1 << (bits - 1);
        return (value & (mask - 1)) - (value & mask);
    }

    private readonly record struct PackedField(int Component, int Shift, int Pair);

    private readonly record struct PackedLayout(
        int Derived, PackedField[] Fields, int ResidualShift, int SignBit);
}
