using System.Buffers.Binary;
using System.Text;

namespace BpConvert.Core.Anim;

/// <summary>
/// One entry of a <c>.skb</c> part bank, in name-blob order. Core bones carry their parent from
/// the bank's 0x40-byte bone record; the optional <c>OptBone_*</c> joints that trail the record run
/// are joints only and report <see cref="Parent"/> = -1 (see <see cref="SkbBank.Records"/>).
/// </summary>
public sealed class SkbRecord
{
    /// <summary>Bone/joint name, e.g. <c>Bone_KOSI</c> or <c>OptBone_sword</c>.</summary>
    public required string Name { get; init; }

    /// <summary>
    /// Parent index in the bank's record list, or -1: a root (<c>Bone_KOSI</c>, <c>Bone_HARA</c>)
    /// or an <c>OptBone_*</c> name with no 0x40-byte record in this part bank.
    /// </summary>
    public required int Parent { get; init; }
}

/// <summary>
/// One decoded <c>.skb</c> skeleton part bank (the <c>xff</c> container, RE-1 §2): the name blob in
/// file order plus the parent links of the leading 0x40-stride bone records.
/// </summary>
/// <remarks>
/// The bank is a model part's subset of the full skeleton, not the whole skeleton: the core bones
/// (which have records) come first in the blob, then the bank's optional joints. Track i of a clip
/// decodes against <see cref="Records"/>[i] (see <see cref="TrackMap"/>).
/// </remarks>
public sealed class SkbBank
{
    /// <summary>File stem, e.g. <c>player_a</c>.</summary>
    public required string Name { get; init; }

    /// <summary>
    /// The name blob in file order. <c>player_a.skb</c> carries 52 entries: 29 core bones (with
    /// <see cref="SkbRecord.Parent"/> from their record) followed by 23 optional joints (parent -1).
    /// </summary>
    public required List<SkbRecord> Records { get; init; }
}

/// <summary>
/// Reader for plaintext <c>.skb</c> skeleton part banks (RE-1 §2; the C# port of
/// <c>tools/re/anb/probe.py</c>'s <c>parse_skb</c>): the <c>xff\0</c> header checks, the
/// <c>Bone_*</c>/<c>OptBone_*</c> NUL-terminated name blob, the tail tables read back-to-front from
/// EOF, and the leading 0x40-byte bone-record run (first u32 = blob-relative name offset, f14 =
/// parent index).
/// </summary>
/// <remarks>
/// The blob offsets and the tail-table offsets are relative to the container's string/data base
/// <c>0x110</c>. The three trailing tag-<c>0x102</c> table entries are section descriptors, not
/// records (two for <c>obj_bat_a.skb</c>); they are skipped by the 0x40-stride rule. Malformed files
/// throw <see cref="InvalidDataException"/> whose message starts with the input path.
/// </remarks>
public static class SkbReader
{
    private const int DataBase = 0x110;
    private const int HeaderLength = 0x70;
    private const int RecordStride = 0x40;
    private const int RecordNameOffsetField = 0x00;
    private const int RecordParentField = 0x38; // f14
    private const uint RecordTag = 0x102;
    private const uint OffsetTag = 0x000;
    private const int MaxTailEntries = 65536; // measured max in the family: 4,517 (.anb)

    private static readonly byte[] XffMagic = { 0x78, 0x66, 0x66, 0x00 };

    /// <summary>Reads and decodes one <c>.skb</c> part bank.</summary>
    /// <param name="path">Path to the <c>.skb</c> file.</param>
    /// <returns>The bank's records in name-blob order; <see cref="SkbBank.Name"/> is the file stem.</returns>
    /// <exception cref="InvalidDataException">
    /// The file is not a well-formed plaintext <c>.skb</c> (bad magic/size field, no
    /// <c>Bone_*</c>/<c>OptBone_*</c> name blob, no tail tables, or a record out of range).
    /// </exception>
    public static SkbBank Read(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        var data = File.ReadAllBytes(path);

        if (data.Length < HeaderLength || !data.AsSpan(0, XffMagic.Length).SequenceEqual(XffMagic))
            throw Invalid(path, "not an xff container (magic mismatch)");
        var sizeField = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(0x14));
        if (sizeField != (uint)data.Length)
            throw Invalid(path, $"size field @0x14 = {sizeField} != file size {data.Length}");

