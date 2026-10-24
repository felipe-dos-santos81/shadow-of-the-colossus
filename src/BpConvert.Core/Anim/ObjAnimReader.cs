using System.Buffers.Binary;
using System.Numerics;

namespace BpConvert.Core.Anim;

/// <summary>One sampled frame of an objanim track, raw game convention (no glb mirror).</summary>
/// <param name="Pos">Position (the volume centre / the parent-local offset).</param>
/// <param name="Quat">Orientation, <c>(x, y, z, w)</c>; identity for type 3.</param>
/// <param name="Vec">Size channel: half extents (box), radii x,z + half height y (cylinder), radii
/// (sphere/ellipsoid); for type 29 the fourth channel (meaning open).</param>
/// <param name="Vec2">Type 44 only: <c>(inner radius fraction, sector angle, _)</c>.</param>
public readonly record struct ObjAnimKey(Vector3 Pos, Quaternion Quat, Vector3 Vec, Vector3 Vec2);

/// <summary>One decoded objanim track (types 3, 10, 13, 29, 44, 46 only).</summary>
public sealed class ObjAnimTrack
{
    /// <summary>The track's index in the file (its aux-word index).</summary>
    public required int Index { get; init; }

    /// <summary>The record type: 3 sphere, 10 cylinder, 13 box (damage); 29 effect; 44/46 judge.</summary>
    public required int Type { get; init; }

    /// <summary>The volume / effect-condition name, e.g. <c>KNIGHT_FOOT_R</c>.</summary>
    public required string Name { get; init; }

    /// <summary>
    /// The aux word (<c>motion+0x08</c> array, <c>FUN_005fc100</c>): ≥ 0 a <c>.skb</c> record index,
    /// <c>0x10000|k</c> the k-th <c>OptBone_*</c>, −1 entity, −2 entity root, −3 skelton (hip).
    /// </summary>
    public required int Parent { get; init; }

    /// <summary>Type 44: 0 cylinder sector, 1 ellipsoid sector (<c>w0 &gt;&gt; 20</c>); type 46: 2 (box); else −1.</summary>
    public required int Kind { get; init; }

    /// <summary>One key per frame.</summary>
    public required IReadOnlyList<ObjAnimKey> Keys { get; init; }

    /// <summary>
    /// Type 29 (motion version ≥ 11): the <c>FUN_0060e370</c> windows <c>[start, start + len]</c> in
    /// frames; empty when the track fires never; <c>null</c> when the track has no event channel
    /// (or an event format the engine treats as never firing).
    /// </summary>
    public IReadOnlyList<(int Start, int End)>? Events { get; init; }

    /// <summary>
    /// Inclusive frame runs whose <see cref="ObjAnimKey.Vec"/> is non-zero: where a damage or judge
    /// volume exists.
    /// </summary>
    public IReadOnlyList<(int First, int Last)> ActiveWindows()
    {
        var runs = new List<(int, int)>();
        var start = -1;
        for (var i = 0; i < Keys.Count; i++)
        {
            var v = Keys[i].Vec;
            var on = Math.Abs(v.X) > 1e-4 || Math.Abs(v.Y) > 1e-4 || Math.Abs(v.Z) > 1e-4;
            if (on && start < 0) start = i;
            if (!on && start >= 0)
            {
                runs.Add((start, i - 1));
                start = -1;
            }
        }

        if (start >= 0) runs.Add((start, Keys.Count - 1));
        return runs;
    }
}

/// <summary>A decoded <c>objanim_*.anb</c>: the event tracks of one clip.</summary>
public sealed class ObjAnimClip
{
    /// <summary>Motion structure version byte (the corpus has 11 and 12).</summary>
    public required int Version { get; init; }

    /// <summary>Frame count (the objanim's own; equals its clip's in 97 of 100 shipped pairs).</summary>
    public required int Frames { get; init; }

    /// <summary>Playback rate in frames per second (30 × the Q12 rate word).</summary>
    public required double Fps { get; init; }

    /// <summary>The decoded tracks in file order; other record types are skipped.</summary>
    public required IReadOnlyList<ObjAnimTrack> Tracks { get; init; }
}

