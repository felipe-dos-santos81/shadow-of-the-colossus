namespace BpConvert.Core.Pipeline;

/// <summary>
/// How a baked clip's root position series relates to the ground, recorded beside the glb as
/// <c>rootMotion</c> (S4.2). A name-keyed table: the clip stem determines the class, never the
/// decoded data, so the record stays stable across re-conversions.
/// </summary>
/// <remarks>
/// The label is provenance only this pass (design §11.4): the app keeps its existing lift rule
/// and does not read the sidecar. <c>ground-height</c> means the series is a terrain-height
/// track (locomotion, climbing grips, falls); <c>body-offset</c> means it is an offset applied to
/// the body (damage reactions, attacks, the colossus); <c>unclassified</c> is the honest
/// fallback for a clip this table has not been taught yet.
/// </remarks>
public static class ClipMotion
{
    /// <summary>The root series gives a ground height.</summary>
    public const string GroundHeight = "ground-height";

    /// <summary>The root series gives a body offset.</summary>
    public const string BodyOffset = "body-offset";

    /// <summary>No decision for this name yet; the record stays honest rather than guessing.</summary>
    public const string Unclassified = "unclassified";

    /// <summary>
    /// Classifies the clip named <paramref name="name"/> (the <c>.anb</c> stem, e.g.
    /// <c>player_a_damage_h_fly1</c>).
    /// </summary>
    public static string For(string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);

        return name switch
        {
            "player_a_stand" or "player_a_walk" or "player_a_run"
                or "player_a_fall_damage_start" or "player_a_fall_damage_end"
                or "player_a_fall_damage_to_death" => GroundHeight,
            "player_a_atk1" => BodyOffset,
            _ when name.StartsWith("player_a_grip_", StringComparison.Ordinal) => GroundHeight,
            _ when name.StartsWith("player_a_damage_", StringComparison.Ordinal) => BodyOffset,
            _ when name.StartsWith("minotaur_a_", StringComparison.Ordinal) => BodyOffset,
            _ => Unclassified,
        };
    }
}
