# SDD ledger — plan: docs/superpowers/plans/2026-10-21-s5-audio.md

## Setup (2026-10-21)

- Worktree: `/Users/felipe.dos.santos/.local/share/opencode/worktree/548673/s5-audio`, branch `s5-audio`, base `5c90f2d` (S5 design + plan on `main`; code identical to the green S4.2 merge `abcbdfe`).
- `out/` symlink to `/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out` (library + encounter manifest + `out/re/eboot.elf`).
- Submodules initialized (`third_party/PSO2-Aqua-Library`).
- `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game`; `SOTC_LIB=<worktree>/out/library`.
- Spec: `docs/superpowers/specs/2026-10-21-s5-audio-design.md` — binding; the plan argues from it.

## Pre-flight scan

| Pair / task | Produces → consumes | Finding |
|---|---|---|
| T0 → T1 | Per-bank `WAVE` layout + AT9 config → attribution map | T0's layout is an output, not pinned in the plan; T1/T2 must consume the note, not assume 44/76. |
| T0 → T2/T3 | Decoder command/config → `SxdTables`/`AudioCodec` | T3 must wrap the decoder T0 proves; do not pick a decoder independently. |
| T1 → T3 | `tools/re/audio/s5_map.json` → `--events` | The JSON schema is only exampled; R2 pins it. |
| T1 → T5 | Event keys → `app/src/audio_map.h` | `cue_for(Sfx)` uses the event keys; load validation against the manifest. |
| T2 → T3 | `SxdBank`/`WaveRecord` → carver/decoder | `WaveRecord.Start` is relative to the payload base; R3 pins which base. |
| T3 → T4 | `audio.manifest` → `AudioEngine::load_manifest` | The plan under-specifies the keys; R1 pins the line format. |
| T3 → T5 | Clip names → `audio_map.h` | Every non-null cue must exist in the manifest, or load fails. |
| T4 → T5 | `AudioEngine` → selftest/wiring | Signature fixed in the plan; T5 uses `play/play_music/play_ambience/update/reset`. |
| T4 ↔ T5 | Both modify `app/CMakeLists.txt` | Sequential only. |
| T5 → T6 | `s5-event` lines + mixdown → acceptance | Line format under-specified; R4 pins it. |
| T6 → S4.2 | Invokes the frozen script | No modification; `s42-acceptance.sh` only. |
| T0 self | Gate script + note | Files/check agree; hard stop defined. |
| T1 self | Audition script + map + note | Core-cue check agrees with the map output. |
| T2 self | Reader/tables + tests | Tests match the interfaces; `com_sys` partition allows the 24-byte head. |
| T3 self | Codec/WAV/manifest + tests | Tests match the interfaces; R1 supplies the missing keys. |
| T4 self | Engine + tests | `play(position)` matches the distance/pan tests. |
| T5 self | Seam + wiring + selftest | `StepEvents` fields and `Sfx` table agree; audio-off byte-identical assertion. |
| T6 self | Script + docs | Assertions match T5's log; the walk is PENDING. |

No task mandates an empty test or a duplicated logic block. The gates are hard stops, as the spec requires.

## Rulings (pre-flight)

- **R1 — Manifest line format.** One entry per line, `key=value` pairs separated by spaces; first line `version=1`; keys exactly `name bank file rate channels frames seconds codec loop gain_db`; sorted by `(bank, name)`. T3 writes it, T4 parses it, T5/T6 read the same names. Cost if wrong: the app cannot load the curated set.
- **R2 — `s5_map.json` schema.** `{"version":1,"cues":[{"event","bank","name","wave","method","confidence"}]}`; `wave` is the table index; `method` ∈ `table|ghidra|audition`; `confidence` ∈ `high|medium|low`. T1 writes it, T3's `--events` reads the names, T5 transcribes the event keys. Cost if wrong: one JSON shape to adjust.
- **R3 — `WaveRecord.Start` base.** `Start` is relative to the bank's payload base: `DATA` chunk body (resident) or the `.sxd2` `SXDS` body at `0x60` (streamed). The verb resolves the base from the bank type; `ByteLength` stays the record's declared length. Cost if wrong: every carved stream is offset/wrong.
- **R4 — `s5-event` format.** `s5-event t=<seconds:.3f> event=<name> clip=<bank/name> gain=<x.2f>`; one line per played event, stable order as emitted by the timeline. T5 emits, T6 asserts anchored prefixes and order. Cost if wrong: acceptance parsing churn.
- **R5 — Workspace.** Worktree + shared `out` symlink, per the user's earlier isolation choice. Cost if wrong: conversions mutate the gitignored library (intended).