/// <summary>
/// Reader for the flag-set <c>objanim_*.anb</c> motion banks: the per-clip event tracks (damage
/// volumes 3/10/13, effect triggers 29 with event windows, AI judge volumes 44/46). Port of
/// <c>.superpowers/sdd/2026-10-23-s12-recon/objanim/py/objanim.py</c> over <c>tools/re/anb/records.py</c>
/// (accessor formats pinned in <c>docs/notes/re2-flagset.md</c>). Type 11 (effect emitters, hundreds
/// per clip) and every other record type are skipped. Raw game convention: the glb mirror is the
/// sidecar writer's.
/// </summary>
/// <remarks>
/// The quaternion channel shares its packed decodes with <see cref="AnbReader"/> through
/// <see cref="AnbKeyMath"/>; <see cref="AnbReader"/> itself still refuses the flag-set variant.
/// </remarks>
public static class ObjAnimReader
{
    private const int DefaultDataBase = 0x110;
    private const uint FlagSetBit = 1u << 16;
    private const int MaxTracks = 4096;
    private const int RecordStride = 12;

    // Float constants read from eboot.elf (records.py C_*).
    private const double Inv32767 = 3.05185094e-05;
    private const double Inv2047 = 0.000488519785;
    private const double Inv1023 = 0.000977517106;
    private const double Inv31 = 0.0322580636;

    private static readonly byte[] XffMagic = { 0x78, 0x66, 0x66, 0x00 };

    /// <summary>Decodes one flag-set objanim file.</summary>
    /// <exception cref="InvalidDataException">
    /// Not an xff container, or the flag-clear variant (a refusal: <see cref="IsRefusal"/> is true);
    /// or malformed: implausible structure, or a read past the end.
    /// </exception>
    public static ObjAnimClip Read(ReadOnlySpan<byte> anb)
    {
        if (anb.Length < DefaultDataBase || !anb[..4].SequenceEqual(XffMagic))
            throw Refused("not an xff container (magic mismatch)");

        var dataBase = anb.Length >= 0xB8 ? (long)U32(anb, 0xB4) : DefaultDataBase;
        if (dataBase < 0x100 || dataBase >= anb.Length) dataBase = DefaultDataBase;

        var motion = dataBase + U32(anb, 0x10);
        if (motion + 0x20 > anb.Length)
            throw Bad($"motion structure out of range (0x{motion:X})");
        var word0 = U32(anb, motion);
        var version = (int)(word0 & 0xFF);
        if (version is < 1 or > 15)
            throw Bad($"implausible version byte {version} at 0x{motion:X}");
        var frames = U16(anb, motion + 4);
        var tracks = U32(anb, motion + 0x14);
        if (frames < 1 || tracks < 1 || tracks > MaxTracks)
            throw Bad($"implausible frames/tracks ({frames}/{tracks})");
        if ((word0 & FlagSetBit) == 0)
        {
            throw Refused(
                $"flag-clear motion bank (version word 0x{word0:X8} bit 16 clear); "
                + "that variant is AnbReader's, not an objanim event-track file");
        }

        var aux = U32(anb, motion + 0x08);
        var recordsOffset = U32(anb, motion + 0x10);
        if (recordsOffset < 0x20 || dataBase + recordsOffset + (long)tracks * RecordStride > anb.Length)
            throw Bad($"record array out of range (offset 0x{recordsOffset:X}, {tracks} tracks)");

        var rate = U16(anb, motion + 6) >> 4;
        var fps = 30.0 * (rate != 0 ? rate / 4096.0 : 1.0);

        var result = new List<ObjAnimTrack>();
        for (var track = 0; track < tracks; track++)
        {
            var record = dataBase + recordsOffset + (long)track * RecordStride;
            var type = (int)(U32(anb, record) & 0xFF);
            if (type is not (3 or 10 or 13 or 29 or 44 or 46))
                continue;

            var parent = aux != 0 ? (int)U32(anb, dataBase + aux + 4L * track) : -1;
            result.Add(DecodeTrack(anb, dataBase, version, frames, track, type, record, parent));
        }

        return new ObjAnimClip { Version = version, Frames = frames, Fps = fps, Tracks = result };
    }

