# S5 Audio — integration recon (read-only)

Status: **recon complete, no code changed.**
Date: 2026-10-21. Repo: `sotc-native` @ `275d249` (S4.2 "He fights back" green).

This is an integration map, not a design. It answers: where audio attaches in the
converter, where it attaches in the app, what the SDL3 audio surface is on this machine,
how to test it headlessly, and what the build needs.

---

## 0. Headline findings

| # | Finding | Consequence |
|---|---|---|
| 1 | The `sound/` container magic is **`SXDF`**, not `SHDF`. A streamed bank is a **`.sxd1` header + `.sxd2` payload pair** whose payload magic is **`SXDS`**. | Any design doc or plan that says "SHDF/.sxd" needs correcting before a decoder is written. |
| 2 | The container is a **chunk chain** shared by every bank: `TONE, REQD, NAME, WAVE, SEQD, LVAR, LVRN, RXFD, RXFN, DATA`. | One parser serves all 25 banks. The `NAME` chunk carries the real event names — the milestone's addressing scheme. |
| 3 | `sound/shadow/_cmn/shadow_sounds.cxml` is a **plaintext table of 8,001 sound-event names**, and `sound_data.calt` lists the 25 banks. | The asset vocabulary does not have to be guessed; it can be read. |
| 4 | `boss01.sxd` already contains the **exact Valus set** this milestone needs (`boss01_atk_hit_ground1..3`, `boss01_whoosh_head_swing1..3`, `boss01_vo_atk1..3`, `boss01_vo_dmg1..8`, `boss01_FS_all/far/mid`, `mammo_body1..6`). | The first audible fight needs one bank, not a general audio stack. |
| 5 | SDL3 **3.4.18** on this machine has the **complete** audio API, and the **`dummy` and `disk` drivers are compiled in** (`SDL_AUDIO_DISK_OUTPUT_FILE` exists). | Headless mixdown to a file needs no custom sink and no new dependency. |
| 6 | SDL3**\_mixer 3.2.4** is installed (headers, dylib, CMake config). | Music/looping/streaming is available without hand-rolling a mixer — but it is an *additional* link, not required for SFX. |
| 7 | The app builds **three near-duplicate frame loops** (interactive, `s41`, `s42`). | Audio hooks must go into the shared functions (`apply_combat_events`, `player_step`), never into a loop, or the three will drift. |
| 8 | `SDL_Init` is called with `SDL_INIT_VIDEO \| SDL_INIT_GAMEPAD` only. | `SDL_INIT_AUDIO` is an additive change at one line; audio must tolerate a failed init (headless CI has no device). |

