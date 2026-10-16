using AquaModelLibrary.Data.BluePoint.CTXR;
using BpConvert.Core.Textures;
using SixLabors.ImageSharp;
using SixLabors.ImageSharp.PixelFormats;

public class CtxrConverterTests : IDisposable
{
    // xunit creates one test-class instance per test, so each test gets a private temp directory.
    private readonly string _dir = Directory.CreateTempSubdirectory("bpconvert-ctxr-").FullName;

    public void Dispose() => Directory.Delete(_dir, recursive: true);

    // ---------------------------------------------------------------------------------------
    // Pinned Task 3 cases (env-gated on real samples; SOTC_GAME_ROOT must be set).
    // ---------------------------------------------------------------------------------------

    [Fact]
    public void Convert_PngVariant_WritesImage()
    {
        var file = TestAssets.Sample("misc/boot/_ps4/saveshadow_scee_tr.ctxr");
        Assert.True(File.Exists(file));
        AssertMagic(file, "8950");

        var outPng = Path.Combine(_dir, "saveshadow.png");
        var result = CtxrConverter.Convert(file, outPng);

        Assert.True(File.Exists(outPng));
        Assert.Equal("png", result.Variant);
        Assert.True(result.Width > 0);
        Assert.True(result.Height > 0);
        // The PNG payload is stripped verbatim — output must be byte-identical to the input.
        Assert.Equal(File.ReadAllBytes(file), File.ReadAllBytes(outPng));
    }

    [Fact]
    public void Convert_OodleVariant_WritesImage()
    {
        var file = TestAssets.Sample("textures/ocean/flowmaps/_ps4/neutral_flowmap_nml.ctxr");
        Assert.True(File.Exists(file));
        AssertMagic(file, "8c0a");

        var outPng = Path.Combine(_dir, "neutral_flowmap_nml.png");
        var result = CtxrConverter.Convert(file, outPng);

        Assert.True(File.Exists(outPng));
        Assert.Equal("8c0a", result.Variant);
        Assert.True(result.Width > 0);
        Assert.True(result.Height > 0);

        // Decode the written PNG with an independent decoder (test-only SixLabors.ImageSharp)
        // and check the actual pixels. This sample is a *neutral* flowmap — BC5 (128, 128) in
        // every texel — so the meaningful assertion is the exact expected value, not variety;
        // BCnEncoder.Net decodes the same file byte-for-byte identically (reconnaissance).
        using var img = Image.Load<Rgba32>(outPng);
        Assert.Equal(result.Width, img.Width);
        Assert.Equal(result.Height, img.Height);
        Assert.All(DistinctPixels(img), p => Assert.Equal(new Rgba32(128, 128, 0, 255), p));
    }

    [Fact]
    public void Convert_OodleVariant_SampleWithDetail_WritesVaryingImage()
    {
        // Same requirement as the pinned test above, on a sample that actually carries detail
        // (the pinned neutral flowmap is uniformly 128/128 by design).
        var file = TestAssets.Sample("textures/ocean/dampenmaps/_ps4/hall_05_dampen.ctxr");
        Assert.True(File.Exists(file));
        AssertMagic(file, "8c0a");

        var outPng = Path.Combine(_dir, "hall_05_dampen.png");
        var result = CtxrConverter.Convert(file, outPng);

        Assert.True(File.Exists(outPng));
        Assert.Equal("8c0a", result.Variant);
        Assert.Equal(512, result.Width);
        Assert.Equal(512, result.Height);
        using var img = Image.Load<Rgba32>(outPng);
        Assert.True(DistinctPixels(img).Count >= 2, "decoded PNG has fewer than 2 distinct RGBA values");
    }

    [Fact]
    public void Convert_UnknownVariant_ThrowsWithPath()
    {
        var file = Path.Combine(_dir, "unknown.ctxr");
        File.WriteAllBytes(file, new byte[] { 0xDE, 0xAD, 0xBE, 0xEF });

        var ex = Assert.ThrowsAny<Exception>(() => CtxrConverter.Convert(file, Path.Combine(_dir, "unknown.png")));

        Assert.Contains(file, ex.Message);
    }

    // ---------------------------------------------------------------------------------------
    // Additional Task 3 coverage (not pinned by the brief).
    // ---------------------------------------------------------------------------------------

    [Fact]
    public void Convert_UncompressedRgba8_ReportsOtherVariant()
    {
        var file = TestAssets.Sample("textures/test/ted/_ps4/random_noise.ctxr");
        Assert.True(File.Exists(file));
        AssertMagic(file, "0000");   // uncompressed CTXR, texture format 0x0 (R8G8B8A8_UNORM)

        var outPng = Path.Combine(_dir, "random_noise.png");
        var result = CtxrConverter.Convert(file, outPng);

        // A container that is neither PNG nor 8C0A/CC0A but still decodes reports "other".
        Assert.Equal("other", result.Variant);
        Assert.Equal(256, result.Width);
        Assert.Equal(256, result.Height);
        using var img = Image.Load<Rgba32>(outPng);
        Assert.True(DistinctPixels(img).Count >= 2);
    }

