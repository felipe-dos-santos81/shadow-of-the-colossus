# Task 1 report — Converter: bake the fight clips and extend the clip record

**Status:** DONE
**Commit:** `9611db0` — `feat: bake S4.2 fight clips and extend the clip record`
**Branch:** `s4-2-he-fights-back` (base `a91facc`)

## What I implemented

1. **`src/BpConvert.Core/Pipeline/ClipMotion.cs` (new, ruling R3).**
   `ClipMotion.For(string name)` returns the provenance class:
   - `ground-height`: `player_a_stand`, `player_a_walk`, `player_a_run`,
     `player_a_fall_damage_start`, `player_a_fall_damage_end`,
     `player_a_fall_damage_to_death`, and every `player_a_grip_*` (ordinal prefix).
   - `body-offset`: `player_a_atk1`, every `player_a_damage_*` (ordinal prefix), every
     `minotaur_a_*` (ordinal prefix).
   - `unclassified`: anything else (includes `horse_a_add_horse_kick`).
   Public constants `GroundHeight`, `BodyOffset`, `Unclassified`. `ArgumentException` on a
   null/blank name.
   The doc comment records the design §11.4 fact that this is **record-only** — the app keeps
   its existing lift rule and does not read the sidecar.

2. **`ClipManifest.cs`.** `Write` keeps its signature; each `ClipEntry` gains a final
   `string RootMotion` member (serialized camelCase as `"rootMotion"`). `Delete(string glbPath)`
   removes `<stem>.clips.json` via `File.Delete` (no-op when absent) and validates its argument.
   Class summary/remarks updated to mention the ground relationship and the stale-delete.

3. **`ModelConverter.cs`.** After `GlbWriter.Write`, the existing `if (animations.Count > 0)`
   is followed by `else ClipManifest.Delete(written.GlbPath);`. The delete sits after the glb
   write, so a conversion that throws leaves the previous glb + sidecar pair untouched.

4. **Tests (`ClipManifestTests.cs`).**
   - Brief's `Write_records_root_motion_and_deletes_a_stale_record`, verbatim.
   - `For_classifies_every_baked_clip` — 24-row theory pinning every one of the 15 Wander and
     8 Valus baked clip names plus one `unclassified` control.
   - `Convert_WithoutClips_DeletesAStaleClipRecord` — wiring test: bake wander with
     `player_a_stand`, assert the sidecar exists, re-convert the same model with no clips into
     the same library, assert the glb exists and the sidecar is gone.

5. **App pins.**
   - `app/tests/test_asset_io.cpp`: 11→15 (wander skeleton test), 5→8 (entity test), the two
     playback-size pins 11→15 / 5→8, `clip_index_named` checks for
     `player_a_damage_h_fly1` (player playback) and `minotaur_a_atk_leg_l` (plus
     `_atk_leg_r`, `_atk_weapon2_a`) on the entity playback; comments updated to S4.2.
   - `app/tests/test_anim.cpp`: the same library pin `rig.animations.size() == 11` → `15`
     (see Concerns).

## RED evidence

First red (API absent — compile-level):

```
$ export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
$ dotnet test tests/BpConvert.Tests -v q --filter "FullyQualifiedName~ClipManifestTests"
ClipManifestTests.cs(49,22): error CS0117: "ClipManifest" não contém uma definição para "Delete"
ClipManifestTests.cs(82,32): error CS0103: O nome "ClipMotion" não existe no contexto atual
```

To get the behavioural red the brief asks for, I added placeholder API surfaces (JSON field
present, `Delete` no-op, `For` → `unclassified`) and re-ran:

```
$ dotnet test tests/BpConvert.Tests -v q --filter "FullyQualifiedName~ClipManifestTests"
Com falha! – Com falha: 25, Aprovado: 4, Ignorado: 0, Total: 29

$ dotnet test tests/BpConvert.Tests --filter "...Write_records_root_motion..."
Assert.Equal() Failure: Strings differ
Expected: "body-offset"
Actual:   "unclassified"
  at ClipManifestTests.Write_records_root_motion_and_deletes_a_stale_record() ... line 47
```

(25 = 23 failing theory rows + the brief's fact + the stale-delete fact; the 4 passes are the
3 pre-existing tests and the `unclassified` theory row.)

## GREEN evidence

```
$ dotnet test tests/BpConvert.Tests -v q --filter "FullyQualifiedName~ClipManifestTests"
Aprovado! – Com falha: 0, Aprovado: 29, Ignorado: 0, Total: 29

$ dotnet test tests/BpConvert.Tests -v q
Aprovado! – Com falha: 0, Aprovado: 119, Ignorado: 0, Total: 119

$ SOTC_LIB="$(pwd)/out/library" ctest --test-dir app/build --output-on-failure
1/2 Test #1: unit_tests .......................   Passed   11.30 sec
2/2 Test #2: windowed_selftest ................   Passed    0.23 sec
100% tests passed out of 2
```

119 = the 93 pre-task tests + the 26 new ones (24 theory rows + 2 facts).

