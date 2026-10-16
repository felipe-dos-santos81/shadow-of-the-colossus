namespace BpConvert.Core.Textures;

/// <summary>
/// Pixel formats this converter can decode into RGBA8 (the M0 subset: everything the SOTC tree
/// uses apart from the BC6H/BC7 HDR variants). <c>Rgbx8</c>/<c>Bgrx8</c> are the legacy 32-bit
/// masked layouts without an alpha channel: the unused byte is ignored and alpha is forced to 255.
/// </summary>
internal enum DdsFormat
{
    Rgba8,
    Bgra8,
    Rgbx8,
    Bgrx8,
    Bc1,
    Bc2,
    Bc3,
    Bc4,
    Bc5,
}

/// <summary>
/// Hand-written decoders for the DDS pixel formats in <see cref="DdsFormat"/> — only the top mip
/// (level 0) is decoded. The block decoders follow the Direct3D specs (BC1 punch-through alpha,
/// BC2 4-bit alpha, BC3/BC4/BC5 interpolated alpha), so no external decoder is needed:
/// <list type="bullet">
/// <item>BC1 → RGBA; two 2-bit colour modes (index 3 is transparent black in the 3-colour mode).</item>
/// <item>BC2 → the colour block (always 4-colour) plus explicit 4-bit alpha (nibble × 17).</item>
/// <item>BC3 → the colour block plus a BC4-style interpolated alpha block.</item>
/// <item>BC4 → the single channel in red, blue/green 0, alpha 255 (same convention as
/// BCnEncoder.Net and texconv; no channel invented).</item>
/// <item>BC5 → red = channel 0, green = channel 1, blue 0, alpha 255 (normal-map X/Y).</item>
/// </list>
/// Interpolated palette entries use integer division (truncation), matching the Direct3D/Vulkan
/// reference formulas; decoders that round (BCnEncoder.Net) can differ by ±1 on such entries.
/// </summary>
internal static class BcnDecoder
{
    /// <summary>Bytes of one 4×4 block for block-compressed formats (4 bytes per pixel for the rest).</summary>
    private static int BytesPerBlock(DdsFormat format) => format switch
    {
        DdsFormat.Bc1 or DdsFormat.Bc4 => 8,
        DdsFormat.Bc2 or DdsFormat.Bc3 or DdsFormat.Bc5 => 16,
        _ => 4,
    };

    /// <summary>Bytes the top mip of a <paramref name="width"/>×<paramref name="height"/> image needs.</summary>
    internal static int Mip0ByteCount(DdsFormat format, int width, int height) =>
        format is DdsFormat.Rgba8 or DdsFormat.Bgra8 or DdsFormat.Rgbx8 or DdsFormat.Bgrx8
            ? width * height * 4
            : Blocks(width) * Blocks(height) * BytesPerBlock(format);

    private static int Blocks(int size) => Math.Max(1, (size + 3) / 4);

    /// <summary>
    /// Decodes the top mip at the start of <paramref name="payload"/> into a tightly packed,
    /// top-down RGBA8 buffer (<c>width * height * 4</c> bytes).
    /// </summary>
    internal static byte[] DecodeToRgba(ReadOnlySpan<byte> payload, DdsFormat format, int width, int height)
    {
        if (width <= 0 || height <= 0)
            throw new InvalidDataException($"invalid image size {width}x{height}");
        var needed = Mip0ByteCount(format, width, height);
        if (payload.Length < needed)
            throw new InvalidDataException(
                $"payload holds {payload.Length} byte(s) but the top mip of {format} {width}x{height} needs {needed}");

        var rgba = new byte[width * height * 4];
        if (format is DdsFormat.Rgba8 or DdsFormat.Bgra8 or DdsFormat.Rgbx8 or DdsFormat.Bgrx8)
        {
            DecodeUncompressed(payload, format, rgba);
            return rgba;
        }

        var bytesPerBlock = BytesPerBlock(format);
        Span<byte> blockRgba = stackalloc byte[64];
        var blocksWide = Blocks(width);
        for (int blockY = 0; blockY * 4 < height; blockY++)
        {
            for (int blockX = 0; blockX * 4 < width; blockX++)
            {
                blockRgba.Clear();
                DecodeBlock(format, payload.Slice((blockY * blocksWide + blockX) * bytesPerBlock, bytesPerBlock), blockRgba);
                CopyBlock(rgba, width, height, blockX * 4, blockY * 4, blockRgba);
            }
        }
        return rgba;
    }

