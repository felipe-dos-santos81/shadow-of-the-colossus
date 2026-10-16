using System.Buffers.Binary;
using System.Numerics;
using AquaModelLibrary.Data.BluePoint.CMAT;
using AquaModelLibrary.Data.BluePoint.CMDL;
using AquaModelLibrary.Data.BluePoint.CMSH;
using BpConvert.Core.Model;
using BpConvert.Core.Pipeline;

namespace BpConvert.Core.Aqua;

/// <summary>
/// AquaModelLibrary.Data → BpConvert DTO adapter for BluePoint models (spec:
/// <c>docs/notes/aqua-core-api.md</c>; rules derived from <c>BluePointConvert.cs</c> are recorded in
/// its "adapter notes" section). This is the only Task 4 type that references library types.
/// </summary>
/// <remarks>
/// <para>
/// One <c>.cmsh</c> becomes one <see cref="ModelData"/>: one <see cref="MeshData"/> per
/// <c>header.meshList</c> sub-mesh, vertices compacted to the ones its faces use (the reference
/// implementation's per-material split), and one <see cref="MaterialData"/> per distinct
/// <c>matName</c>. The material chain is <c>cmsh matName → sibling .cmdl GetCMATMaterialMap() →
/// $/…/x.cmat → CMAT.texNames → $/…/x.ctxr</c>; without a sibling <c>.cmdl</c> the model degrades
/// to one <see cref="DefaultMaterialName"/> material with no textures.
/// </para>
/// <para>
/// Skeleton and skinning are Task 5: <see cref="MeshData.BoneIndices"/>/
/// <see cref="MeshData.BoneWeights"/> stay empty and <see cref="ModelData.Skeleton"/> stays null.
/// Texture slots and <see cref="ModelData.ReferencedTexturePaths"/> carry the <b>resolved absolute
/// .ctxr paths</b> (<c>AssetResolver</c> output); <c>ModelConverter</c> normalises them to
/// game-root-relative paths for the writer. <see cref="ModelData.SourceRelativePath"/> is the file
/// name for the same reason.
/// </para>
/// </remarks>
public sealed class AquaModelAdapter
{
    /// <summary>Material used when the texture chain cannot be established (no sibling .cmdl).</summary>
    internal const string DefaultMaterialName = "default";

    /// <summary>The platform wildcard order for <c>****</c>: shared assets first (Task 3 pinned it).</summary>
    private static readonly string[] PlatformWildcards = { "_cmn", "_ps4", "_ps5" };

    /// <summary>UV streams in TEXCOORD order; channel i is written as TEXCOORD_i.</summary>
    private static readonly VertexMagic[] UvMagics = { VertexMagic.TEX0, VertexMagic.TEX1, VertexMagic.TEX2, VertexMagic.TEX3 };

    private static readonly float[] White = { 1f, 1f, 1f, 1f };
    private static readonly float[] Black = { 0f, 0f, 0f };

    private readonly AssetResolver _resolver;

    public AquaModelAdapter(AssetResolver resolver)
    {
        ArgumentNullException.ThrowIfNull(resolver);
        _resolver = resolver;
    }

