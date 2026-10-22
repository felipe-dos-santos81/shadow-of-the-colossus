# S6 acceptance results (2026-10-22) — the fall

Spec milestone S6 (`docs/superpowers/specs/2026-10-22-s6-the-fall-design.md` §2): **"give the
first colossus the game's actual payoff: the death motion, the black tendrils erupting from the
wound, the collapse into the dead body, a cinematic camera, the death roar and the club whoosh,
and a stable encounter end the player can walk away from (or `R` reset)."** The fight no longer
ends in a freeze: Valus falls, the tendrils erupt, the camera takes over, and the corpse lies on
the arena floor while the control returns.

**Status: the app/converter acceptance is PASS (script) — the user's walk is PENDING.**
`scripts/s6-acceptance.sh` runs green end to end: both builds, both suites, the **frozen S5
acceptance first** (the eight curated clips, its 20 `s5-event`s and the unchanged S4.2 → S4.1 →
S4 → S3 regressions), the S6 decode (ten manifest entries, every file at 48 kHz and non-silent),
the deterministic `s6-*` timeline parsed field by field, five 2560×1440 stills with their
crop-local pixel guards, and the effect pass's frame cost at the burst. The milestone's
done-line is **not** claimed here: whether the tendrils look like the game's, whether the camera
reads as a cinematic, and whether the roar and the whoosh are the right sounds are the user's
eye and ear (§11).

| | |
|---|---|
| Date | 2026-10-22 (local −03; the recorded run is stamped `2026-10-22T19:38:02Z`–`19:49:57Z`) |
| Machine | Apple M3 Max, macOS 27.0.1 (26A434), arm64; 1280×720 points → 2560×1440 px captures |
| Repo / branch | `sotc-native`, worktree `s6-the-fall`; base `e4e04d6`, this note after it |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Library (gitignored) | `SOTC_LIB=<worktree>/out/library` (symlinked 12 GB library) |
| Cue map | `tools/re/audio/s6_map.json` (the S5 eight + `valus.death` + `valus.club_swing`) |
| Audio manifest (gitignored) | `out/library/audio/audio.manifest` — **10 entries**, names exactly the map's; sha256 `158c64614671348a9c66f9665e31fd7f79f9eff39200f1bb1d7ca833c137e6f5` |
| Encounter manifest (gitignored) | `out/re/stage/encounter.manifest` (the S4.0 composition, unchanged) |
| Acceptance script | **`scripts/s6-acceptance.sh`** (bash 3.2) |
| Run log (gitignored) | `out/s6-acceptance-run.log` — ends `s6-acceptance: PASS —` |
| S5 regression log (gitignored) | `out/s6-s5-regression.log` — ends `s5-acceptance: PASS —` |
| Captures (gitignored) | `out/s6/stills/` (four stills + the control) |

## 1 · The acceptance run

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export SOTC_LIB=<worktree>/out/library
caffeinate -dimsu ./scripts/s6-acceptance.sh   # exit 0; everything below is from
                                                # out/s6-acceptance-run.log