## Rulings (Task 0 findings, 2026-10-21)

- **R6 — Music is 5.1; the runtime is stereo.** The converter downmixes decoded 5.1 to stereo (L/R + 0.707·C + 0.5·LFE/surround, clipped) and normalizes each clip to peak ≤ 0.99, recording the applied gain in the manifest. The app mixer stays stereo. Cost if wrong: some surround content is lost, which the no-3D scope already accepts.
- **R7 (updated 2026-10-21, gate review).** `0x42` = **ATRAC9** at 96 kbps/ch (record config `FE7007E0` mono / `FE740FE0` stereo), resolved by the loader formula; `0x21` = **PS-ADPCM** (16 B / 28 samples, filters 0–7, 5–7 fitted). All four `boss01` variants decode. The earlier "0x42 is a second unresolved codec" is superseded.
- **R8 (updated 2026-10-21, gate review).** Resident payload = **`wave_record_offset + 0x24 + start`** (Ghidra `FUN_00983220`, `(tag & 2) == 0` branch); the earlier `DATA+0x20 + (start − min(start))` mapping is **disproved**. The loader formula tiles `boss01`/`com_obj`/`com_v`/`com_b_h` with zero gaps; `edp01` has 11 segments (not tiled) and is not in the curated set. Task 2 implements the loader formula, not the old one.
- **R9 — The format-word list is the gate's, not the plan's.** `boss01` has four words (`0x0121`, `0x0221`, `0x0142`, `0x0242`); `0x0642` also occurs in `com_obj`/`edp01`; other resident banks add `0x0104`, `0x0442`, `0x0842`. The gate/note is authoritative; Task 2/3 read the per-record word, never a hard-coded list.

## Task progress

