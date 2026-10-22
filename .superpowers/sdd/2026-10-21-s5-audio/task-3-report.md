# Task 3 report — converter decode, WAV, manifest, `audio`

Status: **DONE** (2026-10-21). Commit `57ed5f5` — `feat(convert): decode the S5 audio slice`.

The gate's proven decoders are implemented behind one API, the curated map converts end to end,
and the runtime contract (`out/library/audio/audio.manifest` + float32 WAVs) exists. The user's
ear remains the identity/fidelity check for the `0x21` rows (fitted filters 5-7).

## What was built

`src/BpConvert.Core/Audio/SxdTables.cs`
- `WaveRecord` gains `uint CodecConfig` (additive; the Task 2 tests still pass). `0x42` records
  read `+0x40` on the 76-byte music/resident form and `+0x2C` on the 56-byte resident form;
  `0x21` records read 0. The offset is the record's layout, not a `0xFE` scan, so an accidental
  byte pattern cannot be mistaken for a config.
- `TrackNames(bank)`: the `0x40`-byte `<REQD>+0x20` track-name slot in table order (music tracks
  "Prologue" … "Boss 1"). Music record 31's slot reaches 8 bytes into the following `NAME` chunk,
  exactly as the gate reads it, so the slot is file-bounded and NUL-terminated.

`src/BpConvert.Core/Audio/AudioCodec.cs`
- `DecodedAudio(float[] Samples, int Channels, int Rate)` with `GainDb`, `Frames`, `Seconds`,
  `Peak`, `Rms` (interleaved samples).
- `Decode(payload, formatWord, codecConfig, sampleRate, normalize = true)`:
  - `0x21`/`0x0221` → PS-ADPCM, exact port of the gate's `ps_adpcm_decode`: 16-byte blocks,
    28 samples, low nibble first, the 8-entry VAG table with filters 5-7 fitted
    (`(126,-62) (128,-64) (126,-63)`), stereo block interleave, per-channel predictor state.
  - `0x42`/`0x0142`/`0x0242`/`0x0642` → ATRAC9 through `ffmpeg`, with the record's big-endian
    config as extradata in the gate's `WAVEFORMATEXTENSIBLE` wrapper.
  - 6 ch → stereo: `L' = L + 0.707·C + 0.5·LFE + 0.5·SL`, `R' = R + 0.707·C + 0.5·LFE + 0.5·SR`;
    mono/stereo pass through.
  - Peak normalization to ≤ 0.99 with `gain_db = 20·log10(gain)` (0 when no scaling); the final
    output is clamped to [-1, 1].
  - Rejects an unsupported codec/channel count, a missing/mismatched AT9 config, a non-48 kHz
    AT9 config, an empty/failed ffmpeg stream, or a non-finite sample with
    `InvalidDataException` — no silent fallback.

`src/BpConvert.Core/Audio/WavWriter.cs` — float32 RIFF/WAVE, `fmt` tag 3, mono/stereo, 48 kHz;
`Wav_RoundTrips_Float32` pins the 44-byte header, byte rate, block align, data size, and samples.

`src/BpConvert.Core/Audio/AudioManifest.cs` — the R1 line format (`version=1`, then the 10 keys
`name bank file rate channels frames seconds codec loop gain_db`), sorted by `(bank, name)`,
duplicate keys rejected on write and read, invariant numbers, forward-slash paths.

`src/BpConvert.Core/Audio/AudioConverter.cs` — the pipeline behind the verb:
- reads the map (`cues[]`), validates `wave`/`layers`, dedupes events, applies `--events`/`--music`
  (no selector = the whole curated map);
- resolves the bank (resident `.sxd` or music `.sxd1` + `.sxd2`), validates the cue name against
  `NAME` (resident) or the `REQD` track at `wave` (music);
- carves `RecordOffset + 0x24 + Start` inside `[DATA+0x10, DATA+8+size)` for residents, `Start`
  inside the `.sxd2` payload for music, and rejects an escaping range as non-tiling;
