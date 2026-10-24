using System.Text.Json;
using System.Text.Json.Nodes;
using BpConvert.Core.Anim;
using BpConvert.Core.Pipeline;

/// <summary>
/// The objanim event-track reader and the <c>&lt;glb stem&gt;.events.json</c> sidecar (S12 task 3).
/// Pinned values come from the recon decoders (<c>.superpowers/sdd/2026-10-23-s12-recon/objanim/py</c>)
/// and <c>docs/notes/s12-recon.md</c> R4. The game tree is read-only; <see cref="TestAssets"/> fails
/// fast without SOTC_GAME_ROOT.
/// </summary>
public class ObjAnimTests
{
    private static ObjAnimClip Read(string name) =>
        ObjAnimReader.Read(File.ReadAllBytes(TestAssets.Sample($"cp3shadow/anim/{name}.anb")));

    [Fact]
    public void Quadratus_judge_box_decodes_in_the_game_frame()
    {
        var clip = Read("objanim_boss_a_attack_r");
        Assert.Equal(29, clip.Frames);
        Assert.Equal(15.271, clip.Fps, 3); // Q12 rate word 2085 (30 * 2085 / 4096)

        var box = clip.Tracks.Single(t => t.Type == 46 && t.Name == "MAMMOTH_LEG_R_ATK");
        Assert.Equal(clip.Frames, box.Keys.Count);
        Assert.Equal(-1, box.Parent); // entity
        var key = box.Keys[0];
        Assert.Equal(18.0875, key.Pos.X, 3);
        Assert.Equal(2.2019, key.Pos.Y, 3);
        Assert.Equal(37.2787, key.Pos.Z, 3);
        Assert.Equal(20.0, key.Vec.X, 3);
        Assert.Equal(15.0, key.Vec.Y, 3);
        Assert.Equal(30.0, key.Vec.Z, 3);
        // game-frame quaternion (0, 1, 0, 0): a half-turn about y
        Assert.Equal(1.0, key.Quat.Y, 3);
    }

    [Fact]
    public void Knight_footstep_event_is_bone_parented_with_its_window()
    {
        var clip = Read("objanim_knight_a_walk");
        var foot = clip.Tracks.Single(t => t.Type == 29 && t.Name == "KNIGHT_FOOT_R");
        Assert.Equal(8, foot.Parent); // knight_a.skb record 8 = Bone_TUMASAKI_R
        Assert.Equal(new[] { (40, 42) }, foot.Events);
        Assert.Equal(new[] { (17, 19) }, clip.Tracks.Single(t => t.Name == "KNIGHT_FOOT_L").Events);
        // type 11 (armour-debris emitters, hundreds per clip) is skipped on purpose
        Assert.DoesNotContain(clip.Tracks, t => t.Type == 11);
    }

    [Fact]
    public void Knight_stuck_sword_shock_is_active_through_the_whole_clip()
    {
        var clip = Read("objanim_knight_a_atk1_3");
        var shock = clip.Tracks.Single(t => t.Type == 13 && t.Name == "KNIGHT_ATK_SHOCK2");
        Assert.Equal(new[] { (0, 94) }, shock.ActiveWindows());
        Assert.Empty(clip.Tracks.Single(t => t.Name == "KNIGHT_SWORD_ATK1_DMG").ActiveWindows());
    }

    [Fact]
    public void Non_objanim_input_is_refused_with_a_clear_message()
    {
        // objanim_minotaur_a_death_front.anb is a JSON text file, not an xff container (measured).
        var text = Assert.Throws<InvalidDataException>(() => Read("objanim_minotaur_a_death_front"));
        Assert.Contains("not an xff container", text.Message);

        // A flag-clear motion bank is AnbReader's, not this reader's.
        var flagClear = Assert.Throws<InvalidDataException>(() => Read("player_a_stand"));
        Assert.Contains("flag-clear", flagClear.Message);
    }

