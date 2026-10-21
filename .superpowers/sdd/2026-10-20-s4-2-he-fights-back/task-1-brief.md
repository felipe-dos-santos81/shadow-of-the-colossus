### Task 1: Converter — bake the fight clips and extend the clip record

**Files:**
- Modify: `src/BpConvert.Core/Pipeline/ClipManifest.cs` (add `rootMotion`, add `Delete`)
- Modify: `src/BpConvert.Core/Pipeline/ModelConverter.cs` (delete a stale sidecar when no clips)
- Test: `tests/BpConvert.Tests/ClipManifestTests.cs`
- Artifact (gitignored): re-converted `out/library/.../wander.glb` (15 clips), `.../c01_minotaur_a.glb` (8 clips)

**Interfaces:**
- `ClipManifest.Write(string glbPath, string sourceRelPath, IReadOnlyList<AnimClip> clips)` keeps its shape; each entry gains `"rootMotion": "ground-height" | "body-offset" | "unclassified"` from a name-keyed table in `ClipMotion.cs`.
- `ClipManifest.Delete(string glbPath)` removes `<stem>.clips.json`; `File.Delete` is a no-op when absent.

- [ ] **Step 1: Write the failing tests**

```csharp
[Fact]
public void Write_records_root_motion_and_deletes_a_stale_record()
{
    var dir = Directory.CreateTempSubdirectory("bp-clips-");
    var glb = Path.Combine(dir.FullName, "wander.glb");
    var clips = new[] { new AnimClip { Name = "player_a_damage_h_fly1", Frames = 9, Rate = 0.5, Tracks = [], Root = [] } };
    var path = ClipManifest.Write(glb, "models/.../wander.cmsh", clips);
    using var doc = JsonDocument.Parse(File.ReadAllText(path));
    Assert.Equal("body-offset", doc.RootElement.GetProperty("clips")[0].GetProperty("rootMotion").GetString());

    ClipManifest.Delete(glb);
    Assert.False(File.Exists(path));
}
```

- [ ] **Step 2: Run it red** — `dotnet test tests/BpConvert.Tests -v q --filter "FullyQualifiedName~ClipManifestTests"`; expect failure on `rootMotion`.

- [ ] **Step 3: Implement `ClipMotion` + `Delete` + the `else` branch in `ModelConverter`**

`ClipMotion.For(name)` returns `ground-height` for the locomotion and grip/fall clips, `body-offset` for the damage/attack clips, `unclassified` otherwise. `ModelConverter` calls `ClipManifest.Delete` when `animations.Count == 0`.

- [ ] **Step 4: Re-convert both models**

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
dotnet publish src/BpConvert.Cli -c Release -o out/bin
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models/shadow/characters/wander/_cmn/wander.cmsh \
  --clips player_a_stand,player_a_walk,player_a_run,player_a_grip_stand_crouch,player_a_grip_loop,player_a_grip_move_f1,player_a_grip_move_f2,player_a_grip_float_u_hi_attack,player_a_atk1,player_a_damage_m_fly2,player_a_fall_damage_to_death,player_a_damage_h_fly1,player_a_damage_h_fly_end,player_a_fall_damage_start,player_a_fall_damage_end --force
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.cmsh \
  --clips minotaur_a_stand,minotaur_a_add_head_damage,minotaur_a_add_ude_r_damage,minotaur_a_add_mune_r_damage,minotaur_a_death_front,minotaur_a_atk_leg_l,minotaur_a_atk_leg_r,minotaur_a_atk_weapon2_a --force
```

Expected: two `ok` lines; sidecars show 15 and 8 clips. Record the sha256s.

- [ ] **Step 5: Update the clip-count pins** — `app/tests/test_asset_io.cpp` 11→15 and 5→8; add `clip_index_named` checks for `player_a_damage_h_fly1` and `minotaur_a_atk_leg_l`.

- [ ] **Step 6: Run both suites and commit**

`dotnet test tests/BpConvert.Tests -v q`; `SOTC_LIB=$(pwd)/out/library ctest --test-dir app/build --output-on-failure`.
Commit: `feat: bake S4.2 fight clips and extend the clip record`.

---
