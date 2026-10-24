using System.Text.Json;
using System.Text.Json.Nodes;
using BpConvert.Core.Pipeline;

namespace BpConvert.Core.Anim;

/// <summary>
/// The curated, small half of the events sidecar that the objanim files cannot say: which spawned
/// damage objanim a type-29 effect starts (the link lives in the eboot's effect tables), and the
/// colossus's <c>damage_def</c> rows (<c>amount</c>, <c>reaction</c>) by volume name.
/// File: <c>tools/re/objanim/&lt;colossus&gt;_map.json</c>.
/// </summary>
public sealed class ObjAnimMap
{
    /// <summary>The skeleton (bank stem) the map belongs to, e.g. <c>knight_a</c>.</summary>
    public required string Skeleton { get; init; }

    /// <summary>Effect name → spawned objanim names (stems without <c>objanim_</c>).</summary>
    public required IReadOnlyDictionary<string, string[]> Spawns { get; init; }

    /// <summary>Damage volume name → (amount, reaction).</summary>
    public required IReadOnlyDictionary<string, (double Amount, double Reaction)> Damage { get; init; }

    /// <summary>Every spawned objanim the map names, sorted.</summary>
    public IReadOnlyList<string> SpawnedNames =>
        Spawns.Values.SelectMany(names => names).Distinct(StringComparer.Ordinal)
            .Order(StringComparer.Ordinal).ToList();

    /// <summary>Reads a map file.</summary>
    /// <exception cref="InvalidDataException">The file lacks <c>skeleton</c>.</exception>
    public static ObjAnimMap Load(string path)
    {
        var root = JsonNode.Parse(File.ReadAllText(path))?.AsObject()
            ?? throw new InvalidDataException($"{path}: empty objanim map");
        var skeleton = (string?)root["skeleton"]
            ?? throw new InvalidDataException($"{path}: objanim map has no \"skeleton\"");
        return new ObjAnimMap
        {
            Skeleton = skeleton,
            Spawns = (root["spawns"]?.AsObject() ?? []).ToDictionary(
                p => p.Key, p => p.Value!.AsArray().Select(n => (string)n!).ToArray(), StringComparer.Ordinal),
            Damage = (root["damage"]?.AsObject() ?? []).ToDictionary(
                p => p.Key, p => ((double)p.Value!["amount"]!, (double)p.Value["reaction"]!), StringComparer.Ordinal),
        };
    }

    /// <summary>The map in <paramref name="directory"/> (<c>*_map.json</c>) for <paramref name="skeleton"/>, or null.</summary>
    public static ObjAnimMap? Find(string? directory, string skeleton)
    {
        if (directory is null || !Directory.Exists(directory)) return null;
        foreach (var path in Directory.EnumerateFiles(directory, "*_map.json").Order(StringComparer.Ordinal))
        {
            var map = Load(path);
            if (map.Skeleton == skeleton) return map;
        }

        return null;
    }
}

/// <summary>
/// The <c>&lt;glb stem&gt;.events.json</c> sidecar (<c>sotc-objanim-events/1</c>) written beside a glb
/// whose conversion baked clips: each baked clip's <c>objanim_&lt;clip&gt;.anb</c> event tracks (judge
/// volumes, damage volumes, effect events), the skeleton's default judge volumes
/// (<c>objanim_&lt;bank&gt;_skelton.anb</c>), and the spawned damage objanims the curated
/// <see cref="ObjAnimMap"/> names. Row shapes follow the recon
/// <c>objanim/schema.json</c> <c>$defs</c>; the file's top level is <c>{format, skeleton, frame,
/// judgeDefaults, clips{name→{fps, frames, judge, damage, effects}}, spawned}</c>.
/// </summary>
/// <remarks>
/// Frame: the glb model frame. Positions and offsets x → −x, quaternions <c>(x, y, z, w) →
/// (−x, y, z, w)</c> (<c>AnimBridge.ToGlb</c>); bone-parented rows stay in their joint's frame
/// (<c>docs/notes/s12-recon.md</c> R4) and the pose is the app's job, so each row keeps its parent
/// kind (<c>entity</c>, <c>entityRoot</c>, <c>skelton</c>, <c>{bone}</c>, <c>{optBone}</c>,
/// damage-only <c>spawn</c>). Times are the objanim's own frames at its own fps. Like the clip
/// manifest it is written only for conversions that baked clips with objanim files, and it is
/// derived game data: it stays out of git.
/// </remarks>
public static class EventsSidecar
{
    /// <summary>The <c>format</c> string a consumer checks.</summary>
    public const string Format = "sotc-objanim-events/1";

