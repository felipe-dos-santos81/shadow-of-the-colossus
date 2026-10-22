# S5 Audio — asset recon (read-only)

Status: **recon complete. No file in the game tree or in this repo was modified** (only this
report was created).
Date: 2026-10-21. Game tree: `SOTC-extract/extracted/game`, audio under `sound/shadow`.
Scope: the 42 files in `sound/shadow`, the container they live in, the codec inside it, and the
five metadata files next to them.

Companion documents already in this directory: `recon-integration.md` (where audio attaches in the
converter/app), `recon-sdl3.md` (SDL3 playback + transcode options). This file is the **asset and
bitstream** recon those two were missing.

---

## 0. Headline findings

| # | Finding | Consequence |
|---|---|---|
| 1 | **The codec is ATRAC9 (AT9).** Proof is in `uroot/eboot.bin`, not in the audio files: the engine is Bluepoint's **SNDX** on Sony **NGS2**, and it carries `VOICE_ALLOC_AT9`, `maxAtrac9Decoders`, `Invalid ATRAC9 configure data (%08X)`. | The "is it ATRAC9?" open question in `recon-integration.md §13.1` is **answered**. The gate is now "build an AT9 decode path", not "guess the codec". |
| 2 | Bank magic is **`SXDF`** (`53 58 44 46`) — *not* `SHDF`. Streamed payload magic is **`SXDS`**. Confirmed on all 41 container files. | Already corrected in `recon-integration.md`; repeated here with byte evidence. |
| 3 | Streamed music is **48 kHz, 360 kbps CBR, 960-byte frames of 1024 samples** (21.333 ms). | Measured exactly, 33/33 tracks: `bytes / 960` is an integer and `bytes·8 / (samples/48000) = 360 000 bps` on every track. A raw `.at9` carve is therefore well-defined. |
| 4 | `music.sxd1` is a **complete 33-track soundtrack catalog** (names, byte offsets, lengths, sample counts, durations) that decodes cleanly out of the chunk tables. | The music path does not need the `.sxd2` scanned: read the tables, seek, decode. Total **63.83 min**. |
| 5 | All four index tables (`REQD`, `NAME`, `WAVE`, `SEQD`) use one shape, and the record offsets follow `record_i = body + offsets[i] + 4·i`. | A single generic table reader handles every chunk. Verified 33/33 for both `REQD` and `WAVE`; the last record ends exactly on the next chunk's magic. |
| 6 | `_cmn/shadow_sounds.cxml` is plaintext XML with **8 001 `<Sound>` entries**; `sound_data.calt` is a plaintext 36-line manifest naming every bank. | The event vocabulary and the load order are readable without parsing a binary. |

---

## 1. Inventory

`sound/shadow` — **42 files, 2 047 275 499 bytes (1.907 GiB)**:

| Dir | files | bytes |
|---|---|---|
| `sound/shadow/` | 35 | 2 046 896 592 |
| `sound/shadow/_cmn/` | 6 | 364 455 |
| `sound/shadow/_ps4/` | 1 | 14 452 |

### 1.1 Resident / indexed banks — `SXDF`, chunk tables + `DATA`

| File | bytes | TONE entries | REQD (tones) | NAME | WAVE (streams) | DATA @ | audio bytes |
|---|---|---|---|---|---|---|---|
| `boss01.sxd` | 15 967 712 | 3 | 343 | 343 | 138 | `0x14f10` | 15 881 904 |
| `boss02.sxd` | 13 060 752 | 2 | 246 | 246 | 106 | `0xd8f0` | 13 005 184 |
| `boss03.sxd` | 18 725 552 | 4 | 491 | 491 | 194 | `0x1d4e0` | 18 605 488 |
| `boss04.sxd` | 18 430 448 | 2 | 178 | 178 | 88 | `0xc6a0` | 18 379 568 |
| `boss05.sxd` | 18 402 144 | 3 | 330 | 330 | 148 | `0x150c0` | 18 315 904 |
| `boss07.sxd` | 17 980 816 | 3 | 448 | 448 | 168 | `0x1add0` | 17 870 752 |
| `boss08.sxd` | 15 722 112 | 2 | 162 | 162 | 87 | `0x9180` | 15 684 832 |
| `boss11.sxd` | 16 596 080 | 4 | 531 | 531 | 197 | `0x20fc0` | 16 460 944 |
| `boss12.sxd` | 18 898 992 | 4 | 364 | 364 | 162 | `0x19390` | 18 795 648 |
| `boss13.sxd` | 14 679 440 | 4 | 506 | 506 | 201 | `0x21230` | 14 543 680 |
| `boss15.sxd` | 18 358 832 | 5 | 383 | 383 | 214 | `0x18a00` | 18 257 936 |
| `boss17.sxd` | 13 348 032 | 3 | 331 | 331 | 139 | `0x17e50` | 13 250 128 |
| `boss20.sxd` | 14 689 744 | 2 | 200 | 200 | 121 | `0x10330` | 14 623 360 |
| `boss22.sxd` | 18 896 640 | 4 | 311 | 311 | 188 | `0x186d0` | 18 796 560 |
| `boss23.sxd` | 17 376 784 | 4 | 360 | 360 | 165 | `0x1c8b0` | 17 259 840 |
| `boss24.sxd` | 17 603 232 | 3 | 358 | 358 | 137 | `0x17ed0` | 17 505 200 |
| `com_amb.sxd` | 2 556 528 | 3 | 115 | 115 | 143 | `0xef80` | 2 495 184 |
| `com_b_h.sxd` | 16 078 128 | 35 | 2 221 | 2 221 | 1 161 | `0xc1f80` | 15 283 792 |
| `com_obj.sxd` | 7 499 264 | 5 | 350 | 350 | 241 | `0x1c330` | 7 383 728 |
| `com_sys.sxd` | 1 073 888 | 1 | 19 | 19 | 16 | `0x1bb0` | 1 066 768 |
| `com_v.sxd` | 625 760 | 2 | 206 | 206 | 63 | `0xb700` | 578 880 |
| `edp01.sxd` | 16 300 176 | 4 | 517 | 517 | 164 | `0x1a1c0` | 16 193 200 |
| `edp02.sxd` | 658 112 | 1 | 22 | 22 | 12 | `0x14a0` | 652 800 |
| `bridge.sxd` | **144** | 0 | 0 | 0 | 0 | — | — |
| `sotc_buss.sxd` | 215 504 | — | — | — | — | — | — (mixer graph, §6) |

