# Task 4 report — the app's audio engine

Status: **DONE** (2026-10-21). Commit `feat(app): the S5 audio engine`.

The engine loads the Task 3 R1 manifest, mixes 32 voices at 48 kHz float stereo, pushes one
SDL3 audio stream (opened and primed on the first play), and writes the same mix to a float32
WAV headlessly. The mixer is pure math: `render_mixdown` never touches SDL, and only the
dummy-driver smoke test opens an audio device.

## Files

- `app/src/audio.h` / `app/src/audio.cpp` — `AudioCategory`, `AudioClip`, `AudioEngine`.
- `app/src/wav_io.h` / `app/src/wav_io.cpp` — float32/PCM16 RIFF reader, float32 writer.
- `app/tests/test_audio.cpp` — 12 doctest cases (the 8 required plus PCM16/rate, slot
  replacement, the dummy-device lead, and the shipped-manifest load).
- `app/CMakeLists.txt` — `src/audio.cpp` + `src/wav_io.cpp` on both targets,
  `tests/test_audio.cpp` on the test target.

## Manifest load

`load_manifest(manifestPath, libraryRoot)` joins `libraryRoot + "/" + file`, rejects a missing
manifest (clears to silence, one warning), a malformed `key=value` line, a duplicate `name`, an
absolute `file=` (it would escape `libraryRoot`), a missing/unsupported WAV (naming the entry
and the path), and an empty clip. A parse/load failure is transactional: it leaves the previous
clip set and voices untouched, so no voice ever points at freed samples (a missing manifest is
the one case that empties the engine). `gain_db` is parsed as a field but never applied (R12);
the clip keeps the converter's normalized samples.

Measured against the real library (`SOTC_LIB=<repo>/out/library`):

- 8 clips loaded: `valus.hurt`, `valus.impact`, `valus.telegraph`, `player.death`, `player.hit`,
  `player.swing`, `player.hurt`, `music.boss` (stereo, `loop=1`, 6,044,672 frames).
  `valus.impact` is the other stereo clip; the remaining six are mono. `has()` is true for all
  eight and the manifest test asserts 11 conditions.

## Mixer math

- Output: 48 kHz stereo float, preallocated `96000`-float buffer; category gains music 0.7,
  ambience 0.5, SFX 1.0; per-play gain clamped to [0, 1].
- Voices: slot 0 music, slot 1 ambience, slots 2–31 SFX (30). A new SFX takes a free slot, else
  steals the smallest serial (oldest). `play_music`/`play_ambience` replace their one slot.
- Spatial SFX (only `play` with `spatial=true`): `d = |position - listener|`,
  `atten = 1 / (1 + d/8)`; `dir = (position - listener)/d`; yaw 0 looks along +Z, so
  `right = (-cos(yaw), 0, sin(yaw))`; `pan = clamp(dot(right, dir), -1, 1) * 0.7`;
  constant-power gains `L = cos((pan+1)·π/4)`, `R = sin((pan+1)·π/4)`. At d = 0 the voice is
  unattenuated and centred (−3 dB per side).
- Non-spatial voices (music, ambience, `play_ui`) use the same gain on both channels.
- Loops wrap `cursor` modulo the clip's frame count; one-shots deactivate at the end. Mono is
  duplicated to both channels; stereo maps ch0→L, ch1→R.
- Mute zeroes the block after the voices advance; the final stage sanitises non-finite samples
  and clamps to [−1, 1].

## Device path

The first `play*` call opens the device: `SDL_InitSubSystem(SDL_INIT_AUDIO)` →
`SDL_OpenAudioDevice(DEFAULT_PLAYBACK, nullptr)` → `SDL_CreateAudioStream(float stereo 48k →
nullptr)` → `SDL_BindAudioStreams`, then immediately primes ~3 blocks (50 ms) of mixed audio.
`update(dt, listenerPos, yaw)` clamps `dt` to [0, 1] s, pushes that block, and tops the queue
back up to the ~50 ms lead (≤ 8 blocks a call, hard cap 0.5 s). A timeline that never plays
never opens a device; `update` only advances voices then. Every device failure logs once and
stays silent. `render_mixdown(path, seconds)` runs 800-frame (1/60 s) steps through the same
`mix()` and writes via `wav_io`; it never opens a device.

Dummy-driver check (throwaway program, not committed): `clips=8`, `after play: queued=19200
active=2`, 240 updates kept `active=1 music=1`, `after mute: queued=0`, `after
unmute+update: queued=19200`, exit 0, and `afinfo /tmp/s5-smoke-mix.wav` reported
`2 ch, 48000 Hz, Float32, interleaved`, duration 1.000000 s, 384000 audio bytes.

## RED / GREEN

