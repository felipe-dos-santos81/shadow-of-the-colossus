# S5 acceptance results (2026-10-21) — the sound of the forbidden land

Spec milestone S5 (`docs/superpowers/specs/2026-10-21-s5-audio-design.md` §2): **"decode the
game's own audio at build time, play it from the existing simulation through SDL3, and prove the
result by a deterministic headless mixdown plus the user's ears."** The fight now has its own
voice: the seven attributed combat cues plus `Boss 1`, decoded from the shipped `SXDF`/`SXDS`
banks into 48 kHz WAV and mixed by the viewer.

**Status: the app/converter acceptance is PASS (script) — the user's listening walk is PENDING.**
`scripts/s5-acceptance.sh` runs green end to end: both builds, both suites, the curated decode
(8 manifest entries, every WAV present at 48 kHz), the deterministic `s5-*` timeline parsed field
by field, the mixdown checked by the viewer and independently by `ffmpeg astats`, the `s42-*`
lines proven identical with no audio subsystem constructed, and the unchanged S4.2 acceptance
(which runs S4.1 → S4 → S3). The milestone's done-line is **not** claimed here: whether a cue is
the *right* sound, the loop point, the swing/hit sync, `M`, clipping and the fps are the user's
ear (§12).

| | |
|---|---|
| Date | 2026-10-21 (local −03; the recorded run is stamped `2026-10-22T01:19:16Z`–`01:28:09Z`) |
| Machine | Apple M3 Max, macOS 27.0.1 (26A434), arm64; 1280×720 points → 2560×1440 px captures |
| Repo / branch | `sotc-native`, worktree `s5-audio` |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Library (gitignored) | `SOTC_LIB=<worktree>/out/library` (symlinked 12 GB library) |
| Cue map | `tools/re/audio/s5_map.json` (version 3) |
| Audio manifest (gitignored) | `out/library/audio/audio.manifest` — **8 entries**, names exactly the map's |
| Encounter manifest (gitignored) | `out/re/stage/encounter.manifest` (the S4.0 composition, unchanged) |
| Mixdown (gitignored) | `out/s5/mix.wav` — pcm_f32le, 48 kHz stereo, 2,139,200 frames (44.5667 s) |
| Acceptance script | **`scripts/s5-acceptance.sh`** (bash 3.2) |
| Run log (gitignored) | `out/s5-acceptance-run.log` — ends `s5-acceptance: PASS —` |
| S4.2 regression log (gitignored) | `out/s5-s42-regression.log` — ends `s42-acceptance: PASS —` |

## 1 · The acceptance run

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
export SOTC_LIB=<worktree>/out/library
caffeinate -dimsu ./scripts/s5-acceptance.sh   # exit 0; everything below is from
                                               # out/s5-acceptance-run.log