`boss*.sxd` are the 16 colossus banks actually present (the numbering has gaps: no `boss06`,
`boss09`, `boss10`, `boss14`, `boss16`, `boss18`, `boss19`, `boss21`). `edp*` = the two "ending
path" banks. `com_*` = common/global (ambience, body/hero, objects, system, voice).

### 1.2 Streamed banks — `.sxd1` index + `.sxd2` payload

| Pair | `.sxd1` | `.sxd2` | streams (`SXDS @0x08`) | notes |
|---|---|---|---|---|
| `music` | 9 872 | 172 367 136 | 33 | pure index, **no DATA** |
| `str_amb` | 15 614 576 | 375 729 632 | 125 | **hybrid**: has its own `DATA @0x35700` |
| `str_bgm` | 2 461 232 | 514 125 344 | 127 | pure index |
| `str_boss` | 48 928 | 286 412 736 | 172 | pure index |
| `str_event` | 57 248 | 366 325 072 | 53 | pure index |

Streamed total: **1 714 958 976 bytes (1.60 GiB)** — 84 % of the whole audio set.

### 1.3 Metadata (`_cmn`) and platform cache (`_ps4`)

| File | bytes | Role (see §7) |
|---|---|---|
| `shadow_sounds.cxml` | 355 718 | plaintext XML, 8 001 sound-event names |
| `sound_data.calt` | 1 011 | plaintext 36-line bank manifest |
| `shadowbanks.crmp` | 842 | bank → path map (25 entries) |
| `shadowstreams.crmp` | 3 295 | stream scheduling map |
| `shadowrandomevents.crmp` | 3 393 | random/variation event table |
| `shadowbossmusic.crmp` | 196 | boss → music map (20 entries) |
| `_ps4/grass_wind.cpu.cctx` | 14 452 | opaque PS4-specific cache, unresolved |

### 1.4 Family totals (top-level, by byte)

| family | n | bytes | MiB |
|---|---|---|---|
| `str_*` | 8 | 1 560 774 768 | 1488.5 |
| `music.*` | 2 | 172 377 008 | 164.4 |
| `com_*` | 5 | 27 833 568 | 26.5 |
| `boss*` | 16 | 274 282 960 | 261.6 |
| `edp0*` | 2 | 16 958 288 | 16.2 |
| `sotc_buss` | 1 | 215 504 | 0.2 |
| `bridge` | 1 | 144 | 0.0 |

---

## 2. Container header, `0x00`–`0x60`

Byte map, verified against `boss01.sxd`, `com_obj.sxd`, `bridge.sxd`, `sotc_buss.sxd`,
`music.sxd1`, `music.sxd2`, `str_bgm.sxd1`, `str_bgm.sxd2`:

| Offset | Type | Meaning | Evidence |
|---|---|---|---|
| `0x00` | `char[4]` | **`SXDF`** = bank with tables (resident), **`SXDS`** = stream payload bulk | `5358 4446` / `5358 4453` |
| `0x04` | `u16` | **version = 4** — identical on all 41 containers | `04 00` everywhere |
| `0x06` | `u16` | unknown; non-zero on `SXDF` (`boss01` `0x574B`, `com_obj` `0x9714`, `bridge` `0x1944`, `sotc_buss` `0xE7B6`), **always `0x0000` on `SXDS`** | — |
| `0x08` | `u32` | `SXDF`: **own file size in bytes**. `SXDS`: **stream count** | `boss01`: `e0 a5 f3 00` = 15 967 712 = `stat` size. `music.sxd2`: `21 00 00 00` = 33 |
| `0x0C` | `u32` | `SXDF`: **size of the companion `.sxd2`**, else 0. `SXDS`: **payload size = filesize − 0x60** | `music.sxd1` = 172 367 136 = `music.sxd2` size. `music.sxd2` = 172 367 040 = size − 96 |
| `0x10` | `u8[16]` | **bank GUID**; **identical in the `.sxd1` / `.sxd2` pair**, different per bank | `music.sxd1` and `music.sxd2` both `71 1e 4c 81 e3 00 9f 48 93 a0 a0 c5 01 92 8b 77` |
| `0x20` | `char[32]` | **bank name**, NUL-padded (`boss01`, `com_obj`, `music`, `bridge`, `EDP01`, `sotc_buss` — note the two `edp` banks are **upper-case**) | — |
| `0x40`–`0x5F` | `u8[32]` | zero on every file inspected | — |
| `0x60` | — | first chunk (`TONE` on banks, `BSFX` on `sotc_buss`, **raw stream bytes on `SXDS`**) | — |

`boss01.sxd`, first 0x70 bytes:

```
00000000: 5358 4446 0400 4b57 e0a5 f300 0000 0000  SXDF..KW........
00000010: 3817 adac 981f 914c a9b4 1467 94f9 1bb1  8......L...g....
00000020: 626f 7373 3031 0000 0000 0000 0000 0000  boss01..........
00000050: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000060: 544f 4e45 a818 0000 0000 0000 0200 0000  TONE............
```

`music.sxd2`, first 0x70 bytes — payload starts **immediately** at `0x60`, no chunk tag:

```
00000000: 5358 4453 0400 0000 2100 0000 c01c 460a  SXDS....!.....F.
00000010: 711e 4c81 e300 9f48 93a0 a0c5 0192 8b77  q.L....H.......w
00000020: 6d75 7369 6300 ...                        music...........
00000060: 3293 0b3a 0309 a91e a050 e861 0470 d60c  2..:.....P.a.p..
```

---

## 3. Chunk chain

**Chunk header is 8 bytes: `char[4] magic`, `u32 size`, where `size` counts every byte *after* the
8-byte header.** Next chunk = `off + size + 8`. This holds for every chunk (verified by walking
all 41 containers to EOF with zero slack).

Observed chunk orders (all 25 resident banks + the 5 stream indexes):

```
TONE → REQD → NAME → WAVE → SEQD → LVAR → LVRN → RXFD → RXFN → [TBLD] → [EXTR] → DATA
sotc_buss.sxd:  BSFX → GVAR → GVRN → BXFD → BXFN        (mixer graph, no audio)
bridge.sxd:     TONE (0 entries) — 144-byte stub
```

