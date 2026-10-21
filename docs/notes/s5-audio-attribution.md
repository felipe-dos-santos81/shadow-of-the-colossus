# S5 audio — cue attribution

Status: **core cues mapped** (2026-10-21, Task 1). The machine-readable authority is
`tools/re/audio/s5_map.json`; `tools/re/audio/s5_audition.py --map … --check` decodes every
mapped wave with the loader-derived payload formula (`wave_record_offset + 0x24 + start`,
`docs/notes/s5-audio-recon.md` §7) and fails on a missing, silent or short wave.

```
caffeinate -dimsu python3 tools/re/audio/s5_audition.py \
    --root "$SOTC_GAME_ROOT" --map tools/re/audio/s5_map.json \
    --out out/s5/audition --check
  valus.telegraph  chain: reqd 340 -> tone 20 -> wave 3
  valus.telegraph  boss01  w003 boss01_vo_atk1            1.202 s  48000 Hz  RMS -15.6 dBFS
  valus.impact     chain: reqd 303 -> tone 43 -> wave 26
  valus.impact     boss01  w026 boss01_atk_hit_ground1    7.543 s  48000 Hz  RMS -13.3 dBFS
  valus.hurt       chain: reqd 331 -> tone 23 -> wave 6
  valus.hurt       boss01  w006 boss01_vo_dmg1            1.051 s  48000 Hz  RMS -12.4 dBFS
  player.swing     audition: wave 7 (identity not chain-proven)
  player.swing     com_obj w007 wnd_LifeSwd_swish_t1_01   0.465 s  48000 Hz  RMS -10.2 dBFS
  player.hit       audition: wave 21 (identity not chain-proven)
  player.hit       com_obj w021 wnd_swd_hit_gravel_t1_01  0.709 s  48000 Hz  RMS -12.2 dBFS
  player.hurt      audition: wave 40 (identity not chain-proven)
  player.hurt      com_v   w040 bvo_dmg_m                 0.270 s  48000 Hz  RMS  -8.6 dBFS
  player.death     audition: wave 657 (identity not chain-proven)
  player.death     com_b_h w657 b_dead_by_boss            0.812 s  48000 Hz  RMS -15.4 dBFS
AUDITION PASSED: 7 mapped wave(s), all 48000 Hz, non-silent
```

## The map

`name` is the **sound-request name** (the `NAME`/`REQD` namespace), not a per-wave label.
`wave` is the WAVE index the request plays; `layers` lists the other layers of a layered
sound. `reqd` is the `REQD` record index (NAME hash); `tone` is the `PLAY_TONE` operand
where the chain is walked. `wave_pool_label` (in the JSON) is the pool string at that WAVE
index — a label for the record, **not** an identity claim.

| event | bank | request name | reqd | tone | wave | layers | method | confidence |
|---|---|---|---|---|---|---|---|---|
| `valus.telegraph` | boss01 | `boss01_vo_atk1` | 340 | 20 | 3 | — | ghidra | medium |
| `valus.impact` | boss01 | `boss01_atk_hit_ground1` | 303 | 43 | 26 | 26-30 | ghidra | high |
| `valus.hurt` | boss01 | `boss01_vo_dmg1` | 331 | 23 | 6 | — | ghidra | medium |
| `player.swing` | com_obj | `wnd_LifeSwd_swish_t1_01` | 118 | — | 7 | — | audition | medium |
| `player.hit` | com_obj | `wnd_swd_hit_gravel_t1_01` | 60 | — | 21 | — | audition | medium |
| `player.hurt` | com_v | `bvo_dmg_m` | 172 | — | 40 | — | audition | medium |
| `player.death` | com_b_h | `b_dead_by_boss` | 1573 | — | 657 | — | audition | medium |

**`ghidra`** = the row carries a walked chain. `NAME` hash → `REQD` id → `SEQD` offset →
`PLAY_TONE` → `TONE[tone].wave`, and `s5_audition.py --check` re-runs the `NAME` hash and
asserts `TONE[tone].wave == wave`. The boss01 `TONE` table is the flat 12-byte form at
`TONE+0x20` (`key, key, flags, next, wave, 0`). For `valus.telegraph`/`valus.hurt` the
tone→wave step is binary; the name→tone step is the request/play-list order
(`TONE[18..24] → waves 1..7`), hence `medium`.

**`audition`** = no chain could be completed (the com_obj/com_v/com_b_h `TONE` tables are
not the flat 12-byte form), so the row is explicitly re-scoped to a **verified index**:
`verified_index: true` in the JSON, and the script prints "identity not chain-proven".
The ear remains the final identity check. The scripted check verifies the requirement is
present, the name is in the `NAME` table, the wave decodes at 48 kHz and is non-silent —
it does **not** claim identity for these rows.

## Evidence per cue

* **`valus.impact` — boss01 w026 (`boss01_atk_hit_ground1`).** `NAME` hash → `REQD` 303 →
  `SEQD` timeline `+0x13E4` → bytecode `+0x23BA` → `a4 01 43` (`PLAY_TONE 43`) →
  `TONE[43] = (key 57, wave 26)`. The sequence then plays tones 44-47 → waves 27-30, so the
  request is a 5-layer ground impact and wave 26 is the first/longest layer (7.54 s, 2 ch).
  The pool label at index 26 is `boss01_FS_far3_0`, **not** the request name — proof that
  the pool is a request namespace, not a per-wave name list.
