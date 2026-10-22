# Task 5 report — the event seam, the wiring and the `--s5-selftest` mixdown

Status: **DONE** (2026-10-21). Commit `feat(app): wire and prove the S5 sound events`.

The fight now drives the Task 4 mixer: `player_step` and the encounter helpers report what
changed, one app-side seam translates those reports into a 64-cue queue, and a single drain per
frame plays them. `--s5-selftest` runs the *unchanged* S4.2 timeline with that observer on, logs
every played cue, captures the mixed blocks as a 48 kHz float stereo WAV, and fails on a missing
cue clip or a silent mix. With audio off (the default) the `s42-*` lines are byte-identical to
the pre-change run — the full stdout is identical, not just the parsed fields.

## What I built

### `app/src/audio_events.h` (new, pure: no SDL, no I/O, header-only)

- `Sfx` (the plan's 18 ids + `Count`), `SfxEvent { id, gain, position, spatial }`,
  `SfxQueue { SfxEvent items[64]; int count; push/clear/empty }` — fixed capacity, the 65th cue
  is dropped, no allocation.
- `EncounterSfx` names the encounter transition explicitly (`StompStart/StompImpact/ClubStart/
  ClubImpact/ClubCancel/ValusReaction/ValusDefeat/Reset`). The plan's one-argument `Encounter`
  snapshot cannot tell a telegraph from an impact, so `append_encounter_sfx(EncounterSfx, at,
  out)` takes the transition the app just made; every encounter cue is spatial at `at`.
- `append_player_sfx(StepEvents, playerPosition, out)` (the plan's `const PlayerFrame&` carries
  no player position, so the app passes the actual position): `struck` → `Strike` (centred) then
  `damageDealt > 0` → `StrikeHit` (spatial at the player), `hurtLight/hurtHeavy` → the hurt
  cues, `died` → `Death`, `grabbed/gripBroke/landed` → their deferred cues. Order is the
  downbeat order: swing → hit → hurt/death.

### `app/src/audio_map.h` (new)

`SoundCue { clip, gain, loop }`, `cue_for(Sfx)` (nullptr = deliberately silent), `sfx_name(Sfx)`
(the `s5-event` token), `missing_cue(has_clip)` (the first offending `Sfx`), and the music
constants (`music.boss`, 0.45).

| `Sfx` | clip | gain |
|---|---|---|
| `Strike` | `player.swing` | 0.60 |
| `StrikeHit` | `player.hit` | 0.60 |
| `HurtLight` | `player.hurt` | **1.00** |
| `HurtHeavy` | `player.hurt` | **1.25** |
| `Death` | `player.death` | 0.80 |
| `StompStart` / `ClubStart` | `valus.telegraph` | 0.60 |
| `StompImpact` / `ClubImpact` | `valus.impact` | 0.50 |
| `ValusReaction` | `valus.hurt` | 0.60 |
| `Grab`, `GripBreak`, `Land`, `ClubCancel`, `ValusDefeat`, `FootLeft`, `FootRight`, `Reset` | — | deferred (nullptr) |
| music (not an `Sfx`) | `music.boss` | 0.45 |

The hurt gains are the brief's pinned 1.00/1.25. The other gains are the mix-balance values this
task had to choose: the shipped clips are normalized to 0.99, and the pinned 1.0/1.25 plus a
1.0 music bed summed over the 0.99 delivery ceiling (measured pre-clamp peak 1.77 at master
0.85 → hard clip). With these gains the same timeline peaks at 0.9022 (see below), so R14's
headroom actually holds on the shipped slice.

### `AudioEngine` (additive to Task 4)

- `begin_capture(path)` / `end_capture()` / `capturing()` / `captured_frames()`: while capturing,
  every `update` appends the mixed block and `ensure_device()` refuses to open hardware;
  `end_capture` writes float32 stereo 48 kHz through `wav_io` and frees the buffer. One capture at
  a time; a second `begin_capture` fails.
- R14 master gain `kMasterGain = 0.85` in the output stage before the clamp.
- The per-voice gain clamp widened from [0, 1] to [0, `kMaxVoiceGain = 2`] so the table's 1.25
  `HurtHeavy` boost is not silently flattened; the category gains (0.7/0.5/1.0) and the device
  lead/flush are unchanged.

### Wiring (`app.cpp` / `app.h` / `main.cpp`)

- One `AudioSeam` (engine + music lifecycle + log/capture) in `run_app`; one translation path
  (`AudioSeam::play_event`, `cue_for` → `play`/`play_ui`/`play_music`) for the interactive loop
  and the shared timeline.
- Loaded for a composed encounter in interactive play and in `--s5-selftest` only. A failed load
  is one warning and a silent game interactively; fatal in `--s5-selftest`. The load gate rejects
  a manifest whose non-null cue clip is absent and names the `Sfx` and clip.
- Hooks: `start_valus_attack`/`advance_valus_attack` (telegraph), `cancel_valus_attack`
  (`ClubCancel`, deferred), `apply_valus_impact` (impact **and** the hurt/death it causes, pushed
  at impact time so the order is impact → hurt), `apply_combat_events` (Valus reaction / defeat),
  `player_step` events (strike/hit/hurt/death/grab/grip/land), and reset.
- Music starts on the first landed hit (`damageDealt > 0`), loops, stops on Valus defeat, on the
  player's death and on reset (R stops every voice). `M` toggles mute. Ambience is not wired:
  no ambience clip ships (R15).
- `--s5-selftest` / `--audio-mixdown PATH` added to `AppOptions` and `main.cpp` (validated: s5
  needs a manifest, needs `--audio-mixdown`, and is mutually exclusive with `--s42-selftest`).
- `--s42-selftest` (and S3/S4/S4.1/`--entity-time`/`--selftest`) never load the manifest; the
  `AudioEngine` object is constructed but stays inert (no load, no play, no device).

### `StepEvents` (additive; no gameplay change)

`struck`, `hurtLight`, `hurtHeavy`, `died`. `struck` is set on a release (hit or whiff), `died`
on a landing that resolves `pendingDeath`; `player_take_damage` gained an optional
`StepEvents* events` out-parameter and reports HurtLight/HurtHeavy for a non-lethal hit (also in
the air) and `died` for a ground-lethal hit. `player_step` gained no calls; the source test pins
that its translation unit contains no SDL/audio include, throughput or console.

## Tests

New `app/tests/test_audio_events.cpp` (8 cases): hurt flags, landed strike vs whiff, grab/grip/
land, encounter impact/telegraph/reaction/defeat/cancel once per beat, queue reset + overflow,
the cue table (every `Sfx`, the shipped slice, every deferred id), and the load gate against the
real manifest (`missing_cue` reports `valus_reaction` when `valus.hurt` is refused).

`test_player_state.cpp` (+4): light/heavy/died flags, `struck` on a landed release and a whiff,
`died` on the landing that resolves a pending death, purity (trivially copyable events,
deterministic steps, no I/O in `player_state.cpp`).

`test_audio.cpp` (+2): the capture API writes a 48 kHz non-silent WAV from played cues, never
opens a device, and refuses a second capture; the master gain test (music + SFX pre-gain 1.15 →
0.9775, a lone full-scale clip → exactly 0.85).

### RED / GREEN

- RED (before the headers existed): `cmake --build app/build -j 8` → `no member named
  'begin_capture' in 'AudioEngine'`, `no member named 'kMasterGain'`, `audio_events.h: No such
  file` (10+ errors across `test_audio.cpp` and `test_audio_events.cpp`).
- GREEN: `cmake --build app/build -j 8` exit 0; `ctest --test-dir app/build -R unit_tests
  --output-on-failure` → 1/1 passed, 10.8 s, **241 doctest cases / 0 failed** (was 227; +14).

## `--s5-selftest` output (exact, `out/s5/s5-selftest.txt`)

```
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
s5-event t=19.367 event=music_stop clip=music.boss gain=0.00
s5-mix frames=2139200 rms=0.0512 peak=0.9022 window_rms=0.1520
s5-status mixdown=/Users/felipe.dos.santos/.local/share/opencode/worktree/548673/s5-audio/out/s5/mix.wav events=18 music=1
```

(18 `s5-event` lines; the music is confirmed by `music_start` after the first landed hit at
t=2.050 and `music_stop` on the death at t=19.367; `s5-status` was the last line, exit 0.)

## s42 audio-off diff

```
$ diff <(grep '^s42-' out/s5/s42-before.txt) <(grep '^s42-' out/s5/s42-audio-off.txt)
(empty)                                   # S42-DIFF-EMPTY
$ diff out/s5/s42-before.txt out/s5/s42-audio-off.txt
(empty)                                   # FULL-DIFF-EMPTY
```

`out/s5/s42-before.txt` was captured from the pre-change binary (HEAD `d0f9ab4`) before any edit;
`out/s5/s42-audio-off.txt` from the final binary. All 11 `s42-*` lines and every other stdout line
are byte-identical.

S4.2 acceptance: **(see §Regression below)**.

## Mixdown stats (`out/s5/mix.wav`)

- `afinfo`: `2 ch, 48000 Hz, Float32, interleaved`, duration 44.566667 s, 2,139,200 frames,
  17,113,600 audio bytes.
- Peak **0.9022** (≤ 0.99), overall RMS **0.0512**, loudest 1 s window RMS **0.1520**, 0 NaN
  samples (checked over all 4.28 M floats). Per-window: music-only window 3.0-4.0 s RMS 0.029,
  stomp impact+hurt window 16.24-16.50 s RMS 0.231 / peak 0.902, post-reset climb silent (the
  deferred grab/climb cues, see concerns).
- The capture wrote exactly one block per `update`; 2,139,200 frames = the timeline's 44.57 s at
  48 kHz, so the capture and the timeline clock agree.

## Files

- New: `app/src/audio_events.h`, `app/src/audio_map.h`, `app/tests/test_audio_events.cpp`.
- Modified: `app/src/audio.h`, `app/src/audio.cpp` (capture + master gain + gain clamp),
  `app/src/player_state.h`, `app/src/player_state.cpp`, `app/src/app.h`, `app/src/app.cpp`,
  `app/src/main.cpp`, `app/CMakeLists.txt`, `app/tests/test_audio.cpp`,
  `app/tests/test_player_state.cpp`.
- Untracked evidence (gitignored): `out/s5/{s42-before,s42-audio-off,s5-selftest}.txt`,
  `out/s5/mix.wav`, `out/s5/s42-acceptance.log`.

## Concerns

1. **The shared timeline never defeats Valus.** S4.2's timeline resets him at 100 HP after the
   player dies, so `ValusDefeat` and the "music stops on defeat" path are wired and mapped but
   not exercised by `--s5-selftest`; the run's `music_stop` comes from the player's death (and
   reset stops everything). Task 6's acceptance must not require a `valus_defeat` line.
2. **Deferred cues (R15).** `Grab`, `GripBreak`, `Land`, `ClubCancel`, `ValusDefeat`, `FootLeft`,
   `FootRight` and `Reset` map to nullptr. Consequence: after the reset the stamina climb is
   silent (music is stopped), so the capture has a ~23 s silent tail. The run's "silent mix"
   gate therefore requires an audible window somewhere (peak ≥ 0.05, loudest 1 s window RMS ≥
   0.01), not silence nowhere.
3. **Cue gains beyond the pinned hurt values.** The brief pins only `HurtLight` 1.00 and
   `HurtHeavy` 1.25; the other gains are set here so a 0.99-normalized stack stays under the
   0.99 ceiling at master 0.85. R14's arithmetic ("0.7 + 1.0 stays under 0.99") does not hold
   with 0.99 clips without these gains: measured pre-clamp peak was 1.77 → hard clip.
4. **`clip=` carries the manifest `name`** (`player.swing`), not `bank/name`: the engine indexes
   by the R1 `name=` and does not expose the bank. The bank is in the manifest line and the names
   are unique.
5. **Audio off still constructs the empty `AudioEngine`** (two small vectors); it never loads,
   plays, or touches SDL, and the byte-identical `s42-*` diff is the proof. "Do not create the
   engine" is honoured as "do not load/activate it".
6. `M` mute and the interactive loop's live mixing were not exercised headlessly (no window
   input); the mute path is covered by the Task 4 dummy-driver test, and the loop uses the same
   `AudioSeam` translation the timeline proves. The listening checklist remains the user's.
