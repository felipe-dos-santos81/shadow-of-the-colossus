using BpConvert.Core.Anim;

namespace BpConvert.Core.Model;

/// <summary>
/// Renderer-agnostic model data produced from parsed game files (Tasks 4/5) and consumed by
/// <see cref="Gltf.GlbWriter"/>. Geometry is kept in game coordinates: the game and glTF are
/// both Y-up, so no axis conversion happens.
/// </summary>
/// <param name="Name">Mesh name (used for the glTF mesh and node names).</param>
/// <param name="Positions">XYZ per vertex.</param>
/// <param name="Normals">XYZ per vertex; empty means "no normals" (the NORMAL attribute is omitted).</param>
/// <param name="UvChannels">One UV set per channel; channel i is written as TEXCOORD_i.</param>
/// <param name="Indices">Triangle list; always written as UNSIGNED_INT.</param>
/// <param name="BoneIndices">4 joint indices per vertex; empty when the mesh is not skinned.</param>
/// <param name="BoneWeights">4 weights per vertex; empty when the mesh is not skinned.</param>
/// <param name="MaterialIndex">Index into <see cref="ModelData.Materials"/>; -1 = none.</param>
public sealed record MeshData(
    string Name, float[] Positions, float[] Normals, float[][] UvChannels, uint[] Indices,
    int[] BoneIndices, float[] BoneWeights, int MaterialIndex);

/// <summary>
/// One glTF material. Texture slots carry the source (.ctxr) relative path, resolved through the
/// writer's texture map; a slot is omitted from the output when it is null or absent from the map.
/// </summary>
/// <param name="BaseColorFactor">RGBA; 3 components are padded with alpha 1.</param>
/// <param name="EmissiveFactor">RGB.</param>
/// <param name="EmissiveTexture">
/// glTF <c>emissiveTexture</c> source path; null = none. Used as the carrier for the corpse's
/// veins layer (the dead materials' sixth slot): that texture is black with an alpha veil, so the
/// app reads it as a dark overlay — see the S6 design D9 and <c>app/shaders/mesh.metal</c>.
/// </param>
public sealed record MaterialData(
    string Name, string? BaseColorTexture, string? NormalTexture, string? SpecularTexture,
    float[] BaseColorFactor, float[] EmissiveFactor, string? EmissiveTexture = null);

/// <summary>One skeleton joint: parent index in <see cref="SkeletonData.Joints"/> (-1 = root).</summary>
/// <param name="InverseBindMatrix16">Column-major 4x4 inverse bind matrix, written as-is (glTF is column-major).</param>
public sealed record SkeletonJoint(string Name, int ParentIndex, float[] InverseBindMatrix16);

/// <summary>Skeleton used by every skinned mesh of a model; joint order defines the JOINTS_0 indices.</summary>
public sealed record SkeletonData(string Name, List<SkeletonJoint> Joints);

/// <summary>Everything the writer needs for one output .glb.</summary>
/// <param name="ReferencedTexturePaths">Source texture paths used by this model (bookkeeping for the caller).</param>
public sealed record ModelData(
    string SourceRelativePath, List<MeshData> Meshes, List<MaterialData> Materials,
    SkeletonData? Skeleton, List<string> ReferencedTexturePaths)
{
    /// <summary>
    /// Clips emitted as glTF animations (one per clip). Empty means "no animations": the output
    /// keeps the M0 shape exactly (joint nodes carry <c>matrix</c>, no <c>animations</c> array).
    /// Each track's <see cref="AnimTrack.JointIndex"/> must be filled before writing.
    /// </summary>
    public List<AnimClip> Animations { get; } = new();
}