The design doc anticipated all of this: `docs/superpowers/specs/2026-10-15-sotc-native-rebuild-design.md:79`
(D9 "Original audio required … capture-grade fallback if formats resist") and `:173`
("Audio formats (`sound/`, ~1.9 GB: `sxd`, `sxd1/2`, `crmp`, `calt`) have no decoder path yet |
medium | … recon workstream at M4 entry"). Measured: **`du -sh sound` = 1.9 GB**, 39 files.

---

## 1. Converter

### 1.1 CLI verbs and structure

`src/BpConvert.Cli/Program.cs` is a top-level-statements file: a `switch` on `args[0]` at
`Program.cs:11-23`, one local function per verb, and a shared `PrintUsage()` at
`Program.cs:25-37`. Exit-code contract, repeated in every verb's doc comment:

- `0` success, `1` batch/runtime failure, `2` bad invocation.
  (`Program.cs:73-75`, `:200-201`)

| Verb | Entry | Shape |
|---|---|---|
| `inspect <file>` | `Program.cs:39` | One path, no flags; prints size, first 32 bytes hex, library parse summary. |
| `convert --root --out [--under]… [--ext] [--clips] [--jobs] [--force]` | `Program.cs:76` | Builds a `BatchOptions`, calls `BatchRunner.Run(options, Console.WriteLine)`, prints a one-line summary (`Program.cs:173-178`). |
| `report --out [--top N]` | `Program.cs:203` | `Catalog.Load` → `Catalog.Summarize` → counts + top failures. |

Argument parsing is hand-rolled: a `wants_value` predicate (`Program.cs:111-115`) that every
value-taking flag must be listed in, then an if/else-if chain (`Program.cs:128-181`). Helpers
`TryValue` (`:288`), `MissingValue` (`:299`), `FormatElapsed` (`:306`), `HeadHex` (`:309`).

**An `audio` verb belongs here** as `case "audio": return AudioCommand(args);` at
`Program.cs:15-22`, plus two or three `PrintUsage()` lines at `Program.cs:35`. It must follow
the same three conventions: a `--root`/`--out` pair, a `wants_value` entry for every new flag,
and the `0/1/2` exit contract.

### 1.2 Core pipeline

`src/BpConvert.Core/Pipeline/`:

| File | Role | Reusable for audio? |
|---|---|---|
| `BatchRunner.cs` | Enumerates sources under `--under`, fans out `--jobs`, records each outcome, flushes every 25 entries. | **Reuse as-is.** Its shape is generic; only the per-source work is model-specific. |
| `AssetResolver.cs` | Resolves a source path to sibling assets (`.cmdl` material map). | Reuse the sibling-resolution idea for `.sxd1↔.sxd2`. |
| `Catalog.cs` | The resumable catalog (see 1.3). | **Reuse the class, extend the entry** — do not fork it. |
| `ModelConverter.cs` | The one source→glb conversion. | Pattern only. |
| `ClipManifest.cs`, `ClipMotion.cs`, `ClipSet.cs` | `.anb` clip baking (`--clips`). | Pattern only. |

Architecture rule that constrains the new code (`AGENTS.md`, "Architecture rules"): parsing of
`AquaModelLibrary.Data` types lives **only** in `src/BpConvert.Core/Aqua/AquaModelAdapter.cs` +
`Inspect.cs`. The audio format is *not* an Aqua type, so it does **not** go behind that adapter —
but it must follow the same rule the adapter exists to enforce: **parse into our own DTOs, keep
the parser in one place, and let everything downstream work on DTOs.**

### 1.3 Catalog and resumability

`Catalog.cs` is the piece to copy exactly:

- On-disk schema is pinned and versioned: `{"version":1,"entries":{"<src rel>":{...}}}`
  (`Catalog.cs:8-17`, `:201-218`). `SchemaVersion = 1` at `:33`.
- `Catalog.Load` (`:59`) treats a **missing** file as an empty catalog (first run) but throws
  `InvalidDataException` on bad JSON or a wrong version (`:74-87`) — it never silently reconverts.
- `Catalog.Save` (`:107`) is atomic: serialize to `catalog.json.tmp`, `Flush(flushToDisk: true)`,
  `File.Move(..., overwrite: true)`, all under one `WriteGate` (`:115-141`).
- `CatalogData` is thread-safe for the runner's concurrent writers and returns a **key-sorted
  copy** from `Entries` (`:237-241`), so equal state always serializes identically.
- `CatalogEntry` is a record with factories `Ok(...)` / `Failed(error)` (`:265-275`).
- Resumability contract (`AGENTS.md`): sources recorded `ok` are skipped, `failed` are retried,
  `--force` reconverts; **done = a rerun reports `converted: 0`.**

The audio catalog should be a **second document**, not a second `entries` key space in the model
catalog: models and audio have different schemas (`glb`/`pngs`/`meshes` vs
`wav`/`frames`/`sample_rate`). Two files, one library root:

```
out/library/
  catalog.json          # existing, model sources
  audio/catalog.json    # new, audio sources
  audio/<bank>/<event>.wav
  audio/audio.manifest   # the app's runtime index
```

### 1.4 Asset DTO patterns

`src/BpConvert.Core/Model/ModelData.cs` is the DTO exemplar: plain records/classes, no library
types, no I/O, no formatting. `Textures/CtxrConverter.cs` + `Textures/BcnDecoder.cs` show the
"decode to a byte buffer, write the file elsewhere" split. `src/BpConvert.Core/Anim/` holds the
clip DTOs.

Follow that: `Audio/` gets **`AudioBank.cs`** (parsed DTO: header, chunk table, tone/wave/name
entries), **`AudioDecoder.cs`** (codec → PCM), and **`AudioWriter.cs`** (PCM → WAV). No
`File.ReadAllBytes` in the DTO; the reader takes a path and returns a DTO.

### 1.5 What the `audio` verb reads

Verified this session by hexdump + a chunk walker over `sound/shadow/*.sxd`.

**Container header** (identical in `SXDF` and `SXDS`), little-endian:

```
0x00  char[4]  "SXDF"  (bank header)   |  "SXDS"  (stream payload)
0x04  u16      version = 4
0x06  u16      unused
0x08  u32      total size == file length on disk
0x0C  u32      zero
0x10  u8[16]   content hash — THE LINK between .sxd1 and .sxd2
0x20  char[16] bank name, NUL-padded ("boss01", "music", …)
0x60  …        chunk chain
```

Verified: `music.sxd1` and `music.sxd2` carry the same hash `711e4c81e3009f4893a0a0c501928b77`;
`boss01.sxd` size field `0x00F3A5E0` = 15,967,712 = exact file length.

**Chunk chain**: from `0x60`, each chunk is `char[4] magic` + `u32 size` + `size` bytes of body.

| Bank | Chunks (offset, size) |
|---|---|
| `boss01.sxd` | TONE @0x60 (6,312) → REQD → NAME (10,008) → WAVE (8,024) → SEQD (41,976) → LVAR → LVRN → RXFD → RXFN → **DATA @0x14f10 (15,881,928 = to EOF)** |
| `com_sys.sxd` | same chain, DATA @0x1bb0 (1,066,792) |
| `bridge.sxd` | **TONE only, 144 B total** (an empty bank — the parser must accept this) |
| `music.sxd1` | TONE…RXFN but **no DATA** (payload is in the `.sxd2`) |

**Index-table pattern** (REQD, NAME, SEQD, WAVE all use it): `u32 reserved=0`, `u32 count`,
`count × u32` offsets from the chunk body start, then the entries.

**NAME** entry, 12 bytes, `{u32 string_offset, u32 hash, u32 ?}` — 343 entries in `boss01`,
517 in `edp01`, 350 in `com_obj`. The strings are the event vocabulary and are NUL-free ASCII.

**WAVE** entry (32 B): `{u32, u32, u32 1, u32 fmt, u32 sample_rate, u32 frames, u32 0, u32
data_offset}`. Observed `fmt=0x00FF0221`, `sample_rate=0xBB80` = **48000**, `frames=0x2666` =
9830 (= 0.2048 s at 48 kHz), `data_offset=0x18E04`.

**Payload**: `DATA` begins with ~0x28 zero bytes, then compressed frames. **The codec is not yet
identified** — this is the milestone's first real risk. Evidence:

- No `RIFF` and no `OggS` anywhere in `sound/` (grep over every `.sxd*`: 0 hits).
- `AT9` appears in 9 files (`boss03`, `boss07`, `boss08`, `music.sxd2`, `str_*.sxd*`) — consistent
  with **ATRAC9** but not proof (could be a coincidence inside compressed bytes).
- The `WAVE` entry's `fmt`/`rate`/`frames`/`offset` shape is what an AT9-in-custom-container
  would carry.

**The gate for the milestone is a decode proof, not a parser:** dump a WAVE entry, decode it,
and require non-silence + a plausible spectrum before any of the app work starts. If ATRAC9
resists, D9 already sanctions a labelled capture-grade fallback — but that decision must be
explicit, not a silent substitution.

**Companion files** (all plaintext-ish and cheap to read):

| File | Size | What it is |
|---|---|---|
| `_cmn/shadow_sounds.cxml` | 355,718 | **8,001** `<Sound name="…" globalInstVars="Height" testOccl="1" occlRadius="10"/>` entries. One root `<Sounds>`, no other tags. This is the event registry. |
| `_cmn/sound_data.calt` | 1,011 | NUL-free list of the 25 banks: `$/sound/shadow/boss01.sxd` … |
| `_cmn/shadowbanks.crmp` | 842 | Bank map: `u32 count=25`, then `{u32, u32}` pairs. |
| `_cmn/shadowbossmusic.crmp` | 196 | 20 entries + a trailing `PAMR` chunk. |
| `_cmn/shadowstreams.crmp` | 3,295 | Stream registry; contains readable stream names (`st_amb17`, `MIDDEMO_13`, `homeboss15_221`, `cer_ev_02`, `itemget_2`). |
| `_ps4/grass_wind.cpu.cctx` | 14,452 | PS4 compute-side context (ignore for M5). |

**Streamed banks** (music/ambience — the reason the library is 1.9 GB):

| Pair | `.sxd1` | `.sxd2` | Contents |
|---|---|---|---|
| `music` | 9,872 | 172,367,136 | Menus/credits |
| `str_amb` | 15,614,576 | 375,729,632 | Ambient beds |
| `str_bgm` | 2,461,232 | 514,125,344 | **Score** |
| `str_boss` | 48,928 | 286,412,736 | Boss music |
| `str_event` | 57,248 | 366,325,072 | Scripted cues |

The `.sxd1` holds the tables (TONE/NAME/WAVE/…) and **no DATA**; the `.sxd2` holds the payload
under an `SXDS` header. Resolution is by the 16-byte hash at `0x10`. **Do not copy 1.7 GB of
music into `out/library`** — the milestone should extract only the cues it wires (see 2.6).

### 1.6 What the `audio` verb writes

```
out/library/audio/                     # gitignored (out/ already is)
  catalog.json                         # Catalog.cs schema, second document
  audio.manifest                       # the app's runtime index (see below)
  bank/<bank>/<event>.wav              # 48 kHz, S16 or F32, mono/stereo as authored
  bank/<bank>/<event>.wav.json         # provenance: source, WAVE index, frames, codec
```

`audio.manifest` is the **seam that keeps the app offline-first** (design §8: the runtime never
parses original game containers). Minimum fields per entry:

```
name=boss01_atk_hit_ground1  bank=boss01  file=bank/boss01/boss01_atk_hit_ground1.wav
rate=48000  channels=1  frames=9830  seconds=0.2048  codec=at9  loop=0  gain_db=0
```

One line per event, stable sort by `(bank, name)` — the same determinism rule `Catalog` already
enforces. The app reads **only** this file.

### 1.7 Files and classes to add (converter)

| Path | Purpose |
|---|---|
| `src/BpConvert.Core/Audio/SxdContainer.cs` | Header + chunk-chain reader. `SxdChunk{Magic,Offset,Size}`, `SxdBank{Version,Hash,Name,Chunks}`. Rejects a truncated chunk loudly. |
| `src/BpConvert.Core/Audio/SxdTables.cs` | The shared index-table reader + `Tone/Wave/Name` DTOs (the 12-B name entry, the 32-B wave entry). |
| `src/BpConvert.Core/Audio/AudioCodec.cs` | Codec detection + decode to planar float PCM. **The risky file.** One place to swap ATRAC9 for the fallback. |
| `src/BpConvert.Core/Audio/WavWriter.cs` | PCM → WAV (RIFF/fmt/data). Small and testable. |
| `src/BpConvert.Core/Audio/AudioCatalog.cs` | Thin wrapper over `Catalog` with the audio entry schema (`wav`, `frames`, `rate`, `codec`). |
| `src/BpConvert.Core/Audio/AudioManifest.cs` | Writes/reads `audio.manifest`. |
| `src/BpConvert.Core/Audio/SoundEventTable.cs` | Parses `shadow_sounds.cxml` into `{name, ignore, occlRadius}` — the allow-list. |
| `src/BpConvert.Cli/Program.cs` | `case "audio"` + `AudioCommand` + usage lines. |
| `tests/BpConvert.Tests/AudioTests.cs` | Chunk-walk a fixture bank, assert chunk order/sizes, the 12-B name stride, the 48 kHz WAVE field, a WAV round-trip. **No PCM-equality golden tests** (codec-dependent). |

Verb shape:

```bash
bp-convert audio --root "$SOTC_GAME_ROOT" --out out/library \
                 --banks boss01,com_b_h,com_sys,com_obj \
                 --events boss01_atk_hit_ground1,boss01_whoosh_head_swing1,boss01_vo_atk1 \
                 [--all] [--jobs N] [--force]
```

`--events` (with `--banks`) is what keeps the first milestone small; `--all` is the 1.9 GB path
and must be opt-in.

---

## 2. App event seams for SFX

### 2.1 The three structural facts that decide every hook

1. **`StepEvents` is the only per-frame gameplay signal.** `player_state.h:131-136`:
   ```cpp
   struct StepEvents {
       int damageDealt = 0;
       bool gripBroke = false;
       bool landed = false;
       bool grabbed = false;
   };
   ```
   Returned by `player_step` (`player_state.h:188`, defined `player_state.cpp:260`). Everything
   that happens to the player — grab, grip loss, strike damage, landing — is already in there.
   **A sound layer that consumes `StepEvents` is nearly free.**
2. **`apply_combat_events` is the single funnel for damage to Valus.**
   `app.cpp:1221-1240`. Called from exactly three places: the `s41` timeline
   (`app.cpp:1837`), the `s42` timeline (`app.cpp:2720`), and the interactive loop
   (`app.cpp:4302`). One hook here covers all three loops.
3. **Valus's attack has explicit start / impact / end / cancel functions**, so the telegraph and
   the impact are separate sounds with no ambiguity:
   `start_valus_attack` (`app.cpp:1042`), `advance_valus_attack` (`app.cpp:1063`) which calls it
   at `:1095`, `cancel_valus_attack` (`app.cpp:1027`), `apply_valus_impact` (`app.cpp:1137`),
   and `attack_impact_due` (`valus.h:63`, windows at `valus.h:31-34`).

### 2.2 The anchors

**Player — `app/src/player_state.cpp`**

| Event | Anchor | Notes |
|---|---|---|
| Grab succeeds (grounded) | `:288-296`, sets `events.grabbed` at `:293` | rising-edge of `E` within `grabRange` |
| Grab succeeds (catch in air) | `:429-439`, sets `events.grabbed` at `:434` | re-grip inside the catch window |
| **Grip breaks** | `detach(...)` `:119-136`; callers `:391` (landed strike), `:398` (band unresolvable), `:415` (stamina empty) | sets `events.gripBroke` at `:131`; deliberate `E` release passes `false` (`:306`) |
| **Strike releases** | `:373-393` | `damageDealt > 0` = shell hit (`:385`); **`damageDealt == 0` = the whiff/clang** (`:383-387`) — the comment at `:381` already calls it "a whiff is a clang" |
| Charge grows | `:374-375` | `combat.charge` 0→`chargeSeconds`; the charge sound's gain/pitch source |
| Landing | `:447-462`, sets `events.landed` at `:453` | enters `Staggered`, or `Dead` when `pendingDeath` (`:454-457`) |
| Hurt entered | `enter_hurt` `:218`, called from `:532` | `heavy` selects the clip; `heavyHit` at `player_state.h:76` |
| **Death** | `enter_dead` `:228`, called from `:457` (pending) and `:528` (ground) | terminal until `R` |
| i-frame gate | `:503` — returns `false` when `invulnSeconds > 0` | a hit that lands *no* damage — good "blocked" cue, or deliberately silent |
| Soft reset (player half) | `reset_player_combat` `:536-552` | |

**Valus — `app/src/app.cpp`**

| Event | Anchor |
|---|---|
| Attack selected + started (telegraph begins) | `advance_valus_attack` `:1086-1099`; `start_valus_attack` `:1042-1055` |
| Attack clip ends → stand + cooldown | `:1113-1125` |
| Attack reached its impact frame | `:4309-4314` (interactive), `:2724-2730` (s42 timeline); gate `attack_impact_due` `valus.h:63` |
| **Impact damage split (stomp direct / shockwave / club)** | `apply_valus_impact` `:1137-1177`; ranges at `:1156-1169` |
| Attack cancelled by a landed strike | `apply_combat_events` `:1226-1228` → `cancel_valus_attack` `:1027-1035`; guard `valus_attack_cancelled` `valus.h:72` |
| Valus turns | `:4253-4257` (`valus_yaw_slew`, `valus.h:83`) |
| Damage lands on Valus | `apply_combat_events` `:1229-1240`; reaction clip pick at `:1237-1238` (weakpoint 1 → arm, 0 → head, else body) |
| **Defeat** | `:1231-1235` — sets `encounter.defeated`, starts the death clip |

**Encounter / session / HUD**

| Event | Anchor |
|---|---|
| `R` soft reset (interactive) | `app.cpp:4130-4168` — `reset_player_combat` `:4136`, `reset_encounter` `:4142`, entity back to stand `:4143-4150` |
| `R` soft reset (s42 timeline) | `app.cpp:3434-3435` |
| HUD published each frame | `app.cpp:4340` `renderer.set_hud(hud_state(true))`; `HudState` `hud.h:13-18` |
| HP/stamina thresholds (for `hp_alert`/`stamina` cues) | derive in the audio layer from `combat.health` / `combat.stamina`; no existing event |
| Pause | `GamepadInput::pausePressed` `app.cpp:1254`; keyboard pause in the event switch |
| SDL init | `app.cpp:1427` `SDL_Init(SDL_INIT_VIDEO \| SDL_INIT_GAMEPAD)` |

**Footsteps — the one event that does not exist yet.** `PlayerState` (`player.h:24-27`) carries
only `position` and `yawDeg`; there is no stride phase, and `player.cpp` has no callback. The
speed constants are `kWalkSpeed = 2.2f` / `kRunSpeed = 5.0f` (`player.cpp:9-10`) applied at
`player.cpp:68`. Three options, cheapest first:

- **(a) distance accumulator in the audio layer** — `distance += |position - lastPosition|` each
  frame while `mode == Grounded`; fire a step every `strideLength` (walk ≈ 0.75 m, run ≈ 1.6 m),
  alternating left/right. Zero changes to `player_state`, works for the three loops unchanged,
  and is deterministic in the fixed-step timelines. **Recommended for M5.**
- (b) add `float stridePhase` to `PlayerState` and advance it in `player_update` — accurate, but
  it touches the S3 controller, which `AGENTS.md` treats as frozen regression surface.
- (c) drive from the clip time of `walk`/`run` — needs the clip's foot-contact frames, which we
  do not have.

### 2.3 The minimal event set for the first audio milestone

Fifteen events. Every one has a hook above and an asset family in the shipped banks.

| # | Event | Hook | Asset family (measured in the bank) |
|---|---|---|---|
| 1 | `music.encounter` (loop) | session start after compose; restarted by `R` (`:4142`) | `str_boss.sxd2` cue (or `str_bgm` until boss music is wired) |
| 2 | `amb.arena` (loop) | session start; `R` | `str_amb.sxd2` bed / `com_amb.sxd` |
| 3 | `player.strike` | `player_state.cpp:373-392`, `damageDealt == 0` | `com_obj.sxd`: `b_sword_atk1..5`, `wnd_LifeSwd_swish_t1_01..04` |
| 4 | `player.strike_hit` (weakpoint) | `app.cpp:1237-1239` (head/arm reaction) | `boss01.sxd`: `mammo_body1..6`; voice `boss01_vo_dmg1..8` |
| 5 | `valus.reaction` | `apply_combat_events` `:1237` | `boss01_vo_dmg*` |
| 6 | `valus.roar` | `start_valus_attack` `:1042` (attack start) | `boss01_vo_atk1..3`, `boss01_vo_atk` |
| 7 | `valus.club_swing` (whiff) | `:1086-1099` start of `ValusAttack::Club` | `boss01_whoosh_head_swing1..3`, `boss01_mvt_swing_add` |
| 8 | `valus.club_impact` | `apply_valus_impact` `:1164-1168` | `boss01_atk_hit_ground1..3` |
| 9 | `valus.stomp` (telegraph) | `:1096-1098` start of `StompL/R` | `mammo_walk_wa1..8` / `boss01_down_mvt_whoosh_sub` |
| 10 | `valus.stomp_impact` | `apply_valus_impact` `:1156-1163` (direct **and** shockwave) | `boss01_atk_hit_ground1..3`, `boss01_eff_stone1..3` |
| 11 | `valus.footstep` | its `mammo_walk`/`FS` set, driven by the same accumulator as (15) | `boss01_FS_all/far/mid`, `boss01_FS_far1_0..5_0` |
| 12 | `valus.defeat` | `apply_combat_events` `:1231-1235` | `mammo_down`, `boss01_rise_sub` |
| 13 | `player.hurt` | `enter_hurt` `:218` via `:532`; use `heavy` for the two variants | `com_b_h.sxd` (`wnd_*`) + `bvo_atk*` in `com_v.sxd` |
| 14 | `player.death` | `enter_dead` `:228` via `:457`/`:528` | `player_a_fall_damage_to_death` is the *clip*; voice from `com_v.sxd` |
| 15 | `player.footstep` | accumulator (2.2 m/s) over `mode == Grounded` | `com_b_h.sxd` — 650 `wnd_*` names, surface-suffixed (`horse_run_grass/gravel/stone/…` is the convention to mirror) |
| + | `player.grab` / `player.grip_break` | `events.grabbed` / `events.gripBroke` | `com_sys.sxd`: `grip_alert`, `grip_max` |
| + | `ui.reset` | `:4130-4168` | `com_sys.sxd`: `decide`, `cursor` |

**Deliberately out of scope for M5:** occlusion (`testOccl`/`occlRadius` in the cxml), 3D
positional falloff, reverb zones, the `SEQD`/`LVAR` sequence layer (Wander's footsteps are
randomised by a sequence), and the full music state machine. `boss01_FS_all/far/mid` is already
LOD-split by the authors — pick one, do not implement LOD.

### 2.4 The seam that keeps it honest

Do **not** call `SDL_*` from inside `apply_combat_events` or `player_step`. Instead emit into a
small POD queue and consume it once per frame:

```cpp
// app/src/audio_events.h
enum class Sfx : std::uint16_t { Strike, StrikeHit, Grab, GripBreak, Land, HurtLight,
                                 HurtHeavy, Death, StompStart, StompImpact, ClubStart,
                                 ClubImpact, ClubCancel, ValusReaction, ValusDefeat,
                                 FootLeft, FootRight, Reset, Count };
struct SfxEvent { Sfx id; float gain = 1.0f; float seconds = 0.0f; };  // frame-stamped
```

`player_step` is a **pure function with no I/O** today and every test in
`app/tests/test_player_state.cpp` depends on that. Keep it that way: have `StepEvents` grow
`bool hurtLight/hurtHeavy/died` (fields only, no calls), and let the *app* translate
`StepEvents` → `SfxEvent`. The same translation is then used by the interactive loop and both
selftest timelines, which is what makes the s5 test meaningful.

### 2.5 Reset semantics

`R` (`app.cpp:4130-4168`) is the only path that must **stop everything**: cut the music bed's
envelope, drop all one-shots, zero the footstep accumulator, restart the ambience. It already
does the state half (`:4136-4158`); the audio half is one `audio.reset()` call beside
`reset_encounter` at `:4142`. The `s42` timeline has the same reset at `:3434-3435` and needs the
same call, or the s5 timeline and the interactive run will disagree.

### 2.6 What to extract for this milestone

Smallest audible set, by bank:

| Bank | Why | Est. size |
|---|---|---|
| `boss01.sxd` (15.9 MB) | all 11 Valus events | ~15 MB, or a few hundred KB with `--events` |
| `com_obj.sxd` (7.5 MB) | sword swing/whiff | subset |
| `com_b_h.sxd` (16.1 MB) | Wander footstep/landing foley | subset |
| `com_sys.sxd` (1.1 MB) | grab/grip/UI | subset |
| `com_v.sxd` (626 KB) | Wander voice | subset |
| `str_boss.sxd1`+`.sxd2` (286 MB) | fight music — **extract 1–2 cues only** | ~10 MB |

`--events` on the first five is the sane default; the streamed banks are the only place where
"extract everything" is unacceptable.

---

## 3. Audio device — measured on this machine

```
$ brew list --versions sdl3 sdl3_ttf sdl3_mixer
sdl3 3.4.18
sdl3_ttf 3.2.2
sdl3_mixer 3.2.4_1
```

There is **no `sdl3-config`** on this machine (it is not installed by the Homebrew formula), but
`/opt/homebrew/lib/pkgconfig/sdl3.pc` reports `Version: 3.4.18` and `-lSDL3`, and the CMake
package is at `/opt/homebrew/lib/cmake/SDL3/`. `find_package(SDL3 CONFIG REQUIRED)` already works
— `app/CMakeLists.txt:5` proves it.

### 3.1 API surface — all present

Verified in `/opt/homebrew/include/SDL3/SDL_audio.h` and with `nm -gU /opt/homebrew/lib/libSDL3.dylib`:

| Symbol | Header line | In dylib |
|---|---|---|
| `SDL_OpenAudioDeviceStream` | `SDL_audio.h:2058` | ✅ `_SDL_OpenAudioDeviceStream` |
| `SDL_CreateAudioStream` | `:1082` | ✅ |
| `SDL_SetAudioStreamFormat` | `:1167` | ✅ |
| `SDL_PutAudioStreamData` | `:1453` | ✅ |
| `SDL_PutAudioStreamDataNoCopy` | `:1530` | ✅ |
| `SDL_PutAudioStreamPlanarData` | `:1583` | ✅ |
| `SDL_BindAudioStream` / `SDL_BindAudioStreams` | `:995` / `:974` | ✅ |
| `SDL_ResumeAudioStreamDevice` | `:1767` | ✅ |
| `SDL_GetAudioStreamQueued` / `Available` / `Gain` / `FrequencyRatio` | — | ✅ |
| `SDL_LoadWAV` / `SDL_LoadWAV_IO` | `:2266` / `:2230` | ✅ |
| `SDL_GetNumAudioDrivers` / `GetAudioDriver` / `GetCurrentAudioDriver` | — | ✅ |

**Drivers compiled in: `coreaudio`, `disk`, `dummy`** (string table of `libSDL3.0.dylib`).
Relevant hints in `SDL_HINT_AUDIO_*`:
`SDL_HINT_AUDIO_DRIVER` (`SDL_hints.h:513`), `SDL_HINT_AUDIO_DISK_OUTPUT_FILE` (`:486`),
`SDL_HINT_AUDIO_DISK_TIMESCALE` (`:499`), `SDL_HINT_AUDIO_DUMMY_TIMESCALE` (`:526`),
`SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES` (`:372`).

**Reading:** the whole design can be plain async `SDL_AudioStream`s — one stream per voice,
`SDL_PutAudioStreamData` for one-shots, a bound stream per looping bed. `SDL_LoadWAV` reads the
converter's WAVs directly, so the app needs no decoder of its own for M5.

### 3.2 Version compatibility — no mismatch

| Package | Version | Requirement | Verdict |
|---|---|---|---|
| `sdl3` | 3.4.18 | — | base |
| `sdl3_ttf` | 3.2.2 | `Requires: sdl3 >= 3.2.6` (`sdl3-ttf.pc`) | ✅ satisfied |
| `sdl3_mixer` | 3.2.4 | `Requires: sdl3 >= 3.4.0` (`sdl3-mixer.pc`) | ✅ satisfied |

No ABI or version conflict. `SDL3_ttf` is currently linked by both targets
(`app/CMakeLists.txt:15`, `:46`) and needs nothing.

### 3.3 SDL3_mixer: available, optional

`/opt/homebrew/include/SDL3_mixer/SDL_mixer.h`, `libSDL3_mixer.dylib` (0.2.4 ABI), and
`/opt/homebrew/lib/cmake/SDL3_mixer/` are all installed, so `find_package(SDL3_mixer CONFIG
REQUIRED)` + `SDL3_mixer::SDL3_mixer` works.

**Recommendation: do not link it in M5.** The M5 event set is 15 fire-and-forget one-shots plus
two loops — `SDL_AudioStream` covers that with ~200 lines and zero new dependencies, which keeps
the fan-project build as light as it is today. MIX's real value (streaming decode, Music loops
with loop points, channel groups) becomes worth its `opusfile`/`vorbisfile`/`fluidsynth`/
`mpg123`/`wavpack` dependency chain when the *music* milestone arrives. Note that decision in the
design either way.

