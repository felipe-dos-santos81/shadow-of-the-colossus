# Task 1 report — the attribution gate (S5 audio)

**Status: DONE.** The resident payload formula is loader-derived and proven; the decode gate
passes with all four boss01 variants (music unchanged, 33/33 path intact); the seven core cues
are mapped and `s5_audition.py --check` passes.

**Commits:** `e009524 fix(re): derive the resident payload formula from the loader` +
`6fdd9f2 chore(re): attribute the S5 sound cues`

**Files:** `tools/re/audio/s5_decode_gate.py`, `tools/re/audio/s5_audition.py`,
`tools/re/audio/s5_map.json`, `docs/notes/s5-audio-recon.md`,
`docs/notes/s5-audio-attribution.md`, `.gitignore` (`__pycache__/`).

## 1. The loader (Ghidra)

Ghidra 12.2 imported `out/re/eboot.elf` as `x86:LE:64:gcc` (288 s analysis) into a throwaway
project under `$TMPDIR/opencode/s5ghidra`. The Ghidra MCP instance in the desktop app could not
be used: it had no project attached, and the plugin's `/open_project` opened the project in the
FrontEnd without attaching it to the CodeBrowser tool (`/project/info` stayed "No project
open"). The same analysis was driven headless with two dump scripts
(`out/re/ghidra/scripts/DumpS5.java`, `DumpFuncs.java`); all decompilation below is from the
analyzed `out/re/eboot.elf`. Raw dumps: `out/re/ghidra/s5-dump.txt`, `s5-funcs.txt`,
`s5-lookup.txt`, `s5-ce.txt`, `s5-vm.txt`, `s5-callers.txt`.

| vaddr | function | what it proves |
|---|---|---|
| `0x00987990` | wave table lookup | `record = WAVE_base + i*4 + *(int*)(WAVE_base + i*4)`; `WAVE_base = chunk+0x10`, count at `chunk+0x0C` — the `+4*i` table rule. |
| `0x00983220` | wave field copy | for a non-streaming wave, `payload = record + start + 0x24`; also reads fmt/rate/samples/bytes from `+0x04/+0x08/+0x14/+0x20`. |
| `0x00985900` | bank registration | walks the `SXDF` chunk chain at `0x60`; stores `WAVE` base `= chunk+0x10`, `REQD` base `= chunk+0x10`, `TONE` pointers at `chunk+0x10`/`+0x20`, `DATA` at its chunk offset. |
| `0x00983ea0` | format dispatch | `switch(low byte of fmt)`: `0x20/0x21` → `0x594 = channels<<4`, `0x590 = 0x1C` (28 samples/block); `0x42` → AT9 config validation (`cmp 0xFE000000`, `bextr 0xB05` frame bytes, `bextr 0x311` block config). |
| `0x00987a80` | name lookup | `hash = c + u*0x83` over the name, binary search of the `NAME` table (12-byte entries `{str_off, hash, id}`), returns the `REQD` entry for `id`. |
| `0x00972790` | sequence VM | opcode `0xa4` = `SFX_PLAY_TONE`, `0xa2` = `SFX_PLAY_WAVEFORM`, `0xa9` = `SFX_SET_PARAM`; operands are nibble-decimal encoded by `FUN_00970b10`. |
| `0x00980620` | voice setup | calls `FUN_00987990` with the index from the request struct (`+0x30`), then `FUN_00983ea0`. |

### The derived struct (resident `WAVE`)

```
+0x00 u32 tag = 1
+0x04 u32 fmt        (byte1 = channels; low byte = codec: 0x21 PS-ADPCM, 0x42 ATRAC9)
+0x08 u32 rate       (0xBB80 = 48000)
+0x0C u32 codec word
+0x10 u32 0
+0x14 u32 samples    (per channel)
+0x18 u32 loop start (-1 = none; com_obj has real loops)
+0x1C u32 loop end
+0x20 u32 bytes      (payload length)
+0x24 u32 start      (offset relative to the record)
...    variable tail: 0x42 records carry the AT9 config (56 B: +0x2C; 76 B: +0x40)
```

### The formula

```
payload = wave_record_offset + 0x24 + start
length  = bytes
```

Why the raw `start` values overlap: `start` is not a heap offset. Two records can address the
same payload, and the record addresses differ by the stride. On boss01 the 138 ranges tile
`[DATA+0x10, DATA+8+size)` with zero gaps (payload 15 881 920 B); `com_obj` (241 records) and
`com_v` (63) tile exactly too. The old `heap + (start − min(start))` reading was off by the
per-record term, which is why the mono 0x42 record decoded 4.18 s of 4.85 s: it was reading
5 504 bytes before the true payload.

## 2. The end-to-end proof

All four boss01 variants decode at the loader payload:

```
music[00] Prologue     6ch 48000 Hz  10218240 B  227.072 s  RMS -21.3 dBFS
music.sxd1: 33 WAVE records, partition 172367040 B, gaps 0
resident 0x00FF0121 1ch 31120 B -> 1.135 s  RMS -12.4 dBFS
resident 0x00FF0142 1ch 58304 B -> 4.859 s  RMS -17.1 dBFS (AT9 64B/frame)
resident 0x00FF0221 2ch 225920 B -> 4.118 s  RMS -9.5 dBFS
resident 0x00FF0242 2ch 116608 B -> 4.859 s  RMS -17.1 dBFS (AT9 128B/frame)
boss01.sxd: 138 WAVE records, 4 distinct format words, payload 15881920 B
            (mapping: wave_record_offset + 0x24 + start)
GATE PASSED: music 48 kHz non-silent; resident variants decoded
             0x00FF0121, 0x00FF0142, 0x00FF0221, 0x00FF0242          (exit 0)
```

`--require 0x0142` and `--require 0x0121,0x0142,0x0221,0x0242` both exit 0; the config-overlap
negative (`--require 0x0121 --allow-unresolved 0x0121`) exits 2. The 0x42 pair (108 stereo /
109 mono) both decode 233 216 frames = 4.8587 s of 4.85 s, RMS −17.1 dBFS, no decoder errors.
The 0x21 decoder is 16 B/28-sample PS-ADPCM; filters 0-4 are the standard VAG table, filters
5-7 are fitted from block-boundary continuity (126,−62), (128,−64), (126,−63) — the engine's
real table is in the NGS2 firmware, not in `eboot.elf`. With those, boundary jumps drop from
2.5-4.8× to 1.0-2.8× on records not used in the fit.

## 3. The map

`tools/re/audio/s5_map.json` (7 cues, 8 deferred):

| event | bank | name | wave | method | confidence |
|---|---|---|---|---|---|
| `valus.telegraph` | boss01 | `boss01_vo_atk1` | 3 | table | medium |
| `valus.impact` | boss01 | `boss01_atk_hit_ground1` | 26 | ghidra | high |
| `valus.hurt` | boss01 | `boss01_vo_dmg1` | 6 | table | medium |
| `player.swing` | com_obj | `wnd_LifeSwd_swish_t1_01` | 7 | table | high |
| `player.hit` | com_obj | `wnd_swd_hit_gravel_t1_01` | 21 | table | medium |
| `player.hurt` | com_v | `bvo_dmg_m` | 40 | table | medium |
| `player.death` | com_b_h | `b_dead_by_boss` | 657 | table | medium |

`valus.impact` is binary-derived: `NAME` hash of `boss01_atk_hit_ground1` → `REQD` 303 →
`SEQD` timeline `+0x13E4` → bytecode `+0x23BA` → `PLAY_TONE 43` → `TONE[43].wave = 26`
(the cue is a 5-layer sequence, tones 43-47 → waves 26-30). The other boss01 cues and the
player cues use the name-pool/WAVE order (the WAVE table order matches the name insertion
order for the resident subset; durations match the semantic class) plus spectrogram audition;
the map records the method and confidence honestly. `s5_audition.py --check`:

```
valus.telegraph  boss01  w003 boss01_vo_atk1            1.202 s  RMS -15.6 dBFS
valus.impact     boss01  w026 boss01_atk_hit_ground1    7.543 s  RMS -13.3 dBFS
valus.hurt       boss01  w006 boss01_vo_dmg1            1.051 s  RMS -12.4 dBFS
player.swing     com_obj w007 wnd_LifeSwd_swish_t1_01   0.465 s  RMS -10.2 dBFS
player.hit       com_obj w021 wnd_swd_hit_gravel_t1_01  0.709 s  RMS -12.2 dBFS
player.hurt      com_v   w040 bvo_dmg_m                 0.270 s  RMS  -8.6 dBFS
player.death     com_b_h w657 b_dead_by_boss            0.812 s  RMS -15.4 dBFS
AUDITION PASSED: 7 mapped wave(s) decoded, 48 kHz, non-silent      (exit 0)
```

All 138 boss01 waves were also decoded to `out/s5/audition/boss01/` (`--all`) for the ear
path; the seven core WAVs and their spectrograms are under `out/s5/audition/`.

## 4. Constraints and self-review

* Music unchanged: the gate's Prologue decode is identical (227.072 s, RMS −21.3 dBFS) and
  the 33-track sweep numbers in the note are untouched.
* Gate honesty: `--check` fails if a required variant is missing or an unlisted gap is
  skipped; the resident carve now asserts the tiling instead of assuming an offset. No
  exploratory WAV counts as a pass.
* Game tree: read-only (`"rb"`); all artifacts under `out/` (gitignored); no assets committed.
* Frozen scripts `scripts/s3|s4|s41-acceptance.sh` untouched (`git status` clean after the
  two commits).
* `caffeinate -dimsu` wrapped every gate/audition run; no two acceptances concurrently.
* `python3 -m py_compile` clean on both scripts.

## 5. Concerns

1. **0x21 filter 5-7 are fitted, not firmware-exact.** Durations, RMS and envelopes are
   right; a high-fidelity converter may want to dump the NGS2 table from firmware or solve
   the coefficients on a larger corpus. The note records the fitted values.
2. **`valus.telegraph` / `valus.hurt` / the player cues use the name-pool order** (medium
   confidence) rather than a decoded pointer. The binary pointer exists in `REQD → SEQD`,
   but the com_obj/com_v/com_b_h `TONE` tables are not the flat 12-byte form used by boss01
   and the sequence tree needs more follow levels; `valus.impact` is the one high-confidence
   binary chain. If Task 3 wants higher confidence, the sequence tree walk is the next step.
3. **`player.death` lives in `com_b_h`, not `com_v`.** The `bvo_die_*` names in com_v are
   sequence names beyond the 63 resident waves; `com_b_h`'s `b_dead_by_boss` (name 657 of
   1 161 resident waves) is the resident single-wave death cue.