- Baseline: code identical to the green S4.2 merge (`abcbdfe`); docs-only since. No re-run before T0 (the gate is the first new code).
- Task 0: dispatched at base `5c90f2d`. Implementer returned DONE_WITH_CONCERNS (`118f569`): music 33/33 proven, 5.1/48 kHz; resident `0x21` claimed decoded; `0x42` skipped. Pre-review correction requested; fix `85d51a7` **disproved** the `0x21` mapping and identified `0x42` as AT9 with an unresolved payload offset. Gate: music PASS; resident **BLOCKED** (exit 3, no family validated).
- **R10 — Task 0's resident requirement moves into Task 1.** The music gate is complete; the resident decode cannot be proven without the SNDX loader's payload formula, which is Task 1's Ghidra/attribution work. Task 1 now: (a) decompile the loader and derive the wave record/payload formula, (b) prove one `boss01` cue decodes end-to-end, (c) map the core cue names, (d) if (a)/(b) fail, stop and report with the music-only option. Cost if wrong: Task 1 is larger and the milestone may reduce to music-only.
- **R11 — Task 0 and Task 1 share one gate review.** Task 0 is partial by design (music proven, resident reassigned); the combined gate (script + note + attribution) is reviewed after Task 1 resolves or blocks. Cost if wrong: the Task 0 script's music path is reviewed one task later.
- Task 0+1: gate review (`5c90f2d..6fdd9f2`) — spec ❌. **Critical 1:** six core cue rows use the name-pool index as the wave index; `boss01_atk_hit_ground1` is the sound over waves 26–30 (verified chain), not wave 0, so only `valus.impact` is chain-verified. **Important 2:** the audition `--check` cannot fail a missing/deferred core cue. **Important 3:** the audition does not check name↔wave identity. **Important 4:** all seven cues are `0x21`; the fitted filters 5–7 are not firmware-exact and the note's validation is selective. **Important 5:** R7/R8 were stale (now updated above). Fix round 1 dispatched.
- Task 0+1: fix round 1/5 (2 addressed [4,5], 3 open [1,2,3] + 1 new Important [`layers` unvalidated]; commits `6fdd9f2..256fe69`). Re-review: Critical 1 still NOT ADDRESSED (two rows infer name→tone from request order but label it `ghidra`/`chain`); Important 2 still NOT ADDRESSED (a core row with `status: deferred` and a retained wave passes); Important 3 still NOT ADDRESSED (`--map` does not parse SEQD/PLAY_TONE; `tone` unconstrained; `reqd` optional). New Important: `layers` is not decoded by `--check` and its definition is ambiguous. Fix round 2 dispatched.
- Task 0+1: fix round 2/5 (all addressed; commits `256fe69..a0cfaa7`). Re-review approved: only `valus.impact` is `ghidra`/chain-verified; `valus.telegraph`/`valus.hurt` re-scoped to audition; six cues identity-medium; five impact layers decoded and mixed; deferred core fails; `--map` walks SEQD/PLAY_TONE for ghidra rows. Caveats carried to Task 3: the SEQD walker is heuristic (4 KiB scan), layer start offsets are not decoded (mix overlays from t=0), 26-30 layer completeness rests on prose.
- Task 0+1: complete (commits `118f569..a0cfaa7`, gate review clean after 2 fix rounds).
- Task 2: complete (commits `a0cfaa7..6962191`, review clean after one fix; 130 tests; zero-gap tiling on boss01/com_obj/com_v/com_b_h/com_sys; music 33/33 absolute starts).
- Task 3: complete (commits `6962191..57ed5f5`, review approved; 21 AudioTests / 140 total; 8 manifest entries; all 48 kHz).

## Rulings (Task 3)

- **R12 — Normalize then clamp; `gain_db` is informational.** The delivered WAVs are downmixed unclipped, normalized once to peak ≤ 0.99, then clamped; Boss 1's gain is **−10.09 dB** (the recon's −12.5 dB assumed a clipped-first path). The app must not re-apply `gain_db` to the clip; it is provenance/display, and Task 5's music gain comes from the cue table, not from re-scaling the file. Cost if wrong: ~2.4 dB of double attenuation on the music.
- **R13 — Manifest `file` is `libraryRoot`-relative.** `file=audio/<bank>/<event>.wav`; Task 4 joins `libraryRoot + "/" + file` with `libraryRoot = out/library` (the `SOTC_LIB` root). Cost if wrong: the app cannot find the WAVs.
- Task 3 minors (deferred): ffmpeg timeout unreachable/`-nostdin` missing; no decoded-duration sanity check; `--banks/--all` absent; corrupt-manifest write not atomic; no `.sxd1/.sxd2` GUID pairing check; PS-ADPCM rate unconstrained; resident name does not check name→wave; dead channel-map arms; style; coverage gaps (non-tiling carve, unknown event, `--music`, cumulative merge, duplicate Read).
- Task 4: complete (commits `57ed5f5..d0f9ab4`, review approved after one Important fix; 227 doctest cases; 8/8 entries; dummy-driver lead/flush test).
- Task 4 minors (deferred): no master headroom/limiter; loop wrap has no crossfade; pan is gain-only for stereo clips; duplicate keys in a manifest line are silently dropped; 96k `CHECK`s in the mute test; unused `kMaxSfxVoices`; `read_wav` buffers the whole file (~97 MB transient).

## Rulings (Task 4/5)