    private static ObjAnimTrack DecodeTrack(
        ReadOnlySpan<byte> d, long b0, int version, int frames, int index, int type, long record, int parent)
    {
        var nameOffset = Ptr(b0, U32(d, record + 4));
        var blob = Ptr(b0, U32(d, record + 8));
        var name = CString(d, nameOffset) ?? throw Bad($"track {index}: no name at 0x{nameOffset:X}");
        var w0 = U32(d, blob);
        var p1 = U32(d, blob + 4);
        var p2 = U32(d, blob + 8);
        var p3 = U32(d, blob + 12);
        var p4 = U32(d, blob + 16);

        var keys = new ObjAnimKey[frames];
        IReadOnlyList<(int Start, int End)>? events = null;
        var kind = -1;

        switch (type)
        {
            case 10 or 13 or 46:
            {
                // caseD_9 / FUN_00600ef0: A pos (+4), B quat (+8), C size (+0xC, e640).
                int fa = (int)((w0 >> 16) & 0xF), fb = (int)((w0 >> 8) & 0xF), fc = (int)(w0 & 0xF);
                for (var k = 0; k < frames; k++)
                    keys[k] = new(Pos(d, b0, fa, p1, k), Quat(d, b0, fb, p2, k), Size(d, b0, fc, p3, k), default);
                kind = type == 46 ? 2 : -1;
                break;
            }

            case 44:
            {
                // caseD_2a: 5-bit formats; D (+0x10) = (inner, angle, _).
                int fa = (int)((w0 >> 15) & 0x1F), fb = (int)((w0 >> 10) & 0x1F);
                int fc = (int)((w0 >> 5) & 0x1F), fd = (int)(w0 & 0x1F);
                for (var k = 0; k < frames; k++)
                    keys[k] = new(Pos(d, b0, fa, p1, k), Quat(d, b0, fb, p2, k), Size(d, b0, fc, p3, k),
                        Size(d, b0, fd, p4, k));
                kind = (int)(w0 >> 20);
                break;
            }

            case 29 when version < 0xB:
            {
                // caseD_1c, old branch: byte formats, no event channel.
                int fa = (int)((w0 >> 16) & 0xFF), fb = (int)((w0 >> 8) & 0xFF), fc = (int)(w0 & 0xFF);
                for (var k = 0; k < frames; k++)
                    keys[k] = new(Pos(d, b0, fa, p1, k), Quat(d, b0, fb, p2, k), Pos(d, b0, fc, p3, k), default);
                break;
            }

            case 29:
            {
                // caseD_1c: A pos (+4), B quat (+8), event list (+0xC), D vec (+0x10, e0c0).
                int fa = (int)((w0 >> 12) & 0xF), fb = (int)((w0 >> 8) & 0xF);
                int fe = (int)((w0 >> 4) & 0xF), fd = (int)(w0 & 0xF);
                for (var k = 0; k < frames; k++)
                    keys[k] = new(Pos(d, b0, fa, p1, k), Quat(d, b0, fb, p2, k), Pos(d, b0, fd, p4, k), default);
                events = Events(d, b0, p3, fe);
                break;
            }

            case 3:
            {
                // caseD_2 (subobj_damage): A pos (+4, e0c0), C size (+8, e640); no rotation.
                int fa = (int)((w0 >> 8) & 0xFF), fc = (int)(w0 & 0xFF);
                for (var k = 0; k < frames; k++)
                    keys[k] = new(Pos(d, b0, fa, p1, k), Quaternion.Identity, Size(d, b0, fc, p2, k), default);
                break;
            }
        }

        return new ObjAnimTrack
        {
            Index = index,
            Type = type,
            Name = name,
            Parent = parent,
            Kind = kind,
            Keys = keys,
            Events = events,
        };
    }

    // ---- the event channel (FUN_0060e370): {start, len} pairs, len 0 ends ----

