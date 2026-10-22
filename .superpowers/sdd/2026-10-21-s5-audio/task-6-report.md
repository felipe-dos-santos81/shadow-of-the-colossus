# Task 6 report — the S5 acceptance, the results note, the docs

Status: **DONE** (2026-10-21). The acceptance ran to `s5-acceptance: PASS —`; the listening walk
stays the user's (PENDING in `docs/notes/s5-results.md` §12).

## What I built

### `scripts/s5-acceptance.sh` (new, bash 3.2)

The house pattern of `scripts/s42-acceptance.sh` (timestamped steps, `fail`, a `PASS —` summary
line, `caffeinate -dimsu` on every viewer and acceptance call, `exec > >(tee …)` into
`out/s5-acceptance-run.log`). Steps:

1. **Builds** — `dotnet build BpConvert.sln -v q`, `dotnet publish src/BpConvert.Cli -c Release
   -o out/bin`, `cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release` + `cmake --build`.
2. **Suites** — `dotnet test tests/BpConvert.Tests -v q`, `ctest --test-dir app/build
   --output-on-failure`.
3. **Clean + decode** — removes the acceptance's own `out/s5/` artifacts (keeping
   `out/s5/gate/` and `out/s5/audition/`, which belong to Tasks 0/1 and which this script never
   reads), then `caffeinate -dimsu dotnet exec out/bin/bp-convert.dll audio --root … --out … --map
   tools/re/audio/s5_map.json --force`. Asserts 8 manifest entries, the manifest's `name=` set ==
   the map's `cues` events (the deferred array excluded), every `file` exists under `$SOTC_LIB`,
   and `ffprobe` reports 48 kHz / the manifest's channel count / duration (±0.05 s) for each.
4. **Timeline** — `caffeinate -dimsu sotc-viewer … --s5-selftest --audio-mixdown
   out/s5/mix.wav`; captures the whole log into `out/s5/s5-selftest.txt`. Asserts exactly 18
   `s5-event` lines, all 18 well-formed with the anchored pattern `t=%.3f event=<token>
   clip=<dotted manifest name> gain=%.2f` (structurally rejecting `bank/name`, R16); music_start
   at 2.050 s after the first `strike_hit`, clip `music.boss`, gain 0.45, once; `stomp_start`
   before `stomp_impact` and `club_start` before `club_impact`, each with its telegraph/impact
   clip; `hurt_light`/`hurt_heavy` on their impact timestamps with gains 1.00/1.25; `death` at
   19.367 s with `player.death`; `music_stop` at 20.483 s **after** the death and explicitly not
   at 19.367; every `clip=` a manifest name; `s5-status` the last line and exactly `mixdown=<path>
   events=18 music=1`.
5. **Mixdown** — `s5-mix` RMS > 0.01, peak ≤ 0.99, `window_rms` > 0.05, frames inside 44.57 s
   ± 0.2 s; `ffprobe` → `pcm_f32le`, 48 kHz, 2 ch, duration 44.57 ± 0.2 s, and the frames match
   the WAV's duration at 48 kHz (±1 frame); independent `ffmpeg -af astats` Overall block → peak
   < 0 dBFS, RMS > −40 dBFS, 0 NaNs.
6. **Audio-off identity** — `--s42-selftest` (mixer off) into `out/s5/s42-audio-off.txt`; its
   `s42-*` lines `diff -u` against the S5 run's; the diff must be empty.
7. **Regression** — `caffeinate -dimsu ./scripts/s42-acceptance.sh` (unchanged; it runs S4.1 →
   S4 → S3), its final `s42-acceptance: PASS —`, its perf line parsed for the marginal and gated
   at ≤ 16.7 ms, and the nested S4.1/S4/S3 status lines captured.
8. **Summary** — library/map/manifest/mixdown hashes, the 8 cue rows, all 18 events, the mix and
   astats numbers, the identity count, the statuses, and the `s5-acceptance: PASS — …` line.

### `docs/notes/s5-results.md` (new)

The measured record: the decode gate (SXDF/SXDS, the four boss01 variants, the loader formula,
33/33 5.1 music), the attribution table (7 cues, `valus.impact` chain-verified, six audition,
identity/fidelity), the 8 shipped entries with durations/channels/bytes/mean/max/gain_db, the
18-event sequence and what each assertion pins, the mixdown numbers, the audio-off identity, the
S4.2 acceptance/perf, R6/R12/R14/R15/R16 with the gate-note paths, the deferred cues (with the
design §12 items marked deferred, not failed), and the **PENDING** user listening checklist
adjusted to the shipped slice.