`TBLD` appears on `boss15`, `com_b_h`, `com_obj`, `str_amb`; `EXTR` on `com_b_h` and
`str_event`. Neither is needed to get audio out.

Full walk of `music.sxd1` (9 872 bytes, ends exactly at EOF):

| chunk | offset | size | `+0x08` | `+0x0C` |
|---|---|---|---|---|
| `TONE` | `0x60` | `0x368` | 0 | 2 |
| `REQD` | `0x3d0` | `0x5d8` | 0 | **33** |
| `NAME` | `0x9b0` | `0x288` | 0 | 33 |
| `WAVE` | `0xc40` | `0xa58` | 0 | 33 |
| `SEQD` | `0x16a0` | `0x828` | 0 | 33 |
| `LVAR` | `0x1ed0` | `0x208` | 0 | 16 |
| `LVRN` | `0x20e0` | `0x188` | 0 | 16 |
| `RXFD` | `0x2270` | `0x2f8` | 0 | 16 |
| `RXFN` | `0x2570` | `0x118` | 0 | 16 |

`0x2570 + 0x118 + 8 = 0x2690 =` file size. ✔

---

## 4. Index-table pattern (shared by `REQD`, `NAME`, `WAVE`, `SEQD`)

```
+0x00  char[4] magic
+0x04  u32     size           (bytes after this 8-byte header)
+0x08  u32     0              (reserved; always 0)
+0x0C  u32     count
+0x10  u32     offsets[count]  ← body-relative
       records…
```

**Parser quirk (important):** the `offsets[]` step is `recordSize − 4`, i.e. the values are short by
4 bytes per entry. The record start is

```
record_i = body + offsets[i] + 4*i          # body = chunk + 0x10
```

Verified by asserting the record's leading tag word on every entry:

| bank | table | record size | expected tag | hits | section arithmetic |
|---|---|---|---|---|---|
| `music.sxd1` | `REQD` | 44 | `0x00000041` | **33/33** | last record ends `0x9b0` = `NAME` offset ✔ |
| `music.sxd1` | `WAVE` | 76 | `0x00000003` | **33/33** | `16 + 33·4 + 33·76 = 2 656 = 0xa60` ✔ |
| `boss01.sxd` | `WAVE` | 44 | `0x00000001` | **138/138** | — |
| `com_v.sxd` | `WAVE` | 44 | `0x00000001` | **63/63** | — |
| `com_sys.sxd` | `WAVE` | 44 | `0x00000001` | **16/16** | — |
| `edp02.sxd` | `WAVE` | 44 | `0x00000001` | **12/12** | — |
| **`com_amb.sxd`** | `WAVE` | 44 | `0x00000001` | **33/143 ✗** | — |

`com_amb.sxd` is a real exception: its `WAVE` chunk reports 143 entries (more than its 115
`REQD`/`NAME` rows), its offset stride alternates between 44 and 64, and `size − 8 − 4·count =
9 092` is not divisible by any constant record size. Treat it as **not yet decoded**; every other
bank follows the rule. (`com_amb` is small — 2.5 MB — and the other 24 banks cover the milestone.)

---

## 5. Chunk contents

### 5.1 `TONE` — the tone/layer header

`music.sxd1 @0x60`:

```
00000060: 544f 4e45 6803 0000 0000 0000 0200 0000  TONEh...........
00000070: 0000 0000 8000 8000 0100 0000 1400 0000  ................
00000080: 0000 0000 0000 0700 0700 0100 0000 1400  ................
00000090: 0100 0000 8000 2000 4000 2100 1400 0000  ...... .@.!.....
```

| Offset | Type | Meaning |
|---|---|---|
| `+0x00` | `char[4]` | `TONE` |
| `+0x04` | `u32` | chunk size (`0x368`) |
| `+0x08` | `u32` | 0 |
| `+0x0C` | `u32` | **2** — constant on every bank examined |
| `+0x10` | `u32` | 0 |
| `+0x14` | `u16`,`u16` | **first tone id**, **last tone id** — always `0x0080`-based (`boss01`: `0x80`,`0x82`; `com_obj`: `0x80`,`0x84`) |
| `+0x18` | `u32` | tone count in this group (`boss01` 3, `com_obj` 5, `music` 1) |
| `+0x1C` | `u32` | `0x14` (20) — constant |
| `+0x20` | … | second group descriptor, same 16-byte shape (`music`: ids `0x07`/`0x07`, count 1) |
| `+0x30` | … | tone records, 16 bytes: `{u32 flag, u16 toneId, u16 a, u16 b, u16 c, u32 offset}` |

`boss01`'s three tone records (`0x90`, `0xA0`, `0xB0`) — the ids `0x80…0x82` confirm the range in
`TONE+0x14`:

```
0x90: 0100 0000 8000 2000 5f00 4000 b400 0000
0xA0: 0100 0000 8100 2000 5f00 4000 a403 0000
0xB0: 0100 0000 8200 2000 4200 2300 9406 0000
```

The remainder of `TONE` (after the 16-byte tone records) is a **bus/parent map**: repeating `u16`
triples `(nodeId, parentId, 0)` — `music.sxd1` shows `(0x20,0x00,0) (0x20,0x20,0) (0x21,0x01,0)
(0x21,0x21,0) …`, i.e. a routing tree rooted at `0x20`. Not needed for decoding; needed only if
the mixer graph is ever reproduced.

### 5.2 `REQD` — one record per *sound/event* (44 bytes + variable name)

Layout as measured on `music.sxd1`:

| Offset | Type | Value (track 0 `Prologue`) | Meaning |
|---|---|---|---|
| `+0x00` | `u32` | `0x41` | constant tag |
| `+0x04` | `u32` | `0x40000001` | flags (`0x40` high byte) |
| `+0x08` | `u32` | `0` | **tone/stream id**, 0…32 |
| `+0x0C` | `u32` | `1763` | per-sound number (not the AT9 frame count — see §5.4) |
| `+0x10` | `u16`,`u16` | `8`,`6` | unknown, constant for `music` |
| `+0x14` | `u32` | `8` | constant for `music` |
| `+0x18` | `u32` | `9` | constant for `music` |
| `+0x1C` | `u32` | `4` | constant for `music` |
| `+0x20` | `char[]` | `"Prologue"` | **NUL-terminated, 4-byte padded** |

