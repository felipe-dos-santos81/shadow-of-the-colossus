// Audio-engine tests (S5 Task 4): the mixer is pure math over preallocated
// buffers, so every case here runs headless — `render_mixdown` drives the same
// mix the device path pushes, and no SDL audio device is ever opened. Fixtures
// are real RIFF/WAVE files written with `wav_io`, plus a temp R1 manifest, so
// the load path (libraryRoot-relative `file=`) is exercised too.
#include <doctest.h>

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "audio.h"
#include "wav_io.h"

namespace {

// Force SDL's dummy driver for the whole test binary: every play* call now
// opens (and primes) a device, and the smoke test below needs a deterministic,
// silent one. This runs during static initialization — before main and before
// the first SDL audio init — so the driver choice is stable regardless of test
// order. No other test in this binary initializes SDL audio.
[[maybe_unused]] const bool kDummyAudioDriver = [] {
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);
    return true;
}();

// A scratch directory that deletes itself. The counter keeps the cases apart
// even when they run inside the same process.
class TempDir {
public:
    TempDir() {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() /
                ("sotc-audio-fixture-" + std::to_string(::getpid()) + "-" +
                 std::to_string(++counter));
        std::error_code ec;
        std::filesystem::create_directories(path_, ec);
        if (ec) {
            throw std::runtime_error("cannot create fixture dir " + path_.string() + ": " +
                                     ec.message());
        }
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);  // best effort
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

    std::filesystem::path make_parent(const std::string& relPath) const {
        const std::filesystem::path full = path_ / relPath;
        std::error_code ec;
        std::filesystem::create_directories(full.parent_path(), ec);
        if (ec) {
            throw std::runtime_error("cannot create fixture dir " + full.parent_path().string() +
                                     ": " + ec.message());
        }
        return full;
    }

private:
    std::filesystem::path path_;
};

struct FixtureEntry {
    std::string name;
    std::string file;
    int channels = 1;
    std::size_t frames = 0;
    int loop = 0;
};

// A 1 kHz sine, interleaved on `channels`. 48 kHz keeps every fixture on the
// mixer's single rate; 1 kHz is far from the block boundaries, so a wrap or a
// channel swap cannot hide in a zero crossing.
std::vector<float> tone(float seconds, float amplitude, int channels) {
    const std::size_t frames =
        static_cast<std::size_t>(std::lround(seconds * 48000.0f));
    std::vector<float> samples(frames * static_cast<std::size_t>(channels));
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float value =
            amplitude * std::sin(2.0f * 3.14159265358979323846f * 1000.0f *
                                 static_cast<float>(frame) / 48000.0f);
        for (int channel = 0; channel < channels; ++channel) {
            samples[frame * static_cast<std::size_t>(channels) +
                   static_cast<std::size_t>(channel)] = value;
        }
    }
    return samples;
}

void write_tone_wav(const TempDir& dir, const std::string& relPath, float seconds,
                    float amplitude, int channels) {
    const std::vector<float> samples = tone(seconds, amplitude, channels);
    std::string error;
    REQUIRE_MESSAGE(write_wav(dir.make_parent(relPath), samples, channels, 48000, &error),
                    error);
}

// Writes the R1 manifest next to the fixture WAVs.
std::filesystem::path write_manifest(const TempDir& dir, const std::vector<FixtureEntry>& entries) {
    std::ofstream out(dir.path() / "audio.manifest");
    REQUIRE(out.good());
    out << "version=1\n";
    for (const FixtureEntry& entry : entries) {
        out << "name=" << entry.name << " bank=test file=" << entry.file
            << " rate=48000 channels=" << entry.channels << " frames=" << entry.frames
            << " seconds=" << (static_cast<double>(entry.frames) / 48000.0)
            << " codec=psadpcm loop=" << entry.loop << " gain_db=-0.0873\n";
    }
    return dir.path() / "audio.manifest";
}

