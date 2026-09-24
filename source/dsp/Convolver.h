#pragma once

#include "Impulses.h"
#include "Spectrum.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace hl::dsp
{
/** Zero-latency convolution layout: taps [0, head) run as a direct FIR, [head, 2048) in
    128-sample FFT partitions and the rest in 2048-sample partitions. Each FFT stage starts
    exactly one block into the impulse, so its natural one-block latency lines up. */
struct ConvolutionLayout
{
    static constexpr int headSize = 128;
    static constexpr int block1 = 128;
    static constexpr int block2 = 2048;
    static constexpr int stage1Partitions = (block2 - block1) / block1; // taps 128 .. 2047
};

/** Frequency-domain partitions of one impulse segment (bins 0..B of 2B-point FFTs). */
struct PartitionedKernel
{
    int numPartitions = 0;
    std::vector<std::complex<float>> spectra;

    void build (const std::vector<float>& impulse, int start, int blockSize, int maxPartitions)
    {
        const int available = std::max (0, (int) impulse.size() - start);
        numPartitions = std::min (maxPartitions, (available + blockSize - 1) / blockSize);
        const int bins = blockSize + 1;
        spectra.assign ((size_t) (numPartitions * bins), {});

        Fft fft (fftOrderFor (2 * blockSize));
        std::vector<std::complex<float>> work ((size_t) (2 * blockSize));

        for (int p = 0; p < numPartitions; ++p)
        {
            std::fill (work.begin(), work.end(), std::complex<float> {});

            for (int i = 0; i < blockSize; ++i)
            {
                const int tapIndex = start + p * blockSize + i;

                if (tapIndex < (int) impulse.size())
                    work[(size_t) i] = impulse[(size_t) tapIndex];
            }

            fft.forward (work.data());
            std::copy_n (work.begin(), bins, spectra.begin() + p * bins);
        }
    }
};

/** A stereo impulse prepared for the engine. Built off the audio thread. */
struct ConvolutionKernel
{
    uint32_t generation = 0;
    double sampleRate = 0.0;
    std::array<std::vector<float>, 2> head;
    std::array<PartitionedKernel, 2> stage1, stage2;

    static std::unique_ptr<ConvolutionKernel> build (const StereoImpulse& impulse, double rate, uint32_t generation)
    {
        using L = ConvolutionLayout;
        auto k = std::make_unique<ConvolutionKernel>();
        k->generation = generation;
        k->sampleRate = rate;
        const int maxStage2 = (int) std::ceil (kMaxImpulseSeconds * rate / L::block2) + 1;

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& h = impulse[(size_t) ch];
            k->head[(size_t) ch].assign (L::headSize, 0.0f);
            std::copy_n (h.begin(), std::min ((int) h.size(), L::headSize), k->head[(size_t) ch].begin());
            k->stage1[(size_t) ch].build (h, L::block1, L::block1, L::stage1Partitions);
            k->stage2[(size_t) ch].build (h, L::block2, L::block2, maxStage2);
        }

        return k;
    }
};

/** One uniformly partitioned overlap-save stage for one channel, with two kernel slots so a new
    impulse can be crossfaded in while the old one rings out. */
class ConvolutionStage
{
public:
    void prepare (int newBlockSize, int maxPartitions)
    {
        blockSize = newBlockSize;
        bins = blockSize + 1;
        fdlSize = std::max (1, maxPartitions);
        fft = std::make_unique<Fft> (fftOrderFor (2 * blockSize));
        input.assign ((size_t) (2 * blockSize), 0.0f);
        fdl.assign ((size_t) (fdlSize * bins), {});
        work.assign ((size_t) (2 * blockSize), {});
        accum.assign ((size_t) bins, {});

        for (auto& o : output)
            o.assign ((size_t) blockSize, 0.0f);

        reset();
    }

    void reset() noexcept
    {
        std::fill (input.begin(), input.end(), 0.0f);
        std::fill (fdl.begin(), fdl.end(), std::complex<float> {});

        for (auto& o : output)
            std::fill (o.begin(), o.end(), 0.0f);

        pos = 0;
        fdlPos = 0;
        blocksSeen = 0;
    }

