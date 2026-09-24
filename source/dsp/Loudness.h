#pragma once

#include "Convolver.h"
#include "Degrade.h"
#include "Dynamics.h"
#include "Echo.h"
#include "Filter.h"
#include "Motion.h"
#include "Spectrum.h"
#include "Trash.h"


/** Static loudness estimate: how much louder or quieter a patch makes typical music, worked out from
    the settings alone. Nothing here listens to the audio, so the answer is known before anything plays.

    A 1/3-octave spectrum of typical music (the median of 7,992 released tracks) at about -18 LUFS is
    passed through the chain in order. Filters, tone controls, the echo loop and the loaded impulse shape
    it band by band; the distortion keeps part of its input's shape and moves the rest into harmonics
    (both measured per algorithm); dynamics work from the static curve. The result is read like a
    loudness meter would (K-weighted). */
namespace hl::dsp::loudness
{
constexpr double referenceDb = -18.0;

inline double toDb (double power) { return 10.0 * std::log10 (std::max (power, 1.0e-20)); }
inline double toPower (double db) { return std::pow (10.0, db / 10.0); }

inline void scale (Spectrum& s, double powerGain)
{
    for (auto& v : s)
        v *= powerGain;
}

/** a*A + b*B band by band, with correlation rho between the two signals. */
inline Spectrum blend (double a, const Spectrum& A, double b, const Spectrum& B, double rho)
{
    Spectrum out {};

    for (int i = 0; i < kBands; ++i)
        out[(size_t) i] = a * a * A[(size_t) i] + b * b * B[(size_t) i] + 2.0 * a * b * rho * std::sqrt (A[(size_t) i] * B[(size_t) i]);

    return out;
}

inline double lowPassMag (double f, double fc, double q)
{
    const std::complex<double> s (0.0, f / fc);
    return std::abs (1.0 / (s * s + s / q + 1.0));
}

inline double highPassMag (double f, double fc, double q)
{
    const std::complex<double> s (0.0, f / fc);
    return std::abs ((s * s) / (s * s + s / q + 1.0));
}

//==============================================================================
/** For each distortion algorithm and input level (after drive): the level change, and how much of the
    output is harmonics (the part not correlated with the input). Measured once with pink noise. */
class ShaperTable
{
public:
    static constexpr float minDb = -48.0f, maxDb = 48.0f;
    static constexpr int steps = 49;

    static const ShaperTable& get()
    {
        static const ShaperTable table;
        return table;
    }

    float gainDb (Algo a, float inputDb) const noexcept { return lookup (gain[(size_t) a], inputDb); }

    /** Signed gain of the part of the output that follows the input (negative = inverted). */
    float linear (Algo a, float inputDb) const noexcept { return lookup (linearGain[(size_t) a], inputDb); }

private:
    using Row = std::array<float, steps>;

    static float lookup (const Row& row, float inputDb) noexcept
    {
        const float pos = (std::clamp (inputDb, minDb, maxDb) - minDb) / (maxDb - minDb) * (steps - 1);
        const int i0 = std::min ((int) pos, steps - 2);
        const float t = pos - (float) i0;
        return row[(size_t) i0] + t * (row[(size_t) i0 + 1] - row[(size_t) i0]);
    }