## Re-conversion (Step 4)

`dotnet publish src/BpConvert.Cli -c Release -o out/bin` succeeded (no warnings), then the two
brief-verbatim commands:

```
ok    models/shadow/characters/wander/_cmn/wander.cmsh -> .../wander.glb (13924 ms)
in scope: 1  skipped: 0  converted: 1  failed: 0  pending: 0
ok    models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.cmsh -> .../c01_minotaur_a.glb (15719 ms)
in scope: 1  skipped: 0  converted: 1  failed: 0  pending: 0
```

Sidecars: Wander **15** clips, Valus **8** clips. Independent glb-check (parsed the glb JSON
chunk) confirmed the glb animation name lists equal the sidecar name lists, in order.

### sha256

| Artifact | sha256 | clips |
|---|---|---|
| `out/library/models/shadow/characters/wander/_cmn/wander.glb` | `e945e0c1d0ee5f84094ebb7d15ae85e70803f5186790c99de286d54fc6596d7c` | 15 |
| `out/library/models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb` | `7db8fbaaac1b96762c239fd00af311992a6951e8ff690a3f7097f0e792f0678a` | 8 |
| `out/library/models/shadow/characters/wander/_cmn/wander.clips.json` | `796345fc78df3ba4f541efe8a8c31298f26684118f15fc56174e67f197986c93` | 15 |
| `out/library/models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.clips.json` | `8132e6aecf81f0c0abcd677f39e3990c9bd87a112019c58ece122cd12ab61041` | 8 |

Pre-conversion glb hashes (for the record): wander
`d2c8093d1f8223ad57f0421971e6fffbf0e9b3e521070801cf89e2c21c53f540`, Valus
`98b6f7125a8de80dccac3cdd16f4bb8dd1d56dddc2e08037a46f381da55ead86`.

Baked `rootMotion` values (from the sidecars):

- Wander: stand/walk/run, 4 × grip, fall_damage_to_death, fall_damage_start, fall_damage_end →
  `ground-height`; `player_a_atk1`, `player_a_damage_m_fly2`, `player_a_damage_h_fly1`,
  `player_a_damage_h_fly_end` → `body-offset`.
- Valus: all 8 (stand, 3 × add_*_damage, death_front, atk_leg_l, atk_leg_r, atk_weapon2_a) →
  `body-offset`.

## Files changed (committed)

- `src/BpConvert.Core/Pipeline/ClipMotion.cs` (new)
- `src/BpConvert.Core/Pipeline/ClipManifest.cs`
- `src/BpConvert.Core/Pipeline/ModelConverter.cs`
- `tests/BpConvert.Tests/ClipManifestTests.cs`
- `app/tests/test_asset_io.cpp`
- `app/tests/test_anim.cpp`

Not committed: `out/library/**` (gitignored `out` symlink), `.superpowers/**` (gitignored).

## Self-review findings

- **rootMotion coverage:** all 23 baked clip names are pinned by the theory, and both live
  sidecars show a non-`unclassified` value for every entry. The `unclassified` control
  (`horse_a_add_horse_kick`) passes, so the fallback is not dead code.
- **Stale-delete path:** covered twice — directly (`ClipManifest.Delete` removes the file; the
  brief's fact) and through the converter (`Convert_WithoutClips_DeletesAStaleClipRecord`).
  `File.Delete` no-op semantics mean `Convert_WithoutClips_WritesNoClipRecord` (the M0 test,
  still green) is unaffected.
- **Sidecar vs glb:** verified a third way, outside the app loader, by comparing the glb JSON
  `animations[].name` list with the sidecar `clips[].name` list — equal and same order, 15/15
  and 8/8.
- **No assets committed:** `git show --stat HEAD` lists only source/test files; the worktree
  conversion lives behind the gitignored `out` symlink.
- **No unnecessary changes:** `ClipManifest.Write`'s signature, `Version = 1`, and the existing
  JSON keys/order are unchanged; `rootMotion` is appended last.
- **Delete ordering:** the `else` branch is after `GlbWriter.Write`, as required, so a failed
  conversion cannot orphan the previous record.

## Concerns

1. **`app/tests/test_anim.cpp` also pinned 11.** The task text named only
   `test_asset_io.cpp`, but `test_anim.cpp:765` (`wander's baked clips carry the root series…`)
   pins the same library glb's animation count. It would have failed after the re-conversion, so
   I updated 11→15 there too. This is a necessary consequence of the clip-count change, not a
   new assertion.
2. **The table is exact-name for locomotion/fall clips and prefix for the three families.**
   A future clip such as `player_a_run_*` would land in `unclassified` rather than silently
   inheriting `ground-height`; that is the intended honest fallback, but a new clip family will
   need one table line (and one theory row) added deliberately.
3. **`Delete` now runs for every clip-free conversion**, including M0 batch re-runs. That is
   the requested behavior (a re-converted clipped model loses its stale record), and
   `File.Delete` on an absent path costs a syscall — noted only so a later batch-throughput
   review does not surprise-diagnose it.