    [Fact]
    public void Convert_ExternalMipTexture_AssemblesMip0()
    {
        // Containers with external .ctxc mip chunks: we assemble mip 0 ourselves (the library
        // cannot read those chunks on macOS). Wander-class sample, BC1 2048x2048, 3 external levels.
        var file = TestAssets.Sample("textures/shadow/characters/wander/_ps4/wander_skin_col.ctxr");
        Assert.True(File.Exists(file));
        AssertMagic(file, "8c0a");

        var outPng = Path.Combine(_dir, "wander_skin_col.png");
        var result = CtxrConverter.Convert(file, outPng);

        Assert.True(File.Exists(outPng));
        Assert.Equal("8c0a", result.Variant);
        Assert.Equal(2048, result.Width);
        Assert.Equal(2048, result.Height);
        using var img = Image.Load<Rgba32>(outPng);
        Assert.Equal(result.Width, img.Width);
        Assert.Equal(result.Height, img.Height);
        Assert.True(DistinctPixels(img).Count >= 1_000,
            $"decoded PNG has only {DistinctPixels(img).Count} distinct RGBA values");
    }

    [Fact]
    public void AssembleExternalMip0_ReturnsExactlyTheTopMip()
    {
        var file = TestAssets.Sample("textures/shadow/characters/wander/_ps4/wander_skin_col.ctxr");
        var ctxr = new CTXR(File.ReadAllBytes(file));
        Assert.Equal(3, ctxr.externalMipCount);

        var mip0 = CtxrConverter.AssembleExternalMip0(ctxr, file);

        // BC1: ceil(w/4) * ceil(h/4) * 8 bytes per block.
        var expected = ((ctxr.GetWidth() + 3) / 4) * ((ctxr.GetHeight() + 3) / 4) * 8;
        Assert.Equal(expected, mip0.Length);
        // The container's own level-0 reference declares the same byte count (4,194,304 for the
        // 2048x2048 BC5 flowmap, 2,097,152 here) — a second, container-side cross-check.
        Assert.Equal(ctxr.mipPaths.Single(p => p.mipLevel == 0).bufferSize, mip0.Length);
    }

    [Fact]
    public void AssembleExternalMip0_UnresolvableChunk_ThrowsWithReason()
    {
        var file = TestAssets.Sample("textures/shadow/characters/wander/_ps4/wander_skin_col.ctxr");
        var ctxr = new CTXR(File.ReadAllBytes(file));
        ctxr.mipPaths.Clear();
        ctxr.mipPaths.Add(new CTXRExternalReference
        {
            mipLevel = 0,
            externalMipReference = "$/textures/does/not/****/exist.chunk0.ctxc",
        });

        var ex = Assert.ThrowsAny<Exception>(() => CtxrConverter.AssembleExternalMip0(ctxr, file));

        Assert.Contains("exist.chunk0.ctxc", ex.Message);
        Assert.Contains("_cmn", ex.Message);
    }

    [Fact]
    public void Convert_NegativeDimensions_ThrowsWithPath()
    {
        // Permanently unassemblable: the container's SOTC width/height bytes decode to a negative
        // width (-16000x692), so there is no image to write.
        var file = TestAssets.Sample("textures/shadow/ui/_ps4/hdr_calibration_image.ctxr");
        Assert.True(File.Exists(file));
        var outPng = Path.Combine(_dir, "hdr_calibration_image.png");

        var ex = Assert.ThrowsAny<Exception>(() => CtxrConverter.Convert(file, outPng));

        Assert.Contains(file, ex.Message);
        Assert.False(File.Exists(outPng), "no output file may be written on failure");
    }

    // --- Synthetic block decoders (no game assets required) ---------------------------------

    [Fact]
    public void Decode_Bc1_OpaqueBlock_FourColorMode()
    {
        // c0 (red) > c1 (blue) selects the 4-colour mode; every pixel index is 3 → palette[3].
        var block = Bc1Block(0xF800, 0x001F, 0xFF, 0xFF, 0xFF, 0xFF);

        var rgba = BcnDecoder.DecodeToRgba(block, DdsFormat.Bc1, 4, 4);

        // palette[3] = (c0 + 2*c1)/3 = (85, 0, 170), alpha 255.
        AssertAll(rgba, r: 85, g: 0, b: 170, a: 255);
    }