AudioClip read_mixdown(const std::filesystem::path& path) {
    AudioClip clip;
    std::string error;
    REQUIRE_MESSAGE(read_wav(path, clip, &error), error);
    REQUIRE(clip.channels == 2);
    REQUIRE(clip.rate == 48000);
    return clip;
}

float channel_rms(const AudioClip& clip, int channel) {
    const std::size_t frames = clip.frames();
    REQUIRE(frames > 0);
    double sum = 0.0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double value =
            clip.samples[frame * static_cast<std::size_t>(clip.channels) +
                         static_cast<std::size_t>(channel)];
        sum += value * value;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(frames)));
}

float stereo_rms(const AudioClip& clip) {
    const std::size_t frames = clip.frames();
    REQUIRE(frames > 0);
    double sum = 0.0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < 2; ++channel) {
            const double value =
                clip.samples[frame * 2 + static_cast<std::size_t>(channel)];
            sum += value * value;
        }
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(frames * 2)));
}

float peak(const AudioClip& clip) {
    float maximum = 0.0f;
    for (const float sample : clip.samples) {
        maximum = std::max(maximum, std::fabs(sample));
    }
    return maximum;
}

// RMS over the second half of the clip — the loop test's window.
float second_half_rms(const AudioClip& clip) {
    const std::size_t frames = clip.frames();
    REQUIRE(frames > 1);
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t frame = frames / 2; frame < frames; ++frame) {
        for (int channel = 0; channel < clip.channels; ++channel) {
            const double value =
                clip.samples[frame * static_cast<std::size_t>(clip.channels) +
                             static_cast<std::size_t>(channel)];
            sum += value * value;
            ++count;
        }
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

std::uint32_t read_le32(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::uint16_t read_le16(const unsigned char* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(bytes[1] << 8);
}

void append_le16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFu));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
}

void append_le32(std::vector<unsigned char>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<unsigned char>((value >> shift) & 0xFFu));
    }
}

void append_id(std::vector<unsigned char>& out, const char* id) {
    out.insert(out.end(), id, id + 4);
}

// A PCM16 RIFF/WAVE fixture, for the reader's compatibility path. Written by
// hand because `wav_io`'s writer is float-only by design.
std::filesystem::path write_pcm16_wav(const TempDir& dir, const std::string& relPath,
                                      const std::vector<float>& samples, int channels,
                                      int rate) {
    const std::filesystem::path path = dir.make_parent(relPath);
    const std::uint32_t data_bytes =
        static_cast<std::uint32_t>(samples.size() * 2);
    const std::uint16_t block_align =
        static_cast<std::uint16_t>(channels * 2);
    std::vector<unsigned char> bytes;
    append_id(bytes, "RIFF");
    append_le32(bytes, 36 + data_bytes);
    append_id(bytes, "WAVE");
    append_id(bytes, "fmt ");
    append_le32(bytes, 16);
    append_le16(bytes, 1);  // PCM
    append_le16(bytes, static_cast<std::uint16_t>(channels));
    append_le32(bytes, static_cast<std::uint32_t>(rate));
    append_le32(bytes, static_cast<std::uint32_t>(rate * block_align));
    append_le16(bytes, block_align);
    append_le16(bytes, 16);
    append_id(bytes, "data");
    append_le32(bytes, data_bytes);
    for (const float sample : samples) {
        const auto value = static_cast<std::int16_t>(
            std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
        append_le16(bytes, static_cast<std::uint16_t>(value));
    }
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out.good());
    return path;
}

std::filesystem::path test_library() {
    const char* value = std::getenv("SOTC_LIB");
    if (value == nullptr || value[0] == '\0') {
        throw std::runtime_error("Set SOTC_LIB to the converted asset library.");
    }
    std::filesystem::path library = value;
    if (library.is_relative()) {
        const std::filesystem::path repo_root =
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        library = repo_root / library;
    }
    return library;
}

}  // namespace

