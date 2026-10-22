### Task 2: Converter — SXDF container and tables

**Files:**
- Create: `src/BpConvert.Core/Audio/SxdContainer.cs`
- Create: `src/BpConvert.Core/Audio/SxdTables.cs`
- Test: `tests/BpConvert.Tests/AudioTests.cs`

**Interfaces:**
- Consumes: `SOTC_GAME_ROOT` (`sound/shadow/*.sxd`).
- Produces:
```csharp
public sealed record SxdChunk(string Magic, long Offset, int Size);
public sealed record SxdBank(string Path, ushort Version, string HashHex, string Name,
                             IReadOnlyList<SxdChunk> Chunks, long DataOffset, int DataSize);
public static SxdBank SxdContainer.Read(string path);

public sealed record WaveRecord(int Index, uint FormatWord, uint SampleRate, uint Frames,
                                long Start, int ByteLength);
public sealed record NameRecord(int Index, string Name);
public static IReadOnlyList<WaveRecord> SxdTables.Waves(SxdBank bank);
public static IReadOnlyList<NameRecord> SxdTables.Names(SxdBank bank);
```

- [ ] **Step 1: Write the failing tests**

`AudioTests.cs`:
```csharp
[Fact] public void Read_Boss01_HasTheChunkChainAndData()
// Assert: name "boss01", TONE at 0x60, REQD/NAME/WAVE present, DATA at 0x14f10,
// DataOffset+DataSize == file length.

[Fact] public void Read_MusicSxd1_HasNoData_AndMatchesItsSxd2Hash()
// Assert: no DATA chunk; hash == music.sxd2's hash.

[Fact] public void Waves_ComSys_PartitionTheDataPayload()
// Assert: sum(ByteLength) == DataSize (or DataSize - the documented 24-byte head),
// every Start >= 0 and Start+ByteLength <= DataSize.

[Fact] public void Names_Boss01_AreReadableAndUnique()
// Assert: 343 names, all ASCII, no duplicates, first contains "boss01".

[Fact] public void Read_Truncated_Throws()
// Truncate a copy to 0x80; Assert.Throws<InvalidDataException>.

[Fact] public void Read_Bridge_AcceptsToneOnly()
// Assert: one chunk, no DATA, no throw.
```
Use `SOTC_GAME_ROOT`; follow the house pattern for a missing root (fail fast with the path).

- [ ] **Step 2: Run to verify red**

Run: `SOTC_GAME_ROOT=... dotnet test tests/BpConvert.Tests --filter AudioTests -v q`
Expected: FAIL (types missing).

- [ ] **Step 3: Implement the reader**

`SxdContainer.Read`: header 0x00-0x20 (magic, version, size, hash, name), chunks from 0x60
(`magic` + `size` counts the body), `DATA` recorded with offset/size; bounds-check every chunk and
throw `InvalidDataException` on overlap/truncation. `SxdTables`: the shared
`reserved/count/offsets[]` table with `record_i = body + offsets[i] + 4*i`; drive records from the
table, never a fixed stride; `WaveRecord` slots come from **Task 0's per-bank layout** (do not
assume 44/76 or a constant config). `Names` reads the 12-byte entries' string offset when that
stride resolves (music); for resident banks whose stride does not (the builder check), read the
NUL-terminated string pool and pair by index, as documented by Task 1.

- [ ] **Step 4: Run to verify green**

Run: `SOTC_GAME_ROOT=... dotnet test tests/BpConvert.Tests --filter AudioTests -v q`
Expected: PASS, 6/6.

- [ ] **Step 5: Commit**

```bash
git add src/BpConvert.Core/Audio/SxdContainer.cs src/BpConvert.Core/Audio/SxdTables.cs tests/BpConvert.Tests/AudioTests.cs
git commit -m "feat(convert): read SXDF banks and tables"
```

---

