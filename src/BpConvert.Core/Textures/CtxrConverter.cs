using System.IO.Compression;
using System.Text;
using AquaModelLibrary.Data.BluePoint;
using AquaModelLibrary.Data.BluePoint.CTXR;
using AquaModelLibrary.Helpers;

namespace BpConvert.Core.Textures;

/// <summary>Result of converting one BluePoint texture container to PNG.</summary>
/// <param name="Width">Pixel width of the written image.</param>
/// <param name="Height">Pixel height of the written image.</param>
/// <param name="Variant">
/// Container variant as found on disk: <c>"png"</c> (the file is a raw PNG payload),
/// <c>"8c0a"</c> / <c>"cc0a"</c> (Oodle-compressed container) or <c>"other"</c> (any other
/// container magic, e.g. the uncompressed <c>0000</c>/<c>0b00</c> and <c>8c06</c> forms).
/// </param>
public sealed record CtxrResult(int Width, int Height, string Variant);

/// <summary>
/// CTXR (<c>.ctxr</c>) → PNG converter.
/// </summary>
/// <remarks>
/// <para>
/// Routing is by container header: a leading PNG signature is written out verbatim (the game ships
/// some textures as plain PNGs); <c>8C 0A</c> / <c>CC 0A</c> (and everything else) is parsed by
/// <see cref="CTXR"/>, which decompresses through the vendored Oodle dylib and de-swizzles the
/// PS4/PS5 texture buffers down to DDS bytes. Those bytes are then decoded by the hand-written
/// decoders in <see cref="BcnDecoder"/> and re-encoded as an RGBA PNG
/// (<see cref="ZLibStream"/>, filter 0 per scanline).
/// </para>
/// <para>
/// Only the top mip is written. Cube maps and volume textures yield their first slice/face.
/// Containers whose mip chain starts in external <c>.ctxc</c> chunk files are supported by
/// assembling <b>mip 0 only</b> from the level-0 chunk (see <see cref="AssembleExternalMip0"/>);
/// lower levels are skipped because consumers regenerate them. Unsupported pixel formats
/// (BC6H/BC7/SNORM/float) are reported by name; no format is ever decoded on a guess.
/// </para>
/// <para>
/// Failures throw <see cref="InvalidDataException"/> whose message starts with the input path.
/// </para>
/// </remarks>
public static class CtxrConverter
{
    /// <summary>Container variants as pinned by the Task 3 brief.</summary>
    private const string VariantPng = "png";
    private const string Variant8C0A = "8c0a";
    private const string VariantCC0A = "cc0a";
    private const string VariantOther = "other";

    /// <summary>Smallest byte count that could plausibly hold a CTXR header plus footer.</summary>
    private const int MinContainerBytes = 0x20;

    private const uint DdpfFourCc = 0x4;
    private const uint DdpfRgb = 0x40;

    private static readonly byte[] PngSignature = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

