#pragma once

#include "Svf.h"

namespace hl::dsp
{
enum class FilterType
{
    lowPass12, lowPass24, highPass12, highPass24, bandPass12, bandPass24, notch, combPlus, combMinus, vowel, count
};

constexpr int kNumFilterTypes = (int) FilterType::count;

inline const char* const* filterTypeNames() noexcept
{
    static const char* const names[kNumFilterTypes] = { "Low Pass 12", "Low Pass 24", "High Pass 12", "High Pass 24", "Band Pass 12",
                                                        "Band Pass 24", "Notch", "Comb +", "Comb -", "Vowel" };
    return names;
}

struct FilterSettings
{
    int type = (int) FilterType::lowPass24;
    float cutoff = 2000.0f;  // Hz (vowel: position along A-E-I-O-U)
    float reso = 0.3f;       // 0..1
    float driveDb = 0.0f;    // 0..24
    float mix = 1.0f;
};

namespace filterdetail
{
    constexpr float cutoffMin = 20.0f, cutoffMax = 20000.0f;

    inline float q12 (float reso) noexcept { return 0.7071f * std::pow (35.0f, std::clamp (reso, 0.0f, 1.0f)); }
    inline float q24 (float reso) noexcept { return 1.3066f * std::pow (19.0f, std::clamp (reso, 0.0f, 1.0f)); }
    constexpr float q24First = 0.5412f;

    inline float combFeedback (FilterType t, float reso) noexcept
    {
        const float fb = 0.1f + 0.88f * std::clamp (reso, 0.0f, 1.0f);
        return t == FilterType::combMinus ? -fb : fb;
    }

    struct Formant { float f1, f2, f3; };

    inline Formant vowelAt (float cutoff) noexcept
    {
        static const Formant vowels[] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
        const float pos = std::clamp (std::log (cutoff / cutoffMin) / std::log (cutoffMax / cutoffMin), 0.0f, 1.0f) * 4.0f;
        const int i0 = std::min ((int) pos, 3);
        const float t = pos - (float) i0;
        const auto& a = vowels[i0];
        const auto& b = vowels[i0 + 1];
        const auto mixLog = [t] (float x, float y) { return x * std::pow (y / x, t); };
        return { mixLog (a.f1, b.f1), mixLog (a.f2, b.f2), mixLog (a.f3, b.f3) };
    }

    constexpr float formantGains[3] = { 1.0f, 0.63f, 0.4f };

    inline float vowelQ (float reso) noexcept { return 4.0f + 16.0f * std::clamp (reso, 0.0f, 1.0f); }
} // namespace filterdetail

/** Resonant multimode filter with drive. Cutoff and resonance glide per sample so they can be
    modulated at audio-ish rates without zipper noise. */
class FilterModule
{
public:
    void prepare (double rate, int)
    {
        sampleRate = rate;
        glide = onePoleCoeff (0.004, rate);

        for (auto& d : combDelay)
            d.prepare ((int) std::ceil (rate / filterdetail::cutoffMin) + 8);

        duck.reset (rate, 0.004);
        duck.setCurrentAndTarget (1.0f);
        activeType = std::clamp (settings.type, 0, kNumFilterTypes - 1);
        snapToTarget();
        reset();
    }

    void reset()
    {
        for (auto& ch : channels)
            ch = {};

        for (auto& d : combDelay)
            d.reset();
    }

    void setSettings (const FilterSettings& s) noexcept
    {
        settings = s;
        const int wanted = std::clamp (s.type, 0, kNumFilterTypes - 1);

        if (wanted != activeType)
            duck.setTarget (0.0f);
        else if (duck.getTarget() < 1.0f)
            duck.setTarget (1.0f);
    }

    void process (float* left, float* right, int n) noexcept
    {
        const float tLogCutoff = std::log (std::clamp (settings.cutoff, filterdetail::cutoffMin, filterdetail::cutoffMax));
        const float tReso = std::clamp (settings.reso, 0.0f, 1.0f);
        const float tDrive = dbToGain (std::clamp (settings.driveDb, 0.0f, 36.0f));
        const float tMix = std::clamp (settings.mix, 0.0f, 1.0f);
        const float k = 1.0f - glide;
        auto type = (FilterType) activeType;

        for (int i = 0; i < n; ++i)
        {
            logCutoff += (tLogCutoff - logCutoff) * k;
            reso += (tReso - reso) * k;
            drive += (tDrive - drive) * k;
            mix += (tMix - mix) * k;
            const float d = duck.next();

            if (d <= 0.0f && ! duck.isSmoothing() && activeType != std::clamp (settings.type, 0, kNumFilterTypes - 1))
            {
                activeType = std::clamp (settings.type, 0, kNumFilterTypes - 1);
                type = (FilterType) activeType;
                reset();
                duck.setTarget (1.0f);
            }

            const float wet = mix * d;

            if (wet <= 0.0f)
                continue;

            const auto c = makeCoeffs (type, std::exp (logCutoff));
            float* chans[2] = { left, right };

            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = chans[ch][i];
                const float y = processSample (type, ch, x, c);
                chans[ch][i] = x + wet * (y - x);
            }
        }
    }