---

## 4. Test strategy — usable without ears

The S4.x pattern is already exactly right for this: **a deterministic fixed-step timeline that
prints parseable `sNN-*` lines, and a bash script that pins every field to a literal**
(`scripts/s42-acceptance.sh:148-232`, helper `field()` at `:149`, `one_line` regexes at `:208`,
`:246-256`). Audio extends it in three layers.

### 4.1 Layer 1 — pure event/gain logic, no device (doctest)

`app/tests/` is doctest (`test_main.cpp:1-2`), 12 files, all linked into `sotc-app-tests`
(`app/CMakeLists.txt:38-47`). Audio logic that is a pure function is testable today:

- **Voice allocator**: N voices, priority, steal-oldest — assert the returned voice index.
- **Footstep accumulator**: feed a synthetic position track at 1/60 s, assert the exact frame
  indices of the steps and that walk (2.2 m/s) and run (5.0 m/s) differ.
- **Event → asset resolution**: given `audio.manifest` text and an `SfxEvent`, assert the chosen
  name — including a missing asset degrading to silence rather than throwing.
- **Decibel/clamp math**: `gainToLinear`, clamp to [0,1], NaN input never reaches the mix.

These need no SDL device and no files, so they run in `ctest` unchanged.

### 4.2 Layer 2 — headless mixdown (the important one)