    /// <summary>
    /// Converts <paramref name="ctxrAbsPath"/> and writes a PNG next to the requested
    /// <paramref name="outPngAbsPath"/> (missing parent directories are created).
    /// </summary>
    public static CtxrResult Convert(string ctxrAbsPath, string outPngAbsPath)
    {
        ArgumentException.ThrowIfNullOrEmpty(ctxrAbsPath);
        ArgumentException.ThrowIfNullOrEmpty(outPngAbsPath);

        var bytes = File.ReadAllBytes(ctxrAbsPath);

        if (bytes.AsSpan().StartsWith(PngSignature))
        {
            // The container is a raw PNG payload (textureFormat == -1): strip it verbatim.
            var (width, height) = ReadPngSize(bytes, ctxrAbsPath);
            WriteAllBytes(outPngAbsPath, bytes);
            return new CtxrResult(width, height, VariantPng);
        }

        if (bytes.Length < MinContainerBytes)
            throw Error(ctxrAbsPath, $"file is {bytes.Length} byte(s) — too small to be a CTXR container");

        var variant = ContainerVariant(bytes);
        CTXR ctxr;
        try
        {
            ctxr = new CTXR(bytes);
        }
        catch (Exception e)
        {
            throw Error(ctxrAbsPath, $"{variant} container: not decodable ({Describe(e)})", e);
        }

        if (ctxr.isPng)
        {
            // A compressed container whose decompressed payload is itself a PNG. The variant
            // still describes the container that is on disk.
            var png = ctxr.mipMapsList[0][0];
            var (width, height) = ReadPngSize(png, ctxrAbsPath);
            WriteAllBytes(outPngAbsPath, png);
            return new CtxrResult(width, height, variant);
        }

        byte[] dds;
        if (ctxr.externalMipCount > 0)
        {
            try
            {
                var mip0 = AssembleExternalMip0(ctxr, ctxrAbsPath);
                dds = ctxr.GenerateDDSHeader(ctxr.GetFormat(), ctxr.GetWidth(), ctxr.GetHeight(), mipCount: 1)
                    .Concat(mip0)
                    .ToArray();
            }
            catch (Exception e)
            {
                throw Error(ctxrAbsPath, $"{variant} container: external mip 0: {e.Message}", e);
            }
        }
        else
        {
            try
            {
                var ddsBlobs = ctxr.GetDDSBytes(ctxrAbsPath);
                dds = ddsBlobs.Count > 0
                    ? ddsBlobs[0]
                    : throw new InvalidDataException("container holds no texture data");
            }
            catch (Exception e)
            {
                throw Error(ctxrAbsPath, $"{variant} container: CTXR → DDS assembly failed ({Describe(e)})", e);
            }
        }

        DdsImage image;
        byte[] pngBytes;
        try
        {
            image = ParseDdsHeader(dds);
            var rgba = BcnDecoder.DecodeToRgba(dds.AsSpan(image.DataOffset), image.Format, image.Width, image.Height);
            pngBytes = PngWriter.Encode(rgba, image.Width, image.Height);
        }
        catch (Exception e)
        {
            throw Error(ctxrAbsPath, $"{variant} container: {e.Message}", e);
        }

        WriteAllBytes(outPngAbsPath, pngBytes);
        return new CtxrResult(image.Width, image.Height, variant);
    }

    /// <summary>Container variant from the first two bytes (see <see cref="CtxrResult.Variant"/>).</summary>
    private static string ContainerVariant(ReadOnlySpan<byte> bytes) => (bytes[0], bytes[1]) switch
    {
        (0x8C, 0x0A) => Variant8C0A,
        (0xCC, 0x0A) => VariantCC0A,
        _ => VariantOther,
    };

    /// <summary>DDS header fields needed to decode the top mip.</summary>
    internal readonly record struct DdsImage(DdsFormat Format, int Width, int Height, int DataOffset);

    /// <summary>
    /// Assembles mip 0 of a container whose mip chain lives in external <c>.ctxc</c> chunk files:
    /// resolves the level-0 reference, reads/decompresses the chunk and de-swizzles it to raw
    /// mip-0 block bytes. Mirrors what <c>CTXR.GetDDSBytes</c> intends (lower levels are skipped —
    /// Godot/glTF consumers regenerate them), but resolves the paths the way the game stores them
    /// (<c>/</c> separators) instead of the library's Windows-only <c>\</c> substitution.
    /// </summary>
    internal static byte[] AssembleExternalMip0(CTXR ctxr, string ctxrAbsPath)
    {
        if (ctxr.textureType is not (CTextureType.Standard or CTextureType.LargeUI))
            throw new InvalidDataException(
                $"external mip 0 assembly supports Standard/LargeUI textures, not {ctxr.textureType}");

        var reference = ctxr.mipPaths.FirstOrDefault(p => p.mipLevel == 0)
            ?? throw new InvalidDataException(
                $"no external reference for mip level 0 (container has {ctxr.externalMipCount} external level(s))");

        var chunkFile = ResolveChunkFile(ctxrAbsPath, reference.externalMipReference)
            ?? throw new InvalidDataException(
                $"chunk file for mip level 0 not found: '{reference.externalMipReference}' " +
                "(tried the reference below every ancestor directory, with **** → _cmn/_ps4/_ps5)");

        var pixelFormat = ctxr.GetFormat();
        var width = ctxr.GetWidth();
        var height = ctxr.GetHeight();

        // `CheckCompression` decompresses the chunk (they carry their own Oodle marker) and keeps
        // its 12-byte CTXT trailer; the de-swizzle size is computed on the payload only — exactly
        // as CTXR.ReadAndDeSwizzleCTexChunk does it.
        var chunk = CompressionHandler.CheckCompression(File.ReadAllBytes(chunkFile), out _);
        ctxr.GetDeSwizzleSize(chunk.Length - 0xC, pixelFormat, width, height, out var swizzleWidth, out var swizzleHeight);
        var deSwizzled = ctxr.footerData.version switch
        {
            0x25 => DeSwizzler.PS4DeSwizzle(chunk, swizzleWidth, swizzleHeight, pixelFormat),   // SOTC
            0x6E => DeSwizzler.PS5DeSwizzle(chunk, swizzleWidth, swizzleHeight, pixelFormat),   // DeSR
            var version => throw new InvalidDataException(
                $"unsupported container version 0x{version:X} for external mip chunks"),
        };
        return DeSwizzler.ExtractTile(deSwizzled, pixelFormat, swizzleWidth, 0, 0, width, height);
    }