7. The capture holds the whole run in RAM (~17 MB here); `kMixdownSecondsCap` does not bound a
   capture. Fine for the 44.6 s acceptance, a multiplication for a long interactive capture (not
   exposed).
8. `--s5-selftest` writes the WAV only at `end_capture`; a crash loses the mix. Intentional (one
   file, no partial artifact).

## Regression

`caffeinate -dimsu ./scripts/s42-acceptance.sh` (with `SOTC_GAME_ROOT`/`SOTC_LIB`) → **exit 0**:

```
s42-acceptance: PASS — build, both suites, s42 timeline (weakpoint proxies 1.955 m and
2.165 m, gate foot 0 / head 34, arm weakpoint 1 damage 34, stomp 35 health 100->65 hurt,
club 45 health 65->20 hurt, range split club 0 at 4 m / stomp 15 at 3 m, death
player_a_fall_damage_to_death, reset 100/100/grounded/Valus 100, stamina 28.425 ->
43.484 m leaving 39 of 100), six stills (2560x1440, span >= 30, > 5000 B), marginal
5.16 ms/frame (~193.8 fps), S4.1+S4+S3 regressions green
```

The run's own `--s42-selftest` values are the same as the pre-change capture, and the final
binary's `--s5-selftest` run prints the same 11 `s42-*` lines as its audio-off sibling
(`diff` empty). Full log: `out/s5/s42-acceptance.log`.

