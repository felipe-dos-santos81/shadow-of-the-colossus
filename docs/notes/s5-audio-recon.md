# S5 audio — the SXDF/AT9 decode

Status: **music proven; resident banks blocked** (2026-10-21). The music path decodes:
**33/33 tracks**, 48 kHz, 5.1, 63.84 min, every one non-silent and within 24 ms of its
declared duration. The **resident** `boss*.sxd` payload is a different problem: `0x21` is
16 B/28-sample PS-ADPCM *geometry* but its heap offsets are disproved (§7), and `0x42` is
ATRAC9 by its own config word but does not decode to spec (§5.0). The gate therefore
**fails by default** and only a `--allow-unresolved` covering every resident variant can
pass it — which prints `BLOCKED` and stops the milestone at Task 1 (§5.1).

Tool: `tools/re/audio/s5_decode_gate.py` (read-only on the game tree; artifacts land in
`out/s5/gate/`, gitignored). Everything below was measured by that script or by the probes
recorded in §7.

```bash
export SOTC_GAME_ROOT=…/SOTC-extract/extracted/game
caffeinate -dimsu python3 tools/re/audio/s5_decode_gate.py \
    --root "$SOTC_GAME_ROOT" --out out/s5/gate --check
# -> GATE FAILED: 4 problem(s)    (all four resident variants are unresolved, §7)
```

---

## 1. Container header and chunk chain (verified)

`SXDF` = a bank/index with tables, `SXDS` = a stream payload. Same 0x60-byte header on both:

| Offset | Type | Meaning |
|---|---|---|
| `0x00` | `char[4]` | `SXDF` or `SXDS` |
| `0x04` | `u16` | version = 4 on all 41 containers |
| `0x06` | `u16` | per-file, unverified (always 0 on `SXDS`) |
| `0x08` | `u32` | `SXDF`: own file size · `SXDS`: stream count |
| `0x0C` | `u32` | `SXDF`: companion `.sxd2` **file** size, else 0 · `SXDS`: payload size |
| `0x10` | `u8[16]` | bank GUID, identical inside the `.sxd1`/`.sxd2` pair |
| `0x20` | `char[32]` | bank name, NUL padded |
| `0x40` | `u8[32]` | zero |
| `0x60` | — | first chunk |

Chunk = `char[4] magic` + `u32 size`, where **`size` counts the bytes after the 8-byte
header**; next chunk = `off + size + 8`. On both files used here the walk lands exactly on
the file end (9872 B and 15 967 712 B, no slack):

```
music.sxd1  TONE 0x60  REQD 0x3d0  NAME 0x9b0  WAVE 0xc40  SEQD 0x16a0
            LVAR 0x1ed0  LVRN 0x20e0  RXFD 0x2270  RXFN 0x2570      (= 9872 B)
boss01.sxd  TONE 0x60  REQD 0x1910  NAME 0x41a0  WAVE 0x68c0  SEQD 0x8820
            LVAR 0x12c20  LVRN 0x12c90  RXFD 0x12ce0  RXFN 0x14bb0  DATA 0x14f10
```

Table chunks (`REQD`, `NAME`, `WAVE`, `SEQD`) all share one shape:

```
+0x04 u32 size   +0x08 u32 0   +0x0C u32 count   +0x10 u32 offsets[count]
record_i = chunk + 0x10 + offsets[i] + 4*i
```

The `+ 4*i` is not a quirk to work around, it is the format: `offsets[]` advance by
`stride − 4`, so a variable-stride table (see §3) still addresses every record correctly.
Verified by asserting the leading tag word of every record: 33/33 on `music.sxd1` (tag 3),
138/138 on `boss01.sxd` (tag 1).

---

## 2. `music.sxd1` — 76-byte `WAVE` records, 33 tracks

Stride is a constant 76 B (19 `u32`); tag `+0x00` = 3 on all 33.