    /// <summary>
    /// Resolves a <c>$</c>-rooted chunk reference (for example
    /// <c>$/textures/ocean/flowmaps/****/arena_02_flow_nml.chunk0.ctxc</c>) to a real file.
    /// Root derivation (no environment variables, no globals): the reference is game-root relative,
    /// so every ancestor of the container's own directory is a candidate root; the first candidate
    /// that exists wins. <c>****</c> (the platform wildcard) is tried as <c>_cmn</c>, then
    /// <c>_ps4</c>, then <c>_ps5</c>, matching the game's shared-first ordering. Bare references
    /// (no <c>$/</c>) resolve against the container's directory first.
    /// </summary>
    private static string? ResolveChunkFile(string ctxrAbsPath, string externalMipReference)
    {
        var reference = externalMipReference.StartsWith("$/", StringComparison.Ordinal)
            ? externalMipReference[2..]
            : externalMipReference;
        reference = reference.Replace('\\', '/');
        var platforms = reference.Contains("****") ? new[] { "_cmn", "_ps4", "_ps5" } : new[] { string.Empty };

        var directory = Path.GetDirectoryName(Path.GetFullPath(ctxrAbsPath));
        for (var depth = 0; directory != null && depth < 64; depth++)
        {
            foreach (var platform in platforms)
            {
                var candidate = Path.Combine(
                    directory, reference.Replace("****", platform).Replace('/', Path.DirectorySeparatorChar));
                if (File.Exists(candidate))
                    return candidate;
            }
            directory = Path.GetDirectoryName(directory);
        }
        return null;
    }

    /// <summary>
    /// Parses a DDS header (plus the DX10 extension header when present). This deliberately does
    /// not use the library's <c>DDS</c> class: that one resolves fourCCs with a lookup table that
    /// silently maps unknown formats (including the <c>BC4U</c> it emits itself) to RGBA8.
    /// </summary>
    /// <exception cref="InvalidDataException">The blob is not a well-formed DDS.</exception>
    /// <exception cref="NotSupportedException">The declared pixel format is not one we decode.</exception>
    internal static DdsImage ParseDdsHeader(ReadOnlySpan<byte> dds)
    {
        if (dds.Length < 0x80)
            throw new InvalidDataException($"DDS blob truncated: {dds.Length} byte(s)");
        if (!(dds[0] == 'D' && dds[1] == 'D' && dds[2] == 'S' && dds[3] == ' '))
            throw new InvalidDataException("not a DDS blob ('DDS ' magic missing)");

        var height = (int)ReadUInt32(dds, 0x0C);
        var width = (int)ReadUInt32(dds, 0x10);
        var pixelFormatFlags = ReadUInt32(dds, 0x50);
        var fourCc = Encoding.ASCII.GetString(dds.Slice(0x54, 4));

        DdsFormat format;
        var dataOffset = 0x80;
        if (fourCc == "DX10")
        {
            if (dds.Length < 0x94)
                throw new InvalidDataException($"DDS blob truncated inside the DX10 header: {dds.Length} byte(s)");
            format = ResolveDxgiFormat(ReadUInt32(dds, 0x80));
            dataOffset = 0x94;
        }
        else if ((pixelFormatFlags & DdpfFourCc) != 0)
        {
            format = ResolveFourCc(fourCc);
        }
        else if ((pixelFormatFlags & DdpfRgb) != 0)
        {
            format = ResolveMaskedLayout(
                ReadUInt32(dds, 0x58),                                  // dwRGBBitCount
                ReadUInt32(dds, 0x5C), ReadUInt32(dds, 0x60),           // R/G masks
                ReadUInt32(dds, 0x64), ReadUInt32(dds, 0x68));          // B/A masks
        }
        else
        {
            throw new NotSupportedException(
                $"unsupported DDS pixel format (pfFlags 0x{pixelFormatFlags:X}, fourCC '{fourCc}')");
        }

        if (width <= 0 || height <= 0)
            throw new InvalidDataException($"DDS header declares {width}x{height}");

        return new DdsImage(format, width, height, dataOffset);
    }

