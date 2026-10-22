### Task 3: Converter — decode, WAV, manifest, `audio` verb

**Files:**
- Create: `src/BpConvert.Core/Audio/AudioCodec.cs`, `WavWriter.cs`, `AudioManifest.cs`
- Modify: `src/BpConvert.Cli/Program.cs` (add `case "audio"`)
- Test: `tests/BpConvert.Tests/AudioTests.cs`

**Interfaces:**
- Consumes: `SxdContainer`/`SxdTables` (Task 2), the gate's layout/config (Task 0), the curated map (Task 1).
- Produces:
```csharp
public static class AudioCodec {
    public static float[] Decode(ReadOnlySpan<byte> frames, uint formatWord,
                                 uint sampleRate, out int channels);
}
public static class WavWriter {
    public static void Write(string path, ReadOnlySpan<float> samples, int channels, int rate);
}
public sealed record AudioEntry(string Bank, string Name, string File, int Rate, int Channels,
                                long Frames, double Seconds, bool Loop, string Source);
public static class AudioManifest {
    public static string Write(string path, IReadOnlyList<AudioEntry> entries);
    public static IReadOnlyList<AudioEntry> Read(string path);
}
```

- [ ] **Step 1: Write the failing tests**

```csharp
[Fact] public void Wav_RoundTrips_48kStereo()
// Write 480 samples L/R, read the header: RIFF/WAVE, fmt 1, 2 ch, 48000, data size 1920.

[Fact] public void Manifest_RoundTrips_StableSorted()
// Two entries out of order -> Read returns bank/name sorted; every field preserved.

[Fact] public void Decode_CuratedClip_Is48kNonSilent()
// Carve a mapped boss01 wave (Task 1); decode; Assert rate 48000, frames > 0,
// RMS > 1e-3, peak <= 1.0.

[Fact] public void Manifest_DuplicateBankName_Throws()
```

- [ ] **Step 2: Run to verify red**

Run: `SOTC_GAME_ROOT=... dotnet test tests/BpConvert.Tests --filter AudioTests -v q`
Expected: FAIL (types/methods missing).

- [ ] **Step 3: Implement**

`AudioCodec` wraps the Task 0 decoder (VGAudio/FFmpeg) behind one method; throws
`InvalidDataException` on a rejected config or a non-finite sample. `WavWriter` writes float32
RIFF (`fmt` tag 3) or PCM16 — pick one and pin it in the test. `AudioManifest` writes one
`key=value` line per entry, sorted by `(bank, name)`, with a `version=1` first line; `Read` is the
app's only contract.
`AudioCommand`: parse `--root`, `--out`, `--banks`, `--events`, `--all`, `--force`; for each
requested event, read the bank, carve, decode, write
`out/library/audio/<bank>/<name>.wav`, append the entry; write the manifest; a missing event is a
non-zero exit naming it.

- [ ] **Step 4: Run the tests and the verb**

```bash
SOTC_GAME_ROOT=... dotnet test tests/BpConvert.Tests --filter AudioTests -v q
dotnet publish src/BpConvert.Cli -c Release -o out/bin
dotnet exec out/bin/bp-convert.dll audio --root "$SOTC_GAME_ROOT" --out out/library \
  --banks boss01,com_obj,com_v --events <the curated names from Task 1>
```
Expected: PASS; the manifest and WAVs exist; `ffprobe` shows 48 kHz and the gate's durations.

- [ ] **Step 5: Commit**

```bash
git add src/BpConvert.Core/Audio src/BpConvert.Cli/Program.cs tests/BpConvert.Tests/AudioTests.cs
git commit -m "feat(convert): decode the S5 audio slice"
```

---

