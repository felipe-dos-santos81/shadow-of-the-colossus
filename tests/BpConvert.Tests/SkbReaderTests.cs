using BpConvert.Core.Anim;

/// <summary>
/// Pinned cases for the <c>.skb</c> skeleton-bank reader (RE-1 §2, <c>tools/re/anb/probe.py</c>
/// <c>parse_skb</c>): the name blob is the bank's record list, and the leading 0x40-stride bone
/// records supply each core bone's parent index. Real-tree cases are env-gated through
/// <see cref="TestAssets"/> (fail fast when <c>SOTC_GAME_ROOT</c> is unset — never skips silently).
/// </summary>
public class SkbReaderTests
{
    [Fact]
    public void Player_bank_decodes()
    {
        var bank = SkbReader.Read(TestAssets.Sample("cp3shadow/skel/player_a.skb"));

        Assert.Equal("player_a", bank.Name);
        // The name blob is the record list: 29 core bones (`Bone_*`, each with a 0x40-byte record)
        // followed by 23 optional joints (`OptBone_*`, no record in this part bank).
        Assert.Equal(52, bank.Records.Count);
        Assert.Equal("Bone_KOSI", bank.Records[0].Name);
        Assert.Equal("OptBone_sword", bank.Records[29].Name);
        Assert.Equal("OptBone_swordcase3", bank.Records[^1].Name);

        // Parents come from the record's f14; -1 marks a root and also the record-less optional
        // names. The two roots are Bone_KOSI and Bone_HARA.
        Assert.Equal(-1, bank.Records[0].Parent);
        Assert.Equal("Bone_MOMO_L", bank.Records[1].Name);
        Assert.Equal(0, bank.Records[1].Parent);
        Assert.Equal(1, bank.Records[2].Parent); // Bone_SUNE_L under Bone_MOMO_L
        Assert.Equal("Bone_HARA", bank.Records[9].Name);
        Assert.Equal(-1, bank.Records[9].Parent);
        Assert.Equal("Bone_MUNE", bank.Records[10].Name);
        Assert.Equal(9, bank.Records[10].Parent);
        Assert.All(bank.Records.Skip(29), record => Assert.Equal(-1, record.Parent));
    }

    [Fact]
    public void Horse_bank_decodes()
    {
        var bank = SkbReader.Read(TestAssets.Sample("cp3shadow/skel/horse_a.skb"));

        Assert.Equal("horse_a", bank.Name);
        Assert.Equal(40, bank.Records.Count); // 30 core bones + 10 optional joints
        Assert.Equal("Bone_KOSI", bank.Records[0].Name);
        Assert.Equal("OptBone_boy", bank.Records[30].Name);
        Assert.Equal(-1, bank.Records[0].Parent);
        Assert.Equal(-1, bank.Records[1].Parent); // Bone_MUNE, the second root
        Assert.Equal(1, bank.Records[2].Parent); // Bone_F_KATA_L under Bone_MUNE
        Assert.All(bank.Records.Skip(30), record => Assert.Equal(-1, record.Parent));
    }

    [Fact]
    public void Minotaur_bank_decodes()
    {
        // Valus's part bank (S4): 73 names = 58 record-bearing core bones + 15 OptBone_*
        // joints, the same name-blob shape as the player/horse banks at a larger count.
        // Measured against probe.parse_skb (docs/notes/s4-recon.md §2).
        var bank = SkbReader.Read(TestAssets.Sample("cp3shadow/skel/minotaur_a.skb"));

        Assert.Equal("minotaur_a", bank.Name);
        Assert.Equal(73, bank.Records.Count);
        Assert.Equal(58, bank.Records.Count(record =>
            !record.Name.StartsWith("OptBone_", StringComparison.Ordinal)));
        Assert.Equal(15, bank.Records.Count(record =>
            record.Name.StartsWith("OptBone_", StringComparison.Ordinal)));

        // The record-bearing run: Bone_KOSI .. Bone_OYA_C_R (record 57); the optional
        // joints follow from OptBone_IKFootR to OptBone_IKHandL.
        Assert.Equal("Bone_KOSI", bank.Records[0].Name);
        Assert.Equal("Bone_OYA_C_R", bank.Records[57].Name);
        Assert.Equal("OptBone_IKFootR", bank.Records[58].Name);
        Assert.Equal("OptBone_IKHandL", bank.Records[^1].Name);

        // Parents: two roots (Bone_KOSI, Bone_HARA), the spine chain under them, the head
        // under Bone_KUBI. File order is topological — every parent precedes its record.
        Assert.Equal(-1, bank.Records[0].Parent);
        Assert.Equal(0, bank.Records[1].Parent);  // Bone_MOMO_L under Bone_KOSI
        Assert.Equal(1, bank.Records[2].Parent);  // Bone_SUNE_L under Bone_MOMO_L
        Assert.Equal("Bone_HARA", bank.Records[9].Name);
        Assert.Equal(-1, bank.Records[9].Parent);
        Assert.Equal(9, bank.Records[10].Parent);   // Bone_MUNE under Bone_HARA
        Assert.Equal(11, bank.Records[12].Parent);  // Bone_KAO under Bone_KUBI
        for (var record = 0; record < 58; record++)
            Assert.InRange(bank.Records[record].Parent, -1, record - 1);
        Assert.All(bank.Records.Skip(58), record => Assert.Equal(-1, record.Parent));
    }

    [Fact]
    public void Wrong_magic_throws()
    {
        var path = Path.Combine(Path.GetTempPath(), $"bpconvert-skb-{Guid.NewGuid():N}.skb");
        try
        {
            File.WriteAllBytes(path, new byte[] { 0xDE, 0xAD, 0xBE, 0xEF });

            Assert.Throws<InvalidDataException>(() => SkbReader.Read(path));
        }
        finally
        {
            File.Delete(path);
        }
    }
}
