#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "math3d.h"

// S5 audio engine (design §7.4): a software mixer at 48 kHz float stereo that
// reads the converter's R1 audio manifest (Task 3) and feeds one SDL3 audio
// stream. The mix itself is pure math over preallocated buffers — no SDL call
// and no allocation on the per-frame path — so `render_mixdown` drives exactly
// what the device path pushes.
//
// The device is opened lazily on the first `update`; a failure stays silent and
// logs once, never throwing. `render_mixdown` never opens a device.

enum class AudioCategory { Music, Ambience, Sfx };

// One decoded clip, interleaved float32 at 48 kHz. `loop` comes from the
// manifest's `loop=` flag (the mixer wraps a cursor, not an SDL flag).
struct AudioClip {
    std::vector<float> samples;  // interleaved
    int channels = 1;
    int rate = 48000;
    bool loop = false;

    std::size_t frames() const {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }
};

// Forwards the SDL audio types without pulling SDL into the mixer's header: the
// engine only ever passes these handles through to audio.cpp.
struct SDL_AudioStream;

class AudioEngine {
public:
    // 32 voices: slot 0 music, slot 1 ambience, 2..31 SFX.
    static constexpr int kMaxVoices = 32;
    static constexpr int kMaxSfxVoices = kMaxVoices - 2;

    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Parses `manifestPath` (R1 `key=value` lines) and loads every entry's WAV
    // from `libraryRoot + "/" + file`. A duplicate name, a malformed line or a
    // missing/unsupported file returns false with the offending name/path in
    // `last_error()`; a missing manifest leaves the engine empty and warns
    // once. A failed load leaves the previous clip set and voices untouched —
    // the load is transactional. `gain_db` is provenance only (R12): the clip
    // is never rescaled.
    bool load_manifest(const std::string& manifestPath, const std::string& libraryRoot);

    bool has(const std::string& name) const;
    std::size_t clip_count() const { return clips_.size(); }

    // World SFX. `spatial` true applies distance attenuation and pan; false
    // plays centred. A voice at 0 m is unattenuated and centred. `gain` is
    // clamped into [0, 1]; the category gain is applied on top.
    void play(const std::string& name, const Vec3& position, float gain = 1.0f,
              bool spatial = true);
    // Centred SFX (UI/player-local) — same SFX pool as `play`.
    void play_ui(const std::string& name, float gain = 1.0f);
    // Slot 0; a second call replaces the current music.
    void play_music(const std::string& name, float gain = 1.0f);
    void stop_music();
    // Slot 1; a second call replaces the current ambience.
    void play_ambience(const std::string& name, float gain = 1.0f);

    // Stops every voice (music included); the mute state is kept.
    void reset();
    void set_muted(bool muted);
    bool muted() const { return muted_; }

    // Mixes `dt` seconds and pushes to the device (lazy-open). `dt` is clamped
    // to [0, 1] s; a non-positive or non-finite dt is a no-op. The listener
    // pose is remembered for `render_mixdown`.
    void update(float dt, const Vec3& listenerPos, float listenerYawDeg);

    // Runs the same mixer for `seconds` at dt = 1/60 (800 frames a step) and
    // writes float32 stereo 48 kHz RIFF/WAVE to `path`. No device is opened.
    // Uses the last listener pose passed to `update` (origin, yaw 0 by default).
    bool render_mixdown(const std::string& path, int seconds);

    // Introspection for tests and acceptance: active voice count, whether any
    // active voice plays `name`, and the last load/mixdown failure.
    int active_voices() const;
    bool is_playing(const std::string& name) const;
    const std::string& last_error() const { return last_error_; }

private:
    struct Voice {
        const AudioClip* clip = nullptr;
        AudioCategory category = AudioCategory::Sfx;
        float gain = 1.0f;
        bool spatial = true;
        Vec3 position{};
        std::size_t cursor = 0;  // frame index into the clip
        std::uint64_t serial = 0;
        bool active = false;
    };

    static constexpr std::size_t kMusicSlot = 0;
    static constexpr std::size_t kAmbienceSlot = 1;
    static constexpr std::size_t kFirstSfxSlot = 2;
    // One second of headroom: `update` clamps dt here and the buffer is
    // preallocated, so the per-frame mix never allocates.
    static constexpr int kMaxMixFrames = 48000;
    static constexpr int kMixdownSecondsCap = 120;

    void start_voice(std::size_t slot, const AudioClip* clip, AudioCategory category,
                     float gain, const Vec3& position, bool spatial);
    void stop_slot(std::size_t slot);
    void stop_all_voices();
    const AudioClip* find_clip(const std::string& name) const;

    // The pure mix: sums every active voice into `out` (interleaved stereo,
    // `frames` frames), applies category gain, attenuation/pan, mute and the
    // [-1, 1] clamp, and advances the voices' cursors.
    void mix(float* out, std::size_t frames, const Vec3& listenerPos,
             float listenerYawDeg);

    void ensure_device();
    void warn_missing(const std::string& name);

    std::unordered_map<std::string, AudioClip> clips_;
    std::vector<Voice> voices_;
    std::vector<float> mixBuffer_;  // kMaxMixFrames * 2, allocated once
    std::unordered_set<std::string> warnedMissing_;
    Vec3 listenerPos_{0.0f, 0.0f, 0.0f};
    float listenerYawDeg_ = 0.0f;
    std::uint64_t nextSerial_ = 0;
    bool muted_ = false;
    bool deviceTried_ = false;
    std::uint32_t device_ = 0;
    SDL_AudioStream* stream_ = nullptr;
    std::string last_error_;
};
