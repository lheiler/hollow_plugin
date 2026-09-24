#pragma once

#include "Fft.h"
#include "Svf.h"

#include <complex>

namespace hl::dsp
{
/** Synthetic impulse responses: every "space" is modelled (filters, modal resonators, echoes, noise
    tails), so there are no sample files and `size` can genuinely rescale the object. */
enum class IrType
{
    cab1x12, cab4x12, tweed, radio, telephone, megaphone, tornCone,
    tinCan, metalBowl, steelPipe, glassJar, cardboardBox, gong,
    springTank, steelPlate,
    closet, tiledRoom, tunnel, concreteHall, cistern,
    count
};

constexpr int kNumImpulses = (int) IrType::count;
constexpr double kMaxImpulseSeconds = 2.0;

struct ImpulseInfo
{
    const char* name;
    const char* family;
};

inline const ImpulseInfo& impulseInfo (int index) noexcept
{
    static const ImpulseInfo infos[kNumImpulses] = {
        { "Cab 1x12", "Speaker" }, { "Cab 4x12", "Speaker" }, { "Tweed Combo", "Speaker" }, { "Radio", "Speaker" },
        { "Telephone", "Speaker" }, { "Megaphone", "Speaker" }, { "Torn Cone", "Speaker" },
        { "Tin Can", "Object" }, { "Metal Bowl", "Object" }, { "Steel Pipe", "Object" }, { "Glass Jar", "Object" },
        { "Cardboard Box", "Object" }, { "Gong", "Object" },
        { "Spring Tank", "Mechanical" }, { "Steel Plate", "Mechanical" },
        { "Closet", "Space" }, { "Tiled Room", "Space" }, { "Tunnel", "Space" }, { "Concrete Hall", "Space" }, { "Cistern", "Space" },
    };

    return infos[std::clamp (index, 0, kNumImpulses - 1)];
}

using StereoImpulse = std::array<std::vector<float>, 2>;

namespace ir
{
    using Buffer = std::vector<float>;

    inline int samples (double seconds, double rate) { return std::max (1, (int) std::lround (seconds * rate)); }

    inline void filter (Buffer& b, SvfShape shape, double freq, double q, double gainDb, double rate)
    {
        const auto c = SvfCoeffs::make (shape, std::min (freq, rate * 0.45), q, gainDb, rate);
        SvfState s;

        for (auto& x : b)
            x = s.process (c, x);
    }

    inline void tap (Buffer& b, double ms, float gain, double rate)
    {
        const auto i = (size_t) std::lround (ms * 0.001 * rate);

        if (i < b.size())
            b[i] += gain;
    }

    /** Adds a decaying sinusoid (one resonant mode) via a rotating phasor. */
    inline void mode (Buffer& b, double freq, double t60, float amp, float phase, double rate)
    {
        if (freq <= 20.0 || freq >= rate * 0.45)
            return;

        const double decay = std::exp (-6.907755 / (std::max (0.005, t60) * rate));
        const std::complex<double> rot = std::polar (decay, 2.0 * kPi * freq / rate);
        std::complex<double> z = std::polar ((double) amp, (double) phase);
        const auto len = std::min (b.size(), (size_t) (t60 * 1.2 * rate) + 1);

        for (size_t i = 0; i < len; ++i)
        {
            b[i] += (float) z.imag();
            z *= rot;
        }
    }

    /** Exponentially decaying noise from `startMs`, with a short fade-in. */
    inline void noiseTail (Buffer& b, double startMs, double t60, float amp, Random& rng, double rate, double fadeInMs = 4.0)
    {
        const auto start = (size_t) std::lround (startMs * 0.001 * rate);
        const double decay = std::exp (-6.907755 / (std::max (0.01, t60) * rate));
        const double fadeIn = std::max (1.0, fadeInMs * 0.001 * rate);
        double env = amp;

        for (size_t i = start; i < b.size(); ++i)
        {
            const double k = (double) (i - start);
            b[i] += (float) (env * std::min (1.0, k / fadeIn)) * rng.nextBipolar();
            env *= decay;
        }
    }