    private const string Prefix = "objanim_";

    private static readonly string[] JudgeShapes = ["cylinder", "ellipsoid", "box"];

    /// <summary>
    /// Builds the sidecar JSON (compact) for <paramref name="clipNames"/> of one skeleton bank.
    /// Clips without an objanim file, or whose objanim is not a flag-set bank (refused), are left out.
    /// </summary>
    /// <param name="gameRoot">Game tree root; files are read from <c>cp3shadow/anim</c> and <c>cp3shadow/skel</c>.</param>
    /// <param name="bankStem">The skb bank stem, e.g. <c>knight_a</c>; its record names resolve bone parents.</param>
    /// <param name="clipNames">Baked clip stems (<c>knight_a_walk</c>).</param>
    /// <param name="map">Optional curated map (spawns, damage defs).</param>
    /// <param name="note">Optional sink for skipped-file notes.</param>
    /// <exception cref="InvalidDataException">A named objanim is malformed, or a parent index is outside the bank.</exception>
    /// <exception cref="FileNotFoundException">The map names a spawned objanim that is not in the game tree.</exception>
    public static string Build(string gameRoot, string bankStem, IReadOnlyList<string> clipNames,
        ObjAnimMap? map, Action<string>? note = null)
    {
        var animDirectory = Path.Combine(gameRoot, ClipSet.AnimDirectory);
        var bones = SkbReader.Read(Path.Combine(gameRoot, ClipSet.SkeletonDirectory, bankStem + ".skb"))
            .Records.Select(record => record.Name).ToList();

        var root = new JsonObject
        {
            ["format"] = Format,
            ["skeleton"] = bankStem,
            ["frame"] = "glb",
        };

        var defaults = TryRead(Path.Combine(animDirectory, $"{Prefix}{bankStem}_skelton.anb"), note);
        if (defaults is not null)
        {
            var judge = new JsonArray();
            foreach (var track in defaults.Tracks.Where(t => t.Type is 44 or 46))
                judge.Add(JudgeRow(track, bones));
            if (judge.Count > 0) root["judgeDefaults"] = judge;
        }

        var clips = new JsonObject();
        foreach (var name in clipNames)
        {
            var objanim = TryRead(Path.Combine(animDirectory, $"{Prefix}{name}.anb"), note);
            if (objanim is null) continue;

            var judge = new JsonArray();
            var damage = new JsonArray();
            var effects = new JsonArray();
            foreach (var track in objanim.Tracks)
            {
                switch (track.Type)
                {
                    case 44 or 46:
                        judge.Add(JudgeRow(track, bones));
                        break;
                    case 3 or 10 or 13:
                        if (DamageRow(track, ParentOf(track.Parent, bones), map) is { } row) damage.Add(row);
                        break;
                    case 29:
                        if (EffectRow(track, bones, map) is { } effect) effects.Add(effect);
                        break;
                }
            }

            if (judge.Count + damage.Count + effects.Count == 0) continue;
            clips[name] = new JsonObject
            {
                ["fps"] = Round(objanim.Fps),
                ["frames"] = objanim.Frames,
                ["judge"] = judge,
                ["damage"] = damage,
                ["effects"] = effects,
            };
        }

        root["clips"] = clips;

        if (map is not null && map.SpawnedNames.Count > 0)
        {
            var spawned = new JsonObject();
            foreach (var name in map.SpawnedNames)
            {
                var path = Path.Combine(animDirectory, $"{Prefix}{name}.anb");
                var objanim = ObjAnimReader.Read(File.ReadAllBytes(path)); // the map names it: absent = loud
                var damage = new JsonArray();
                foreach (var track in objanim.Tracks.Where(t => t.Type is 3 or 10 or 13))
                    if (DamageRow(track, "spawn", map) is { } row) damage.Add(row);
                spawned[name] = new JsonObject
                {
                    ["frames"] = objanim.Frames,
                    ["fps"] = Round(objanim.Fps),
                    ["damage"] = damage,
                };
            }

            root["spawned"] = spawned;
        }

        return root.ToJsonString();
    }