        var names = ExtractNames(data);
        if (names.Count == 0)
            throw Invalid(path, "no Bone_/OptBone_ name blob (not a .skb part bank)");
        var blobStart = names[0].Offset;

        var tail = ReadTailTables(data);
        if (tail.Count == 0)
            throw Invalid(path, "no tail tables (truncated container)");

        var parentByRecordStart = ReadRecordParents(data, path, tail, blobStart);

        var records = new List<SkbRecord>(names.Count);
        foreach (var (offset, name) in names)
            records.Add(new SkbRecord { Name = name, Parent = parentByRecordStart.GetValueOrDefault(offset, -1) });

        return new SkbBank { Name = Path.GetFileNameWithoutExtension(path), Records = records };
    }

    /// <summary>
    /// The NUL-terminated printable strings that start with <c>Bone_</c>/<c>OptBone_</c>, in file
    /// order — the probe's <c>[ -~]+\0</c> rule over the whole file.
    /// </summary>
    private static List<(int Offset, string Text)> ExtractNames(byte[] data)
    {
        var names = new List<(int, string)>();
        var index = 0;
        while (index < data.Length)
        {
            var nul = Array.IndexOf(data, (byte)0, index);
            if (nul < 0)
                break;
            var start = nul;
            while (start > 0 && data[start - 1] is >= 0x20 and <= 0x7E)
                start--;
            if (nul > start)
            {
                var text = Encoding.Latin1.GetString(data, start, nul - start);
                if (text.StartsWith("Bone_", StringComparison.Ordinal) ||
                    text.StartsWith("OptBone_", StringComparison.Ordinal))
                    names.Add((start, text));
            }

            index = nul + 1;
        }

        return names;
    }

    /// <summary>
    /// Walks the tail tables back-to-front from EOF: 8-byte <c>{u32 offset, u32 tag}</c> entries
    /// while the tag is 0x102 or 0x000 (exact for <c>.skb</c>; RE-1 §1.1/§2.1). Returns entries in
    /// file order.
    /// </summary>
    private static List<(uint Offset, uint Tag)> ReadTailTables(byte[] data)
    {
        var entries = new List<(uint, uint)>();
        if (data.Length < 16)
            return entries;

        var offset = data.Length - 8;
        while (offset >= 0 && entries.Count < MaxTailEntries)
        {
            var tag = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(offset + 4));
            if (tag != RecordTag && tag != OffsetTag)
                break;
            entries.Add((BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(offset)), tag));
            offset -= 8;
        }

        entries.Reverse();
        return entries;
    }

    /// <summary>
    /// Reads the leading run of tag-0x102 bone records (the probe's <c>skb_records</c> stride rule:
    /// each entry starts exactly 0x40 bytes after the previous one, which stops before the trailing
    /// section descriptors). Returns the parent index keyed by the record's name start (blob-relative
    /// f0 added to the blob base).
    /// </summary>
    private static Dictionary<int, int> ReadRecordParents(
        byte[] data, string path, List<(uint Offset, uint Tag)> tail, int blobStart)
    {
        var parents = new Dictionary<int, int>();
        uint? first = null;
        var count = 0;
        foreach (var (offset, tag) in tail)
        {
            if (tag != RecordTag)
                continue;

            var recordOffset = (long)blobStart + offset;
            if (recordOffset + RecordStride > data.Length)
                throw Invalid(path, $"bone record @0x{offset:X} out of range");
            if (first is null)
            {
                first = offset;
            }
            else if (offset != first + (uint)(RecordStride * count))
            {
                break; // a trailing section descriptor, not part of the record run
            }

            count++;
            var nameOffset = BinaryPrimitives.ReadUInt32LittleEndian(
                data.AsSpan((int)recordOffset + RecordNameOffsetField));
            var parent = BinaryPrimitives.ReadInt32LittleEndian(
                data.AsSpan((int)recordOffset + RecordParentField));
            parents[(int)(blobStart + nameOffset)] = parent;
        }

        return parents;
    }

    private static InvalidDataException Invalid(string path, string detail) =>
        new($"{path}: {detail}");
}