```

The script's steps (the viewer and acceptance runs under `caffeinate -dimsu`; the builds and the
suites are plain commands). The recorded run took **11 min 55 s** end to end, of which the
frozen S5 acceptance (with its nested S4.2 → S4.1 → S4 → S3 chain) is 9 min 32 s:

- **Builds.** `dotnet build BpConvert.sln -v q`, `dotnet publish src/BpConvert.Cli -c Release -o
  out/bin`, and the app `cmake -DCMAKE_BUILD_TYPE=Release` + `cmake --build` (both targets).
- **Suites.** `dotnet test` **146 passed, 0 failed** (1 m 31 s); `ctest` **2/2** (13.55 s unit,
  0.26 s windowed; 13.81 s total). §8.
- **The frozen S5 acceptance first** (design D8 / ruling R4): `scripts/s5-acceptance.sh` ran
  unchanged and printed its own `PASS —`; it regenerates the eight-entry manifest, asserts its
  20 `s5-event`s and runs the S4.2 → S4.1 → S4 → S3 regressions. §7.- **The S6 decode.** `bp-convert audio … --map tools/re/audio/s6_map.json --force` wrote
  **10/10** cues and the manifest: `audio: 10 written, 0 reused, 10 manifest entries`. Every file
  is 48 kHz; the manifest's names are exactly the map's `cues`; the channel count and the
  duration match ffprobe; and every cue passes its own `astats` band (RMS −20.9…−8.7 dBFS, peak
  −5.7…−0.09 dBFS). §6.
- **The death timeline.** `--s6-selftest --s6-stills out/s6/stills` exited 0 with **13** `s6-*`
  lines: the four phases, the five events, the perf line, the death status, the status and the
  reset. §2.
- **The stills.** Five 2560×1440 PNGs (four milestones plus the control), each guarded by the
  PNG magic, > 5000 B, the exact size and the viewer's own non-uniformity span. §4.
- **The pixel guards.** The burst against its control, the corpse's veins on the sand, the two
  framings and the HUD on every still — all crop-local. §4.
- **The effect pass's cost.** The `s6-perf` line's 240-frame pair at the burst. §5.
- **Summary.** The script's final line is the block in §1.1, verbatim.

### 1.1 The summary line

```text
s6-acceptance: PASS — build, both suites, the frozen S5 acceptance green (8 curated clips, 20 s5-events, the unchanged S4.2 render path at 9.15 ms/frame, S4.2+S4.1+S4+S3 regressions green), the S6 decode at 48 kHz (10 manifest entries: the S5 eight plus valus.death and valus.club_swing; every file present, every cue non-silent at peak <= 0.99), the S6 death timeline (Idle -> Motion at 0.000 s locked with 2 roots -> Collapse at 6.183 s with the corpse -> Done at 8.000 s unlocked; 5 s6-events: the club telegraph, the whoosh at 0.300 s, the score at the defeat and its stop, the death roar at 0.400 s; health 0, camera back to follow; the reset restores Idle/100/100), five stills (2560x1440, span >= 30, > 5000 B) and the crop-local pixel guards (the burst 14599 changed pixels against its control, 1766 violet and 258 strong, inside (600,0)-(1800,1150); the corpse 15449 vein pixels on 32527 bright sand pixels; the pull-back 101960 floor pixels and 27286 body pixels at the top edge; the motion 698840 floor pixels, 0 at the top edge and 500826 body pixels; the HUD on all four stills: Valus 0 red, stamina and player bars present), the effect pass at 8.895 ms/frame (marginal 0.113 ms) and the windowed, cursor-free status
```

The individual steps, for reproduction (the script is the source of truth):

```bash
dotnet build BpConvert.sln -v q
dotnet publish src/BpConvert.Cli -c Release -o out/bin
cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release && cmake --build app/build -j
dotnet test tests/BpConvert.Tests -v q
ctest --test-dir app/build --output-on-failure
caffeinate -dimsu ./scripts/s5-acceptance.sh    # the frozen S5 + S4.2 + S4.1 + S4 + S3
caffeinate -dimsu dotnet exec out/bin/bp-convert.dll audio --root "$SOTC_GAME_ROOT" \
    --out "$SOTC_LIB" --map tools/re/audio/s6_map.json --force
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
    --manifest out/re/stage/encounter.manifest \
    --model models/shadow/characters/wander/_cmn/wander.glb \
    --s6-selftest --s6-stills out/s6/stills