| Offset | Field | Prologue | Meaning |
|---|---|---|---|
| `+0x00` | tag | `3` | waveform kind |
| `+0x04` | format word | `0x00FF0642` | **byte 1 = channel count = 6** (5.1); low byte = codec/quality id |
| `+0x08` | rate | `48000` | sample rate (`0xBB80` on every record of every bank) |
| `+0x0C` | codec word | `0x0F3B` | per-bank, unresolved — **not** used for the decode |
| `+0x10` | 0 | `0` | — |
| `+0x14` | samples | `10 898 874` | **per-channel sample count** |
| `+0x18`,`+0x1C` | −1 | `0xFFFFFFFF` | — |
| `+0x20` | bytes | `10 218 240` | **payload length in `music.sxd2`** |
| `+0x24` | start | `96` | **absolute byte offset in `music.sxd2`** |
| `+0x28` | `0x00120401` | | codec word, constant across the 33 music records |
| `+0x2C`…`+0x3C` | | | constant per bank |
| `+0x40` | | `0xFE761DF0` | **the AT9 config word itself**, identical on all 33 (= the derived `FE761DF0`) |
| `+0x44`,`+0x48` | | `6`,`100` | constant |

**Partition check (the demux is exact).** Sorted `(start, bytes)`:

* record 0 starts at `0x60`, i.e. at the first byte after the 0x60-byte `SXDS` header;
* every consecutive pair is gap-free (`start[i+1] == start[i] + bytes[i]`), 32/32;
* the last record ends at `172 367 136` = the file size;

so `start` is absolute, **not** relative to the payload base. `Σbytes = 172 367 040 =
172 367 136 − 0x60`, and `music.sxd1 +0x0C` equals `music.sxd2`'s file size exactly. The
script refuses to carve if any of those three invariants fails.

Geometry: 360 kbps CBR, 960-byte superframes of 1024 samples, i.e. **4 AT9 frames of 240
bytes / 256 samples** per superframe. Encoder padding shows up as one extra superframe on
7 of the 33 tracks (`ceil(samples/1024)` vs `bytes/960`), which the script tolerates
(`at9_geometry`, +0..+2 superframes).

---

## 3. Resident banks — `boss01.sxd` `WAVE` records

Same field slots, but **variable stride** and a different payload codec:

* 138 records, tag `1` on all of them;
* `offsets[]` advance by 44 (94×), 52 (13×), 64 (14×) and 72 (16×), so the real records are
  48/56/68/76 bytes and the `+4*i` rule absorbs the difference — the script never assumes a
  stride, only reads the fixed `0x28`-byte prefix (`+0x04` fmt, `+0x08` rate, `+0x14`
  samples, `+0x20` bytes, `+0x24` start);
* four distinct format words in the bank: `0x00FF0121` (64 recs, 1 ch),
  `0x00FF0221` (44, 2 ch), `0x00FF0142` (15, 1 ch), `0x00FF0242` (15, 2 ch).

Sweeping the `WAVE` table of all 25 resident banks gives the full format-word set:
`0x0104` (16 recs, edp01), `0x0121` (2 511), `0x0142` (606), `0x0221` (795), `0x0242`
(302), `0x0442` (3, boss02/boss04), `0x0642` (18: com_obj 4, edp01 14), `0x0842` (2,
boss12/edp01). So `0x0642` is **not** music-only — some resident SFX are 6-channel too —
and the plan's "curated set" for boss01 listed a fifth word that belongs to other banks.

**Payload mapping: disproved.** The `DATA` chunk is `8 + 24` bytes of header then a heap of
`size − 24 = 15 881 904` bytes. The obvious reading —

```
heap_base   = DATA_chunk_offset + 0x20
record_data = heap_base + (start − min(start))        # min(start) = 0xE404 on boss01
```