4. **`edp01` does not tile** (11 segments, 70 bytes of gaps); the gate does not carve it.
   Other banks may need the same check before Task 2 relies on the formula there.
5. **Ghidra MCP was unusable in this environment** (no project attached; `/open_project`
   does not attach the project to the tool). All decompilation used Ghidra 12.2 headless on
   the same analyzed ELF; the addresses/functions above are reproducible from the committed
   `out/re/ghidra/*.txt` dumps (gitignored).

---

# Fix (post-review, 2026-10-21)

**Commit:** `256fe69 fix(re): verify the S5 cue identity through the loader chain`
**Re-run:** gate passes all four boss01 variants; audition passes 7/7 core rows.

## Critical 1 — the map no longer equates pool order with wave identity

`s5_map.json` is now **v2**. `name` is the sound-request name (`NAME`/`REQD`);
`wave` is the verified WAVE index; `reqd` is the `REQD` index from the NAME hash;
`tone` is the `PLAY_TONE` operand where the chain is walked; `layers` lists the
other layers of a layered sound; `wave_pool_label` is the pool string at that
WAVE index (a label, not an identity). The script re-walks the NAME hash and
asserts `TONE[tone].wave == wave` on `ghidra` rows.

| event | bank | request name | reqd | tone | wave | layers | method | confidence |
|---|---|---|---|---|---|---|---|---|
| `valus.telegraph` | boss01 | `boss01_vo_atk1` | 340 | 20 | 3 | — | ghidra | medium |
| `valus.impact` | boss01 | `boss01_atk_hit_ground1` | 303 | 43 | 26 | 26-30 | ghidra | high |
| `valus.hurt` | boss01 | `boss01_vo_dmg1` | 331 | 23 | 6 | — | ghidra | medium |
| `player.swing` | com_obj | `wnd_LifeSwd_swish_t1_01` | 118 | — | 7 | — | audition | medium |
| `player.hit` | com_obj | `wnd_swd_hit_gravel_t1_01` | 60 | — | 21 | — | audition | medium |
| `player.hurt` | com_v | `bvo_dmg_m` | 172 | — | 40 | — | audition | medium |
| `player.death` | com_b_h | `b_dead_by_boss` | 1573 | — | 657 | — | audition | medium |