    /// <summary>
    /// Writes <c>&lt;glb stem&gt;.events.json</c> beside <paramref name="glbPath"/> for the baked
    /// <paramref name="clips"/> (those sharing the first clip's bank), or removes a stale one when
    /// no objanim file applies. Returns the path written, or <c>null</c>.
    /// </summary>
    public static string? Write(string glbPath, string gameRoot, IReadOnlyList<ClipSource> clips,
        string? mapDirectory, Action<string>? note = null)
    {
        var path = PathFor(glbPath);
        if (clips.Count > 0)
        {
            var bank = Path.GetFileNameWithoutExtension(clips[0].BankPath);
            var names = clips.Where(c => Path.GetFileNameWithoutExtension(c.BankPath) == bank)
                .Select(c => c.Name).ToList();
            if (names.Count < clips.Count)
                note?.Invoke($"events sidecar: clips of other banks than '{bank}' are not covered");

            var animDirectory = Path.Combine(gameRoot, ClipSet.AnimDirectory);
            var any = names.Any(n => File.Exists(Path.Combine(animDirectory, $"{Prefix}{n}.anb")))
                || File.Exists(Path.Combine(animDirectory, $"{Prefix}{bank}_skelton.anb"));
            if (any)
            {
                var json = Build(gameRoot, bank, names, ObjAnimMap.Find(mapDirectory, bank), note);
                File.WriteAllText(path, json);
                return path;
            }
        }

        File.Delete(path);
        return null;
    }

    /// <summary>Removes the sidecar beside <paramref name="glbPath"/>, if any.</summary>
    public static void Delete(string glbPath) => File.Delete(PathFor(glbPath));

    private static string PathFor(string glbPath) => Path.ChangeExtension(glbPath, ".events.json");

    private static ObjAnimClip? TryRead(string path, Action<string>? note)
    {
        if (!File.Exists(path)) return null;
        try
        {
            return ObjAnimReader.Read(File.ReadAllBytes(path));
        }
        catch (InvalidDataException e) when (ObjAnimReader.IsRefusal(e))
        {
            note?.Invoke($"events sidecar: skipped {Path.GetFileName(path)} ({e.Message})");
            return null;
        }
    }

    // ---- rows ----

    private static JsonObject JudgeRow(ObjAnimTrack track, List<string> bones)
    {
        if (track.Kind is < 0 or > 2)
            throw new InvalidDataException($"objanim: judge '{track.Name}' has unknown kind {track.Kind}");
        var row = new JsonObject
        {
            ["name"] = track.Name,
            ["shape"] = JudgeShapes[track.Kind],
            ["parent"] = ParentOf(track.Parent, bones),
        };
        if (track.Type == 44)
        {
            row["inner"] = Round(track.Keys[0].Vec2.X);
            row["angle"] = Round(track.Keys[0].Vec2.Y);
        }

        Series(row, track, Enumerable.Range(0, track.Keys.Count).ToList(), "size");
        return row;
    }