    /** Scattered early reflections between `fromMs` and `toMs`. */
    inline void reflections (Buffer& b, int count, double fromMs, double toMs, float gain, Random& rng, double rate)
    {
        for (int i = 0; i < count; ++i)
        {
            const double t = fromMs + (toMs - fromMs) * std::pow (rng.nextFloat(), 0.7);
            const float g = gain * (0.4f + 0.6f * rng.nextFloat()) * (float) std::pow (0.5, (t - fromMs) / std::max (1.0, toMs - fromMs));
            tap (b, t, rng.nextFloat() < 0.5f ? -g : g, rate);
        }
    }

    struct Cabinet
    {
        double hp, bump, bumpDb, dip, dipDb, peak, peakQ, peakDb, lp;
        bool steep;
    };

    inline Buffer cabinet (const Cabinet& c, double size, double rate, Random& rng, std::initializer_list<std::pair<double, float>> taps)
    {
        Buffer b ((size_t) samples (0.06, rate), 0.0f);
        b[0] = 1.0f;

        for (auto [ms, g] : taps)
            tap (b, ms * size, g, rate);

        // tiny random cone ripple
        for (int i = 0; i < 6; ++i)
            tap (b, 0.05 + 0.6 * rng.nextFloat(), 0.04f * rng.nextBipolar(), rate);

        const double s = 1.0 / size;
        filter (b, SvfShape::highPass, c.hp * s, 0.75, 0.0, rate);
        filter (b, SvfShape::bell, c.bump * s, 1.4, c.bumpDb, rate);
        filter (b, SvfShape::bell, c.dip * s, 1.0, c.dipDb, rate);
        filter (b, SvfShape::bell, c.peak * std::sqrt (s), c.peakQ, c.peakDb, rate);
        filter (b, SvfShape::lowPass, c.lp * std::pow (s, 0.3), 0.7, 0.0, rate);

        if (c.steep)
            filter (b, SvfShape::lowPass, c.lp * std::pow (s, 0.3) * 1.1, 0.9, 0.0, rate);

        return b;
    }

    /** Frequency-dependent decay: a one-pole low-pass that closes over time. */
    inline void damp (Buffer& b, double amount, double rate)
    {
        if (amount <= 0.0)
            return;

        float y = 0.0f;

        for (size_t i = 0; i < b.size(); ++i)
        {
            const double t = (double) i / rate;
            const double fc = 18000.0 / (1.0 + amount * 24.0 * t);
            const float a = (float) std::exp (-2.0 * kPi * std::min (fc, rate * 0.45) / rate);
            y = b[i] + a * (y - b[i]);
            b[i] = y;
        }
    }

    inline void fadeTail (Buffer& b, double fraction = 0.15)
    {
        const size_t fadeLen = std::max ((size_t) 1, (size_t) ((double) b.size() * fraction));
        const size_t start = b.size() - fadeLen;

        for (size_t i = start; i < b.size(); ++i)
            b[i] *= 0.5f + 0.5f * (float) std::cos (kPi * (double) (i - start) / (double) fadeLen);
    }

    /** Pink-weighted power (so noise-like music keeps its loudness through any impulse). */
    inline double pinkPower (const Buffer& b, double rate)
    {
        const int order = fftOrderFor ((int) b.size());
        Fft fft (order);
        const int n = fft.getSize();
        std::vector<std::complex<float>> data ((size_t) n);

        for (size_t i = 0; i < b.size(); ++i)
            data[i] = b[i];

        fft.forward (data.data());
        double power = 0.0, weights = 0.0;

        for (int k = 1; k < n / 2; ++k)
        {
            const double f = rate * k / n;

            if (f < 40.0 || f > 12000.0)
                continue;

            const double w = 1.0 / f;
            power += w * std::norm (data[(size_t) k]);
            weights += w;
        }

        return weights > 0.0 ? power / weights : 1.0;
    }

