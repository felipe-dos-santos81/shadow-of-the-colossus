using System.Collections.Concurrent;
using BpConvert.Core.Aqua;
using BpConvert.Core.Gltf;
using BpConvert.Core.Textures;

namespace BpConvert.Core.Pipeline;

/// <summary>
/// One game model → one <c>.glb</c> plus its textures as <c>.png</c>, both mirrored under the
/// output library at the source's tree-relative path (pinned Task 4 mapping:
/// <c>models/…/wander.cmsh</c> → <c>&lt;outLib&gt;/models/…/wander.glb</c>; referenced
/// <c>textures/…/x.ctxr</c> → <c>&lt;outLib&gt;/textures/…/x.png</c>).
/// </summary>
/// <remarks>
/// <para>
/// The heavy lifting lives in <see cref="AquaModelAdapter"/> (library types) and
/// <see cref="CtxrConverter"/> (texture decode); this type owns path normalisation, the
/// <c>textureUriMap</c> (source .ctxr → uri relative to the .glb's directory) and the skip policy:
/// a texture that refuses (BC6H/HDR, negative dimensions, …) is skipped — the model still writes
/// its .glb and the channel is omitted — while a missing or unreadable source model throws.
/// </para>
/// <para>
/// Resolvers are cached per game root: indexing the tree costs ≈0.34 s and batch conversion calls
/// this once per file (one adapter instance per call; no shared mutable state).
/// </para>
/// </remarks>
public static class ModelConverter
{
    private static readonly ConcurrentDictionary<string, AssetResolver> Resolvers = new(StringComparer.Ordinal);

    /// <summary>
    /// Converts the model at <paramref name="sourceRelPath"/> (tree-relative to
    /// <paramref name="gameRoot"/>) into <paramref name="outLibDir"/>. Throws
    /// <see cref="FileNotFoundException"/> when the source does not exist, and
    /// <see cref="NotSupportedException"/> for a source that is not a <c>.cmsh</c> (v1).
    /// </summary>
    /// <param name="gameRoot">Game tree root (the directory holding <c>eboot.bin</c>'s assets).</param>
    /// <param name="sourceRelPath">Model path relative to <paramref name="gameRoot"/>, either separator style.</param>
    /// <param name="outLibDir">Output library root; the source's tree path is mirrored below it.</param>
    /// <param name="note">
    /// Optional diagnostic sink (additive to the pinned three-argument call shape): unresolved
    /// <c>$</c>/<c>****</c> references, default-material fallbacks, skipped sub-meshes and refused
    /// textures are reported here — each is a degradation, not a failure.
    /// </param>
    public static ConvertResult Convert(string gameRoot, string sourceRelPath, string outLibDir,
        Action<string>? note = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(gameRoot);
        ArgumentException.ThrowIfNullOrWhiteSpace(sourceRelPath);
        ArgumentException.ThrowIfNullOrWhiteSpace(outLibDir);

        // Same normalisation the resolver applies to its root, so both agree on the tree.
        var root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(gameRoot));
        var resolver = Resolvers.GetOrAdd(root, static path => new AssetResolver(path));

        var sourceSpelling = sourceRelPath.Replace('\\', '/');
        var sourceAbsolute = resolver.Resolve(sourceSpelling, sourceSpelling)
            ?? throw new FileNotFoundException(
                $"model not found in the game tree: '{sourceSpelling}'", Path.Combine(root, sourceSpelling));

        var model = new AquaModelAdapter(resolver).Load(sourceAbsolute, note);

        var sourceRelative = Relative(root, sourceAbsolute);
        var glbPath = Path.Combine(outLibDir, ToHost(Path.ChangeExtension(sourceRelative, ".glb")));
        var glbDirectory = Path.GetDirectoryName(glbPath) ?? outLibDir;

        var pngPaths = new List<string>();
        var textureUriMap = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var textureAbsolute in model.ReferencedTexturePaths)
        {
            var textureRelative = Relative(root, textureAbsolute);
            var pngPath = Path.Combine(outLibDir, ToHost(Path.ChangeExtension(textureRelative, ".png")));
            try
            {
                CtxrConverter.Convert(textureAbsolute, pngPath);
            }
            catch (Exception e) when (e is InvalidDataException or NotSupportedException)
            {
                // A refused texture is a skip, not a fatal (see remarks).
                note?.Invoke($"texture skipped: {textureRelative} ({e.Message.Split('\n')[0]})");
                continue;
            }
            if (!File.Exists(pngPath)) continue;

            textureUriMap[textureRelative] = Relative(glbDirectory, pngPath);
            pngPaths.Add(pngPath);
        }

        // The adapter reports resolved absolute .ctxr paths; the writer's contracts speak
        // game-root-relative source paths, so normalise once, here (see the adapter's remarks).
        var output = model with
        {
            SourceRelativePath = sourceRelative,
            Materials = model.Materials.Select(material => material with
            {
                BaseColorTexture = RelativeSlot(root, material.BaseColorTexture),
                NormalTexture = RelativeSlot(root, material.NormalTexture),
                SpecularTexture = RelativeSlot(root, material.SpecularTexture),
            }).ToList(),
            ReferencedTexturePaths = model.ReferencedTexturePaths.Select(path => Relative(root, path)).ToList(),
        };

        var written = GlbWriter.Write(output, glbDirectory, Path.GetFileName(glbPath), textureUriMap);

        return new ConvertResult(written.GlbPath, pngPaths, output.Meshes.Count,
            output.Meshes.Sum(mesh => mesh.Positions.Length / 3));
    }

    /// <summary>Tree-relative, '/'-separated path of an absolute path below <paramref name="root"/>.</summary>
    private static string Relative(string root, string absolute) => Path.GetRelativePath(root, absolute).Replace('\\', '/');

    private static string? RelativeSlot(string root, string? absolute) => absolute is null ? null : Relative(root, absolute);

    private static string ToHost(string slashPath) => slashPath.Replace('/', Path.DirectorySeparatorChar);
}

/// <summary>Result of one model conversion (pinned Task 4 API).</summary>
/// <param name="GlbPath">Written .glb: the pinned mirror of the source path under the output library.</param>
/// <param name="PngPaths">Converted textures, deduped, as written (a refused texture is absent).</param>
/// <param name="MeshCount">Mesh primitives written (one per non-degenerate CMSH sub-mesh).</param>
/// <param name="TotalVertices">Sum of vertex counts over those meshes.</param>
public sealed record ConvertResult(string GlbPath, List<string> PngPaths, int MeshCount, long TotalVertices);
