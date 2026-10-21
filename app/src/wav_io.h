#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "audio.h"

// Minimal RIFF/WAVE I/O for the S5 audio slice. The mixer owns its own
// 48 kHz float representation, so the reader accepts exactly what the
// converter produces (IEEE float32, fmt tag 3) plus PCM16 (tag 1) for
// robustness; mono or stereo, including the WAVE_FORMAT_EXTENSIBLE wrappers of
// either. Any rate other than 48 kHz is an error — the mixer does not resample.

// Reads `path` into `out` as interleaved float32. Returns false and fills
// `error` (when non-null) with a message that names the path and the reason.
bool read_wav(const std::filesystem::path& path, AudioClip& out,
              std::string* error = nullptr);

// Writes interleaved float32 frames as a RIFF/WAVE file (fmt tag 3). The
// mixdown calls this with 2 channels at 48000; tests use it to build monos.
bool write_wav(const std::filesystem::path& path, const std::vector<float>& samples,
               int channels, int rate, std::string* error = nullptr);
