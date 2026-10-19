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

    /// <summary>Per-frame root position series; empty when the file carries no root descriptor.</summary>
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
}
