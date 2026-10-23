# The colossus census (S10 Task 2)

All sixteen colossi, from data alone: `python3 -I tools/re/census/census.py --check --out tools/re/census/census.json`
(`make census`; about 2 s). The tool is read-only on the game tree and the library; its output is
`tools/re/census/census.json`. This note holds identifiers, counts, sizes and hashes only, no game content.

Columns marked **name-derived** come from clip, material, token or bank-cue names and are **not verified
behaviour**: they are RE or audition items, to be confirmed by an RE probe or by listening before any
S11-style work leans on them. Everything else is measured (glb parse, `.anb` parse, catalog, zone file).

## 1 · The table (measured)

`stem` is the clip family, found by skeleton containment: every `OptBone`-free skeleton bone name of the
stem's `.skb` is a node of the live glb (coverage **1.0** for all 16; the closest rival is listed in the json).
`flag-set` counts `.anb` files the S2 decoder refuses (version bit 16 set); `joints` is the live glb's skin.
Every colossus converted `ok` (catalog `conv ok` is every source under its model dir).

| # | dir (library) | stem | joints | live glb MB | conv ok | dead body | weakpoint mats | skel cov | clips | flag-set | death clips |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | `colossus_01_minotaur_a` | `minotaur_a` | 238 | 38.5 | 7/7 | mound | 2 | 1.0 | 82 | 2 | 2 |
| 2 | `colossus_02_bull_a` | `boss_a` | 34 | 7.4 | 3/3 | body | 7 | 1.0 | 58 | 0 | 2 |
| 3 | `colossus_03_knight` | `knight_a` | 35 | 16.7 | 21/21 | body | 3 | 1.0 | 62 | 0 | 2 |
| 4 | `colossus_04_kirin` | `kirin_a` | 38 | 17.5 | 8/8 | mound | 3 | 1.0 | 77 | 0 | 2 |
| 5 | `colossus_05_eagle` | `bird_a` | 71 | 6.8 | 3/3 | body | 3 | 1.0 | 60 | 0 | 2 |
| 6 | `colossus_06_minotaur_b` | `minotaur_b` | 238 | 26.2 | 6/6 | body | 3 | 1.0 | 16 | 0 | 2 |
| 7 | `colossus_07_electric_serpent` | `eel_b` | 192 | 5.1 | 3/3 | body | 1 | 1.0 | 21 | 0 | 2 |
| 8 | `colossus_08_lizard` | `yamori_b` | 80 | 7.4 | 4/4 | body | 2 | 1.0 | 88 | 0 | 2 |
| 9 | `colossus_09_desert_tortoise` | `kame_a` | 35 | 8.9 | 4/4 | body | 2 | 1.0 | 83 | 0 | 2 |
| 10 | `colossus_10_sand_serpent` | `narga_a` | 25 | 3.6 | 5/5 | mound (stone) | 2 | 1.0 | 29 | 0 | 2 |
| 11 | `colossus_11_lion` | `leo_a` | 42 | 16.0 | 14/14 | body | 1 | 1.0 | 87 | 0 | 2 |
| 12 | `colossus_12_lake_turtle` | `poseidon_a` | 42 | 33.8 | 13/13 | body | 1 | 1.0 | 56 | 0 | 2 |
| 13 | `colossus_13_sky_serpent` | `snake_c` | 122 | 12.3 | 3/3 | body | 3 | 1.0 | 56 | 0 | 2 |
| 14 | `colossus_14_cerberus` | `cerberus_a` | 42 | 3.8 | 25/25 | body | 1 | 1.0 | 41 | 0 | 2 |
| 15 | `colossus_15_minotaur_c` | `minotaur_c` | 238 | 38.6 | 9/9 | body | 3 | 1.0 | 37 | 0 | 2 |
| 16 | `colossus_16_mega_maid` | `buddha_a` | 88 | 12.5 | 31/31 | body | 1 | 1.0 | 58 | 0 | 2 |

