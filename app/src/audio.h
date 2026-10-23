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
// The device is opened lazily on the first `play*`; a failure stays silent and
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

    // R14 headroom: the output stage scales the summed voices by 0.85 before
    // the clamp. The shipped clips are normalized to 0.99, so the cue table's
    // gains (music 0.45, most SFX 0.5-0.8) keep an ordinary music + SFX stack
    // under the 0.99 delivery ceiling instead of square-waving. A per-voice gain
    // is accepted up to 2.0 so the table can boost one clip (HurtHeavy's 1.25)
    // and still be scaled by the master stage.
    static constexpr float kMasterGain = 0.85f;
    static constexpr float kMaxVoiceGain = 2.0f;

    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Parses `manifestPath` (R1 `key=value` lines) and loads every entry's WAV
    // from `libraryRoot + "/" + file`. A duplicate name, a malformed line, an
    // absolute `file=` or a missing/unsupported file returns false with the
    // offending name/path in `last_error()`; a parse/load failure is
    // transactional (the previous clip set and voices stay untouched), while a
    // missing manifest empties the engine and warns once. `gain_db` is
    // provenance only (R12): the clip is never rescaled.
    bool load_manifest(const std::string& manifestPath, const std::string& libraryRoot);

    bool has(const std::string& name) const;
    std::size_t clip_count() const { return clips_.size(); }

    // World SFX. `spatial` true applies distance attenuation and pan; false
    // plays centred. A voice at 0 m is unattenuated and centred. `gain` is
    // clamped into [0, kMaxVoiceGain]; the category gain is applied on top.
    void play(const std::string& name, const Vec3& position, float gain = 1.0f,
              bool spatial = true);
    // Centred SFX (UI/player-local) — same SFX pool as `play`.
    void play_ui(const std::string& name, float gain = 1.0f);
    // Slot 0; a second call replaces the current music.
    void play_music(const std::string& name, float gain = 1.0f);
    // Stops the music voice only while it is live, and leaves the queued device
    // mix to drain: the frame that stops the music may also have queued the
    // SFX that caused the stop. Idempotent. `reset` and `set_muted(true)` are
    // the calls that drop the queue.
    void stop_music();
    // Stops every SFX voice playing `name` (the music and ambience slots are
    // untouched); a no-op when none does. Like `stop_music`, it leaves the
    // queued device mix to drain.
    void stop(const std::string& name);
    // Slot 1; a second call replaces the current ambience.
    void play_ambience(const std::string& name, float gain = 1.0f);

    // Stops every voice (music included) and clears queued device audio; the
    // mute state is kept.
    void reset();
    void set_muted(bool muted);
    bool muted() const { return muted_; }

    // Mixes `dt` seconds and tops up the device queue (the device itself opens
    // on the first play* call). `dt` is clamped to [0, 1] s; a non-positive or
    // non-finite dt is a no-op. The listener pose is remembered for
    // `render_mixdown`.
    void update(float dt, const Vec3& listenerPos, float listenerYawDeg);

    // Runs the same mixer for `seconds` at dt = 1/60 (800 frames a step) and
    // writes float32 stereo 48 kHz RIFF/WAVE to `path`. No device is opened.
    // Uses the last listener pose passed to `update` (origin, yaw 0 by default).
    bool render_mixdown(const std::string& path, int seconds);

    // Headless capture (S5 Task 5): while capturing, every `update` appends the
    // block it mixed to an internal buffer and no device is opened — a capture
    // must work without hardware. `end_capture` writes the accumulated mix as a
    // float32 stereo 48 kHz WAV and releases the buffer. Only one capture at a
    // time; a second `begin_capture` fails.
    bool begin_capture(const std::string& path);
    bool end_capture();
    bool capturing() const { return capturing_; }
    std::size_t captured_frames() const { return capture_samples_.size() / 2; }

    // Introspection for tests and acceptance: active voice count, whether any
    // active voice plays `name`, the device queue lead in bytes (0 when no
    // device is open), and the last load/mixdown failure.
    int active_voices() const;
    bool is_playing(const std::string& name) const;
    int queued_audio_bytes() const;
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
    // 600 s covers the full 125.93 s `music.boss` with room to spare: the S5
    // acceptance can dump a whole encounter without truncation.
    static constexpr int kMixdownSecondsCap = 600;

    void start_voice(std::size_t slot, const AudioClip* clip, AudioCategory category,
                     float gain, const Vec3& position, bool spatial);
    void stop_slot(std::size_t slot);
    void stop_all_voices();
    const AudioClip* find_clip(const std::string& name) const;

    // The pure mix: sums every active voice into `out` (interleaved stereo,
    // `frames` frames), applies category gain, attenuation/pan, mute, the
    // master headroom gain and the [-1, 1] clamp, and advances the voices'
    // cursors.
    void mix(float* out, std::size_t frames, const Vec3& listenerPos,
             float listenerYawDeg);
    // Appends the block just mixed to the capture buffer (interleaved stereo).
    void append_capture(std::size_t frames);

    void ensure_device();
    // Mixes one 800-frame block and pushes it to the bound stream. A failed
    // push destroys the stream (the engine stays silent, already logged once).
    void push_block(std::size_t frames);
    // Keeps ~3 blocks (50 ms) queued so the device never starves between
    // frames; bounded by the 0.5 s cap and a small iteration guard.
    void top_up();
    // Drops queued device audio, so mute/reset take effect immediately instead
    // of trailing the prebuffer. `stop_music` deliberately does not come here.
    void clear_stream();
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
    // Capture state: the sink path, the accumulated float stereo samples, and
    // whether `update` should append instead of pushing to a device.
    bool capturing_ = false;
    std::string capture_path_;
    std::vector<float> capture_samples_;
    std::string last_error_;
};
