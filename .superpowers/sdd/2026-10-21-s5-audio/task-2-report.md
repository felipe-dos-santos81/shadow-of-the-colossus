# Task 2 report — converter SXDF container and tables

Status: **DONE** (2026-10-21). Commit `c46e009` — `feat(convert): read SXDF banks and tables`.
The gate's approved layout is implemented: the variable-stride `offsets[]` table, absolute music
starts, and the loader formula `record + 0x24 + start` for resident banks.

## What was built

`src/BpConvert.Core/Audio/SxdContainer.cs`
- `SxdChunk(string Magic, long Offset, int Size)` — `Size` is the body after the 8-byte header.
- `SxdBank(...)` — the requested signature (`Path`, `Version`, `HashHex`, `Name`, `Chunks`,
  `DataOffset`, `DataSize`, `HasData`, `CompanionSize`).
- `SxdContainer.Read`: header `0x00` magic `SXDF`/`SXDS`, `0x04` version, `0x08` own size,
  `0x0C` companion/payload size, `0x10` 16-byte hash (lowercase hex), `0x20` 32-byte name.
  `SXDF` chunks walk from `0x60` (`char[4]` tag in the gate's `0x30..0x5A` range + `u32` body),
  must land exactly on EOF, and every chunk is bounds-checked; the `+0x08` size field must equal
  the file length. Truncation, an overrun, a bad tag, a duplicate tag, or an empty chain throws
  `InvalidDataException` prefixed with the path. `DATA` sets `DataOffset` to its chunk header and
  `DataSize` to its body size (the payload is `DataOffset+0x10`, the gate's `DATA+0x10`).
  `SXDS` has no chunk chain: `Chunks` is empty, `DataOffset = 0x60` is the payload itself and
  `DataSize` is the `+0x0C` payload size, which must fit the file.

`src/BpConvert.Core/Audio/SxdTables.cs`
- `WaveRecord(Index, RecordOffset, RecordSize, FormatWord, SampleRate, Frames, Start, ByteLength)`
  and `NameRecord(Index, Name)`.
- `Waves`: generic table `+0x0C count`, `+0x10 offsets[count]`,
  `record_i = chunk + 0x10 + offsets[i] + 4*i`; `RecordSize` is the next record minus this one (or
  the chunk end). Records must be inside the chunk, at least the 0x28-byte prefix long, and
  strictly increasing (an overlap throws). Field slots are read per record:
  `+0x04` fmt, `+0x08` rate, `+0x14` frames, `+0x20` bytes, `+0x24` start. No stride or format
  list is assumed anywhere.
- `Names`: the 12-byte `NAME` entries `{u32 string_offset, u32 hash, u32 reqd_id}` with
  `name = entry + string_offset` (the offset is relative to the entry, not the chunk — this is
  what the audition gate's `lookup_name_id` does). The table wins when every entry resolves and
  every ASCII name matches the engine hash `u = byte + u*0x83` (`FUN_00987a80`); otherwise the
  NUL-terminated pool after the entry array is read in order and paired by index. Names are
  decoded Latin-1, and a bank fails if any name is empty, has a C0/DEL byte, or repeats.

`tests/BpConvert.Tests/AudioTests.cs` — the eight requested tests, env-gated through
`TestAssets` (fails fast without `SOTC_GAME_ROOT`).

## RED / GREEN

- RED: `dotnet test tests/BpConvert.Tests --filter AudioTests -v q` →
  `error CS0234: BpConvert.Core.Audio does not exist` (types missing), as the brief expected.
- GREEN: 8/8 passed (77 ms). Full suite 127/127 (50 s). `dotnet build BpConvert.sln`: 0 errors,
  0 warnings.

```
SOTC_GAME_ROOT=… dotnet test tests/BpConvert.Tests --filter AudioTests -v q
Aprovado! – Com falha: 0, Aprovado: 8, Ignorado: 0, Total: 8
```

## Tiling numbers (loader formula `RecordOffset + 0x24 + Start`, length `ByteLength`)

Measured by a throwaway console sweep over all 41 containers (deleted, not committed); every
`DATA` payload below is `[DATA+0x10, DATA+8+size)`.

| bank | waves | strides (B) | payload range | payload B | merged union |
|---|---|---|---|---|---|
| `boss01` (test 3) | 138 | 48/56/68/76 | `0x14f20..0xf3a5e0` | 15 881 920 | **1 segment, EXACT** |
| `com_sys` (test 5) | 16 | 48/56/68/76/148 | `0x1bc0..0x1062e0` | 1 066 784 | **1 segment, EXACT** (Σ bytes = payload, no duplicates) |
| `com_obj` | 241 | 48/56/68/76/80/264 | `0x1c340..0x726e00` | 7 383 744 | **1 segment, EXACT** |
| `com_v` | 63 | 48/68/80 | `0xb710..0x98c60` | 578 896 | **1 segment, EXACT** |
| `com_b_h` | 1161 | 48/68/72/160 | `0xc1ed0..0xf55530` | 15 283 808 | **1 segment, EXACT** |
| 13 other `bossNN` + `com_amb` + `edp02` | — | 48…88 | — | — | **1 segment, EXACT** |
| `edp01` | 164 | 48/56/60/68 | — | — | 11 segments (known, recon §7.1) |
| `boss24` | 137 | 48/56/60/68/76 | — | — | 27 segments (**new**: does not tile, not curated) |

`boss01` duplicate ranges: the 138 records collapse to 129 distinct `(payload, length)` pairs;
those sum to exactly 15 881 920 B (test 3 asserts both the merged union and the distinct sum).

Music (`music.sxd1`, test 4): 33 records, every `RecordSize == 76`, `start[0] = 0x60`, 32/32
consecutive pairs gap-free, every `ByteLength % 960 == 0`, last end `0xa461d20` = the `.sxd2`
file size = `CompanionSize`; `HashHex` is identical in the pair.

Names (test 6): `boss01` 343 entries, 343 ASCII, 343 unique, first
`boss01_atk_hit_ground1` (REQD 303). The sweep resolved the 12-byte table on all 30 `NAME`
banks, including `com_obj` (350) and `com_b_h` (2221), whose Japanese placeholder names
(`\x83…\x2e\x32\x32\x30\x34\x36.vag_13`) are the only non-ASCII entries; their stored hash is
not the ASCII engine hash, so the reader accepts them without the hash check but still requires
printable, unique names.

## Files

- `src/BpConvert.Core/Audio/SxdContainer.cs` (new)
- `src/BpConvert.Core/Audio/SxdTables.cs` (new)
- `tests/BpConvert.Tests/AudioTests.cs` (new)
- Commit `c46e009`; no other files touched, no assets added (`.superpowers/` is gitignored).

## Concerns

1. **`DataOffset` semantics vs the brief's test-1 arithmetic.** The brief's test 1 says
   `DataOffset + DataSize == file length`; test 3 says the payload is
   `[DataOffset+0x10, DataOffset+8+DataSize)`. Those are 8 bytes apart. I kept the gate's
   meaning (`DataOffset` = the `DATA` chunk header, `DataSize` = the body; payload at
   `+0x10`), so test 1 asserts `DataOffset + 8 + DataSize == length` (measured: 85 776 +
   0xf256c8 + 8 = 15 967 712). Any consumer wanting the brief's old equality would be off by
   the chunk header.
2. **`Waves`/`Names` re-read the whole file per call** (the DTO carries only `Path`). On a
   16 MB bank that is two reads if Task 3 wants both tables; call each once per bank.
3. **`Waves` does not validate the payload against `DATA`.** Where a payload lands depends on
   the bank kind (`Start` is absolute for music, record-relative for residents), so the check
   belongs in the carver; the sweep proves the formula is in bounds on every tiled bank.
4. **`boss24` does not tile** (27 segments, like `edp01`); it is not in the curated set, but a
   future `--banks` sweep must not assume every resident bank tiles. The streaming indexes
   (`str_amb/str_bgm/str_event.sxd1`) also carry non-tiling streamed records by design; they
   parse, only `music.sxd1` is consumed as an index.
5. **Non-ASCII names are Latin-1, not Shift-JIS.** The engine hash is only verified for ASCII
   names; the Japanese placeholders stay byte-stable and unique but are not human-readable.
   `boss01` (and the other curated banks) are pure ASCII where it matters.

---

## Review fix (2026-10-21, commit `6962191`)

`fix(convert): expose the NAME REQD key and pin the WAVE fields`

### Important — `NameRecord.Index` is not the REQD id

- `NameRecord` is now `NameRecord(int Index, string Name, uint ReqdId)`; `ReqdId` is the third u32
  of the 12-byte entry (`entry + 8`). The doc now says explicitly that `Index` is the NAME entry
  index — not the REQD key and not a WAVE index — and that `ReqdId` is the REQD key
  (`boss01_atk_hit_ground1`: entry 0, REQD 303, wave 26). Additive change; Task 3 uses `ReqdId`
  for the chain.
- The pool fallback reads the id from the same entry array (the pool keeps the entry order), so
  `ReqdId` is populated on both paths.
- `Names_Boss01_AreReadableAndUnique` now asserts `Index == 0` **and** `ReqdId == 303`.

### Minors folded in

- WAVE field slots are pinned: `SampleRate == 48000` on every `boss01` and music record, and
  `boss01`'s four format words `0x00FF0121 / 0x00FF0142 / 0x00FF0221 / 0x00FF0242` as a set.
- `Waves_ComObj_PartitionTheDataPayload` (new): 241 records, merged union exactly
  `[DATA+0x10, DATA+8+size)` = `0x1c340..0x726e00` (7 383 744 B). It covers the 80/264-byte
  tail records, so a curated bank beyond `boss01`/`com_sys` now has the formula pinned.
- `Names_FallBackToThePool_WhenTheEntryStrideDoesNotResolve` (new): no shipped bank needs the
  fallback (the 12-byte table resolves on all 30 `NAME` chunks, including the Shift-JIS
  placeholders), so the test builds a minimal synthetic SXDF whose entries carry a zero string
  offset and asserts the pool order and the REQD ids survive the fallback.
- `RecordSize` doc: "48/56/68/76 on `boss01` (com_obj 80/264, com_v 80, com_b_h 160); 76 on
  music" instead of the too-broad "48/56/68/76 on the resident banks".
- Comments corrected: the tag rule is the gate's `0x30..0x5A` range (it admits the punctuation
  between `9` and `A`), and the name check rejects C0/DEL only — C1 bytes pass on purpose because
  they are the Shift-JIS lead bytes.
- `Read_ChunkBodyOverrun_Throws` (new): truncates a copy to `0x80` **and** patches `+0x08` to the
  truncated length, so the own-size guard passes and the chunk-bounds guard
  (`SxdContainer.cs:140-149`) throws "overruns the file".
- `SXDS` now requires exact EOF (`0x60 + payload size == file length`), same as `SXDF`; all five
  `.sxd2` files satisfy it (music 0xa461d20, str_amb 0x16652de0, str_bgm 0x1ea4ee20, str_boss
  0x11124fc0, str_event 0x15d5ad50).
- `DataOffset == 0` when `HasData` is false is documented on `SxdBank` and asserted in
  `Read_Bridge_AcceptsToneOnly`.

Untouched by design (per review): the per-call file re-read, and the `edp01` / `boss24` /
`com_b_h` non-tiling notes (not curated).

### Results after the fix

```
SOTC_GAME_ROOT=… dotnet test tests/BpConvert.Tests --filter AudioTests -v q
Aprovado! – Com falha: 0, Aprovado: 11, Ignorado: 0, Total: 11   (139 ms)
dotnet test tests/BpConvert.Tests        -> 130/130 passed (49 s)
dotnet build BpConvert.sln               -> 0 errors, 0 warnings
```

Tests: 8 original + `Waves_ComObj_PartitionTheDataPayload` +
`Names_FallBackToThePool_WhenTheEntryStrideDoesNotResolve` + `Read_ChunkBodyOverrun_Throws`.

