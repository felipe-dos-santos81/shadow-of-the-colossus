# S5 — "The sound of the forbidden land": the game's own audio (design)

**Date:** 2026-10-21 · **Status:** design (pre-plan) · **Follows:** S4.2
(`2026-10-20-s4-2-he-fights-back-design.md`)

## 1 · Intent

The app is silent. S4.2 made the encounter a real fight, but nothing in it makes a sound: no
music, no wind, no roar, no club impact, no hurt. The original design names audio as its own recon
workstream at M4 entry, and the formats were the reason: 1.9 GB of `SXD`-family files with no
known decoder. That recon is now done (2026-10-21). The banks are **SXDF** with **SXDS** stream
payloads, the codec is **ATRAC9**, the music catalog decodes out of the tables, and the bank string
pools carry the game's own sound names (`boss01_vo_atk1`, `wnd_LifeSwd_swish`, `wind_canyon1_1`).

This milestone turns that into sound: decode the game's own audio at build time, play it from the
existing simulation through SDL3, and prove the result by a deterministic headless mixdown plus the
user's ears. No synthesized placeholders are passed off as the game's audio, and no original
container is parsed at runtime.

## 2 · Goals (the done-line)

1. **SXDF/SXDS is decoded at build time.** A new `bp-convert audio` verb reads the resident banks
   and the `.sxd1`/`.sxd2` pairs, resolves names and waveform geometry from the chunk tables,
   carves the AT9 frames, and writes PCM WAV plus `audio.manifest.json` under
   `out/library/audio/`. The runtime loads only WAV + manifest.
2. **ATRAC9 is proven, not assumed.** A decode gate decodes at least one music track and one
   `boss01` sound to 48 kHz, non-silent, correct-duration PCM, and pins the config word and channel
   count. If the gate fails after the Ghidra fallback, the milestone **stops and reports** — a
   silent or noisy result is not a pass.
3. **The fight has its own voice — after an attribution gate.** Music (`Boss 1`, loop) and at
   least the core combat cues: Valus telegraph, club/stomp impact, hurt; Wander swing, hit, hurt,
   death. A **resident-bank attribution gate** must connect each curated sound name to its waveform
   bytes before the converter is built — by a table resolver, or by auditioning the candidate waves
   and recording the index and confidence. Ambience, fall/land, grab/climb and breath are included
   only when attributed. The map is a named table validated against the manifest at load; a name
   that is not in the manifest is a load error.
4. **Audio is spatial enough.** World sounds attenuate with distance and pan by the player's
   relative bearing; music and ambience are non-positional. No occlusion, reverb, or convolution
   this pass.
5. **The mixer is deterministic and headless.** A software mixer (48 kHz float, categories, gains,
   loops, 32 voices) feeds one SDL3 audio stream. `--s5-selftest` drives the existing encounter
   timeline and writes an ordered `s5-event` log plus a mixdown WAV; the acceptance checks the
   samples without an audio device.
6. **Acceptance.** `scripts/s5-acceptance.sh`: build, both suites, decode inventory (every
   manifest entry exists, header/rate/duration/RMS), the event sequence, a non-silent
   non-clipping mixdown, perf, and the S4.2→S4.1→S4→S3 regressions.
7. **The user walks by ear.** `docs/notes/s5-results.md` records the measured numbers and a
   PENDING listening checklist (music, roar, club, stomp, strike, hurt, death, ambience, mute,
   loop, windowed and cursor-free).

## 3 · Non-goals (this pass)

- All 8,001 `shadow_sounds.cxml` events; the full 63.8-minute soundtrack; every variation.
- Occlusion, reverb, convolution, object audio, dynamic music layers or stingers.
- Agro, other colossi, dialogue, UI voice, event cameras, cutscenes.
- Reproducing the SNDX bus graph, effectors, or the `.crmp` routing/random tables.
- Runtime decoding of original containers (forbidden by the offline-first rule).
- Footsteps on every surface; the arena is one surface, and a single stone/sand step set is
  enough if a suitable name exists (otherwise footsteps stay deferred).

