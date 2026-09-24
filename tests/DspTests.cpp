// Native unit tests for the JUCE-independent DSP core.
// Build & run: scripts/test-dsp.sh

#include "dsp/Chain.h"

#include <chrono>
#include <complex>
#include <cstdio>
#include <functional>
#include <random>
#include <string>

using namespace hl::dsp;

//==============================================================================
namespace
{
int failures = 0, checks = 0;

#define CHECK_NEAR(actual, expected, tol, what)                                                        \
    do {                                                                                              \
        ++checks;                                                                                     \
        const double a_ = (actual), e_ = (expected);                                                  \
        if (! (std::abs (a_ - e_) <= (tol))) {                                                        \
            ++failures;                                                                               \
            std::printf ("    FAIL %s: got %.6f expected %.6f (tol %.6f)  [%s:%d]\n",                  \
                         std::string (what).c_str(), a_, e_, (double) (tol), __FILE__, __LINE__);      \
        }                                                                                             \
    } while (0)

#define CHECK_TRUE(cond, what)                                                                        \
    do {                                                                                              \
        ++checks;                                                                                     \
        if (! (cond)) {                                                                               \
            ++failures;                                                                               \
            std::printf ("    FAIL %s  [%s:%d]\n", std::string (what).c_str(), __FILE__, __LINE__);    \
        }                                                                                             \
    } while (0)

void section (const char* name) { std::printf ("[%s]\n", name); }

std::vector<float> sine (double freq, double amp, double fs, int n, double phase = 0.0)
{
    std::vector<float> x ((size_t) n);

    for (int i = 0; i < n; ++i)
        x[(size_t) i] = (float) (amp * std::sin (2.0 * kPi * freq * i / fs + phase));

    return x;
}

std::vector<float> noise (int n, double amp, uint32_t seed = 1)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    std::vector<float> x ((size_t) n);

    for (auto& v : x)
        v = (float) amp * u (rng);

    return x;
}

/** Amplitude of the component at `freq` (Hann-windowed correlation). */
double amplitudeAt (const float* x, int n, double freq, double fs)
{
    std::complex<double> acc = 0.0;
    double wsum = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (n - 1));
        acc += w * (double) x[i] * std::polar (1.0, -2.0 * kPi * freq * i / fs);
        wsum += w;
    }

    return 2.0 * std::abs (acc) / wsum;
}

double rms (const float* x, int n)
{
    double s = 0.0;

    for (int i = 0; i < n; ++i)
        s += (double) x[i] * x[i];

    return std::sqrt (s / std::max (1, n));
}

double db (double gain) { return 20.0 * std::log10 (std::max (1.0e-12, gain)); }

bool allFinite (const std::vector<float>& x, float bound = 1.0e6f)
{
    for (auto v : x)
        if (! std::isfinite (v) || std::abs (v) > bound)
            return false;

    return true;
}

float peakOf (const std::vector<float>& x, size_t from = 0)
{
    float p = 0.0f;

    for (size_t i = from; i < x.size(); ++i)
        p = std::max (p, std::abs (x[i]));

    return p;
}

/** Runs a stereo module over copies of `l`/`r` in blocks of `block`. */
template <typename Module, typename Setup>
void runModule (Module& m, std::vector<float>& l, std::vector<float>& r, int block, Setup&& perBlock)
{
    for (size_t start = 0; start < l.size(); start += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, l.size() - start);
        perBlock (n);
        m.process (l.data() + start, r.data() + start, n);
    }
}
} // namespace

//==============================================================================
static void testShapers()
{
    section ("shapers");
    const auto p0 = ShapeParams::make (0.0f, 192000.0);
    const auto pMax = ShapeParams::make (kMaxDriveDb, 192000.0);

    for (int a = 0; a < kNumAlgos; ++a)
    {
        const auto algo = (Algo) a;
        bool finite = true;
        float peak = 0.0f;
        ShaperState st;

        for (int i = -20000; i <= 20000; ++i)
        {
            const float x = (float) i * 0.005f; // -100 .. 100
            for (const auto* p : { &p0, &pMax })
            {
                const float y = shape (algo, x, *p, st);
                finite = finite && std::isfinite (y);
                peak = std::max (peak, std::abs (y));
            }
        }

        CHECK_TRUE (finite && peak <= 3.5f, std::string (algoInfo (a).name) + ": finite and bounded for |x| <= 100 (peak " + std::to_string (peak) + ")");
    }

    // Saturators behave like a wire for small signals
    for (auto a : { Algo::tube, Algo::tape, Algo::warm, Algo::transformer, Algo::diode, Algo::valve, Algo::softClip, Algo::hardClip, Algo::sineClip, Algo::sineFold, Algo::triFold })
    {
        const float y = shapeStatic (a, 0.01f, p0) - shapeStatic (a, 0.0f, p0);
        CHECK_NEAR (y / 0.01f, 1.0, 0.02, std::string (algoInfo ((int) a).name) + ": unity small-signal slope");
    }

    // Auto gain keeps a -12 dBFS sine near its input level at every drive
    const auto& table = AutoGainTable::get();
    int badLevels = 0;

    for (int a = 0; a < kNumAlgos; ++a)
        for (float drive : { 0.0f, 12.0f, 24.0f, 48.0f })
        {
            const float c = table.compensationDb ((Algo) a, drive);

            if (! std::isfinite (c) || c < -30.5f || c > 24.5f)
                ++badLevels;
        }

    CHECK_TRUE (badLevels == 0, "auto-gain table finite and within range");
    CHECK_NEAR (table.compensationDb (Algo::warm, 0.0f), 0.0, 0.3, "warm at 0 dB drive needs ~no compensation");
    // A -12 dBFS sine hard-clipped into a full-scale square is ~15 dB louder
    CHECK_NEAR (table.compensationDb (Algo::hardClip, 36.0f), -15.0, 1.0, "hard clip at +36 dB drive is compensated down by ~15 dB");
}