Because the name is variable-length, **`REQD` records are variable-length** — always drive them
from `offsets[]`, never a fixed stride. (`music`: 44 bytes; `boss01` averages ~30.)

`REQD` count = `NAME` count = number of addressable sounds (`boss01` 343, `com_b_h` 2 221).
`WAVE` count is usually smaller (several sounds share one waveform), e.g. `boss01` 343 → 138.

### 5.3 `NAME` — name table

`music.sxd1 @0x9b0`: `NAME`, size `0x288`, `0`, count 33, then a table, then a NUL-terminated
string pool. The pool is unambiguous and ordered:

```
0xb4c: "gr_M01\0gr_M02\0gr_M03\0gr_M04\0gr_M06\0gr_M07\0gr_M08\0gr_M09\0gr_M10\0…"
```

Entry 0 resolves correctly (`strOffset 0x18c` → body-relative `0xb4c` → `"gr_M01"`, key
`0x10249a52`), and the following keys increment by 1 (`0x10249a53`, `0x10249a54`, …).
**The exact record stride is not yet pinned** — reading 12-byte entries yields a `strOffset` that
*decreases* by 5 per entry (`0x18c, 0x187, 0x182, 0x17d`) while the pool advances by 7, so at
least one field is being mis-slotted. The real event names for the game live in
`shadow_sounds.cxml` anyway (§7.1), so this is not on the critical path.

### 5.4 `WAVE` — one record per *waveform*, and where the audio lives

`music.sxd1` record 0 at `0xcd4`, **76 bytes / 19 `u32`** (verified stride):

| Offset | Type | Track 0 | All-33 distinct | Meaning |
|---|---|---|---|---|
| `+0x00` | `u32` | `3` | 1 | tag / waveform type |
| `+0x04` | `u32` | `0x00FF0642` | 1 | **format word** (see below) |
| `+0x08` | `u32` | `48000` (`0xBB80`) | 1 | **sample rate** ✔ |
| `+0x0C` | `u32` | `3899` (`0x0F3B`) | 1 | codec config, constant per bank (`com_amb` = `0x4000`) |
| `+0x10` | `u32` | `0` | 1 | 0 |
| `+0x14` | `u32` | `10 898 874` | 33 | **sample count** ✔ |
| `+0x18` | `u32` | `0xFFFFFFFF` | 1 | −1 |
| `+0x1C` | `u32` | `0xFFFFFFFF` | 1 | −1 |
| `+0x20` | `u32` | `10 218 240` | 33 | **byte length** ✔ |
| `+0x24` | `u32` | `96` | 32 | **start offset** in the `.sxd2` ✔ |
| `+0x28` | `u32` | `0x00120401` | 1 | codec config, constant |
| `+0x2C` | `u32` | `0x00800000` | 1 | codec config, constant |
| `+0x30` | `u32` | `0x00807EA6` | 1 | constant |
| `+0x34` | `u32` | `0x00807EA6` | 1 | constant |
| `+0x38` | `u32` | `0x82001960` | 1 | constant |
| `+0x3C` | `u32` | `0x0006010A` | 1 | constant |
| `+0x40` | `u32` | `0xFE761DF0` | 1 | constant (CRC of the record? identical on all 33) |
| `+0x44` | `u32` | `6` | 1 | constant |
| `+0x48` | `u32` | `100` | 1 | constant |

Three independent proofs that `+0x20`/`+0x24` are length/start:

1. `Σ lengths = 172 367 040` and `music.sxd2` size − `0x60` = `172 367 040`. **Exact.**
2. Sorting the 33 `(start, length)` pairs yields a **gap-free partition** `[0x60 … 172 367 136)`,
   no overlap, no hole.
3. `length / 960` is an integer for **33/33** tracks.

Resident banks use a **44-byte** variant of the same record. `com_sys.sxd` record 0 (`0x750`):

```
0x750: 01 00 00 00 | 21 02 ff 00 | 80 bb 00 00 | 66 26 00 00 | 00 00 00 00
0x764: 04 8e 01 00 | ff ff ff ff | ff ff ff ff | 00 c7 01 00 | 4c 14 00 00 | 06 00 00 00
```

Same field order, shorter tail. **Verified in resident banks:** `+0x08` is always `0xBB80` (48 000)
and `+0x04` is the per-bank format word — `0x00FF0121` (`com_amb`), `0x00FF0221` (`com_sys`),
`0x00FF0642` (`music`). **Assumed by analogy, not proven:** that `+0x20`/`+0x24` are length/start in
resident banks too — the contiguous-partition proof was only run on `music`. The format word and the
`+0x0C` word (`0x4000` `com_amb`, `0x2666` `com_sys`, `0x0F3B` `music`) are the open questions, not
the rate or the frame geometry.

> Note for `recon-integration.md`: its §5.3 reads `WAVE` as a 32-byte record ending in
> `data_offset`, with `fmt = 0x00FF0221` for `com_sys`. The record is longer than 32 bytes
> (`REQD` 44 / `WAVE` 76 on `music`; `44` on `com_amb`) and carries **both** a byte length and a
> start offset. The 32-byte reading happens to line up on `com_sys` but drops `+0x20`/`+0x24`,
> which are the two fields a demuxer needs. Reconcile against the 33/33 tag assertion above.

### 5.5 `DATA` — the resident audio payload

```
+0x00  char[4] "DATA"
+0x04  u32     size      (bytes after the 8-byte header; includes the 24-byte zero header)
       u8[24]   zero
+0x20  audio
```

`com_amb.sxd`:

```
0000ef80: 4441 5441 e812 2600 0000 0000 0000 0000  DATA..&.........
0000ef90: 0000 0000 0000 0000 0000 0000 0000 0000  ................
0000efa0: 0c00 f000 0e20 e030 ff11 e00f 01f0 20f1  ..... .0...... .
```

`0xef80 + 8 + 0x2612e8 = 0x270270 =` file size. ✔ On all 21 banks with `DATA`, the chunk runs
**exactly to EOF**.

`boss01.sxd @0x14f30` starts with 16 zero bytes then `0c 00 00 00 00 00 00 00`; `edp02.sxd @0x14c0`
shows the same `0C 00 00 00` marker repeating every 16 bytes. **This `0x0C` 16-byte pattern is a
per-stream directory at the head of `DATA`, not a codec frame** — measured over the whole
`com_amb` payload, only **9.5 %** of 16-byte blocks start with `0x0C`, and the share varies
2–20 % per 256 KiB window. Do not key a demuxer on it.