TEST_CASE("mixdown of a played tone is non-silent and does not clip") {
    const TempDir dir;
    write_tone_wav(dir, "audio/test/tone.wav", 2.0f, 0.5f, 1);
    const std::filesystem::path manifest =
        write_manifest(dir, {{"tone", "audio/test/tone.wav", 1, 96000, 0}});

    AudioEngine engine;
    REQUIRE_MESSAGE(engine.load_manifest(manifest.string(), dir.path().string()),
                    engine.last_error());
    REQUIRE(engine.has("tone"));
    CHECK(engine.clip_count() == 1);

    engine.play_ui("tone");
    const std::filesystem::path mix = dir.path() / "mix.wav";
    REQUIRE_MESSAGE(engine.render_mixdown(mix.string(), 1), engine.last_error());

    const AudioClip clip = read_mixdown(mix);
    CHECK(clip.frames() == 48000);
    CHECK(channel_rms(clip, 0) > 0.05f);
    CHECK(channel_rms(clip, 1) > 0.05f);
    CHECK(peak(clip) <= 1.0f);
    CHECK(peak(clip) > 0.1f);
}

TEST_CASE("pan moves the energy between channels") {
    const TempDir dir;
    write_tone_wav(dir, "audio/test/tone.wav", 2.0f, 0.5f, 1);
    const std::filesystem::path manifest =
        write_manifest(dir, {{"tone", "audio/test/tone.wav", 1, 96000, 0}});

    AudioEngine engine;
    REQUIRE_MESSAGE(engine.load_manifest(manifest.string(), dir.path().string()),
                    engine.last_error());

    // Listener yaw 0 looks along +Z, so its right is -X: +X is the left side.
    engine.play("tone", Vec3{4.0f, 0.0f, 0.0f}, 1.0f);
    REQUIRE_MESSAGE(engine.render_mixdown((dir.path() / "left.wav").string(), 1),
                    engine.last_error());
    const AudioClip left = read_mixdown(dir.path() / "left.wav");
    CHECK(channel_rms(left, 0) > channel_rms(left, 1) * 1.5f);

    engine.reset();
    engine.play("tone", Vec3{-4.0f, 0.0f, 0.0f}, 1.0f);
    REQUIRE_MESSAGE(engine.render_mixdown((dir.path() / "right.wav").string(), 1),
                    engine.last_error());
    const AudioClip right = read_mixdown(dir.path() / "right.wav");
    CHECK(channel_rms(right, 1) > channel_rms(right, 0) * 1.5f);
}

TEST_CASE("distance attenuates a spatial voice") {
    const TempDir dir;
    write_tone_wav(dir, "audio/test/tone.wav", 2.0f, 0.5f, 1);
    const std::filesystem::path manifest =
        write_manifest(dir, {{"tone", "audio/test/tone.wav", 1, 96000, 0}});

    AudioEngine engine;
    REQUIRE_MESSAGE(engine.load_manifest(manifest.string(), dir.path().string()),
                    engine.last_error());

    engine.play("tone", Vec3{0.0f, 0.0f, 1.0f}, 1.0f);
    REQUIRE_MESSAGE(engine.render_mixdown((dir.path() / "near.wav").string(), 1),
                    engine.last_error());
    const float near_rms = stereo_rms(read_mixdown(dir.path() / "near.wav"));

    engine.reset();
    engine.play("tone", Vec3{0.0f, 0.0f, 20.0f}, 1.0f);
    REQUIRE_MESSAGE(engine.render_mixdown((dir.path() / "far.wav").string(), 1),
                    engine.last_error());
    const float far_rms = stereo_rms(read_mixdown(dir.path() / "far.wav"));

    REQUIRE(near_rms > 0.0f);
    REQUIRE(far_rms > 0.0f);
    // 1/(1 + 1/8) over 1/(1 + 20/8) = 3.111...
    CHECK(near_rms / far_rms == doctest::Approx(3.5f / 1.125f).epsilon(0.02));
}

