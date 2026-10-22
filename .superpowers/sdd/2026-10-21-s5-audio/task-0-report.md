# Task 0 report — the decode gate (SXDF → PCM)

**Status:** **BLOCKED on the resident half, music proven** — see the *Fix* section at the end,
which supersedes the resident claims below.
**Commits:** `118f569 chore(re): prove the SXDF/AT9 decode` + `85d51a7 fix(re): make the decode
gate honest about unresolved variants`
**Files:** `tools/re/audio/s5_decode_gate.py`, `docs/notes/s5-audio-recon.md`
**Artifacts:** `out/s5/gate/` (385 MB, gitignored: one 5.1 Prologue WAV 131 MB + its float reference, two boss01 WAVs, `manifest.json`)

## Gate result

```
caffeinate -dimsu python3 tools/re/audio/s5_decode_gate.py \
    --root "$SOTC_GAME_ROOT" --out out/s5/gate --check
  music[00] Prologue     6ch 48000 Hz  10218240 B  227.072 s  RMS -21.3 dBFS
  music.sxd1: 33 WAVE records, partition 172367040 B, gaps 0
  resident 0x00FF0121 1ch 31120 B -> 1.135 s  RMS -19.6 dBFS
  resident 0x00FF0142 1ch 58304 B: skipped (codec unresolved)
  resident 0x00FF0221 2ch 225920 B -> 4.118 s  RMS -10.2 dBFS
  resident 0x00FF0242 2ch 116608 B: skipped (codec unresolved)
GATE PASSED: every carved WAV is 48 kHz, non-silent and full length      (exit 0)
```

**The whole catalog was also swept** (`--first N --tracks 1`, N = 0…32): **33/33 pass**,
63.84 min total, duration error 6.4–24.0 ms (encoder padding, always one/two extra
superframes), RMS −25.4…−11.6 dBFS, float peak 0.50–4.22. Numbers in
`docs/notes/s5-audio-recon.md` §6.

## What was proven (the things Tasks 2/3 consume)

> **Superseded notice:** the two bullets about the resident mapping and the `0x21` decode below
> were the pre-review reading. The Fix section (§2) disproves that mapping; treat the resident
> banks as unresolved and see `docs/notes/s5-audio-recon.md` §5.0/§7.

* **Header/chunks.** `0x60` header (magic/version/size/companion/GUID/name), chunks at
  `0x60` = `char[4]` + `u32 size` (bytes *after* the 8-byte header), walk lands exactly on
  EOF for `music.sxd1` (9872 B) and `boss01.sxd` (15 967 712 B).
* **Table rule.** `record_i = chunk + 0x10 + offsets[i] + 4*i`; verified by the leading tag
  word on 33/33 (`music`, tag 3) and 138/138 (`boss01`, tag 1) records.
* **`music.sxd1` WAVE = 76 B, absolute starts.** Field slots `+0x04` fmt, `+0x08` rate,
  `+0x14` samples (per channel), `+0x20` bytes, `+0x24` start. Record 0 starts at `0x60`;
  the 33 `(start, bytes)` pairs tile the 172 367 040-byte payload with **zero gaps** and end
  exactly at the file size. `start` is absolute in the `.sxd2`, *not* relative to `0x60`.
* **`boss01.sxd` WAVE = variable stride** (44/52/64/72 steps in `offsets[]` ⇒ 48/56/68/76-byte
  records), four format words: `0x0121`(64), `0x0221`(44), `0x0142`(15), `0x0242`(15).
  `0x0642` is *not* music-only: `com_obj.sxd` (4) and `edp01.sxd` (14) carry it too, and
  the full resident set also has `0x0104`, `0x0442`, `0x0842` — the plan's fifth boss01
  word came from other banks.
* **Resident payload mapping** (measured, fits 138/138): `heap = DATA + 0x20`,
  `record = heap + (start − min(start))`, `min(start) = 0xE404` on boss01.