## 4 · Anti-goals (a failure even if it runs)

- **Shipping game audio in git** — assets and derived audio are gitignored, as the models are.
- **A "successful" decode that is silent, noise, or the wrong duration.**
- **Synthesized placeholder sounds presented as the game's own.**
- **A runtime that parses `.sxd`/`.sxd2`** or depends on the game tree.
- **Acceptance that needs a physical audio device.**
- **A regression of S4.2's acceptance, the windowed mode, or the free cursor.**
- **Docs that disagree** with the decoder, the map, or the measured numbers.

## 5 · Constraints (inherited, fixed)

- **SDL3 3.4.18 + SDL_GPU/Metal**, C++20, Apple Silicon; **windowed**; **the cursor is never
  captured or locked** (user, 2026-10-16).
- Offline-first: only the converted library, manifests, and clip records are read at runtime.
- Static-only RE. Personal use, no redistribution; assets and derived audio uncommitted.
- .NET 10 converter; tests fail fast without `SOTC_GAME_ROOT` / `SOTC_LIB`; bash 3.2; typed
  commits; **≤4 concurrent agents**.
- The S4.2/S4.1/S4/S3 manifests and scripts are not modified.
- Build-time decode may use VGAudio (MIT, managed) or an FFmpeg (LGPL) subprocess; neither is
  shipped in the runtime.
- Acceptance runs under `caffeinate -dimsu`; never two acceptances concurrently.

## 6 · Recon facts (2026-10-21)