    private static JsonObject? DamageRow(ObjAnimTrack track, JsonNode parent, ObjAnimMap? map)
    {
        var windows = track.ActiveWindows();
        if (windows.Count == 0) return null;
        var row = new JsonObject
        {
            ["name"] = track.Name,
            ["type"] = track.Type,
            ["shape"] = track.Type switch { 3 => "sphere", 10 => "cylinder", _ => "box" },
            ["parent"] = parent,
            ["windows"] = new JsonArray(windows.Select(w => (JsonNode)new JsonArray(w.First, w.Last)).ToArray()),
        };
        if (map is not null && map.Damage.TryGetValue(track.Name, out var def))
            row["def"] = new JsonObject { ["amount"] = def.Amount, ["reaction"] = def.Reaction };

        var frames = windows.SelectMany(w => Enumerable.Range(w.First, w.Last - w.First + 1)).ToList();
        Series(row, track, frames, "size");
        return row;
    }

    private static JsonObject? EffectRow(ObjAnimTrack track, List<string> bones, ObjAnimMap? map)
    {
        if (track.Events is not { Count: > 0 } events) return null;
        var row = new JsonObject
        {
            ["name"] = track.Name,
            ["type"] = track.Type,
            ["parent"] = ParentOf(track.Parent, bones),
            ["events"] = new JsonArray(events.Select(e => (JsonNode)new JsonArray(e.Start, e.End)).ToArray()),
        };
        if (map is not null && map.Spawns.TryGetValue(track.Name, out var spawns))
            row["spawns"] = new JsonArray(spawns.Select(s => (JsonNode)s).ToArray());

        Series(row, track, events.Select(e => e.Start).Where(f => f < track.Keys.Count).ToList(), "vec");
        return row;
    }

    /// <summary>
    /// The pose channel of a row: <c>static</c> when every exported frame is equal (4 decimals),
    /// else <c>keys</c> = one <c>{f, pos, quat, size|vec}</c> per exported frame.
    /// </summary>
    private static void Series(JsonObject row, ObjAnimTrack track, List<int> frames, string vecName)
    {
        var keys = frames.Select(f => (Frame: f, Key: KeyJson(track.Keys[f], vecName))).ToList();
        if (keys.All(k => k.Key.ToJsonString() == keys[0].Key.ToJsonString()))
        {
            row["static"] = keys[0].Key;
            return;
        }

        var array = new JsonArray();
        foreach (var (frame, key) in keys)
        {
            var withFrame = new JsonObject { ["f"] = frame };
            foreach (var (name, value) in key.ToArray()) withFrame[name] = value?.DeepClone();
            array.Add(withFrame);
        }

        row["keys"] = array;
    }

    /// <summary>One key in the glb frame: position x and quaternion x mirrored.</summary>
    private static JsonObject KeyJson(ObjAnimKey key, string vecName) => new()
    {
        ["pos"] = Vec(-key.Pos.X, key.Pos.Y, key.Pos.Z),
        ["quat"] = new JsonArray(Round(-key.Quat.X), Round(key.Quat.Y), Round(key.Quat.Z), Round(key.Quat.W)),
        [vecName] = Vec(key.Vec.X, key.Vec.Y, key.Vec.Z),
    };

    private static JsonArray Vec(double x, double y, double z) => new(Round(x), Round(y), Round(z));

    /// <summary>The track's aux word as a schema <c>parent</c>.</summary>
    private static JsonNode ParentOf(int word, List<string> bones)
    {
        switch (word)
        {
            case -1: return "entity";
            case -2: return "entityRoot";
            case -3: return "skelton";
        }

        if (word > 0 && (word & 0x10000) != 0)
            return new JsonObject { ["optBone"] = word & 0xFFFF };
        if (word >= 0 && word < bones.Count)
            return new JsonObject { ["bone"] = bones[word] };
        throw new InvalidDataException($"objanim: parent word {word} is outside the bank's {bones.Count} records");
    }

    /// <summary>Rounds to 4 decimals; −0 prints as 0.</summary>
    private static double Round(double value) => Math.Round(value, 4) + 0.0;
}