    private static IReadOnlyList<(int Start, int End)>? Events(ReadOnlySpan<byte> d, long b0, uint ptr, int format)
    {
        if (ptr == 0) return [];
        var size = format switch { 0 => 1, 1 => 2, 2 => 4, _ => 0 };
        if (size == 0) return null; // other formats: the engine returns -1 (never fires)

        var list = new List<(int, int)>();
        for (var o = Ptr(b0, ptr); o + 2L * size <= d.Length; o += 2L * size)
        {
            var start = (int)Unsigned(d, o, size);
            var len = (int)Unsigned(d, o + size, size);
            if (len == 0) break;
            list.Add((start, start + len));
        }

        return list;
    }

    // ---- the three accessors (records.py sample_e0c0 / sample_bba0 / sample_e640) ----

    private static Vector3 Pos(ReadOnlySpan<byte> d, long b0, int fmt, uint ptr, int key)
    {
        var desc = Ptr(b0, ptr);
        switch (fmt)
        {
            case 0:
                return V3(d, desc);
            case 2:
            {
                var data = Ptr(b0, U32(d, desc + 24));
                Span<float> axis = stackalloc float[3];
                for (var i = 0; i < 3; i++)
                    axis[i] = (float)(F32(d, desc + 8 * i)
                        + I16(d, data + key * 6L + 2 * i) * (double)F32(d, desc + 4 + 8 * i) * Inv32767);
                return new Vector3(axis[0], axis[1], axis[2]);
            }
            default:
                return V3(d, desc + key * 12L);
        }
    }

    private static Vector3 Size(ReadOnlySpan<byte> d, long b0, int fmt, uint ptr, int key)
    {
        var desc = Ptr(b0, ptr);
        switch (fmt)
        {
            case 0:
                return V3(d, desc);
            case 2:
            {
                var word = U32(d, Ptr(b0, U32(d, desc + 24)) + key * 4L);
                return new Vector3(
                    (float)(F32(d, desc) + (word >> 21) * (double)F32(d, desc + 4) * Inv2047),
                    (float)(F32(d, desc + 8) + ((word >> 10) & 0x7FF) * (double)F32(d, desc + 12) * Inv2047),
                    (float)(F32(d, desc + 16) + (word & 0x3FF) * (double)F32(d, desc + 20) * Inv1023));
            }
            case 3:
            {
                var word = U16(d, Ptr(b0, U32(d, desc + 24)) + key * 2L);
                return new Vector3(
                    (float)(F32(d, desc) + (word >> 11) * (double)F32(d, desc + 4) * Inv31),
                    (float)(F32(d, desc + 8) + ((word >> 5) & 0x3F) * (double)F32(d, desc + 12) * Inv31),
                    (float)(F32(d, desc + 16) + (word & 0x1F) * (double)F32(d, desc + 20) * Inv31));
            }
            default:
                return V3(d, desc + key * 12L);
        }
    }

    private static Quaternion Quat(ReadOnlySpan<byte> d, long b0, int fmt, uint ptr, int key)
    {
        var desc = Ptr(b0, ptr);
        switch (fmt)
        {
            case 0:
                return new Quaternion(F32(d, desc), F32(d, desc + 4), F32(d, desc + 8), F32(d, desc + 12));
            case 1:
            {
                var o = desc + key * 16L;
                float x = F32(d, o + 4), y = F32(d, o + 8), z = F32(d, o + 12);
                var w = Math.Sqrt(Math.Abs(1.0 - Math.Min(1.0, x * x + y * y + z * z)));
                if ((Byte(d, o + 1) & 0x80) != 0) w = -w;
                return new Quaternion(x, y, z, (float)w);
            }
            case 2:
            {
                float ax = F32(d, desc), ay = F32(d, desc + 4), az = F32(d, desc + 8);
                var angle = F32(d, Ptr(b0, U32(d, desc + 12)) + key * 4L);
                double s = Math.Sin(angle), c = Math.Cos(angle);
                return new Quaternion((float)(ax * s), (float)(ay * s), (float)(az * s), (float)c);
            }
            case >= 3 and <= 6:
            {
                var word = U32(d, Ptr(b0, U32(d, desc + 32)) + key * 4L);
                return AnbKeyMath.Packed((byte)fmt, word, Floats(d, desc, 8));
            }
            case 10:
            {
                var o = desc + key * 12L;
                float x = F32(d, o), y = F32(d, o + 4), z = F32(d, o + 8);
                var w = Math.Sqrt(Math.Abs(1.0 - (x * x + y * y + z * z)));
                if ((U32(d, o) & 1) != 0) w = -w;
                return new Quaternion(x, y, z, (float)w);
            }
            case 0xB:
            {
                var o = desc + key * 6L;
                double x = I16(d, o) * Inv32767, y = I16(d, o + 2) * Inv32767, z = I16(d, o + 4) * Inv32767;
                var w = Math.Sqrt(Math.Abs(1.0 - (x * x + y * y + z * z)));
                if ((U16(d, o) & 1) != 0) w = -w;
                return new Quaternion((float)x, (float)y, (float)z, (float)w);
            }
            case 0xC:
            {
                var o = Ptr(b0, U32(d, desc + 24)) + key * 6L;
                return AnbKeyMath.Int16Triple(I16(d, o), I16(d, o + 2), I16(d, o + 4), U16(d, o), Floats(d, desc, 6));
            }
            case 0xD:
                return AnbKeyMath.Angle(I16(d, Ptr(b0, U32(d, desc + 20)) + key * 2L), Floats(d, desc, 5));
            case 0xE:
                return AnbKeyMath.Packed11(U32(d, Ptr(b0, U32(d, desc + 24)) + key * 4L), Floats(d, desc, 6));
            default:
                return new Quaternion(0, 0, 0, 0); // > 0xE: the engine's zero case
        }
    }