    private static DdsFormat ResolveFourCc(string fourCc) => fourCc switch
    {
        "DXT1" => DdsFormat.Bc1,
        "DXT2" or "DXT3" => DdsFormat.Bc2,
        "DXT4" or "DXT5" => DdsFormat.Bc3,
        "ATI1" or "BC4U" => DdsFormat.Bc4,
        "ATI2" or "BC5U" => DdsFormat.Bc5,
        _ => throw new NotSupportedException($"unsupported DDS pixel format: fourCC '{fourCc}'"),
    };

    private static DdsFormat ResolveDxgiFormat(uint dxgiFormat) => dxgiFormat switch
    {
        28 or 29 => DdsFormat.Rgba8,        // R8G8B8A8_UNORM / _SRGB
        87 or 91 => DdsFormat.Bgra8,        // B8G8R8A8_UNORM / _SRGB
        70 or 71 or 72 => DdsFormat.Bc1,    // BC1 typeless/UNORM/_SRGB
        73 or 74 or 75 => DdsFormat.Bc2,
        76 or 77 or 78 => DdsFormat.Bc3,
        79 or 80 => DdsFormat.Bc4,          // BC4 typeless/UNORM (signed variants are rejected)
        82 or 83 => DdsFormat.Bc5,
        _ => throw new NotSupportedException(
            $"unsupported DDS DXGI format {dxgiFormat} ({DxgiFormatName(dxgiFormat)})"),
    };

    private static string DxgiFormatName(uint dxgiFormat) => dxgiFormat switch
    {
        10 => "R16G16B16A16_FLOAT",
        31 => "R8G8B8A8_SNORM",
        41 => "R32_FLOAT",
        81 => "BC4_SNORM",
        84 => "BC5_SNORM",
        95 => "BC6H_UF16",
        96 => "BC6H_SF16",
        98 => "BC7_UNORM",
        _ => "unknown",
    };

    /// <summary>Resolves the byte order of a 32-bit masked (uncompressed) DDS pixel format.</summary>
    private static DdsFormat ResolveMaskedLayout(uint bitCount, uint rMask, uint gMask, uint bMask, uint aMask)
    {
        if (bitCount != 32)
            throw new NotSupportedException($"unsupported DDS masked format: {bitCount} bits per pixel");
        var opaque = aMask == 0;
        if (opaque || aMask == 0xFF000000)
        {
            if (rMask == 0x000000FF && gMask == 0x0000FF00 && bMask == 0x00FF0000)
                return opaque ? DdsFormat.Rgbx8 : DdsFormat.Rgba8;
            if (rMask == 0x00FF0000 && gMask == 0x0000FF00 && bMask == 0x000000FF)
                return opaque ? DdsFormat.Bgrx8 : DdsFormat.Bgra8;
        }
        throw new NotSupportedException(
            $"unsupported DDS channel masks: R=0x{rMask:X8} G=0x{gMask:X8} B=0x{bMask:X8} A=0x{aMask:X8}");
    }

