### Task 4: App — the audio engine

**Files:**
- Create: `app/src/audio.h`, `app/src/audio.cpp`, `app/src/wav_io.h`, `app/src/wav_io.cpp`
- Test: `app/tests/test_audio.cpp`
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Consumes: `out/library/audio/audio.manifest` + WAVs (Task 3); SDL3 audio.
- Produces:
```cpp
enum class AudioCategory { Music, Ambience, Sfx };
struct AudioClip { std::vector<float> samples; int channels = 1; int rate = 48000; bool loop = false; };

class AudioEngine {
public:
    bool load_manifest(const std::string& manifestPath, const std::string& libraryRoot);
    bool has(const std::string& name) const;
    void play(const std::string& name, const Vec3& position, float gain = 1.0f,
              bool spatial = true);
    void play_ui(const std::string& name, float gain = 1.0f);
    void play_music(const std::string& name, float gain = 1.0f);
    void stop_music();
    void play_ambience(const std::string& name, float gain = 1.0f);
    void reset();
    void set_muted(bool muted);
    bool muted() const;
    void update(float dt, const Vec3& listenerPos, float listenerYawDeg);
    bool render_mixdown(const std::string& path, int seconds);
};
```

- [ ] **Step 1: Write the failing tests**

`test_audio.cpp` (doctest, no device):
```cpp
TEST_CASE("mixdown of a played tone is non-silent and does not clip")
TEST_CASE("pan moves the energy between channels")
TEST_CASE("distance attenuates a spatial voice")
TEST_CASE("a looping voice wraps and keeps sounding")
TEST_CASE("mute silences the mix")
TEST_CASE("voice stealing keeps the newest voice")
```
Use a synthetic 1 kHz mono clip in the fixture (no WAV files); assert RMS/peak with tolerances,
and `render_mixdown` returns true and writes a 48 kHz WAV.

- [ ] **Step 2: Run to verify red**

Run: `SOTC_LIB=... cmake --build app/build -j 8` → expected FAIL (audio.h missing).

- [ ] **Step 3: Implement**

`wav_io`: read a RIFF/WAVE float or PCM16 file into `AudioClip`; write a float stereo mixdown.
`AudioEngine::load_manifest`: parse `key=value` lines; load each WAV; a duplicate name or missing
file is an error; a missing manifest leaves the engine empty (silent) with one warning.
Mixer: 32 voices; music/ambience non-spatial; SFX attenuation `1/(1 + d/8)`, pan
`clamp(dot(right, dir), -1, 1) * 0.7`; loops wrap; mute zeroes; mix into a float stereo 48 kHz
buffer, `SDL_PutAudioStreamData` when the device is open, or append to the mixdown buffer.
`render_mixdown`: run the same mixer for N seconds with `dt = 1/60`, write the WAV.

- [ ] **Step 4: Run to verify green**

Run: `SOTC_LIB=... cmake --build app/build -j 8 && ctest --test-dir app/build -R unit_tests --output-on-failure`
Expected: PASS; the six cases green.

- [ ] **Step 5: Commit**

```bash
git add app/src/audio.h app/src/audio.cpp app/src/wav_io.h app/src/wav_io.cpp app/tests/test_audio.cpp app/CMakeLists.txt
git commit -m "feat(app): the S5 audio engine"
```

---

