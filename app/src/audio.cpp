#include "audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "wav_io.h"

namespace {

constexpr float kCategoryGains[] = {0.7f, 0.5f, 1.0f};  // music, ambience, sfx
constexpr float kReferenceDistance = 8.0f;              // attenuation 1/(1 + d/8)
constexpr float kPanSpread = 0.7f;
constexpr int kMixdownFramesStep = 800;                 // 1/60 s at 48 kHz
constexpr int kMaxQueuedBytes = 48000 * 2 * 4 / 2;      // half a second of output

float category_gain(AudioCategory category) {
    return kCategoryGains[static_cast<int>(category)];
}

float sanitize_gain(float gain) {
    if (!std::isfinite(gain)) {
        return 0.0f;
    }
    return std::clamp(gain, 0.0f, 1.0f);
}

// Whitespace-separated tokens; `#` starts a comment. `\r` counts as
// whitespace, so CRLF manifests parse.
std::vector<std::string> split_tokens(const std::string& line) {
    std::vector<std::string> tokens;
    std::string token;
    for (const char character : line) {
        if (character == '#') {
            break;
        }
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            if (!token.empty()) {
                tokens.push_back(std::move(token));
                token.clear();
            }
        } else {
            token.push_back(character);
        }
    }
    if (!token.empty()) {
        tokens.push_back(std::move(token));
    }
    return tokens;
}

}  // namespace

AudioEngine::AudioEngine() {
    voices_.resize(kMaxVoices);
    mixBuffer_.resize(static_cast<std::size_t>(kMaxMixFrames) * 2);
}

AudioEngine::~AudioEngine() {
    if (stream_ != nullptr) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
}

bool AudioEngine::load_manifest(const std::string& manifestPath,
                                const std::string& libraryRoot) {
    last_error_.clear();

    auto fail = [this](const std::string& message) {
        last_error_ = message;
        std::fprintf(stderr, "sotc-viewer: audio: %s\n", message.c_str());
        return false;
    };

    std::error_code file_error;
    if (!std::filesystem::is_regular_file(manifestPath, file_error)) {
        // A missing manifest degrades to silence with one warning; the engine
        // is left empty rather than keeping a stale clip set.
        stop_all_voices();
        clips_.clear();
        warnedMissing_.clear();
        return fail(manifestPath + ": cannot open audio manifest");
    }
    std::ifstream in(manifestPath);
    if (!in) {
        stop_all_voices();
        clips_.clear();
        warnedMissing_.clear();
        return fail(manifestPath + ": cannot open audio manifest");
    }

    std::unordered_map<std::string, AudioClip> loaded;
    std::string line;
    int line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        const std::vector<std::string> tokens = split_tokens(line);
        if (tokens.empty()) {
            continue;
        }

        std::unordered_map<std::string, std::string> fields;
        for (const std::string& token : tokens) {
            const std::size_t equals = token.find('=');
            if (equals == std::string::npos || equals == 0) {
                return fail(manifestPath + ":" + std::to_string(line_number) +
                            ": expected key=value, got '" + token + "'");
            }
            fields.emplace(token.substr(0, equals), token.substr(equals + 1));
        }

        const auto version = fields.find("version");
        if (version != fields.end() && version->second != "1") {
            return fail(manifestPath + ":" + std::to_string(line_number) +
                        ": unsupported audio manifest version '" + version->second + "'");
        }
        if (version != fields.end() && fields.find("name") == fields.end()) {
            continue;  // the format line
        }

        const auto name_it = fields.find("name");
        if (name_it == fields.end() || name_it->second.empty()) {
            return fail(manifestPath + ":" + std::to_string(line_number) +
                        ": audio entry has no name=");
        }
        const std::string& name = name_it->second;
        if (loaded.find(name) != loaded.end()) {
            return fail(manifestPath + ":" + std::to_string(line_number) +
                        ": duplicate audio name '" + name + "'");
        }

        const auto file_it = fields.find("file");
        if (file_it == fields.end() || file_it->second.empty()) {
            return fail(manifestPath + ":" + std::to_string(line_number) + ": audio '" + name +
                        "' has no file=");
        }
        const std::filesystem::path wav_path =
            std::filesystem::path(libraryRoot) / file_it->second;

        AudioClip clip;
        std::string wav_error;
        if (!read_wav(wav_path, clip, &wav_error)) {
            return fail(manifestPath + ":" + std::to_string(line_number) + ": audio '" + name +
                        "': " + wav_error);
        }
        if (clip.frames() == 0) {
            return fail(manifestPath + ":" + std::to_string(line_number) + ": audio '" + name +
                        "': " + wav_path.string() + " carries no sample frames");
        }
        const auto loop = fields.find("loop");
        if (loop != fields.end() && loop->second == "1") {
            clip.loop = true;
        }
        loaded.emplace(name, std::move(clip));
    }
    if (in.bad()) {
        return fail(manifestPath + ": read error");
    }

    // Commit: the previous clip set stays untouched on any failure above, so a
    // voice never outlives the samples it points at.
    stop_all_voices();
    clips_ = std::move(loaded);
    warnedMissing_.clear();
    return true;
}