The boss01 rows now walk the chain:
`NAME hash → REQD id → SEQD offset → request-order tone → TONE[tone].wave`.
`valus.impact` is the direct bytecode chain (`0x13E4 → 0x23BA → a4 01 43 →
TONE[43].wave=26`, layers 26-30). For `valus.telegraph`/`valus.hurt` the
tone→wave step is binary (`TONE[18..24] → waves 1..7`) and the name→tone step is
the request/play-list order, so those stay `medium`. The other three banks have
a `TONE` chunk that is **not** the flat 12-byte form; their rows are explicitly
re-scoped to `audition` with `verified_index: true`, and the script prints
"identity not chain-proven". The note's false "the WAVE table order matches the
name-pool insertion order" sentence is replaced by this policy.

Proof that the pool is not a per-wave name list: `valus.impact` wave 26's pool
label is `boss01_FS_far3_0`, while the request name is
`boss01_atk_hit_ground1`.

## Important 2/3 — the audition is now a real gate

`s5_audition.py --check` reads both `cues` and `deferred`, and fails when:
any of the seven core events has no cue row with a valid wave; a core event is
also in `deferred`; a `ghidra` row's `reqd` mismatches the NAME hash or
`TONE[tone].wave != wave`; an `audition` row lacks `verified_index`; two rows
share a name; a mapped wave is missing/silent/not 48 kHz or the duration error
is > 50 ms. It prints the actual rate and asserts `rate == 48000`.