TEST_CASE("a looping voice wraps and keeps sounding") {
    const TempDir dir;
    write_tone_wav(dir, "audio/test/loop.wav", 0.1f, 0.5f, 1);
    const std::filesystem::path manifest =
        write_manifest(dir, {{"loop", "audio/test/loop.wav", 1, 4800, 1}});

    AudioEngine engine;
    REQUIRE_MESSAGE(engine.load_manifest(manifest.string(), dir.path().string()),
                    engine.last_error());
    engine.play_ui("loop");
    REQUIRE_MESSAGE(engine.render_mixdown((dir.path() / "loop.wav").string(), 1),
                    engine.last_error());

    const AudioClip clip = read_mixdown(dir.path() / "loop.wav");
    CHECK(second_half_rms(clip) > 0.05f);
    CHECK(engine.active_voices() == 1);
}

TEST_CASE("mute silences the mix") {
    const TempDir dir;
    write_tone_wav(dir, "audio/test/tone.wav", 2.0f, 0.5f, 1);
    const std::filesystem::path manifest =
        write_manifest(dir, {{"tone", "audio/test/tone.wav", 1, 96000, 0}});

    AudioEngine engine;
    REQUIRE_MESSAGE(engine.load_manifest(manifest.string(), dir.path().string()),
                    engine.last_error());
    engine.play_ui("tone");
    engine.set_muted(true);
    CHECK(engine.muted());

    REQUIRE_MESSAGE(engine.render_mixdown((dir.path() / "muted.wav").string(), 1),
                    engine.last_error());
    const AudioClip clip = read_mixdown(dir.path() / "muted.wav");
    REQUIRE(!clip.samples.empty());
    for (const float sample : clip.samples) {
        CHECK(sample == 0.0f);
    }

    // reset stops voices but keeps the mute state.
    engine.reset();
    CHECK(engine.muted());
    CHECK(engine.active_voices() == 0);
}

TEST_CASE("voice stealing keeps the newest") {
    const TempDir dir;
    std::vector<FixtureEntry> entries;
    for (int i = 0; i < 33; ++i) {
        const std::string name = "voice." + (i < 10 ? std::string("0") : std::string()) +
                                 std::to_string(i);
        const std::string file = "audio/test/" + name + ".wav";
        write_tone_wav(dir, file, 0.2f, 0.1f, 1);
        entries.push_back({name, file, 1, 9600, 0});
    }
    const std::filesystem::path manifest = write_manifest(dir, entries);

    AudioEngine engine;
    REQUIRE_MESSAGE(engine.load_manifest(manifest.string(), dir.path().string()),
                    engine.last_error());
    REQUIRE(engine.clip_count() == 33);
    for (const FixtureEntry& entry : entries) {
        engine.play_ui(entry.name);
    }

    // 30 SFX slots: the first three voices were stolen, the newest survives.
    CHECK(engine.active_voices() == 30);
    CHECK_FALSE(engine.is_playing("voice.00"));
    CHECK_FALSE(engine.is_playing("voice.02"));
    CHECK(engine.is_playing("voice.03"));
    CHECK(engine.is_playing("voice.32"));
}