Full evidence: `.superpowers/sdd/s5-audio/recon-{assets,binary,integration,sdl3}.md` (scratch,
committed with the milestone's SDD records).

**Inventory.** `sound/shadow/` is 42 files / 1.9 GB: 25 `SXDF` resident banks, 5 `.sxd1` index
files + 5 `.sxd2` `SXDS` payloads, 4 `.crmp` routing maps, 1 `shadow_sounds.cxml` (8,001
`<Sound>` events), 1 `sound_data.calt` (36-line load list), 1 `.cctx`. Magic is **`SXDF`**
(`53 58 44 46`), not `SHDF`; streamed payload is **`SXDS`**.

**Container.** Header: magic `0x00`, size `0x08`, companion size `0x0C`, bank GUID `0x10–0x1F`
(identical in a `.sxd1`/`.sxd2` pair), name `0x20`; chunks start at `0x60`. Chunk header is
`char[4] magic` + `u32 size` (bytes after the header). Index tables (`REQD`, `NAME`, `WAVE`,
`SEQD`) share `magic/size` + `0` + `count` + `offsets[count]`; **record_i = body + offsets[i] +
4·i** (the offsets are short by 4 bytes per entry). Records are variable-length for `REQD`, so
always drive them from the table.

**Music.** `music.sxd1` (9,872 B) is a 33-track catalog; `music.sxd2` (172,367,136 B) is the
payload. Each `WAVE` record is 76 bytes: `+0x04` format word `0x00FF0642`, `+0x08` 48,000 Hz,
`+0x0C` `0x0F3B`, `+0x14` sample count, `+0x20` byte length, `+0x24` start offset into
`.sxd2`. The 33 `(start,length)` pairs partition the payload gap-free; every length is a multiple
of **960 bytes = 1024 samples at 48 kHz = 21.333 ms**, and every track computes to **360 kbps
CBR**. Names are readable ("Prologue", "Opening 1", "Boss 1", "Defeat", …). Total 63.83 min.
`music.sxd1` carries no `DATA`.

**Resident banks.** `boss01.sxd` (15,967,712 B) has 343 `REQD` / 343 `NAME` / 138 `WAVE` entries
and `DATA @0x14f10`; resident `WAVE` records are 44 bytes with `[5]=bytes`, `[9]=start` into
`DATA+0x20`. `com_v` (Wander voice), `com_obj` (weapons/objects), `com_amb` (ambience) follow the
same shape; `com_amb`'s `WAVE` table is a known exception (143 rows vs 115 `REQD`), so it is
decoded best-effort until pinned.

**Names.** The string pools are readable even where the `REQD` name offset is not yet pinned.
Valus (`boss01`): `boss01_vo_atk1..3`, `boss01_atk_whoosh1..3`, `boss01_atk_hit_ground1..3`,
`boss01_vo_dmg1..8`, `boss01_vo_idling1..4`, `boss01_rise`/`boss01_rise_sub`,
`boss01_down_mvt_sub`/`boss01_down_mvt_whoosh_sub`, `boss01_eff_stone1..4`,
`boss01_eff_sand1..5`. Wander (`com_v`): `bvo_atk_s/m`, `bvo_dmg_s/m/hard`, `bvo_die_dmg`,
`bvo_die_fall`, `bvo_fall1..12`, `bvo_breath_Loop1/2`. Weapons (`com_obj`):
`wnd_LifeSwd_swish_t1_01..04`, `b_sword_atk_normal`, `b_sword_sand_hit`, `b_sword_stone_hit`,
`b_sword_ground_hit`. Ambience (`com_amb`): `wind_canyon1_*`, `wind_cavern1_*`,
`grass_touch_*`, `wave_*`.

**Resident attribution is not yet proven (builder-check measurements, 2026-10-21).** In `boss01`,
`REQD+0x08` is a sequential id 0..342, **not** a wave index; there are 343 `REQD` but 138 `WAVE`
entries; the wave byte regions overlap (Σ lengths 18.4 MB against a 15.88 MB `DATA` payload), so
"the i-th sound is the i-th wave" is false. The `NAME` table does not stride-resolve in resident
banks, and the ordered string pool is an assumption — `com_v`'s pool is not a clean 1:1 list.
Resident `WAVE` records are **44 or 64 bytes**, the format word varies per record
(`0x00FF0121`, `0x00FF0142`, `0x00FF0221`, `0x00FF0242`, `0x00FF0642`), and `+0x0C` varies within a
bank. In `com_sys` the lengths at `+0x20` partition the `DATA` payload (1,066,784 against
1,066,760) while `+0x14` sums to 1.83 MB. These are plan-level facts: attribution and the
per-record layout are gates, not assumptions.

**Codec.** The engine is Bluepoint **SNDX** on Sony **NGS2**. `eboot.bin` is a plaintext PS4 SELF
whose LOADs match `out/re/eboot.elf` byte-for-byte; Ghidra 12.2 imports the ELF as
`x86:LE:64:gcc`. Strings: `VOICE_ALLOC_AT9` (`0x12AC61F`), `maxAtrac9Decoders` (`0x12AC36E`),
`Invalid ATRAC9 configure data (%08X)` (`0x12B5660`), with xrefs at `0x94A7B5`, `0x94876A`,
`0x98429E`; no CRI/HCA/Vorbis/CELT/Opus/MP3 hits. The raw payload is **AT9 frames**, not a RIFF
container; the first bytes are `32 93`/`33 93` followed by payload.

**SDL3.** 3.4.18 has the full playback path: `SDL_OpenAudioDevice`, `SDL_CreateAudioStream`,
`SDL_BindAudioStreams`, `SDL_SetAudioStreamGain`, `SDL_GetAudioStreamQueued`,
`SDL_PutAudioStreamData`, `SDL_PauseAudioDevice`; the `dummy` and `disk` drivers are compiled in,
and `SDL_AUDIO_DISK_OUTPUT_FILE` gives a headless mixdown. SDL3_mixer is not required.

**Open unknowns.** The AT9 config word's bit split and channel count; the resident-bank `REQD`
name offset (music is `+0x20`; boss/com records are shorter); loop points; the `com_amb` `WAVE`
exception; the `.crmp` semantics. None blocks the gate except the config word.

## 7 · The how (the decided shape)

### 7.1 Gates first: decode and attribution

**Task 0 — the decode gate.** A standalone script/verb carves `music.sxd1` track "Prologue" and one
`boss01` record per resident format variant in the curated set, decodes them, and asserts 48 kHz,
duration, RMS > threshold, peak ≤ 1.0, and no NaN. It tries the recorded config candidates
(`+0x04` `0x00FF0642`, `+0x0C` `0x0F3B`, `+0x28` `0x00120401`) **and** the per-record words the
builder measured (`0x00FF0121`, `0x00FF0142`, `0x00FF0221`, `0x00FF0242`). If they fail, it reads
the bit split from the `Invalid ATRAC9 configure data` xref in Ghidra. A failed gate stops the
milestone with a written blocker; it does not proceed to a silent placeholder.

**Task 1 — the attribution gate.** Resident banks do not yet map a sound name to waveform bytes
(§6). Two paths, in order:

- **Table path:** decode `TONE`/`SEQD`/`WAVE` tags and the remaining `REQD` fields; if ambiguous,
  use Ghidra on the `sndx_resource.c` resolver in `out/re/eboot.elf` to find the sound→wave lookup.
- **Ear path:** if the table path is not pinned in the time-box, decode the 138 `boss01` `WAVE`
  records to audition WAVs (and the curated `com_v`/`com_obj` waves), identify the core cues, and
  record `bank#index`, confidence, and the method in `docs/notes/s5-audio-attribution.md`.

The gate must yield a mapping for the core cues (goal 3). If it cannot, the milestone stops and
reports; the user decides whether to fund deeper RE or ship music-only. The scripted acceptance
cannot detect a wrong-but-plausible sound, so the user's ear is the identity check and the results
note labels the mapping method.

### 7.2 Converter — `bp-convert audio` (curated scope)

New pipeline under `src/BpConvert.Core/Audio/` plus an `audio` CLI verb. **S5 decodes the curated
set only** (the banks and sounds the map needs), not the full 1.9 GB; the verb takes a scope
(`--banks` + `--events`, or a curation file) and the manifest lists only what it decoded.

- `SxdfReader`: header + chunks; `ReadTable(magic)` driven by the table's own offsets (no fixed
  stride); `WAVE` records may be **44 or 64 bytes**, with a **per-record** format word and `+0x0C`;
  the attribution gate's per-bank layout table is the authority; `DATA` offset.