    /** Returns the outputs of both kernel slots for this sample, then consumes the input. */
    void process (float x, const std::array<const PartitionedKernel*, 2>& kernels, float& y0, float& y1) noexcept
    {
        y0 = output[0][(size_t) pos];
        y1 = output[1][(size_t) pos];
        input[(size_t) (blockSize + pos)] = x;

        if (++pos == blockSize)
        {
            pos = 0;
            transformInput();

            for (int s = 0; s < 2; ++s)
                if (kernels[(size_t) s] != nullptr)
                    render (*kernels[(size_t) s], output[(size_t) s]);

            std::copy (input.begin() + blockSize, input.end(), input.begin());
        }
    }

    /** Recomputes the pending output block of `slot` with a new kernel, from the stored input
        spectra, so a freshly installed impulse is complete from its very first sample. */
    void refresh (int slot, const PartitionedKernel* kernel) noexcept
    {
        auto& out = output[(size_t) slot];

        if (kernel == nullptr || blocksSeen == 0)
            std::fill (out.begin(), out.end(), 0.0f);
        else
            render (*kernel, out); // the pending block was rendered from the newest spectrum
    }

    void clearSlot (int slot) noexcept { std::fill (output[(size_t) slot].begin(), output[(size_t) slot].end(), 0.0f); }

private:
    void transformInput() noexcept
    {
        for (int i = 0; i < 2 * blockSize; ++i)
            work[(size_t) i] = input[(size_t) i];

        fft->forward (work.data());
        fdlPos = (fdlPos + 1) % fdlSize;
        std::copy_n (work.begin(), bins, fdl.begin() + fdlPos * bins);
        ++blocksSeen;
    }

    void render (const PartitionedKernel& kernel, std::vector<float>& out) noexcept
    {
        std::fill (accum.begin(), accum.end(), std::complex<float> {});
        const int parts = std::min (kernel.numPartitions, fdlSize);

        for (int p = 0; p < parts; ++p)
        {
            const auto* x = fdl.data() + ((fdlPos - p + fdlSize) % fdlSize) * bins;
            const auto* h = kernel.spectra.data() + p * bins;

            for (int k = 0; k < bins; ++k)
                accum[(size_t) k] += x[k] * h[k];
        }

        for (int k = 0; k < bins; ++k)
            work[(size_t) k] = accum[(size_t) k];

        for (int k = bins; k < 2 * blockSize; ++k)
            work[(size_t) k] = std::conj (accum[(size_t) (2 * blockSize - k)]);

        fft->inverse (work.data());

        for (int i = 0; i < blockSize; ++i)
            out[(size_t) i] = work[(size_t) (blockSize + i)].real();
    }

    int blockSize = 128, bins = 129, fdlSize = 1, pos = 0, fdlPos = 0;
    int64_t blocksSeen = 0;
    std::unique_ptr<Fft> fft;
    std::vector<float> input;
    std::vector<std::complex<float>> fdl, work, accum;
    std::array<std::vector<float>, 2> output;
};

/** Stereo zero-latency convolver with click-free kernel changes. Audio thread only. */
class Convolver
{
public:
    void prepare (double rate)
    {
        using L = ConvolutionLayout;
        sampleRate = rate;
        const int maxStage2 = (int) std::ceil (kMaxImpulseSeconds * rate / L::block2) + 1;

        for (auto& c : channels)
        {
            c.stage1.prepare (L::block1, L::stage1Partitions);
            c.stage2.prepare (L::block2, maxStage2);
            c.history.assign ((size_t) (2 * L::headSize), 0.0f);
        }

        fadeLength = std::max (1, (int) (0.05 * rate));
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : channels)
        {
            c.stage1.reset();
            c.stage2.reset();
            std::fill (c.history.begin(), c.history.end(), 0.0f);
            c.historyPos = 0;
        }