- RED (before implementation): `cmake -S app -B app/build` →
  `Cannot find source file: src/audio.cpp`, `No SOURCES given to target: sotc-viewer` and
  `sotc-app-tests`. The tests reference `audio.h`/`wav_io.h`, which did not exist.
- GREEN: `cmake --build app/build -j 8` exit 0;
  `ctest --test-dir app/build -R unit_tests --output-on-failure` → 1/1 passed, 10.32 s;
  doctest reports 227 cases / 0 failed (the full pre-existing suite plus the audio cases).
  The shipped-manifest case alone: 1 case, 11 assertions, 0 failed; the dummy-device case:
  1 case, 12 assertions, 0 failed.

## Concerns

- `render_mixdown` takes whole `int` seconds (per the interface), so the brief's 0.5 s mixdown
  test runs 1 s; `clip.frames() == 48000` is asserted for that.
- `update` truncates `dt` > 1 s to 1 s, and drops a push (but still advances voices) when the
  device backlog exceeds 0.5 s — the simulation clock stays authoritative.
- Pan is gain-only (no HRTF/occlusion/reverb), and a stereo spatial clip keeps its own L/R while
  each side gets its pan gain; no spatial stereo clip ships in the slice.
- The device path is covered by a deterministic dummy-driver test plus a smoke run, not by
  audible hardware; the listening checklist (music identity, mix balance, mute, no clipping)
  remains the user's.
- A missing manifest returns `false` with one warning and an empty engine, per the brief; a
  future encounter caller must treat that as fatal when the curated map is required.
- Loud stacks hard-clamp at the output; the shipped clips are peak-normalized ≤ 0.99.

## Review fixes (follow-up)

Status: **DONE**; commit `fix(app): prebuffer the audio device and flush on mute`.

- **Prebuffer / top-up.** The first `play*` call opens and binds the stream, then primes
  `kLeadBlocks = 3` blocks of 800 frames (50 ms). Every `update` pushes the `dt` block and calls
  `top_up()`, which mixes and pushes more 16.7 ms blocks until the queue is back at the 50 ms
  lead, bounded to 8 blocks per call and the existing 0.5 s cap. `top_up` is the only extra
  mixing: a steady real-time loop pushes exactly `dt`, so the lead stays ~50 ms instead of the
  old one-block hover against a ~20 ms device buffer.
- **Flush on mute/stop/reset.** `set_muted(true)`, `stop_music()` and `reset()` call one shared
  `clear_stream()` (`SDL_ClearAudioStream`), so the prebuffer cannot trail a mute/stop by 50 ms.
  Unmuting does not clear; the queued muted blocks finish and `update` resumes topping up.
- **Open on first play.** `ensure_device()` moved out of `update` into `play`, `play_music` and
  `play_ambience` (after the slot is filled, so the primed lead contains the new voice). A
  headless timeline that never plays (`--selftest`, `--s42-selftest`) never attempts a hardware
  open; `update` advances voices and only tops up when a stream exists.
- **Guard and cap.** `mix()` asserts `frames * 2 <= mixBuffer_.size()`; `kMixdownSecondsCap`
  raised from 120 to 600 s, so the full 125.93 s `music.boss` can be dumped.
- **Manifest scope.** An absolute `file=` is rejected with
  `file= must be libraryRoot-relative, got '…'`; the load-rejection test grew that subcase.
- **Docs and includes.** The `audio.h` load contract now says parse/load failures are
  transactional while a missing manifest empties the engine; `audio.cpp` directly includes
  `<cassert>`, `<cstddef>`, `<cstdint>`, `<filesystem>`, `<unordered_map>`, `<unordered_set>`,
  and `wav_io.cpp` includes `<filesystem>`.
- **Device test.** `test_audio.cpp` sets `SDL_AUDIODRIVER=dummy` in a static initializer (before
  main and the first SDL audio init), so the device path is deterministic without touching
  hardware. `TEST_CASE("the dummy device gets a primed lead and clears on mute")` asserts:
  no device before the first play, `queued_audio_bytes() > 0` after `play_ui`, a top-up keeps it
  positive across `update`, mute/reset clear to 0, and unmute+update primes again
  (12 assertions; 1/1 case green). It runs in the same `unit_tests` binary as everything else.
- **Re-run.** `SOTC_LIB=<repo>/out/library cmake --build app/build -j 8` exit 0;
  `ctest --test-dir app/build -R unit_tests --output-on-failure` → 1/1 passed, 10.32 s,
  227 doctest cases / 0 failed. Dummy smoke: play 19200 B queued, 240 updates stayed live,
  mute 0 B, unmute+update 19200 B, exit 0.

**Deferred (recorded, not fixed here):** output headroom/limiter beyond the hard clamp; loop
crossfade; gain-only stereo pan; duplicate keys within one manifest line; expected-failure
warnings in the test stderr; the now-unused `AudioEngine::kMaxSfxVoices`; and the transient
whole-file copy in `read_wav`.