---

## Review fixes (follow-up)

Status: **DONE**; commit `fix(app): stop the music on defeat and reset, not death`.

### Important 1 — the score runs through the player's death (D5)

Both stop sites (`step_frame` and the interactive loop) no longer test
`combat.mode == PlayerMode::Dead`; only `encounter.defeated` stops the music. D5 names defeat and
reset, not Wander's death, so the score now plays through the death clip. The reset (R key and
the timeline's phase 7) still stops everything.

### Important 2 — the reset's stop is observable

`AudioSeam::reset()` now calls `stop_music()` before `engine.reset()`, so the reset emits the same
`s5-event ... event=music_stop ...` line the defeat path does (and reuses its idempotence: no
second line when the music is already stopped). The S5 stream after the fix:

```
s5-event t=19.367 event=club_impact clip=valus.impact gain=0.50
s5-event t=19.367 event=death clip=player.death gain=0.80
s5-event t=20.483 event=music_stop clip=music.boss gain=0.00     <-- the reset, not the death
s5-mix frames=2139200 rms=0.0514 peak=0.9022 window_rms=0.1520
s5-status mixdown=…/out/s5/mix.wav events=18 music=1
```

`music_stop` moved from t=19.367 (the death frame) to **t=20.483** (the reset). The event count is
still 18; the score continues for ~1.1 s past the death (the death clip plus the pin steps) and
the reset then stops it. The mixdown is still non-silent with the same peak (0.9022 ≤ 0.99); RMS
moved 0.0512 → 0.0514 because the music bed now covers the death tail.