    // ---- bounds-checked little-endian reads (a bad pointer is InvalidDataException, never a crash) ----

    private static long Ptr(long dataBase, uint value) => dataBase + (value & 0x7FFFFFFF);

    private static Vector3 V3(ReadOnlySpan<byte> d, long o) => new(F32(d, o), F32(d, o + 4), F32(d, o + 8));

    private static float[] Floats(ReadOnlySpan<byte> d, long o, int count)
    {
        var f = new float[count];
        for (var i = 0; i < count; i++) f[i] = F32(d, o + 4L * i);
        return f;
    }

    private static ReadOnlySpan<byte> At(ReadOnlySpan<byte> d, long offset, int size)
    {
        if (offset < 0 || offset + size > d.Length)
            throw Bad($"read past end of file at 0x{offset:X}");
        return d.Slice((int)offset, size);
    }

    private static byte Byte(ReadOnlySpan<byte> d, long o) => At(d, o, 1)[0];

    private static ushort U16(ReadOnlySpan<byte> d, long o) => BinaryPrimitives.ReadUInt16LittleEndian(At(d, o, 2));

    private static short I16(ReadOnlySpan<byte> d, long o) => BinaryPrimitives.ReadInt16LittleEndian(At(d, o, 2));

    private static uint U32(ReadOnlySpan<byte> d, long o) => BinaryPrimitives.ReadUInt32LittleEndian(At(d, o, 4));

    private static float F32(ReadOnlySpan<byte> d, long o) => BinaryPrimitives.ReadSingleLittleEndian(At(d, o, 4));

    private static uint Unsigned(ReadOnlySpan<byte> d, long o, int size) => size switch
    {
        1 => Byte(d, o),
        2 => U16(d, o),
        _ => U32(d, o),
    };

    private static string? CString(ReadOnlySpan<byte> d, long offset)
    {
        if (offset < 0 || offset >= d.Length) return null;
        var tail = d[(int)offset..];
        var end = tail[..Math.Min(tail.Length, 128)].IndexOf((byte)0);
        if (end <= 0) return null;
        foreach (var c in tail[..end])
            if (c is < 0x20 or > 0x7E) return null;
        return System.Text.Encoding.ASCII.GetString(tail[..end]);
    }

    private static InvalidDataException Bad(string message) => new($"objanim: {message}");

    private const string RefusalKey = "objanim-refusal";

    private static InvalidDataException Refused(string message)
    {
        var e = new InvalidDataException($"objanim: {message}");
        e.Data[RefusalKey] = true;
        return e;
    }

    /// <summary>True for an exception <see cref="Read"/> raised as a refusal (input of another kind), not a parse fault.</summary>
    public static bool IsRefusal(Exception e) => e.Data.Contains(RefusalKey);
}