    /// <summary>
    /// Loads <paramref name="absolutePath"/> (a bare <c>.cmsh</c>; a <c>.cmdl</c> lists its meshes
    /// but is not a mesh itself) into the renderer-agnostic DTOs. Reference-mesh/LOD variants that
    /// carry no geometry (the library's early return) yield a model with no meshes.
    /// </summary>
    /// <param name="absolutePath">Absolute path of the <c>.cmsh</c> to load.</param>
    /// <param name="note">
    /// Optional diagnostic sink (additive to the pinned one-argument call shape): unresolved
    /// <c>$</c>/<c>****</c> references, the default-material fallback and skipped sub-meshes are
    /// reported here. Nothing is reported anywhere else — a batch run that wants a trail passes one.
    /// </param>
    public ModelData Load(string absolutePath, Action<string>? note = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(absolutePath);
        var fullPath = Path.GetFullPath(absolutePath);
        var extension = Path.GetExtension(fullPath).ToLowerInvariant();
        if (extension != ".cmsh")
        {
            throw new NotSupportedException(
                $"'{extension}' is not a supported source in v1 — convert the .cmsh (a .cmdl only lists its meshes)");
        }

        var cmsh = new CMSH(File.ReadAllBytes(fullPath));
        if (cmsh.header is null || cmsh.vertData is null || cmsh.faceData is null || cmsh.header.meshList.Count == 0)
        {
            // Reference-mesh variant: the library returns early with no geometry (CMSH.cs:28-37).
            return new ModelData(Path.GetFileName(fullPath), new(), new(), null, new());
        }

        var positions = cmsh.vertData.positionList;
        var normals = ReadNormals(cmsh.vertData, positions.Count);
        var uvChannels = ReadUvChannels(cmsh.vertData, positions.Count);
        var faces = cmsh.faceData.faceList;
        var chain = MaterialChain(fullPath, note);

        var meshes = new List<MeshData>();
        var materials = new List<MaterialData>();
        var materialIndexByName = new Dictionary<string, int>(StringComparer.Ordinal);
        var texturePaths = new List<string>();
        var knownTexturePaths = new HashSet<string>(StringComparer.Ordinal);

        var currentFace = 0;
        for (int m = 0; m < cmsh.header.meshList.Count; m++)
        {
            if (!TryGetFaceRange(cmsh.header, m, faces.Count, ref currentFace, out var startFace, out var faceCount))
            {
                note?.Invoke($"sub-mesh {m} skipped: empty face range");
                continue;
            }

            var remap = new Dictionary<int, int>();
            var localPositions = new List<float>();
            var localNormals = normals is null ? null : new List<float>();
            var localUvs = uvChannels.Select(_ => new List<float>()).ToArray();
            var indices = new List<uint>();
            var dropped = 0;

            for (int f = startFace; f < startFace + faceCount; f++)
            {
                var face = faces[f];
                // The writer has no index range check; drop faces that escape the vertex buffer.
                if (!InRange(face.X, positions.Count) || !InRange(face.Y, positions.Count) || !InRange(face.Z, positions.Count))
                {
                    dropped++;
                    continue;
                }

                indices.Add((uint)LocalIndex(face.X));
                indices.Add((uint)LocalIndex(face.Y));
                indices.Add((uint)LocalIndex(face.Z));
            }

            if (dropped > 0)
                note?.Invoke($"sub-mesh {m}: dropped {dropped} face(s) with out-of-range vertex indices");

            if (indices.Count == 0)
            {
                note?.Invoke($"sub-mesh {m} skipped: no drawable faces");
                continue;
            }

            int materialIndex = MaterialIndex(cmsh.header.meshList[m].matName, chain, materials, materialIndexByName,
                fullPath, texturePaths, knownTexturePaths, note);

            meshes.Add(new MeshData($"Mesh_{m}", localPositions.ToArray(), localNormals?.ToArray() ?? Array.Empty<float>(),
                localUvs.Select(uv => uv.ToArray()).ToArray(), indices.ToArray(), Array.Empty<int>(), Array.Empty<float>(),
                materialIndex));

            int LocalIndex(int vertex)
            {
                if (remap.TryGetValue(vertex, out var local)) return local;

                local = remap.Count;
                remap[vertex] = local;
                var position = positions[vertex];
                localPositions.Add(position.X);
                localPositions.Add(position.Y);
                localPositions.Add(position.Z);
                if (localNormals is not null)
                {
                    var normal = normals![vertex];
                    localNormals.Add(normal.X);
                    localNormals.Add(normal.Y);
                    localNormals.Add(normal.Z);
                }
                for (int c = 0; c < uvChannels.Count; c++)
                {
                    var uv = uvChannels[c][vertex];
                    localUvs[c].Add(uv.X);
                    localUvs[c].Add(uv.Y);
                }
                return local;
            }
        }

        return new ModelData(Path.GetFileName(fullPath), meshes, materials, null, texturePaths);
    }