Notes. `minotaur_a/b/c` (#1, #6, #15) share one skeleton (238 joints; rival coverage 1.0 each way);
`leo_a` (#11) and `cerberus_a` (#14) are interchangeable (rival coverage 1.0); `eel_b`'s (#7) live glb
contains all of `narga_a`'s (#10) bones (1.0; the reverse is 0.43). Dead bodies: #1 has a dirtmound, #4 a mound, #10 a stone; the rest are a body only.
`weakpoint mats` counts distinct glb material names containing `weak` (name-derived: the material name is
how S4 found Valus's two). #16 has no `*_dead_body.cgpr` script (the other fifteen do).

## 2 · Clips (measured)

Track counts are per `.anb` (all clips of a stem normally share one count, i.e. one skeleton map). The last
column counts flag-clear clips whose track types fall outside the S2 decoder's set `{0,2,3,4,5,6,12,13,14}`:
**0 for all 16**. Types 12 and 14 appear in about 15 clips and remain unvalidated (the S2 note).

| # | stem | tracks per clip | root clips | objanim | death | idle | walk/run | attack | damage/down | refuse/shake | types outside S2 set |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | `minotaur_a` | 58x80 | 68 | 75 | 2 | 4 | 6 | 10 | 32 | 16 | 0 |
| 2 | `boss_a` | 23x58 | 48 | 48 | 2 | 5 | 6 | 8 | 23 | 7 | 0 |
| 3 | `knight_a` | 21x62 | 48 | 37 | 2 | 4 | 3 | 16 | 17 | 4 | 0 |
| 4 | `kirin_a` | 27x75, 4x2 | 50 | 44 | 2 | 4 | 9 | 16 | 23 | 13 | 0 |
| 5 | `bird_a` | 62x60 | 42 | 28 | 2 | 4 | 0 | 3 | 8 | 3 | 0 |
| 6 | `minotaur_b` | 58x15, 5x1 | 11 | 75 | 2 | 3 | 2 | 7 | 0 | 2 | 0 |
| 7 | `eel_b` | 49x21 | 3 | 11 | 2 | 3 | 0 | 0 | 4 | 0 | 0 |
| 8 | `yamori_b` | 65x88 | 75 | 55 | 2 | 4 | 12 | 10 | 30 | 1 | 0 |
| 9 | `kame_a` | 23x83 | 51 | 45 | 2 | 6 | 6 | 12 | 35 | 3 | 0 |
| 10 | `narga_a` | 20x21, 7x8 | 10 | 11 | 2 | 3 | 0 | 3 | 3 | 0 | 0 |
| 11 | `leo_a` | 32x87 | 73 | 39 | 2 | 4 | 33 | 6 | 1 | 1 | 0 |
| 12 | `poseidon_a` | 23x56 | 43 | 51 | 2 | 4 | 5 | 0 | 5 | 3 | 0 |
| 13 | `snake_c` | 112x56 | 10 | 17 | 2 | 3 | 0 | 0 | 11 | 0 | 0 |
| 14 | `cerberus_a` | 32x41 | 36 | 15 | 2 | 4 | 14 | 4 | 1 | 2 | 0 |
| 15 | `minotaur_c` | 58x37 | 25 | 75 | 2 | 3 | 0 | 22 | 1 | 0 | 0 |
| 16 | `buddha_a` | 76x58 | 42 | 24 | 2 | 13 | 0 | 5 | 10 | 10 | 0 |

## 3 · Arenas (measured)

Arena zones are `arena_NN` (`arena_12_south` and `_north` for #12). Every colossus has a converted level
dir (`arena glbs`), a level file set and one world cgpr. Start positions are the
`misc/playerstartpositions.txt` entries.

| # | zone(s) | inherits | areas | level files | arena glbs | cgpr | start (x,y,z) |
|---|---|---|---|---|---|---|---|
| 1 | arena_01, arena_01_entrance | base_world | 34 | 21 | 13 | 2 | (353.0, 29.0, 345.0) |
| 2 | arena_02, arena_02_fast | base_world | 39 | 104 | 67 | 2 | (273.0, -97.0, -842.0) |
| 3 | arena_03, arena_03_ring | base_world | 39 | 37 | 24 | 3 | (-301.0, 44.0, -1375.0) |
| 4 | arena_04 | base_world | 33 | 40 | 26 | 2 | (984.0, 52.5, 142.0) |
| 5 | arena_05 | hall_06_east | 33 | 39 | 25 | 3 | (1285.0, 6.5, -330.0) |
| 6 | arena_06 | base_world | 16 | 20 | 13 | 1 | (-971.0, -91.0, 815.0) |
| 7 | arena_07, arena_07_fast | base_world | 19 | 44 | 28 | 3 | (-767.0, 45.0, -2093.0) |
| 8 | arena_08 | level_11 | 21 | 23 | 16 | 1 | (916.0, -21.0, 950.0) |
| 9 | arena_09, arena_09_outer | base_world | 41 | 73 | 48 | 3 | (-844.0, 24.0, -774.0) |
| 10 | arena_10, arena_10_nt, arena_10_st | vista_02 | 40 | 11 | 7 | 2 | (-2145.0, -10.5, -306.0) |
| 11 | arena_11 | level_01_west | 30 | 44 | 29 | 3 | (403.0, -50.0, -2053.0) |
| 12 | arena_12_south, arena_12_north | base_world | 25 | 23 | 14 | 3 | (876.0, 12.5, -1475.0) |
| 13 | arena_13, arena_13_hall_08 | base_world | 34 | 36 | 23 | 3 | (-635.0, -51.0, 1209.0) |
| 14 | arena_14 | base_world | 16 | 55 | 36 | 2 | (-1481.0, 32.0, -1490.0) |
| 15 | arena_15 | base_world | 26 | 67 | 43 | 3 | (1045.0, 46.0, -2048.0) |
| 16 | arena_16 | base_world | 10 | 53 | 34 | 4 | (350.0, -11.0, 2057.0) |

## 4 · Mechanics (name-derived)

Clip-name keyword hits per stem (first two names per hint) and identifier-token counts in the arena's
decompressed cgpr (`water 233` means 233 identifiers containing the word). Hints only: a `climb struct`
hit is a clip called `hashigo`/`kune`/`ude`, not proof of a climbable structure.

| # | clip-name hints (sample) | arena-script tokens (count) |
|---|---|---|
| 1 | climb struct: add_ude_l_damage, add_ude_r_damage | - |
| 2 | - | climb 2, sand 16, underwater 4, water 87 |
| 3 | - | climb 2, fire 4, underwater 4, water 15 |
| 4 | climb struct: hashigo_l_base, hashigo_r_base | climb 2, fire 120, fly 57, underwater 2, water 36 |
| 5 | flying: add_flap_break_l_end, add_flap_break_l_loop; bow/arrow: add_arrow_damage; ride: atk_gride, gride_end | underwater 5, water 14 |
| 6 | - | bow 6, fire 2, torch 36, water 26 |
| 7 | climb struct: addp_kune_tate_end, addp_kune_tate_hi_loop | climb 1, underwater 6, water 15 |
| 8 | bow/arrow: add_arrow_damage_l, add_arrow_damage_r; breath/beam: atk_breath_end, atk_breath_ground_end; geyser/pulse: atk_pulse_a, atk_pulse_b; climb struct: wall_fall_downward_start, wall_fall_downward_start_low | bow 62, cloud 1, fire 63, torch 405 |
| 9 | geyser/pulse: add_arm_l_geyser, add_arm_r_geyser | cloud 4, geyser 25, steam 4, water 233 |
| 10 | climb struct: addp_kune_atk, addp_kune_atk_end | fire 2, sand 4 |
| 11 | jump/dash: add_dash_hit, jump | fire 16, sand 59, torch 35, underwater 2, water 6 |
| 12 | swim/under: add_dai_nozoki_end, add_dai_nozoki_loop; geyser/pulse: dai_nori_hi_pulse_5hit_a, dai_nori_hi_pulse_5hit_b; ride: dai_nori_hi_dmg_end, dai_nori_hi_end; jump/dash: dash_a, dash_b | climb 4, cloud 3, underwater 15, water 273 |
| 13 | flying: add_hane_dmg_head, add_hane_dmg_mune; swim/under: add_hane_under_loop, add_hane_under_start; climb struct: addp_kune_down_end, addp_kune_down_loop | climb 2, cloud 1, sand 73, storm 4, underwater 1, water 2 |
| 14 | climb struct: tower_end, tower_loop; jump/dash: add_dash_hit, jump2 | bow 1, fire 7, torch 4, underwater 2, water 63 |
| 15 | climb struct: add_atk_wall_high_end, add_atk_wall_high_loop | cloud 10, water 28 |
| 16 | bow/arrow: add_atk_shot_charge, add_atk_shot_fire; breath/beam: add_atk_shot_charge, add_atk_shot_fire | bow 16, climb 1, cloud 2, fire 16, lightning 59, storm 21, torch 56, water 124 |

### Ranking by how far each is from Valus (judgement from the numbers above; name-derived)

1. **#6 `minotaur_b`, #15 `minotaur_c`**: same skeleton and rig as Valus, no mechanic hints, three weakpoint
   materials each. Cheapest; #6 has only 16 clips (a thin set), #15 has 37 with 22 attack-named clips.
2. **#2 `boss_a` (Quadratus), #3 `knight_a`, #4 `kirin_a`, #11 `leo_a`, #14 `cerberus_a`**: ground bodies on a
   new skeleton (34-42 joints), no flight, swim or ranged hints in the clips. #2 has seven weakpoint materials
   (four named `hoof_weakpoint`), 58 clips, and no fire or torch tokens in its arena script; #11 and #14 share a rig, so one port covers both. #4's arena script is flight- and fire-heavy.
3. **#9 `kame_a`, #12 `poseidon_a`, #8 `yamori_b`, #16 `buddha_a`**: ground-based but with a second system
   (geyser or pulse attacks, riding or swimming clips, bow and breath clips, the lightning-storm arena of #16).
4. **#5 `bird_a`, #13 `snake_c`, #7 `eel_b`, #10 `narga_a`**: flight, swim or serpentine bodies (71, 122, 192
   and 25 joints) whose climb surface is not a vertical band set.

## 5 · Audio banks by cue stem (name-derived)

`bank` is `sound/shadow/bossNN.sxd`; stems are the leading `_`-separated word of the cue names found in the
first 128 KiB of the bank (names scanned = matches). The bank number is **not** the colossus number; the
stems say which colossus the cues belong to: `mammo`/`mavo` #2, `bird`/`bdvo` #5, `kame`/`kmvo` #9,
`snake`/`sncvo` #13, `knight`/`ktvo` #3, `yamori`/`ymbvo` #8, `eel`/`eevo` #7, `leo`/`levo` #11, `pose`/`psvo` #12,
`cerb`/`cbvo` #14, `kirin`/`kivo` #4, `narga`/`navo` #10, `evis`/`evvo` #16. Two further banks carry
`minotaur`/`mivo` stems (`boss15`, `boss22`) and `boss23` leads with `par` (unidentified); the three minotaur
colossi (#1, #6, #15) cannot be assigned to banks from names alone.

| bank | names scanned | leading cue stems (count) |
|---|---|---|
| `boss01` | 342 | `mammo` 122, `boss01` 71, `boss` 50, `mavo` 41, `boss03` 22, `mid` 9 |
| `boss02` | 244 | `bird` 96, `boss02` 60, `boss` 40, `bdvo` 34, `boss03` 5, `boss06` 5 |
| `boss03` | 518 | `kame` 179, `boss09` 170, `kmvo` 59, `boss` 48, `boss15` 29, `boss03` 20 |
| `boss04` | 192 | `boss13` 74, `snake` 60, `sncvo` 29, `boss` 18, `boss03` 5, `boss02` 4 |
| `boss05` | 341 | `knight` 124, `boss05` 68, `boss03` 53, `boss` 41, `ktvo` 37, `eight` 10 |
| `boss07` | 469 | `yamori` 190, `boss07` 147, `ymbvo` 58, `boss` 42, `boss03` 24, `boss08` 4 |
| `boss08` | 166 | `boss07` 78, `eel` 58, `eevo` 26, `boss` 4 |
| `boss11` | 527 | `leo` 257, `boss11` 167, `levo` 58, `boss` 22, `boss03` 12, `low` 7 |
| `boss12` | 385 | `boss12` 130, `pose` 130, `psvo` 55, `boss` 45, `boss03` 12, `low` 7 |
| `boss13` | 505 | `cerb` 209, `boss14` 190, `cbvo` 61, `boss` 22, `boss03` 12, `low` 7 |
| `boss15` | 385 | `boss01` 105, `minotaur` 85, `boss15` 73, `mivo` 35, `boss` 17, `minoboot` 16 |
| `boss17` | 313 | `boss17` 80, `kirin` 79, `boss` 42, `kivo` 36, `boss03` 21, `kist` 20 |
| `boss20` | 218 | `boss10` 85, `narga` 54, `boss` 33, `navo` 31, `eight` 7, `boss03` 5 |
| `boss22` | 306 | `boss22` 86, `minotaur` 58, `boss` 51, `mivo` 35, `boss06` 23, `boss03` 19 |
| `boss23` | 281 | `boss15` 108, `boss` 40, `boss03` 27, `par` 23, `boss06` 14, `l` 8 |
| `boss24` | 377 | `evis` 135, `boss16` 115, `evvo` 74, `boss` 25, `boss03` 18, `eight` 7 |

### The Valus `boss01` / `boss15` conflict (recorded, not corrected; design s13.14)

S5's frozen `tools/re/audio/s5_map.json` takes Valus's cues from bank `boss01` (`boss01_vo_atk1`,
`boss01_atk_hit_ground1`, ...). By cue stem `boss01` is the `mammo_`/`mavo_` bank (Quadratus, #2), while the
`minotaur_`/`mivo_`/`minoboot_` stems sit in `boss15` (and `boss22`). The `boss01_*` cue-name prefix itself
occurs in both: 71 names in `boss01` and 105 distinct in `boss15`. So either the prefix is a shared
generic-boss set and `boss01` is correct, or Valus's real bank is `boss15`; names alone cannot decide.
`s5_map.json` is frozen. A correction (an audition or a Ghidra chain walk of `boss15`) is a separate
decision for the user.

## 6 · Shipped-hash check and the clobbered files

`logs/game.sha256` lists the shipped sha256 per file. The census checks only the colossus model dirs
(`models/shadow/characters/colossus_*`, live and `_deadbody`) and `cp3shadow/anim` (the whole tree is 130k
files): **5505 files checked, 4 mismatched, 0 missing**. The four mismatches are the known Valus clobber (S6): each is a
JSON dump in place of the original `.anb` (all four now hash identically, to the same actual digest).
The library's baked copy is fine, but a re-bake of Valus's death clips or camera from the game tree would
fail; restoring them from `uroot/pak3.psarc` is the user's call (the tree is read-only to the project).
The census reports them without failing.

| path (under the game tree root) | expected sha256 (`logs/game.sha256`) | actual |
|---|---|---|
| `game/cp3shadow/anim/camera_minoa_death_front_camera_01.anb` | `e15eecc7375751ecc2ece8998dbacba8a29599468b09939d19402bfeb71f3a51` | `a4d9c3b0d54feb7ecc48a56b26b5df9dfaead2125dc575c7949412788c40f75b` |
| `game/cp3shadow/anim/minotaur_a_death_front.anb` | `0db1d990ba55687d6539ce61b39274684c29c0d281a6732cb7392cb07edf6c2b` | `a4d9c3b0d54feb7ecc48a56b26b5df9dfaead2125dc575c7949412788c40f75b` |
| `game/cp3shadow/anim/minotaur_a_death_front_loop.anb` | `a7639824b3e004a6207113d358072914df2a26706bf09fc609a5b56b3fb87a3f` | `a4d9c3b0d54feb7ecc48a56b26b5df9dfaead2125dc575c7949412788c40f75b` |
| `game/cp3shadow/anim/objanim_minotaur_a_death_front.anb` | `f5daae1a9450a82bdc1aa33b6d6e7ef6ee607a49b5729bd43f127e7007644351` | `a4d9c3b0d54feb7ecc48a56b26b5df9dfaead2125dc575c7949412788c40f75b` |

## 7 · Check

`census.py --check` exits 1 unless there are 16 rows, every stem's skeleton coverage is 1.0, and every
arena has a zone, a level dir, a cgpr and a converted glb. Hash mismatches never fail it. Result at
the pinned commit: exit 0, 16 rows.