Two SDL3-native options, both verified present on this machine:

**Option A — the `disk` audio driver (zero new code):**
```bash
SDL_AUDIODRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=out/s5/mix.raw \
  app/build/sotc-viewer --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --s5-selftest --audio-null
```
SDL writes the device's PCM to a file. Deterministic if the timeline runs in fixed steps and the
mixer is driven by the same step clock — which the existing timelines already do
(`stepSeconds` 1/60, `app.cpp:2695-2740`).

**Option B — an app-side null sink with an explicit mixdown (recommended):**
`audio.mix(float dtSeconds) -> std::span<const float>` — the mixer is clocked by the caller, and
the device is only one consumer of it. The `--s5-selftest` path then writes a WAV itself. This
removes all dependence on device timing, is byte-reproducible, and is testable at *any* sample
rate. **This is the design that makes the acceptance honest**, and it is why the mixer should be
a plain class with a `mix(dt)` entry point rather than a device callback.

### 4.3 What the s5 checks can actually prove

| Property | How | Passes without listening |
|---|---|---|
| **Event order** | Print one `s5-evt` line per fired event with its frame: `s5-evt frame=… id=club_impact gain=…`. Pin the sequence in the script. | ✅ exact |
| **Non-silence** | Peak/RMS over each event's window in the mixdown buffer: `s5-mix frame0=… frames=… peak=… rms=…`; assert `peak > 0` and `rms > floor`. | ✅ |
| **Gain** | Same buffer, per event: `s5-gain id=hurt_heavy peak_db=-9.0`; assert within a pinned band and monotonic (heavy > light). | ✅ |
| **Looping** | Run the timeline past the loop point; assert the bed's energy is continuous across the seam and the loop count increments: `s5-loop music loops=2 seamless=1`. | ✅ |
| **Ducking/mixing** | Sum check: peak of (music only) < peak of (music + impact) — proves the layers actually add. | ✅ |
| **Silence-by-design** | `s5-mute reason=no_asset id=…` when a manifest entry is absent — a missing sound must be a line, never a crash. | ✅ |
| **Timbre / identity** | ❌ not possible. Zero-crossing rate + spectral centroid are a *smoke* test only ("this is not the wrong file / not silence"), never "this is the right sound". | ⚠️ weak |
| **"Sounds right"** | The user's walk. Same place S4.2's feel/telegraph/fps checklist sits (`docs/notes/s42-results.md` §9). | ❌ human |