— is what the gate used, and it is **wrong**: `Σ bytes` is 18 407 584 over a 15 874 512-byte
span and all 137 consecutive `(start, bytes)` pairs *overlap* (total overlap = exactly
`Σ bytes − span`), so the fields cannot describe a sequential heap. §7 has the full evidence
and what is still open.

---

## 4. Codec — ATRAC9, 5.1, and the exact decode

### 4.1 The config word

The AT9 config is 4 bytes, big-endian:

```
FE  <sri:4><blockconfig:3><verify:1>  <frame_bytes−1:11><superframe:2>
```

For the music: `sri = 7` (48 kHz), `blockconfig = 3` (**5.1**), `frame_bytes = 240`,
`superframe = 2` (4 frames/superframe) → **`FE761DF0`**.

The record also carries that word verbatim at `+0x40` (`0xFE761DF0` on all 33 music
records), and the script asserts that the derived word equals it — so the derivation and
the shipped bytes check each other. Tasks 2/3 can simply read `WAVE+0x40`.

That split is not a guess: the engine's own validator at `eboot.elf` vaddr `0x00984240`
reads a `u32`, compares its top byte with `0xFE000000`, extracts **bits 5..15** with
`bextr …,0xB05` and adds 1 (frame bytes), and **bits 17..19** with `bextr …,0x311` (block
config), then walks the channel table. ffmpeg's `atrac9` decoder wants the same 4 bytes as
12-byte extradata (`LE32 version 0` + config + 4 zero bytes) inside a
`WAVEFORMATEXTENSIBLE` `fmt ` chunk whose GUID is the AT9 one.

### 4.2 Channel count — the thing that blocked the first attempts

`0x00FF0642` has `0x06` in byte 1 = **6 channels**, and the music really is 5.1. Decoded as
stereo the bitstream parses a few hundred bits and then fails (`Invalid stereo band`,
`Invalid extension band`) because the 5.1 block layout (CPE, SCE, LFE, CPE) is not the
stereo layout (one CPE). With block config 3 the reference decoder (libatrac9) and ffmpeg
both decode **64 consecutive frames with zero errors** and frame sizes of 239–243 bytes
(variable inside the 960-byte superframe, as `avg_frame_size` implies).

Per-channel RMS of the Prologue: `-18.8, -19.2, -25.8, -25.9, -21.4, -21.9` dBFS — six live
channels in the 5.1 layout, not duplicated mono.

### 4.3 Exact commands

The script builds the wrapper itself; reproduced here so Tasks 2/3 do not have to re-derive it:

```
RIFF <n> WAVE
  fmt  52: FF FE | channels | rate | 0 | block_align=960 | bits=0
          cbSize=34 | valid=0 | mask | GUID d242e147-ba36-8d4d-88fc-61654f8c836c
          extradata(12): 00 00 00 00 | FE 76 1D F0 | 00 00 00 00
  data <bytes>: the carved AT9 superframes
```

```bash
# one record (Prologue, 0x60 .. 0x9BEB60 of music.sxd2) -> 5.1 float + s16
ffmpeg -hide_banner -v error -y -i music_00_Prologue.at9.wav \
       -map 0:a -c:a pcm_f32le -f f32le music_00_Prologue.f32 \
       -c:a pcm_s16le music_00_Prologue.wav
```

Nothing else is needed: no `-ss`, no per-frame headers, no byte swapping.

---

## 5. What failed, and what was ruled out