Negative tests (all exit 1):

```
drop player.death        -> player.death: core cue has no row in cues
defer player.death       -> player.death: core cue has no valid wave
wrong tone (44)          -> valus.impact: TONE[44].wave=27 != mapped wave 26
unverified audition      -> player.swing: audition row must set verified_index
duplicate name           -> duplicate cue name ...; player.hit: reqd 60 != NAME lookup 118
```

## Important 4 — 0x21 fidelity is stated, not hidden

All seven cues are 0x21; filters 5-7 are fitted. Per-cue block counts,
filter-5-7 fractions and boundary ratios are now in
`s5-audio-recon.md` §7.2 and `s5-audio-attribution.md`:

```
telegraph w003  2060 blocks 10.5% 2.73
impact    w026 25862 blocks 11.1% 0.98
hurt      w006  1802 blocks 21.0% 2.86
swing     w007   797 blocks 29.9% 2.66
hit       w021  1215 blocks 39.3% 1.64
hurt      w040   463 blocks 23.3% 1.26
death     w657  1392 blocks 27.8% 2.52
```

The metric is neutral/worse for w006/w007/w657, so the old "1.0-2.8×" claim was
selective. A targeted search of `eboot.elf` found no PS-ADPCM coefficient table
(exact/approximate int16, int32×64, float: 0 hits); the true filters live in the
NGS2 firmware. The gate proves existence, rate, length and non-silence, **not
fidelity**; all 0x21 rows are `medium` confidence and the ear remains the final
check.

## Cheap review fixes

* `--no-resident --check` now exits 2 unless `--allow-unresolved` names every
  variant; with the full list it prints
  `GATE PASSED: music 48 kHz non-silent; resident skipped (all variants allow-listed)`.
* The manifest records skipped variants as `"ok": False` with the reason.
* Duplicate `(payload,length)` counts corrected: **9 groups / 18 records**
  (boss01), 24/60 (com_obj), 7/14 (com_v), 89/302 (com_b_h).
* The AT9 config wording now says LE-unpack (`0xFE761DF0`, bytes
  `f0 1d 76 fe`) / BE-emit into the extradata.
* `player.grab` deferred row aligned to `com_b_h`.
* `parse_fmt_list` normalizes `0xFF0642` to `0x00FF0642` (tested with
  `--require 0xFF0121,0xFF0142,0xFF0221,0xFF0242`).

## Re-run output