    private static void DecodeUncompressed(ReadOnlySpan<byte> payload, DdsFormat format, Span<byte> rgba)
    {
        var bgra = format is DdsFormat.Bgra8 or DdsFormat.Bgrx8;
        var opaque = format is DdsFormat.Rgbx8 or DdsFormat.Bgrx8;
        for (int pixel = 0; pixel < rgba.Length / 4; pixel++)
        {
            var source = payload.Slice(pixel * 4, 4);
            rgba[pixel * 4 + 0] = bgra ? source[2] : source[0];
            rgba[pixel * 4 + 1] = source[1];
            rgba[pixel * 4 + 2] = bgra ? source[0] : source[2];
            rgba[pixel * 4 + 3] = opaque ? (byte)255 : source[3];
        }
    }

    private static void DecodeBlock(DdsFormat format, ReadOnlySpan<byte> block, Span<byte> blockRgba)
    {
        Span<byte> alpha = stackalloc byte[16];
        switch (format)
        {
            case DdsFormat.Bc1:
                DecodeColorBlock(block, blockRgba, punchThrough: true);
                break;
            case DdsFormat.Bc2:
                DecodeNibbleAlpha(block[..8], alpha);
                DecodeColorBlock(block[8..], blockRgba, punchThrough: false);
                CopyAlpha(alpha, blockRgba);
                break;
            case DdsFormat.Bc3:
                DecodeAlphaBlock(block[..8], alpha);
                DecodeColorBlock(block[8..], blockRgba, punchThrough: false);
                CopyAlpha(alpha, blockRgba);
                break;
            case DdsFormat.Bc4:
                DecodeAlphaBlock(block, alpha);
                for (int pixel = 0; pixel < 16; pixel++)
                {
                    blockRgba[pixel * 4 + 0] = alpha[pixel];
                    blockRgba[pixel * 4 + 3] = 255;
                }
                break;
            case DdsFormat.Bc5:
                DecodeAlphaBlock(block[..8], alpha);
                for (int pixel = 0; pixel < 16; pixel++)
                {
                    blockRgba[pixel * 4 + 0] = alpha[pixel];
                    blockRgba[pixel * 4 + 3] = 255;
                }
                DecodeAlphaBlock(block[8..], alpha);
                for (int pixel = 0; pixel < 16; pixel++)
                    blockRgba[pixel * 4 + 1] = alpha[pixel];
                break;
            default:
                throw new ArgumentOutOfRangeException(nameof(format), format, "not a block-compressed format");
        }
    }

    /// <summary>BC1-style colour block: two RGB565 endpoints, then 16 2-bit indices (LSB first).</summary>
    private static void DecodeColorBlock(ReadOnlySpan<byte> block, Span<byte> blockRgba, bool punchThrough)
    {
        ushort color0 = (ushort)(block[0] | (block[1] << 8));
        ushort color1 = (ushort)(block[2] | (block[3] << 8));
        Span<byte> palette = stackalloc byte[16];
        Expand565(color0, palette[..4]);
        Expand565(color1, palette[4..8]);
        if (!punchThrough || color0 > color1)
        {
            // 4-colour mode: two thirds of each endpoint.
            for (int channel = 0; channel < 3; channel++)
            {
                palette[8 + channel] = (byte)((2 * palette[channel] + palette[4 + channel]) / 3);
                palette[12 + channel] = (byte)((palette[channel] + 2 * palette[4 + channel]) / 3);
            }
            palette[11] = 255;
            palette[15] = 255;
        }
        else
        {
            // 3-colour mode: the midpoint, and index 3 is transparent black. Entry 3 is written
            // explicitly: stackalloc content is undefined without localsinit, and the copy loop
            // below would otherwise publish whatever happened to be on the stack.
            for (int channel = 0; channel < 3; channel++)
                palette[8 + channel] = (byte)((palette[channel] + palette[4 + channel]) / 2);
            palette[11] = 255;
            palette[12] = palette[13] = palette[14] = palette[15] = 0;
        }

        for (int pixel = 0; pixel < 16; pixel++)
        {
            var index = (block[4 + pixel / 4] >> ((pixel % 4) * 2)) & 0x3;
            palette.Slice(index * 4, 4).CopyTo(blockRgba.Slice(pixel * 4, 4));
        }
    }

