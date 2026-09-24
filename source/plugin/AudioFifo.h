#pragma once

#include <juce_core/juce_core.h>

namespace hl
{
/** Lock-free single-producer / single-consumer sample FIFO (audio thread -> UI thread).
    Holds `numChannels` interleaved channels; when full, new samples are dropped. */
class AudioFifo
{
public:
    AudioFifo (int numChannelsToUse, int capacityFrames)
        : numChannels (numChannelsToUse), fifo (capacityFrames), buffer ((size_t) (capacityFrames * numChannelsToUse), 0.0f)
    {
    }

    /** Audio thread. `channels` holds numChannels pointers. */
    void push (const float* const* channels, int numFrames) noexcept
    {
        const auto scope = fifo.write (std::min (numFrames, fifo.getFreeSpace()));
        copyIn (channels, 0, scope.startIndex1, scope.blockSize1);
        copyIn (channels, scope.blockSize1, scope.startIndex2, scope.blockSize2);
    }

    /** UI thread. Calls fn (const float* interleavedFrame) for each available frame. */
    template <typename Fn>
    void pop (Fn&& fn)
    {
        const auto scope = fifo.read (fifo.getNumReady());

        for (int i = 0; i < scope.blockSize1; ++i)
            fn (buffer.data() + (size_t) ((scope.startIndex1 + i) * numChannels));

        for (int i = 0; i < scope.blockSize2; ++i)
            fn (buffer.data() + (size_t) ((scope.startIndex2 + i) * numChannels));
    }

    int getNumChannels() const noexcept { return numChannels; }

private:
    void copyIn (const float* const* channels, int srcOffset, int start, int count) noexcept
    {
        for (int i = 0; i < count; ++i)
            for (int ch = 0; ch < numChannels; ++ch)
                buffer[(size_t) ((start + i) * numChannels + ch)] = channels[ch][srcOffset + i];
    }

    int numChannels;
    juce::AbstractFifo fifo;
    std::vector<float> buffer;
};

} // namespace hl