### `app/README.md` — new `## Audio` section, S5 status, `--s5-selftest`/`--audio-mixdown`
options, the `s5-event` block, `M` in the controls, the audio install command, the curated slice
and the deferred cues.

### `AGENTS.md` — S5 status (scripted green, user walk PENDING), the layout row, the audio
converter command, `M` in the run controls, the deferred cues, and the acceptance command
`caffeinate -dimsu ./scripts/s5-acceptance.sh` (S5 + S4.2 → S4.1 → S4 → S3), with the frozen
regression list extended to `s42`.

## The acceptance run

`caffeinate -dimsu ./scripts/s5-acceptance.sh`, stamped `2026-10-22T00:37:34Z`–`00:46:17Z`
(8 min 43 s). Final line, verbatim:

```text
s5-acceptance: PASS — build, both suites, 8 curated clips (7 attributed cues + Boss 1) decoded at 48 kHz (manifest names = map events, every file present), 18 ordered s5-events (music on the first landed hit at 2.050 s, telegraph before every stomp/club impact, hurt light/heavy at their impacts, death at 19.367 s, music stop at the reset 20.483 s), mixdown 2139200 frames / 44.566667 s pcm_f32le 2 ch (rms 0.0514, peak 0.9022, window_rms 0.1520; astats peak -0.894302 dBFS, RMS -25.777860 dBFS, no NaN), s42-* identical with audio off, marginal 6.92 ms/frame, S4.2+S4.1+S4+S3 regressions green
```

Measured numbers from the same run:

| what | value |
|---|---|
| `dotnet test` | **140 passed**, 0 failed (50 s) |
| `ctest` | **2/2** (unit 10.69 s, windowed 0.18 s; total 10.87 s) |
| converter | `audio: 8 written, 0 reused, 8 manifest entries` |
| manifest sha256 | `7ce2ea6c9a46a8bce68f192b9c9b165f51b47f3296a3af12dc9252be6064e117` |
| `s5-event` lines | **18**, 18 well-formed |
| `s5-mix` | `frames=2139200 rms=0.0514 peak=0.9022 window_rms=0.1520` |
| WAV | `pcm_f32le 48000 Hz 2 ch 44.566667 s`; sha256 `612a66d9f9a6075a9f9107ec5dd84e62286d7c41342bdffef7e889136457f023` |
| `ffmpeg astats` Overall | peak **−0.894302 dBFS**, RMS **−25.777860 dBFS**, 0 NaNs, 0 Infs |
| audio-off identity | **11/11** `s42-*` lines identical (empty diff) |
| S4.2 regression | `s42-acceptance: PASS — …`, marginal **6.92 ms/frame (~144.5 fps)** |
| S4.1/S4/S3 | `s41-status grab=left_foot climb=4.77 …`, `s4-status … frames=423`, `s3-status … frames=240` |

## Files

- `scripts/s5-acceptance.sh` (new, executable)
- `docs/notes/s5-results.md` (new)
- `app/README.md` (audio section, S5 status, options, `M`)
- `AGENTS.md` (S5 status, commands, deferred cues, acceptance)

Commit: `docs: S5 acceptance and results` (`a0d1f4a`) — the script (mode 100755), the results
note, `app/README.md` and `AGENTS.md`.

## Concerns

- **The script deviates from the brief on one point:** it cleans `out/s5/` except `gate/` and
  `audition/`, instead of `rm -rf out/s5`. Those directories are Tasks 0/1's recorded gate
  evidence, the results note cites them, and this script never reads them — deleting them would
  destroy evidence without strengthening an assertion. The acceptance-owned artifacts (mixdown,
  logs, diffs) are still removed first, so a stale `mix.wav` cannot pass.
- **`valus.impact` is 8.15 s.** It is the five-layer ground impact mixed to the longest layer
  (w28 is 8.153 s), so the cue rings well past the 0.37 s stomp window and the 0.43 s club
  window. The acceptance proves it plays and is non-silent; whether it sounds like an impact or
  a rumble is the user's ear (checklist item 4, added in the fix pass).
- **Identity is not scripted.** Six of the seven cues are audition picks (medium identity,
  medium fidelity) and the 0x21 decode uses fitted filters; the scripted pass cannot detect a
  wrong-but-plausible cue. The note labels each method/confidence and keeps the listening
  checklist PENDING.
- **Music stop on defeat is implemented but not scripted.** The S4.2 timeline never defeats
  Valus, so the script pins the reset stop (20.483 s) and the assertion against a death stop;
  the defeat path is covered by the unit tests, not by this acceptance.