- `AudioNameResolver`: music uses the record `+0x20`; resident banks use the resolved name pool or
  the auditioned `bank#index` from the attribution gate. Every curated name must resolve uniquely;
  a miss is an error, not a fallback.
- `At9Carver`: for each selected `WAVE`, seek the bank's `DATA+0x20` (resident) or the `.sxd2`
  (streamed), copy the record's byte length, attach the record's format word, and validate the
  frame geometry (960 B / 1024 samples) plus the record's start/length against the bank bounds.
  The per-bank lengths must partition the payload; a record outside the bounds is an error.
- `At9Decoder`: **VGAudio (MIT, managed)** primary; if its AT9 path rejects a config, an
  **FFmpeg `atrac9`** subprocess (LGPL, build-time only). Config is taken per record, not per bank.
  Output is 48 kHz PCM16 WAV (mono or stereo as decoded); the manifest duration is measured from
  the decoded PCM, not the declared sample count.
- `AudioManifestWriter`: `audio.manifest.json` with `version`, banks, and one entry per **decoded**
  sound: `{bank, name, file, rate, channels, samples, duration, loop, source, confidence}`. Stable
  paths under `out/library/audio/<bank>/<name>.wav`; duplicates per bank are an error.
- `Delete` semantics mirror `ClipManifest`: a re-conversion with no audio removes the scoped stale
  outputs and the manifest entries.

### 7.3 Curation — the first slice

The map is a C++ table (`app/src/audio_map.h`) plus the manifest: `event -> {bank, name, gain,
loop, spatial}`. Candidates (the plan pins the final names; every name must exist in the manifest):