    private static uint ReadUInt32(ReadOnlySpan<byte> bytes, int offset) =>
        (uint)(bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24));

    /// <summary>Width/height of a PNG payload, from the IHDR chunk (big-endian).</summary>
    private static (int Width, int Height) ReadPngSize(byte[] png, string path)
    {
        if (png.Length < 24 || !png.AsSpan().StartsWith(PngSignature))
            throw Error(path, "PNG payload is truncated or does not start with the PNG signature");
        var width = (png[16] << 24) | (png[17] << 16) | (png[18] << 8) | png[19];
        var height = (png[20] << 24) | (png[21] << 16) | (png[22] << 8) | png[23];
        if (width <= 0 || height <= 0)
            throw Error(path, $"PNG payload declares {width}x{height}");
        return (width, height);
    }

    private static void WriteAllBytes(string outPath, byte[] bytes)
    {
        var directory = Path.GetDirectoryName(Path.GetFullPath(outPath));
        if (!string.IsNullOrEmpty(directory))
            Directory.CreateDirectory(directory);
        File.WriteAllBytes(outPath, bytes);
    }

    private static InvalidDataException Error(string path, string reason, Exception? inner = null) =>
        new($"{path}: {reason}", inner);

    private static string Describe(Exception e) => $"{e.GetType().Name}: {e.Message.Split('\n')[0]}";

    /// <summary>
    /// Minimal PNG encoder: 8-bit RGBA (colour type 6), filter 0 on every scanline, one IDAT.
    /// Uses the in-box <see cref="ZLibStream"/> for the zlib stream and a table-driven CRC-32.
    /// </summary>
    private static class PngWriter
    {
        private const int BytesPerPixel = 4;

        internal static byte[] Encode(ReadOnlySpan<byte> rgba, int width, int height)
        {
            if (width <= 0 || height <= 0)
                throw new InvalidDataException($"invalid image size {width}x{height}");
            if (rgba.Length != width * height * BytesPerPixel)
                throw new InvalidDataException(
                    $"RGBA buffer holds {rgba.Length} byte(s) but {width}x{height} needs {width * height * BytesPerPixel}");

            var stride = width * BytesPerPixel;
            var raw = new byte[(stride + 1) * height];
            for (int y = 0; y < height; y++)
            {
                raw[y * (stride + 1)] = 0;   // filter type 0 (None)
                rgba.Slice(y * stride, stride).CopyTo(raw.AsSpan(y * (stride + 1) + 1));
            }

            byte[] compressed;
            using (var buffer = new MemoryStream())
            {
                using (var zlib = new ZLibStream(buffer, CompressionLevel.Optimal, leaveOpen: true))
                    zlib.Write(raw);
                compressed = buffer.ToArray();
            }

            using var png = new MemoryStream(compressed.Length + 64);
            png.Write(PngSignature);
            Span<byte> ihdr = stackalloc byte[13];
            WriteBigEndian(ihdr, 0, width);
            WriteBigEndian(ihdr, 4, height);
            ihdr[8] = 8;    // bit depth
            ihdr[9] = 6;    // colour type: truecolour with alpha
            ihdr[10] = 0;   // compression: deflate
            ihdr[11] = 0;   // filter method
            ihdr[12] = 0;   // no interlace
            WriteChunk(png, "IHDR", ihdr);
            WriteChunk(png, "IDAT", compressed);
            WriteChunk(png, "IEND", ReadOnlySpan<byte>.Empty);
            return png.ToArray();
        }

        private static void WriteChunk(Stream stream, string type, ReadOnlySpan<byte> data)
        {
            Span<byte> header = stackalloc byte[4];
            WriteBigEndian(header, 0, data.Length);
            stream.Write(header);
            var typeBytes = Encoding.ASCII.GetBytes(type);
            stream.Write(typeBytes);
            stream.Write(data);
            var crc = Crc32(typeBytes, data);
            Span<byte> crcBytes = stackalloc byte[4];
            WriteBigEndian(crcBytes, 0, (int)crc);
            stream.Write(crcBytes);
        }

        private static void WriteBigEndian(Span<byte> destination, int offset, int value)
        {
            destination[offset] = (byte)(value >> 24);
            destination[offset + 1] = (byte)(value >> 16);
            destination[offset + 2] = (byte)(value >> 8);
            destination[offset + 3] = (byte)value;
        }

        private static readonly uint[] CrcLookup = BuildCrcLookup();

        private static uint[] BuildCrcLookup()
        {
            var table = new uint[256];
            for (uint i = 0; i < table.Length; i++)
            {
                var value = i;
                for (int bit = 0; bit < 8; bit++)
                    value = (value & 1) != 0 ? 0xEDB88320 ^ (value >> 1) : value >> 1;
                table[i] = value;
            }
            return table;
        }

        /// <summary>CRC-32 (PNG/IEEE) over the chunk type and data.</summary>
        private static uint Crc32(ReadOnlySpan<byte> type, ReadOnlySpan<byte> data)
        {
            var crc = 0xFFFFFFFF;
            foreach (var b in type)
                crc = CrcLookup[(crc ^ b) & 0xFF] ^ (crc >> 8);
            foreach (var b in data)
                crc = CrcLookup[(crc ^ b) & 0xFF] ^ (crc >> 8);
            return ~crc;
        }
    }
}
