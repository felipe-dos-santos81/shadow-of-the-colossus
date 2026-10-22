# S6 — the death bundle, the corpse, and the black tendrils

Status: **the asset/death gate passes** (2026-10-22, Task 0). The five death-bundle clips are
accounted for, the baked death motion (94 keys) and the corpse (6 meshes / 6 materials / 216,272
verts) are in the library, the three `.cprt` emitters are parsed with their `.cptx` and `.csdr`
references, and the three tendril atlases plus the veins texture decode to valid PNGs. The gate is
green: `S6 ASSET GATE PASSED`, exit 0.

What this note does **not** claim: the type-36 camera clips are undecoded and unused
(D2), and the tendril motion is a **labeled reconstruction** (D1) — the original textures, emitter
names/roles and the binary's 201 × 0.25 m segment geometry, with authored curl/turbulence, widths,
timings and camera. See §7.

Tool: `tools/re/s6/s6_asset_gate.py` (read-only on the game tree and the library; nothing written).
The conversions use the new CLI verb `bp-convert texture --in <texture> --out <png>`
(`src/BpConvert.Cli/Program.cs`), which calls `CtxrConverter.Convert`.

```bash
export SOTC_GAME_ROOT=…/SOTC-extract/extracted/game
export SOTC_LIB=<worktree>/out/library
python3 tools/re/s6/s6_asset_gate.py --root "$SOTC_GAME_ROOT" --out "$SOTC_LIB" --check
# -> checked 13 required asset(s): 13 ok, 0 failed
# -> source warnings: 4 (not required assets — the known game-tree clobber; see §2.1)
# -> S6 ASSET GATE PASSED (4 source warning(s))        (exit 0)

# one atlas (the other two are the same command with their paths)
caffeinate -dimsu dotnet exec out/bin/bp-convert.dll texture \
  --in "$SOTC_GAME_ROOT/textures/vfx/common/smoke/_ps4/8x8_wispysmoke02_1024_white_pfx.cptx" \
  --out "$SOTC_LIB/textures/vfx/common/smoke/_ps4/8x8_wispysmoke02_1024_white_pfx.png"
# -> texture: …cptx -> …png (1024x1024, variant 8c0a)
```

---

## 1. The gate

`--check` walks five groups and prints one line per asset:

| group | what is asserted | hard? |
|---|---|---|
| `clips (source)` | the five `.anb` paths exist; the shipped bytes are the shipped bytes (size + SHA-256) | missing = **fail**; the exact known clobber (246,449 B, SHA-256 `a4d9c3b0…`) = **source warning** (§2.1); anything else = **fail** |
| `clips (library)` | `c01_minotaur_a.clips.json` has `minotaur_a_death_front` frames 94 / fps 15.08056640625 / tracks 58; `c01_minotaur_a.glb` has the animation named `minotaur_a_death_front` with 94 keys and 6.1669 s | **fail** |
| `corpse` | `c01_minotaur_a_dead_body.glb` = 6 meshes, the six exact `colossus_01_{waist,back,face,arms,chest,legs}_dead` materials, 0 skins; `c01_minotaur_a_dead_dirtmound.glb` has ≥ 1 mesh | **fail** |
| `cprt` | `death_tentacle.cprt` = 2,376 B, header `{0,3,8,1,100}`, `TRAPL` footer; exactly three emitters `main_wisp`/`smoke`/`veins`, each with `emitter_<i>_{emit,spawn,update}.csdr` and its full `.cptx` reference; every referenced `.csdr`/`.cptx` exists in the game tree | **fail** |
| `textures` | the three converted atlas PNGs + `death_tentacle_veins.png` exist, carry the PNG signature, the pinned dimensions **and the pinned SHA-256** | **fail** |

Exit codes: `0` pass, `4` a required asset is missing/invalid (each is named on its line and in a
final list), `2` configuration/IO error (bad `--root`/`--out`). Without `--check` it prints the
inventory and exits 0.