TEST_CASE("load_manifest rejects a duplicate or a missing file") {
    SUBCASE("duplicate name") {
        const TempDir dir;
        write_tone_wav(dir, "audio/test/tone.wav", 0.5f, 0.5f, 1);
        write_tone_wav(dir, "audio/test/tone2.wav", 0.5f, 0.5f, 1);
        const std::filesystem::path manifest =
            write_manifest(dir, {{"tone", "audio/test/tone.wav", 1, 24000, 0},
                                 {"tone", "audio/test/tone2.wav", 1, 24000, 0}});

        AudioEngine engine;
        CHECK_FALSE(engine.load_manifest(manifest.string(), dir.path().string()));
        CHECK(engine.last_error().find("tone") != std::string::npos);
        CHECK_FALSE(engine.has("tone"));
    }

    SUBCASE("missing WAV") {
        const TempDir dir;
        const std::filesystem::path manifest =
            write_manifest(dir, {{"ghost", "audio/test/ghost.wav", 1, 24000, 0}});

        AudioEngine engine;
        CHECK_FALSE(engine.load_manifest(manifest.string(), dir.path().string()));
        CHECK(engine.last_error().find("ghost") != std::string::npos);
        CHECK_FALSE(engine.has("ghost"));
    }

    SUBCASE("absolute file escapes the library") {
        const TempDir dir;
        const std::filesystem::path manifest =
            write_manifest(dir, {{"escape", "/tmp/escape.wav", 1, 24000, 0}});

        AudioEngine engine;
        CHECK_FALSE(engine.load_manifest(manifest.string(), dir.path().string()));
        CHECK(engine.last_error().find("libraryRoot-relative") != std::string::npos);
        CHECK_FALSE(engine.has("escape"));
    }

    SUBCASE("a .. component escapes the library") {
        const TempDir dir;
        const std::filesystem::path manifest =
            write_manifest(dir, {{"dotdot", "../../escape.wav", 1, 24000, 0}});

        AudioEngine engine;
        CHECK_FALSE(engine.load_manifest(manifest.string(), dir.path().string()));
        CHECK(engine.last_error().find("escapes libraryRoot") != std::string::npos);
        CHECK_FALSE(engine.has("dotdot"));
    }
}

TEST_CASE("render_mixdown writes a 48 kHz float WAV") {
    const TempDir dir;
    write_tone_wav(dir, "audio/test/tone.wav", 2.0f, 0.5f, 1);
    const std::filesystem::path manifest =
        write_manifest(dir, {{"tone", "audio/test/tone.wav", 1, 96000, 0}});

    AudioEngine engine;
    REQUIRE_MESSAGE(engine.load_manifest(manifest.string(), dir.path().string()),
                    engine.last_error());
    engine.play_ui("tone");
    const std::filesystem::path mix = dir.path() / "header.wav";
    REQUIRE_MESSAGE(engine.render_mixdown(mix.string(), 1), engine.last_error());

    std::ifstream in(mix, std::ios::binary);
    REQUIRE(in.good());
    unsigned char header[40] = {};
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    REQUIRE(static_cast<std::size_t>(in.gcount()) == sizeof(header));

    CHECK(std::memcmp(header + 0, "RIFF", 4) == 0);
    CHECK(std::memcmp(header + 8, "WAVE", 4) == 0);
    CHECK(std::memcmp(header + 12, "fmt ", 4) == 0);
    CHECK(read_le32(header + 16) == 16);
    CHECK(read_le16(header + 20) == 3);       // IEEE float
    CHECK(read_le16(header + 22) == 2);       // stereo
    CHECK(read_le32(header + 24) == 48000);   // rate
    CHECK(read_le16(header + 34) == 32);      // bits per sample
    CHECK(std::memcmp(header + 36, "data", 4) == 0);
    // 44-byte header + one second of stereo float32 frames.
    CHECK(std::filesystem::file_size(mix) == 44u + 48000u * 2u * 4u);
}

TEST_CASE("wav_io reads PCM16 and refuses a non-48 kHz rate") {
    const TempDir dir;
    const std::vector<float> samples = tone(0.25f, 0.5f, 1);
    const std::filesystem::path pcm_path =
        write_pcm16_wav(dir, "audio/test/pcm16.wav", samples, 1, 48000);

    AudioClip clip;
    std::string error;
    REQUIRE_MESSAGE(read_wav(pcm_path, clip, &error), error);
    CHECK(clip.channels == 1);
    CHECK(clip.rate == 48000);
    REQUIRE(clip.samples.size() == samples.size());
    for (std::size_t i = 0; i < samples.size(); i += 97) {
        CHECK(clip.samples[i] == doctest::Approx(samples[i]).epsilon(0.001));
    }

    const std::filesystem::path wrong_rate =
        write_pcm16_wav(dir, "audio/test/44100.wav", samples, 1, 44100);
    AudioClip rejected;
    CHECK_FALSE(read_wav(wrong_rate, rejected, &error));
    CHECK(error.find("48") != std::string::npos);
}