//==============================================================================
static void testTrash()
{
    section ("trash");
    const double fs = 48000.0;
    const int n = 48000;

    // Latency: a clean band (mix 0) is a delayed copy of the input
    {
        TrashModule t;
        TrashSettings s;
        s.bands[0].mix = 0.0f;
        t.setSettings (s, 1);
        t.prepare (fs, 512);
        const int latency = t.getLatency();
        CHECK_TRUE (latency > 0 && latency < 200, "latency is " + std::to_string (latency) + " samples");

        auto l = sine (1000.0, 0.5, fs, n), r = l;
        const auto in = l;
        runModule (t, l, r, 256, [&] (int m) { t.setSettings (s, m); });

        double maxErr = 0.0;

        for (int i = 4000; i < n; ++i)
            maxErr = std::max (maxErr, (double) std::abs (l[(size_t) i] - in[(size_t) (i - latency)]));

        // what remains is the 8 Hz DC blocker's phase shift at 1 kHz (~0.008 rad x 0.5)
        CHECK_TRUE (maxErr < 6.0e-3, "dry path = input delayed by the reported latency (max err " + std::to_string (maxErr) + ")");
    }

    // Every oversampling factor has the same latency, and its dry path lines up exactly
    for (int factor : { 1, 2, 4, 8 })
    {
        TrashModule t;
        TrashSettings s;
        s.bands[0].mix = 0.0f;
        t.setOversampling (factor);
        t.setSettings (s, 1);
        t.prepare (fs, 512);
        auto l = sine (3000.0, 0.5, fs, n), r = l;
        const auto in = l;
        runModule (t, l, r, 256, [&] (int m) { t.setSettings (s, m); });
        double maxErr = 0.0;

        for (int i = 4000; i < n; ++i)
            maxErr = std::max (maxErr, (double) std::abs (l[(size_t) i] - in[(size_t) (i - t.getLatency())]));

        CHECK_TRUE (t.getLatency() == 68 && maxErr < 3.0e-3, std::to_string (factor) + "x: latency " + std::to_string (t.getLatency())
                                                              + " samples, dry path aligned (max err " + std::to_string (maxErr) + ")");
    }

    // 1x really aliases and 8x really doesn't: a 7 kHz fuzz leaves images below it only when not oversampled
    {
        const auto aliasDb = [&] (int factor)
        {
            TrashModule t;
            TrashSettings s;
            s.bands[0].algoA = (int) Algo::hardClip;
            s.bands[0].driveDb = 24.0f;
            t.setOversampling (factor);
            t.setSettings (s, 1);
            t.prepare (fs, 512);
            auto l = sine (7000.0, 0.3, fs, n), r = l;
            runModule (t, l, r, 256, [&] (int m) { t.setSettings (s, m); });
            // 3rd harmonic 21 kHz is fine; the 5th (35 kHz) folds to 13 kHz at 48 kHz
            return db (amplitudeAt (l.data() + n / 2, n / 2, 13000.0, fs) / amplitudeAt (l.data() + n / 2, n / 2, 7000.0, fs));
        };

        const double at1 = aliasDb (1), at8 = aliasDb (8);
        CHECK_TRUE (at1 > -30.0 && at8 < at1 - 35.0, "aliasing at 13 kHz: " + std::to_string (at1) + " dB at 1x, " + std::to_string (at8) + " dB at 8x");
    }

    // Three bands with mix 0 sum back to a flat response
    {
        TrashModule t;
        TrashSettings s;
        s.numBands = 3;

        for (auto& b : s.bands)
            b.mix = 0.0f;

        t.setSettings (s, 1);
        t.prepare (fs, 512);

        for (double f : { 60.0, 250.0, 1000.0, 2500.0, 9000.0 })
        {
            auto l = sine (f, 0.3, fs, n), r = l;
            runModule (t, l, r, 128, [&] (int m) { t.setSettings (s, m); });
            CHECK_NEAR (db (amplitudeAt (l.data() + 8000, n - 8000, f, fs) / 0.3), 0.0, 0.1, "3-band split is flat at " + std::to_string ((int) f) + " Hz");
        }
    }

    // Auto gain: heavy distortion stays roughly at the input loudness, every algorithm stays finite
    {
        const auto input = noise (n, 0.0, 1);
        int finiteCount = 0;
        double worstDb = 0.0;

        for (int a = 0; a < kNumAlgos; ++a)
        {
            TrashModule t;
            TrashSettings s;
            s.bands[0].algoA = a;
            s.bands[0].driveDb = 30.0f;
            t.setSettings (s, 1);
            t.prepare (fs, 512);

            auto l = sine (220.0, 0.25, fs, n), r = sine (330.0, 0.25, fs, n);
            runModule (t, l, r, 64, [&] (int m) { t.setSettings (s, m); });

            if (allFinite (l) && allFinite (r))
                ++finiteCount;

            const double level = db (rms (l.data() + 4800, n - 4800) / (0.25 / std::sqrt (2.0)));
            worstDb = std::max (worstDb, std::abs (level));
        }

        CHECK_TRUE (finiteCount == kNumAlgos, "all algorithms finite at +30 dB drive");
        CHECK_TRUE (worstDb < 9.0, "auto gain keeps every algorithm within 9 dB of the input level (worst " + std::to_string (worstDb) + " dB)");
    }

    // Parallel blend: pushing the input 24 dB harder must make the band dirtier, not let the clean part take over
    {
        const auto run = [&] (float amp, std::vector<float>& out)
        {
            TrashModule t;
            TrashSettings s;
            s.bands[0].algoA = (int) Algo::tube;
            s.bands[0].driveDb = 9.0f;
            s.bands[0].mix = 0.75f;
            t.setSettings (s, 1);
            t.prepare (fs, 512);
            out = sine (220.0, amp, fs, n);
            auto r = out;
            runModule (t, out, r, 64, [&] (int m) { t.setSettings (s, m); });
        };

        std::vector<float> normal, hot;
        run (0.1f, normal);
        run (1.58f, hot); // +24 dB
        const auto thd = [&] (const std::vector<float>& x)
        {
            const double f = amplitudeAt (x.data() + n / 2, n / 2, 220.0, fs);
            double h = 0.0;

            for (int k = 2; k <= 9; ++k)
                h += std::pow (amplitudeAt (x.data() + n / 2, n / 2, 220.0 * k, fs), 2.0);

            return std::sqrt (h) / f;
        };

        const double levelUp = db (rms (hot.data() + n / 2, n / 2) / rms (normal.data() + n / 2, n / 2));
        CHECK_TRUE (thd (hot) > 2.0 * thd (normal), "75% mix: +24 dB input doubles the harmonic content (" + std::to_string (thd (normal)) + " -> " + std::to_string (thd (hot)) + ")");
        // the saturating band compresses (overall loudness is Auto Level's job, at the end of the chain)
        CHECK_TRUE (levelUp < 18.0, "and it compresses: only " + std::to_string (levelUp) + " dB louder for +24 dB in");
    }

    // Morph halfway produces something between A and B, and algorithm changes don't click
    {
        TrashModule t;
        TrashSettings s;
        s.bands[0].algoA = (int) Algo::warm;
        s.bands[0].algoB = (int) Algo::bitcrush;
        s.bands[0].driveDb = 24.0f;
        t.setSettings (s, 1);
        t.prepare (fs, 512);
        auto l = sine (440.0, 0.3, fs, n), r = l;
        int block = 0;
        float maxStep = 0.0f;

        runModule (t, l, r, 32, [&] (int m)
        {
            ++block;
            s.bands[0].morph = 0.5f + 0.5f * std::sin ((float) block * 0.01f);
            s.bands[0].algoA = (block / 300) % 2 == 0 ? (int) Algo::warm : (int) Algo::hardClip;
            t.setSettings (s, m);
        });

        for (int i = 4801; i < n; ++i)
            maxStep = std::max (maxStep, std::abs (l[(size_t) i] - l[(size_t) i - 1]));

        CHECK_TRUE (allFinite (l) && maxStep < 0.9f, "morph sweep + algorithm swaps stay finite and click-free (max step " + std::to_string (maxStep) + ")");
    }
}