    ShaperTable()
    {
        constexpr int n = 4096;
        std::vector<float> pink ((size_t) n);
        Random rng (2024);
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        double sumSq = 0.0;

        for (auto& v : pink) // Paul Kellet's pink filter
        {
            const float w = rng.nextBipolar();
            b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
            b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
            b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
            v = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
            b6 = w * 0.115926f;
            sumSq += (double) v * v;
        }

        const float norm = (float) (1.0 / std::sqrt (sumSq / n));

        for (auto& v : pink)
            v *= norm;

        const auto params = ShapeParams::make (24.0f, 192000.0);

        for (int a = 0; a < kNumAlgos; ++a)
        {
            for (int step = 0; step < steps; ++step)
            {
                const float level = minDb + (maxDb - minDb) * (float) step / (float) (steps - 1);
                const float g = dbToGain (level);
                ShaperState state;
                const float dc = shapeStatic ((Algo) a, 0.0f, params);
                double sum = 0.0, sq = 0.0, cross = 0.0, inSq = 0.0;

                for (auto v : pink)
                {
                    const double x = v * g;
                    const double y = shape ((Algo) a, (float) x, params, state) - dc;
                    sum += y;
                    sq += y * y;
                    cross += x * y;
                    inSq += x * x;
                }

                const double mean = sum / n;
                const double outPower = std::max (1.0e-30, sq / n - mean * mean);
                gain[(size_t) a][(size_t) step] = (float) std::clamp (10.0 * std::log10 (outPower / (g * g)), -80.0, 40.0);
                linearGain[(size_t) a][(size_t) step] = (float) (cross / inSq);
            }
        }
    }