        fadePos = fadeLength;
    }

    ConvolutionKernel* getCurrent() const noexcept { return slots[(size_t) current]; }
    bool isFading() const noexcept { return fadePos < fadeLength; }

    /** Removes both kernels and hands them back to the caller. */
    std::array<ConvolutionKernel*, 2> releaseAll() noexcept
    {
        auto out = slots;
        slots = {};

        for (auto& c : channels)
            for (int s = 0; s < 2; ++s)
            {
                c.stage1.clearSlot (s);
                c.stage2.clearSlot (s);
            }

        fadePos = fadeLength;
        return out;
    }

    /** Installs `kernel` as the current one; the previous current kernel fades out and is later
        handed back by takeRetired(). Call only when the non-current slot is empty. */
    void install (ConvolutionKernel* kernel) noexcept
    {
        const int next = 1 - current;
        slots[(size_t) next] = kernel;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& c = channels[(size_t) ch];
            c.stage1.refresh (next, kernel != nullptr ? &kernel->stage1[(size_t) ch] : nullptr);
            c.stage2.refresh (next, kernel != nullptr ? &kernel->stage2[(size_t) ch] : nullptr);
        }

        current = next;
        fadePos = slots[(size_t) (1 - current)] != nullptr ? 0 : fadeLength;
    }

    bool hasFreeSlot() const noexcept { return slots[(size_t) (1 - current)] == nullptr; }

    /** After a crossfade has finished, hands back the kernel that faded out (else nullptr). */
    ConvolutionKernel* takeRetired() noexcept
    {
        if (isFading())
            return nullptr;

        const int old = 1 - current;
        auto* k = slots[(size_t) old];
        slots[(size_t) old] = nullptr;

        for (auto& c : channels)
        {
            c.stage1.clearSlot (old);
            c.stage2.clearSlot (old);
        }

        return k;
    }

    void process (float* left, float* right, int n) noexcept
    {
        using L = ConvolutionLayout;
        float* chans[2] = { left, right };
        const auto* cur = slots[(size_t) current];
        const auto* old = slots[(size_t) (1 - current)];

        for (int i = 0; i < n; ++i)
        {
            float gCur = 1.0f, gOld = 0.0f;

            if (fadePos < fadeLength)
            {
                gCur = (float) fadePos / (float) fadeLength;
                gOld = 1.0f - gCur;
                ++fadePos;
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                auto& c = channels[(size_t) ch];
                const float x = chans[ch][i];

                c.historyPos = (c.historyPos - 1 + L::headSize) % L::headSize;
                c.history[(size_t) c.historyPos] = c.history[(size_t) (c.historyPos + L::headSize)] = x;
                const float* hist = c.history.data() + c.historyPos;

                std::array<const PartitionedKernel*, 2> k1 {}, k2 {};
                float y = 0.0f;
                const auto* kernels = &slots[0];

                for (int s = 0; s < 2; ++s)
                {
                    const auto* kern = kernels[s];

                    if (kern == nullptr)
                        continue;

                    const float g = kern == cur ? gCur : (kern == old ? gOld : 0.0f);
                    k1[(size_t) s] = &kern->stage1[(size_t) ch];
                    k2[(size_t) s] = &kern->stage2[(size_t) ch];

                    if (g > 0.0f)
                    {
                        const float* h = kern->head[(size_t) ch].data();
                        float acc = 0.0f;

                        for (int t = 0; t < L::headSize; ++t)
                            acc += h[t] * hist[t];

                        y += g * acc;
                    }
                }

                float a0, a1, b0, b1;
                c.stage1.process (x, k1, a0, a1);
                c.stage2.process (x, k2, b0, b1);
                const float g0 = slots[0] == cur ? gCur : gOld, g1 = slots[1] == cur ? gCur : gOld;
                y += g0 * (a0 + b0) + g1 * (a1 + b1);
                chans[ch][i] = y;
            }
        }
    }

private:
    struct Channel
    {
        ConvolutionStage stage1, stage2;
        std::vector<float> history;
        int historyPos = 0;
    };

    double sampleRate = 44100.0;
    std::array<Channel, 2> channels;
    std::array<ConvolutionKernel*, 2> slots {};
    int current = 0, fadePos = 0, fadeLength = 1;
};