| Try | Result |
|---|---|
| RIFF + `FE741DF0` (stereo, block config **2** = `0x74`, 240 B/frame, 4 frames/superframe) | `Invalid extension band 13` / `Invalid stereo band 15` inside frame 1 — *the* original blocker |
| Same, block config 0 (mono), 1 (dual mono), 4 (7.1), 5 (quad) | reject at frame 0 |
| 960 B units as **one** AT9 frame (`superframe = 0`, avg 960) | "decodes", 256 samples per 960 B ⇒ ¼ of the declared duration — a valid prefix of a wrong model, not audio |
| strip a 2-byte `0x3293`/`0x3393` unit header, then 958 B / avg 239 | reject (`Invalid stereo band 15`) — the two bytes are genuine bitstream flags, not a header |
| strip 2 bytes per 240-byte frame, 952 B units | reject |
| ffmpeg only | a decode might be an ffmpeg bug, so the MIT reference decoder (libatrac9 C, same tables) was built locally and cross-checked: identical parse, identical failure at block config 2, identical success at 3 |
| ATRAC9 for the resident `0x21` family | **ruled out**: `bytes/samples` is exactly 16/28 (`boss01` 31 120 B / 54 432 samples), i.e. 16-byte blocks of 28 samples; the record carries no `0xFE` config word |
| the music `+0x0C` / `+0x28` words as the AT9 config | reject: neither has `0xFE` in its top byte, which the engine's validator requires |

### 5.0 The resident `0x42` family — identified as ATRAC9, still a gap

The `0x42` records are 56 bytes and carry a **second** codec word at `+0x2C` (the `0x21`
records stop at `+0x28`). Decoded with the same bit split as §4.1:

| fmt | record `+0x2C` | sri | block config | frame bytes | superframe |
|---|---|---|---|---|---|
| `0x00FF0142` (1 ch) | `0xFE7007E0` | 7 (48 kHz) | 0 (mono) | **64** | 0 (1 frame) |
| `0x00FF0242` (2 ch) | `0xFE740FE0` | 7 (48 kHz) | 2 (stereo) | **128** | 0 (1 frame) |

That word matches the family's geometry exactly (`0x00FF0142`: 58 304 B / 232 800 samples
= 64 B per 256 samples), so the engine stores these waves as **ATRAC9 at 96 kbps/channel**,
not as a 2-bit ADPCM.

The payload does not decode to spec, though. At the heap offset the record's `start`
implies (see §7):

* mono (`0x0142`): ffmpeg accepts the record's own config and decodes **200 448 frames
  (4.18 s of 4.85 s)**, RMS −16.5 dBFS, peak 0.60, with one concealed
  `Invalid extension band 13!` — i.e. real audio, 86 % of the declared length;
* stereo (`0x0242`): rejects at frame 0.

The bounded ADPCM attempt (`--probe-0x42`, mono record, 58 304 B, declared 4.85 s) found
nothing that reaches the declared duration at 16 B / 64 samples:

| decoder | invocation | result |
|---|---|---|
| `adpcm_psx` | `.vag` wrapper, 16 B blocks | rc 0, 31 668 frames = **0.66 s**, `Error submitting packet to decoder` |
| `adpcm_ms` | WAV tag `0x0002`, block 64 | rc 69, `ERROR: block_predictor[0] = 55` (out of range), 0 frames |
| `adpcm_ima_wav` | WAV tag `0x0011`, block 64 | rc 0, 6 534 frames = **0.14 s** |
| `adpcm_ct` | WAV tag `0x0020`, block 64 | rc 0, 116 608 frames = **2.43 s** (exactly half: 2 samples/byte) |

The binary shows no 2-bit ADPCM variant either: the ADPCM path in `eboot.elf` is the
streaming manager (`adpcmManager.cpp`, `B_adpcmOpenReq`, `Sg2ExStAdpcmOpen`,
`Show ADPCM Streams`) and there is no `2bit`/`4bit` string anywhere in the image.

**Verdict:** `0x42` = ATRAC9 (proven by the record's own config word and by 86 % of a mono
record decoding to audio), but its per-record payload offset is not derivable from the
record, so it stays an **accepted gap**. The gate prints it as such and refuses to pass
unless the gap is named on the command line (§5.1).

### 5.1 What the gate checks, and the variant honesty rules

Per carved WAV (hard, non-zero exit on failure):

* the delivered `pcm_s16le` WAV's header is the record's channel count and rate;
* frame count within **50 ms** of `samples/rate` (measured 6.4 … 24.0 ms on all 33 tracks —
  encoder padding, always in the same direction);
