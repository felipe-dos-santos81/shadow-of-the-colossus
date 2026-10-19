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
/// the decoded quaternions live in the game's frame, whose Y and Z axes are mirrored relative to
/// glTF's, so the rotation is emitted as <c>(x, −y, −z, w)</c>. The root position series needs no
/// bridge (it is already in the glb frame).
/// </summary>
public static class AnimBridge
{
    /// <summary>Converts a game-convention quaternion's XYZ and W to the glTF frame: <c>(x, −y, −z, w)</c>.</summary>
    public static Quaternion ToGlb(Vector3 xyz, float w) => new(xyz.X, -xyz.Y, -xyz.Z, w);
}