    inline void trimSilence (Buffer& l, Buffer& r)
    {
        float peak = 0.0f;

        for (size_t i = 0; i < l.size(); ++i)
            peak = std::max (peak, std::max (std::abs (l[i]), std::abs (r[i])));

        const float floor = peak * 1.0e-4f; // -80 dB
        size_t end = l.size();

        while (end > 64 && std::abs (l[end - 1]) < floor && std::abs (r[end - 1]) < floor)
            --end;

        l.resize (end);
        r.resize (end);
    }
} // namespace ir

/** Builds a stereo impulse. `size` 0.25..2 rescales the object/space, `damp` 0..1 darkens the tail. */
inline StereoImpulse makeImpulse (int typeIndex, float size, float damp, bool reverse, double rate)
{
    using namespace ir;
    const auto type = (IrType) std::clamp (typeIndex, 0, kNumImpulses - 1);
    const double sz = std::clamp ((double) size, 0.25, 2.0);
    const int maxLen = samples (kMaxImpulseSeconds, rate);
    Random rng (0x5EED0000u + (uint32_t) typeIndex * 7919u);
    Buffer l, r;

    const auto sized = [&] (double seconds) { return Buffer ((size_t) std::min (maxLen, samples (seconds, rate)), 0.0f); };
    const auto both = [&] (Buffer b) { l = b; r = std::move (b); };

    switch (type)
    {
        case IrType::cab1x12:
            both (cabinet ({ 85, 115, 3.5, 650, -2.5, 2300, 1.3, 5.0, 5600, true }, sz, rate, rng, { { 1.4, -0.28f }, { 2.9, 0.1f } }));
            break;

        case IrType::cab4x12:
            both (cabinet ({ 72, 95, 5.5, 480, -3.5, 2800, 1.1, 4.0, 5000, true }, sz, rate, rng, { { 0.09, 0.45f }, { 0.17, 0.3f }, { 0.8, 0.12f } }));
            break;

        case IrType::tweed:
            both (cabinet ({ 100, 160, 2.0, 900, 1.5, 1300, 0.8, 4.0, 6800, false }, sz, rate, rng, { { 1.1, -0.3f }, { 2.4, 0.15f } }));
            break;

        case IrType::radio:
        {
            auto b = cabinet ({ 280, 420, 6.0, 900, -1.0, 1800, 1.5, 7.0, 4200, true }, sz, rate, rng, { { 0.6, 0.35f } });
            filter (b, SvfShape::highPass, 280.0 / sz, 0.8, 0.0, rate);
            both (std::move (b));
            break;
        }

        case IrType::telephone:
        {
            auto b = cabinet ({ 380, 700, 2.0, 2000, -1.0, 1500, 1.2, 5.0, 3100, true }, sz, rate, rng, { { 0.3, 0.2f } });
            filter (b, SvfShape::highPass, 380.0 / sz, 0.9, 0.0, rate);
            filter (b, SvfShape::lowPass, 3200.0, 0.9, 0.0, rate);
            both (std::move (b));
            break;
        }

        case IrType::megaphone:
        {
            auto b = cabinet ({ 550, 1100, 10.0, 1700, -4.0, 2600, 6.0, 8.0, 5000, true }, sz, rate, rng, { { 0.8, 0.45f }, { 1.6, 0.2f } });
            filter (b, SvfShape::highPass, 550.0 / sz, 0.9, 0.0, rate);
            both (std::move (b));
            break;
        }

        case IrType::tornCone:
        {
            auto b = cabinet ({ 90, 120, 3.0, 700, -18.0, 3400, 8.0, 12.0, 6000, false }, sz, rate, rng, { { 0.23, 0.7f }, { 0.47, 0.5f }, { 1.2, -0.3f } });
            filter (b, SvfShape::notch, 1900.0 / sz, 3.0, 0.0, rate);
            both (std::move (b));
            break;
        }

        case IrType::tinCan:
        case IrType::metalBowl:
        case IrType::steelPipe:
        case IrType::glassJar:
        case IrType::gong:
        {
            struct Mode { double ratio; double t60; float amp; };
            std::vector<Mode> modes;
            double f0 = 500.0, direct = 0.3, length = 1.0;

            if (type == IrType::tinCan)
            {
                f0 = 520.0;
                length = 0.5;
                const double ratios[] = { 1.0, 1.42, 1.97, 2.35, 2.61, 3.09, 3.48, 4.02, 4.59, 5.17, 5.83, 6.62, 7.4, 8.9 };
                int i = 0;

                for (double rr : ratios)
                    modes.push_back ({ rr, 0.35 / (1.0 + 0.15 * i), 1.0f / std::sqrt ((float) ++i) });

                direct = 0.45;
            }
            else if (type == IrType::metalBowl)
            {
                f0 = 330.0;
                length = 2.0;
                const double ratios[] = { 1.0, 2.71, 5.15, 8.43, 12.3 };
                const float amps[] = { 1.0f, 0.6f, 0.4f, 0.25f, 0.15f };

                for (int i = 0; i < 5; ++i)
                {
                    modes.push_back ({ ratios[i] * 0.997, 2.0 / (1.0 + 0.3 * i), amps[i] });
                    modes.push_back ({ ratios[i] * 1.003, 2.0 / (1.0 + 0.3 * i), amps[i] * 0.8f });
                }

                direct = 0.15;
            }
            else if (type == IrType::steelPipe)
            {
                f0 = 150.0;
                length = 1.5;

                for (int h = 1; h <= 15; h += 2)
                    modes.push_back ({ (double) h, 1.1 / (1.0 + 0.08 * h), 1.0f / std::sqrt ((float) h) });

                for (double rr : { 2.76, 5.40, 8.93, 13.34 })
                    modes.push_back ({ rr * 1.01, 1.5, 0.35f });

                direct = 0.2;
            }
            else if (type == IrType::glassJar)
            {
                f0 = 1180.0;
                length = 0.9;
                const double ratios[] = { 1.0, 1.99, 3.26, 4.58, 6.19 };

                for (int i = 0; i < 5; ++i)
                    modes.push_back ({ ratios[i], 0.7 / (1.0 + 0.2 * i), 0.8f / (1.0f + 0.4f * (float) i) });

                modes.push_back ({ 260.0 / 1180.0, 0.08, 1.4f }); // air cavity
                direct = 0.35;
            }
            else // gong
            {
                f0 = 90.0;
                length = 2.0;

                for (int i = 0; i < 60; ++i)
                {
                    const double rr = std::pow (i + 1.0, 1.4) * (1.0 + 0.03 * rng.nextBipolar());
                    modes.push_back ({ rr, 2.4 / (1.0 + 0.05 * i), 1.0f / std::sqrt (1.0f + (float) i) });
                }

                direct = 0.05;
            }

            const double fBase = f0 / sz;
            l = sized (length * std::sqrt (sz) * 1.2);
            r = l;

            for (auto* b : { &l, &r })
            {
                for (const auto& m : modes)
                {
                    const double detune = 1.0 + 0.002 * rng.nextBipolar();
                    mode (*b, fBase * m.ratio * detune, m.t60 * std::sqrt (sz), m.amp * (0.7f + 0.3f * rng.nextFloat()),
                          6.2831853f * rng.nextFloat(), rate);
                }

                (*b)[0] += (float) direct;
                filter (*b, SvfShape::highPass, 120.0, 0.7, 0.0, rate);
            }

            break;
        }

        case IrType::cardboardBox:
        {
            l = sized (0.25 * sz);
            r = l;

            for (auto* b : { &l, &r })
            {
                (*b)[0] = 0.8f;
                reflections (*b, 10, 0.5 * sz, 3.5 * sz, 0.45f, rng, rate);

                for (int i = 0; i < 25; ++i)
                    mode (*b, (150.0 + 1350.0 * std::pow (rng.nextFloat(), 1.5)) / sz, 0.06 + 0.06 * rng.nextFloat(),
                          0.5f * rng.nextFloat(), 6.2831853f * rng.nextFloat(), rate);

                filter (*b, SvfShape::lowPass, 4500.0, 0.7, 0.0, rate);
                filter (*b, SvfShape::highPass, 110.0, 0.7, 0.0, rate);
            }

            break;
        }

        case IrType::springTank:
        {
            l = sized (2.2 * sz);
            r = l;
            const double period[] = { 0.0331 * sz, 0.0373 * sz };
            const double decay = std::exp (-6.907755 * period[0] / (2.4 * sz));

            for (int ch = 0; ch < 2; ++ch)
            {
                auto& b = ch == 0 ? l : r;
                float g = 0.9f;

                for (double t = 0.002; t < (double) b.size() / rate; t += period[ch] * (1.0 + 0.02 * rng.nextBipolar()))
                {
                    tap (b, t * 1000.0, rng.nextFloat() < 0.5f ? -g : g, rate);
                    g *= (float) decay;
                }

                noiseTail (b, 5.0, 2.0 * sz, 0.012f, rng, rate);

                // Dispersion: a long first-order all-pass chain smears each echo into the classic "drip" chirp
                for (int stage = 0; stage < 80; ++stage)
                {
                    constexpr float a = 0.62f;
                    float x1 = 0.0f, y1 = 0.0f;

                    for (auto& x : b)
                    {
                        const float y = -a * x + x1 + a * y1;
                        x1 = x;
                        y1 = y;
                        x = y;
                    }
                }

                filter (b, SvfShape::lowPass, 4500.0, 0.7, 0.0, rate);
                filter (b, SvfShape::highPass, 160.0, 0.7, 0.0, rate);
            }

            break;
        }

        case IrType::steelPlate:
        {
            l = sized (2.4 * sz);
            r = l;

            for (auto* b : { &l, &r })
            {
                noiseTail (*b, 0.2, 2.2 * sz, 1.0f, rng, rate, 1.0);
                filter (*b, SvfShape::highPass, 180.0, 0.7, 0.0, rate);
                filter (*b, SvfShape::lowPass, 10000.0, 0.7, 0.0, rate);
            }

            break;
        }

        case IrType::closet:
        case IrType::tiledRoom:
        case IrType::tunnel:
        case IrType::concreteHall:
        case IrType::cistern:
        {
            struct Space { double pre, erSpan; int erCount; double t60, lp, hp; float tail; };
            const Space spaces[] = {
                { 0.5, 6.0, 14, 0.22, 7000, 120, 0.5f },   // closet
                { 1.0, 12.0, 10, 0.8, 12000, 90, 0.35f },  // tiled room
                { 12.0, 60.0, 6, 1.6, 5000, 60, 0.4f },    // tunnel
                { 18.0, 80.0, 20, 2.2, 9000, 70, 0.3f },   // concrete hall
                { 25.0, 50.0, 8, 4.0, 6000, 50, 0.35f },   // cistern
            };
            const auto& sp = spaces[(int) type - (int) IrType::closet];
            l = sized ((sp.pre + sp.erSpan) * 0.001 * sz + sp.t60 * sz * 1.1);
            r = l;

            for (auto* b : { &l, &r })
            {
                reflections (*b, sp.erCount, sp.pre * sz, (sp.pre + sp.erSpan) * sz, 0.6f, rng, rate);

                if (type == IrType::tiledRoom)
                {
                    // flutter echo between parallel tiled walls
                    float g = 0.5f;

                    for (double t = 9.0 * sz; t < 400.0 * sz; t += 9.0 * sz)
                    {
                        tap (*b, t + 0.3 * rng.nextBipolar(), g, rate);
                        g *= 0.86f;
                    }
                }

                noiseTail (*b, (sp.pre + sp.erSpan * 0.3) * sz, sp.t60 * sz, sp.tail, rng, rate, sp.erSpan * 0.5 * sz);
                filter (*b, SvfShape::lowPass, sp.lp, 0.7, 0.0, rate);
                filter (*b, SvfShape::highPass, sp.hp, 0.7, 0.0, rate);

                if (type == IrType::closet)
                    filter (*b, SvfShape::bell, 350.0 / sz, 1.2, 4.0, rate);

                if (type == IrType::tunnel)
                    filter (*b, SvfShape::bell, 250.0 / sz, 0.9, 5.0, rate);
            }

            break;
        }

        case IrType::count:
            break;
    }

    if (l.empty())
    {
        l.assign (1, 1.0f);
        r = l;
    }

    for (auto* b : { &l, &r })
    {
        if ((int) b->size() > maxLen)
            b->resize ((size_t) maxLen);

        ir::damp (*b, (double) damp, rate);
    }

    trimSilence (l, r);

    if ((double) l.size() > 0.1 * rate)
    {
        fadeTail (l, 0.08);
        fadeTail (r, 0.08);
    }

    const double power = 0.5 * (pinkPower (l, rate) + pinkPower (r, rate));
    const float gain = power > 1.0e-12 ? (float) (1.0 / std::sqrt (power)) : 1.0f;

    for (auto* b : { &l, &r })
    {
        for (auto& x : *b)
            x *= gain;

        if (reverse)
            std::reverse (b->begin(), b->end());
    }

    return { std::move (l), std::move (r) };
}

} // namespace hl::dsp