//==============================================================================
static void testFilters()
{
    section ("filters");
    const double fs = 48000.0;
    const int n = 32768;

    const auto measure = [&] (FilterSettings s, double freq)
    {
        FilterModule f;
        f.setSettings (s);
        f.prepare (fs, 512);
        auto l = sine (freq, 0.05, fs, n), r = l;
        runModule (f, l, r, 256, [&] (int) { f.setSettings (s); });
        return db (amplitudeAt (l.data() + n / 2, n / 2, freq, fs) / 0.05);
    };

    FilterSettings lp;
    lp.type = (int) FilterType::lowPass24;
    lp.cutoff = 1000.0f;
    lp.reso = 0.0f;
    CHECK_NEAR (measure (lp, 100.0), 0.0, 0.3, "LP24 1 kHz passes 100 Hz");
    CHECK_TRUE (measure (lp, 8000.0) < -60.0, "LP24 1 kHz rejects 8 kHz by > 60 dB");

    for (auto type : { FilterType::lowPass12, FilterType::lowPass24, FilterType::highPass12, FilterType::highPass24, FilterType::bandPass12,
                       FilterType::bandPass24, FilterType::notch, FilterType::combPlus, FilterType::vowel })
    {
        FilterSettings s;
        s.type = (int) type;
        s.cutoff = 900.0f;
        s.reso = 0.3f;
        double worst = 0.0;

        for (double f : { 200.0, 700.0, 1300.0, 3000.0 })
        {
            const double expected = db (FilterModule::magnitude (s, f, fs));

            if (expected > -40.0)
                worst = std::max (worst, std::abs (measure (s, f) - expected));
        }

        CHECK_TRUE (worst < 1.0, std::string (filterTypeNames()[(int) type]) + ": display curve matches the audio (worst " + std::to_string (worst) + " dB)");
    }

    // Screaming resonance stays bounded, even with a hot input and fast sweeps
    {
        FilterModule f;
        FilterSettings s;
        s.type = (int) FilterType::lowPass24;
        s.reso = 1.0f;
        s.driveDb = 24.0f;
        f.setSettings (s);
        f.prepare (fs, 512);
        auto l = noise (n, 1.0), r = noise (n, 1.0, 2);
        int block = 0;
        runModule (f, l, r, 32, [&] (int)
        {
            s.cutoff = 50.0f * std::pow (400.0f, 0.5f + 0.5f * std::sin ((float) ++block * 0.05f));
            s.type = (block / 200) % kNumFilterTypes;
            f.setSettings (s);
        });
        CHECK_TRUE (allFinite (l, 8.0f) && allFinite (r, 8.0f), "max resonance + drive + fast sweeps + type changes stay bounded (peak " + std::to_string (peakOf (l)) + ")");
    }
}