    /// <summary>
    /// SOTC/DeSR sub-mesh face range, exactly as the reference implementation derives it
    /// (<c>BluePointConvert.cs:660-690</c>, which the spec quotes): SOTC uses the face-vertices
    /// fields, DeSR the face-index fields; an all-zero range means "the rest of the buffer"
    /// (failsafe for optimized-away ranges); <paramref name="currentFace"/> advances by the range
    /// actually taken. Returns false for a sub-mesh with nothing to draw (skipped).
    /// </summary>
    private static bool TryGetFaceRange(
        CMSHHeader header, int meshIndex, int faceListCount, ref int currentFace, out int startFace, out int faceCount)
    {
        var reference = header.meshList[meshIndex];
        if (header.isDeSR)
        {
            startFace = reference.startingFaceIndex / 3;
            faceCount = reference.faceIndexCount / 3;
        }
        else
        {
            startFace = reference.startingFaceVertIndex / 3;
            faceCount = reference.faceVertIndicesUsed / 3;
        }

        // BluePoint's optimisation left some sub-meshes with no faces: the marker stays in the
        // face-index field (DeSR-only data), while the range itself is zero.
        if (faceCount == 0 && reference.faceIndexCount > 1)
            return false;

        bool emptyRange = header.isDeSR
            ? reference.startingFaceIndex <= 0 && reference.faceIndexCount <= 0
            : reference.startingFaceVertIndex <= 0 && reference.faceVertIndicesUsed <= 0;
        if (emptyRange)
        {
            startFace = currentFace;
            faceCount = faceListCount - currentFace;
        }

        currentFace = startFace + faceCount;
        if (startFace < 0 || startFace >= faceListCount || faceCount <= 0)
            return false;

        // Defensive clamp: the reference slices without bounds checks; the writer has none either.
        faceCount = Math.Min(faceCount, faceListCount - startFace);
        return true;
    }

    private static bool InRange(int vertex, int vertexCount) => vertex >= 0 && vertex < vertexCount;

    /// <summary>
    /// Per-vertex normals: the packed QUT0 TBN stream when a title stores one (the library decodes
    /// it), else the raw 4-byte-per-vertex NRM0 stream SOTC actually ships (the library has no
    /// decoder; <see cref="DecodePackedNormal"/> implements the packing). Null when neither stream
    /// matches the vertex count.
    /// </summary>
    private static List<Vector3>? ReadNormals(CMSHVertexData vertexData, int vertexCount)
    {
        if (vertexCount == 0) return null;

        if (vertexData.qut0List.Count == vertexCount)
        {
            vertexData.GetQut0Data(out var quatNormals, out _, out _);
            if (quatNormals.Count == vertexCount) return quatNormals;
        }

        // NRM0/TAN0 both append to normalTemp, in vertDefs order; take the NRM0 block.
        int block = 0;
        foreach (var definition in vertexData.vertDefs)
        {
            if (definition.dataMagic is not (VertexMagic.NRM0 or VertexMagic.TAN0)) continue;

            if (definition.dataMagic == VertexMagic.NRM0 && vertexData.normalTemp.Count >= (block + 1L) * vertexCount)
            {
                var normals = new List<Vector3>(vertexCount);
                for (int v = 0; v < vertexCount; v++)
                    normals.Add(DecodePackedNormal(vertexData.normalTemp[block * vertexCount + v]));
                return normals;
            }
            block++;
        }
        return null;
    }

    /// <summary>
    /// One NRM0/TAN0 entry (4 bytes) as a unit normal: three 10-bit signed components scaled by
    /// 1/511.5 — the same packing the library's <c>UnpackQUT0</c> uses for a QUT0's vector part
    /// (<c>CMSHVertexData.cs:414-421</c>) — read little-endian, normalized defensively. Derived
    /// and verified in the adapter notes: decoded lengths are 0.998–1.001 on the pinned samples
    /// and the directions agree with the face winding (mean dot 0.91–0.999 across 5 meshes).
    /// </summary>
    private static Vector3 DecodePackedNormal(byte[] entry)
    {
        var packed = BinaryPrimitives.ReadUInt32LittleEndian(entry);
        var vector = new Vector3(
            ((packed & 0x3FF) - 511.5f) / 511.5f,
            (((packed >> 10) & 0x3FF) - 511.5f) / 511.5f,
            (((packed >> 20) & 0x3FF) - 511.5f) / 511.5f);
        var length = vector.Length();
        // A degenerate entry (no direction) falls back to the Y-up flat normal rather than NaN.
        return length > 1e-6f ? vector / length : Vector3.UnitY;
    }

    /// <summary>UV streams (TEX0..TEX3) whose length matches the vertex count, in channel order.</summary>
    private static List<List<Vector2>> ReadUvChannels(CMSHVertexData vertexData, int vertexCount)
    {
        var channels = new List<List<Vector2>>();
        foreach (var magic in UvMagics)
        {
            if (vertexData.uvDict.TryGetValue(magic, out var uv) && uv.Count == vertexCount && uv.Count > 0)
                channels.Add(uv);
        }
        return channels;
    }

    // ---------------------------------------------------------------------------------------------
    // Material chain: cmsh matName → sibling .cmdl material map → CMAT → texture references.
    // ---------------------------------------------------------------------------------------------

