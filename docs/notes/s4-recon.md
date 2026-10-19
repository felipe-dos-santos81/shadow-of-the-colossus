# S4 recon (2026-10-19) — Valus, placement, app readiness, RE state

Recon for the S4.0 "The encounter" design (`docs/superpowers/specs/2026-10-19-s4-encounter-design.md`).
Four read-only passes: assets, placement, app readiness, RE/decode state. Facts are cited; inferences
are marked. No game/port runs.

## 1 · Valus asset set

- `models/shadow/characters/colossus_01_minotaur_a/_cmn/`:
  `c01_minotaur_a.cmsh` 6,736,137 B; `.cskl` 9,402 B (Oodle `8c0a`); `.cmdl` 1,684 B;
  `cclm` 73,222 B; `_collision.cmsh` 54,412 B; `_lod1.cmsh` 290,073 B; club `_weapon.cmsh`
  265,730 B and knee `_hiza.cmsh` 37,834 B (both unskinned).
- Converted: `out/library/.../c01_minotaur_a.glb` **38,049,000 B**, 12 meshes, 12 materials,
  250 nodes, **1 skin / 238 joints**, 523,865 vertices, 0 animations; bind AABB x[−17.481, 17.481],
  **y[−8.739, 8.932]**, z[−4.439, 5.367] → 34.96 × 17.67 × 9.81 m, feet at local −8.739.
  LOD1 glb 1,763,640 B; collision glb 624,940 B (229 meshes, 5,329 verts, 238 joints);
  club glb 613,576 B and knee glb 78,212 B (no skin).
  Evidence: `bp-convert inspect` (`meshes=12 vertices=476666`) + glb JSON parse + measured bounds.
- Materials/textures: 15 `.cmat` under `materials/shadow/characters/colossus_01/_ps4/`; the live glb
  uses 12 (legs, arms_new2, banner, waist, weakpoint_03/04, eyes2, back, trigger_02,
  dynamicringh2, chest4, face). 36 converted PNGs / 126 MB.
- Fur: textures + `colossus_01_minotaur_a.cgpr` shader strings (`fur_col.ctxr`, `flowmap_nm`,
  shell modifier); **no fur mesh, no converted fur PNGs** → not emitted.
- Cloth: `cp3shadow/cloth/minotaur_a.cls` 13,592 B; `cclm` references a collision cmsh. The banner
  mesh (12,940 verts) is weighted to 38 banner joints, but the 58-track body clips animate none of
  them; the glb writer emits no `alphaMode`/`doubleSided` (`docs/notes/m0-results.md:344`).

## 2 · Valus animation

- `cp3shadow/anim/minotaur_a_*.anb`: **82 files** (3,420–24,732 B). All **version 12, flag-clear**
  (bit 16 clear); corpus rule `docs/notes/re2-flagset.md:34-36`; family coverage
  `docs/notes/anb-playback-recon.md:80-83`.
- 58 tracks each; types `{0x00,0x03,0x04,0x05,0x06,0x0D}` — all inside `AnbReader`'s set. 69/82
  carry a root position series; rate 0.5 (≈15 fps) for 66, 0.502–0.545 for 15, 1.0 for
  `minotaur_a_limit_pose`.
- `minotaur_a_stand`: 26 frames, 58 tracks (type histogram 0x00=21, 0x03=7, 0x04=7, 0x05=7,
  0x06=3, 0x0D=13); `minotaur_a_standby`/`_boot` are 1-frame poses. `check-curves` 16/16 on
  `stand` and `walk1`.
- `minotaur_a.skb`: 6,972 B, **73 names = 58 record-bearing + 15 `OptBone_*`**; all 73 present in
  the 238-joint glb; **58/58 track mapping, 0 unresolved** (read-only emulation of
  `TrackMap`/`ClipSet`). `ClipSet.FindSiblingBank` resolves `minotaur_a_*` → `minotaur_a.skb`.
- `c01_minotaur_a.cskl` is Oodle; plaintext `.cani` siblings report **238 joints**, matching the
  glb. The `.skb` is a 73-name part bank, not the full skeleton.
- Clip roles (name-inferred): `stand` (idle loop), `walk1/2`, `atk_weapon2_*`, `atk_leg_l/r`,
  `nozoki_atk`, `refuse_*`, `down_*`, `death_front`. **No roar-named clip exists**; `boot` is a
  1-frame pose.
- 42 `objanim_minotaur_a_*.anb` are **flag-set**, no sibling `.skb`, tracks are event/cue names
  (`MINO_FOOT`, `MINO_A_WEAPON_ATK`, …) — an object/event layer, not skeletal animation.
- **No state→clip table** was found (`cp3shadowdata.calt` is a path index;
  `docs/notes/anb-playback-recon.md` G7). **No minotaur pose validation exists** (S2's xval is
  player-only), and **no Valus capture** exists (C2 captured the Shrine).

## 3 · Placement evidence

- **Player start (data-derived):** `misc/playerstartpositions.txt:1` —
  `+$/scripts/shadow/world/arena_01/arena_01.sgpr  353 29 345`; entrance line 2 `304 3.5 277`.
  Ground ray-cast: (353,345) → **27.951 m**; (304,277) → 2.448 (≈1 m below the file's y — a capsule
  offset). The file's y is not the feet height.
