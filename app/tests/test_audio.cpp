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
        write_tone_wav(dir, file, 0.05f, 0.1f, 1);
        entries.push_back({name, file, 1, 2400, 0});
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
    engine.play_music("music");
    CHECK(engine.active_voices() == 1);  // a second cue replaces the first
    engine.play_ambience("wind");
    CHECK(engine.active_voices() == 2);  // separate slots
    engine.stop_music();
    CHECK_FALSE(engine.is_playing("music"));
    CHECK(engine.is_playing("wind"));
    CHECK(engine.active_voices() == 1);
}

TEST_CASE("the shipped audio manifest loads every entry") {
    const std::filesystem::path library = test_library();
    const std::filesystem::path manifest = library / "audio" / "audio.manifest";
    REQUIRE_MESSAGE(std::filesystem::is_regular_file(manifest),
                    ("missing " + manifest.string()).c_str());

    AudioEngine engine;
    REQUIRE_MESSAGE(engine.load_manifest(manifest.string(), library.string()),
                    engine.last_error());
    CHECK(engine.clip_count() == 8);
    for (const char* name : {"valus.hurt", "valus.impact", "valus.telegraph", "player.death",
                             "player.hit", "player.swing", "player.hurt", "music.boss"}) {
        CHECK_MESSAGE(engine.has(name), name);
    }
}