* **AT9 config**, confirmed independently by the record itself (`WAVE+0x40` holds the word
  verbatim) and against the engine's own unpacker (`eboot.elf` vaddr
  `0x00984240`: top byte `0xFE`, `bextr 0xB05` = bits 5..15 = frame bytes−1,
  `bextr 0x311` = bits 17..19 = block config):
  `FE <sri:4><cfg:3><verify:1> <frame_bytes−1:11><superframe:2>`.
  Music = **`FE761DF0`**: sri 7 (48 kHz), block config **3 = 5.1/6 channels**,
  frame 240 B, 4 frames per 960-byte superframe (1024 samples). The same word is stored
  verbatim in the record at `WAVE+0x40` (all 33 records) and the gate asserts derived ==
  stored, so Tasks 2/3 can read the config instead of deriving it.

## Exact decode command

```bash
# wrapper: RIFF/WAVE, "fmt " 52 = FFFE | ch | rate | 0 | blockalign 960 | bits 0 |
# cbSize 34 | valid 0 | mask 0x3F | GUID d242e147-ba36-8d4d-88fc-61654f8c836c |
# extradata 12 = 00000000 FE 76 1D F0 00000000 ; then "data" <carved superframes>
ffmpeg -hide_banner -v error -y -i music_00_Prologue.at9.wav \
       -map 0:a -c:a pcm_f32le -f f32le music_00_Prologue.f32 \
       -c:a pcm_s16le music_00_Prologue.wav
```

## What failed (and how the wall was broken)

Decoding **stereo** (block config 2) failed inside frame 1 with `Invalid extension band 13`
/ `Invalid stereo band 15`. Everything plausible was ruled out: mono/dual-mono/quad/7.1,
1 frame per 960 B, 2-byte `0x3293` header stripping (per unit and per frame), 238/239/240
frame sizes, 952/958/960 block aligns, byte-swapped configs. A local build of the MIT
reference decoder (libatrac9) reproduced ffmpeg's parse exactly, so it was not an ffmpeg
bug. The answer was the channel count: the format word's byte 1 is the **channel count**
(`0x00FF0642` = 6), so the music is **5.1**; with block config 3 both decoders run 64
consecutive frames with zero errors and variable 239–243 B frames.

## Concerns / things the milestone must know

1. **Resident banks are not ATRAC9.** Their byte/sample ratio is exactly 16/28 → PS-ADPCM
   (16-byte blocks, 28 samples). The `0x21` family decodes; the `0x42` family is a second
   codec at 2 bits/sample (16 B per 64 samples) and is **unresolved** — the gate prints
   `codec unresolved` and skips it rather than faking a WAV. Any S5 SFX task that assumes
   "everything is AT9" needs revising.
2. **The resident `start` field's origin is unknown.** The back-to-back mapping above is
   measured, not derived; `min(start)` differs per bank.
3. **Music masters are hot**: every track overshoots unity in float (peak 0.50–4.22) and the
   s16 export clamps 0.0000–0.54 % of samples. Decode to float (or add ~1 dB headroom) in
   the converter if peaks matter.
4. **The gate's `--check` cannot fail on a silent-but-valid AT9 stream** other than through
   the RMS rule (it does: RMS ≤ −60 dBFS fails), but the end-to-end negative test exercised
   a zero payload that ffmpeg refuses outright (exit 2), plus synthetic-stat unit tests for
   silence, a 60 ms duration error, NaN and peak runaway — each rule fires on its own case.
5. **The brief's boss01 format-word list is off.** boss01 has four words, not five; `0x0642`
   also occurs in com_obj/edp01, and three more words (`0x0104`, `0x0442`, `0x0842`) exist in
   other resident banks — the resident codec question is wider than the gate's scope.
6. Default `--tracks 1` keeps the gate at 0.9 s and 385 MB; the 33-track sweep was run with
   `--first N --tracks 1` and deleted its artifacts as it went.

## Self-review