| Event | Bank / name (candidate) |
|---|---|
| `music.boss` | `music` / `Boss 1` (loop) |
| `ambience.wind` | `com_amb` / `wind_canyon1_1` (loop) |
| `valus.telegraph` | `boss01` / `boss01_vo_atk1` |
| `valus.club_swing` | `boss01` / `boss01_atk_whoosh1` |
| `valus.club_impact` | `boss01` / `boss01_atk_hit_ground1` |
| `valus.stomp_impact` | `boss01` / `boss01_atk_hit_ground2` (or `boss01_eff_stone1`) |
| `valus.hurt` | `boss01` / `boss01_vo_dmg1` |
| `valus.death` | `boss01` / `boss01_rise_sub` (candidate; the plan's listening check pins it) |
| `player.swing` | `com_obj` / `wnd_LifeSwd_swish_t1_01` |
| `player.hit` | `com_obj` / `b_sword_sand_hit` |
| `player.effort` | `com_v` / `bvo_atk_s` |
| `player.hurt_light` | `com_v` / `bvo_dmg_s1` |
| `player.hurt_heavy` | `com_v` / `bvo_dmg_m1` |
| `player.death` | `com_v` / `bvo_die_dmg` |
| `player.fall` | `com_v` / `bvo_fall1` |
| `player.breath` | `com_v` / `bvo_breath_Loop1` (low stamina, optional) |

**Ambience is optional** until the attribution gate produces a provable wind clip; the acceptance's
hard criteria are music + the core cues in goal 3. Grab/climb, fall/land and breath are wired only
when attributed; otherwise they stay deferred and the map says so. The map is validated at load: a
missing name is an error naming the event and the manifest path.

**Music bank (builder-check Q4):** `music.sxd1` is the catalog with the proven name→bytes geometry
and the names "Boss 1"/"Defeat"/"Collapse"; `str_boss.sxd2` is the larger streamed boss bank. S5
uses `music/Boss 1` first. If the user's ear says it is the menu cue, the follow-up switches to
`str_boss` (which needs its own attribution pass); that is a listening decision, not a gate.

### 7.4 Runtime — the mixer

- **Load:** `asset_io` loads the curated WAVs into float PCM at startup (only the manifest's
  curated entries, not the full 63.8 min). A missing manifest degrades to silence with one warning
  in non-encounter runs; an encounter run with a missing curated entry fails loudly.
- **Device:** one `SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, …)`, one
  `SDL_AudioStream` at 48 kHz float stereo → device format, bound to the device. The mixer owns a
  ring of float samples; each frame it mixes active voices, applies category gains
  (music/ambience/sfx), distance attenuation and pan for spatial voices, then pushes via
  `SDL_PutAudioStreamData` while `SDL_GetAudioStreamQueued` is below the target.
- **Voices:** 32; one music voice, one ambience voice, the rest SFX with stealing (oldest/quietest).
  Loops are a cursor wrap, not an SDL flag. `M` toggles mute; the gain is clamped; no new HUD.
- **Seam:** the simulation never calls SDL. `player_step` stays pure; `StepEvents` grows
  `hurtLight`, `hurtHeavy`, `died` fields (no calls); the app translates `StepEvents` plus the
  encounter events into a small `SfxEvent` queue consumed once per frame. The same translation is
  used by the interactive loop, the S4.2 timeline and the S5 timeline, so they cannot drift.
- **Hooks:** `apply_combat_events` (weakpoint hit, reaction, defeat), `apply_valus_impact` (stomp
  direct/wave, club), `start_valus_attack` (telegraph), `player_step` mode changes (hurt/death,
  fall), `reset`. Music starts on the first landed hit, loops, stops on defeat and reset; ambience
  loops from encounter load.
- **Headless:** `--audio-mixdown PATH` runs the same mixer with a disk sink (no device), or
  `SDL_AUDIO_DISK_OUTPUT_FILE`; `--s5-selftest` drives the existing 1/60 s timeline, logs
  `s5-event t=<s> event=<name> clip=<bank/name> gain=<x>`, writes the mixdown, and exits 1 on a
  missing clip or a silent mix window. **The audio layer is an additive observer on the S4.2
  stepper:** with audio off (the default) the `s42-*` values are byte-identical, and the frozen
  `scripts/s42-acceptance.sh` is only invoked — never edited. The S5 run enables the observer and
  the disk sink; it does not duplicate or re-choreograph the encounter.
- **Cost:** the mix is target <0.5 ms/frame at 48 kHz stereo; the acceptance's 240-frame run is the
  gate.

### 7.5 Acceptance — `scripts/s5-acceptance.sh`

House pattern (S4.2 script as the model):

1. Build (`BpConvert.sln`, app Release), `dotnet test`, `ctest`.
2. `bp-convert audio` for the curated scope **after both gates pass**; assert the manifest count,
   every entry's file exists, and every curated map name resolves.
3. `--s5-selftest --audio-mixdown out/s5/mix.wav` under `caffeinate`; capture the `s5-*` lines.
4. Assert the event sequence: music start on the first hit, `valus.telegraph` before
   `valus.club_swing`/`impact`, `player.swing`/`hit` around the strike, hurt/death on the lethal
   hit, music stop on defeat, reset restores the opening state. **The hard criteria are music +
   the goal-3 core cues**; ambience and the optional cues are asserted only if the attribution gate
   mapped them.
5. WAV checks: 48 kHz, stereo (or the decoded channel layout), duration within tolerance, RMS in a
   non-silent band, peak ≤ 0.99, no NaN; a per-window RMS check proves music and SFX are both
   present (not just one). The scripted checks prove presence, timing, and non-silence, **not
   identity** — the user's ear is the identity check, and the results note records the mapping
   method and confidence.
6. Perf: the 240-frame run's marginal stays in the S4.2 class; audio adds no frame spike.
7. Regression: `caffeinate -dimsu ./scripts/s42-acceptance.sh` (it runs S4.1→S4→S3); assert its
   `PASS —`.
8. Summary with the measured numbers and `PASS —`.

### 7.6 Execution shape (≤4 agents)

Decode gate → attribution gate → (converter parser ∥ app mixer) → converter decode/manifest → app
wiring → acceptance/docs. **Both gates are hard stops.** No task may modify the frozen S3/S4/S4.1
scripts; the S4.2 script is only invoked.

## 8 · Risks

| Risk | Likelihood | Response |
|---|---|---|
| AT9 config/channel count resists the decoder | medium | Task 0 tries the recorded and per-record candidates; Ghidra the `Invalid ATRAC9 configure data` bit split; if still blocked, stop and report — no placeholder. |
| Resident name→wave attribution resists | medium-high | Task 1 table path, then Ghidra `sndx_resource`, then audition the 138 `boss01` waves; if the core cues cannot be mapped, stop and report — no guessed names. |
| Per-record `WAVE` geometry/config | medium | Task 0/1 pin the per-bank layout and every format variant in the curated set; no fixed stride; duration from decoded PCM. |
| A wrong-but-plausible cue passes the scripted checks | medium | The scripted checks prove presence, timing, and non-silence; the user's ear is the identity check; method/confidence recorded. |
| Loop points unknown | low | Loop the whole track (Boss 1) or play once; the user walk decides. |
| `com_amb` `WAVE` exception / ambience unproven | low-medium | Ambience is optional; the hard acceptance is music + the core cues. |
| Audio device absent in acceptance | low | `disk` sink / mixdown WAV; no hardware required. |
| Scope creep to the full 8,001 events | medium | Curated scope only (D8); the full catalog is a later milestone. |
| Licensing | low | VGAudio MIT; FFmpeg LGPL build-time only; assets never committed. |
| Mix cost above budget | low | 32 voices, 48 kHz; the 240-frame acceptance measures it. |

## 9 · Left to the builder

- Decoder choice (VGAudio vs FFmpeg) inside the gate; WAV PCM16 vs float; the exact curated list
  beyond the candidates; the name resolver's fallback; the death/loop cue; whether music starts on
  first hit or on load; the `M` mute key; the voice-stealing rule.
- The parser's per-bank delete scope and manifest layout.
- Whether grab/climb/footstep sounds are wired now or deferred.

## 10 · Decisions recorded

- **D1** Build-time decode to PCM WAV; the runtime never parses `.sxd`/`.sxd2`.
- **D2** A software mixer into one SDL stream, not one stream per voice.
- **D3** A curated first slice, not the full 8,001 events or the full soundtrack.
- **D4** A disk-sink mixdown is the headless acceptance; no physical device is required.
- **D5** Music starts on the first landed hit, loops, stops on defeat and reset; ambience loops
  from encounter load.
- **D6** No new UI; `M` mute only.
- **D7** The S5 acceptance invokes S4.2, which invokes S4.1→S4→S3.
- **D8** Curated scope only: S5 decodes the banks/sounds the map needs, not the full 1.9 GB.
- **D9** The resident-bank attribution gate precedes the converter; table resolver, then Ghidra,
  then audition with recorded index/confidence; if the core cues cannot be mapped, stop and report.
- **D10** Per-record `WAVE` config and variable record size; no fixed-stride or per-bank-constant
  assumptions; the manifest duration is measured from decoded PCM.
- **D11** The S5 selftest is an additive observer on the S4.2 stepper; with audio off the `s42-*`
  values are byte-identical; the S4.2 script is invoked, never edited.

## 11 · Builder-check resolutions (2026-10-21)

The builder check measured the resident banks and raised six plan-level forks. Rulings (the user
opted out of questions, so these are the executor's calls):

1. **Name→wave attribution (Q1) → D9, a hard gate.** The scripted checks cannot detect a
   wrong-but-plausible sound; the table/Ghidra path is tried first, then audition with the index and
   confidence recorded. If the core cues cannot be mapped, the milestone stops and reports; no
   guessed names.
2. **Resident `WAVE` geometry/config (Q2) → D10.** Records are 44/64 bytes with per-record format
   words; the gate pins the per-bank layout and every variant in the curated set; the parser drives
   from table offsets and validates the payload partition; duration comes from decoded PCM.
3. **Full inventory vs curated (Q3) → D8.** S5 decodes the curated scope only; the full 1.9 GB is a
   later milestone.
4. **Fight-music bank (Q4) → `music/Boss 1` first.** Its name→bytes is proven; `str_boss` is the
   alternative if the user's ear says the catalog cue is wrong. A listening decision, not a gate.
5. **Ambience (Q5) → optional.** The hard acceptance is music + the core cues; wind is asserted
   only when a bank is attributed.
6. **Selftest reuse (Q6) → D11.** The audio layer is an additive observer on the S4.2 stepper; with
   audio off the `s42-*` values are byte-identical; the S4.2 script is invoked, never edited.

No question is left for the user; the two gates carry the residual risk.

## 12 · Done-line and the user's walk

The milestone is done when `scripts/s5-acceptance.sh` prints `PASS —` with the curated decode
inventory, the attribution note, the event sequence, the non-silent mixdown, the perf number, and
the S4.2→S3 regressions green; and the listening checklist in `docs/notes/s5-results.md` is filled
by the user:

- [ ] Music starts when the fight starts, loops cleanly, stops on defeat and reset.
- [ ] Valus's telegraph roar, club whoosh, club/stomp impact, hurt and death are audible and
      distinct; a cancelled attack makes no impact sound.
- [ ] Wander's swing, hit, hurt, death and fall are audible; the swing/hit sync is tight.
- [ ] Wind ambience (if attributed) sits under the fight without masking the SFX.
- [ ] `M` mutes and unmutes; the mixer does not clip; the app stays windowed and the cursor never
      jumps or locks.
- [ ] The fps class stays ≥60.