TEST_CASE("music and ambience each own one replaceable voice") {
    const TempDir dir;
    write_tone_wav(dir, "audio/test/music.wav", 0.5f, 0.2f, 2);
    write_tone_wav(dir, "audio/test/wind.wav", 0.5f, 0.2f, 1);
    const std::filesystem::path manifest =
        write_manifest(dir, {{"music", "audio/test/music.wav", 2, 24000, 1},
                             {"wind", "audio/test/wind.wav", 1, 24000, 1}});

    AudioEngine engine;
    REQUIRE_MESSAGE(engine.load_manifest(manifest.string(), dir.path().string()),
                    engine.last_error());
    engine.play_music("music");
    CHECK(engine.queued_audio_bytes() > 0);  // primed on the first play
    engine.play_music("music");
    CHECK(engine.active_voices() == 1);  // a second cue replaces the first
    engine.play_ambience("wind");
    CHECK(engine.active_voices() == 2);  // separate slots
    engine.stop_music();
    CHECK_FALSE(engine.is_playing("music"));
    CHECK(engine.is_playing("wind"));
    CHECK(engine.active_voices() == 1);
    // stop_music leaves the queued mix to drain (the frame may carry an SFX);
    // only mute/reset drop it.
    CHECK(engine.queued_audio_bytes() > 0);
}

TEST_CASE("stopping the music leaves the queued SFX intact") {
    const TempDir dir;
    write_tone_wav(dir, "audio/test/score.wav", 2.0f, 0.5f, 1);
    write_tone_wav(dir, "audio/test/kill.wav", 2.0f, 0.4f, 1);
    const std::filesystem::path manifest =
        write_manifest(dir, {{"score", "audio/test/score.wav", 1, 96000, 1},
                             {"kill", "audio/test/kill.wav", 1, 96000, 0}});

    AudioEngine engine;
    REQUIRE_MESSAGE(engine.load_manifest(manifest.string(), dir.path().string()),
                    engine.last_error());
    engine.play_music("score");
    engine.play_ui("kill");  // the killing blow's SFX, queued the same frame
    REQUIRE(engine.queued_audio_bytes() > 0);

    engine.stop_music();
    CHECK_FALSE(engine.is_playing("score"));  // the music voice is inactive
    CHECK(engine.is_playing("kill"));
    const int after_first = engine.queued_audio_bytes();
    CHECK(after_first > 0);  // ~50 ms of lead: not chopped by the stop

    // The defeat path calls stop_music every frame: the second call (and every
    // later one) is a no-op. A flush here would read exactly 0.
    engine.stop_music();
    CHECK(engine.queued_audio_bytes() > 0);
    CHECK(engine.is_playing("kill"));

    // The SFX is still audible in the following window, and it is alone: the
    // 0.4 clip at the master stage peaks at 0.34, while the music (0.5 x 0.7)
    // plus SFX would peak near 0.64.
    REQUIRE_MESSAGE(engine.render_mixdown((dir.path() / "after-stop.wav").string(), 1),
                    engine.last_error());
    const AudioClip after = read_mixdown(dir.path() / "after-stop.wav");
    CHECK(peak(after) == doctest::Approx(0.4f * AudioEngine::kMasterGain).epsilon(0.02));
    CHECK(stereo_rms(after) > 0.1f);

    // reset() is still the call that drops the queue.
    engine.reset();
    CHECK(engine.queued_audio_bytes() == 0);
}