bool AudioEngine::has(const std::string& name) const {
    return clips_.find(name) != clips_.end();
}

const AudioClip* AudioEngine::find_clip(const std::string& name) const {
    const auto it = clips_.find(name);
    return it == clips_.end() ? nullptr : &it->second;
}

void AudioEngine::start_voice(std::size_t slot, const AudioClip* clip, AudioCategory category,
                              float gain, const Vec3& position, bool spatial) {
    Voice& voice = voices_[slot];
    voice.clip = clip;
    voice.category = category;
    voice.gain = sanitize_gain(gain);
    voice.spatial = spatial;
    voice.position = position;
    voice.cursor = 0;
    voice.serial = nextSerial_++;
    voice.active = clip != nullptr && clip->frames() > 0;
}

void AudioEngine::stop_slot(std::size_t slot) {
    voices_[slot].active = false;
    voices_[slot].clip = nullptr;
}

void AudioEngine::stop_all_voices() {
    for (Voice& voice : voices_) {
        voice.active = false;
        voice.clip = nullptr;
        voice.cursor = 0;
    }
}

void AudioEngine::warn_missing(const std::string& name) {
    if (warnedMissing_.insert(name).second) {
        std::fprintf(stderr, "sotc-viewer: audio: no clip named '%s'\n", name.c_str());
    }
}

void AudioEngine::play(const std::string& name, const Vec3& position, float gain,
                       bool spatial) {
    const AudioClip* clip = find_clip(name);
    if (clip == nullptr || clip->frames() == 0) {
        warn_missing(name);
        return;
    }

    std::size_t slot = static_cast<std::size_t>(kMaxVoices);
    for (std::size_t i = kFirstSfxSlot; i < static_cast<std::size_t>(kMaxVoices); ++i) {
        if (!voices_[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == static_cast<std::size_t>(kMaxVoices)) {
        // All SFX slots are busy: steal the oldest (smallest serial).
        slot = kFirstSfxSlot;
        for (std::size_t i = kFirstSfxSlot + 1; i < static_cast<std::size_t>(kMaxVoices);
             ++i) {
            if (voices_[i].serial < voices_[slot].serial) {
                slot = i;
            }
        }
    }
    start_voice(slot, clip, AudioCategory::Sfx, gain, position, spatial);
}

void AudioEngine::play_ui(const std::string& name, float gain) {
    play(name, Vec3{0.0f, 0.0f, 0.0f}, gain, /*spatial=*/false);
}

void AudioEngine::play_music(const std::string& name, float gain) {
    const AudioClip* clip = find_clip(name);
    if (clip == nullptr || clip->frames() == 0) {
        warn_missing(name);
        return;
    }
    // Slot 0 is a single voice: a new cue replaces the current music.
    start_voice(kMusicSlot, clip, AudioCategory::Music, gain, Vec3{0.0f, 0.0f, 0.0f},
                /*spatial=*/false);
}

void AudioEngine::stop_music() { stop_slot(kMusicSlot); }

void AudioEngine::play_ambience(const std::string& name, float gain) {
    const AudioClip* clip = find_clip(name);
    if (clip == nullptr || clip->frames() == 0) {
        warn_missing(name);
        return;
    }
    start_voice(kAmbienceSlot, clip, AudioCategory::Ambience, gain,
                Vec3{0.0f, 0.0f, 0.0f}, /*spatial=*/false);
}

void AudioEngine::reset() { stop_all_voices(); }

void AudioEngine::set_muted(bool muted) { muted_ = muted; }

int AudioEngine::active_voices() const {
    int count = 0;
    for (const Voice& voice : voices_) {
        if (voice.active) {
            ++count;
        }
    }
    return count;
}

bool AudioEngine::is_playing(const std::string& name) const {
    const AudioClip* clip = find_clip(name);
    if (clip == nullptr) {
        return false;
    }
    for (const Voice& voice : voices_) {
        if (voice.active && voice.clip == clip) {
            return true;
        }
    }
    return false;
}

void AudioEngine::mix(float* out, std::size_t frames, const Vec3& listenerPos,
                      float listenerYawDeg) {
    const std::size_t sample_count = frames * 2;
    std::fill(out, out + sample_count, 0.0f);

    // Yaw 0 looks along +Z, so the listener's right is forward x +Y.
    const float yaw_radians = listenerYawDeg * kDegToRad;
    const Vec3 right{-std::cos(yaw_radians), 0.0f, std::sin(yaw_radians)};

    for (Voice& voice : voices_) {
        if (!voice.active) {
            continue;
        }
        const AudioClip* clip = voice.clip;
        if (clip == nullptr || clip->frames() == 0) {
            voice.active = false;
            continue;
        }

        float gain = voice.gain * category_gain(voice.category);
        float left_gain = gain;
        float right_gain = gain;
        if (voice.spatial) {
            const Vec3 delta{voice.position.x - listenerPos.x,
                             voice.position.y - listenerPos.y,
                             voice.position.z - listenerPos.z};
            const float distance = std::sqrt(vec3_dot(delta, delta));
            gain *= 1.0f / (1.0f + distance / kReferenceDistance);
            const Vec3 direction =
                distance > 0.0f ? Vec3{delta.x / distance, delta.y / distance,
                                       delta.z / distance}
                                : Vec3{0.0f, 0.0f, 0.0f};
            const float pan =
                std::clamp(vec3_dot(right, direction), -1.0f, 1.0f) * kPanSpread;
            // Constant-power pan: centre is -3 dB on each side, hard pan puts
            // the energy on one channel.
            const float angle = (pan + 1.0f) * (kPi / 4.0f);
            left_gain = gain * std::cos(angle);
            right_gain = gain * std::sin(angle);
        }

        const std::size_t clip_frames = clip->frames();
        const int channels = clip->channels;
        std::size_t cursor = voice.cursor;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            if (cursor >= clip_frames) {
                if (clip->loop) {
                    cursor %= clip_frames;
                } else {
                    voice.active = false;
                    break;
                }
            }
            const float* source =
                clip->samples.data() + cursor * static_cast<std::size_t>(channels);
            const std::size_t out_index = frame * 2;
            if (channels == 1) {
                out[out_index] += source[0] * left_gain;
                out[out_index + 1] += source[0] * right_gain;
            } else {
                out[out_index] += source[0] * left_gain;
                out[out_index + 1] += source[1] * right_gain;
            }
            ++cursor;
        }
        voice.cursor = cursor;
    }

    if (muted_) {
        std::fill(out, out + sample_count, 0.0f);
        return;
    }
    // The output stage is the only place samples are clamped: a loud stack may
    // saturate but never leaves the float WAV's [-1, 1] range.
    for (std::size_t i = 0; i < sample_count; ++i) {
        const float sample = out[i];
        out[i] = std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0.0f;
    }
}

void AudioEngine::ensure_device() {
    if (deviceTried_) {
        return;
    }
    deviceTried_ = true;

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "sotc-viewer: audio: SDL audio init failed: %s\n",
                     SDL_GetError());
        return;
    }
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (device_ == 0) {
        std::fprintf(stderr, "sotc-viewer: audio: no output device: %s\n", SDL_GetError());
        return;
    }
    const SDL_AudioSpec source_format{SDL_AUDIO_F32, 2, 48000};
    stream_ = SDL_CreateAudioStream(&source_format, nullptr);
    if (stream_ == nullptr) {
        std::fprintf(stderr, "sotc-viewer: audio: cannot create stream: %s\n",
                     SDL_GetError());
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        return;
    }
    if (!SDL_BindAudioStreams(device_, &stream_, 1)) {
        std::fprintf(stderr, "sotc-viewer: audio: cannot bind stream: %s\n",
                     SDL_GetError());
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        return;
    }
    SDL_SetAudioStreamGain(stream_, 1.0f);
}