    [Fact]
    public void Decode_Bc1_PunchThroughBlock_IsTransparentBlack()
    {
        // c0 (blue) < c1 (red) selects the punch-through mode; every pixel index is 3 → transparent.
        var block = Bc1Block(0x001F, 0xF800, 0xFF, 0xFF, 0xFF, 0xFF);

        var rgba = BcnDecoder.DecodeToRgba(block, DdsFormat.Bc1, 4, 4);

        AssertAll(rgba, r: 0, g: 0, b: 0, a: 0);
    }

    [Fact]
    public void Decode_Bc2_ExplicitAlphaNibbles()
    {
        // Alpha rows are 4-bit; byte 0 holds pixel 0 in the low nibble and pixel 1 in the high one.
        var block = new byte[16];
        block[0] = 0xF0;                                  // pixel 0 → 0x0, pixel 1 → 0xF
        WriteBc1Color(block.AsSpan(8), 0xF800, 0x001F, 0x00, 0x00, 0x00, 0x00);   // all pixels → red

        var rgba = BcnDecoder.DecodeToRgba(block, DdsFormat.Bc2, 4, 4);

        Assert.Equal(new byte[] { 255, 0, 0, 0 }, Pixel(rgba, 0));      // alpha 0x0 * 17 = 0
        Assert.Equal(new byte[] { 255, 0, 0, 255 }, Pixel(rgba, 1));    // alpha 0xF * 17 = 255
        Assert.Equal(new byte[] { 255, 0, 0, 0 }, Pixel(rgba, 2));
    }

    [Fact]
    public void Decode_Bc3_InterpolatedAlpha()
    {
        // a0 = 255 > a1 = 0 → 8-value mode; pixel i uses palette[i % 8] (3 bits per pixel, LSB first).
        byte[] expected =
        [
            255, 0, 218, 182, 145, 109, 72, 36,      // (6*a0+a1)/7 … (a0+6*a1)/7, integer division
            255, 0, 218, 182, 145, 109, 72, 36,
        ];
        byte[] indices = [0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA];   // indices 0,1,…,7,0,1,…,7
        var block = new byte[16];
        block[0] = 255;
        block[1] = 0;
        indices.CopyTo(block, 2);
        WriteBc1Color(block.AsSpan(8), 0xF800, 0x001F, 0x00, 0x00, 0x00, 0x00);   // all pixels → red

        var rgba = BcnDecoder.DecodeToRgba(block, DdsFormat.Bc3, 4, 4);

        for (int i = 0; i < 16; i++)
            Assert.Equal(new byte[] { 255, 0, 0, expected[i] }, Pixel(rgba, i));
    }

    [Fact]
    public void Decode_Bc4_MapsChannelToRed()
    {
        byte[] indices = [0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA];   // indices 0,1,…,7,0,1,…,7
        byte[] values = [255, 0, 218, 182, 145, 109, 72, 36];
        var block = new byte[8];
        block[0] = 255;
        block[1] = 0;
        indices.CopyTo(block, 2);

        var rgba = BcnDecoder.DecodeToRgba(block, DdsFormat.Bc4, 4, 4);

        // Single channel in red (BCnEncoder.Net/texconv convention); nothing invented elsewhere.
        for (int i = 0; i < 16; i++)
            Assert.Equal(new byte[] { values[i % 8], 0, 0, 255 }, Pixel(rgba, i));
    }

    [Fact]
    public void Decode_Bc5_MapsChannelsToRedGreen()
    {
        // BC5 = two BC4-style blocks: red channel then green channel.
        var indices = new byte[] { 0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA };
        byte[] values = [255, 0, 218, 182, 145, 109, 72, 36];
        var block = new byte[16];
        block[0] = 255;                                           // R block: a0=255, a1=0 → 8-value mode
        block[1] = 0;
        indices.CopyTo(block, 2);
        block[8] = 0;                                             // G block: a0=a1=0 → 6-value mode …
        block[9] = 0;
        indices.CopyTo(block, 10);

        var rgba = BcnDecoder.DecodeToRgba(block, DdsFormat.Bc5, 4, 4);

        for (int i = 0; i < 16; i++)
        {
            // … where palette[6] is 0 and palette[7] is 255, so only indices 7/15 pick up green.
            byte expectedGreen = i % 8 == 7 ? (byte)255 : (byte)0;
            Assert.Equal(new byte[] { values[i % 8], expectedGreen, 0, 255 }, Pixel(rgba, i));
        }
    }

    [Fact]
    public void Decode_MaskedRgba8_PreservesMaskByteOrder()
    {
        // Rgba8: bytes are R,G,B,A. Bgra8: bytes are B,G,R,A.
        var rgba = BcnDecoder.DecodeToRgba(new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 }, DdsFormat.Rgba8, 1, 2);
        Assert.Equal(new byte[] { 1, 2, 3, 4 }, Pixel(rgba, 0));
        Assert.Equal(new byte[] { 5, 6, 7, 8 }, Pixel(rgba, 1));