Be honest in the results note: the script proves **structure and level**, the user proves
**sound**. Do not let a green `s5-status` imply the latter.

### 4.4 Where `--s5-selftest` hooks

The branch chain is a flat if-ladder inside `run_app`: `s41Selftest` at `app.cpp:1783`,
`s42Selftest` at `:2637`, `s4Selftest` at `:3668`, `s3Selftest` at `:3939`, `selftest` at `:3990`.
A new block goes **after `s42`** (it reuses the same composed encounter, so it must run where the
entity, player, clips and `Encounter` are all live), i.e. a new `if (opts.s5Selftest) { … }`
immediately following the `s42` block's `return 0;` at `:3665`.

Wiring, in order:

1. `app.h:107` — add `bool s5Selftest = false;` beside `s42Selftest`, plus `std::string
   s5WavDir;` beside `s42StillsDir` (`app.h:120`).
2. `main.cpp:106-109` — add the `--s5-selftest` early-return branch (it takes no value).
3. `main.cpp:111-115` — add `--s5-wav` / `--audio-manifest` to `wants_value`.
4. `main.cpp:216-230` — add the `needs --manifest` guard and the `--s5-wav needs --s5-selftest`
   guard, mirroring `:221-225`.
5. `main.cpp:15-57` — usage lines.
6. `app.cpp` — extract the `s42` `step_frame` lambda (`:2695-2750`) into one shared helper, or
   copy it with the audio emit added. **Prefer extracting**: it is the third copy of the same
   loop (`:1822`, `:2695`, `:4295`) and the audio layer is the point where drift becomes audible.