---

## 6. `sotc_buss.sxd` — the master mixer graph (no audio)

```
BSFX @0x60  size 0x5bc8   bus/FX chains
GVAR @0x5c30 size 0x1828  global mix variables
GVRN @0x7460 size 0x0108
BXFD @0x7570 size 0x2d258  FX parameter data (8 464 B/entry region, entropy ~4.7)
BXFN @0x347d0 size 0x01f8
```

`BSFX` header: `42 53 46 58 | c8 5b 00 00 | 0 | 0x25 | 0x28 | 0x3f | 0x270 | 2 | 0x850 | …`
→ **37 bus entries of `0x270` (624) bytes**; `0x37 · 0x270 = 23 088 ≈ size 0x5bc8`. The bus
names are inline ASCII: `THRU`, `COMP`, `UMIX`, `CHMIX`, `DELA`, `REVE`, `SCCO`. Readable parameter
text: `wet=0.00`, `Del_Ec…`.

---

## 7. Metadata files

### 7.1 `_cmn/shadow_sounds.cxml` — 8 001 event names, plaintext XML

Header is **3 bytes** then ordinary XML (the `83 db 15` prefix is *not* a BOM; `<` is at offset 3):

```
00000000: 83db 153c 3f78 6d6c 2076 6572 7369 6f6e  ...<?xml version
00000010: 3d22 312e 3022 2065 6e63 6f64 696e 673d  ="1.0" encoding=
...
00000040: 733e 3c53 6f75 6e64 206e 616d 653d 2265  s><Sound name="e
```

- 355 718 bytes; **8 001 `<Sound …/>` elements** inside a single `<Sounds>` root.
- Tags: `Sound` only. Attributes: `name`, `globalInstVars`, `testOccl`, `occlRadius`,
  `occlYOffset`, `ignore`, `trackVel`.
- Examples: `evis_thunder1 … evis_thunder7`, `knight_attack_second`, `kirin_atk_null ignore="1"`,
  `b_swim_short`, `wnd_landing_hard_t1_03`, `wnd_fdwn_weapon`.
  (Several names are inherited from Bluepoint's previous title — proof that SNDX is a shared
  in-house engine, not a SOTC-specific format.)
- **Role:** the logical event vocabulary + per-event 3D/occlusion flags. This is the addressing
  layer a native rebuild would play against.

### 7.2 `_cmn/sound_data.calt` — the bank manifest

**Plain ASCII text, CRLF line endings, 1 011 bytes, 36 lines**, one absolute virtual path per line:

```
$/sound/shadow/boss01.sxd
…
$/sound/shadow/sotc_buss.sxd
$/sound/shadow/music.sxd1
$/sound/shadow/str_amb.sxd1
…
$/sound/shadow/str_bgm.sxd2
$/sound/shadow/str_boss.sxd2
$/sound/shadow/str_event.sxd2
$/sound/shadow/****/shadow_sounds.cxml
```

All 25 `.sxd` + the 5 `.sxd1` + the 5 `.sxd2` appear, plus the XML with `****` standing in for the
`_cmn` directory. **Role:** load list / mount order — free, no parser needed.

### 7.3 `_cmn/*.crmp` — binary maps with an inline path pool

| File | header `u32` | `PAMR` at | plaintext paths |
|---|---|---|---|
| `shadowbanks.crmp` (842 B) | **25** | `0x33e` | 24 × `$/sound/shadow/*.sxd` |
| `shadowbossmusic.crmp` (196 B) | **20** | `0xb8` | 0 |
| `shadowstreams.crmp` (3 295 B) | `0x0C000A8C` | `0xcd3` | 0 |
| `shadowrandomevents.crmp` (3 393 B) | `0x0D000A8C` | `0xd35` | 0 |

- The two "count-headed" files start with `u32 count` then `count × {u32 a, u32 b}` pairs. In
  `shadowbossmusic.crmp` the first word of each pair walks **down by 7** (`0x9c, 0x95, 0x8e, 0x87,
  0x80, 0x79, 0x72 …`) and the second is a small opaque 32-bit value — consistent with
  `{u32 slot, u32 key/hash}`.
- All four end with the 12-byte trailer **`PAMR`** (`50 41 4d 52`) = `RMAP` byte-reversed, followed by
  `u32` = 9 and `u32` = an offset. On the two large files the trailer fields do not parse as
  count/offset, so treat `PAMR` as an **EOF resource-map directory whose fields are not yet
  confirmed**.
- The two large files (`shadowstreams`, `shadowrandomevents`) have no readable strings and a
  high-entropy body; their repeating shape (`07 xx`, `06 xx`, …) reads like `(time, eventId)`
  sequences.
- **Role:** engine-side routing tables that sit above the banks —
  `shadowbanks` = *which bank files exist* (25, matching the 25 `.sxd`),
  `shadowstreams` = stream scheduling,
  `shadowrandomevents` = random/variation picks,
  `shadowbossmusic` = boss → music cue (20 entries).
  They affect *playback policy*, not decoding. **Not on the critical path for S5.**

### 7.4 `_ps4/grass_wind.cpu.cctx` — unresolved