* Game tree: opened `"rb"` only; the negative test copied `music.sxd1` into `$TMPDIR`.
* `git status` clean after the commit; `out/s5/gate` sits under the gitignored `out` symlink.
* Every gate run wrapped in `caffeinate -dimsu`; no two runs concurrent.
* `python3 -m py_compile` clean; `--help`, `--verbose`, `--no-resident`, `--first`,
  `--tracks` exercised; missing `ffmpeg` → exit 2 with one line, not a traceback.

---

# Fix (post-review, 2026-10-21)

**Commit:** `85d51a7 fix(re): make the decode gate honest about unresolved variants`
**Result:** the resident half is **BLOCKED**, not "decoded with a caveat". The previous report's
"boss01 `0x21` + `0x221` decode" claim does not survive the correction below; the music half is
unchanged and still proven 33/33.

## 1. The gate no longer passes while skipping a required variant

New flags (both accept `0x0142` or the full `0x00FF0142`):

| flag | meaning |
|---|---|
| `--require <fmt,...>` | variants the gate must decode; **default = every format word found in the bank** (four on boss01) |
| `--allow-unresolved <fmt,...>` | explicitly accepted gaps; printed in the summary |

New outcomes:

* `--check` **exits 1** if a required variant was not decoded, or if a skipped variant is not
  named in `--allow-unresolved`;
* **exits 2** for a configuration error (a word in both `--require` and `--allow-unresolved`);
* **exits 3** when the allowlist covers *every* variant, i.e. there is no decodable family for
  Task 1 to attribute cues to — the printed line is `BLOCKED: … the milestone stops at Task 1`;
* on any passing run with gaps it prints
  `POLICY: the curated cues must come from the decoded families (…); if Task 1 attributes a core
  cue to <gap> the milestone stops at Task 1.`

Re-run (full output captured, `--check`):

```
$ python3 tools/re/audio/s5_decode_gate.py --root "$SOTC_GAME_ROOT" --out out/s5/gate --check
  music[00] Prologue     6ch 48000 Hz  10218240 B  227.072 s  RMS -21.3 dBFS
  music.sxd1: 33 WAVE records, partition 172367040 B, gaps 0
  resident 0x00FF0121 1ch 31120 B -> 1.135 s  RMS -19.6 dBFS
  resident 0x00FF0142 1ch 58304 B: skipped (codec unresolved; record config fe7007e0)
  resident 0x00FF0221 2ch 225920 B -> 4.118 s  RMS -10.2 dBFS
  resident 0x00FF0242 2ch 116608 B: skipped (codec unresolved; record config fe740fe0)
  boss01.sxd: 138 WAVE records, 4 distinct format words, heap 15881904 B
  FAIL required variant 0x00FF0121 was not decoded and is not in --allow-unresolved
  FAIL required variant 0x00FF0142 was not decoded and is not in --allow-unresolved
  FAIL required variant 0x00FF0221 was not decoded and is not in --allow-unresolved
  FAIL required variant 0x00FF0242 was not decoded and is not in --allow-unresolved
GATE FAILED: 4 problem(s)                                             (exit 1)

$ ... --check --allow-unresolved 0x0121,0x0142,0x0221,0x0242
  BLOCKED: no resident family is validated — the accepted gaps cover every variant, so
  there is no decodable family for Task 1 to attribute the curated cues to.
  POLICY: the milestone stops at Task 1 until a resident family decodes (note §7).   (exit 3)

$ ... --check --allow-unresolved 0x0142,0x0242
  FAIL required variant 0x00FF0121 … / 0x00FF0221 …                               (exit 1)
```

## 2. Why the resident half is now BLOCKED (the correction that matters)

The previous report claimed `heap + (start − min(start))` decoded the `0x21` family. That
mapping is **disproved**:

* `Σ bytes = 18 407 584` but the span of the `(start, bytes)` ranges is `15 874 512` (heap
  `15 881 904`);
* **all 137 consecutive pairs overlap**; total overlap `2 533 072` = `Σ bytes − span` exactly,
  zero gaps — the fields cannot describe a sequential heap;