    std::array<Row, kNumAlgos> gain {}, linearGain {};
};

//==============================================================================
inline void filterStage (Spectrum& s, const FilterSettings& f, double rate)
{
    for (int i = 0; i < kBands; ++i)
        s[(size_t) i] *= bandGain ([&] (double hz) { return (double) FilterModule::magnitude (f, hz, rate); }, i);

    scale (s, toPower (0.5 * std::clamp ((double) f.driveDb, 0.0, 36.0) * std::clamp ((double) f.mix, 0.0, 1.0)));

    // the saturating resonance keeps hot outputs from growing past about -6 dBFS RMS
    const double level = toDb (totalPower (s));

    if (level > -6.0)
        scale (s, toPower (-0.7 * (level + 6.0)));
}

inline void trashStage (Spectrum& s, const TrashSettings& t)
{
    const auto& grid = Grid::get();
    const auto& table = ShaperTable::get();
    const int numBands = std::clamp (t.numBands, 1, kTrashBands);
    Spectrum out {};

    for (int b = 0; b < numBands; ++b)
    {
        const double lo = b == 0 ? 0.0 : (double) t.crossovers[(size_t) b - 1];
        const double hi = b == numBands - 1 ? 1.0e9 : (double) t.crossovers[(size_t) b];
        Spectrum in {};

        for (int i = 0; i < kBands; ++i)
            if (grid.freq[(size_t) i] >= lo && grid.freq[(size_t) i] < hi)
                in[(size_t) i] = s[(size_t) i];

        const double inPower = totalPower (in);

        if (inPower <= 1.0e-20)
            continue;

        const auto& band = t.bands[(size_t) b];
        const double inDb = toDb (inPower);
        const double morph = std::clamp ((double) band.morph, 0.0, 1.0);

        // The morph blends the two shapers' outputs sample by sample, so their in-phase parts add with
        // their signs (Fuzz and Cheby 3 are inverted versions of each other for small signals) and only
        // the harmonics add as power. All relative to the band input.
        double linear = 0.0, harmonicPower = 0.0;

        for (int k = 0; k < 2; ++k)
        {
            const auto a = (Algo) std::clamp (k == 0 ? band.algoA : band.algoB, 0, kNumAlgos - 1);
            const double weight = k == 0 ? 1.0 - morph : morph;

            if (weight <= 0.0)
                continue;

            const double pre = preGainDb (a, band.driveDb);
            const float x = (float) (inDb + pre);
            const double outPower = toPower (pre + table.gainDb (a, x));
            const double lin = table.linear (a, x) * std::pow (10.0, pre / 20.0);
            linear += weight * lin;
            harmonicPower += weight * weight * std::max (0.0, outPower - lin * lin);
        }

        const double wetPower = linear * linear + harmonicPower;
        const double h = wetPower > 0.0 ? harmonicPower / wetPower : 0.0;
        double wetDb = inDb + toDb (wetPower);

        if (t.autoGain)
            wetDb += TrashModule::compensation (band);

        wetDb += band.levelDb;

        // What stays follows the input's shape; harmonics land 1, ~1.6 and ~2.3 octaves higher
        // (anything pushed past 20 kHz is removed by the oversampler)
        Spectrum wet {};

        for (int i = 0; i < kBands; ++i)
        {
            const auto at = [&] (int j) { return j >= 0 ? in[(size_t) j] : 0.0; };
            wet[(size_t) i] = ((1.0 - h) * in[(size_t) i] + h * (0.45 * at (i - 3) + 0.30 * at (i - 5) + 0.25 * at (i - 7))) / inPower;
        }

        scale (wet, toPower (wetDb));
        const double tone = trashToneCutoff (band.tone);

        for (int i = 0; i < kBands; ++i)
            wet[(size_t) i] *= bandGain ([&] (double hz) { return lowPassMag (hz, tone, 0.7071); }, i);

        // Parallel blend; the clean part follows the distorted part's loudness down (see TrashModule)
        const double mix = std::clamp ((double) band.mix, 0.0, 1.0);
        const double follow = std::min (1.0, 4.0 * mix);
        Spectrum dry = in;
        scale (dry, toPower (follow * std::min (0.0, toDb (totalPower (wet)) - inDb)));
        const auto result = blend (mix, wet, 1.0 - mix, dry, 0.5);

        for (int i = 0; i < kBands; ++i)
            out[(size_t) i] += result[(size_t) i];
    }

    s = out;
}

inline void convolveStage (Spectrum& s, const ConvolveSettings& c, const ImpulseBands* impulse)
{
    // (1 - m) + m H per frequency, averaged per band: |.|^2 = (1-m)^2 + m^2 |H|^2 + 2 m (1-m) Re(H)
    const double m = std::clamp ((double) c.mix, 0.0, 1.0);

    for (int i = 0; i < kBands; ++i)
    {
        const double power = impulse != nullptr ? impulse->power[(size_t) i] : 1.0;
        const double real = impulse != nullptr ? impulse->real[(size_t) i] : 0.0;
        s[(size_t) i] *= std::max (0.0, (1.0 - m) * (1.0 - m) + m * m * power + 2.0 * m * (1.0 - m) * real);
    }
}

inline void motionStage (Spectrum& s, const MotionSettings& ms)
{
    const double m = std::clamp ((double) ms.mix, 0.0, 1.0);
    const double depth = std::clamp ((double) ms.depth, 0.0, 1.0);
    const double fb = std::clamp ((double) ms.feedback, -1.0, 1.0);
    const auto resonance = [] (double loop) { return 1.0 / std::max (0.05, 1.0 - loop * loop); };
    const auto mixed = [&] (double wetPower, double rho) { return (1.0 - m) * (1.0 - m) + m * m * wetPower + 2.0 * m * (1.0 - m) * rho * std::sqrt (wetPower); };
    double power = 1.0;

    switch ((MotionMode) std::clamp (ms.mode, 0, kNumMotionModes - 1))
    {
        // two voices at different delays are largely uncorrelated with each other and with the dry
        case MotionMode::chorus:    power = mixed (0.55 * resonance (0.5 * std::abs (fb)), 0.0); break;
        case MotionMode::flanger:   power = mixed (resonance (0.95 * fb), 0.3); break;
        case MotionMode::phaser:    power = mixed (resonance (0.9 * fb), 0.3); break;
        case MotionMode::vibrato:   power = mixed (1.0, 0.6); break;

        case MotionMode::tremolo:
        {
            // gain = a + b * lfo: E[g^2] = a^2 + b^2 E[lfo^2] (feedback squares the wave off)
            const double a = 1.0 - 0.5 * depth, b = 0.5 * depth, lfoPower = 0.5 + 0.5 * std::max (0.0, fb);
            const double dc = (1.0 - m) + m * a;
            power = dc * dc + m * m * b * b * lfoPower;
            break;
        }

        case MotionMode::ringMod:   power = mixed (0.5, 0.0); break;
        case MotionMode::freqShift: power = mixed (resonance (0.85 * std::abs (fb)), 0.0); break;
        case MotionMode::count:     break;
    }

    scale (s, power);
}

inline void degradeStage (Spectrum& s, const DegradeSettings& d)
{
    const double age = std::clamp ((double) d.age, 0.0, 1.0);
    const double lp = 20000.0 * std::pow (2800.0 / 20000.0, age), hp = 10.0 * std::pow (22.0, age);
    Spectrum worn = s;

    if (age > 1.0e-3)
        for (int i = 0; i < kBands; ++i)
            worn[(size_t) i] *= bandGain ([&] (double f) { return lowPassMag (f, lp, 0.75) * highPassMag (f, hp, 0.6); }, i);

    // dropouts: ~3 d^2 events per second of ~155 ms, mostly well below the signal
    const double drop = std::clamp ((double) d.dropout, 0.0, 1.0);
    scale (worn, 1.0 - 0.9 * std::min (0.8, 3.0 * drop * drop * 0.155));

    const double m = std::clamp ((double) d.mix, 0.0, 1.0);
    s = blend (1.0 - m, s, m, worn, 0.9);
}

inline void dynamicsStage (Spectrum& s, const DynamicsSettings& d)
{
    const double detector = toDb (totalPower (s)) + 9.0; // the peak detector rides well above the RMS
    double change = compressorGainDb ((float) detector, d.thresholdDb, std::max (1.0f, d.ratio), 6.0f) + d.makeupDb;

    if (d.gateDb > DynamicsModule::gateOffDb)
    {
        const double open = detector - d.gateDb; // > 0: mostly open
        change += open >= 6.0 ? 0.0 : (open >= -6.0 ? -3.0 - 0.5 * (6.0 - open) : -30.0);
    }

    const double m = std::clamp ((double) d.mix, 0.0, 1.0);
    const double gain = (1.0 - m) + m * std::pow (10.0, change / 20.0);
    scale (s, gain * gain);
}

inline void echoStage (Spectrum& s, const EchoSettings& e, double rate)
{
    const double t = std::clamp ((double) e.tone, -1.0, 1.0);
    const double lp = std::min (t < 0.0 ? 16000.0 * std::pow (600.0 / 16000.0, -t) : 16000.0, rate * 0.45);
    const double hp = t > 0.0 ? 40.0 * std::pow (2500.0 / 40.0, t) : 40.0;

    // loop saturation: tanh(x g)/g compresses the repeats once the loop signal is hot
    const double drive = std::clamp ((double) e.drive, 0.0, 1.0);
    const double peak = std::sqrt (totalPower (s)) * 4.0 * std::pow (10.0, 18.0 * drive / 20.0);
    const double driveLoss = peak > 1.0 ? 1.0 / peak : 1.0;
    const double fb = std::clamp ((double) e.feedback, 0.0, 1.2);

    Spectrum wet {};
    double maxLoop = 0.0;

    for (int i = 0; i < kBands; ++i)
    {
        // every repeat passes the tone filter again: a geometric series per band
        const double pass = bandGain ([&] (double f) { return lowPassMag (f, lp, 0.6) * highPassMag (f, hp, 0.6); }, i);
        const double loop2 = fb * fb * pass * driveLoss;
        maxLoop = std::max (maxLoop, std::sqrt (loop2));
        wet[(size_t) i] = s[(size_t) i] / std::max (0.1, 1.0 - loop2);
    }

    // runaway: the loop sustains itself around -10 dBFS RMS, whatever comes in
    if (maxLoop >= 0.95 && totalPower (wet) < toPower (-10.0))
        scale (wet, toPower (-10.0) / std::max (1.0e-20, totalPower (wet)));

    const double m = std::clamp ((double) e.mix, 0.0, 1.0);
    s = blend (1.0 - m, s, m, wet, 0.0);
}

} // namespace hl::dsp::loudness