- decodes every layer `normalize: false`, mixes with `1/N` from t=0 padded to the longest, then
  normalizes once (the gate's `valus.impact` mix);
- writes `<out>/audio/<bank>/<event>.wav`, merges the cumulative manifest, and reuses an existing
  entry + file unless `--force`.

`src/BpConvert.Cli/Program.cs` — `case "audio"` + `AudioCommand` (exit 0 ok, 1 decode/IO/map
failure naming the event, 2 invocation), usage text.

`tools/re/audio/s5_map.json` — the `music.boss` cue (`bank=music`, `name="Boss 1"`, `wave=6`,
`layers=[6]`, `method=table`, confidence/fidelity high). `s5_audition.py` gained a music-row check
(wave → `REQD` track name, 5.1, config at `+0x40`) so the documented `--check` stays green; it
does not decode the 126 s track (the converter test does).

## RED / GREEN

RED (tests written first, before the types existed):
```
error CS1061: 'WaveRecord' does not contain a definition for "CodecConfig"
error CS0103: The name "WavWriter" does not exist in the current context
error CS0246: The type or namespace name "AudioEntry" could not be found
error CS0103: The name "AudioCodec"/"AudioConverter" does not exist ...
```
GREEN:
```
SOTC_GAME_ROOT=… dotnet test tests/BpConvert.Tests --filter AudioTests -v q
Aprovado! – Com falha: 0, Aprovado: 21, Ignorado: 0, Total: 21   (892 ms)

SOTC_GAME_ROOT=… dotnet test tests/BpConvert.Tests -v q
Aprovado! – Com falha: 0, Aprovado: 140, Ignorado: 0, Total: 140   (48 s)

dotnet build BpConvert.sln -> 0 warnings, 0 errors
```
The 10 new tests: `Waves_CodecConfig_Pinned`, `Wav_RoundTrips_Float32`,
`Manifest_RoundTrips_StableSorted`, `Manifest_DuplicateBankName_Throws`,
`Decode_0x42_Is48kNonSilent`, `Decode_RejectedConfigAndNonFinite_Throw`,
`Decode_0x21_Is48kNonSilent`, `Decode_Music_IsStereo48k`, `Verb_WritesManifestAndFiles`,
`Layers_Mix_NonSilent`.

The verb run:
```
dotnet publish src/BpConvert.Cli -c Release -o out/bin
caffeinate -dimsu dotnet exec out/bin/bp-convert.dll audio \
  --root "$SOTC_GAME_ROOT" --out out/library --map tools/re/audio/s5_map.json --force
  write boss01/valus.telegraph  1.202 s  48000 Hz 1 ch  gain -0.09 dB  (psadpcm)
  write boss01/valus.impact     8.153 s  48000 Hz 2 ch  gain  0.00 dB  (psadpcm)
  write boss01/valus.hurt       1.051 s  48000 Hz 1 ch  gain -0.09 dB  (psadpcm)
  write com_obj/player.swing    0.465 s  48000 Hz 1 ch  gain -0.09 dB  (psadpcm)
  write com_obj/player.hit      0.709 s  48000 Hz 1 ch  gain -0.09 dB  (psadpcm)
  write com_v/player.hurt       0.270 s  48000 Hz 1 ch  gain -0.09 dB  (psadpcm)
  write com_b_h/player.death    0.812 s  48000 Hz 1 ch  gain -0.09 dB  (psadpcm)
  write music/music.boss      125.931 s  48000 Hz 2 ch  gain -10.09 dB (at9)
manifest: out/library/audio/audio.manifest (8 entries)
audio: 8 written, 0 reused, 8 manifest entries
```
`--events nope` exits 1 with `bp-convert audio: event 'nope' is not in …`; `--music "Boss 1"`
selects and reuses the music row; `--events player.swing` reuses one row and leaves all 8 entries.

## Decoder commands / configs

| family | record | config | geometry | result |
|---|---|---|---|---|
| PS-ADPCM | `0x0121`/`0x0221` | — | 16 B / 28 samples, 8 filters | in-process float |
| ATRAC9 music | `0x00FF0642` | `0xFE761DF0` | 240 B/frame, superframe 2 (960 B), 5.1 | ffmpeg |
| ATRAC9 resident | `0x00FF0142` | `0xFE7007E0` | 64 B/frame, 1 frame (56/76-B records) | ffmpeg |
| ATRAC9 resident | `0x00FF0242` | `0xFE740FE0` | 128 B/frame, 1 frame | ffmpeg |

Wrapper (byte-identical to the gate's `at9_riff_with_config`): `RIFF/WAVE`, `fmt ` 52 bytes,
tag `0xFFFE`, AT9 GUID `d242e147-ba36-8d4d-88fc-61654f8c836c`, `block_align = frame_bytes <<
superframe`, extradata `LE32 0 + config BE + 4 zeros`, one `data` chunk. Decode:
```
ffmpeg -hide_banner -v error -y -i carved.at9.wav -map 0:a -c:a pcm_f32le -f f32le decoded.f32
```
`ffmpeg` (9.0.2 with `atrac9`) is converter-time only; `FFMPEG` overrides the binary.

## Manifest and measured clips

`out/library/audio/audio.manifest` (8 entries; `file` relative to the library root):
```
version=1
name=valus.hurt bank=boss01 file=audio/boss01/valus.hurt.wav rate=48000 channels=1 frames=50456 seconds=1.051167 codec=psadpcm loop=0 gain_db=-0.0873
name=valus.impact bank=boss01 file=audio/boss01/valus.impact.wav rate=48000 channels=2 frames=391356 seconds=8.153250 codec=psadpcm loop=0 gain_db=0
name=valus.telegraph bank=boss01 file=audio/boss01/valus.telegraph.wav rate=48000 channels=1 frames=57680 seconds=1.201667 codec=psadpcm loop=0 gain_db=-0.0873
name=player.death bank=com_b_h file=audio/com_b_h/player.death.wav rate=48000 channels=1 frames=38976 seconds=0.812000 codec=psadpcm loop=0 gain_db=-0.0873
name=player.hit bank=com_obj file=audio/com_obj/player.hit.wav rate=48000 channels=1 frames=34020 seconds=0.708750 codec=psadpcm loop=0 gain_db=-0.0873
name=player.swing bank=com_obj file=audio/com_obj/player.swing.wav rate=48000 channels=1 frames=22316 seconds=0.464917 codec=psadpcm loop=0 gain_db=-0.0873
name=player.hurt bank=com_v file=audio/com_v/player.hurt.wav rate=48000 channels=1 frames=12964 seconds=0.270083 codec=psadpcm loop=0 gain_db=-0.0873
name=music.boss bank=music file=audio/music/music.boss.wav rate=48000 channels=2 frames=6044672 seconds=125.930667 codec=at9 loop=1 gain_db=-10.0912
```

| clip | ch | duration | peak | RMS |
|---|---|---|---|---|
| `valus.telegraph` | 1 | 1.2017 s | 0.990 | -15.7 dBFS |
| `valus.impact` (5 layers, 1/N) | 2 | 8.1533 s | 0.5176 | -20.9 dBFS |
| `valus.hurt` | 1 | 1.0512 s | 0.990 | -12.5 dBFS |
| `player.swing` | 1 | 0.4649 s | 0.990 | -10.3 dBFS |
| `player.hit` | 1 | 0.7088 s | 0.990 | -12.3 dBFS |
| `player.hurt` | 1 | 0.2701 s | 0.990 | -8.7 dBFS |
| `player.death` | 1 | 0.8120 s | 0.990 | -15.5 dBFS |
| `music.boss` | 2 | 125.9307 s | 0.990 | -18.8 dBFS |

`ffprobe` reports `48000,<channels>,<duration>` for all eight; durations match the gate/audition
reference (music 125.93 s; `valus.impact` max layer 8.153 s; the 0x21 clips within one block of
their declared counts). The gate's decode and the audition `--check` still pass; the audition now
prints the music row and says `8 cue(s)`.

## Files

- `src/BpConvert.Core/Audio/AudioCodec.cs` (new)
- `src/BpConvert.Core/Audio/AudioConverter.cs` (new; pipeline, so the verb is testable)
- `src/BpConvert.Core/Audio/AudioManifest.cs` (new)
- `src/BpConvert.Core/Audio/WavWriter.cs` (new)
- `src/BpConvert.Core/Audio/SxdTables.cs` (`CodecConfig`, `TrackNames`)
- `src/BpConvert.Cli/Program.cs` (`audio` verb + usage)
- `tests/BpConvert.Tests/AudioTests.cs` (10 new tests; 21 in the class)
- `tools/re/audio/s5_map.json` (`music.boss`)
- `tools/re/audio/s5_audition.py` (music-row identity/config check; needed for the expanded map)
- artifacts: `out/library/audio/**` (gitignored, not committed)

## Concerns

1. **`file` is relative to the library root, not the manifest directory.** The brief's sample
   (`file=bank/boss01/...`) is not literal: the delivered key is
   `file=audio/<bank>/<event>.wav`, exactly `<out>/<file>`. Task 4's
   `load_manifest(manifestPath, libraryRoot)` must join `libraryRoot + file` (the manifest is at
   `<out>/audio/audio.manifest`). This is the only interpretation consistent with
   `out/library/audio/<bank>/<event>.wav` and the Task 4 signature.
2. **The 5.1 downmix is summed before normalization, not clamped first.** Clamping the raw sum at
   [-1, 1] hard-clips **2.36 %** of Boss 1's stereo samples (its 5.1 master peaks 4.22); that is
   the distortion R6 exists to avoid. The converter sums the formula, normalizes the sum to 0.99,
   then clamps the final output (a no-op after the scale). This is why music's `gain_db` is
   **-10.09 dB** (raw stereo-sum peak 3.16) rather than the recon's -12.5 dB (the 5.1 float peak).
   If the reviewer requires the literal clip-first order, the change is one `Math.Clamp` in
   `DownmixToStereo`, at the cost of that clipping.
3. **PS-ADPCM filters 5-7 are the fitted table**, not firmware coefficients. The manifests and
   tests prove existence/rate/length/non-silence, not fidelity; the user's ear decides the six
   0x21 cue identities.
4. **`ffmpeg` is required to convert AT9** (as the gate already required); it is not a runtime
   dependency and no NuGet package was added. A missing binary is a clear converter error.
5. **`gain_db` is informational.** The WAV is already normalized; the runtime should not apply
   the gain a second time (the value records what normalization removed).
6. **The music track is named by event in the manifest** (`name=music.boss`, not `"Boss 1"`):
   values are space-free by contract (the R1 line is space-separated), and the sample's
   `name=valus.impact` shows the manifest name is the event key. The map keeps `"Boss 1"` for
   selection/validation.
7. **`codec` is `psadpcm` for the 0x21 rows**, not the brief sample's `at9`; that sample row was
   illustrative (the real `valus.impact` is PS-ADPCM). `mixed` is emitted only if a cue's layers
   ever span codecs.
8. **`valus.impact` peaks at 0.5176** because the five layers are mixed with equal `1/N` gains
   (the gate's mix); the runtime gain, not the clip, sets its loudness. This matches the
   attribution note's "safe first-slice" decision.
9. **Memory:** the music path holds the 172 MB `.sxd2`, ~145 MB of 5.1 float, and the 48 MB
   stereo mix at once (the verb run is ~2 s). A streaming decode would reduce the peak if that
   matters later.
10. **`s5_audition.py` was extended** for the new streamed cue (the map change otherwise breaks
    its documented `--check`). It checks identity/config only; Task 3's tests do the 126 s decode.