    /** Magnitude response (linear) of the current settings, for the display. */
    static float magnitude (const FilterSettings& s, double freq, double rate) noexcept
    {
        using namespace filterdetail;
        using C = std::complex<double>;
        const auto type = (FilterType) std::clamp (s.type, 0, kNumFilterTypes - 1);
        const double fc = std::clamp ((double) s.cutoff, (double) cutoffMin, std::min ((double) cutoffMax, rate * 0.45));
        const double f = std::min (freq, rate * 0.4999);
        const double w = std::tan (kPi * f / rate) / std::tan (kPi * fc / rate);
        const C sj (0.0, w);
        const auto den = [&] (double q) { return sj * sj + sj / q + 1.0; };
        C h = 1.0;

        switch (type)
        {
            case FilterType::lowPass12:  h = 1.0 / den (q12 (s.reso)); break;
            case FilterType::lowPass24:  h = 1.0 / (den (q24First) * den (q24 (s.reso))); break;
            case FilterType::highPass12: h = sj * sj / den (q12 (s.reso)); break;
            case FilterType::highPass24: h = (sj * sj) * (sj * sj) / (den (q24First) * den (q24 (s.reso))); break;
            case FilterType::bandPass12: { const double q = q12 (s.reso); h = (sj / q) / den (q); break; }
            case FilterType::bandPass24: { const double q = q12 (s.reso) * 0.7; const C b = (sj / q) / den (q); h = b * b; break; }
            case FilterType::notch:      h = (sj * sj + 1.0) / den (q12 (s.reso)); break;

            case FilterType::combPlus:
            case FilterType::combMinus:
            {
                // loop = delay line + the one-pole damping in processSample (y += 0.35 (x - y))
                const double fb = combFeedback (type, s.reso);
                const double w = 2.0 * kPi * f / rate;
                const C delayed = std::polar (1.0, -w * rate / fc);
                const C damping = 0.35 / (1.0 - 0.65 * std::polar (1.0, -w));
                h = std::sqrt (1.0 - fb * fb) / (1.0 - fb * damping * delayed);
                break;
            }

            case FilterType::vowel:
            {
                const auto fm = vowelAt (s.cutoff);
                const double q = vowelQ (s.reso);
                const double freqs[] = { fm.f1, fm.f2, fm.f3 };
                h = 0.0;

                for (int i = 0; i < 3; ++i)
                {
                    const double wf = std::tan (kPi * f / rate) / std::tan (kPi * std::min (freqs[i], rate * 0.45) / rate);
                    const C sf (0.0, wf);
                    h += (double) formantGains[i] * 1.6 * (sf / q) / (sf * sf + sf / q + 1.0);
                }

                break;
            }

            case FilterType::count:
                break;
        }

        const double m = std::clamp ((double) s.mix, 0.0, 1.0);
        return (float) std::abs ((1.0 - m) + m * h);
    }

private:
    struct ChannelState
    {
        DrivenSvf a, b, c;
        float combDamp = 0.0f;
    };

    static constexpr float stateLimit = 2.0f;

    /** Per-sample coefficients shared by both channels. */
    struct Coeffs
    {
        float g = 0.0f, k1 = 1.0f, k2 = 1.0f, combDelay = 1.0f, fb = 0.0f;
        std::array<float, 3> gFormant {};
    };

    float gFor (float f) const noexcept { return (float) std::tan (kPi * std::min ((double) f, sampleRate * 0.45) / sampleRate); }