TEST_CASE("the dummy device gets a primed lead and clears on mute") {
    const TempDir dir;
    write_tone_wav(dir, "audio/test/device.wav", 2.0f, 0.5f, 1);
    const std::filesystem::path manifest =
        write_manifest(dir, {{"device", "audio/test/device.wav", 1, 96000, 0}});

    AudioEngine engine;
    REQUIRE_MESSAGE(engine.load_manifest(manifest.string(), dir.path().string()),
                    engine.last_error());
    REQUIRE(engine.queued_audio_bytes() == 0);  // no device before a play

    engine.play_ui("device");
    CHECK(engine.active_voices() == 1);
    CHECK(engine.queued_audio_bytes() > 0);  // ~3 blocks of lead

    engine.update(1.0f / 60.0f, Vec3{0.0f, 0.0f, 0.0f}, 0.0f);
    CHECK(engine.queued_audio_bytes() > 0);  // top-up keeps the lead
    CHECK(engine.active_voices() == 1);

    engine.set_muted(true);
    CHECK(engine.queued_audio_bytes() == 0);  // no mute trail
    engine.set_muted(false);
    engine.update(1.0f / 60.0f, Vec3{0.0f, 0.0f, 0.0f}, 0.0f);
    CHECK(engine.queued_audio_bytes() > 0);  // unmute keeps pushing

    engine.reset();
    CHECK(engine.queued_audio_bytes() == 0);  // reset clears too
    engine.update(1.0f / 60.0f, Vec3{0.0f, 0.0f, 0.0f}, 0.0f);
    CHECK(engine.queued_audio_bytes() > 0);  // top-up resumes even with no voice
}

TEST_CASE("the shipped audio manifest loads every entry") {
    const std::filesystem::path library = test_library();
    const std::filesystem::path manifest = library / "audio" / "audio.manifest";
    REQUIRE_MESSAGE(std::filesystem::is_regular_file(manifest),
                    ("missing " + manifest.string()).c_str());

    AudioEngine engine;
    REQUIRE_MESSAGE(engine.load_manifest(manifest.string(), library.string()),
                    engine.last_error());
    // The entry count is the library's slice: eight after the frozen S5
    // acceptance, ten after the S6 converter, 34 after the S8 converter
    // (the S6 ten + 24 event-variants). Anything else is drift, so pin the
    // three states and check the S6 rows are present exactly when it is not eight.
    std::size_t entries = 0;
    {
        std::ifstream in(manifest);
        REQUIRE_MESSAGE(in.good(), ("cannot read " + manifest.string()).c_str());
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("name=", 0) == 0) {
                ++entries;
            }
        }
    }
    const bool slice_size_ok = entries == 8 || entries == 10 || entries == 34;
    REQUIRE_MESSAGE(slice_size_ok,
                    ("expected the S5 eight, the S6 ten or the S8 34 manifest entries, found " +
                     std::to_string(entries))
                        .c_str());
    CHECK(engine.clip_count() == static_cast<int>(entries));
    if (entries == 34) {
        for (const char* name : {"agro.hoof_walk.0", "agro.hoof_walk.9", "agro.hoof_trot",
                                 "agro.hoof_gallop.0", "agro.hoof_gallop.4", "agro.neigh",
                                 "player.whistle.0", "player.whistle.2", "agro.mount",
                                 "player.kick", "player.stop_horse.0", "player.stop_horse.1"}) {
            CHECK_MESSAGE(engine.has(name), name);
        }
    }
    if (entries >= 10) {
        CHECK_MESSAGE(engine.has("valus.death"), "valus.death");
        CHECK_MESSAGE(engine.has("valus.club_swing"), "valus.club_swing");
    } else {
        CHECK_FALSE(engine.has("valus.death"));
        CHECK_FALSE(engine.has("valus.club_swing"));
    }
    for (const char* name : {"valus.hurt", "valus.impact", "valus.telegraph", "player.death",
                             "player.hit", "player.swing", "player.hurt", "music.boss"}) {
        CHECK_MESSAGE(engine.has(name), name);
    }
}