//==============================================================================
static void testMotion()
{
    section ("motion");
    const double fs = 48000.0;
    const int n = 48000;

    {
        MotionModule m;
        MotionSettings s;
        s.mode = (int) MotionMode::freqShift;
        s.freqHz = 200.0f;
        s.depth = 0.0f;
        s.spread = 0.0f;
        s.mix = 1.0f;
        m.setSettings (s);
        m.prepare (fs, 512);
        auto l = sine (1000.0, 0.5, fs, n), r = l;
        runModule (m, l, r, 256, [&] (int) { m.setSettings (s); });
        const double up = amplitudeAt (l.data() + n / 2, n / 2, 1200.0, fs);
        const double down = amplitudeAt (l.data() + n / 2, n / 2, 800.0, fs);
        const double orig = amplitudeAt (l.data() + n / 2, n / 2, 1000.0, fs);
        CHECK_NEAR (db (up / 0.5), 0.0, 0.5, "frequency shifter moves 1 kHz to 1.2 kHz at full level");
        CHECK_TRUE (db (down / up) < -35.0 && db (orig / up) < -35.0, "image and original suppressed by > 35 dB (" + std::to_string (db (down / up)) + " dB)");
    }

    for (int mode = 0; mode < kNumMotionModes; ++mode)
    {
        MotionModule m;
        MotionSettings s;
        s.mode = mode;
        s.depth = 1.0f;
        s.feedback = mode % 2 == 0 ? 1.0f : -1.0f;
        s.rateHz = 5.0f;
        s.mix = 1.0f;
        m.setSettings (s);
        m.prepare (fs, 512);
        auto l = noise (n, 0.8), r = noise (n, 0.8, 3);
        runModule (m, l, r, 100, [&] (int) { m.setSettings (s); });
        CHECK_TRUE (allFinite (l, 6.0f) && allFinite (r, 6.0f), std::string (motionModeNames()[mode]) + ": bounded at max depth/feedback (peak " + std::to_string (peakOf (l)) + ")");
    }

    {
        MotionModule m;
        MotionSettings s;
        s.mode = (int) MotionMode::tremolo;
        s.depth = 1.0f;
        s.rateHz = 4.0f;
        s.spread = 1.0f;
        s.mix = 1.0f;
        m.setSettings (s);
        m.prepare (fs, 512);
        std::vector<float> l ((size_t) n, 0.5f), r = l;
        runModule (m, l, r, 64, [&] (int) { m.setSettings (s); });
        float minL = 1.0f, maxL = 0.0f, maxSum = 0.0f;

        for (int i = n / 2; i < n; ++i)
        {
            minL = std::min (minL, l[(size_t) i]);
            maxL = std::max (maxL, l[(size_t) i]);
            maxSum = std::max (maxSum, std::abs (l[(size_t) i] + r[(size_t) i] - 0.5f));
        }

        CHECK_TRUE (minL < 0.01f && maxL > 0.49f, "tremolo at full depth swings 0 .. 1");
        CHECK_TRUE (maxSum < 0.02f, "spread 100% turns it into an equal-power-ish auto-pan (L+R constant)");
    }
}

//==============================================================================
static void testConvolution()
{
    section ("convolution");
    const double fs = 48000.0;

    // Engine vs. direct convolution with a random impulse long enough to hit every stage
    {
        const int irLen = 9000, n = 20000;
        StereoImpulse h { noise (irLen, 0.1, 11), noise (irLen, 0.1, 12) };

        for (auto& ch : h)
            for (int i = 0; i < irLen; ++i)
                ch[(size_t) i] *= (float) std::exp (-3.0 * i / irLen);

        Convolver c;
        c.prepare (fs);
        auto kernel = ConvolutionKernel::build (h, fs, 0);
        c.install (kernel.get());

        auto l = noise (n, 0.5, 21), r = noise (n, 0.5, 22);
        const auto inL = l, inR = r;
        const int blocks[] = { 1, 17, 64, 333, 128, 5 };
        int bi = 0;

        for (int start = 0; start < n;)
        {
            const int m = std::min (blocks[bi++ % 6], n - start);
            c.process (l.data() + start, r.data() + start, m);
            start += m;
        }

        double maxErr = 0.0, maxRef = 0.0;

        for (int i = 0; i < n; i += 7)
        {
            double refL = 0.0, refR = 0.0;

            for (int k = 0; k < irLen && k <= i; ++k)
            {
                refL += (double) h[0][(size_t) k] * inL[(size_t) (i - k)];
                refR += (double) h[1][(size_t) k] * inR[(size_t) (i - k)];
            }

            maxErr = std::max ({ maxErr, std::abs (refL - l[(size_t) i]), std::abs (refR - r[(size_t) i]) });
            maxRef = std::max (maxRef, std::abs (refL));
        }

        CHECK_TRUE (maxErr < 1.0e-4 * std::max (1.0, maxRef), "zero-latency partitioned convolution == direct convolution (max err " + std::to_string (maxErr) + ")");
        c.releaseAll();
    }

    // Every synthetic impulse is finite, within length and loudness-normalised
    {
        int ok = 0;
        double worst = 0.0;

        for (int t = 0; t < kNumImpulses; ++t)
            for (float size : { 0.25f, 1.0f, 2.0f })
            {
                const auto imp = makeImpulse (t, size, 0.5f, t % 3 == 0, fs);
                const bool finite = allFinite (imp[0]) && allFinite (imp[1]);
                const bool length = imp[0].size() == imp[1].size() && (double) imp[0].size() <= kMaxImpulseSeconds * fs + 1;
                const double p = 0.5 * (ir::pinkPower (imp[0], fs) + ir::pinkPower (imp[1], fs));
                worst = std::max (worst, std::abs (db (std::sqrt (p))));

                if (finite && length)
                    ++ok;
            }

        CHECK_TRUE (ok == kNumImpulses * 3, "all impulses finite and at most 2 s long");
        CHECK_TRUE (worst < 0.1, "all impulses normalised to unity pink-noise gain (worst " + std::to_string (worst) + " dB)");
    }

    // Module: impulse swaps via the worker path crossfade without clicks and keep ringing
    {
        ConvolveModule m;
        ConvolveSettings s;
        s.impulse = (int) IrType::steelPlate;
        s.mix = 1.0f;
        m.setSettings (s);
        m.prepare (fs, 512);

        const int n = 96000;
        auto l = sine (330.0, 0.3, fs, n), r = l;
        float maxStep = 0.0f;

        for (int start = 0; start < n; start += 480)
        {
            if (start % 9600 == 0)
            {
                s.impulse = (s.impulse + 5) % kNumImpulses;
                s.size = 0.5f + 0.1f * (float) (start / 9600);
                m.setSettings (s);
                m.settleForTesting();
            }
            else
            {
                m.setSettings (s);
                m.service();
            }

            m.process (l.data() + start, r.data() + start, 480);
        }

        for (int i = 1; i < n; ++i)
            maxStep = std::max (maxStep, std::abs (l[(size_t) i] - l[(size_t) i - 1]));

        CHECK_TRUE (allFinite (l, 20.0f), "impulse changes during playback stay finite");
        CHECK_TRUE (maxStep < 1.0f, "impulse changes crossfade without jumps (max step " + std::to_string (maxStep) + ")");
    }
}