**Timeline** (reuse the s42 phases, add audio assertions; `kS42*` constants are already pinned):

| Phase | Frames | Expect |
|---|---|---|
| session start | 0 | `music.encounter` starts, `amb.arena` starts |
| walk in | 60 | ≥ 2 footstep events, alternating L/R |
| grab | 1 | `player.grab` |
| charge + whiff | 60 | `player.strike` with `damageDealt=0`, no `strike_hit` |
| heavy strike → head | 60 | `player.strike_hit`, `valus.reaction` |
| stomp | until window | `valus.stomp` at start, `valus.stomp_impact` in [0.55, 0.70] of the clip (`valus.h:31-32`) |
| club | until window | `valus.club_swing` at start, `valus.club_impact` in [0.45, 0.65] (`valus.h:33-34`) |
| cancel | 1 landed strike mid-swing | `ClubCancel`, and **no** `club_impact` afterwards |
| hurt | stomp/club hit | `player.hurt` (heavy flag matches the attack) |
| death | lethal club | `player.death` |
| `R` | 1 | `ui.reset`, music restarts, footstep accumulator zeroed, no one-shot survives |
| loop seam | +2 s past the bed length | `s5-loop loops>=2` |

The **cancel** row is the most valuable test in the set: `apply_combat_events:1226-1228` cancels
the attack *before* the impact window is polled at `:4309-4314`, so a club-impact sound after a
cancel would prove the audio layer is reading state instead of consuming events.