**The red before the conversion** (the milestone's stop sign) was exit 4, three failures — exactly
the three missing atlas PNGs — with the rest already green. The line below is the final gate's
negative run, reproduced by moving the three PNGs aside and re-running (§1's exit-4 contract):

```
  FAIL texture  8x8_wispysmoke02_1024_white_pfx.png  missing: …/textures/vfx/common/smoke/_ps4/…
  FAIL texture  8x8_smoke_wispy_sharp_1024_pfx.png  missing: …/textures/vfx/common/smoke/_ps4/…
  FAIL texture  2x2_smoke_tendrils_01_pfx.png        missing: …/textures/vfx/common/smokeman/_ps4/…
checked 13 required asset(s): 10 ok, 3 failed
source warnings: 4 (not required assets — the known game-tree clobber; see §2.1)
S6 ASSET GATE FAILED — 3 required asset(s) missing or invalid
```

The fingerprint is exercised in both directions: a moved atlas, a zero-byte atlas, and a
same-dimension wrong-pixel atlas (the sharp PNG copied over the white one) each exit 4 — the last
one fails on the SHA-256, not the signature or the size.

---

## 2. The death bundle

`cp3shadow/anim/` (a hardlinked copy of `psarc/pak3` + `psarc/cachedFiles`; the game tree is a
merged extraction, see `SOTC-extract/README.md`):

| clip | bytes | frames | notes |
|---|---:|---:|---|
| `minotaur_a_death_front.anb` | 24,732 | 94 | the death motion; 58 tracks, 15.08056640625 fps = **6.233 s** of frames (94/fps) |
| `minotaur_a_death_front_loop.anb` | 3,532 | 1 | the shipped dead-hold pose; baked nowhere today |
| `objanim_minotaur_a_death_front.anb` | 4,808 | — | object animation, track type **29** |
| `camera_minoa_death_front_camera_01.anb` | 844 | — | camera, track type **36** (undecoded, §6) |
| `camera_minoa_death_front_camera_02.anb` | 2,244 | — | camera, track type **36** (undecoded, §6) |

The clip record (converted, `out/library/models/shadow/characters/colossus_01_minotaur_a/_cmn/
c01_minotaur_a.clips.json`) says:

```json
{ "name": "minotaur_a_death_front", "frames": 94, "fps": 15.08056640625,
  "tracks": 58, "root": true, "rootMotion": "body-offset" }
```

and the model glb carries the baked animation of the same name: **94 input keys per animated
track, 59 channels, sampler max 6.1669 s** (`(94−1)/15.0806`). The loop clip is not in the record
(the app pins the front clip's last frame instead — `docs/notes/s41-results.md:361`).

The frame/track counts were re-read from the recovered originals with `tools/re/anb/parse2.py`:
front **94 frames / 58 tracks** and loop **1 frame / 58 tracks**, both flag-clear (version word
`0xc`); the object animation is flag-set (version word `0x0001000C`) with track type 29.

**Timing detail for S6.** The runtime clip's duration is its maximum input time, **6.166877 s**
(93/15.08056640625 = 6.16687727 s); `94/15.0806 = 6.2332 s` is the frame-count figure (94 frames
as intervals). Task 2 **pinned** that value as `kMotionSeconds = 6.166877f`
(`app/src/death.h`), and the app cross-checks the *loaded* clip's duration against the constant
with a 1e-5 relative tolerance before the S6 timeline runs — so a re-baked clip that moved fails
the selftest loudly instead of shifting the phase boundaries. The design's "94 f / 6.233 s" is
descriptive, one frame interval longer than the clip's last key. (An earlier revision of this
note printed "6.166933 s" in front of the quotient above; the quotient was always right and the
number in front of it was a typo — it is 56 ms off the pinned constant.)

### 2.1 The four corrupted game-tree `.anb` copies (source warning)

On 2026-10-22 10:54 the game-tree copies of the four `.anb` files in this bundle (except
`camera_02`) were replaced, **in place and in their hardlinked psarc copies**, by a 246,449-byte
JSON dump of `player_a_stand` (`head 7b 0a 20 20`, `{"clip": "player_a_stand", …`, a parse2-style
dump). All four have the same content — a parse2-style dump of `player_a_stand`, a Wander clip S6 never
reads — which rules out a packaging or conversion quirk. The extraction manifest
(`SOTC-extract/logs/game.sha256`) and the pak archives still hold the originals; re-reading the
entries from `uroot/pak3.psarc` (three clips) and `uroot/cachedFiles.psarc` (`camera_01`) gave
byte-identical files:

| game-tree path (clobbered) | now | shipped bytes | shipped SHA-256 | pak |
|---|---:|---:|---|---|
| `cp3shadow/anim/minotaur_a_death_front.anb` | 246,449 | 24,732 | `0db1d990ba55687d6539ce61b39274684c29c0d281a6732cb7392cb07edf6c2b` | pak3 |
| `cp3shadow/anim/minotaur_a_death_front_loop.anb` | 246,449 | 3,532 | `a7639824b3e004a6207113d358072914df2a26706bf09fc609a5b56b3fb87a3f` | pak3 |
| `cp3shadow/anim/objanim_minotaur_a_death_front.anb` | 246,449 | 4,808 | `f5daae1a9450a82bdc1aa33b6d6e7ef6ee607a49b5729bd43f127e7007644351` | pak3 |
| `cp3shadow/anim/camera_minoa_death_front_camera_01.anb` | 246,449 | 844 | `e15eecc7375751ecc2ece8998dbacba8a29599468b09939d19402bfeb71f3a51` | cachedFiles |
| `cp3shadow/anim/camera_minoa_death_front_camera_02.anb` | 2,244 | 2,244 | `1a57e540eedf629c8b8b5cb4452b266755ad0544e00a9be999fd76541fe51146` | intact |

All four clobbered files have the same current SHA-256 (`a4d9c3b0d54feb7e…`), the same size and the
same content: a parse2-style dump of `player_a_stand` — a Wander clip S6 never reads — was written
over its own inputs. The author is unknown; the corruption was detected during Task 0 (mtimes
10:54, before the task started), and the content argument rules out a packaging or conversion
quirk. The project did **not** write the game tree in this task.

The gate treats exactly this incident as a warning (246,449 B **and** that SHA-256); any other
source mismatch — a truncated, zeroed or arbitrary `.anb` — is a hard failure. The rationale for
the warning: the viewer does not read the `.anb` at runtime — it plays the baked animation in
`c01_minotaur_a.glb`, and the gate proves that baked clip (94 keys / 6.1669 s) independently. What
is lost is the ability to **re-bake** the death clip from source until the four files are restored.

**Restore procedure (not run here).** The entries live in `uroot/pak3.psarc`
(`cp3shadow/anim/minotaur_a_death_front.anb`, `…_loop.anb`, `objanim_minotaur_a_death_front.anb`)
and `uroot/cachedFiles.psarc` (`camera_minoa_death_front_camera_01.anb`). The extractor is
`SOTC-extract/tools/psarc_final.py`:

```bash
python3 SOTC-extract/tools/psarc_final.py extract <tmp>/psarc <uroot>/pak3.psarc <uroot>/cachedFiles.psarc
# then copy <tmp>/psarc/pak3/cp3shadow/anim/{three}.anb and
#           <tmp>/psarc/cachedFiles/cp3shadow/anim/camera_..._01.anb into the game tree,
# and re-check: shasum -a 256  (the five hashes above / logs/game.sha256)
```

Caveat: the game tree is hardlinked to the `psarc/` trees, so writing a file **in place** (truncate
or overwrite) also rewrites the pak copy — that is how the clobber propagated. Replace the file
(unlink + new inode) or restore both trees; then re-run the gate and confirm the four warnings are
gone. The gate's four `warn source` lines are the durable reminder.

---

## 3. The dead body

Source: `models/shadow/characters/colossus_01_minotaur_a_deadbody/_cmn/` —

| source | bytes | converted |
|---|---:|---|
| `c01_minotaur_a_dead_body.cmsh` | 2,735,727 | `…_deadbody/_cmn/c01_minotaur_a_dead_body.glb` 11,924,836 B: **6 meshes / 6 primitives / 6 materials / 0 skins**, 216,272 vertices |
| `c01_minotaur_a_dead_body_skirt.cmsh` | 45,215 | `…_skirt.glb` 128,844 B, 1 mesh, 2,103 verts |
| `c01_minotaur_a_dead_dirtmound.cmsh` | 134,740 | `…_dead_dirtmound.glb` 254,948 B, 1 mesh, 3,557 verts |

The six dead materials (all in the glb, all `_dead`): `colossus_01_waist_dead`,
`colossus_01_back_dead`, `colossus_01_face_dead`, `colossus_01_arms_dead`,
`colossus_01_chest_dead`, `colossus_01_legs_dead`.

**The six-slot dead `.cmat`.** `materials/shadow/characters/colossus_01/_ps4/colossus_01_<part>_dead.cmat`
(1,588–1,603 B each) carries six texture references; the chest one is the pattern:

```
1  $/textures/tiling/dirt/_ps4/cmn_dirt_pebbly_01_col.ctxr
2  $/textures/tiling/dirt/_ps4/cmn_dirt_pebbly_01_nml.ctxr
3  $/textures/shadow/characters/colossus_01/_ps4/c01_chest_col.ctxr
4  $/textures/shadow/characters/colossus_01/_ps4/c01_chest_rgh_695c5610.ctxr
5  $/textures/shadow/characters/colossus_01/_ps4/c01_chest_nml.ctxr
6  $/textures/shadow/characters/common/_ps4/death_tentacle_veins.ctxr   <- the veil layer
```

All six `_dead` materials name the same sixth slot (`death_tentacle_veins.ctxr`), which is the
game's black-vein layer. **The converted glb does not carry it** (D9): its `images` are only
`cmn_dirt_pebbly_01_col.png` + `cmn_dirt_pebbly_01_nml.png`, and all six materials bind those two
(base/normal). Slots 3–5 (`c01_<part>_col/_rgh/_nml`) are collapsed too — i.e. the D9 work is
"carry the sixth slot as the emissive/veins texture", but the material path today keeps only the
first base+normal pair, so the corpse will look like the dirt base until Task 3's material work
lands. The veins PNG is already in the library (1024×256, 228,032 B) and is gated.

Corpse wiring in the shipped scripts: `scripts/shadow/entities/dead_colossi/_cmn/
minotaur_a_dead_body.cgpr` (925 B) loads `minotaur_a_dead_body.csml` (6,929 B) and `.cpid`
(3,272 B); the corpse end state is `ACT_BOSS_DEAD_BODY` (§5). The veil texture is the same
`textures/shadow/characters/common/_ps4/death_tentacle_veins.ctxr` (Oodle CTXR, 8c0a) that the app
will sample on the ribbon.

---

## 4. The `.cprt` emitters

`particles/shadow/colossus/death_tentacle/_ps4/death_tentacle.cprt`, 2,376 B:

```
0x00  u32[5] = { 0, 3, 8, 1, 0x64 }        # 3 emitters, data base 0x64
0x60  u32 = 0, then section offsets { 0x1c, 0x28, 0x2c, 0x2c, 0x30, 0x33c, 0x620 }
0x80  "death_tentacle\0", then per-emitter records
0x93c "TRAPL" + 3 pad bytes + u32 0x93c (= file size − 12)   # 12-byte footer
```

The emitter blocks are delimited by their `death_tentacle:<name>` markers. Each carries three
`.csdr` references and one `.cptx`:

| # | emitter | marker | csdr (bytes) | cptx (bytes) | converted atlas |
|---:|---|---:|---|---:|---|
| 0 | `main_wisp` | 0x218 | `emitter_0_emit` 1,456 / `_spawn` 5,322 / `_update` 4,319 | 492,165 | `8x8_wispysmoke02_1024_white_pfx.cptx` → **1024×1024**, 707,088 B PNG |
| 1 | `smoke` | 0x504 | `emitter_1_emit` 1,456 / `_spawn` 6,158 / `_update` 4,269 | 780,747 | `8x8_smoke_wispy_sharp_1024_pfx.cptx` → **1024×1024**, 919,600 B PNG |
| 2 | `veins` | 0x7a8 | `emitter_2_emit` 1,456 / `_spawn` 4,681 / `_update` 4,366 | 47,183 | `2x2_smoke_tendrils_01_pfx.cptx` → **512×512**, 73,087 B PNG |

The three `.cptx` are the same Oodle/CTXR family the converter already decodes: magic `8c 0a`
(`8c0a` variant), exactly like `textures/vfx/common/trees/_ps4/2x2_oakleaf_pfx.ctxr`. All four
textures (the three atlases + `death_tentacle_veins`) decode to RGBA8 PNGs with live alpha — the
gate's smoke samples (mean / max alpha) were 41.9 / 197, 18.3 / 116, 6.6 / 180 and
168.1 / 255 respectively, so none is a blank decode.

Note the names: the emitter called `veins` binds the **2×2 smoke-tendrils atlas**, not
`death_tentacle_veins.ctxr`. The latter is the ribbon's texture (the dead material's sixth slot).
The reconstruction should use the atlas for the emitter's particles and the veins texture on the
ribbon.

Wiring in the shipped scripts: `scripts/shadow/entities/colossi/_cmn/death_tentacle.cgpr` (297 B)
names the effect `Death Tentacle` and references
`$/particles/shadow/colossus/death_tentacle/****/death_tentacle.cprt`;
`scripts/shadow/world/arena_01/_cmn/arena_01.cgpr` carries the `death_tentacle_loader`;
`scripts/shadow/entities/colossi/_cmn/colossus_01_minotaur_a_death.cgpr` (271 B) is the
death-mode script; `particles/shadow/_cmn/effect_map.crmp` (5,805 B) is the effect map.

---

## 5. The binary (static RE, `eboot.elf`)

`CreateDeathTentacle(ISYSObj *)` — **0x008424d0** (signature string at 0x0128ec66; the file
`…\gamecore\basescript\src\death_tentacle.cpp` at 0x0128ec0c). One call site: **0x0082d8c5**
inside `FUN_0082c080` (the boss update). The function:

* builds a manager with `FUN_00676690(&LAB_008426a0, 0, &DAT_02f35858, 0x2000, 0x4e4, 8, obj, 0)`
  — **0x4e4 B**;
* fills the param record: **`+0x18 = 0xc9`** (201 segments), `+0x40 = 10` (the design's
  "`+0x10 = 10`" is the same field counted in dwords), the owner pointer, and the owner matrix
  copied from **`*(obj+0x150)`** (`FUN_00660fc0`);
* registers the callback `FUN_008426d0`; the callback loads
  `$/scripts/shadow/entities/colossi/****/death_tentacle.cgpr` as `DeathTent`, allocates two
  `segments << 4` byte arrays (201 × `vec4` positions and directions) and stores the **0.25 m**
  spacing `0x3e800000` in the per-tendril instance. The milestone's geometry constants
  (201 × 0.25 m) therefore come straight from the binary (this note's evidence), not from the
  design prose; the asset gate itself does not re-read `eboot.elf`.

**The trigger is the motion-change latch, not HP.** `FUN_00636650` (the motion setter) sets
`byte [obj+0x52c] = 1` at **0x00637015** and clears it at **0x00636ed8**; `FUN_0082c080` tests
`*(char *)(FUN_00611510(obj) + 0x52c) != 0` and calls `CreateDeathTentacle` when it is set (and
the owner entity is the active one). The HP → death-mode decision is script-side
(`Minotaur_A_death`), as the design says.

**The roots.** `FUN_006e11d0` / `FUN_006e11e0` maintain the global hit-point list:
`&DAT_03241368` (points, **stride 0x38**), `DAT_032415c0` (count), cap **10**, with a score word at
`DAT_03241598` and a descending sort. One tendril is created per recorded hit point; the app's
weakpoint shells are the fallback when the list is not available (the design's §8.2).

**The corpse state.** `ACT_BOSS_DEAD_BODY` at 0x012d1d49 (one data reference, from the table at
0x015d61d8); `BossStatus_Alive_MinotaurA` at 0x0101581a (`FUN_00539aa0`);
`BossStatue_Whole_MinotaurA` at 0x0101599d; the entity script is
`minotaur_a_dead_body.cgpr`.

---

## 6. The camera gap (type 36)

Both `camera_minoa_death_front_camera_01/02.anb` are **track type 36** — the ANB reader rejects
them by name:

```
$ python3 tools/re/anb/records.py decode …/camera_minoa_death_front_camera_02.anb --track 0
ParseError: track 0 is type 36, not in the caseD_9 family
```

The type-36 fact for `camera_02` was read from the intact game-tree file. `camera_01`'s game-tree
copy is one of the four clobbered files (§2.1) and is unreadable as an ANB; its type-36 fact was
read from the **recovered pak copy** (844 B, hash above), so this section does not depend on the
damaged tree.

`tools/re/anb/records.py`'s **Python** dispatch table does list type 36's sampler case (0x23:
`FUN_00600810` for the pose plus `FUN_00600d70(t, 0, motion, "camera_0",
viewAngle/aspectRatio/timeScale)` and the camera id at `motion+8 + track*4`); that table mirrors
the **native** sampler `FUN_005fd900`'s switch, but neither has a decoded pose reader for the
type — the reader rejects it by name. The clips are therefore **not played**: S6 reconstructs the
camera (D2, §8.3 of the design), and the note keeps the gap explicit.

---

## 7. Original vs reconstruction (A1/A4)

**From the game (original bytes, shipped paths):**

* the death motion `minotaur_a_death_front` (baked, 94 keys) and the loop clip's source (1 frame);
* the dead-body meshes, the skull/skirt/dirtmound, the six `_dead` materials, the veins texture;
* the three `.cprt` emitters (`main_wisp`, `smoke`, `veins`), their names/roles, their `.csdr`
  scripts and their three `.cptx` atlases (decoded here);
* the 201-segment × 0.25 m ribbon geometry, the `+0x52c` motion latch, the hit-point root list.

**Authored for S6 (labeled reconstruction):** the curl/turbulence path, the ribbon width envelope
and UVs, the billboard counts (24 `smoke` / 16 `main_wisp`), the 0.4–3.0 s burst window, the
camera pull-back/orbit/low-angle path, the corpse yaw/offset, the root jitter. The `.csdr` GCN
evaluator is **not** ported; the type-36 camera clips are **not** played; the corpse is static (no
ragdoll). The tendril look must be described as a reconstruction in the results note and the walk.

---

## 8. Reproduce / files

```bash
# the gate (read-only)
python3 tools/re/s6/s6_asset_gate.py --root "$SOTC_GAME_ROOT" --out "$SOTC_LIB" --check

# the three atlases (PNG paths are the library's texture mirror)
for pair in \
  "vfx/common/smoke/_ps4/8x8_wispysmoke02_1024_white_pfx" \
  "vfx/common/smoke/_ps4/8x8_smoke_wispy_sharp_1024_pfx" \
  "vfx/common/smokeman/_ps4/2x2_smoke_tendrils_01_pfx"; do
  caffeinate -dimsu dotnet exec out/bin/bp-convert.dll texture \
    --in "$SOTC_GAME_ROOT/textures/$pair.cptx" --out "$SOTC_LIB/textures/$pair.png"
done
```

Converted PNGs (all `variant 8c0a`, RGBA8; the gate pins dimensions **and** SHA-256):
`textures/vfx/common/smoke/_ps4/8x8_wispysmoke02_1024_white_pfx.png` 1024×1024 / 707,088 B /
`0fd737f277bf3b7077b16f088113acfbd1e2462f01bba8005b84405749dd1a45`;
`…/8x8_smoke_wispy_sharp_1024_pfx.png` 1024×1024 / 919,600 B /
`135499f5485f2a373fc714f6c2177bfc522549f4604f3aa2f45ac41aaa79c69d`;
`textures/vfx/common/smokeman/_ps4/2x2_smoke_tendrils_01_pfx.png` 512×512 / 73,087 B /
`2788814f0d2b06d0fbab743685a50a32371ec9919971bbf59d9e061c0df0bd4f`;
the pre-existing `textures/shadow/characters/common/_ps4/death_tentacle_veins.png` 1024×256 /
228,032 B / `f45991263e0ebc329b0bd367c05e561700b0f454c54fcbc718f2f7cc255a3e3c`.

Files: `tools/re/s6/s6_asset_gate.py` (new), `src/BpConvert.Cli/Program.cs` (the `texture` verb and
usage line), this note. The library and the PNGs are gitignored; no asset is committed.