void AudioEngine::update(float dt, const Vec3& listenerPos, float listenerYawDeg) {
    if (!std::isfinite(dt) || dt <= 0.0f) {
        return;
    }
    listenerPos_ = listenerPos;
    listenerYawDeg_ = std::isfinite(listenerYawDeg) ? listenerYawDeg : 0.0f;

    ensure_device();

    const float clamped = std::min(dt, static_cast<float>(kMaxMixFrames) / 48000.0f);
    const std::size_t frames = static_cast<std::size_t>(std::lround(clamped * 48000.0f));
    if (frames == 0) {
        return;
    }
    mix(mixBuffer_.data(), frames, listenerPos_, listenerYawDeg_);
    if (stream_ == nullptr) {
        return;  // silent, already logged once
    }
    // Never let the device backlog grow: the simulation's clock is the mixer's.
    if (SDL_GetAudioStreamQueued(stream_) >= kMaxQueuedBytes) {
        return;
    }
    const int bytes = static_cast<int>(frames * 2 * sizeof(float));
    if (!SDL_PutAudioStreamData(stream_, mixBuffer_.data(), bytes)) {
        std::fprintf(stderr, "sotc-viewer: audio: stream push failed: %s\n",
                     SDL_GetError());
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
}

bool AudioEngine::render_mixdown(const std::string& path, int seconds) {
    last_error_.clear();
    if (seconds <= 0 || seconds > kMixdownSecondsCap) {
        last_error_ = path + ": mixdown seconds must be in [1, " +
                      std::to_string(kMixdownSecondsCap) + "]";
        return false;
    }

    const std::size_t total_frames =
        static_cast<std::size_t>(seconds) * 48000u;
    std::vector<float> samples;
    samples.reserve(total_frames * 2);
    for (std::size_t done = 0; done < total_frames;
         done += static_cast<std::size_t>(kMixdownFramesStep)) {
        mix(mixBuffer_.data(), static_cast<std::size_t>(kMixdownFramesStep), listenerPos_,
            listenerYawDeg_);
        const std::size_t produced = static_cast<std::size_t>(kMixdownFramesStep) * 2;
        samples.insert(samples.end(), mixBuffer_.begin(), mixBuffer_.begin() + produced);
    }

    std::string error;
    if (!write_wav(path, samples, 2, 48000, &error)) {
        last_error_ = error;
        return false;
    }
    return true;
}