### 4.5 Acceptance script

`scripts/s5-acceptance.sh`, cloned from `scripts/s42-acceptance.sh` (438 lines), with the frozen
rule respected: `s3`/`s4`/`s41`/`s42` scripts are **never edited**
(`AGENTS.md` "Acceptance"; `s42-acceptance.sh:30`). Steps:

1. build converter + app;
2. `dotnet test` + `ctest`;
3. `caffeinate -dimsu` `--s5-selftest --audio-null --s5-wav out/s5/` — parse every `s5-*` line,
   pin literals;
4. re-run **without** `--s5-wav` and require the `s5-evt` lines byte-identical (the stills rule
   from `s42-acceptance.sh:20-22`, applied to audio: *observation must not change the timeline*);
5. WAV assertions in bash: RIFF magic, `fmt ` chunk rate 48000, duration within 1 %, peak > 0;
6. `scripts/s42-acceptance.sh` unchanged as the regression;
7. summary block.

Every viewer run wrapped in `caffeinate -dimsu` (`AGENTS.md`; an unwrapped 1-frame run has taken
907 s). Never two acceptances concurrently.

---

## 5. Build

### 5.1 `app/CMakeLists.txt`

No new dependency is required for M5.

| Change | Where | Why |
|---|---|---|
| Add sources to `add_executable` | `:9-13` | `src/audio.cpp src/audio_mix.cpp src/audio_events.cpp` |
| Add the same sources to `sotc-app-tests` | `:38-42` | the pure logic must be unit-tested; the test target already links SDL3 (`:46`) |
| Add `tests/test_audio.cpp` | `:38` | doctest file |
| Nothing else | — | `SDL3::SDL3` already carries the audio symbols; `SDL3_ttf` already linked |

If SDL3_mixer is later adopted: `find_package(SDL3_mixer CONFIG REQUIRED)` beside `:5-6`, and
`SDL3_mixer::SDL3_mixer` on both targets.

Note `app/CMakeLists.txt:44-50`: `add_test(NAME windowed_selftest …)` pins `s1-status` output
with `PASS_REGULAR_EXPRESSION`. An audio init failure must not change that line, or the S1
regression breaks. Audio init therefore must be **non-fatal**: log one `stderr` line, set
`audio.available = false`, and keep every other status line identical.