    /// <summary>
    /// The <c>matName → CMAT</c> chain for a bare <c>.cmsh</c>. An empty map means "no sibling
    /// <c>.cmdl</c>" (recorded on <paramref name="note"/>): the model degrades to the single
    /// <see cref="DefaultMaterialName"/> material. A map entry is null when its <c>.cmat</c>
    /// reference does not resolve (also recorded).
    /// </summary>
    private Dictionary<string, CMAT?> MaterialChain(string cmshPath, Action<string>? note)
    {
        var chain = new Dictionary<string, CMAT?>(StringComparer.Ordinal);
        var cmdlPath = FindSiblingCmdl(cmshPath);
        if (cmdlPath is null)
        {
            note?.Invoke($"no sibling .cmdl beside {Path.GetFileName(cmshPath)}: single default material, no textures");
            return chain;
        }

        var cmdl = new CMDL(File.ReadAllBytes(cmdlPath));
        foreach (var (matName, cmatReference) in cmdl.GetCMATMaterialMap())
        {
            var cmatPath = ResolveReference(cmatReference, cmshPath);
            if (cmatPath is null)
                note?.Invoke($"unresolved material reference '{cmatReference}' (material '{matName}')");
            chain[matName] = cmatPath is null ? null : new CMAT(File.ReadAllBytes(cmatPath));
        }
        return chain;
    }

    /// <summary>
    /// The sibling <c>.cmdl</c> that owns <paramref name="cmshPath"/>: the exact stem first
    /// (<c>wander.cmsh</c> → <c>wander.cmdl</c>), then a stem extension (<c>obj_plank_01.cmsh</c> →
    /// <c>obj_plank_01_52f8cc91.cmdl</c>; the game's unique-id suffix), deterministically the first
    /// by ordinal path order. Covers 6,902 of 22,012 tree meshes; see the adapter notes for the rest.
    /// </summary>
    private static string? FindSiblingCmdl(string cmshPath)
    {
        var directory = Path.GetDirectoryName(cmshPath);
        if (directory is null || !Directory.Exists(directory)) return null;

        var stem = Path.GetFileNameWithoutExtension(cmshPath);
        string? extensionMatch = null;
        foreach (var candidate in Directory.EnumerateFiles(directory).OrderBy(path => path, StringComparer.Ordinal))
        {
            if (!Path.GetExtension(candidate).Equals(".cmdl", StringComparison.OrdinalIgnoreCase)) continue;

            var candidateStem = Path.GetFileNameWithoutExtension(candidate);
            if (string.Equals(candidateStem, stem, StringComparison.OrdinalIgnoreCase)) return candidate;
            if (extensionMatch is null && candidateStem.StartsWith(stem + "_", StringComparison.OrdinalIgnoreCase))
                extensionMatch = candidate;
        }
        return extensionMatch;
    }

    /// <summary>
    /// Resolves a <c>$</c>-rooted game reference: strip <c>$/</c>, replace the <c>****</c> platform
    /// wildcard with <c>_cmn</c>, <c>_ps4</c>, then <c>_ps5</c> (first existing wins — the same rule
    /// Task 3 pinned for <c>.ctxc</c> chunks; the library's own order is <c>_cmn</c>/<c>_ps5</c>/<c>_ps4</c>).
    /// </summary>
    private string? ResolveReference(string reference, string relativeToFile)
    {
        var relative = reference.StartsWith("$/", StringComparison.Ordinal) ? reference[2..] : reference;
        relative = relative.Replace('\\', '/');

        if (!relative.Contains("****"))
            return _resolver.Resolve(relative, relativeToFile);

        foreach (var platform in PlatformWildcards)
        {
            var resolved = _resolver.Resolve(relative.Replace("****", platform), relativeToFile);
            if (resolved is not null) return resolved;
        }
        return null;
    }