TEST_CASE("the capture API writes a non-silent 48 kHz WAV from played cues") {
    const TempDir dir;
    write_tone_wav(dir, "audio/test/tone.wav", 2.0f, 0.5f, 1);
    const std::filesystem::path manifest =
        write_manifest(dir, {{"tone", "audio/test/tone.wav", 1, 96000, 0}});

    AudioEngine engine;
    REQUIRE_MESSAGE(engine.load_manifest(manifest.string(), dir.path().string()),
                    engine.last_error());
    CHECK_FALSE(engine.capturing());
    CHECK_FALSE(engine.end_capture());  // nothing to finish

    const std::filesystem::path mix = dir.path() / "capture.wav";
    REQUIRE_MESSAGE(engine.begin_capture(mix.string()), engine.last_error());
    CHECK(engine.capturing());
    CHECK_FALSE(engine.begin_capture((dir.path() / "second.wav").string()));
    CHECK(engine.last_error().find("in progress") != std::string::npos);

    // The cue is played after the capture starts; no device may open.
    engine.play_ui("tone");
    CHECK(engine.queued_audio_bytes() == 0);
    for (int frame = 0; frame < 60; ++frame) {
        engine.update(1.0f / 60.0f, Vec3{0.0f, 0.0f, 0.0f}, 0.0f);
    }
    CHECK(engine.captured_frames() == 48000);
    REQUIRE_MESSAGE(engine.end_capture(), engine.last_error());
    CHECK_FALSE(engine.capturing());
    CHECK(engine.queued_audio_bytes() == 0);  // still no device

    const AudioClip clip = read_mixdown(mix);
    CHECK(clip.frames() == 48000);
    CHECK(stereo_rms(clip) > 0.05f);
    CHECK(peak(clip) > 0.1f);
    CHECK(peak(clip) <= 1.0f);

    // 60 frames at 1/60 s is exactly one second of 48 kHz stereo float32.
    CHECK(std::filesystem::file_size(mix) == 44u + 48000u * 2u * 4u);
}

TEST_CASE("the master gain keeps a music+SFX sum under 0.99") {
    const TempDir dir;
    // The realistic worst pair: a 0.5-peak music bed (category 0.7) under a
    // 0.8-peak one-shot (category 1.0) sums to 1.15 before the master stage.
    write_tone_wav(dir, "audio/test/score.wav", 1.0f, 0.5f, 1);
    write_tone_wav(dir, "audio/test/impact.wav", 1.0f, 0.8f, 1);
    write_tone_wav(dir, "audio/test/full.wav", 1.0f, 1.0f, 1);
    const std::filesystem::path manifest =
        write_manifest(dir, {{"score", "audio/test/score.wav", 1, 48000, 1},
                             {"impact", "audio/test/impact.wav", 1, 48000, 0},
                             {"full", "audio/test/full.wav", 1, 48000, 0}});

    AudioEngine engine;
    REQUIRE_MESSAGE(engine.load_manifest(manifest.string(), dir.path().string()),
                    engine.last_error());

    engine.play_music("score");
    engine.play_ui("impact");
    REQUIRE_MESSAGE(engine.render_mixdown((dir.path() / "sum.wav").string(), 1),
                    engine.last_error());
    const AudioClip sum = read_mixdown(dir.path() / "sum.wav");
    // Without the 0.85 master gain this block would peak at 1.15 (clamped);
    // with it the sum lands at 0.9775, inside the 0.99 delivery ceiling.
    CHECK(peak(sum) == doctest::Approx(AudioEngine::kMasterGain * 1.15f).epsilon(0.01));
    CHECK(peak(sum) <= 0.99f);

    // A lone full-scale sample proves the stage itself: exactly -1.41 dB.
    engine.reset();
    engine.play_ui("full");
    REQUIRE_MESSAGE(engine.render_mixdown((dir.path() / "full.wav").string(), 1),
                    engine.last_error());
    const AudioClip full = read_mixdown(dir.path() / "full.wav");
    CHECK(peak(full) == doctest::Approx(AudioEngine::kMasterGain).epsilon(0.001));
}