```

Every viewer and acceptance run is wrapped in `caffeinate -dimsu` (the machine suspends idle
background work; an unwrapped 1-frame run has been observed at 907 s). The script deletes
`out/s6/stills/` and its own captures before the run, so no stale still can mask a failed write.

## 2 · The death timeline

`--s6-selftest` composes the encounter manifest, drives one club attack on the production path
(so the whoosh fires through the real `advance_valus_attack`), applies a scripted lethal event
through the production `apply_combat_events` (ruling R6 — the real three-strike defeat stays the
frozen S4.1 acceptance), and then runs the death machine to `Done` through fixed 1/60 s steps.
The `s6-*` lines, verbatim:

```text
s6-phase phase=Idle t=0.000 total=0.000 lock=0 corpse=0 roots=0
s6-event t=0.000 event=club_start clip=valus.telegraph gain=0.60
s6-event t=0.300 event=club_swing clip=valus.club_swing gain=0.70
s6-event t=0.000 event=music_start clip=music.boss gain=0.45
s6-event t=0.000 event=music_stop clip=music.boss gain=0.00
s6-phase phase=Motion t=0.000 total=0.000 lock=1 corpse=0 roots=2
s6-event t=0.400 event=valus_defeat clip=valus.death gain=0.80
s6-phase phase=Collapse t=0.016 total=6.183 lock=1 corpse=1 roots=2
s6-phase phase=Done t=0.000 total=8.000 lock=0 corpse=1 roots=2
s6-death clip=minotaur_a_death_front total=8.000 corpse=1 health=0 camera=follow
s6-status defeated=1 corpse=1 roots=2 player_dead=0
s6-reset phase=Idle corpse=0 lock=0 valus=100 player=100
```

plus the perf line (§5):

```text
s6-perf frames=240 effect_ms=2134.684 base_ms=2107.470 marginal_ms=0.113 frame_ms=8.895
```

What the acceptance pins, beyond the count:

- **The phase order** is `Idle → Motion → Collapse → Done`, and every line's flags are read from
  the line, not assumed.
- **Idle** is unlocked, no corpse, no roots.
- **Motion** starts at `total = 0.000` with the input **locked** and **2 roots**; the death clip
  is driven by the machine's clock (`clipTime = min(death.total, kMotionSeconds)`), so the
  clip and the phase cannot drift apart.
- **Collapse** lands at **6.183 s** — the death clip's own end (`kMotionSeconds = 6.166877 s`,
  93 / 15.08056640625 s) plus the step's overshoot — with the corpse **visible** and the input
  still locked. The acceptance window is 6.15–6.20 s.
- **Done** at **8.000 s**, the input **unlocked** (the control returns), the corpse still there
  and the two roots still resolved.
- **The death cue is the burst's cue**: `valus_defeat` at **t = 0.400 s** on the death clock,
  which is the tendril burst's start (`kDeathTendrilStart = 0.4 s`), playing `valus.death` at
  gain 0.80.
- **The whoosh is the swing's cue**: `club_swing` at **t = 0.300 s**
  (`kValusClubSwingSeconds = 0.30 s`), playing `valus.club_swing` at gain 0.70 — the swing
  phase, not the telegraph (the telegraph keeps `valus.telegraph`).
- **The score starts and stops on the same step** (the defeat is the downbeat, and the defeat
  stops the score). The acceptance requires the two timestamps to be equal.
- **Every `clip=` is a manifest name** (R16): the five clips are checked against the ten-entry
  manifest, and a `bank/name` form is rejected.
- **`s6-death`** names the clip (`minotaur_a_death_front`), the total (8.000), the corpse (1),
  the health (**0**) and the camera key (**follow**: the camera is handed back).
- **`s6-status`** is the post-death state: `defeated=1 corpse=1 roots=2 player_dead=0` — the
  player is alive and the dead colossus is a prop.
- **`s6-reset`** restores the opening state: `phase=Idle corpse=0 lock=0 valus=100 player=100`.
  The selftest also checks the *draw gate* behind those flags (the live entity and the club
  visible, the corpse's range hidden) and that the dead colossus has no collision capsules, no
  climb joints and no weakpoint proxies left (the S6 review's Important item).
- **The window stays windowed and the cursor stays free**: `s1-status windowed=1
  relative_mouse=0 mouse_auto_capture=0`.

## 3 · The tendrils — a labeled reconstruction

**What is the game's own.** The textures, the emitter roles and the ribbon's geometry:

| part | value | source |
|---|---:|---|
| ribbon stations per root | **201** (200 segments) | `CreateDeathTentacle`'s 201-segment array (binary `0x008424d0`) |
| station spacing | **0.25 m** | the binary's `+0x18 = 0x3e800000` |
| ribbon vertices per root | 402 (a triangle strip) | the app's `kTendrilRibbonVertices` |
| emitters | **3** — `main_wisp`, `smoke`, `veins` | `death_tentacle.cprt` |
| smoke billboards | 24 on the ribbon (the `2x2` atlas) + 16 wisp puffs at the root (the two `8x8` atlases) | the design's emitter roles |
| textures | **4** — `8x8_wispysmoke02_1024_white_pfx`, `8x8_smoke_wispy_sharp_1024_pfx`, `2x2_smoke_tendrils_01_pfx`, `death_tentacle_veins` | the library's converted PNGs |
| roots | 2 — the hit weakpoint first (the forehead by default, the right arm on an arm hit), then the other weakpoint shell | the binary's one-tendril-per-hit-point list, the weakpoint shells as the fallback |

**What is authored, and labelled as such.** The `.csdr` GCN particle evaluator is **not**
ported (design D1): the curl, the turbulence and the growth are the builder's reconstruction,
and the note says so. The same is true of the camera (D2): the shipped
`camera_minoa_death_front_camera_01/02.anb` clips are **type 36** (undecoded) and are not
played; the five-key path (follow → 12 m pull-back 4 m up → low angle at 3.0 s → wide on the
corpse at 6.2 s → follow at 8.0 s) is authored.

**Known deviations, recorded.** The vein mask is sampled with `u` across the ribbon's width and
`v` along its path, which is about **100:1 anisotropic** — the veins read as fine cross-ribs
rather than the game's own stretch. There is **no per-segment stagger**: the growth is a single
front over 1.2 s (the design's per-segment stagger is a builder deviation). The dead body's
veins are a black **alpha veil** (ruling R10), not a glow: the glTF `emissiveTexture` carries
it and the mesh shader applies it as a multiplicative dark veil.

## 4 · The stills and the pixel guards

All five captures are 2560×1440, written by `--s6-stills` with the HUD visible, and each is
guarded by the PNG magic, > 5000 B, the exact size and the viewer's own non-uniformity span
(the largest channel span from its `s1-pixels` line). Stale PNGs are deleted with
`out/s6/stills/` before the run.

| file (`out/s6/stills/`) | bytes | span | what it shows |
|---|---:|---:|---|
| `motion.png` | 10,388,483 | 254 | 0.200 s — the death motion's first beat, the whole body from the follow pose, the arena floor below |
| `camera.png` | 8,531,937 | 255 | 0.400 s — the pull-back key (12 m out, the eye 4 m above the base, pitched up at the body's midpoint) |
| `tendrils.png` | 8,085,319 | 254 | 1.200 s — the burst: the violet vein ribbon and the smoke over the body, with both roots in frame |
| `corpse.png` | 11,587,297 | 255 | 6.200 s — the corpse wide: the dirtmound and the dead body with its black veins on the sand |
| `tendrils-control.png` | 8,061,854 | 254 | the same frame as `tendrils.png` with the effect pass **suppressed** — the control the burst is differenced against |

The pixel guards are **crop-local**: a whole-frame count would be noise in a scene this dark
(the shadowed snow is bluish), so every check reads a named rectangle. The thresholds are the
measured values with a wide margin; the script asserts the measured truth, not an aspiration.

### 4.1 The burst against its control

`tendrils.png` and `tendrils-control.png` are the same frame (the same pose, the same camera,
the same HUD) with and without the effect pass, so their difference is **exactly** the tendrils'
own pixels:

| measurement | measured | gate |
|---|---:|---:|
| changed pixels | **14 599** | ≥ 8 000 |
| the changed pixels' bounding box | **719,0 – 1515,1048** | inside (600,0) – (1800,1150) |
| violet pixels among them (`b > r + 8`) | **1 766** | ≥ 800 |
| strongly violet among them (`b > r + 15`) | **258** | ≥ 100 |

The two clusters are the two roots: the forehead's tendril rises out of the top of the frame and
the right arm's curls at the lower left. The Task 5 hand-over suggested a wound crop of
x 1100–2100, y 100–850 for this still — a visual estimate against a different framing; the
**measured** footprint (the control diff, not an estimate) sits at x 719–1515, y 0–1048, so the
acceptance gates the region the pixels are actually in. A dead effect pass would change 0 pixels
and fail the first gate.

### 4.2 The corpse

The dead body lies on the bright sand of the wide shot; the crop (1000,600)–(1900,950) reads:

| measurement | measured | gate |
|---|---:|---:|
| near-black pixels (luma < 25) — the vein stripes | **15 449** | ≥ 10 000 |
| bright sand pixels (luma > 150) around them | **32 527** | ≥ 20 000 |

The second number is what makes the first specific: in the three earlier stills the same crop
carries 24–4 658 bright pixels (rocks, shadow and the live rig's fur), so "black stripes on
bright sand" is the corpse's signature and the live rig's is not.

### 4.3 The two framings

| still | measurement | measured | gate |
|---|---|---:|---:|
| `camera.png` (the pull-back, low and looking up) | the arena floor in the bottom band (0,1150)–(2559,1439) | **101 960** (13.7 % of the band) | ≤ 185 600 (25 %) |
| `camera.png` | body pixels in the top band (1100,0)–(1500,80) | **27 286** | ≥ 20 000 |
| `motion.png` (the follow pose, looking down) | the arena floor in the bottom band | **698 840** (94.1 %) | ≥ 593 920 (80 %) |
| `motion.png` | body pixels in the top band | **0** | ≤ 5 000 |
| `motion.png` | body pixels in (900,250)–(1699,1199) | **500 826** | ≥ 300 000 |

Together: the pull-back's camera is low, looks up at the body (the body reaches the top edge of
the frame and the floor is barely in it) and the motion's camera is the follow pose (the floor
fills the bottom, the body is whole in the frame and the sky is above it). The `motion.png`
numbers are also the "the body is on screen" guard — a camera that lost the entity would empty
the body crop.

### 4.4 The HUD

The HUD is authored on a 1280×720 frame, so the pixel boxes are doubled. On **all four** stills:

| bar | box (design → pixels) | measured | gate |
|---|---|---:|---:|
| Valus's health (red) — **hidden** after the defeat | (340,24)–(940,32) | **0** red px | = 0 |
| the stamina (green) | (24,684)–(264,696) | **11 520** green px | ≥ 500 |
| the player's health (amber) | (24,652)–(264,660) | **7 680** amber px | ≥ 500 |

Valus's bar is absent because his health is 0 (the bar's width is the fraction), and the
player's two bars are present — so the HUD is drawn and only Valus's bar is gone.

## 5 · Performance — the effect pass

The burst is the milestone's first per-frame dynamic geometry: two 402-vertex ribbons and 40
billboards are rebuilt and uploaded every frame. The `s6-perf` line times the frame at the burst
with a 240-frame pair — the same shape as the S4.1/S4.2 render gates: 240 frames with the pass
drawing and 240 with it suppressed from the same pose and camera.

| run | measured |
|---|---:|
| 240 frames with the pass (`effect_ms`) | **2 134.684 ms** |
| 240 frames without it (`base_ms`) | **2 107.470 ms** |
| the frame at the tendril phase (`frame_ms`) | **8.895 ms/frame** (≈ 112 fps) |
| the pass's marginal (`marginal_ms`) | **+0.113 ms/frame** |

The gate is **≤ 16.7 ms** (60 fps) on both the frame and the marginal. The marginal is at the
measurement's noise floor (two 240-frame runs differ by ±50 ms, i.e. ±0.2 ms/frame — an earlier
iteration of the same pair measured −0.218 ms), so the
honest reading is that **the tendril pass is free inside the frame budget** — it is a few
hundred vertices and forty billboards over a 2560×1440 target. The absolute frame cost is the
number to watch: 8.9 ms against the 16.7 ms budget, with the scene's own render (the 30-glb
arena, 165 textures and the 38 MB Valus glb) inside it.

The timed loop **renders only** — it does not advance the mixer — so the number is the render
path's, not an audio-on frame (the S5 note's open item). The frozen **silent** render path is
still the render regression: the S5 acceptance's nested
S4.2 pair measured **9.15 ms/frame (~109.3 fps)** in the same session (§7).

## 6 · The audio

`bp-convert audio --map tools/re/audio/s6_map.json --force` wrote **10/10** cues
(`audio: 10 written, 0 reused, 10 manifest entries`); the manifest's names are exactly the map's
ten `cues`, every file is present under the library at 48 kHz, and every cue passes its own
`astats` band (RMS inside (−40, 0) dBFS and peak ≤ −0.08 dBFS, i.e. ≤ 0.99), so one quiet cue
fails the pass by itself:

| event | bank | seconds | ch | RMS | peak | method |
|---|---|---:|---:|---:|---:|---|
| `valus.telegraph` | boss01 | 1.201667 | 1 | −15.70 dB | −0.09 dB | audition |
| `valus.impact` | boss01 | 8.153250 | 2 | −20.86 dB | −5.72 dB | ghidra |
| `valus.hurt` | boss01 | 1.051167 | 1 | −12.46 dB | −0.09 dB | audition |
| `valus.death` | boss01 | 5.278000 | 1 | −13.71 dB | −0.09 dB | audition |
| `valus.club_swing` | boss01 | 3.801583 | 1 | −17.39 dB | −1.19 dB | audition |
| `player.swing` | com_obj | 0.464917 | 1 | −10.32 dB | −0.09 dB | audition |
| `player.hit` | com_obj | 0.708750 | 1 | −12.29 dB | −0.09 dB | audition |
| `player.hurt` | com_v | 0.270083 | 1 | −8.67 dB | −0.09 dB | audition |
| `player.death` | com_b_h | 0.812000 | 1 | −15.46 dB | −0.09 dB | audition |
| `music.boss` | music | 125.930667 | 2 | −18.79 dB | −0.09 dB | table |

The two new rows are **audition** rows (ruling R9): the manual chains are high identity and
medium fidelity, but the `SEQD` bytecode of their voices has no `PLAY_TONE` marker the walker
can follow, so they are not chain-proven. Their identity is the user's ear (§11).

The S5 slice stays frozen: the S5 map, the S5 acceptance and the eight-entry manifest the S5
acceptance asserts are untouched; the S6 script runs that acceptance **first** (design D8 /
ruling R4) and only then overwrites the manifest with the ten entries.

## 7 · The frozen regressions

`scripts/s5-acceptance.sh` is unmodified by this task and ran green under `caffeinate -dimsu`
(log `out/s6-s5-regression.log`), which is also the S4.2 → S4.1 → S4 → S3 chain. Its final line:

```text
s5-acceptance: PASS — build, both suites, 8 curated clips (7 attributed cues + Boss 1) decoded at 48 kHz (manifest names = map events, every file present, every cue non-silent at peak <= 0.99), 20 ordered s5-events (music on the first landed hit at 2.050 s, telegraph before each of the two stomp/club cycles, hurt light/heavy at their impacts, death at 19.367 s, music stop at the reset 20.483 s, R18 defeat stop after the reset), mixdown 2140000 frames / 44.583333 s pcm_f32le 2 ch (rms 0.0514, peak 0.9022, window_rms 0.1520; stomp window -15.408139 dBFS is 15.1 dB above the music-only window -30.509470 dBFS; astats peak -0.894302 dBFS, RMS -25.779484 dBFS, no NaN/Inf), s42-* identical with no audio manifest loaded, marginal 9.15 ms/frame on the unchanged S4.2 render path (the audio-on frame cost is not measured), S4.2+S4.1+S4+S3 regressions green
```

The nested statuses, unchanged:

```text
s42-acceptance: PASS — … (the S4.2 acceptance, inside the S5 run)
s41-status grab=left_foot climb=4.77 strikes=3 entity_health=0 mode=grounded defeated=1
s4-status instances=30 rigs=2 entity=minotaur_a_stand placement=builder-pick spawn=data-position+builder-yaw feet=ok entity_bbox=993,237,1485,751 pos=378.187,28.430,369.028 frames=423
s3-status instances=30 spawn=309.312,4.394,285.403 pos=309.312,29.802,305.405 ground=ok frames=240
```

The S6 app change is inside the `--s6-selftest` branch only, so the frozen timelines cannot
move; the identity was checked directly as well: `--s42-selftest` (audio off) printed its 11
`s42-*` lines **byte-identical** to the stored run, and `--s5-selftest` printed its 20
`s5-event` lines **byte-identical** to `out/s5/events.txt`.

## 8 · Suites

- `dotnet build BpConvert.sln -v q`: clean.
- `dotnet publish src/BpConvert.Cli -c Release -o out/bin`: clean.
- `dotnet test tests/BpConvert.Tests -v q`: **146 passed**, 0 failed (1 m 31 s).
- `ctest --test-dir app/build --output-on-failure`: **2/2 passed** (unit 13.55 s, windowed
  0.26 s; total 13.81 s). The app's suite is **275 cases / 135 027 assertions**.

## 9 · Provenance and rulings

- **R6 — the selftest's defeat is a scripted lethal** `apply_combat_events`, not the full
  three-strike climb; the real defeat stays the frozen S4.1 acceptance. §2.
- **R7 — `DeathState.roots = 2`** on the defeat (the two weakpoints); the effect frame carries
  the real root count.
- **R8 — the Motion→Collapse boundary is the runtime clip's duration** (6.166877 s, the baked
  `minotaur_a_death_front` sampler max), not the design's 94-frame 6.233 s.
- **R9 — `valus.death` and `valus.club_swing` are audition rows**, not chain-verified. §6.
- **R10 — the veins texture is a black alpha veil, not a glow**: the glTF `emissiveTexture`
  carries it, the `emissiveFactor` stays `[0,0,0]` and the mesh shader applies
  `mix(1, rgb, alpha)` as a multiplicative dark veil.
- **D8 / R4 — the frozen S5 acceptance runs first**, then the S6 decode. §7.
- **D1 / D2 — the tendrils and the camera are reconstructions**, labelled as such. §3.
- **The asset gate**: `docs/notes/s6-recon.md` (the 13 assets, the four PNGs' SHA-256s, the
  dead body's 6/6/0 shape).
- **The maps**: `tools/re/audio/s5_map.json` (frozen) and `tools/re/audio/s6_map.json` (the ten
  cues).

## 10 · Known limitations

- **The tendrils are authored.** The `.csdr` simulator is not ported; the shape, the curl and
  the timing of the burst are the builder's. The user's eye is the check.
- **The camera is authored.** The type-36 death-camera clips are undecoded and unused.
- **No ragdoll, no per-segment stagger, an anisotropic vein mask.** §3.
- **The burst is subtle at the scripted framing.** The measured footprint is 14 599 changed
  pixels (the violet ribbon 1 766); the shape and the colour are the eye check.
- **The interactive death path is not driven by an automated run** — a real defeat through
  gameplay is needed. Every helper the interactive loop calls (`step_death`,
  `camera_death_pose`, `apply_death_visibility`, `build_effect_frame`, `death_cue_due`,
  `maybe_club_swing`) is driven by `--s6-selftest`, and the loop's call sites were reviewed and
  smoke-run, but the *interactive* path is the user's walk.
- **The death camera's pull-back is a push-in in practice.** The design's key is 12 m from the
  entity, while the follow camera sits ≈94 m away (the player's spawn to the entity), so at
  0.4 s the camera moves *closer* to Valus than the follow pose was, not further. The framing
  reads (the low angle, the body cut off at the top) and the pixel guards measure it.
- **The audio-on frame cost is still unmeasured** (the S5 note's open item): the S6 perf pair
  times the render only (the timed loop does not advance the mixer), so the numbers above are
  the render path's.

## 11 · The user's walk — PENDING

The scripted pass cannot judge the tendrils' look, the camera's read, the roar's identity or the
feel of the locked input. Run the encounter from the repo root, defeat Valus (the forehead or
the right arm) and fill the cells:

```bash
export SOTC_LIB=<worktree>/out/library
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
    --manifest out/re/stage/encounter.manifest \
    --model models/shadow/characters/wander/_cmn/wander.glb