    /// <summary>
    /// Material for one sub-mesh: the chain's CMAT when the <c>matName</c> is known, otherwise a
    /// named material without textures — or the single default material when there is no chain.
    /// Every <c>texNames</c> entry of a resolved CMAT is registered as a referenced texture
    /// (they are all converted), while the slots take the determinable ones.
    /// </summary>
    private int MaterialIndex(string? matName, Dictionary<string, CMAT?> chain, List<MaterialData> materials,
        Dictionary<string, int> indexByName, string cmshPath, List<string> texturePaths, HashSet<string> knownTextures,
        Action<string>? note)
    {
        bool chained = chain.Count > 0 && !string.IsNullOrEmpty(matName);
        var key = chained ? matName! : DefaultMaterialName;
        if (indexByName.TryGetValue(key, out var existing)) return existing;

        string? baseColor = null;
        string? normal = null;
        string? tangentNormal = null;
        if (chained && chain.TryGetValue(matName!, out var cmat) && cmat is not null)
        {
            foreach (var texName in cmat.texNames)
            {
                var resolved = ResolveTexture(texName, cmshPath, texturePaths, knownTextures, note);
                if (resolved is null) continue;
                if (baseColor is null && IsBaseColorTexture(texName)) baseColor = resolved;
                switch (ClassifyNormal(texName))
                {
                    case NormalKind.Plain when normal is null:
                        normal = resolved;
                        break;
                    case NormalKind.Tangent when tangentNormal is null:
                        tangentNormal = resolved;
                        break;
                }
            }
            // Hair has only a tangent-space normal (6 of 8,920 materials): still a valid glTF normal map.
            normal ??= tangentNormal;
            // No '_col' anywhere: the first reference is the best available albedo (adapter notes).
            if (baseColor is null && cmat.texNames.Count > 0)
                baseColor = ResolveTexture(cmat.texNames[0], cmshPath, texturePaths, knownTextures, note);
        }

        materials.Add(new MaterialData(key, baseColor, normal, null, White, Black));
        indexByName[key] = materials.Count - 1;
        return materials.Count - 1;
    }

    /// <summary>
    /// Registers one <c>$/…x.ctxr</c> reference; returns its resolved absolute path (or null,
    /// recorded on <paramref name="note"/>).
    /// </summary>
    private string? ResolveTexture(string reference, string relativeToFile, List<string> texturePaths,
        HashSet<string> known, Action<string>? note)
    {
        var resolved = ResolveReference(reference, relativeToFile);
        if (resolved is null)
        {
            note?.Invoke($"unresolved texture reference '{reference}'");
            return null;
        }
        if (known.Add(resolved)) texturePaths.Add(resolved);
        return resolved;
    }

    /// <summary>
    /// The albedo heuristic of the reference implementation (<c>BluePointConvert.cs:987-1001</c>:
    /// "If we can find it, make the albedo the texture" via <c>tex.Contains("_col.")</c>),
    /// evaluated token-wise so the <c>_col_02</c>/<c>_col_alpha</c> spellings are caught too.
    /// </summary>
    private static bool IsBaseColorTexture(string texName) => StemTokens(texName).Contains("col");

    /// <summary>Normal-map classification from the last two stem tokens (see the adapter notes).</summary>
    private enum NormalKind
    {
        /// <summary>Not a normal map (or a specular-detail/flow variant that must not be one).</summary>
        None,

        /// <summary>A surface normal map: last token <c>nml</c>/<c>nrm</c>, no special family token.</summary>
        Plain,

        /// <summary>A tangent-space normal map (<c>_tangent_nml</c>): the fallback when no plain one exists.</summary>
        Tangent,
    }

    /// <summary>
    /// The map type of a <c>_nml</c>/<c>_nrm</c> reference, from the token before the extension
    /// token: <c>spec</c>/<c>specular</c> (specular-detail normals) and <c>flow</c>/<c>flowmap</c>
    /// (velocity maps, not normals at all) are never mapped; <c>tangent</c> is a tangent-space
    /// normal — exactly what a glTF <c>normalTexture</c> is — and is the fallback.
    /// </summary>
    private static NormalKind ClassifyNormal(string texName)
    {
        var tokens = StemTokens(texName);
        if (tokens.Count == 0) return NormalKind.None;

        var last = tokens[^1];
        if (last is not ("nml" or "nrm")) return NormalKind.None;

        var family = tokens.Count >= 2 ? tokens[^2] : string.Empty;
        return family switch
        {
            "spec" or "specular" or "flow" or "flowmap" => NormalKind.None,
            "tangent" => NormalKind.Tangent,
            _ => NormalKind.Plain,
        };
    }

    /// <summary>Stem of a texture reference, lowercased, split on '_', minus a trailing unique-id hash.</summary>
    private static List<string> StemTokens(string texName)
    {
        var stem = Path.GetFileNameWithoutExtension(texName).ToLowerInvariant();
        var tokens = stem.Split('_', StringSplitOptions.RemoveEmptyEntries).ToList();
        if (tokens.Count > 1 && tokens[^1].Length == 8 && tokens[^1].All(IsHexDigit))
            tokens.RemoveAt(tokens.Count - 1);
        return tokens;
    }

    private static bool IsHexDigit(char c) => (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}
