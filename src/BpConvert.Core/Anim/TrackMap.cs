namespace BpConvert.Core.Anim;

/// <summary>
/// The clip track → glb joint map (RE-1 §2.2 linkage, the <c>tools/re/anb/pose_dump.py</c> rule):
/// track i is the sibling <c>.skb</c> bank's record i; the record's name is looked up exactly in
/// the model's glb joint names.
/// </summary>
public static class TrackMap
{
    /// <summary>
    /// Maps every track of <paramref name="clip"/> to a joint index.
    /// </summary>
    /// <param name="clip">The decoded clip; <c>Tracks[i]</c> is file track i.</param>
    /// <param name="bank">The sibling part bank; <c>Records[i]</c> is track i's bone.</param>
    /// <param name="jointNames">The model glb's skin joint names, in skin-joint order.</param>
    /// <param name="unresolved">
    /// How many tracks have no joint: the index is past the bank's records, or the name is absent
    /// from <paramref name="jointNames"/>.
    /// </param>
    /// <returns>
    /// <c>jointIndexByTrack[i]</c>: the index into <paramref name="jointNames"/> for track i, or -1
    /// when unresolved.
    /// </returns>
    public static int[] Map(AnimClip clip, SkbBank bank, IReadOnlyList<string> jointNames, out int unresolved)
    {
        ArgumentNullException.ThrowIfNull(clip);
        ArgumentNullException.ThrowIfNull(bank);
        ArgumentNullException.ThrowIfNull(jointNames);

        // Exact (ordinal) match; were a glb ever to repeat a name, the first joint keeps it.
        var jointByName = new Dictionary<string, int>(jointNames.Count, StringComparer.Ordinal);
        for (var joint = 0; joint < jointNames.Count; joint++)
            if (jointNames[joint] is { } name)
                jointByName.TryAdd(name, joint);

        var map = new int[clip.Tracks.Count];
        var missing = 0;
        for (var track = 0; track < map.Length; track++)
        {
            if (track < bank.Records.Count &&
                jointByName.TryGetValue(bank.Records[track].Name, out var jointIndex))
            {
                map[track] = jointIndex;
            }
            else
            {
                map[track] = -1;
                missing++;
            }
        }

        unresolved = missing;
        return map;
    }
}