### Minors

- **`music.boss` is in the load gate.** `AudioSeam::load` checks `engine.has(kMusicClip)` after
  `missing_cue` and fails with `music cue names clip 'music.boss', which <manifest> does not
  load`; verified against a temp library whose manifest dropped the music line (exit 1, no
  timeline run).
- **CLI mutual exclusion.** `--s5-selftest` with `--selftest`, `--s3-selftest`, `--s4-selftest`,
  `--s41-selftest` or `--s42-selftest` is a usage error (exit 2: "cannot be combined with another
  selftest; pass one"), so no earlier branch can exit 0 without `s5-status`. All five checked.
- **Shared-block error text** names the invoked flag: `--s5-selftest`/`--s42-selftest` is selected
  once (`selftestFlag`) and used by the missing-entity and missing-fight-clip messages.
- **`clip=` deviation documented at the log site**: the token is the R1 manifest `name=` (e.g.
  `valus.impact`), not `bank/name`; the engine exposes no bank, and ledger R16 makes the manifest
  name the contract Task 6 parses.

### Re-run (final binary)

- `cmake --build app/build -j 8` exit 0; `ctest -R unit_tests --output-on-failure` → 1/1 passed,
  10.8 s, **241 cases / 0 failed**.
- `--s5-selftest --audio-mixdown out/s5/mix.wav` exit 0; 18 `s5-event` lines, `music_stop` at
  t=20.483 (above), WAV 2,139,200 frames / 44.57 s, RMS 0.0514, peak 0.9022, loudest 1 s window
  0.1520, no NaN, `s5-status … events=18 music=1`.
- `--s42-selftest` audio off: exit 0; **full stdout diff against the pre-change capture is empty**
  (not just the `s42-*` lines); the S5 run's `s42-*` lines are identical to it.
- S4.2 acceptance: `caffeinate -dimsu ./scripts/s42-acceptance.sh` → **exit 0**, `PASS — build,
  both suites, s42 timeline (…), six stills (2560x1440, span >= 30, > 5000 B), marginal 9.70
  ms/frame (~103.1 fps), S4.1+S4+S3 regressions green` (`out/s5/s42-acceptance-fix.log`). The
  marginal was 5.16 ms in the pre-fix run: machine variance on the same binary class, both inside
  the 16.7 ms budget.

### Deferred (recorded, not changed here)

The silence gate stays "an audible window exists" (the reset still leaves the deferred-cue climb
silent); the death-cue truncation at the reset (the 0.81 s clip is cut by `engine.reset()`); the
"once per beat" test exercises the helper, not the app's once-only impact mark; R14's headroom is
empirical (the cue gains tuned above it, not a limiter); the loop wrap keeps no crossfade; and
`apply_valus_impact` still takes both the queue and the `StepEvents*` (the app passes both, and
the flags are written after the player's cues are pushed, so no double cue). The report excerpt,
unused includes and the informational `SoundCue::loop` field remain as listed above.

Process note: the music-gate negative test initially overwrote the shared
`out/library/audio/audio.manifest` through a symlinked temp library; the exact `music.boss` line
was restored (9 lines, original order), and the shipped-manifest unit test (8/8 clips) plus the
identical S5 output confirm it. The library is a generated artifact that `bp-convert audio`
rewrites.