### 5.2 .NET projects

| Project | Change |
|---|---|
| `src/BpConvert.Core/BpConvert.Core.csproj` | None. The audio folder is compiled in automatically (SDK globbing); **no new package** — WAV writing is 44 bytes of header plus the buffer, and the codec decode is in-repo. |
| `src/BpConvert.Cli/BpConvert.Cli.csproj` | None. |
| `tests/BpConvert.Tests` | None (new `AudioTests.cs`; xUnit already there). |
| `BpConvert.sln` | None. |

The one standing constraint from `AGENTS.md` that still applies: the vendored Oodle dylib copy in
`BpConvert.Core.csproj` must stay intact. Audio does not touch it.

### 5.3 Runtime contract to keep

- `out/library/audio/` is **gitignored** — `out/` already is (`AGENTS.md`, "Layout").
- Game-derived audio (WAVs, the manifest) is **never committed** (`AGENTS.md`, "Hard
  constraints"). The manifest contains only paths, names and numbers, but it is derived — treat it
  as untracked with the rest of `out/`.
- The viewer keeps working with **no** `audio.manifest`: every event degrades to silence with one
  informational line. The library is optional by construction (design §8, offline-first).

---

## 6. Open questions the plan must answer before coding

1. **Codec.** ATRAC9 or not? This gates everything else. Cheapest probe: pick one short
   `WAVE` entry, decode both ways, and require non-silence + a sane spectrum. Budget one
   timeboxed spike; if it fails, invoke D9's labelled capture-grade fallback explicitly.
2. **Sample format.** `WAVE.fmt = 0x00FF0221` — channel count and codec id are not yet separated.
   Resolve against `frames × channels × bytes` vs the recorded data length.
3. **Music scope.** `str_bgm.sxd2` is 514 MB. M5 should wire **one** encounter cue, extracted by
   name through `shadowstreams.crmp`, not a streaming system.
4. **Footstep surface.** The banks are surface-suffixed (`…_grass/_gravel/_stone/_wood`). M5
   should hard-code one surface (stone/canyon), not a surface query.
5. **Voice budget.** 15 events × overlapping one-shots; 16 voices is plenty. Pin it.
6. **Latency.** With `SDL_AudioStream` + `SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES`, ~1024 frames at
   48 kHz ≈ 21 ms. Acceptable for SFX; state it rather than discover it.

---

## 7. Anchor index (quick reference)

```
app/src/app.h              14-42 Encounter   49-61 reset_encounter   64-126 AppOptions
app/src/app.cpp            128 print_status_line        182 select_clip
                           844-863 update_combat_clips   872-897 player_frame_of
                           925-939 start_encounter_one_shot        941 advance_encounter_reaction
                           1027-1035 cancel_valus_attack           1042-1055 start_valus_attack
                           1063-1128 advance_valus_attack          1137-1177 apply_valus_impact
                           1184-1212 valus_attack_input            1221-1240 apply_combat_events
                           1245-1290 GamepadInput                  1427 SDL_Init
                           1783 s41Selftest   1822 s41 step_frame 2637 s42Selftest
                           2695-2750 s42 step_frame  3434-3435 s42 reset
                           3668 s4  3939 s3  3990 s1 selftest
                           4130-4168 R soft reset  4295-4314 interactive step
                           4340 set_hud
app/src/player_state.h     36-45 PlayerMode  56-78 PlayerCombat  90-98 PlayerFrame
                           103-127 CombatTuning  131-136 StepEvents  188 player_step
                           207 player_take_damage  215 reset_player_combat
app/src/player_state.cpp   119-136 detach   218 enter_hurt   228 enter_dead
                           260-499 player_step (288 grab, 373-393 strike, 397-417 grip,
                           421-467 fall/land, 470 stagger, 479 hurt, 490 dead)
                           501-534 player_take_damage   536-552 reset_player_combat
app/src/valus.h            18 ValusAttack  22-34 pinned ranges/windows
                           57 select  63 attack_impact_due  67 cooldown  72 cancelled  83 slew
app/src/player.h           13-21 PlayerInput  24-27 PlayerState  51-52 player_update
app/src/player.cpp         9-10 speeds  68 speed select
app/src/hud.h              13-18 HudState  23-33 bars
app/src/asset_io.h         21 load_scene  44 compose_scene  59 append_static_model
app/CMakeLists.txt         5-6 find_package  9-16 viewer  38-47 tests  44-50 s1 regex
app/src/main.cpp           15-57 usage  85-243 arg loop  111-115 wants_value  216-230 guards
src/BpConvert.Cli/Program.cs   11-23 verbs  25-37 usage  39 inspect  76 convert  203 report
src/BpConvert.Core/Pipeline/Catalog.cs  33 schema  51 FilePath  59 Load  107 Save
                                        150 Summarize  265-275 CatalogEntry
docs/superpowers/specs/2026-10-15-...design.md  79 (D9)  173 (audio risk)
scripts/s42-acceptance.sh  148 field()  199-232 status pins  57 manifest path
```

Assets (measured, `$SOTC_GAME_ROOT/sound/shadow/`):

```
SXDF header 0x00 magic/0x04 ver 4/0x08 size/0x10 hash/0x20 name        chunks from 0x60
boss01.sxd   TONE 630,  REQD 10,376, NAME 10,008 (343 names), WAVE 8,024, SEQD 41,976,
             LVAR/LVRN/RXFD/RXFN, DATA @0x14f10 15,881,928
music.sxd1   same chain, no DATA          music.sxd2  SXDS, hash 711e4c81…  (link)
com_sys.sxd  WAVE entry: rate 48000, frames 9830, fmt 0x00FF0221, data @0x18E04
sound/shadow/_cmn/shadow_sounds.cxml      8,001 <Sound name=…>
sound/shadow/_cmn/sound_data.calt         25 bank paths
sound/shadow/_cmn/shadowstreams.crmp      stream names (st_amb17, MIDDEMO_13, …)
du -sh sound                              1.9G
```