```
$ caffeinate -dimsu python3 tools/re/audio/s5_decode_gate.py \
    --root "$SOTC_GAME_ROOT" --out out/s5/gate --check
  music[00] Prologue     6ch 48000 Hz  10218240 B  227.072 s  RMS -21.3 dBFS
  music.sxd1: 33 WAVE records, partition 172367040 B, gaps 0
  resident 0x00FF0121 1ch 31120 B -> 1.135 s  RMS -12.4 dBFS
  resident 0x00FF0142 1ch 58304 B -> 4.859 s  RMS -17.1 dBFS (AT9 64B/frame)
  resident 0x00FF0221 2ch 225920 B -> 4.118 s  RMS -9.5 dBFS
  resident 0x00FF0242 2ch 116608 B -> 4.859 s  RMS -17.1 dBFS (AT9 128B/frame)
  boss01.sxd: 138 WAVE records, 4 distinct format words, payload 15881920 B
              (mapping: wave_record_offset + 0x24 + start)
GATE PASSED: music 48 kHz non-silent; resident variants decoded
             0x00FF0121, 0x00FF0142, 0x00FF0221, 0x00FF0242

$ caffeinate -dimsu python3 tools/re/audio/s5_audition.py \
    --map tools/re/audio/s5_map.json --check
  valus.telegraph  chain: reqd 340 -> tone 20 -> wave 3
  valus.impact     chain: reqd 303 -> tone 43 -> wave 26
  valus.hurt       chain: reqd 331 -> tone 23 -> wave 6
  player.swing     audition: wave 7 (identity not chain-proven)
  player.hit       audition: wave 21 (identity not chain-proven)
  player.hurt      audition: wave 40 (identity not chain-proven)
  player.death     audition: wave 657 (identity not chain-proven)
AUDITION PASSED: 7 mapped wave(s), all 48000 Hz, non-silent
```

## Residual concerns

1. **Boss01 name→tone is request-order**, not a direct pointer; the tone→wave
   step is binary. Confidence `medium` for telegraph/hurt, `high` for impact.
2. **Three player rows are audition-scoped**, not chain-proven; the script
   marks them explicitly and the ear is the final identity check.
3. **0x21 filters 5-7 remain fitted** (no table in eboot.elf; NGS2 firmware).
4. `edp01` still does not tile (11 segments, 70 bytes of gaps); not carved.

---

# Fix 2 (scoped re-review, 2026-10-21)

**Commit:** `a0cfaa7 fix(re): make the S5 chain check real`
**Result:** only `valus.impact` remains `ghidra`; the other six rows are explicitly
`audition` with `verified_index: true`. The gate and audition both pass.

## Critical 1 — no order inference is labelled `ghidra`

The `SEQD` walk for `valus.telegraph` (REQD 340) and `valus.hurt` (REQD 331) was attempted
with the same timeline/sub-table walk that proves `valus.impact`:

* `REQD 340 -> SEQD 0x10B1` follows a parent timeline (20 B, `u16 +0x04 == 0x4000`,
  next `u32 +0x0C`) to a sub-table entry (24 B, `u16 +0x0C == 0x4000`, sound id 340 at
  `+0x10`, bytecode `+0x14` = `0x6ABC`).
* The bytecode at `0x6ABC` is a **parameter envelope** (`a9 SFX_SET_PARAM`) whose
  `PLAY_TONE` operands are **complex/random** (`a4 02 03 13 …`), not a fixed `a4 01 XX`.
  There is no fixed tone to walk; the same holds for `vo_dmg1` (`REQD 331 -> 0x11B6`).
* `valus.impact` is the only row with a decoded fixed operand: `0x13E4 -> 0x23BA ->
  a4 01 43 -> TONE[43].wave = 26`.

Both voice rows are therefore re-scoped to `method: "audition"`, `confidence: "medium"`,
`verified_index: true`, with `tone: null` and no chain claim. The script prints
`audition: wave 3 (identity not chain-proven)` / `wave 6`, not `chain: …`.

## Important 2 — deferred core rows with a wave now fail

The structure pass and the decode pass both check core events:

```
player.death: core cue is status=deferred
player.death: core cue is deferred
AUDITION FAILED: 2 problem(s)      (exit 1)
```

The map no longer treats `status=deferred` + an integer `wave` as acceptable.

## Important 3 — the `--map` check now parses the bytecode

`walk_play_tone()` re-walks `REQD -> SEQD timeline/sub-table -> PLAY_TONE` and returns the
first simple `a4` tone. For `ghidra` rows the script requires `reqd` and `tone`, requires
the walked tone to equal `tone`, and requires `TONE[tone].wave == wave`. Negative tests:

```
tone 44 instead of 43  -> walked PLAY_TONE 43 != mapped tone 44
                           TONE[44].wave=27 != mapped wave 26       (exit 1)
missing reqd           -> ghidra row is missing reqd               (exit 1)
```