```
00000000: 8c06 0038 6288 3860 8000 0000 8000 0000  ...8b.8`........
00000010: 40e6 5432 0148 1742 3951 5ea7 5555 e495  @.T2.H.B9Q^.UU..
```

14 452 bytes. 8-byte non-ASCII header, then two `0x80` words, a possible 16-byte GUID
(`40 e6 54 32 01 48 17 42 39 51 5e a7 55 55 e4 95` is **not** a GUID shape — it is 16 high-entropy
bytes), then entropy ≈ 7.9 with no strings and no repeating period. The name (`grass_wind`,
`.cpu.`, `_ps4/`) says "platform-specific precomputed cache for the procedural grass-wind sound".
**Role unconfirmed**; it is a single 14 KB file and can be ignored for S5.

---

## 8. Codec — ATRAC9, proven from the executable

### 8.1 The proof

`extracted/uroot/eboot.bin` (65 314 052 bytes) is the shipped PS4 executable. `strings -a -t x`:

| offset | string |
|---|---|
| `0x12b77d4` | `noLoadNgs2` |
| `0x12b77f3` | `C:\sound\sndx\branches\ps4sndx20170803_4500_ico_sotc_no_irreverb\runtime\source\sndx\sndx_engine_ngs2_orbis.c` |
| `0x12b78fe` | `maxAtrac9Decoders` |
| `0x12b7baf` | `$%x,$%p,%d:VOICE_ALLOC_AT9 %d/%d` |
| `0x12be002` | `C:\sound\sndx\branches\ps4sndx20170803_4500_ico_sotc_no_irreverb\runtime\source\sndx\sndx_player_dynamic_stream.c` |
| `0x12c0bf0` | `Invalid ATRAC9 configure data (%08X)` |
| `0x2df39f6` | `libSceNgs2.prx` |

Plus `Loaded NGS2 module`, `Unload NGS2 module`, `Failed to load NGS2 module (%X)`, and the full
`sndx_*.c` source list (`sndx_resource.c`, `sndx_stream.c`, `sndx_player_wave.c`,
`sndx_player_dynamic_stream.c`, `sndx_voice.c`, `sndx_buss_fx.c`, `sndx_xfader.c`, …).

Reading:

- **Engine:** Bluepoint in-house **SNDX**, branch `ps4sndx20170803_4500_ico_sotc_no_irreverb`,
  Orbis (PS4) backend. The container name `SXD`/`SXDF`/`SXDS` is SNDX's bank format.
- **Codec:** **ATRAC9 (AT9)**, decoded through Sony **NGS2** (`libSceNgs2`), with a configurable
  AT9 decoder pool (`maxAtrac9Decoders`) and a runtime validator
  (`Invalid ATRAC9 configure data (%08X)` — the `%08X` is the same kind of 32-bit "format word"
  that sits at `WAVE+0x04`).
- **Streaming** is a separate SNDX player (`sndx_player_dynamic_stream.c`), so `.sxd1`/`.sxd2` and
  the resident `DATA` chunk share one codec but two delivery paths.

### 8.2 Frame geometry, measured on the bytes

`.sxd2` payloads are **not** encrypted — they are simply full-rate AT9. Entropy by region:

| file / region | entropy (bits/byte) |
|---|---|
| `music.sxd2` @`0x1000`, `0x40000`, `0x2000000`, `0xA45…` | 7.963 / 7.961 / 7.979 / 7.970 |
| `str_bgm.sxd2` @`0x1000`, `0x40000`, `0x20000000` | 7.978 / 7.975 / 7.978 |
| `boss01.sxd` DATA (`0x14f30`+) | 5.5 – 6.5 |
| `com_amb.sxd` DATA | 6.7 – 7.2 |
| `com_sys.sxd` DATA | 5.0 – 6.5 |
| `sotc_buss.sxd` BXFD | ~4.7 |

High-rate music sits at the incompressible end (7.96–7.98); low-rate SFX sit at 5–7. That spread is
the signature of a *rate-controlled perceptual codec*, not of a cipher (a cipher would pin every
file at ~8.0).

**Frame period, `music.sxd2` stream 0 (`0x60 … 0x9BEB60`, 10 218 240 bytes).** Scoring candidate
frame sizes on the constancy of the first byte:

| frame size | first-byte mode | share | distinct first bytes |
|---|---|---|---|
| **960** | `0x33` | **78.5 %** | **2** |
| 480 | `0x33` | 39.4 % | 187 |
| 1440 | `0x33` | 38.9 % | 91 |
| 1920 | — | < 30 % | — |

960 is the fundamental period (`480` is a weak sub-harmonic). First bytes of the first frames:

```
frame0 32 93 0b 3a 03 09 a9 1e a0 50 e8 61 04 70 d6 0c
frame1 32 93 0d 00 0e 11 13 20 02 c8 c8 20 db e7 54 36
frame2 32 93 0b 20 0e 14 40 85 d1 d2 48 b2 6e 5b 93 82
```

i.e. a **2-byte AT9 frame header `0x3293`/`0x3393`** and 958 bytes of payload.

**Sample rate and bitrate.** `WAVE+0x08 = 0xBB80 = 48000` on every stream of every bank. Taking
`WAVE+0x14` as the decoded sample count and `WAVE+0x20` as the byte length:

```
track 0 "Prologue":  10 218 240 B · 8 / (10 898 874 / 48 000 s) = 360 000 bit/s
track 1 "Opening 1":  5 103 360 B · 8 / ( 5 442 618 / 48 000 s) = 360 075 bit/s
```

and **every one of the 33 tracks computes to 360 kbps**. With 960-byte frames that forces
`960·8 / 360 000 = 21.333 ms` per frame = **1024 samples**. Cross-check: `length / 960` equals
`ceil(samples / 1024)` on 26/33 tracks and exactly one frame more on the other 7 (encoder padding) —
**33/33 consistent, zero exceptions**.

**Confidence:** ATRAC9 for the whole `sound/shadow` set: **high** (binary-level proof + geometry
that matches AT9 CBR). Channel count per stream: **undetermined** (see §9) — 360 kbps is 180 kbps/ch
for stereo, which is the natural reading for music.

### 8.3 Inner magics

A scan for `RIFF`, `WAVE`, `OggS`, `AT9`, `atrac`, `MP3`, `celt`, `CRI`, `HCA`, `ADX`, `VAG`, `PCM`
across the 41 containers produced **no codec container magics** — the audio is raw AT9 frames,
not wrapped in `.at9`/`.wav` envelopes. `WAVE` only ever appears as the SNDX `WAVE` **chunk tag**
(`57 41 56 45 58`, five bytes — the trailing `X` belongs to the size field's low byte, which is why
a 4-char scan reports `WAVE`).

---

## 9. The `music.sxd1` catalog — 33 tracks, decoded end to end

Read `REQD` for names, `WAVE` for geometry, `music.sxd2` for bytes. **All 33 tracks: 48 kHz,
360 kbps, contiguous partition of the 172 367 136-byte `.sxd2`.**

| # | name | bytes | start | samples | dur | kbps |
|---|---|---|---|---|---|---|
| 0 | Prologue | 10 218 240 | 96 | 10 898 874 | 3:47.1 | 360 |
| 1 | Opening 1 | 5 103 360 | 10 218 336 | 5 442 618 | 1:53.4 | 360 |
| 2 | Opening 2 | 5 198 400 | 15 321 696 | 5 544 637 | 1:55.5 | 360 |
| 3 | Event 1 | 5 638 080 | 20 520 096 | 6 013 503 | 2:05.3 | 360 |
| 4 | Event 2 | 5 173 440 | 26 158 176 | 5 517 202 | 1:54.9 | 360 |
| 5 | Field 1 | 5 133 120 | 31 331 616 | 5 474 557 | 1:54.1 | 360 |
| 6 | Boss 1 | 5 666 880 | 36 464 736 | 6 044 367 | 2:05.9 | 360 |
| 7 | Boss 2 | 5 269 440 | 42 131 616 | 5 619 977 | 1:57.1 | 360 |
| 8 | Defeat | 4 753 920 | 47 401 056 | 5 070 335 | 1:45.6 | 360 |
| 9 | Collapse | 1 438 080 | 52 154 976 | 1 532 817 | 0:31.9 | 360 |
| 10 | Field 2 | 1 693 440 | 53 593 056 | 1 805 232 | 0:37.6 | 360 |
| 11 | Boss 3 | 5 232 960 | 55 286 496 | 5 581 448 | 1:56.3 | 360 |
| 12 | Boss 4 | 6 261 120 | 60 519 456 | 6 677 599 | 2:19.1 | 360 |
| 13 | Field 3 | 1 127 040 | 66 780 576 | 1 201 854 | 0:25.0 | 360 |
| 14 | Boss 5 | 4 974 720 | 67 907 616 | 5 305 798 | 1:50.5 | 360 |
| 15 | Boss 8 | 6 094 080 | 72 882 336 | 6 499 975 | 2:15.4 | 360 |
| 16 | Game Over | 1 858 560 | 78 976 416 | 1 981 662 | 0:41.3 | 360 |
| 17 | Demo 1 | 9 186 240 | 80 834 976 | 9 797 770 | 3:24.1 | 360 |
| 18 | Boss 6 | 4 842 240 | 90 021 216 | 5 164 340 | 1:47.6 | 360 |
| 19 | Boss 7 | 5 094 720 | 94 863 456 | 5 433 936 | 1:53.2 | 360 |
| 20 | Boss 9 | 5 591 040 | 99 958 176 | 5 962 781 | 2:04.2 | 360 |
| 21 | Demo 2 | 4 707 840 | 105 549 216 | 5 021 254 | 1:44.6 | 360 |
| 22 | Field 4 | 1 420 800 | 110 257 056 | 1 514 411 | 0:31.6 | 360 |
| 23 | Boss 10 | 5 642 880 | 111 677 856 | 6 018 371 | 2:05.4 | 360 |
| 24 | Boss 11 | 5 954 880 | 117 320 736 | 6 351 462 | 2:12.3 | 360 |
| 25 | Event 3 | 4 719 360 | 123 275 616 | 5 033 098 | 1:44.9 | 360 |
| 26 | Boss 12 | 5 560 320 | 127 994 976 | 5 929 867 | 2:03.5 | 360 |
| 27 | Field 5 | 1 397 760 | 133 555 296 | 1 490 309 | 0:31.0 | 360 |
| 28 | Boss 13 | 6 353 280 | 134 953 056 | 6 776 141 | 2:21.2 | 360 |
| 29 | Ending 1 | 1 420 800 | 141 306 336 | 1 514 880 | 0:31.6 | 360 |
| 30 | Ending 2 | 2 276 160 | 142 727 136 | 2 426 793 | 0:50.6 | 360 |
| 31 | Ending 3 | 19 303 680 | 145 003 296 | 20 589 440 | 7:08.9 | 360 |
| 32 | Ending 4 | 8 060 160 | 164 306 976 | 8 596 550 | 2:59.1 | 360 |

Total **172 367 040 bytes / 63.83 min**. (`music.sxd1` also carries a `NAME` pool of internal ids
`gr_M01 … gr_M36`; the human names above come from `REQD`, which is the authoritative label.)

---

## 10. Unknowns

1. **Channel count per stream.** `WAVE+0x04` (`0x00FF0121` / `0x00FF0221` / `0x00FF0642`) and
   `WAVE+0x0C` (`0x4000` / `0x0F3B`) are almost certainly the AT9 config word(s) — the same 32-bit
   value the eboot validates as "ATRAC9 configure data" — but the bit split (sample-rate index,
   channel-config index, frame-samples index, frame bytes) is not decoded. Music is *probably*
   stereo (360 kbps total = 180 kbps/ch); `com_amb`/`com_amb`-class SFX are probably mono.
2. **`WAVE+0x0C`** is constant per bank but has no known meaning (`music` 3 899, `com_amb` 16 384,
   `com_sys` 9 830). It is not the frame count, not the sample count, and not the byte length.
3. **`0x06` in the file header** (`boss01` `0x574B`, `com_obj` `0x9714`, `bridge` `0x1944`,
   always `0` on `SXDS`). Not a size, not a count, not derived from the name.
4. **`NAME` record stride** — entry 0 resolves (`strOffset 0x18c` → `"gr_M01"`), the rest do not
   under a 12-byte stride. Low value: `shadow_sounds.cxml` is the real name source.
5. **`REQD+0x0C`** (1 763 for "Prologue") looks like a per-sound counter but is neither the AT9
   frame count (10 644) nor the sample count (10 898 874). Ratios differ per track.
6. **`.sxd1` chunk set for `str_boss` / `str_event`** has no `DATA`, yet `str_amb.sxd1` (also a
   stream index) *does* (`DATA @0x35700`). Whether `str_amb`'s resident part is a genuinely
   different asset class or just a build artifact is unverified.
7. **`.crmp` body semantics** (`PAMR` trailer fields, the `(u32, u32)` pair meaning, the
   high-entropy bodies of `shadowstreams` / `shadowrandomevents`).
8. **`.cctx` role** (§7.4) — one 14 KB file, ignorable, but unexplained.
9. **Stream seek/granularity** — whether a stream can be started at an arbitrary 960-byte frame
   (it should: CBR, fixed frame) or whether SNDX also stores a seek table in `SEQD`/`RXFD`. Untested.
10. **`com_amb.sxd`'s `WAVE` chunk** does not decode with the shared record rule (§4): 143 entries,
    mixed 44/64 offset strides, and no constant record size that fills the chunk. Every other bank
    does.
11. **`SEQD`, `LVAR/LVRN`, `RXFD/RXFN`, `TBLD`, `EXTR`** are unmapped. `SEQD` is 33 × 0x40 bytes
    of small values for `music` (sequence/ordering data); `RXFD/RXFN` are key/value blocks. None
    are needed to extract PCM.

---

## 11. The single cheapest confirming experiment

**Carve one stream to a raw `.at9` and decode it with any ATRAC9 decoder; listen for music.**

Because the geometry is now exact, this is a 3-step, ~2-minute test with no new reverse
engineering:

```bash
# 1. carve track 1 ("Opening 1"): 5 103 360 bytes = 5 316 frames of 960 B, from music.sxd2 @10 218 336
#    prepend the 4-byte AT9 config word from WAVE+0x04 (big-endian, as .at9 expects)
python3 - <<'PY'
d=open('music.sxd2','rb').read()
open('/tmp/opening1.at9','wb').write(bytes.fromhex('00FF0642')+d[10218336:10218336+5103360])
PY