- **No colossus spawn record** in the stage families: `.fs` files are `u32 n; n×{u32 v, u32 hash32}`
  with no strings (`docs/notes/re-stage.md` §1.3); `.cwks` carries asset dependencies, not spawns;
  `.sgpr` is not shipped as a file.
- `arena_01.cgpr` (35,370 B) references `$/scripts/shadow/entities/colossi/****/.cgpr`,
  `North_MinoA_SensorDM`, `Relay`, `death_tentacle_loader`. `arena_01_shared.cgpr` (10,775 B)
  references `minotaur_a_body.cgpr`, `Mino_walking_damage_A`, `st17a…camera`.
- `eboot.bin` has `MinotaurARelay`, `CCPLdrShadowEventsMinotaurARelay`, `MinA_Spawn` (and B/C),
  `PlayerSpawnLocator` — event/class names, not transforms.
- `cp3shadow/path/pathdata/minotaur.path` (800 B): 6 nodes, bbox x[−81.6, 51.5] y[33,35]
  z[0, 87.4]; the coordinate frame is unresolved (`docs/notes/re-stage.md` §4.3). Not usable
  without an anchor.
- `camdata` CSVs carry `Camera, Frame, Focal Distance, Parallax, FOVAdjust, NearPlaneAdj,
  CharMotionBlur` — parameters only, no transform. Arena camera clips exist
  (`camera_arena_event_1/2`, `camera_st17_mino_start_camera`), but camera 36/37 is undecoded and the
  consumer is open.

## 4 · App readiness

- `Scene` = one flat mesh list + **one** `SkinCpu` + one `animations` vector
  (`app/src/scene.h:82-89`); each mesh carries a baked world `Mat4 model` and no rig index.
- `compose_scene` flattens manifest instances and appends the player last; `playerMeshBegin` is the
  only grouping (`app/src/asset_io.cpp:592-624`).
- A skinned glb as an instance is loaded with `includeRig=false` (skeleton/clips skipped), but
  `JOINTS_0`/`WEIGHTS_0` are still copied (`app/src/asset_io.cpp:465-489,548-559`), and the renderer
  binds **one** palette (`app/src/renderer.cpp:586-590`; shader `app/shaders/mesh.metal:50-73`) —
  Valus would deform with Wander's palette.
- One `Playback`, one `update_pose`, one `upload_pose` per frame (`app/src/app.cpp:173-183`;
  `app/src/renderer.cpp:473-526`). `apply_player_transform` overwrites only the player's tail range
  (`app/src/app.cpp:235-241`).
- Camera: follow target `playerPos + (0, 1.4, 0)`, distance clamp 2–12 m, pitch ±60°
  (`app/src/camera.cpp:16-21,119-161`); near/far are relative to distance (1:5000), so a 20 m model
  is fine geometrically; the clamps are the framing break. `camera_frame` (orbit fit) is
  single-model only.
- Heightmap: 1 m cells, max-Y per column, built from every instance mesh excluding the player
  (`app/src/app.cpp:222,438-446`). A rigged entity must be excluded.
- Collision: none beyond ground-follow + a 0.5 m edge clamp (`app/src/player.cpp:46-108`); the hook
  is after `player_update` / before `apply_player_transform` (`app/src/app.cpp:531-532,571-572,685-686`).
- Input: keyboard/mouse only (`SDL_INIT_VIDEO`); no gamepad, no SDL_ttf, no on-screen HUD; fps in
  the window title. Flags: `--selftest`, `--s3-selftest`, `--screenshot`/`--frames`,
  `--anim`/`--anim-time`.
- S3 acceptance: `scripts/s3-acceptance.sh` asserts 30 instances and a player-region wave guard; an
  animated Valus in the S3 manifest would break it → a separate encounter manifest/script.

## 5 · RE/decode and tooling state

- Flag-clear decode covers `player_*` 967/967, `horse_*` 270/270, `minotaur_a_*` 82/82
  (`docs/notes/anb-playback-recon.md:80-83`); flag-set `caseD_9` is partial (40/1,461 files), camera
  36/37 undecoded (`docs/notes/re2-flagset.md:191-210,293-295`).
- RE-4 (behavior/logic) has **no decoded note**; `*.cgpr` has a generic structure parser in the
  vendored library but no SOTC semantics; movement/camera constants are still the app's hand-tuned
  values; colossus behavior/AI is entirely open.
- RE-5 (collision/climb) is conditional; meshes-first uses the already-converted `*_collision.cmsh`
  (`docs/superpowers/specs/2026-10-16-sdl-rebuild-design.md:115`). `.cclm`/`.cpid`/`.clb` have no
  decoded format.
- Tooling: Ghidra 12.1.4 headless + the analyzed project (`out/re/ghidra`, 38,845 functions) works;
  Ghidra-MCP blocked by an unrelated instance on port 8089; REA/Hopper is shallow and blind in the
  sampler region. Static only.

## 6 · Implications for S4.0

- The model, rig, clip, mapping, and collision mesh are ready; the only converter work is the bake.
- The app work is the milestone's center of gravity: per-rig scene records, palettes, playback, a
  manifest entity directive, an entity-aware heightmap, a minimal capsule collision, and a framing
  camera mode.
- Placement is the only genuine unknown. The honest split is: bounded pull (data) or labeled
  builder pick (fallback); both are shippable, and the user's walk judges the spot.
- Visual gaps to label up front: no club, no knee, no shell fur, opaque banner, no encounter camera.

*Personal-use fan project. No game assets are distributed from this repository.*
