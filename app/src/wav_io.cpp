#include "wav_io.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

namespace {

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

std::string path_error(const std::filesystem::path& path, const std::string& reason) {
    return path.string() + ": " + reason;
}

std::uint16_t read_le16(const unsigned char* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(bytes[1] << 8);
}

std::uint32_t read_le32(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
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

bool read_file(const std::filesystem::path& path, std::vector<unsigned char>& bytes,
               std::string* error) {
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(path, file_error)) {
        set_error(error, path_error(path, "cannot open WAV file"));
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        set_error(error, path_error(path, "cannot open WAV file"));
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        set_error(error, path_error(path, "read error"));
        return false;
    }
    in.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        in.read(reinterpret_cast<char*>(bytes.data()), size);
    }
    if (!in) {
        set_error(error, path_error(path, "read error"));
        return false;
    }
    return true;
}

}  // namespace

bool read_wav(const std::filesystem::path& path, AudioClip& out, std::string* error) {
    std::vector<unsigned char> bytes;
    if (!read_file(path, bytes, error)) {
        return false;
    }
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        set_error(error, path_error(path, "not a RIFF/WAVE file"));
        return false;
    }

    bool have_format = false;
    bool have_data = false;
    std::uint16_t format_tag = 0;
    std::uint16_t channels = 0;
    std::uint16_t block_align = 0;
    std::uint16_t bits = 0;
    std::uint32_t rate = 0;
    const unsigned char* data = nullptr;
    std::size_t data_bytes = 0;

    std::size_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        const unsigned char* header = bytes.data() + offset;
        const std::uint32_t chunk_size = read_le32(header + 4);
        const std::size_t payload = offset + 8;
        if (chunk_size > bytes.size() - payload) {
            set_error(error, path_error(path, "truncated WAV chunk"));
            return false;
        }
        if (std::memcmp(header, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                set_error(error, path_error(path, "fmt chunk is shorter than 16 bytes"));
                return false;
            }
            const unsigned char* fmt = bytes.data() + payload;
            format_tag = read_le16(fmt + 0);
            channels = read_le16(fmt + 2);
            rate = read_le32(fmt + 4);
            block_align = read_le16(fmt + 12);
            bits = read_le16(fmt + 14);
            if (format_tag == 0xFFFE) {  // WAVE_FORMAT_EXTENSIBLE: the sub-format GUID
                if (chunk_size < 40) {   // starts with the ordinary format tag.
                    set_error(error,
                              path_error(path, "extensible fmt chunk is shorter than 40 bytes"));
                    return false;
                }
                format_tag = read_le16(fmt + 24);
            }
            have_format = true;
        } else if (std::memcmp(header, "data", 4) == 0) {
            data = bytes.data() + payload;
            data_bytes = chunk_size;
            have_data = true;
        }
        // RIFF chunks are word-aligned: an odd payload is followed by a pad byte.
        offset = payload + chunk_size + (chunk_size & 1u);
    }

    if (!have_format) {
        set_error(error, path_error(path, "WAV has no fmt chunk"));
        return false;
    }
    if (!have_data) {
        set_error(error, path_error(path, "WAV has no data chunk"));
        return false;
    }
    if (channels != 1 && channels != 2) {
        set_error(error, path_error(path, "unsupported channel count " +
                                             std::to_string(channels) +
                                             " (mono or stereo only)"));
        return false;
    }
    if (rate != 48000) {
        set_error(error, path_error(path, "sample rate " + std::to_string(rate) +
                                             " Hz is not 48 kHz (the mixer does not resample)"));
        return false;
    }
    const bool is_float = format_tag == 3 && bits == 32;
    const bool is_pcm16 = format_tag == 1 && bits == 16;
    if (!is_float && !is_pcm16) {
        set_error(error, path_error(path, "unsupported WAV encoding (format tag " +
                                             std::to_string(format_tag) + ", " +
                                             std::to_string(bits) + "-bit); float32 or PCM16"));
        return false;
    }
    const std::size_t bytes_per_sample = is_float ? 4u : 2u;
    const std::size_t frame_bytes = bytes_per_sample * channels;
    if (block_align != frame_bytes) {
        set_error(error, path_error(path, "fmt block alignment " +
                                             std::to_string(block_align) +
                                             " does not match " + std::to_string(channels) +
                                             " x " + std::to_string(bytes_per_sample) + " bytes"));
        return false;
    }
    if (data_bytes % frame_bytes != 0) {
        set_error(error, path_error(path, "data chunk is not a whole number of frames"));
        return false;
    }
    const std::size_t frames = data_bytes / frame_bytes;

    out.samples.resize(frames * channels);
    for (std::size_t i = 0; i < out.samples.size(); ++i) {
        if (is_float) {
            const std::uint32_t raw = read_le32(data + i * 4);
            out.samples[i] = std::bit_cast<float>(raw);
        } else {
            const auto raw = static_cast<std::int16_t>(read_le16(data + i * 2));
            out.samples[i] = static_cast<float>(raw) / 32768.0f;
        }
    }
    out.channels = static_cast<int>(channels);
    out.rate = 48000;
    out.loop = false;
    return true;
}

bool write_wav(const std::filesystem::path& path, const std::vector<float>& samples,
               int channels, int rate, std::string* error) {
    if (channels != 1 && channels != 2) {
        set_error(error, path_error(path, "channel count must be 1 or 2, got " +
                                             std::to_string(channels)));
        return false;
    }
    if (rate <= 0) {
        set_error(error, path_error(path, "sample rate must be positive"));
        return false;
    }
    if (samples.size() % static_cast<std::size_t>(channels) != 0) {
        set_error(error, path_error(path, "sample count is not a whole number of frames"));
        return false;
    }
    const std::size_t data_bytes = samples.size() * 4u;
    if (data_bytes > std::numeric_limits<std::uint32_t>::max() - 36u) {
        set_error(error, path_error(path, "too large for a RIFF/WAVE file"));
        return false;
    }

    std::vector<unsigned char> bytes;
    bytes.reserve(44 + data_bytes);
    append_id(bytes, "RIFF");
    append_le32(bytes, static_cast<std::uint32_t>(36 + data_bytes));
    append_id(bytes, "WAVE");
    append_id(bytes, "fmt ");
    append_le32(bytes, 16);
    append_le16(bytes, 3);  // IEEE float
    append_le16(bytes, static_cast<std::uint16_t>(channels));
    append_le32(bytes, static_cast<std::uint32_t>(rate));
    const auto block_align =
        static_cast<std::uint16_t>(channels * 4);
    append_le32(bytes, static_cast<std::uint32_t>(rate) * block_align);
    append_le16(bytes, block_align);
    append_le16(bytes, 32);
    append_id(bytes, "data");
    append_le32(bytes, static_cast<std::uint32_t>(data_bytes));
    for (const float sample : samples) {
        append_le32(bytes, std::bit_cast<std::uint32_t>(sample));
    }

    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            set_error(error, path_error(path, "cannot create " + parent.string() + ": " +
                                                 ec.message()));
            return false;
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        set_error(error, path_error(path, "cannot open for writing"));
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        set_error(error, path_error(path, "write error"));
        return false;
    }
    return true;
}