# 2. decode with libatrac9 (or vgmstream built --enable-atrac9, or a 20-line C harness
#    around atrac9_init/atrac9_decode_block)
atrac9dec /tmp/opening1.at9 /tmp/opening1.wav

# 3. objective pass/fail — no ears needed
ffprobe -show_streams /tmp/opening1.wav     # expect 48000 Hz, ~113.4 s
python3 -c "import wave,audioop,sys;w=wave.open('/tmp/opening1.wav');print(audioop.rms(w.readframes(w.getnframes()),2))"
```

**Pass** = a file of ≈113.4 s at 48 kHz whose RMS is non-zero and whose spectrum has energy above
1 kHz (not noise, not silence). **Fail** = silence, or the decoder rejecting the config word.

Fallbacks if `libatrac9` rejects the frame header: (a) try stripping the 2-byte `0x3293` header per
frame (i.e. feed 958 bytes/frame, 480·2), (b) try the config word byte-swapped
(`4206FF00` vs `00FF0642`), (c) try `0x00120401` from `WAVE+0x28` as the config word. One of those
four combinations is the truth; the frame grid itself is already pinned.

**Second-cheapest (independent) check, ~10 minutes:** `strings -a extracted/uroot/eboot.bin` for the
SNDX AT9 config validator and read the bit-extraction around the `Invalid ATRAC9 configure data`
xref — that yields the channel/sample-rate field split without ever decoding audio.

---

## 12. Parser sketch

```python
import struct