An `audition` row without `verified_index` fails; an audition row never prints `chain`.

## Layers — full set, validated, mixed

`layers` is defined as the **full set of waves in the sound, including the primary**
(`layers[0] == wave`). `valus.impact` carries `layers: [26,27,28,29,30]`; `--check` decodes
all five, fails on a missing/silent/short layer, and writes an equal-gain mix
(`1/N` per layer, padded to the longest) to
`out/s5/audition/boss01/valus.impact_mix.wav`. Negative tests: a `layers` list with a
missing index fails (`wave index 999 out of range`); `layers[0] != wave` and duplicate
layers both fail.

**Converter decision:** mix all layers with equal gains for the cue; Task 3 implements
that. The per-layer WAVs are kept for a later per-layer tuning pass. The note and the
recon §8 state this.

## Confidence — identity vs fidelity

The map now has both fields:

* `confidence` = **identity** confidence: `valus.impact` `high` (walked chain), the six
  audition rows `medium`.
* `fidelity` = `medium` for **every** 0x21 row (fitted filters 5-7, no firmware table).

The attribution note and recon §7.2 agree; the old "0x21 map confidence is medium" wording
is replaced.

## Cheap minors

* `--no-resident --check` with a full allowlist but no `--allow-empty-resident` now prints
  `BLOCKED: … the resident half is the milestone's decode proof` and exits **3**; with the
  explicit flag it exits 0. Without the allowlist it still exits 2.
* `AUDITION PASSED` prints the actual rate set (`1 rate(s) (48000 Hz)`) and `check_stats`
  asserts `rate == 48000`; the message is no longer a bare hardcoded claim.
* `s5_audition.py` layers/mix code is pure Python; no new dependency.

## Re-run output

```
$ caffeinate -dimsu python3 tools/re/audio/s5_decode_gate.py \
    --root "$SOTC_GAME_ROOT" --out out/s5/gate --check
  music[00] Prologue     6ch 48000 Hz  10218240 B  227.072 s  RMS -21.3 dBFS
  music.sxd1: 33 WAVE records, partition 172367040 B, gaps 0
  resident 0x00FF0121 1ch 31120 B -> 1.135 s  RMS -12.4 dBFS
  resident 0x00FF0142 1ch 58304 B -> 4.859 s  RMS -17.1 dBFS (AT9 64B/frame)
  resident 0x00FF0221 2ch 225920 B -> 4.118 s  RMS -9.5 dBFS
  resident 0x00FF0242 2ch 116608 B -> 4.859 s  RMS -17.1 dBFS (AT9 128B/frame)
  boss01.sxd: 138 WAVE records, 4 distinct format words, payload 15881920 B
              (mapping: wave_record_offset + 0x24 + start)
GATE PASSED: music 48 kHz non-silent; resident variants decoded
             0x00FF0121, 0x00FF0142, 0x00FF0221, 0x00FF0242

$ caffeinate -dimsu python3 tools/re/audio/s5_audition.py \
    --map tools/re/audio/s5_map.json --check
  valus.telegraph  audition: wave 3 (identity not chain-proven)
  valus.impact     chain: reqd 303 -> PLAY_TONE 43 -> wave 26
  valus.impact     boss01 w026 w027 w028 w029 w030  (5 layers, all non-silent)
  valus.impact     mixed 5 layers -> boss01/valus.impact_mix.wav
  valus.hurt       audition: wave 6 (identity not chain-proven)
  player.swing     audition: wave 7 (identity not chain-proven)
  player.hit       audition: wave 21 (identity not chain-proven)
  player.hurt      audition: wave 40 (identity not chain-proven)
  player.death     audition: wave 657 (identity not chain-proven)
AUDITION PASSED: 7 cue(s), 1 rate(s) (48000 Hz), all layers non-silent
```

## Residual concerns

1. **Only `valus.impact` is chain-verified.** The boss01 voice bytecode is a parameter
   envelope with complex/random PLAY_TONE operands, so `valus.telegraph`/`valus.hurt`
   remain audition picks (medium identity, medium fidelity).
2. **All 0x21 cues are fidelity-medium** because filters 5-7 are fitted; the gate proves
   existence/length/rate, not fidelity.
3. **The impact mix is equal-gain only**; per-layer tuning is a later converter decision.
4. `edp01` still does not tile (11 segments, 70 bytes of gaps).