    [Fact]
    public void Knight_sidecar_is_in_the_glb_frame_valid_and_small()
    {
        var map = ObjAnimMap.Load(RepoFile("tools/re/objanim/knight_map.json"));
        var clips = Directory.EnumerateFiles(TestAssets.Sample("cp3shadow/anim"), "objanim_knight_a_*.anb")
            .Select(p => Path.GetFileNameWithoutExtension(p)["objanim_".Length..])
            .Where(n => n != "knight_a_skelton")
            .OrderBy(n => n, StringComparer.Ordinal)
            .ToList();

        var json = EventsSidecar.Build(TestAssets.Root, "knight_a", clips, map);
        Assert.True(json.Length < 64 * 1024, $"sidecar is {json.Length} bytes");

        var root = JsonNode.Parse(json)!.AsObject();
        Assert.Equal("sotc-objanim-events/1", (string?)root["format"]);
        Assert.Equal("knight_a", (string?)root["skeleton"]);
        Assert.Equal("glb", (string?)root["frame"]);
        ValidateAgainstSchema(root);

        // R4: the walk footstep, the stuck-sword box and the glb mirror (x negated).
        var walk = root["clips"]!["knight_a_walk"]!.AsObject();
        Assert.Equal(46, (int)walk["frames"]!);
        var foot = walk["effects"]!.AsArray().Single(e => (string?)e!["name"] == "KNIGHT_FOOT_R")!;
        Assert.Equal(29, (int)foot["type"]!);
        Assert.Equal("Bone_TUMASAKI_R", (string?)foot["parent"]!["bone"]);
        Assert.Equal(40, (int)foot["events"]![0]![0]!);
        Assert.Equal(42, (int)foot["events"]![0]![1]!);
        Assert.Equal("knight_walk_dmg", (string?)foot["spawns"]![0]);

        var shock = root["clips"]!["knight_a_atk1_3"]!["damage"]!.AsArray()
            .Single(d => (string?)d!["name"] == "KNIGHT_ATK_SHOCK2")!;
        Assert.Equal("box", (string?)shock["shape"]);
        Assert.Equal(0, (int)shock["windows"]![0]![0]!);
        Assert.Equal(94, (int)shock["windows"]![0]![1]!);
        Assert.Equal(-9.936, (double)shock["static"]!["pos"]![0]!, 3); // raw +9.936, mirrored
        Assert.Equal(5, (int)shock["def"]!["amount"]!);
        Assert.Equal(9, (int)shock["def"]!["reaction"]!);

        // the spawned damage objanims named by the map are in the sidecar
        var spawned = root["spawned"]!.AsObject();
        Assert.Contains("knight_walk_dmg", spawned.Select(p => p.Key));
        Assert.Contains("knight_atk_dmg", spawned.Select(p => p.Key));
        var step = spawned["knight_walk_dmg"]!["damage"]!.AsArray()
            .Single(d => (string?)d!["name"] == "KNIGHT_SWORD_ATK1_DMG")!;
        Assert.Equal("spawn", (string?)step["parent"]);
        Assert.Equal(40, (int)step["def"]!["amount"]!);

        Assert.Equal(15, root["judgeDefaults"]!.AsArray().Count);
        Assert.All(root["judgeDefaults"]!.AsArray(), j => Assert.Equal("skelton", (string?)j!["parent"]));
    }

