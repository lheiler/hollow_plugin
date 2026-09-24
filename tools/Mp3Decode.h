#pragma once

// Tiny MP3 -> float stereo helper for the offline corpus tools (minimp3, public domain).
// Define HL_MP3_IMPLEMENTATION in exactly one translation unit.

#ifdef HL_MP3_IMPLEMENTATION
 #define MINIMP3_IMPLEMENTATION
#endif
#define MINIMP3_FLOAT_OUTPUT
#include "third_party/minimp3.h"
#include "third_party/minimp3_ex.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace hl::tools
{
struct StereoAudio
{
    std::vector<float> left, right;
    double sampleRate = 0.0;
};

inline bool decodeMp3 (const std::string& path, StereoAudio& out)
{
    mp3dec_t decoder;
    mp3dec_file_info_t info {};

    if (mp3dec_load (&decoder, path.c_str(), &info, nullptr, nullptr) != 0 || info.samples == 0 || info.hz == 0)
    {
        std::free (info.buffer);
        return false;
    }

    const int channels = info.channels;
    const size_t frames = info.samples / (size_t) channels;
    out.left.resize (frames);
    out.right.resize (frames);

    for (size_t i = 0; i < frames; ++i)
    {
        out.left[i] = info.buffer[i * (size_t) channels];
        out.right[i] = info.buffer[i * (size_t) channels + (channels > 1 ? 1 : 0)];
    }

    out.sampleRate = info.hz;
    std::free (info.buffer);
    return true;
}
} // namespace hl::tools