```

| # | check | do this | expected | result |
|---|---|---|---|---|
| 1 | the death motion | land a killing blow on the forehead or the right arm | Valus plays his fall once (no freeze on the last frame) and the camera leaves the follow pose | ☐ |
| 2 | the tendrils | watch the burst from 0.4 s to 3.0 s | black tendrils erupt from the wound you struck, with the game's violet veins and smoke | ☐ |
| 3 | the tendrils' look | compare the burst with the still (`out/s6/stills/tendrils.png`) | the silhouette and the colour read as the game's tendrils, not as generic particles | ☐ |
| 4 | the collapse | watch from 6 s | the live colossus becomes the dead body on its dirtmound, with the black veins | ☐ |
| 5 | the camera | watch the whole sequence | the pull-back frames the body from below, the low angle looks up at the tendrils, the wide shows the corpse, and the follow pose returns | ☐ |
| 6 | the control lock | try to move and strike during the motion and the collapse | the input is locked; `R`, `M` and `Escape` still work | ☐ |
| 7 | the death roar and the whoosh | defeat him; watch the burst, and let a club swing land first | the roar plays with the burst (0.4 s), the whoosh with the swing | ☐ |
| 8 | the music stop | defeat him | the score stops at the defeat | ☐ |
| 9 | the end state | after 8 s | the control returns, the corpse stays, Valus's bar is gone and the enemy's proxies are dead (no climbing, no striking) | ☐ |
| 10 | `R` reset | press `R` during or after the sequence | the encounter is back to full: Valus alive and standing, the corpse gone, the player whole | ☐ |
| 11 | no clipping, windowed, cursor | move the mouse over the window; watch the title | a 1280×720 window, the cursor free, no crackle over the roar, the fps in the ≥ 60 class | ☐ |
| 12 | `M` mute | press `M` during the cinematic | the whole mix mutes and unmutes with no pop | ☐ |

**Deferred — do not mark as failures:** the exact `.csdr` particle motion, the type-36 camera
clips, the ragdoll, the player's fall, the ambience, the grab/climb/footstep sounds.

**Verdict: PENDING — the user's eye and ear.** S6's done-line closes only when this table is
filled and passes; nothing in §1–§10 claims it.

## 12 · Evidence files (all gitignored)

- `out/s6-acceptance-run.log` — the recorded run's full output (ends with the PASS line).
- `out/s6-s5-regression.log` — the frozen S5 acceptance's full output, ending `PASS —`.
- `out/s6/s6-selftest.txt` — the captured `--s6-selftest` output (the 13 `s6-*` lines).
- `out/s6/s6-pixels.py` + `out/s6/s6-pixels.txt` — the pixel helper and its output.
- `out/s6/stills/` — `motion.png`, `camera.png`, `tendrils.png`, `corpse.png` and
  `tendrils-control.png`.
- `out/s6/manifest-names.txt`, `map-names.txt`, `manifest-entries.txt`, `event-clips.txt` — the
  parsed manifest/map/event sets.
- `out/library/audio/audio.manifest` + `out/library/audio/<bank>/*.wav` — the shipped ten.

## 13 · Cross-references

- Design: `docs/superpowers/specs/2026-10-22-s6-the-fall-design.md` (§2 goals, §6 evidence, §7
  decisions D1–D9, §8 the design, §9 the builder's calls, §11 the walk).
- Plan: `docs/superpowers/plans/2026-10-22-s6-the-fall.md`; rulings R1–R10 in
  `.superpowers/sdd/2026-10-22-s6-the-fall/progress.md`.
- Asset gate: `docs/notes/s6-recon.md`.
- Task records: `.superpowers/sdd/2026-10-22-s6-the-fall/task-*-report.md` (this one is Task 6).
- S5 results: `docs/notes/s5-results.md`; S4.2: `s42-results.md`; S4.1: `s41-results.md`; S4:
  `s4-results.md`; S3: `s3-results.md`.
- Script: `scripts/s6-acceptance.sh` (this pass); the frozen regressions are
  `scripts/s5-acceptance.sh`, `scripts/s42-acceptance.sh`, `scripts/s41-acceptance.sh`,
  `scripts/s4-acceptance.sh`, `scripts/s3-acceptance.sh`.