    [Fact]
    public void Phaedra_stomp_spawns_once_per_chain_and_the_sidecar_is_small()
    {
        var map = ObjAnimMap.Load(RepoFile("tools/re/objanim/kirin_map.json"));
        var anim = TestAssets.Sample("cp3shadow/anim");
        var clips = Directory.EnumerateFiles(anim, "objanim_kirin_a_*.anb")
            .Select(p => Path.GetFileNameWithoutExtension(p)["objanim_".Length..])
            .Where(n => File.Exists(Path.Combine(anim, n + ".anb"))) // _skelton / _kirin_region are not clips
            .OrderBy(n => n, StringComparer.Ordinal)
            .ToList();

        var json = EventsSidecar.Build(TestAssets.Root, "kirin_a", clips, map);
        Assert.True(json.Length < 64 * 1024, $"sidecar is {json.Length} bytes");
        var root = JsonNode.Parse(json)!.AsObject();
        ValidateAgainstSchema(root);

        JsonObject? Effect(string clip, string name) => root["clips"]![clip]!["effects"]!.AsArray()
            .FirstOrDefault(e => (string?)e!["name"] == name)?.AsObject();

        var stomp = Effect("kirin_a_atack_leg_r_a", "KIRIN_FOOT_ATK")!;
        Assert.Equal("Bone_TOE_R", (string?)stomp["parent"]!["bone"]);
        Assert.Equal("kirin_atk_dmg", (string?)stomp["spawns"]![0]);

        // the planted-hoof holds re-fire the stomp at frame 0: dropped (one hit per chain)
        Assert.Null(Effect("kirin_a_atack_leg_r_a_wait", "KIRIN_FOOT_ATK"));
        Assert.Null(Effect("kirin_a_atack_leg_l_b_wait", "KIRIN_FOOT_ATK"));
        Assert.Null(Effect("kirin_a_atack_leg_r_c_wait", "KIRIN_FOOT_ATK_ST"));
        Assert.NotNull(Effect("kirin_a_atack_leg_r_c", "KIRIN_FOOT_ATK_ST")); // the chain's own hit stays

        var step = Effect("kirin_a_walk", "KIRIN_FOOT")!;
        Assert.Equal("kirin_walk_dmg", (string?)step["spawns"]![0]);

        var spawned = root["spawned"]!.AsObject();
        var core = spawned["kirin_atk_dmg"]!["damage"]!.AsArray()
            .Single(d => (string?)d!["name"] == "GROUNDDEST_IMPACT_CORE")!;
        Assert.Equal(60, (int)core["def"]!["amount"]!); // KIRIN_DAMAGE_JIWARE 0x198e5e0
        Assert.Equal(30, (int)spawned["kirin_walk_dmg"]!["damage"]!.AsArray()
            .Single(d => (string?)d!["name"] == "GROUNDDEST_IMPACT2_CORE")!["def"]!["amount"]!);
    }

    [Fact]
    public void Quadratus_sidecar_row_is_the_mirrored_entity_box()
    {
        var json = EventsSidecar.Build(TestAssets.Root, "boss_a", ["boss_a_attack_r"], null);
        var root = JsonNode.Parse(json)!.AsObject();
        ValidateAgainstSchema(root);
        var row = root["clips"]!["boss_a_attack_r"]!["judge"]!.AsArray()
            .Single(j => (string?)j!["name"] == "MAMMOTH_LEG_R_ATK")!;
        Assert.Equal("entity", (string?)row["parent"]);
        Assert.Equal("box", (string?)row["shape"]);
        var first = row["keys"]![0]!;
        Assert.Equal(-18.0875, (double)first["pos"]![0]!, 3);
        Assert.Equal(2.2019, (double)first["pos"]![1]!, 3);
        Assert.Equal(37.2787, (double)first["pos"]![2]!, 3);
        Assert.Equal(20.0, (double)first["size"]![0]!, 3);
        Assert.Equal(-0.0, (double)first["quat"]![0]!, 3);
        Assert.Equal(1.0, (double)first["quat"]![1]!, 3);
    }

    [Fact]
    public void Write_puts_the_sidecar_beside_the_glb_and_removes_a_stale_one()
    {
        var dir = Directory.CreateTempSubdirectory("bp-events-");
        var glb = Path.Combine(dir.FullName, "knight.glb");
        var events = Path.Combine(dir.FullName, "knight.events.json");

        var clips = ClipSet.Resolve(TestAssets.Root, ["knight_a_walk", "knight_a_stand"]);
        var written = EventsSidecar.Write(glb, TestAssets.Root, clips,
            Path.GetDirectoryName(RepoFile("tools/re/objanim/knight_map.json")));
        Assert.Equal(events, written);
        var root = JsonNode.Parse(File.ReadAllText(events))!.AsObject();
        Assert.True(root["clips"]!.AsObject().ContainsKey("knight_a_walk"));

        // wander has no objanim at all: no sidecar, and the stale one goes
        var wander = ClipSet.Resolve(TestAssets.Root, ["player_a_stand"]);
        Assert.Null(EventsSidecar.Write(glb, TestAssets.Root, wander, null));
        Assert.False(File.Exists(events));
    }