```

The recorded run is stamped **`2026-10-22T01:19:16Z` → `01:28:09Z`** (8 min 53 s; the S4.2
regression alone is ~7 min). Its steps (the viewer and acceptance runs under `caffeinate -dimsu`;
the builds and suites are plain commands):

- **Builds.** `dotnet build BpConvert.sln -v q` (clean, 0 warnings / 0 errors), `dotnet publish
  src/BpConvert.Cli -c Release -o out/bin`, and the app `cmake -DCMAKE_BUILD_TYPE=Release` +
  `cmake --build` (both targets up to date).
- **Suites.** `dotnet test` **140 passed, 0 failed** (50 s); `ctest` **2/2** (10.73 s unit,
  0.21 s windowed; 10.94 s total). §9.
- **Decode.** `bp-convert audio … --force` wrote **8/8** cues and the manifest:
  `audio: 8 written, 0 reused, 8 manifest entries`. Every file is 48 kHz; the manifest's names
  are exactly the map's `cues`; the channel count and duration match ffprobe. §4.
- **Timeline.** `--s5-selftest --audio-mixdown out/s5/mix.wav` exited 0 with **18** well-formed
  `s5-event` lines, `s5-mix` and `s5-status` last. §5–§6.
- **Mixdown.** pcm_f32le, 48 kHz, stereo, 2,139,200 frames (44.5667 s), rms 0.0514, peak 0.9022,
  loudest-window rms 0.1520; `ffmpeg astats` Overall peak −0.894 dBFS, RMS −25.778 dBFS, 0 NaNs,
  0 Infs; the music-only window −30.509 dBFS and the stomp window −15.408 dBFS (15.1 dB apart).
  §6.
- **Identity.** `--s42-selftest` with no audio subsystem constructed: its **11** `s42-*` lines
  are byte-identical to the S5 run's (empty diff). §7.
- **Regression.** the unchanged `scripts/s42-acceptance.sh` printed
  `s42-acceptance: PASS — …` and a marginal of **9.25 ms/frame (~108.1 fps)**, under the 16.7 ms
  gate; S4.1/S4/S3 green. §8.
- **Summary.** the script's final line is the block below, verbatim.

```text
s5-acceptance: PASS — build, both suites, 8 curated clips (7 attributed cues + Boss 1) decoded at 48 kHz (manifest names = map events, every file present), 18 ordered s5-events (music on the first landed hit at 2.050 s, telegraph before each of the two stomp/club cycles, hurt light/heavy at their impacts, death at 19.367 s, music stop at the reset 20.483 s), mixdown 2139200 frames / 44.566667 s pcm_f32le 2 ch (rms 0.0514, peak 0.9022, window_rms 0.1520; stomp window -15.408139 dBFS is 15.1 dB above the music-only window -30.509470 dBFS; astats peak -0.894302 dBFS, RMS -25.777860 dBFS, no NaN/Inf), s42-* identical with audio off, marginal 9.25 ms/frame on the unchanged S4.2 render path (the audio-on frame cost is not measured), S4.2+S4.1+S4+S3 regressions green
```

Artifact hashes from the run: the audio manifest sha256 is
`7ce2ea6c9a46a8bce68f192b9c9b165f51b47f3296a3af12dc9252be6064e117`; the mixdown sha256 is
`612a66d9f9a6075a9f9107ec5dd84e62286d7c41342bdffef7e889136457f023`.

The individual steps, for reproduction (the script is the source of truth):

```bash
dotnet build BpConvert.sln -v q
dotnet publish src/BpConvert.Cli -c Release -o out/bin
cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release && cmake --build app/build -j
dotnet test tests/BpConvert.Tests -v q
ctest --test-dir app/build --output-on-failure
caffeinate -dimsu dotnet exec out/bin/bp-convert.dll audio --root "$SOTC_GAME_ROOT" \
    --out "$SOTC_LIB" --map tools/re/audio/s5_map.json --force
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
    --manifest out/re/stage/encounter.manifest \
    --model models/shadow/characters/wander/_cmn/wander.glb \
    --s5-selftest --audio-mixdown out/s5/mix.wav
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
    --manifest out/re/stage/encounter.manifest \
    --model models/shadow/characters/wander/_cmn/wander.glb --s42-selftest   # audio off