    Coeffs makeCoeffs (FilterType type, float cutoff) const noexcept
    {
        using namespace filterdetail;
        Coeffs c;

        switch (type)
        {
            case FilterType::lowPass24:
            case FilterType::highPass24:
                c.g = gFor (cutoff);
                c.k1 = 1.0f / q24First;
                c.k2 = 1.0f / q24 (reso);
                break;

            case FilterType::bandPass24:
                c.g = gFor (cutoff);
                c.k1 = c.k2 = 1.0f / (q12 (reso) * 0.7f);
                break;

            case FilterType::combPlus:
            case FilterType::combMinus:
                c.fb = combFeedback (type, reso);
                c.combDelay = (float) sampleRate / cutoff - 1.0f; // read happens before the push
                break;

            case FilterType::vowel:
            {
                const auto fm = vowelAt (cutoff);
                c.gFormant = { gFor (fm.f1), gFor (fm.f2), gFor (fm.f3) };
                c.k1 = 1.0f / vowelQ (reso);
                break;
            }

            default:
                c.g = gFor (cutoff);
                c.k1 = 1.0f / q12 (reso);
                break;
        }

        return c;
    }

    float processSample (FilterType type, int ch, float x, const Coeffs& c) noexcept
    {
        using namespace filterdetail;
        auto& st = channels[(size_t) ch];
        const float v0 = x * drive;
        const float outGain = 1.0f / std::sqrt (drive);
        float lp, bp, hp, lp2, bp2, hp2;

        switch (type)
        {
            case FilterType::lowPass12:
                st.a.process (v0, c.g, c.k1, stateLimit, lp, bp, hp);
                return lp * outGain;

            case FilterType::lowPass24:
                st.a.process (v0, c.g, c.k1, stateLimit, lp, bp, hp);
                st.b.process (lp, c.g, c.k2, stateLimit, lp2, bp2, hp2);
                return lp2 * outGain;

            case FilterType::highPass12:
                st.a.process (v0, c.g, c.k1, stateLimit, lp, bp, hp);
                return hp * outGain;

            case FilterType::highPass24:
                st.a.process (v0, c.g, c.k1, stateLimit, lp, bp, hp);
                st.b.process (hp, c.g, c.k2, stateLimit, lp2, bp2, hp2);
                return hp2 * outGain;

            case FilterType::bandPass12:
                st.a.process (v0, c.g, c.k1, stateLimit, lp, bp, hp);
                return bp * c.k1 * outGain;

            case FilterType::bandPass24:
                st.a.process (v0, c.g, c.k1, stateLimit, lp, bp, hp);
                st.b.process (bp * c.k1, c.g, c.k2, stateLimit, lp2, bp2, hp2);
                return bp2 * c.k2 * outGain;

            case FilterType::notch:
                st.a.process (v0, c.g, c.k1, stateLimit, lp, bp, hp);
                return (lp + hp) * outGain;

            case FilterType::combPlus:
            case FilterType::combMinus:
            {
                auto& line = combDelay[(size_t) ch];
                const float delayed = line.read (c.combDelay);
                // gentle damping inside the loop keeps high combs from sounding like a buzzer
                st.combDamp += 0.35f * (delayed - st.combDamp);
                const float y = v0 + c.fb * stateLimit * fastTanh (st.combDamp / stateLimit);
                line.push (y);
                return y * std::sqrt (1.0f - c.fb * c.fb) * outGain;
            }

            case FilterType::vowel:
            {
                float sum = 0.0f;
                DrivenSvf* stages[] = { &st.a, &st.b, &st.c };

                for (int f = 0; f < 3; ++f)
                {
                    stages[f]->process (v0, c.gFormant[(size_t) f], c.k1, stateLimit, lp, bp, hp);
                    sum += formantGains[f] * bp * c.k1;
                }

                return 1.6f * sum * outGain;
            }

            case FilterType::count:
                break;
        }

        return x;
    }

    void snapToTarget() noexcept
    {
        logCutoff = std::log (std::clamp (settings.cutoff, filterdetail::cutoffMin, filterdetail::cutoffMax));
        reso = std::clamp (settings.reso, 0.0f, 1.0f);
        drive = dbToGain (settings.driveDb);
        mix = std::clamp (settings.mix, 0.0f, 1.0f);
    }

    double sampleRate = 44100.0;
    float glide = 0.0f;
    FilterSettings settings;
    int activeType = (int) FilterType::lowPass24;
    float logCutoff = std::log (2000.0f), reso = 0.3f, drive = 1.0f, mix = 1.0f;
    LinearSmoother duck;
    std::array<ChannelState, 2> channels {};
    std::array<FractionalDelay, 2> combDelay;
};

} // namespace hl::dsp