//==============================================================================
static void testDelayDynamicsDegrade()
{
    section ("echo / dynamics / degrade");
    const double fs = 48000.0;

    {
        EchoModule e;
        EchoSettings s;
        s.timeMs = 250.0f;
        s.feedback = 0.0f;
        s.mix = 1.0f;
        s.wobble = 0.0f;
        s.drive = 0.0f;
        e.setSettings (s);
        e.prepare (fs, 512);
        std::vector<float> l (48000, 0.0f), r = l;
        l[100] = r[100] = 1.0f;
        runModule (e, l, r, 128, [&] (int) { e.setSettings (s); });
        const auto peakAt = (int) (std::max_element (l.begin(), l.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); }) - l.begin());
        CHECK_NEAR (peakAt - 100, 12000, 3, "echo lands 250 ms later");
    }

    {
        EchoModule e;
        EchoSettings s;
        s.timeMs = 120.0f;
        s.feedback = 1.2f;
        s.drive = 0.5f;
        s.mix = 1.0f;
        s.pingPong = true;
        e.setSettings (s);
        e.prepare (fs, 512);
        const int n = 48000 * 10;
        auto l = noise (n, 0.5), r = l;

        for (int i = 4800; i < n; ++i)
            l[(size_t) i] = r[(size_t) i] = 0.0f;

        runModule (e, l, r, 256, [&] (int) { e.setSettings (s); });
        CHECK_TRUE (allFinite (l, 2.0f) && allFinite (r, 2.0f), "runaway feedback (120%) self-oscillates but stays bounded (peak " + std::to_string (peakOf (l)) + ")");
        CHECK_TRUE (rms (l.data() + n - 48000, 48000) > 0.01, "and keeps going after the input stops");
    }

    {
        DynamicsModule d;
        DynamicsSettings s;
        s.thresholdDb = -20.0f;
        s.ratio = 4.0f;
        s.attackMs = 1.0f;
        s.releaseMs = 50.0f;
        d.setSettings (s);
        d.prepare (fs, 512);
        auto l = sine (1000.0, dbToGain (-8.0f), fs, 48000), r = l;
        runModule (d, l, r, 256, [&] (int) { d.setSettings (s); });
        const double outDb = db (amplitudeAt (l.data() + 24000, 24000, 1000.0, fs));
        CHECK_NEAR (outDb, -17.0, 1.2, "compressor: -8 dB peak into 4:1 at -20 dB comes out near -17 dB");
    }

    {
        DynamicsModule d;
        DynamicsSettings s;
        s.ratio = 1.0f;
        s.gateDb = -30.0f;
        d.setSettings (s);
        d.prepare (fs, 512);
        auto l = sine (500.0, dbToGain (-50.0f), fs, 48000), r = l;
        runModule (d, l, r, 256, [&] (int) { d.setSettings (s); });
        CHECK_TRUE (rms (l.data() + 24000, 24000) < 1.0e-6, "gate closes on a signal 20 dB under its threshold");
    }

    {
        DegradeModule d;
        DegradeSettings s;
        s.wow = s.flutter = s.age = s.noise = s.crackle = s.dropout = s.glitch = 1.0f;
        d.setSettings (s);
        d.prepare (fs, 512);
        d.setTempo (140.0);
        const int n = 48000 * 5;
        auto l = sine (440.0, 0.5, fs, n), r = sine (660.0, 0.5, fs, n);
        int glitchBlocks = 0;
        runModule (d, l, r, 128, [&] (int) { d.setSettings (s); glitchBlocks += d.isGlitching() ? 1 : 0; });
        CHECK_TRUE (allFinite (l, 4.0f) && allFinite (r, 4.0f), "everything at 100% stays finite and bounded (peak " + std::to_string (peakOf (l)) + ")");
        CHECK_TRUE (glitchBlocks > 50, "glitch engine fires");
    }

    {
        DegradeModule d;
        DegradeSettings s;
        s.mix = 1.0f;
        d.setSettings (s);
        d.prepare (fs, 512);
        auto l = noise (48000, 0.5), r = noise (48000, 0.5, 2);
        const auto inL = l;
        runModule (d, l, r, 128, [&] (int) { d.setSettings (s); });
        double maxErr = 0.0;

        for (size_t i = 0; i < l.size(); ++i)
            maxErr = std::max (maxErr, (double) std::abs (l[i] - inL[i]));

        CHECK_TRUE (maxErr < 1.0e-6, "all at 0 is transparent");
    }
}

//==============================================================================
static void testModulation()
{
    section ("modulation");

    for (int shape = 0; shape < kNumLfoShapes; ++shape)
    {
        Lfo lfo (7);
        float lo = 1.0f, hi = -1.0f;

        for (int i = 0; i < 20000; ++i)
        {
            lfo.advance (32, 3.0, 48000.0);
            const float v = lfo.value ((LfoShape) shape);
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }

        CHECK_TRUE (lo >= -1.0001f && hi <= 1.0001f && hi - lo > 1.0f, std::string (lfoShapeNames()[shape]) + " spans the bipolar range");
    }

    ChainSettings s;
    s.filters[0].cutoff = 1000.0f;
    applyModulation (s, destF1Cutoff, 0.1f);
    CHECK_NEAR (s.filters[0].cutoff, 1000.0 * std::pow (1000.0, 0.1), 1.0, "cutoff modulation moves in log space (0.1 = one decade/10)");

    s.trash.bands[1].driveDb = 40.0f;
    applyModulation (s, destTrashDrive, 0.5f);
    CHECK_NEAR (s.trash.bands[1].driveDb, kMaxDriveDb, 1.0e-4, "modulation clamps at the top of the range");
}

