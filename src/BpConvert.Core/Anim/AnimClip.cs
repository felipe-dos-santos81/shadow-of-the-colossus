using System.Numerics;

namespace BpConvert.Core.Anim;

/// <summary>
/// One decoded flag-clear <c>.anb</c> clip, in game convention: quaternions are raw (the
/// track→joint mapping and the glTF bridge are applied by later stages) and <see cref="Root"/>
/// is the per-frame root position series.
/// </summary>
public sealed class AnimClip
{
    /// <summary>File stem, e.g. <c>player_a_stand</c>.</summary>
    public required string Name { get; init; }

    /// <summary>Frame count (the structure's low u16 at motion+4).</summary>
    public required int Frames { get; init; }

    /// <summary>The clip's Q12 rate field (<c>(u16@(motion+6) &gt;&gt; 4) / 4096</c>), default 1.0.</summary>
    public required double Rate { get; init; }

    /// <summary>Playback rate in frames per second (<c>rate × 30</c>, the engine's identity).</summary>
    public double Fps => Rate * 30.0;

    /// <summary>One entry per track, in file order (track i == .skb record i).</summary>
    public required List<AnimTrack> Tracks { get; init; }

    /// <summary>
    /// Per-frame root position series; empty when the file carries no root descriptor. When
    /// non-empty it must have exactly <see cref="Frames"/> entries (the emission invariant: one
    /// key per engine frame; a mismatch is rejected with the clip's name).
    /// </summary>
    public required List<Vector3> Root { get; init; }
}

/// <summary>One quaternion curve: keyed (types 3–6/0xC/0xD/0xE/2) or static (type 0).</summary>
public sealed class AnimTrack
{
    /// <summary>Track index in the file (track i == .skb record i).</summary>
    public required int Index { get; init; }

    /// <summary>True for type 0: a single unit quaternion and no key array.</summary>
    public required bool IsStatic { get; init; }

    /// <summary>Keys in game convention, unmodified; a static track carries its single quaternion.</summary>
    public required List<Quaternion> Keys { get; init; }

    /// <summary>
    /// The emitted node's joint index, filled from <see cref="TrackMap.Map"/> by the pipeline;
    /// -1 = not mapped yet. The writer refuses a clip with any unresolved track rather than
    /// silently dropping its channel.
    /// </summary>
    public int JointIndex { get; set; } = -1;
}

/// <summary>
/// The game → glTF rotation bridge (RE-2 cross-validation, <c>docs/notes/re2-xval.md</c> §2):
/// the decoded quaternions are bridged by the RE tool to <c>(x, −y, −z, w)</c>, but the tool
/// composes them as <b>row-vector</b> local matrices, while a glTF node rotation is applied as
/// a <b>column-vector</b> local transform. The emitted glTF rotation is therefore the
/// inverse of the tool's bridged quaternion — <c>(−x, y, z, w)</c>, the same map with every
/// vector component negated. The root position series needs no bridge (it is already in the
/// glb frame, applied raw).
/// </summary>
/// <remarks>
/// Evidence (stand clip, frame 0): with the old row-vector map the reloaded glb put the head
/// below the pelvis (<c>Bone_KAO</c> y 0.3758 vs <c>Bone_KOSI</c> 0.7429 — Blender's own glTF
/// import agrees); the corrected map reproduces the decoder's upright pose
/// (<c>pose_dump.py pose</c>): <c>Bone_KAO</c> 1.117 (decode 1.1165), <c>Bone_HAND_A_L</c>
/// 0.627 (0.6269), <c>Bone_HAND_A_R</c> 0.624 (0.6243), toe tips 0.025/0.029
/// (0.0250/0.0286). The remaining bind-local residuals are the xval's own bands
/// (<c>AnimWriterTests</c>): core ≤ 40°, arm ≤ 60°.
/// </remarks>
public static class AnimBridge
{
    /// <summary>Converts a game-convention quaternion's XYZ and W to the glTF frame: <c>(−x, y, z, w)</c>.</summary>
    public static Quaternion ToGlb(Vector3 xyz, float w) => new(-xyz.X, xyz.Y, xyz.Z, w);
}