//==============================================================================
struct ConvolveSettings
{
    int impulse = (int) IrType::cab1x12;
    float size = 1.0f;     // 0.25 .. 2
    float damp = 0.3f;     // 0 .. 1
    bool reverse = false;
    float mix = 0.5f;
};

/** Convolution module. Impulses are synthesised and partitioned on a worker thread (see
    service()); the audio thread only swaps pointers. */
class ConvolveModule
{
public:
    ~ConvolveModule()
    {
        auto* p = pending.exchange (nullptr);
        auto* r = retired.exchange (nullptr);
        delete p;
        delete r;
        delete holding;

        for (auto* k : convolver.releaseAll())
            delete k;
    }

    /** Builds the current impulse synchronously (not on the audio thread). */
    void prepare (double rate, int)
    {
        sampleRate = rate;
        workerRate.store (rate);
        const auto gen = ++generation;
        const uint32_t spec = pack (settings);
        wanted.store (((uint64_t) gen << 32) | spec);

        convolver.prepare (rate);

        for (auto* k : convolver.releaseAll())
            delete k;

        delete pending.exchange (nullptr);
        delete retired.exchange (nullptr);
        delete holding;
        holding = nullptr;

        auto impulse = makeImpulse (settings.impulse, settings.size, settings.damp, settings.reverse, rate);
        publishForDisplay (impulse, spec, rate);
        convolver.install (ConvolutionKernel::build (impulse, rate, gen).release());

        mixSmoother.reset (rate, 0.03);
        mixSmoother.setCurrentAndTarget (settings.mix);
    }

    void reset() noexcept { convolver.reset(); }

    void setSettings (const ConvolveSettings& s) noexcept
    {
        settings = s;
        mixSmoother.setTarget (std::clamp (s.mix, 0.0f, 1.0f));
        const uint64_t w = ((uint64_t) generation << 32) | pack (s);

        if (w != wanted.load (std::memory_order_relaxed))
            wanted.store (w);
    }

    void process (float* left, float* right, int n) noexcept
    {
        collectKernels();

        float dryL[64], dryR[64];

        for (int start = 0; start < n; start += 64)
        {
            const int m = std::min (64, n - start);
            std::copy_n (left + start, m, dryL);
            std::copy_n (right + start, m, dryR);
            convolver.process (left + start, right + start, m);

            for (int i = 0; i < m; ++i)
            {
                const float w = mixSmoother.next();
                left[start + i] = dryL[i] + w * (left[start + i] - dryL[i]);
                right[start + i] = dryR[i] + w * (right[start + i] - dryR[i]);
            }
        }
    }

    /** Worker thread: builds wanted kernels and frees retired ones. Returns true if it did work. */
    bool service()
    {
        bool worked = false;

        if (auto* r = retired.exchange (nullptr))
        {
            delete r;
            worked = true;
        }

        const uint64_t w = wanted.load();

        if (w != lastBuilt.load())
        {
            const auto spec = (uint32_t) (w & 0xffffffffu);
            const auto gen = (uint32_t) (w >> 32);
            const auto s = unpack (spec);
            const double rate = workerRate.load();
            auto impulse = makeImpulse (s.impulse, s.size, s.damp, s.reverse, rate);
            auto kernel = ConvolutionKernel::build (impulse, rate, gen);
            publishForDisplay (impulse, spec, rate);
            delete pending.exchange (kernel.release());
            lastBuilt.store (w);
            worked = true;
        }

        return worked;
    }

    /** 1/3-octave response of the most recently built impulse. */
    loudness::ImpulseBands getResponseBands() const noexcept
    {
        loudness::ImpulseBands b;

        for (int i = 0; i < loudness::kBands; ++i)
        {
            b.power[(size_t) i] = responsePower[(size_t) i].load (std::memory_order_relaxed);
            b.real[(size_t) i] = responseReal[(size_t) i].load (std::memory_order_relaxed);
        }

        return b;
    }

    /** True once after a new impulse's response was published. */
    bool takeResponseChanged() noexcept { return responseChanged.exchange (false); }