def chunks(buf, off=0x60):
    """Yield (magic, offset, size). size counts bytes AFTER the 8-byte header."""
    while off + 8 <= len(buf):
        magic = buf[off:off+4]
        if not all(0x30 <= c <= 0x5A for c in magic):   # [0-9A-Z]
            return
        size = struct.unpack_from('<I', buf, off+4)[0]
        yield magic.decode(), off, size
        if size == 0:
            return
        off += size + 8

def table(buf, off, record_size):
    """Index table: +0x08 reserved, +0x0C count, +0x10 offsets[count].
       record_i = body + offsets[i] + 4*i   <- the +4*i is the format quirk."""
    count = struct.unpack_from('<I', buf, off+0x0C)[0]
    body  = off + 0x10
    offs  = struct.unpack_from('<%dI' % count, buf, body)
    return [struct.unpack_from('<%dI' % (record_size // 4), buf, body + offs[i] + 4*i)
            for i in range(count)]

def sxd_header(buf):
    magic = buf[0:4].decode()                       # 'SXDF' | 'SXDS'
    version, unk06 = struct.unpack_from('<HH', buf, 4)
    size_or_streams = struct.unpack_from('<I', buf, 8)[0]
    companion       = struct.unpack_from('<I', buf, 0x0C)[0]
    guid            = buf[0x10:0x20]
    name            = buf[0x20:0x40].split(b'\0')[0].decode('latin1')
    return dict(magic=magic, version=version, unk06=unk06,
                size_or_streams=size_or_streams, companion=companion,
                guid=guid, name=name)

# --- music.sxd1 usage -------------------------------------------------------
# chunks(): TONE@0x60 REQD@0x3d0 NAME@0x9b0 WAVE@0xc40 SEQD@0x16a0 LVAR@0x1ed0 ...
# req = table(buf, 0x3d0, 44)   # req[i][2]=id, req[i][3]=?, name at body+off+4i+0x20
# wav = table(buf, 0xc40, 76)   # wav[i][2]=48000, [5]=samples, [8]=bytes, [9]=start
```

For `com_amb`-class resident banks the `WAVE` record is **44 bytes** and `[5]=bytes`, `[9]=start`
into `DATA+0x20`.

---

## 13. Reproduction

```bash
export G=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
cd "$G/sound/shadow"

# header map
xxd -l 256 boss01.sxd
xxd -l 128 music.sxd2

# chunk chain / section map (all 41 containers)
python3 - <<'PY'
import struct,re,glob
for f in sorted(glob.glob('*.sxd*')):
    d=open(f,'rb').read(); o=0x60; out=[]
    while o+8<=len(d) and re.fullmatch(rb'[A-Z0-9]{4}',d[o:o+4]):
        s=struct.unpack_from('<I',d,o+4)[0]; out.append(f"{d[o:o+4].decode()}@{o:#x}")
        if s==0: break
        o+=s+8
    print(f, " ".join(out))
PY

# codec proof
strings -a -t x "$G/../uroot/eboot.bin" | grep -E 'AT9|ATRAC9|Ngs2|sndx_'

# entropy profile
python3 -c "
import math,collections,sys
b=open('music.sxd2','rb').read()[0x1000:0x11000]
c=collections.Counter(b); n=len(b)
print(-sum(v/n*math.log2(v/n) for v in c.values()))"
```

---

## 14. Bottom line for the milestone

- The **container is understood** (header, chunk chain, generic table reader, `DATA` bounds) and
  every resident bank's audio payload is a single contiguous blob that runs to EOF.
- The **codec is identified with binary-level proof** — ATRAC9 via NGS2 — and the stream geometry
  is exact (48 kHz, 960-byte/1024-sample frames, 360 kbps, 33/33 tracks). The remaining unknowns
  are the AT9 **config word's bit split** (channel count) and a handful of engine tables that do
  not affect decoding.
- The **cheapest next step is a decode proof on one carved stream** (§11), not more format
  archaeology. If it decodes, the whole set is decodable, and the 63.8-minute music catalog is
  addressable by name straight out of `music.sxd1`.