    // ---- a hand-rolled check of the row shapes in objanim/schema.json ($defs) ----

    private static void ValidateAgainstSchema(JsonObject root)
    {
        Assert.NotNull(root["clips"]);
        foreach (var (name, clipNode) in root["clips"]!.AsObject())
        {
            var clip = clipNode!.AsObject();
            Assert.True((int)clip["frames"]! >= 1, name);
            Assert.True((double)clip["fps"]! > 0, name);
            foreach (var j in clip["judge"]!.AsArray()) Judge(j!.AsObject());
            foreach (var d in clip["damage"]!.AsArray()) Damage(d!.AsObject());
            foreach (var e in clip["effects"]!.AsArray()) Effect(e!.AsObject());
        }
        foreach (var j in root["judgeDefaults"]?.AsArray() ?? []) Judge(j!.AsObject());
        foreach (var (_, spawnNode) in root["spawned"]?.AsObject() ?? [])
        {
            var spawn = spawnNode!.AsObject();
            Assert.NotNull(spawn["frames"]);
            Assert.NotNull(spawn["fps"]);
            foreach (var d in spawn["damage"]!.AsArray()) Damage(d!.AsObject());
        }
    }

    private static void Judge(JsonObject row)
    {
        Assert.Contains((string?)row["shape"], new[] { "box", "cylinder", "ellipsoid" });
        Parent(row["parent"]!, allowSpawn: false);
        Assert.False(string.IsNullOrEmpty((string?)row["name"]));
        Animated(row, "size");
        if (row["shape"]!.ToString() == "box")
            Assert.Null(row["angle"]); // only type 44 carries the sector
        else
        {
            Assert.NotNull(row["inner"]);
            Assert.NotNull(row["angle"]);
        }
    }

    private static void Damage(JsonObject row)
    {
        Assert.Contains((int)row["type"]!, new[] { 3, 10, 13 });
        Assert.Contains((string?)row["shape"], new[] { "sphere", "cylinder", "box" });
        Parent(row["parent"]!, allowSpawn: true);
        Assert.NotEmpty(row["windows"]!.AsArray());
        foreach (var w in row["windows"]!.AsArray())
            Assert.True((int)w![0]! <= (int)w[1]!);
        Animated(row, "size");
        if (row["def"] is JsonObject def)
            Assert.All(def, p => Assert.True(p.Value!.GetValueKind() == JsonValueKind.Number));
    }

    private static void Effect(JsonObject row)
    {
        Assert.Contains((int)row["type"]!, new[] { 11, 29 });
        Parent(row["parent"]!, allowSpawn: false);
        Assert.NotEmpty(row["events"]!.AsArray());
        Animated(row, "vec");
    }

    private static void Parent(JsonNode parent, bool allowSpawn)
    {
        if (parent.GetValueKind() == JsonValueKind.String)
        {
            var allowed = new List<string> { "entity", "entityRoot", "skelton" };
            if (allowSpawn) allowed.Add("spawn");
            Assert.Contains(parent.ToString(), allowed);
        }
        else
        {
            var o = parent.AsObject();
            Assert.Single(o);
            Assert.True(o.ContainsKey("bone") ^ o.ContainsKey("optBone"));
        }
    }

    private static void Animated(JsonObject row, string vecName)
    {
        Assert.True(row.ContainsKey("static") ^ row.ContainsKey("keys"), "exactly one of static/keys");
        var keys = row.ContainsKey("static") ? new[] { row["static"] } : row["keys"]!.AsArray().ToArray();
        foreach (var k in keys)
        {
            Assert.Equal(3, k!["pos"]!.AsArray().Count);
            Assert.Equal(4, k["quat"]!.AsArray().Count);
            if (k[vecName] is { } v) Assert.Equal(3, v.AsArray().Count);
        }
    }

    private static string RepoFile(string relative)
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null)
        {
            var candidate = Path.Combine(dir.FullName, relative);
            if (File.Exists(candidate))
                return candidate;
            dir = dir.Parent;
        }

        throw new FileNotFoundException($"{relative} not found above {AppContext.BaseDirectory}");
    }
}