* **`valus.telegraph` — boss01 w003 (`boss01_vo_atk1`).** `REQD` 340 (sequence `0x10B1`) →
  request-order tone 20 → `TONE[20] = (key 34, wave 3)`. The `TONE[18..24] → waves 1..7`
  range and the request order (ground2, ground3, vo_atk1, vo_atk2, vo_atk3, vo_dmg1,
  vo_dmg2) put vo_atk1 on tone 20. Wave 3 is a 1.2 s broadband roar with formant bands.
* **`valus.hurt` — boss01 w006 (`boss01_vo_dmg1`).** `REQD` 331 (sequence `0x11B6`) →
  request-order tone 23 → `TONE[23] = (key 37, wave 6)`. Wave 6 is a 1.05 s roar.
* **`player.swing` — com_obj w007 (`wnd_LifeSwd_swish_t1_01`).** `REQD` 118 (sequence
  `0x227A`) → `PLAY_TONE 16/17`; the com_obj `TONE` chunk is not flat, so no tone→wave
  proof. Audition pick: wave 7 is a 0.464 s 0x21 whoosh; the pool label at 7 is the same
  request string, but that is a label only.
* **`player.hit` — com_obj w021 (`wnd_swd_hit_gravel_t1_01`).** `REQD` 60 (sequence
  `0x2779`); no `PLAY_TONE` in the first window. Audition pick: wave 21 is a 0.708 s
  0x21 impact, the first of the gravel-hit variants.
* **`player.hurt` — com_v w040 (`bvo_dmg_m`).** `REQD` 172 (sequence `0x0FD0`); no
  `PLAY_TONE` decoded. Audition pick: wave 40 is a 0.270 s 0x21 grunt; the `bvo_dmg10/11/
  12`, `bvo_dmg_m/s`, `bvo_dmg_feeler`, `bvo_dmg_m1/m2/s1/s2` set is the resident damage
  vocal range.
* **`player.death` — com_b_h w657 (`b_dead_by_boss`).** `REQD` 1573 (sequence `0xD710`);
  no `PLAY_TONE` decoded. Audition pick: wave 657 is a 0.811 s 0x21 vocal, between the
  water-landing sounds (654-656) and the grass-walk set (658+). The dedicated `bvo_die_*`
  names in com_v are sequence names beyond the 63 resident com_v waves.

## Fidelity of the 0x21 rows

All seven cues are 0x21 PS-ADPCM. Filters 5-7 are fitted, not firmware-exact; the
per-cue distribution and boundary metric are in `s5-audio-recon.md` §7.2. In short:

| cue | wave | filter 5-7 blocks | boundary ratio |
|---|---|---|---|
| `valus.telegraph` | 3 | 10.5 % | 2.73 |
| `valus.impact` | 26 | 11.1 % | 0.98 |
| `valus.hurt` | 6 | 21.0 % | 2.86 |
| `player.swing` | 7 | 29.9 % | 2.66 |
| `player.hit` | 21 | 39.3 % | 1.64 |
| `player.hurt` | 40 | 23.3 % | 1.26 |
| `player.death` | 657 | 27.8 % | 2.52 |

The gate proves existence, rate, length and non-silence — **not fidelity**. No PS-ADPCM
coefficient table was found in `eboot.elf` (exact/approximate int16, int32×64 and float
scans: 0 hits), so the true filters 5-7 live in the NGS2 firmware. The user's ear is the
final fidelity check; the 0x21 map confidence is `medium` for this reason.

## Deferred (with reasons)

| event | bank | reason |
|---|---|---|
| `valus.death` | boss01 | `mavo_death` resolves to tones 97-99 → waves 75-77 (0x21, 3.7-5.3 s), but the layering is unresolved and it is not a core cue. |
| `valus.grab` | boss01 | layered 0x21 body-movement sequences; not a core cue. |
| `valus.fall` | boss01 | layered 0x21 rise/fall sequences; not a core cue. |
| `valus.breath` | boss01 | layered 0x21 ambience; not a core cue. |
| `valus.ambience` | boss01 | `boss01_FS_*` / `mammo_*` beds are layered 0x21 sequences; not a core cue. |
| `player.grab` | com_b_h | hand/grab cues are layered; not a core cue. |
| `player.fall` | com_v | `bvo_fall*` names are beyond the 63 resident com_v waves; streamed or layered. |
| `player.breath` | com_v | `bvo_breath*` / `bvo_run_breath*` names are beyond the 63 resident com_v waves; streamed or layered. |

## Reproduce

```bash
export SOTC_GAME_ROOT=…/SOTC-extract/extracted/game
# gate: music + all four boss01 resident variants
caffeinate -dimsu python3 tools/re/audio/s5_decode_gate.py \
    --root "$SOTC_GAME_ROOT" --out out/s5/gate --check
# attribution: decode the seven core cues
caffeinate -dimsu python3 tools/re/audio/s5_audition.py \
    --root "$SOTC_GAME_ROOT" --map tools/re/audio/s5_map.json \
    --out out/s5/audition --check
# explore a whole bank (writes every wave to out/s5/audition/<bank>/)
caffeinate -dimsu python3 tools/re/audio/s5_audition.py \
    --root "$SOTC_GAME_ROOT" --bank boss01 --all --out out/s5/audition
```