//==============================================================================
static ChainSettings busySettings()
{
    ChainSettings s;
    s.enabled.fill (true);
    s.filters[0].type = (int) FilterType::highPass12;
    s.filters[0].cutoff = 120.0f;
    s.trash.numBands = 3;
    s.trash.bands[0].driveDb = 18.0f;
    s.trash.bands[1].algoA = (int) Algo::sineFold;
    s.trash.bands[1].driveDb = 20.0f;
    s.trash.bands[2].algoA = (int) Algo::bitcrush;
    s.trash.bands[2].morph = 0.4f;
    s.filters[1].type = (int) FilterType::bandPass12;
    s.filters[1].mix = 0.5f;
    s.convolve.impulse = (int) IrType::tinCan;
    s.motion.mode = (int) MotionMode::phaser;
    s.degrade.wow = 0.4f;
    s.degrade.crackle = 0.3f;
    s.degrade.glitch = 0.3f;
    s.echo.feedback = 0.8f;
    s.slots[0] = { sourceLfo1, destF1Cutoff, 0.3f };
    s.slots[1] = { sourceEnvelope, destTrashDrive, 0.4f };
    s.slots[2] = { sourceLfo2, destEchoTime, -0.2f };
    s.lfos[1].shape = (int) LfoShape::sampleHold;
    return s;
}

static void testChain()
{
    section ("chain");
    const double fs = 48000.0;

    // Everything off: a pure delay by the (constant) latency
    {
        Chain c;
        ChainSettings s;
        s.autoLevel = false;
        c.setSettings (s);
        c.prepare (fs, 512);
        const int latency = c.getLatency();
        const int n = 20000;
        auto l = noise (n, 0.5), r = noise (n, 0.5, 9);
        const auto inL = l, inR = r;
        TransportInfo t;

        for (int start = 0; start < n; start += 300)
        {
            c.setSettings (s);
            c.process (l.data() + start, r.data() + start, std::min (300, n - start), t);
        }

        double maxErr = 0.0;

        for (int i = latency; i < n; ++i)
            maxErr = std::max ({ maxErr, (double) std::abs (l[(size_t) i] - inL[(size_t) (i - latency)]), (double) std::abs (r[(size_t) i] - inR[(size_t) (i - latency)]) });

        CHECK_TRUE (maxErr < 1.0e-6, "all modules off == input delayed by " + std::to_string (latency) + " samples (max err " + std::to_string (maxErr) + ")");
    }

    // Busy settings, shuffled order, random automation: finite; and CPU cost report
    {
        Chain c;
        auto s = busySettings();
        c.setSettings (s);
        c.prepare (fs, 512);
        std::mt19937 rng (5);
        const int n = (int) fs * 10;
        auto l = noise (n, 0.3), r = noise (n, 0.3, 4);
        TransportInfo t;
        t.playing = true;
        double seconds = 0.0;

        for (int start = 0, b = 0; start < n; start += 512, ++b)
        {
            if (b % 40 == 0)
            {
                std::shuffle (s.order.begin(), s.order.end(), rng);
                s.enabled[(size_t) (rng() % numModules)] = rng() % 2 == 0;
                s.trash.bands[0].algoA = (int) (rng() % kNumAlgos);
                s.motion.mode = (int) (rng() % kNumMotionModes);
            }

            c.setSettings (s);
            c.getConvolve().service();
            const int m = std::min (512, n - start);
            const auto t0 = std::chrono::steady_clock::now();
            c.process (l.data() + start, r.data() + start, m, t);
            seconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            t.ppq += m / fs * 2.0;
        }

        CHECK_TRUE (allFinite (l, 30.0f) && allFinite (r, 30.0f), "all modules on, reordering + random switching: finite (peak " + std::to_string (peakOf (l)) + ")");
        std::printf ("    all 8 modules at 48 kHz: %.1f%% of one core (native -O2)\n", 100.0 * seconds / 10.0);
    }
}

//==============================================================================
static std::vector<float> pinkNoise (int n, double rmsDb, uint32_t seed)
{
    std::vector<float> x ((size_t) n);
    Random rng (seed);
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;

    for (auto& v : x)
    {
        const float w = rng.nextBipolar();
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        v = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
    }

    const double scale = dbToGain (rmsDb) / rms (x.data(), n);

    for (auto& v : x)
        v = (float) (v * scale);

    return x;
}

/** Noise with the median long-term spectrum of released music (what the level estimate assumes). */
static std::vector<float> musicNoise (int n, double rmsDb, uint32_t seed, double fs = 48000.0)
{
    const int order = fftOrderFor (n);
    Fft fft (order);
    const int size = fft.getSize();
    std::vector<std::complex<float>> data ((size_t) size);
    Random rng (seed);

    for (auto& v : data)
        v = rng.nextBipolar();

    fft.forward (data.data());

    for (int k = 0; k < size; ++k)
    {
        const double f = std::max (1.0, (k <= size / 2 ? k : size - k) * fs / size);
        const double pos = std::clamp (6.0 * std::log2 (f / 20.0), 0.0, 60.0); // 1/6-octave index
        const int i0 = std::min ((int) pos, 59);
        const double t = pos - i0;
        const double tilted = loudness::musicMedianDb[i0] + t * (loudness::musicMedianDb[i0 + 1] - loudness::musicMedianDb[i0]);
        const double psdDb = tilted - 4.5 * std::log2 (f / 1000.0) - (f < 20.0 ? 40.0 : 0.0);
        data[(size_t) k] *= (float) std::pow (10.0, psdDb / 20.0);
    }

    fft.inverse (data.data());
    std::vector<float> x ((size_t) n);

    for (int i = 0; i < n; ++i)
        x[(size_t) i] = data[(size_t) i].real();

    const double scale = dbToGain (rmsDb) / rms (x.data(), n);

    for (auto& v : x)
        v = (float) (v * scale);

    return x;
}

