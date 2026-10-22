### Task 5: App — event seam, wiring, `--s5-selftest` mixdown

**Files:**
- Create: `app/src/audio_events.h`, `app/src/audio_map.h`
- Modify: `app/src/player_state.h`, `app/src/player_state.cpp` (`StepEvents` fields only)
- Modify: `app/src/app.cpp`, `app/src/app.h`, `app/src/main.cpp`
- Test: `app/tests/test_audio_events.cpp`, `app/tests/test_player_state.cpp`
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Consumes: `StepEvents`, the encounter events, `AudioEngine` (Task 4), the curated map names.
- Produces:
```cpp
enum class Sfx : std::uint16_t {
    Strike, StrikeHit, Grab, GripBreak, Land, HurtLight, HurtHeavy, Death,
    StompStart, StompImpact, ClubStart, ClubImpact, ClubCancel,
    ValusReaction, ValusDefeat, FootLeft, FootRight, Reset, Count
};
struct SfxEvent { Sfx id; float gain = 1.0f; Vec3 position{}; bool spatial = false; };
struct SfxQueue { std::array<SfxEvent, 64> items{}; int count = 0; void push(const SfxEvent&); };

struct SoundCue { const char* clip; float gain; bool loop; };
const SoundCue* cue_for(Sfx id);   // nullptr = deliberately silent (e.g. FootLeft if deferred)

void append_player_sfx(const StepEvents& events, const PlayerFrame& frame, SfxQueue& out);
void append_encounter_sfx(const Encounter& encounter, const Vec3& at, SfxQueue& out);
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("StepEvents hurt flags translate to HurtLight/HurtHeavy")
TEST_CASE("a landed strike translates to Strike + StrikeHit; a whiff to Strike")
TEST_CASE("grab/grip break/land translate")
TEST_CASE("Valus impact translates StompImpact/ClubImpact once per beat")
TEST_CASE("reset clears the queue and stops music")
```
`test_player_state.cpp`: damage while grounded sets `hurtLight`/`hurtHeavy`; lethal sets `died`;
`player_step` still performs no I/O.

- [ ] **Step 2: Run to verify red**

Run: `SOTC_LIB=... cmake --build app/build -j 8` → expected FAIL.

- [ ] **Step 3: Implement**

`player_state`: add `bool hurtLight = false; bool hurtHeavy = false; bool died = false;` to
`StepEvents`; set them in `player_take_damage`/`enter_hurt`/`enter_dead` (no calls). `audio_events`:
pure translations, no SDL. `audio_map`: the `cue_for` table (clip name, gain, loop); every non-null
cue's clip must exist in the engine's manifest, or load fails naming the `Sfx`. `app.cpp`: one
`AudioEngine` in `run_app`; load the manifest when the
encounter composes; translate the queue once per frame; music starts on the first landed hit,
stops on defeat/reset; ambience on encounter load; `M` mute; `reset` stops everything.
`--s5-selftest`: run the S4.2 timeline with audio enabled and `--audio-mixdown <path>`, log
`s5-event t=<s> event=<name> clip=<bank/name> gain=<x>` in order, exit 1 on a missing clip or a
silent window. **Audio off (the default) must print the same `s42-*` lines as before** — capture
both and `diff`.

- [ ] **Step 4: Run to verify green**

```bash
SOTC_LIB=... cmake --build app/build -j 8
SOTC_LIB=... ctest --test-dir app/build -R unit_tests --output-on-failure
./app/build/sotc-viewer ... --s42-selftest > out/s5/s42-audio-off.txt   # exact prior lines
./app/build/sotc-viewer ... --s5-selftest --audio-mixdown out/s5/mix.wav
```
Expected: PASS; `out/s5/mix.wav` is 48 kHz, non-silent; the `s42-*` diff is empty.

- [ ] **Step 5: Commit**

```bash
git add app/src/audio_events.h app/src/player_state.h app/src/player_state.cpp app/src/app.cpp app/src/app.h app/src/main.cpp app/tests/test_audio_events.cpp app/tests/test_player_state.cpp app/CMakeLists.txt
git commit -m "feat(app): wire and prove the S5 sound events"
```

---