    /** True when the wanted impulse has been built and picked up by the audio thread. */
    bool isSettled() const noexcept { return wanted.load() == lastBuilt.load() && pending.load() == nullptr; }

    /** Runs service() until nothing is left to do and the audio side has picked everything up
        (tests and offline tools). */
    void settleForTesting()
    {
        service();
        collectKernels();
    }

    /** The impulse currently being played (for the display); may be briefly behind. */
    std::shared_ptr<const StereoImpulse> getDisplayImpulse() const
    {
        std::lock_guard<std::mutex> lock (displayMutex);
        return displayImpulse;
    }

    uint32_t getDisplaySpec() const
    {
        std::lock_guard<std::mutex> lock (displayMutex);
        return displaySpec;
    }

    static uint32_t pack (const ConvolveSettings& s) noexcept
    {
        const auto imp = (uint32_t) std::clamp (s.impulse, 0, kNumImpulses - 1);
        const auto size = (uint32_t) std::lround (std::clamp (s.size, 0.25f, 2.0f) * 100.0f);  // 25..200
        const auto damp = (uint32_t) std::lround (std::clamp (s.damp, 0.0f, 1.0f) * 100.0f);  // 0..100
        return imp | (size << 6) | (damp << 14) | ((s.reverse ? 1u : 0u) << 21);
    }

    static ConvolveSettings unpack (uint32_t p) noexcept
    {
        ConvolveSettings s;
        s.impulse = (int) (p & 63u);
        s.size = (float) ((p >> 6) & 255u) * 0.01f;
        s.damp = (float) ((p >> 14) & 127u) * 0.01f;
        s.reverse = ((p >> 21) & 1u) != 0;
        return s;
    }

private:
    /** Audio thread: retires a finished fade and installs a waiting kernel. Never blocks or frees. */
    void collectKernels() noexcept
    {
        if (holding == nullptr)
            holding = convolver.takeRetired();

        if (holding != nullptr)
        {
            ConvolutionKernel* expected = nullptr;

            if (retired.compare_exchange_strong (expected, holding))
                holding = nullptr;
        }

        if (holding != nullptr || convolver.isFading() || ! convolver.hasFreeSlot())
            return;

        if (auto* k = pending.exchange (nullptr))
        {
            if (k->generation != generation || k->sampleRate != sampleRate)
                holding = k; // stale (built for an old sample rate): retire it
            else
                convolver.install (k);
        }
    }

    void publishForDisplay (const StereoImpulse& impulse, uint32_t spec, double rate)
    {
        // Frequency response for the level estimate (read lock-free by the audio thread)
        const auto response = loudness::impulseBands<Fft> (impulse[0], impulse[1], rate);

        for (int i = 0; i < loudness::kBands; ++i)
        {
            responsePower[(size_t) i].store ((float) response.power[(size_t) i], std::memory_order_relaxed);
            responseReal[(size_t) i].store ((float) response.real[(size_t) i], std::memory_order_relaxed);
        }

        responseChanged.store (true);

        auto copy = std::make_shared<const StereoImpulse> (impulse);
        std::lock_guard<std::mutex> lock (displayMutex);
        displayImpulse = std::move (copy);
        displaySpec = spec;
    }

    double sampleRate = 44100.0;
    ConvolveSettings settings;
    Convolver convolver;
    LinearSmoother mixSmoother;

    uint32_t generation = 0;
    std::atomic<uint64_t> wanted { 0 };
    std::atomic<double> workerRate { 44100.0 };
    std::atomic<uint64_t> lastBuilt { 0 }; // written by the worker thread only
    std::atomic<ConvolutionKernel*> pending { nullptr }, retired { nullptr };
    ConvolutionKernel* holding = nullptr; // audio thread: waiting to be handed to the worker

    std::array<std::atomic<float>, loudness::kBands> responsePower {}, responseReal {};
    std::atomic<bool> responseChanged { false };

    mutable std::mutex displayMutex;
    std::shared_ptr<const StereoImpulse> displayImpulse;
    uint32_t displaySpec = 0xffffffffu;
};

} // namespace hl::dsp