- **R14 — Master headroom.** The mixer's output stage adds a `0.85` master gain before the clamp so music (0.7) + SFX (1.0) sums stay under 0.99; the per-category gains are unchanged. Cost if wrong: ~1.4 dB lower output; the device volume compensates.
- **R15 — The first slice is the gate's seven cues + music.** `valus.death`, the club whoosh, player fall, ambience, grab/climb/footsteps are **deferred**; the design's §12 walk items that name them are marked deferred in the S5 results note. The milestone's goal 3 core list (telegraph/impact/hurt; swing/hit/hurt/death) is met. Cost if wrong: the user hears a smaller slice than the aspirational walk list.
- **R16 — `s5-event clip=` is the manifest name**, e.g. `valus.impact`, not `bank/name`. The bank is in the manifest; Task 6 parses the names. Cost if wrong: Task 6 parsing churn.

## Task progress (continued)

- Task 5: complete (commits `d0f9ab4..83360e5`, review approved after one Important fix pair; 241 cases; S4.2 acceptance PASS; 18 `s5-event`s; mixdown 44.57 s, RMS 0.0514, peak 0.9022; `music_stop` at the reset t=20.483, not the death t=19.367).
- Task 5 minors (deferred): the silence gate accepts one audible window; the death cue is truncated by the reset one frame later; the "once per beat" test translates but does not exercise `attack_impact_due`; R14 headroom is empirical (HurtHeavy 1.25 × 0.99 × 0.85 = 1.052 can clamp on a full-scale sample); music looping is unproven (17.3 s of 125.9 s); the shared timeline never defeats Valus; `apply_valus_impact` writes into the step's events after `append_player_sfx`; unused include/`SoundCue::loop`.
- Task 6: review ❌ — 4 Important: (1) the mixdown gates cannot distinguish music+SFX from music-only (no peak lower bound), yet the note claims they do; (2) the 6.92 ms/frame perf is the silent `--entity-time 0` path, not audio-on; (3) the club telegraph-before-impact check only tests the first of two cycles; (4) the deferred-cue provenance says "absent from the map" while the map has a `deferred` array (eight rows; whoosh/footsteps in neither). Fix round 1 dispatched.
- Task 6: fix round 1/5 (4 addressed; commits `a0d1f4a..e61f7f2`); re-review approved: peak floor ≥0.5 + stomp window 15.1 dB above music-only, two club cycles ordered, audio-on perf claim removed, deferred provenance corrected. **Task 6 complete.**
- Final whole-branch review dispatched (`5c90f2d..e61f7f2`, 15 commits).
- Final whole-branch review: **With fixes** — no Critical; Important: (1) the defeat state calls `stop_music` every frame, flushing the device queue and mangling the killing blow; (2) the defeat path has no automated coverage; (3) the spec's scoped `Delete` is absent (stale manifest/WAVs persist); (4) the spec's audio-on 240-frame cost is unmeasured; (5) the Python gate and C# PS-ADPCM decoder can drift silently. Minors: the 0.27 music-ceiling arithmetic; `file=../` traversal; `M` on key repeat; AGENTS missing-cue wording; `audio.h` device comment; per-cue RMS/peak; the uncommitted SDD records; two doc phrasings; the numpy note. Deferred-minor triage: the defeat-coverage line and the music-ceiling line are fix-before-merge; the rest park.

## Rulings (final review)

- **R17 — The audio-on frame cost is waived.** The spec's 240-frame mixer-cost gate is not implemented; the measured pair is the unchanged S4.2 render path (`--entity-time 0`, no audio), and the results note says so. The user's fps walk is the check. Cost if wrong: the mixer's per-frame cost is unmeasured until the walk.
- **R18 — The S5 selftest adds a synthetic post-reset defeat phase.** It starts the music, sets `encounter.defeated`, and stops; the scripted weakpoint defeat is not reachable in the shared S4.2 timeline. The real defeat state is proven by the S4.1 acceptance (audio off) and the user walk; the S5 phase proves the stop wiring. Cost if wrong: the defeat stop is wiring-verified, not gameplay-proven.
- Final fix wave: re-review approved (Important 1–5 all addressed, no new breakage; commits `e61f7f2..b7d4e74`). **Final whole-branch review clean.** The workspace stays tracked (force-added per the design/S4.2 precedent); it is the durable hand-off record.