* at the implied offsets the 16-byte block headers are *uniform* (filter nibble 0-7 at 14-24 %,
  shift 0-15 at 5-10 %), where real PS-ADPCM would only ever show legal shift/filter values;
* ffmpeg's strict `adpcm_psx` on the same bytes stops after ~63 % of the blocks with
  `Error submitting packet to decoder`.

The old `0x21` WAVs still get written, now explicitly as **exploratory artifacts** (status
`exploratory`, reason recorded in the manifest), and the variant counts as a gap. The note's
§3 and §7 were rewritten to say this; what `start` indexes (record headers, a de-duplicated
pool, stale metadata from the rebake) is the open question before Task 1 can attribute SFX.

## 3. One bounded attempt at `0x42` — identified, still a gap

New `--probe-0x42` mode makes the attempt reproducible (no WAVs, exits 0). Findings:

* the `0x42` records are 56 B and carry a **second codec word at `+0x2C`** that decodes with the
  same bit split as §4.1:
  `0xFE7007E0` = mono, 48 kHz, **64-byte frames**, 1 frame/superframe;
  `0xFE740FE0` = stereo, 48 kHz, **128-byte frames**. Both match the family's own geometry
  (58 304 B / 232 800 samples = 64 B per 256 samples), so `0x42` is **ATRAC9 at 96 kbps/ch**, not
  a 2-bit ADPCM.
* AT9 with that config decodes the **mono** record to 200 448 frames = 4.18 s of the declared
  4.85 s (RMS −16.5 dBFS, peak 0.60, one concealed `Invalid extension band 13!`); the stereo
  record rejects at frame 0.
* ADPCM matrix at 16 B / 64 samples (mono record, declared 4.85 s):
  `adpcm_psx` 0.66 s (rc 69), `adpcm_ms` 0 frames (`block_predictor[0] = 55`, rc 69),
  `adpcm_ima_wav` 0.14 s, `adpcm_ct` 2.43 s (exactly half: 2 samples/byte).
* `eboot.elf` has no 2-bit ADPCM variant: the ADPCM path is the streaming manager
  (`adpcmManager.cpp`, `B_adpcmOpenReq`, `Sg2ExStAdpcmOpen`), and there is no `2bit`/`4bit`
  string in the image.

**`0x42` is not resolved.** Its codec is identified (ATRAC9, per the record's own config word),
but the payload does not decode to the declared length at any derivable offset, so it stays an
accepted gap. The report's R7 wording ("a second codec, unresolved") should be updated to
"ATRAC9 at 96 kbps/ch, payload offset unresolved".

## 4. Exact PS-ADPCM invocation (for the note's §7 and for Task 2)

There is no ffmpeg decoder for this block layout (see above), so the gate decodes it in
`ps_adpcm_decode()`; the algorithm and the exploratory invocation are written out in
`docs/notes/s5-audio-recon.md` §7. Cross-check: wrapping the same bytes in a 48-byte `VAGp`
header and running `ffmpeg -f vag -i x.vag -c:a pcm_s16le` yields 34 580 frames (0.72 s)
before the strict decoder rejects a block — the same evidence from the other direction.

## 5. Note updates (docs/notes/s5-audio-recon.md)

* §5.0 (new): the `0x42` record config table, the AT9 partial decode, the full ADPCM attempt
  matrix, the binary check, the verdict.
* §5.1 (rewritten): the variant honesty rules, the four exit codes, the policy line, and the
  negative tests for each.
* §7 (rewritten): the record strides 48/56/68/76, the overlap proof that kills the mapping,
  the exact PS-ADPCM algorithm and invocation, and the three open questions.
* §8 (new): the converter policy from the measured facts — 5.1 → stereo downmix at conversion
  (`L/R + 0.707·C + 0.5·surround`), per-clip normalization to peak ≤ 0.99 with the gain
  recorded in the manifest (`gain_db`), the runtime mixer stays stereo (R6); implied gains
  −0.1 dB (Ending 1) to −12.5 dB (Boss 1); no resident clip until a family maps.