        var bgra = BcnDecoder.DecodeToRgba(new byte[] { 3, 2, 1, 4, 7, 6, 5, 8 }, DdsFormat.Bgra8, 1, 2);
        Assert.Equal(new byte[] { 1, 2, 3, 4 }, Pixel(bgra, 0));
        Assert.Equal(new byte[] { 5, 6, 7, 8 }, Pixel(bgra, 1));
    }

    [Fact]
    public void ParseDdsHeader_LegacyBc4U_ResolvesBc4()
    {
        // Sharp edge found in reconnaissance: the library's own DDS table maps "ATI1" but not
        // "BC4U" (the fourCC its CTXR writer emits for texture format 0xE).
        var dds = MakeDds(width: 16, height: 8, pfFlags: DdsPfFourCc, fourCc: "BC4U");

        var image = CtxrConverter.ParseDdsHeader(dds);

        Assert.Equal(DdsFormat.Bc4, image.Format);
        Assert.Equal(16, image.Width);
        Assert.Equal(8, image.Height);
        Assert.Equal(0x80, image.DataOffset);
    }

    [Fact]
    public void ParseDdsHeader_Dx10Bc6H_ThrowsNamingFormat()
    {
        // The pinned CC0A samples are all BC6H (HDR) — out of the M0 decode scope, and must be
        // named, not decoded on a guess.
        var dds = MakeDds(width: 8, height: 8, pfFlags: DdsPfFourCc, fourCc: "DX10", dxgiFormat: 95);

        var ex = Assert.ThrowsAny<Exception>(() => CtxrConverter.ParseDdsHeader(dds));

        Assert.Contains("BC6H", ex.Message);
    }

    private const uint DdsPfFourCc = 0x4;

    // --- helpers ----------------------------------------------------------------------------

    private static void AssertMagic(string file, string expectedHex)
    {
        using var stream = File.OpenRead(file);
        Span<byte> head = stackalloc byte[2];
        stream.ReadExactly(head);
        Assert.Equal(expectedHex, Convert.ToHexString(head).ToLowerInvariant());
    }

    private static byte[] Bc1Block(ushort c0, ushort c1, params byte[] indices)
    {
        var block = new byte[8];
        WriteBc1Color(block, c0, c1, indices);
        return block;
    }

    private static void WriteBc1Color(Span<byte> destination, ushort c0, ushort c1, params byte[] indices)
    {
        destination[0] = (byte)(c0 & 0xFF);
        destination[1] = (byte)(c0 >> 8);
        destination[2] = (byte)(c1 & 0xFF);
        destination[3] = (byte)(c1 >> 8);
        var count = Math.Min(4, indices.Length);
        for (int i = 0; i < count; i++)
            destination[4 + i] = indices[i];
    }

    private static byte[] Pixel(byte[] rgba, int index) =>
        rgba[(index * 4)..(index * 4 + 4)];

    private static void AssertAll(byte[] rgba, byte r, byte g, byte b, byte a)
    {
        var expected = new byte[] { r, g, b, a };
        for (int i = 0; i < rgba.Length / 4; i++)
            Assert.Equal(expected, Pixel(rgba, i));
    }

    private static HashSet<Rgba32> DistinctPixels(Image<Rgba32> image)
    {
        var seen = new HashSet<Rgba32>();
        for (int y = 0; y < image.Height; y++)
            for (int x = 0; x < image.Width; x++)
                seen.Add(image[x, y]);
        return seen;
    }

    private static byte[] MakeDds(int width, int height, uint pfFlags, string fourCc, uint dxgiFormat = 0)
    {
        var isDx10 = fourCc == "DX10";
        var dds = new byte[isDx10 ? 0x94 : 0x80];
        dds[0] = (byte)'D'; dds[1] = (byte)'D'; dds[2] = (byte)'S'; dds[3] = (byte)' ';
        BitConverter.TryWriteBytes(dds.AsSpan(0x04), 124u);
        BitConverter.TryWriteBytes(dds.AsSpan(0x0C), (uint)height);
        BitConverter.TryWriteBytes(dds.AsSpan(0x10), (uint)width);
        BitConverter.TryWriteBytes(dds.AsSpan(0x4C), 32u);
        BitConverter.TryWriteBytes(dds.AsSpan(0x50), pfFlags);
        System.Text.Encoding.ASCII.GetBytes(fourCc).CopyTo(dds, 0x54);
        if (isDx10)
            BitConverter.TryWriteBytes(dds.AsSpan(0x80), dxgiFormat);
        return dds;
    }
}