caffeinate -dimsu ./scripts/s42-acceptance.sh   # the S4.2 + S4.1 + S4 + S3 regressions
```

Every viewer and acceptance run is wrapped in `caffeinate -dimsu` (the machine suspends idle
background work; an unwrapped 1-frame run has been observed at 907 s). The script deletes the
acceptance's own `out/s5/` artifacts before the decode, so no stale `mix.wav` can mask a failed
write; the gate/attribution evidence (`out/s5/gate/`, `out/s5/audition/`) is another task's and
is left alone.

## 2 · The decode gate

**Container.** `sound/shadow/` is 42 files / 1.9 GB of `SXDF` (resident banks and the music
index) and `SXDS` (the streamed payload). Chunks are `char[4] magic` + `u32 size`; the index
tables (`REQD`, `NAME`, `WAVE`, `SEQD`) share `magic/size` + `0` + `count` + `offsets[count]`
with `record_i = chunk + 0x10 + offsets[i] + 4·i` (the offsets are short by 4 bytes per entry —
the format, not a quirk). The chunk walk lands exactly on both tested files' ends (9,872 B and
15,967,712 B).

**Music.** `music.sxd1` is a **33-track catalog**; `music.sxd2` is 172,367,136 B. Each `WAVE`
record is 76 B: format word `0x00FF0642` (byte 1 = channel count = **6 → 5.1**), 48,000 Hz,
samples at `+0x14`, bytes at `+0x20`, start at `+0x24`, and the AT9 config word **`FE761DF0`**
at `+0x40` (identical on all 33). The 33 `(start, bytes)` pairs partition the payload gap-free
(32/32 consecutive pairs, last end = file size), every length is a multiple of 960 B = 1,024
samples, and every track is 360 kbps CBR. **33/33 decode at 48 kHz, 5.1, 63.84 min, non-silent
and within 24 ms of the declared duration.**

**Resident banks — the loader formula.** The payload of a resident wave is
**`wave_record_offset + 0x24 + start`**, and that formula **tiles** the `DATA` payload exactly on
`boss01`, `com_obj` and `com_v`. Records are variable-stride (the `+4·i` rule absorbs it) and
carry a **per-record** format word. `boss01.sxd` (15,967,712 B) has 343 `REQD` / 343 `NAME` /
138 `WAVE` and four format words, all four decoded by the gate:

| format word | records | channels | codec | gate duration | gate RMS |
|---|---:|---:|---|---:|---:|
| `0x00FF0121` | 64 | 1 | PS-ADPCM, 16 B / 28 samples | 1.135 s | −12.4 dBFS |
| `0x00FF0221` | 44 | 2 | PS-ADPCM, 16 B / 28 samples | 4.118 s | −9.5 dBFS |
| `0x00FF0142` | 15 | 1 | ATRAC9, 64 B/frame (`0xFE7007E0`) | 4.859 s | −17.1 dBFS |
| `0x00FF0242` | 15 | 2 | ATRAC9, 128 B/frame (`0xFE740FE0`) | 4.859 s | −17.1 dBFS |

The gate's final line:

```text
GATE PASSED: music 48 kHz non-silent; resident variants decoded 0x00FF0121, 0x00FF0142, 0x00FF0221, 0x00FF0242
```

The seven curated cues are all `0x21` PS-ADPCM; its filters 5–7 are fitted (no coefficient table
exists in `eboot.elf`), so every `0x21` row is **medium fidelity** — the gate proves existence,
rate, length and non-silence, not fidelity. Full evidence: `docs/notes/s5-audio-recon.md`
(§2 music, §3 resident records, §5.0 the `0x42` family, §7 the payload formula and ADPCM).

## 3 · Attribution — the seven cues

The machine-readable authority is `tools/re/audio/s5_map.json`; the record is
`docs/notes/s5-audio-attribution.md`. `name` is the sound-request name (`NAME`/`REQD` namespace),
`wave` the `WAVE` index it plays, `layers` the full set (primary included).

| event | bank | request name | REQD | tone | wave | layers | method | identity | fidelity |
|---|---|---|---:|---:|---|---:|---|---|---|---|
| `player.swing` | com_obj | `wnd_LifeSwd_swish_t1_01` | 118 | — | 7 | [7] | audition | medium | medium |
| `player.hit` | com_obj | `wnd_swd_hit_gravel_t1_01` | 60 | — | 21 | [21] | audition | medium | medium |
| `player.hurt` | com_v | `bvo_dmg_m` | 172 | — | 40 | [40] | audition | medium | medium |
| `player.death` | com_b_h | `b_dead_by_boss` | 1573 | — | 657 | [657] | audition | medium | medium |
| `valus.telegraph` | boss01 | `boss01_vo_atk1` | 340 | — | 3 | [3] | audition | medium | medium |
| `valus.impact` | boss01 | `boss01_atk_hit_ground1` | 303 | **43** | 26 | 26–30 | **ghidra** | **high** | medium |
| `valus.hurt` | boss01 | `boss01_vo_dmg1` | 331 | — | 6 | [6] | audition | medium | medium |

- **`valus.impact` is chain-verified.** `NAME` hash → `REQD` 303 → `SEQD` timeline `0x13E4` →
  bytecode `0x23BA` → `a4 01 43` (PLAY_TONE 43) → `TONE[43] = (key 57, wave 26)`; the sequence
  then plays tones 44–47 → waves 27–30, so the sound is a **five-layer** ground impact. Its pool
  label at index 26 is `boss01_FS_far3_0`, **not** the request name — proof that the string pool
  is a request namespace, not a per-wave name list. `s5_audition.py --check` re-parses the
  bytecode and requires the walked tone and the `TONE[tone].wave` match.
- **The six audition rows are not chain-proven.** The `boss01` voice bytecode at `0x6ABC` is a
  parameter envelope (`a9` SET_PARAM) whose PLAY_TONE operands are complex/random (`a4 02 03 …`),
  so no fixed tone can be walked; the `com_obj`/`com_v`/`com_b_h` `TONE` chunks are not the flat
  12-byte form either. Each row is a verified index with an explicit ear pick: w3 (1.202 s roar),
  w6 (1.051 s roar), w7 (0.464 s whoosh), w21 (0.708 s impact), w40 (0.270 s grunt), w657
  (0.811 s vocal). Identity confidence is **medium**; the ear is the final identity check.
- **Music is the table path, high confidence.** `music.sxd1` track 6 is `"Boss 1"`; its 76-byte
  record is 5.1 ATRAC9 (`FE761DF0`) with `start` absolute in `music.sxd2`. It is streamed music,
  not a resident SFX, and is looped by the runtime.

## 4 · The curated eight entries

`bp-convert audio --map tools/re/audio/s5_map.json --force` wrote **8/8** cues (`8 written, 0
reused, 8 manifest entries`); the manifest's `name=` set is exactly the map's `cues` events. Every
file exists under the library, every one is 48 kHz, and the channel count and duration match the
manifest. Measured from the shipped WAVs (`ffprobe`/`ffmpeg volumedetect`):

| event | bank | seconds | ch | bytes | mean | max | `gain_db` |
|---|---|---:|---:|---:|---:|---:|---:|
| `valus.telegraph` | boss01 | 1.201667 | 1 | 230,764 | −15.7 dB | −0.1 dB | −0.0873 |
| `valus.impact` | boss01 | 8.153250 | 2 | 3,130,892 | −20.9 dB | −5.7 dB | 0 |
| `valus.hurt` | boss01 | 1.051167 | 1 | 201,868 | −12.5 dB | −0.1 dB | −0.0873 |
| `player.swing` | com_obj | 0.464917 | 1 | 89,308 | −10.3 dB | −0.1 dB | −0.0873 |
| `player.hit` | com_obj | 0.708750 | 1 | 136,124 | −12.3 dB | −0.1 dB | −0.0873 |
| `player.hurt` | com_v | 0.270083 | 1 | 51,900 | −8.7 dB | −0.1 dB | −0.0873 |
| `player.death` | com_b_h | 0.812000 | 1 | 155,948 | −15.5 dB | −0.1 dB | −0.0873 |
| `music.boss` | music | 125.930667 | 2 | 48,357,420 | −18.8 dB | −0.1 dB | −10.0912 |

Two conversions are audible design decisions:

- **R6 — 5.1 → stereo at conversion time.** `Boss 1` decodes as 5.1 and is downmixed to stereo
  (`L/R + 0.707·C + 0.5·LFE/surround`, clipped); the runtime mixer stays stereo.
- **R12 — normalize once, then clamp; `gain_db` is informational.** Every delivered WAV is
  normalized to peak ≤ 0.99 (the mono `0x21` rows by −0.0873 dB, `Boss 1` by −10.0912 dB), and
  the app does **not** re-apply `gain_db`. The `valus.impact` mix of five layers already peaks at
  −5.7 dB, so its gain is 0.
- **`valus.impact` is 8.153 s.** It is the five-layer ground impact mixed to the longest layer
  (w28, 8.153 s), so the cue rings for seconds past the stomp (0.533 s after its telegraph) and
  club (0.433 s) impact points. That is the sound the game ships — a layered sequence, not a
  single hit — but whether the ring reads as a decaying impact or as a rumble that muddles the
  next telegraph is a listening call (checklist item 4).

## 5 · The event sequence

`--s5-selftest --audio-mixdown out/s5/mix.wav` runs the S4.2 timeline through fixed 1/60 s steps
with the audio observer on. Exactly **18** `s5-event` lines, in order:

```text
s5-event t=1.017 event=strike clip=player.swing gain=0.60
s5-event t=2.050 event=strike clip=player.swing gain=0.60
s5-event t=2.050 event=strike_hit clip=player.hit gain=0.60
s5-event t=2.050 event=valus_reaction clip=valus.hurt gain=0.60
s5-event t=2.050 event=music_start clip=music.boss gain=0.45
s5-event t=13.667 event=strike clip=player.swing gain=0.60
s5-event t=13.667 event=strike_hit clip=player.hit gain=0.60
s5-event t=13.667 event=valus_reaction clip=valus.hurt gain=0.60
s5-event t=15.700 event=stomp_start clip=valus.telegraph gain=0.60
s5-event t=16.233 event=stomp_impact clip=valus.impact gain=0.50
s5-event t=16.233 event=hurt_light clip=player.hurt gain=1.00
s5-event t=17.250 event=club_start clip=valus.telegraph gain=0.60
s5-event t=17.683 event=club_impact clip=valus.impact gain=0.50
s5-event t=17.683 event=hurt_heavy clip=player.hurt gain=1.25
s5-event t=18.933 event=club_start clip=valus.telegraph gain=0.60
s5-event t=19.367 event=club_impact clip=valus.impact gain=0.50
s5-event t=19.367 event=death clip=player.death gain=0.80
s5-event t=20.483 event=music_stop clip=music.boss gain=0.00
s5-mix frames=2139200 rms=0.0514 peak=0.9022 window_rms=0.1520
s5-status mixdown=…/out/s5/mix.wav events=18 music=1
```

What the acceptance pins, beyond the count:

- **Music starts on the first landed hit** — `music_start` is at 2.050 s, after the first
  `strike_hit` (the 1.017 s foot clang is a swing with no hit and starts nothing), and it plays
  `music.boss` at gain 0.45 exactly once.
- **Every attack telegraphs** — `stomp_start` (the `valus.telegraph` roar) precedes
  `stomp_impact`, and **both** club cycles are checked in order (17.250 → 17.683 and
  18.933 → 19.367), so a regression that inverts only the second still fails. Every impact
  (`stomp_impact` and both `club_impact`s) plays `valus.impact`.
- **Hurt lands with its impact** — `hurt_light` is on the same line time as `stomp_impact`
  (16.233 s, `player.hurt`, gain 1.00) and `hurt_heavy` on the same time as `club_impact`
  (17.683 s, gain 1.25: the same clip pushed).
- **The death is its own cue** — `death` at 19.367 s plays `player.death` at gain 0.80.
- **The music stops at the reset, not at the death** — `music_stop` is at **20.483 s**, after the
  death and exactly at the `R` reset; the script explicitly rejects a `music_stop` at 19.367 s.
  (The commit `fix(app): stop the music on defeat and reset, not death` is what the assertion
  guards: the death cue is the last thing the player hears before the reset.)
- **`clip=` is the manifest name** (R16), e.g. `valus.impact`, never `bank/name`: the anchored
  pattern only admits a dotted lowercase name, and every clip must exist in the manifest.
- **`s5-status` is last** and agrees: `events=18 music=1`.

## 6 · The mixdown

| property | measured | gate |
|---|---|---|
| codec / layout | `pcm_f32le`, 48 kHz, stereo | exactly that |
| length | 2,139,200 frames = 44.5667 s | 44.57 s ± 0.2 |
| RMS | 0.0514 (−25.8 dBFS) | > 0.01 |
| peak | 0.9022 (−0.9 dBFS) | **≤ 0.99 and ≥ 0.5** |
| loudest 1 s window RMS | 0.1520 | > 0.05 |
| music-only window (5.000–6.000 s) | **−30.509 dBFS** | non-silent (> −40) |
| stomp window (16.233–16.733 s) | **−15.408 dBFS** | **≥ 6 dB above the music-only window** (measured **+15.1 dB**) |
| `ffmpeg astats` Overall | peak **−0.894 dBFS**, RMS **−25.778 dBFS**, **0 NaNs**, **0 Infs** | peak < 0 dBFS, RMS > −40 dBFS, no NaN/Inf |

`Boss 1`'s voice gain is 0.45 and the master headroom 0.85, so **music alone tops out at
~0.38** — it cannot reach the 0.5 peak floor, and the stomp window is 15.1 dB above the
music-only second. That is what makes the capture evidence of **music *and* SFX**, not just the
score: dropping every SFX voice fails both the peak floor and the window comparison. The viewer's
own `s5-mix` line and the independent `astats` decode agree (0.9022 ≈ −0.894 dBFS), and `astats`
reads the samples, so "non-silent and non-clipping" is not self-reported.

## 7 · The audio-off identity

`--s42-selftest` (which never creates the S5 seam, so no audio subsystem is constructed) was run
into `out/s5/s42-audio-off.txt`; its **11** `s42-*` lines were diffed against the S5 run's own
`s42-*` lines: **empty diff** (`out/s5/s42-identity.diff`). The audio layer is an additive
observer (design D11): turning it on changes no damage, no window, no status value, and the
frozen `scripts/s42-acceptance.sh` is only invoked.

## 8 · The S4.2 acceptance (which runs S4.1, S4 and S3)

`scripts/s42-acceptance.sh` is unmodified by this task and ran green under `caffeinate -dimsu`
(exit 0; log `out/s5-s42-regression.log`). Its final line:

```text
s42-acceptance: PASS — build, both suites, s42 timeline (weakpoint proxies 1.955 m and 2.165 m, gate foot 0 / head 34, arm weakpoint 1 damage 34, stomp 35 health 100->65 hurt, club 45 health 65->20 hurt, range split club 0 at 4 m / stomp 15 at 3 m, death player_a_fall_damage_to_death, reset 100/100/grounded/Valus 100, stamina 28.425 -> 43.484 m leaving 39 of 100), six stills (2560x1440, span >= 30, > 5000 B), marginal 9.25 ms/frame (~108.1 fps), S4.1+S4+S3 regressions green
```

Its measured perf (same `--frames 1` vs `--frames 240` pair, both under `caffeinate`):

| run | wall time |
|---|---:|
| `--frames 1` (load-dominated) | **8.709 s** |
| `--frames 240` | **10.919 s** |
| marginal (239 frames) | **9.25 ms/frame ≈ 108.1 fps** |

The gate is **≤ 16.7 ms** (60 fps) — and the number describes the **unchanged S4.2 render path,
not the mixer**: the pair passes `--entity-time 0`, which sets `entityTimeSet` and therefore
`audio_wanted = false` in `app.cpp`, so **no audio subsystem is constructed** and the 9.25 ms
carries no audio cost. S5 does **not** measure an audio-on frame. The `--s5-selftest` path is an
offline mixdown of fixed timeline steps, not a timed render loop, so it cannot price the mixer
either. The audio-on frame cost is **unmeasured**; the user's fps walk (checklist item 11) is the
check, and this note makes no audio-perf claim. The nested statuses, unchanged:

```text
s41-status grab=left_foot climb=4.77 strikes=3 entity_health=0 mode=grounded defeated=1
s4-status instances=30 rigs=2 entity=minotaur_a_stand placement=builder-pick spawn=data-position+builder-yaw feet=ok entity_bbox=993,237,1485,751 pos=378.187,28.430,369.028 frames=423
s3-status instances=30 spawn=309.312,4.394,285.403 pos=309.312,29.802,305.405 ground=ok frames=240
```

The S4.1 acceptance's own final line (nested inside the S4.2 run) is `s41-acceptance: PASS — …`,
so S4.2 → S4.1 → S4 → S3 are green in this run.

## 9 · Suites

- `dotnet build BpConvert.sln -v q`: clean (0 warnings, 0 errors).
- `dotnet publish src/BpConvert.Cli -c Release -o out/bin`: clean.
- `dotnet test tests/BpConvert.Tests -v q`: **140 passed**, 0 failed (50 s).
- `ctest --test-dir app/build --output-on-failure`: **2/2 passed** (unit 10.73 s, windowed
  0.21 s; total 10.94 s).

## 10 · Provenance and rulings

- **R6 — music is 5.1, the runtime is stereo.** §4.
- **R12 — normalize then clamp; `gain_db` is informational.** §4.
- **R14 — master headroom.** The mixer's output stage applies a `0.85` master gain before the
  `[-1, 1]` clamp, so music (0.45) + SFX gains sum under 0.99; the mixdown's peak (−0.9 dBFS)
  is measured, not asserted from the arithmetic.
- **R15 — the first slice is the gate's seven cues + music.** §11.
- **R16 — `s5-event clip=` is the manifest name**, not `bank/name`; §5.
- **Gate records:** `docs/notes/s5-audio-recon.md` (decode, container, codec, payload formula)
  and `docs/notes/s5-audio-attribution.md` (the seven cues, method, confidence, deferred).
- **Map:** `tools/re/audio/s5_map.json`; **gate tools:** `tools/re/audio/s5_decode_gate.py`,
  `tools/re/audio/s5_audition.py`.

## 11 · Deferred cues (R15)

`valus.death`, the club whoosh, the player's fall, ambience, grab/climb and footsteps are
**deferred**: none is in the map's `cues` array, so the app is silent for them — never a
placeholder. **Eight** events are listed under the map's `deferred` array with a reason each
(`valus.death`, `valus.grab`, `valus.fall`, `valus.breath`, `valus.ambience`, `player.grab`,
`player.fall`, `player.breath`); the **club whoosh and the footstep set have no entry at all**.
The reasons are repeated in `s5-audio-attribution.md`: the boss death roar's sequence layering is
unresolved, the ambience beds are layered `0x21` sequences, and the `bvo_fall*`/`bvo_breath*`
names live beyond the 63 resident `com_v` waves.

The design's §12 walk list names some of these; they are marked **deferred**, not failed:

| design §12 item | state |
|---|---|
| music starts/loops/stops on reset | **shipped** (stop on defeat is implemented; the scripted timeline does not defeat Valus) |
| Valus telegraph roar, club/stomp impact, hurt | **shipped** |
| club whoosh | **deferred** |
| Valus death (roar) | **deferred** |
| Wander swing, hit, hurt, death | **shipped** |
| Wander fall | **deferred** |
| wind ambience | **deferred** |
| `M` mute, no clipping, windowed, cursor | **shipped** |
| fps ≥ 60 | **implemented**; the silent-path marginal is measured in §8, the audio-on fps is the user's walk (item 11) |

## 12 · The user's listening checklist — PENDING

The scripted pass proves presence, order, timing and non-silence — **not identity**. Run the
encounter from the repo root, turn the speakers on, and fill the cells:

```bash
export SOTC_LIB=<worktree>/out/library
caffeinate -dimsu app/build/sotc-viewer --library "$SOTC_LIB" \
    --manifest out/re/stage/encounter.manifest \
    --model models/shadow/characters/wander/_cmn/wander.glb