/** K-weighted power (BS.1770, 48 kHz) of a stereo pair from sample `from`. */
static double kPower (const std::vector<float>& l, const std::vector<float>& r, size_t from)
{
    double sum = 0.0;

    for (const auto* ch : { &l, &r })
    {
        double z[4] = {};

        for (size_t i = 0; i < ch->size(); ++i)
        {
            const double x = (*ch)[i];
            const double y1 = 1.53512485958697 * x + z[0];
            z[0] = -2.69169618940638 * x + 1.69065929318241 * y1 + z[1];
            z[1] = 1.19839281085285 * x - 0.73248077421585 * y1;
            const double y2 = y1 + z[2];
            z[2] = -2.0 * y1 + 1.99004745483398 * y2 + z[3];
            z[3] = y1 - 0.99007225036621 * y2;

            if (i >= from)
                sum += y2 * y2;
        }
    }

    return sum;
}

static void testLevel()
{
    section ("auto level: static estimate vs. measured loudness (music-spectrum noise, -18 dBFS)");
    const double fs = 48000.0;
    const int n = (int) fs * 4;

    struct Case { const char* name; std::function<void (ChainSettings&)> setup; double tolerance; };
    const auto only = [] (ChainSettings& s, int module) { s.enabled.fill (false); s.enabled[(size_t) module] = true; };

    const std::vector<Case> cases = {
        { "input gain +12 dB, nothing on", [&] (ChainSettings& s) { s.enabled.fill (false); s.inputGainDb = 12.0f; }, 0.2 },
        { "filter LP24 400 Hz", [&] (ChainSettings& s) { only (s, moduleFilter1); s.filters[0].type = (int) FilterType::lowPass24; s.filters[0].cutoff = 400.0f; }, 1.5 },
        { "filter HP12 3 kHz reso 60%", [&] (ChainSettings& s) { only (s, moduleFilter1); s.filters[0].type = (int) FilterType::highPass12; s.filters[0].cutoff = 3000.0f; s.filters[0].reso = 0.6f; }, 1.5 },
        { "filter BP24 1 kHz", [&] (ChainSettings& s) { only (s, moduleFilter1); s.filters[0].type = (int) FilterType::bandPass24; s.filters[0].cutoff = 1000.0f; s.filters[0].reso = 0.5f; }, 2.0 },
        { "filter comb+ 200 Hz reso 80%", [&] (ChainSettings& s) { only (s, moduleFilter1); s.filters[0].type = (int) FilterType::combPlus; s.filters[0].cutoff = 200.0f; s.filters[0].reso = 0.8f; }, 2.0 },
        { "filter vowel", [&] (ChainSettings& s) { only (s, moduleFilter1); s.filters[0].type = (int) FilterType::vowel; s.filters[0].cutoff = 300.0f; s.filters[0].reso = 0.5f; }, 3.5 },
        { "trash tube +12 dB", [&] (ChainSettings& s) { only (s, moduleTrash); }, 2.0 },
        { "trash fuzz +30 dB", [&] (ChainSettings& s) { only (s, moduleTrash); s.trash.bands[0].algoA = (int) Algo::fuzz; s.trash.bands[0].driveDb = 30.0f; }, 2.5 },
        { "trash hard clip +24 dB, no auto gain", [&] (ChainSettings& s) { only (s, moduleTrash); s.trash.autoGain = false; s.trash.bands[0].algoA = (int) Algo::hardClip; s.trash.bands[0].driveDb = 24.0f; }, 2.5 },
        { "trash sine fold +20 dB, tone 40%", [&] (ChainSettings& s) { only (s, moduleTrash); s.trash.bands[0].algoA = (int) Algo::sineFold; s.trash.bands[0].driveDb = 20.0f; s.trash.bands[0].tone = 0.4f; }, 2.5 },
        { "trash 3 bands, mixed algos, mix 70%", [&] (ChainSettings& s) { only (s, moduleTrash); s.trash.numBands = 3; s.trash.bands[1].algoA = (int) Algo::bitcrush;
                                                                         s.trash.bands[1].driveDb = 30.0f; s.trash.bands[2].algoA = (int) Algo::octaveFuzz; for (auto& b : s.trash.bands) b.mix = 0.7f; }, 2.5 },
        { "trash tube, input +24 dB", [&] (ChainSettings& s) { only (s, moduleTrash); s.inputGainDb = 24.0f; }, 2.5 },
        { "convolve cab 1x12, 100%", [&] (ChainSettings& s) { only (s, moduleConvolve); s.convolve.mix = 1.0f; }, 2.0 },
        { "convolve concrete hall, 50%", [&] (ChainSettings& s) { only (s, moduleConvolve); s.convolve.impulse = (int) IrType::concreteHall; s.convolve.mix = 0.5f; }, 2.0 },
        { "motion chorus 50%", [&] (ChainSettings& s) { only (s, moduleMotion); }, 1.5 },
        { "motion flanger 50%, feedback 70%", [&] (ChainSettings& s) { only (s, moduleMotion); s.motion.mode = (int) MotionMode::flanger; s.motion.feedback = 0.7f; }, 2.0 },
        { "motion phaser 50%, feedback 50%", [&] (ChainSettings& s) { only (s, moduleMotion); s.motion.mode = (int) MotionMode::phaser; s.motion.feedback = 0.5f; }, 2.0 },
        { "motion vibrato 100%", [&] (ChainSettings& s) { only (s, moduleMotion); s.motion.mode = (int) MotionMode::vibrato; s.motion.mix = 1.0f; }, 1.5 },
        { "motion freq shift 60%, feedback 50%", [&] (ChainSettings& s) { only (s, moduleMotion); s.motion.mode = (int) MotionMode::freqShift; s.motion.mix = 0.6f; s.motion.feedback = 0.5f; }, 2.0 },
        { "motion tremolo full depth", [&] (ChainSettings& s) { only (s, moduleMotion); s.motion.mode = (int) MotionMode::tremolo; s.motion.depth = 1.0f; s.motion.mix = 1.0f; }, 1.5 },
        { "motion ring mod 100%", [&] (ChainSettings& s) { only (s, moduleMotion); s.motion.mode = (int) MotionMode::ringMod; s.motion.mix = 1.0f; }, 1.5 },
        { "degrade age 80%", [&] (ChainSettings& s) { only (s, moduleDegrade); s.degrade.age = 0.8f; s.degrade.noise = 0.0f; }, 2.0 },
        { "dynamics -30 dB 4:1", [&] (ChainSettings& s) { only (s, moduleDynamics); s.dynamics.thresholdDb = -30.0f; s.dynamics.ratio = 4.0f; }, 2.5 },
        { "dynamics -18 dB 2:1 +4 makeup", [&] (ChainSettings& s) { only (s, moduleDynamics); s.dynamics.thresholdDb = -18.0f; s.dynamics.ratio = 2.0f; s.dynamics.makeupDb = 4.0f; }, 2.0 },
        { "dynamics -40 dB 20:1, mix 50%", [&] (ChainSettings& s) { only (s, moduleDynamics); s.dynamics.thresholdDb = -40.0f; s.dynamics.ratio = 20.0f; s.dynamics.mix = 0.5f; }, 2.5 },
        { "dynamics gate -20 dB", [&] (ChainSettings& s) { only (s, moduleDynamics); s.dynamics.ratio = 1.0f; s.dynamics.gateDb = -20.0f; }, 3.0 },
        { "echo 60% feedback, 50% mix", [&] (ChainSettings& s) { only (s, moduleEcho); s.echo.feedback = 0.6f; s.echo.mix = 0.5f; s.echo.drive = 0.0f; }, 2.0 },
        { "echo runaway 115%", [&] (ChainSettings& s) { only (s, moduleEcho); s.echo.feedback = 1.15f; s.echo.mix = 0.5f; }, 3.0 },
    };

    double worst = 0.0;

    for (const auto& c : cases)
    {
        ChainSettings s;
        c.setup (s);
        s.autoLevel = false;
        s.clipGuard = false;

        Chain chain;
        chain.setSettings (s);
        chain.prepare (fs, 512);
        chain.getConvolve().settleForTesting();

        auto l = musicNoise (n, -18.0, 11), r = musicNoise (n, -18.0, 12);
        const auto inL = l, inR = r;
        TransportInfo t;

        for (int start = 0; start < n; start += 512)
        {
            chain.setSettings (s);
            chain.process (l.data() + start, r.data() + start, std::min (512, n - start), t);
        }

        const double measured = 10.0 * std::log10 (kPower (l, r, (size_t) n / 2) / kPower (inL, inR, (size_t) n / 2));
        const double estimated = chain.estimateLevelChange();
        const double err = estimated - measured;
        worst = std::max (worst, std::abs (err));
        std::printf ("    %-40s measured %+6.1f dB   estimated %+6.1f dB   error %+5.1f\n", c.name, measured, estimated, err);
        CHECK_TRUE (std::abs (err) <= c.tolerance, std::string (c.name) + ": estimate within " + std::to_string (c.tolerance) + " dB");
    }

    std::printf ("    worst estimate error %.1f dB\n", worst);

    // With Auto Level on, a +15 dB input push is compensated immediately (no measuring time)
    {
        ChainSettings s;
        s.enabled.fill (false);
        s.inputGainDb = 15.0f;
        Chain chain;
        chain.setSettings (s);
        chain.prepare (fs, 512); // the estimate exists before any audio
        const int m = (int) (fs * 0.5);
        auto l = pinkNoise (m, -18.0, 3), r = pinkNoise (m, -18.0, 4);
        const auto in = l;
        TransportInfo t;

        for (int start = 0; start < m; start += 256)
        {
            chain.setSettings (s);
            chain.process (l.data() + start, r.data() + start, std::min (256, m - start), t);
        }

        CHECK_NEAR (db (rms (l.data() + 1000, m - 1000) / rms (in.data() + 1000, m - 1000)), 0.0, 0.2, "+15 dB input is compensated from the first block on");
    }

    // Clip guard: hot noise never passes -0.3 dBFS, quiet material is untouched
    CHECK_TRUE (ClipGuard::process (0.5f) == 0.5f && ClipGuard::process (-0.7f) == -0.7f, "clip guard is transparent below -3 dBFS");
    float worstPeak = 0.0f;

    for (int i = 0; i < 20000; ++i)
        worstPeak = std::max (worstPeak, std::abs (ClipGuard::process ((float) (i - 10000) * 0.002f)));

    CHECK_TRUE (worstPeak <= ClipGuard::ceiling, "clip guard never exceeds -0.3 dBFS (up to +26 dBFS in)");
}

//==============================================================================
int main()
{
    const auto start = std::chrono::steady_clock::now();
    testShapers();
    testTrash();
    testFilters();
    testMotion();
    testConvolution();
    testDelayDynamicsDegrade();
    testModulation();
    testChain();
    testLevel();
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    std::printf ("\n%d checks, %d failures (%.1f s)\n", checks, failures, secs);
    return failures == 0 ? 0 : 1;
}
