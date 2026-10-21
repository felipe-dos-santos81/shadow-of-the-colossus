# S5 audio — cue attribution

Status: **core cues mapped** (2026-10-21, Task 1). The machine-readable authority is
`tools/re/audio/s5_map.json`; `tools/re/audio/s5_audition.py --map … --check` decodes every
mapped wave with the loader-derived payload formula (`wave_record_offset + 0x24 + start`,
`docs/notes/s5-audio-recon.md` §7) and fails on a missing, silent or short wave.

```
caffeinate -dimsu python3 tools/re/audio/s5_audition.py \
    --root "$SOTC_GAME_ROOT" --map tools/re/audio/s5_map.json \
    --out out/s5/audition --check
  valus.telegraph  boss01  w003 boss01_vo_atk1            1.202 s  RMS -15.6 dBFS
  valus.impact     boss01  w026 boss01_atk_hit_ground1    7.543 s  RMS -13.3 dBFS
  valus.hurt       boss01  w006 boss01_vo_dmg1            1.051 s  RMS -12.4 dBFS
  player.swing     com_obj w007 wnd_LifeSwd_swish_t1_01   0.465 s  RMS -10.2 dBFS
  player.hit       com_obj w021 wnd_swd_hit_gravel_t1_01  0.709 s  RMS -12.2 dBFS
  player.hurt      com_v   w040 bvo_dmg_m                 0.270 s  RMS  -8.6 dBFS
  player.death     com_b_h w657 b_dead_by_boss            0.812 s  RMS -15.4 dBFS
AUDITION PASSED: 7 mapped wave(s) decoded, 48 kHz, non-silent
```

## The map

| event | bank | name | WAVE index | method | confidence |
|---|---|---|---|---|---|
| `valus.telegraph` | boss01 | `boss01_vo_atk1` | 3 | table | medium |
| `valus.impact` | boss01 | `boss01_atk_hit_ground1` | 26 | ghidra | high |
| `valus.hurt` | boss01 | `boss01_vo_dmg1` | 6 | table | medium |
| `player.swing` | com_obj | `wnd_LifeSwd_swish_t1_01` | 7 | table | high |
| `player.hit` | com_obj | `wnd_swd_hit_gravel_t1_01` | 21 | table | medium |
| `player.hurt` | com_v | `bvo_dmg_m` | 40 | table | medium |
| `player.death` | com_b_h | `b_dead_by_boss` | 657 | table | medium |

**`ghidra`** = the name resolves through the bank's `NAME` hash table to a `REQD` record,
whose `+0x0C` points into `SEQD`; the sequence's first `0xa4` (`SFX_PLAY_TONE`) operand is
a tone id, and the boss01 `TONE` table (12-byte entries at `TONE+0x20`, fields
`key, key, flags, next, wave, 0`) gives the WAVE index. This chain is binary-derived.

**`table`** = the WAVE table order matches the bank's name-pool insertion order for the
resident subset; the mapped wave's duration matches the cue's semantic class. This is
strong for `com_obj`/`com_v`/`com_b_h` (see the evidence below) but is an inference from
the shipped table order, not a decoded pointer — hence `medium` except where the duration
is unmistakable (`player.swing`).

**`audition`** = decoded and inspected (spectrogram + envelope) without a table link.

## Evidence per cue

* **`valus.impact` — boss01 w026 (`boss01_atk_hit_ground1`).** `NAME` hash lookup of
  `boss01_atk_hit_ground1` → REQD record 303 → `SEQD` timeline at `+0x13E4` → bytecode at
  `+0x23BA` → `PLAY_TONE 43`; `TONE[43].wave = 26`. The cue is a 5-layer sequence
  (tones 43-47 → waves 26-30); wave 26 is the first and longest layer (7.54 s, 2 ch).
  The decode is a low-frequency evolving rumble, consistent with a colossus stomp with a
  long debris tail.
* **`valus.telegraph` — boss01 w003 (`boss01_vo_atk1`).** Name-pool order: the first 108
  WAVE records match the first 108 names in duration class (wave 0 4.12 s 2 ch ground
  rumble; waves 1-2 the shorter ground hits; waves 3-5 the three attack vocals at 1.20 /
  0.93 / 1.08 s; waves 6-13 the eight damage vocals). Wave 3's spectrogram is a 1.2 s
  broadband roar with formant bands. Alternative if the order breaks: the 0x42 pair
  108/109 (AT9, 4.86 s) is a large roar; recorded in the map's `note`.
* **`valus.hurt` — boss01 w006 (`boss01_vo_dmg1`).** Same order; wave 6 is the first
  damage vocal (1.05 s), spectrogram a 1.05 s roar. The dedicated `mavo_damage_l/s` names
  are later sequences, not the seven core cues.
* **`player.swing` — com_obj w007 (`wnd_LifeSwd_swish_t1_01`).** Name-pool order; wave 7 is
  a 0.464 s whoosh. The name is the first of the four LifeSword swish variants at names
  7-10; waves 7-10 are 0.464 / 1.561 / 1.561 / 1.476 s, the first being the short variant
  a combat swing uses. The `PLAY_TONE` chain for this name resolves to tones 16/17, but the
  com_obj `TONE` chunk is not the flat 12-byte form used by boss01, so the chain cannot be
  completed; the duration match is the deciding evidence.
* **`player.hit` — com_obj w021 (`wnd_swd_hit_gravel_t1_01`).** Name-pool order; wave 21 is
  a 0.708 s impact, first of the four gravel-hit variants (names 21-24 → waves 21-24 at
  0.708 / 0.621 / 0.668 / 0.792 s).
* **`player.hurt` — com_v w040 (`bvo_dmg_m`).** Name-pool order; wave 40 is a 0.270 s
  grunt, one of the ten resident damage vocals (`bvo_dmg10/11/12`, `bvo_dmg_m/s`,
  `bvo_dmg_feeler`, `bvo_dmg_m1/m2/s1/s2` at names 37-46 → waves 37-46, all 0.17-0.81 s).
  `bvo_dmg_m` is the medium-strength default; the longer `bvo_dmg10` (0.807 s) is the
  heavier hit.
* **`player.death` — com_b_h w657 (`b_dead_by_boss`).** Name-pool order at name 657 of
  1 161 resident waves; wave 657 is a 0.811 s vocal, between the water-landing sounds
  (654-656) and the grass-walk set (658+). The dedicated `bvo_die_fall` / `bvo_die_dmg`
  names in com_v are **sequence names beyond the 63 resident com_v waves**, so they cannot
  be decoded as a single resident wave.

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