- **The marginal varies between runs** (5.16, 9.70 and 6.92 ms/frame in this worktree's
  sessions); all are well inside the 16.7 ms gate, but the number is a machine measurement, not
  a fixed value.
- **The decoded audio is gitignored** (assets and derived audio never committed), so the
  acceptance rebuilds the slice from the game tree each run; the game tree is required.

---

## Fix pass — review follow-up (commit `e61f7f2`)

Four Important findings and six minors, all fixed; the acceptance was re-run to `PASS —`.

### 1 — the mixdown gate could not tell music+SFX from music-only

The old gates (`rms > 0.01`, `peak <= 0.99`, `window_rms > 0.05`) pass a mix with every SFX
voice dropped: `Boss 1` alone tops out at 0.45 (voice) × 0.85 (master headroom) = **~0.38**. New
assertions:

- **peak ≥ 0.5** — the music cannot reach it (`s5-mix peak=0.9022`).
- **a segment comparison via `ffmpeg astats`**: the stomp window (16.233–16.733 s) must be at
  least **6 dB above** a music-only window (5.000–6.000 s). Measured **−15.408 vs −30.509 dBFS =
  +15.1 dB**, and the music-only window must itself be non-silent (> −40 dBFS). A capture that
  lost its SFX voices has the same level in both windows.

The results note §6 now cites the assertions it actually makes (and names the ~0.38 music-only
ceiling); the summary and PASS line print the window numbers.

### 2 — the perf claim was false

The S4.2 perf pair passes `--entity-time 0`, which sets `entityTimeSet` and makes `audio_wanted`
false in `app.cpp`: **no audio subsystem is constructed**, so the marginal is the silent render
path. The note's claim that it "loads the same audio slice" was wrong. The note (§8, §11 item
fps, checklist item 11) and the script header and PASS line now say the gate covers the
**unchanged S4.2 render path**, that the S5 selftest is an offline mixdown and cannot price the
mixer, and that the **audio-on frame cost is unmeasured** — the user's fps walk is the check.

### 3 — the club order was checked for one of two cycles

The timeline has two club cycles (17.250 → 17.683 and 18.933 → 19.367); the old check compared
the first start with the first impact only. The script now asserts **two** `club_start` and
**two** `club_impact` lines, that `start1 < impact1 < start2 < impact2`, and that each line
carries its clip (`valus.telegraph` / `valus.impact`), so inverting only the second cycle fails.

### 4 — the deferred-cue provenance was false

The map has a `deferred` array with eight rows; the club whoosh and the footstep set have no
entry at all, and the earlier "absent from the cue map" wording contradicted the next line. The
results note §11 and both `app/README.md` passages now say: **none is in the map's `cues` array;
eight are listed under `deferred` with a reason each (`valus.death`, `valus.grab`, `valus.fall`,
`valus.breath`, `valus.ambience`, `player.grab`, `player.fall`, `player.breath`); the club whoosh
and footsteps have no entry.**

### Minors folded in

- The `valus.impact` **8.153 s tail** (five layers padded to the longest, w28) is disclosed in
  the results note §4, and a new checklist item 4 asks whether the ring reads as an impact or a
  rumble (the other items were renumbered; fps is now item 11).
- `app/README.md`: the music stop is **1.116 s after the death**, not "one step".
- "the mixer off" → **"no audio subsystem constructed"** (the identity run uses
  `--s42-selftest`, which never creates the seam) in the script stamp, the script header and the
  results note §7.
- `strike`/`strike_hit`/`valus_reaction` now assert their clip and gain (`player.swing` 0.60,
  `player.hit` 0.60, `valus.hurt` 0.60).
- `astats` **Infs** are parsed and asserted 0 (the note's "0 Infs" is now checked, not just
  observed), and the summary/PASS line say "no NaN/Inf".
- `need` now includes `diff`, `cut`, `tr` and `tee`.
- `app/README.md` now says a missing curated cue is fatal in `--s5-selftest` and **warns once and
  runs the scene silent** in interactive play (the whole mixer is disabled, never a partial mix
  or placeholder).
- The results note's "all under `caffeinate`" is reworded: the viewer/acceptance runs are
  wrapped; the builds and suites are plain commands.

### Re-run

`caffeinate -dimsu ./scripts/s5-acceptance.sh`, stamped `2026-10-22T01:19:16Z`–`01:28:09Z`
(8 min 53 s). Final line, verbatim:

```text
s5-acceptance: PASS — build, both suites, 8 curated clips (7 attributed cues + Boss 1) decoded at 48 kHz (manifest names = map events, every file present), 18 ordered s5-events (music on the first landed hit at 2.050 s, telegraph before each of the two stomp/club cycles, hurt light/heavy at their impacts, death at 19.367 s, music stop at the reset 20.483 s), mixdown 2139200 frames / 44.566667 s pcm_f32le 2 ch (rms 0.0514, peak 0.9022, window_rms 0.1520; stomp window -15.408139 dBFS is 15.1 dB above the music-only window -30.509470 dBFS; astats peak -0.894302 dBFS, RMS -25.777860 dBFS, no NaN/Inf), s42-* identical with audio off, marginal 9.25 ms/frame on the unchanged S4.2 render path (the audio-on frame cost is not measured), S4.2+S4.1+S4+S3 regressions green
```

Other numbers from the re-run: `dotnet test` 140 passed (50 s); `ctest` 2/2 (10.73 + 0.21 s);
`astats` Infs 0; identity 11/11; S4.2 perf 8.709 s / 10.919 s / **9.25 ms/frame**; the mixdown
and its sha are unchanged (`612a66d9…57f023`), as is the manifest sha (`7ce2ea6c…64e117`).

Files in the fix commit (`e61f7f2`, `fix: harden the S5 acceptance gates`):
`scripts/s5-acceptance.sh`, `docs/notes/s5-results.md`, `app/README.md`. Tree clean.

Concerns after the fix: the **audio-on frame cost is still unmeasured** (deliberate; the fps
walk is the check), the marginal moves with the machine (6.92 ms then 9.25 ms across the two
runs), and the segment comparison pins two fixed windows of the deterministic timeline — if the
timeline's clocks move, the windows must move with it (the note documents both times).

## Whole-branch review fix wave (2026-10-22)