```

| # | check | do this | expected | result |
|---|---|---|---|---|
| 1 | music starts on the fight | climb to a weakpoint and land the first hit (hold `J`, release at the forehead or the arm) | `Boss 1` starts on the first landed hit and loops without a seam | ☐ |
| 2 | music stops on the reset, not the death | let the club kill you, wait through the death clip, press `R` | the music keeps playing over the death, then stops at the reset; the next fight's first hit restarts it | ☐ |
| 3 | Valus's telegraph and impact | stand within 4.5 m of a foot, then within the club's reach | the telegraph roar precedes the stomp and the club, and the layered ground impact is distinct from it | ☐ |
| 4 | the impact's 8.15 s tail | land a stomp or a club hit, then keep listening through the next telegraph | the five-layer impact decays as part of the hit, not as a rumble that masks the next telegraph | ☐ |
| 5 | Valus hurt reaction | land a weakpoint hit | his hurt vocal plays with the reaction clip | ☐ |
| 6 | Wander's swing and hit | hold `J` and release next to a weakpoint | the swish precedes the hit; one hit per swing, tight sync | ☐ |
| 7 | Wander hurt (light and heavy) | take a stomp (35), then a club (45) | two grunts; the heavy one is louder (1.25) | ☐ |
| 8 | Wander's death | let the club empty the amber bar | the death vocal plays once, with the death clip | ☐ |
| 9 | `M` mute | press `M` mid-fight, then again | the whole mix mutes/unmutes with no pop; the state is obvious without UI | ☐ |
| 10 | no clipping | fight through the heaviest moment (music + impact + hurt together) | no crackle or distortion | ☐ |
| 11 | windowed, cursor, fps | move the mouse over the window; read the title | a 1280×720 window, the cursor visible and free, the fps in the ≥ 60 class (the scripted perf number is the silent path — this is the audio-on check) | ☐ |

**Deferred — do not mark as failures:** the club whoosh, Valus's death roar, Wander's fall, the
wind ambience, and grab/climb/footstep sounds. Their absence is R15, not a regression.

**Verdict: PENDING — the user's ear.** S5's done-line closes only when this table is filled and
passes; nothing in §1–§11 claims it.

## 13 · Evidence files (all gitignored)

- `out/s5-acceptance-run.log` — the recorded run's full output (ends with the PASS line).
- `out/s5/mix.wav` — the 44.5667 s stereo mixdown; `out/s5/s5-selftest.txt` — the captured log.
- `out/s5/events.txt`, `out/s5/event-clips.txt`, `out/s5/manifest-names.txt`,
  `out/s5/map-names.txt` — the parsed event stream and the manifest/map name sets.
- `out/s5/s42-audio-off.txt`, `out/s5/s42-audio-off.lines`, `out/s5/s42-audio-on.lines`,
  `out/s5/s42-identity.diff` — the audio-off identity (empty diff).
- `out/s5-s42-regression.log` — the unchanged S4.2 acceptance's full output, ending `PASS —`.
- `out/s5/gate/`, `out/s5/audition/` — the decode and attribution gate artifacts (Tasks 0/1).
- `out/library/audio/audio.manifest` + `out/library/audio/<bank>/*.wav` — the shipped slice.

## 14 · Cross-references

- Design: `docs/superpowers/specs/2026-10-21-s5-audio-design.md` (§2 goals, §6 recon, §7.1 gates,
  §7.2 converter, §7.3 curation, §7.4 mixer, §7.5 acceptance, §12 the user's walk).
- Plan: `docs/superpowers/plans/2026-10-21-s5-audio.md`; rulings R1–R16 in
  `.superpowers/sdd/2026-10-21-s5-audio/progress.md`.
- Gates: `docs/notes/s5-audio-recon.md`, `docs/notes/s5-audio-attribution.md`.
- Task records: `.superpowers/sdd/2026-10-21-s5-audio/task-*-report.md` (this one is Task 6).
- S4.2 results: `docs/notes/s42-results.md`; S4.1: `s41-results.md`; S4: `s4-results.md`;
  S3: `s3-results.md`.
- Script: `scripts/s5-acceptance.sh` (this pass); the regression is `scripts/s42-acceptance.sh`
  (which runs `s41` → `s4` → `s3`).