* RMS > **−60 dBFS**, samples finite, s16 peak ≤ 1.0 (float peak > 8.0 is treated as a
  broken decode);
* the AT9 config word derived from `(rate, channels, bytes/samples)` equals the word stored
  in the record at `WAVE+0x40` (it does on all 33: `FE761DF0`).

Warnings (recorded in `out/s5/gate/manifest.json`, not fatal): float peak > 1.0, s16 clamp
fraction > 0.5 %.

**Resident variants are required by default.** Every format word found in the bank must
decode; a variant that is skipped fails the gate unless it is named on the command line:

```bash
# default: require all four boss01 variants -> exit 1 today (§7 says why)
python3 tools/re/audio/s5_decode_gate.py --root "$SOTC_GAME_ROOT" --out out/s5/gate --check

# accept the two 0x42 gaps explicitly
python3 tools/re/audio/s5_decode_gate.py --root "$SOTC_GAME_ROOT" --out out/s5/gate --check     --allow-unresolved 0x0142,0x0242

# narrow the requirement (e.g. one bank's subset)
... --check --require 0x0121,0x0221
```

* `--require` wins over the default "all variants"; `--allow-unresolved` names accepted
  gaps. A word in both is a configuration error (exit 2).
* **Exit codes:** 0 = every required variant decoded; 1 = a required variant is missing or
  a skipped variant is not allow-listed; 2 = configuration/error; 3 = every variant is an
  accepted gap, i.e. **no decodable family for Task 1 to attribute cues to → the milestone
  stops at Task 1**.
* On a passing run the gate prints `ALLOWED GAPS: …` and
  `POLICY: the curated cues must come from the decoded families (…); if Task 1 attributes a
  core cue to <gap> the milestone stops at Task 1.`

Negative tests run against the final script:

* an all-zero `music.sxd2` of the right size → `GATE ERROR … ffmpeg failed` , exit 2;
* synthetic stats for silence, a 60 ms duration error, NaN, a runaway peak and an s16 peak
  > 1.0 → all rejected by `check_analysis` (each rule fires on its own case);
* default run (all four resident variants required) → exit 1;
* `--allow-unresolved 0x0121,0x0142,0x0221,0x0242` → exit 3 (`BLOCKED: no resident family
  is validated`);
* `--allow-unresolved 0x0142,0x0242` → exit 1 (the `0x21` pair is still required).

---

## 6. The 33-track sweep (evidence)

`--tracks 1 --first N` for N = 0..32, one decode at a time:

| # | track | dur s | err ms | RMS dBFS | peak (float) | s16 clamps |
|---|---|---|---|---|---|---|
| 0 | Prologue | 227.07 | 12.1 | −21.3 | 1.11 | 0.0000% |
| 1 | Opening 1 | 113.41 | 20.1 | −21.6 | 0.74 | 0.0000% |
| 2 | Opening 2 | 115.52 | 6.7 | −22.1 | 0.61 | 0.0000% |
| 3 | Event 1 | 125.29 | 9.4 | −17.1 | 3.86 | 0.0310% |
| 4 | Event 2 | 114.97 | 23.6 | −17.6 | 1.23 | 0.0010% |
| 5 | Field 1 | 114.07 | 16.1 | −18.9 | 2.17 | 0.0100% |
| 6 | Boss 1 | 125.93 | 6.4 | −13.1 | 4.22 | 0.1970% |
| 7 | Boss 2 | 117.10 | 15.8 | −13.6 | 2.44 | 0.1690% |
| 8 | Defeat | 105.64 | 10.7 | −16.9 | 2.22 | 0.0050% |
| 9 | Collapse | 31.96 | 23.6 | −19.1 | 2.07 | 0.0070% |
| 10 | Field 2 | 37.63 | 23.0 | −23.4 | 0.78 | 0.0000% |
| 11 | Boss 3 | 116.29 | 7.8 | −14.0 | 3.87 | 0.1600% |
| 12 | Boss 4 | 139.14 | 19.4 | −11.6 | 4.02 | 0.5410% |
| 13 | Field 3 | 25.05 | 6.7 | −22.5 | 1.54 | 0.0010% |
| 14 | Boss 5 | 110.55 | 11.9 | −23.2 | 1.11 | 0.0000% |
| 15 | Boss 8 | 135.42 | 7.9 | −14.8 | 3.07 | 0.0580% |
| 16 | Game Over | 41.30 | 16.7 | −19.6 | 0.75 | 0.0000% |
| 17 | Demo 1 | 204.14 | 18.5 | −19.5 | 2.15 | 0.0130% |
| 18 | Boss 6 | 107.61 | 14.9 | −19.5 | 1.02 | 0.0000% |
| 19 | Boss 7 | 113.22 | 9.0 | −15.6 | 3.31 | 0.0730% |
| 20 | Boss 9 | 124.25 | 20.7 | −14.7 | 2.20 | 0.0320% |
| 21 | Demo 2 | 104.62 | 9.2 | −18.7 | 2.02 | 0.0110% |
| 22 | Field 4 | 31.57 | 23.1 | −21.4 | 0.99 | 0.0000% |
| 23 | Boss 10 | 125.40 | 14.6 | −16.4 | 2.42 | 0.0380% |
| 24 | Boss 11 | 132.33 | 8.5 | −14.7 | 2.21 | 0.0250% |
| 25 | Event 3 | 104.88 | 18.5 | −13.7 | 3.02 | 0.0640% |
| 26 | Boss 12 | 123.56 | 23.8 | −14.0 | 3.02 | 0.0850% |
| 27 | Field 5 | 31.06 | 13.2 | −18.0 | 2.01 | 0.0120% |
| 28 | Boss 13 | 141.18 | 14.4 | −19.7 | 2.40 | 0.0070% |
| 29 | Ending 1 | 31.57 | 13.3 | −25.4 | 0.50 | 0.0000% |
| 30 | Ending 2 | 50.58 | 23.1 | −14.5 | 2.38 | 0.0840% |
| 31 | Ending 3 | 428.97 | 24.0 | −19.5 | 2.47 | 0.0090% |
| 32 | Ending 4 | 179.12 | 19.9 | −22.6 | 1.43 | 0.0000% |

Total 3830.38 s = **63.84 min**; 33/33 pass.

## 7. Resident banks — geometry fits PS-ADPCM, the offsets do not

`boss01.sxd` has 138 `WAVE` records; the `offsets[]` steps are 44 (94×), 52 (13×),
64 (14×) and 72 (16×), i.e. real record sizes of **48 / 56 / 68 / 76 bytes** (the `+4*i`
rule absorbs the difference). Field slots are the same as §2: `+0x04` fmt, `+0x08` rate,
`+0x14` samples, `+0x20` bytes, `+0x24` start; records ≥ 56 B add the AT9 config word at
`+0x2C` (§5.0).

The `0x21` family's ratio is exactly **16 bytes per 28 samples**, i.e. classic PS-ADPCM
(16-byte blocks: 1 header byte, 1 flags byte, 14 nibble bytes). The gate can decode it —
`ps_adpcm_decode()` in the script, 28 samples per block, low nibble first, predictors
`{0,0},{60,0},{115,-52},{98,-55},{122,-60}`, `(s1·c1 + s2·c2) >> 6`, clamped to int16,
channels block-interleaved for 2 ch:

```bash
# exploratory only (no --check): writes boss01_fmt00FF0121_*.wav for inspection
python3 tools/re/audio/s5_decode_gate.py --root "$SOTC_GAME_ROOT" --out out/s5/gate
```

**The offsets are the problem, and they are disproved, not merely unproven.** The
`start`/`bytes` fields cannot be a partition of `DATA`:

* `Σ bytes = 18 407 584` over a span of `15 874 512` (heap = 15 881 904);
* **all 137 consecutive `(start, bytes)` pairs overlap**, and the total overlap is
  `2 533 072` bytes = `Σ bytes − span` **exactly**, with zero gaps;
* at the implied offsets (`heap + (start − min(start))`, `min(start) = 0xE404`) the 16-byte
  block headers are *uniform* — filter nibble 0-7 at 14-24 % each, shift 0-15 at 5-10 % —
  where a real PS-ADPCM stream would show shift/filter values only in the legal ranges.

So `heap + (start − min(start))` is **not** the resident layout: the first record happens to
land on the heap head (which is why the first `0c 00 00…` blocks looked like VAG silence) and
every later record drifts. The `0x21` WAVs the gate writes are therefore **exploratory
artifacts, not validated decodes**, and the variant is reported unresolved.

The exact PS-ADPCM invocation that produces them (kept so Task 2 does not have to re-derive
the algorithm, and so anyone can compare it against a future mapping):

```
blocks   = bytes // 16                 # 1 945 blocks for a 31 120-byte record
for each block at  p = heap_offset + 16*k:
    h        = data[p]                 # shift = h & 0x0F, filter = (h >> 4) & 7
    shift, filter = h & 0xF, (h >> 4) & 7
    c1, c2   = ((0,0),(60,0),(115,-52),(98,-55),(122,-60))[filter]
    for each nibble in data[p+2 .. p+16] (low nibble first):
        v = sign_extend(nibble << 12) >> shift
        s = clamp16(v + ((s1*c1 + s2*c2) >> 6)); s2, s1 = s1, s
# stereo: block k belongs to channel k % 2 (block interleaved), each channel keeps
# its own s1/s2; the declared sample count is per channel.
```

Cross-check: ffmpeg's strict `adpcm_psx` on the same bytes (a 48-byte `VAGp` wrapper) stops
after ~63 % of the blocks with `Error submitting packet to decoder`, because the uniform
header bytes are not legal PS-ADPCM shifts/filters. That is the same evidence from the other
side.

Open (Task 1/2 must not assume otherwise):

1. the resident heap mapping — what `start` indexes (record headers? a de-duplicated pool?
   stale metadata from a rebuild?) is unknown; every resident carve today is a guess;
2. the `0x21` block header layout, once the mapping is fixed;
3. whether `0x42`'s AT9 payload is interleaved differently from `0x21`'s (the mono record
   decodes 86 % of its frames at the same offset, the stereo one 0 %).

## 8. Converter policy (from the measured facts)

Rulings R6-R9 in `.superpowers/sdd/2026-10-21-s5-audio/progress.md` are the binding record;
the measurements that drive them:

* **Music is 5.1 / 48 kHz, and every master overshoots unity** — decoded float peak 0.50 …
  4.22 across the 33 tracks, s16 export clamping 0.0000 … 0.54 % of samples (§6).
* The app mixer is **stereo** (R6). The converter therefore:

  1. decodes ATRAC9 to float (ffmpeg `pcm_f32le`) at the record's rate and channel count;
  2. **downmixes 5.1 → stereo** at conversion time (`L/R + 0.707·C + 0.5·LFE/surround`,
     clipped) — the runtime never sees 6 channels;
  3. **normalizes each clip to peak ≤ 0.99** and records the applied gain in the manifest
     (`gain_db`), so the mixer cannot clip on a hot master;
  4. keeps the decoded length; the manifest's duration is the measured PCM length, not the
     record's sample field (R1/D10).

  Implied gains from the sweep: −0.1 dB (Ending 1, peak 0.50) to −12.5 dB (Boss 1, peak
  4.22).
* **No resident clip exists yet** — §7 keeps every resident family unresolved, so the
  manifest carries music only until a family maps.