Five Important findings (no Critical) and the cheap minors, fixed in one wave on top of `e61f7f2`.
Waivers R17 (the spec's 240-frame audio-on gate) and R18 (the defeat-path coverage) are recorded;
R18 is implemented here, R17 is respected (no measurement is claimed).

### Important 1 — the defeat state flushed the device queue every frame

`AudioEngine::stop_music` now returns early unless the music voice is live, and it no longer
calls `clear_stream`: the ~50 ms prebuffer (which carries the killing blow's own `player.swing` /
`player.hit`, queued the same frame) drains instead of being chopped, and the ~50 ms music tail
is imperceptible. `set_muted(true)` and `reset()` still clear. `AudioSeam::stop_music` is
idempotent on `musicStarted`, so the per-frame defeat calls are no-ops and log once.

Test (`app/tests/test_audio.cpp`): *stopping the music leaves the queued SFX intact* — music + a
2 s SFX, primed; `stop_music` twice; the music voice is inactive, the SFX is still playing, the
queue is still non-zero after both calls (a flush reads 0), and a following 1 s mixdown peaks at
`0.4 × 0.85 = 0.34` (SFX alone; music would have added 0.5 × 0.7). `reset()` still empties the
queue. The old "stop drops the prebuffer" assertion was inverted with the semantics.

### Important 2 — the defeat path had no coverage (R18)

`--s5-selftest` only: after the `s42-status` line, a synthetic post-reset defeat phase starts the
score a second time, sets `encounter.defeated = true`, and runs one shared-timeline `step_frame`,
which executes the app's own `if (encounter.defeated) audio.stop_music()`. It throws if the voice
is still live after the step. New lines (verbatim, deterministic):

```text
s5-event t=44.566 event=music_start clip=music.boss gain=0.45
s5-event t=44.566 event=music_stop clip=music.boss gain=0.00
s5-status mixdown=…/out/s5/mix.wav events=20 music=1 defeat_stop=1
```

The phase is after the s42 status, so the **`s42-*` lines stay byte-identical** (11/11 diffed
empty). Event count 18 → **20**; the mixdown grows one 1/60 s tick (800 frames) to **2,140,000**;
the acceptance asserts the two events are adjacent, share t=44.566, follow the reset's stop, and
that `defeat_stop=1`. This proves the **stop wiring**, not the real weakpoint defeat: that is the
S4.1 acceptance (audio off) + the user's listening walk, as the results note now says (R18).

### Important 3 — scoped Delete

`AudioConverter.Run` prunes on a full-map run (`--events`/`--music` absent): every manifest entry
whose `(bank, name)` is outside the selected set is removed, its WAV deleted (path checked inside
the output library, weakly canonicalized), and reported in the new `AudioConvertResult.Pruned`
(and as `  prune bank/name file` log lines). Scoped runs keep the cumulative merge.

Test (`tests/BpConvert.Tests/AudioTests.cs`): *FullMapRun_PrunesStaleEntries_ScopedRunKeepsThem* —
two scoped runs, then a stale manifest entry + WAV; a scoped run keeps both (3 entries), the
full map reports `test/stale` pruned, writes 8 entries, and the stale entry and file are gone.

### Important 4 — audio-on frame cost (R17, no code)

No doc claims an audio-on measurement: the results note §8, the script header step 7, §12 item 11
and the PASS line all say the 9.07 ms marginal is the **silent** S4.2 path and the audio-on cost
is unmeasured. Nothing was added.

### Important 5 — the gate and the C# decoder can drift

New test *Decode_0x21_MatchesTheAuditedGateHash*: com_obj wave 7 (`player.swing`, `0x00FF0121`,
12,752 B), first 4,800 samples (100 ms) quantized to s16 and SHA-256'd, compared to the value the
gate's Python `ps_adpcm_decode` produces for the same carved bytes:

```text
f99204f54a08b6b935960a97b690ec9d9dff07f885434e32b378dbbb5e3c6005   (matched)
```

The command and inputs are recorded in the test comment. The predictor table is now pinned
end-to-end, not just by duration/RMS/peak.

### Cheap minors

- Music-only ceiling: the missing **0.7 music category gain** is restored — `0.45 × 0.7 × 0.85 ≈
  0.27` — in `docs/notes/s5-results.md` §6 and `scripts/s5-acceptance.sh` (header + peak floor
  comment and failure text).
- `audio.cpp` rejects a `..` component in a manifest `file=` (weakly canonicalized prefix check
  under `libraryRoot`); new subcase *a .. component escapes the library*.
- `M` ignores `event.key.repeat` (no mute flapping while held).
- `AGENTS.md` matches `app/README.md`: a missing curated cue warns once and runs silent
  interactively; `--s5-selftest` is fatal.
- `audio.h`: the device opens on the first `play*`, not `update` (comment fixed), and the
  `stop_music`/`clear_stream` contracts now say which call flushes.
- The decode inventory measures **every WAV** with `ffmpeg astats` (RMS in (−40, 0) dBFS, peak ≤
  −0.08 dBFS = 0.99) and prints rms/peak per cue, so one quiet cue fails alone.
- SDD records committed (`git add -f .superpowers/sdd/2026-10-21-s5-audio/`), workspace kept.
- Results note wording: "no audio subsystem constructed" → the seam/engine exist but no manifest
  is loaded and no device is opened (§1, §7, §8); "the sound the game ships" → the gate's
  t=0 equal-gain mix of the five layers, layer starts unknown (§4).
- `s5_decode_gate.py`: numpy is a documented dependency and a missing import fails with one line.

### Re-run

`caffeinate -dimsu ./scripts/s5-acceptance.sh`, stamped `2026-10-22T03:03:36Z`–`03:11:35Z`
(7 min 59 s). Final line, verbatim:

```text
s5-acceptance: PASS — build, both suites, 8 curated clips (7 attributed cues + Boss 1) decoded at 48 kHz (manifest names = map events, every file present, every cue non-silent at peak <= 0.99), 20 ordered s5-events (music on the first landed hit at 2.050 s, telegraph before each of the two stomp/club cycles, hurt light/heavy at their impacts, death at 19.367 s, music stop at the reset 20.483 s, R18 defeat stop after the reset), mixdown 2140000 frames / 44.583333 s pcm_f32le 2 ch (rms 0.0514, peak 0.9022, window_rms 0.1520; stomp window -15.408139 dBFS is 15.1 dB above the music-only window -30.509470 dBFS; astats peak -0.894302 dBFS, RMS -25.779484 dBFS, no NaN/Inf), s42-* identical with no audio manifest loaded, marginal 9.07 ms/frame on the unchanged S4.2 render path (the audio-on frame cost is not measured), S4.2+S4.1+S4+S3 regressions green
```

Other numbers: `dotnet test` **142 passed** (49 s; was 140 — the two new C# tests); `ctest` **2/2**
(10.32 s + 0.17 s); identity **11/11** empty diff; mixdown sha256
`4917731af412760fcd9abf97c5208e8f404c221b1fd6c4cb48c0f96e2cf77a53`; manifest sha unchanged
(`7ce2ea6c…64e117`); S4.2 perf 7.803 s / 9.970 s → **9.07 ms/frame (~110.3 fps)**.

Concerns after this wave: the audio-on frame cost is still unmeasured (R17, deliberate), the
`--s5-selftest` defeat phase is synthetic (it proves the guard, not the kill — S4.1 + the ear
remain the real check), and the two audio windows are pinned to fixed timeline clocks (they move
with the timeline).