    /// <summary>BC2 alpha: 16 explicit 4-bit values (low nibble = even pixel), scaled to 0–255.</summary>
    private static void DecodeNibbleAlpha(ReadOnlySpan<byte> block, Span<byte> alpha)
    {
        for (int pixel = 0; pixel < 16; pixel++)
        {
            var nibble = (block[pixel / 2] >> ((pixel % 2) * 4)) & 0xF;
            alpha[pixel] = (byte)(nibble * 17);
        }
    }

    /// <summary>BC3/BC4/BC5 alpha block: two endpoints and 16 3-bit indices (LSB first, 6 bytes).</summary>
    private static void DecodeAlphaBlock(ReadOnlySpan<byte> block, Span<byte> alpha)
    {
        byte alpha0 = block[0];
        byte alpha1 = block[1];
        Span<byte> palette = stackalloc byte[8];
        palette[0] = alpha0;
        palette[1] = alpha1;
        if (alpha0 > alpha1)
        {
            // 8-value mode: 6 interpolated steps between the endpoints.
            for (int i = 1; i <= 6; i++)
                palette[i + 1] = (byte)(((7 - i) * alpha0 + i * alpha1) / 7);
        }
        else
        {
            // 6-value mode: 4 interpolated steps, then 0 and 255.
            for (int i = 1; i <= 4; i++)
                palette[i + 1] = (byte)(((5 - i) * alpha0 + i * alpha1) / 5);
            palette[6] = 0;
            palette[7] = 255;
        }

        ulong indices = 0;
        for (int i = 0; i < 6; i++)
            indices |= (ulong)block[2 + i] << (8 * i);
        for (int pixel = 0; pixel < 16; pixel++)
            alpha[pixel] = palette[(int)((indices >> (pixel * 3)) & 0x7)];
    }

    private static void CopyAlpha(ReadOnlySpan<byte> alpha, Span<byte> blockRgba)
    {
        for (int pixel = 0; pixel < 16; pixel++)
            blockRgba[pixel * 4 + 3] = alpha[pixel];
    }

    private static void Expand565(ushort value, Span<byte> rgba)
    {
        byte r = (byte)((value >> 11) & 0x1F);
        byte g = (byte)((value >> 5) & 0x3F);
        byte b = (byte)(value & 0x1F);
        rgba[0] = (byte)((r << 3) | (r >> 2));
        rgba[1] = (byte)((g << 2) | (g >> 4));
        rgba[2] = (byte)((b << 3) | (b >> 2));
        rgba[3] = 255;
    }

    /// <summary>Copies a decoded 4×4 block into the image, clipping the right/bottom edges.</summary>
    private static void CopyBlock(Span<byte> rgba, int width, int height, int originX, int originY, ReadOnlySpan<byte> blockRgba)
    {
        for (int y = 0; y < 4 && originY + y < height; y++)
        {
            for (int x = 0; x < 4 && originX + x < width; x++)
            {
                blockRgba.Slice((y * 4 + x) * 4, 4).CopyTo(rgba.Slice(((originY + y) * width + originX + x) * 4, 4));
            }
        }
    }
}
