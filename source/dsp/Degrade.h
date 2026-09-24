#pragma once

#include "Svf.h"

namespace hl::dsp
{
struct DegradeSettings
{
    float wow = 0.0f;      // slow pitch drift
    float flutter = 0.0f;  // fast pitch jitter
    float age = 0.0f;      // bandwidth loss + saturation
    float noise = 0.0f;    // tape hiss
    float crackle = 0.0f;  // vinyl clicks and pops
    float dropout = 0.0f;  // random level dips
    float glitch = 0.0f;   // probability of stutter / reverse / half-speed repeats
    float mix = 1.0f;
};

/** Broken-media emulation: a tape transport, worn heads, dust, dropouts and a glitch sampler. */
class DegradeModule
{
public:
    void prepare (double rate, int)
    {
        sampleRate = rate;
        glide = onePoleCoeff (0.02, rate);
        centreGlide = onePoleCoeff (0.25, rate);
        dropoutGlide = onePoleCoeff (0.008, rate);
        envGlide = onePoleCoeff (0.003, rate);

        for (auto& d : tape)
            d.prepare ((int) std::ceil (rate * 0.02));

        for (auto& b : glitchBuffer)
            b.assign ((size_t) (rate * 2.5), 0.0f);

        wow = settings.wow;
        flutter = settings.flutter;
        age = settings.age;
        mix = settings.mix;
        reset();
    }

    void reset()
    {
        for (auto& d : tape)
            d.reset();

        for (auto& b : glitchBuffer)
            std::fill (b.begin(), b.end(), 0.0f);

        for (auto& ch : channels)
        {
            ch = {};
            ch.clickHp.setCutoff (700.0, sampleRate);
        }

        channels[1].noise.seed (0x7654321u); // independent hiss and dust per side
        wowPhase = flutterPhase = 0.0;
        wowRandom.reset();
        flutterRandom.reset();
        centre = 0.0f;
        dropGain = dropTarget = 1.0f;
        dropRemaining = 0;
        writePos = 0;
        gridCounter = 0;
        glitch = {};
        glitchEnv = 0.0f;
        lastDelay = 0.0f;
        pitchCents = 0.0f;
    }

    void setSettings (const DegradeSettings& s) noexcept { settings = s; }
    void setTempo (double bpm) noexcept { tempo = std::clamp (bpm, 30.0, 300.0); }

    /** For the display: current transport pitch error, glitch state and dropout gain. */
    float getPitchCents() const noexcept { return pitchCents; }
    bool isGlitching() const noexcept { return glitch.active; }
    float getDropoutGain() const noexcept { return dropGain; }

    void process (float* left, float* right, int n) noexcept
    {
        const float k = 1.0f - glide;
        const float fs = (float) sampleRate;
        float* chans[2] = { left, right };

        // Block-rate coefficients for the "worn" filters
        age += (std::clamp (settings.age, 0.0f, 1.0f) - age) * std::min (1.0f, k * (float) n);
        const float lpBase = 20000.0f * std::pow (2800.0f / 20000.0f, age);
        const auto hpCoeffs = SvfCoeffs::make (SvfShape::highPass, 10.0 * std::pow (22.0, (double) age), 0.6, 0.0, sampleRate);
        const auto lpCoeffs = SvfCoeffs::make (SvfShape::lowPass, std::min ((double) lpBase * (0.25 + 0.75 * dropGain), sampleRate * 0.45), 0.75, 0.0, sampleRate);
        const float satDrive = 1.0f + 1.5f * age;
        const bool worn = age > 1.0e-3f;

        const float hissLevel = settings.noise > 0.0f ? dbToGain (-72.0f + 42.0f * std::clamp (settings.noise, 0.0f, 1.0f)) : 0.0f;
        const float crackle = std::clamp (settings.crackle, 0.0f, 1.0f);
        const float smallRate = 40.0f * crackle * crackle / fs, bigRate = 3.0f * crackle * crackle / fs;
        const float dropoutAmount = std::clamp (settings.dropout, 0.0f, 1.0f);
        const float dropRate = 3.0f * dropoutAmount * dropoutAmount / fs;
        const float glitchAmount = std::clamp (settings.glitch, 0.0f, 1.0f);
        const int gridSamples = std::max (64, (int) (sampleRate * 60.0 / tempo * 0.25)); // sixteenth notes
        const int bufferSize = (int) glitchBuffer[0].size();

        for (int i = 0; i < n; ++i)
        {
            wow += (settings.wow - wow) * k;
            flutter += (settings.flutter - flutter) * k;
            mix += (std::clamp (settings.mix, 0.0f, 1.0f) - mix) * k;

            // --- glitch sampler: decisions on a sixteenth-note grid -----------------------
            if (++gridCounter >= gridSamples)
            {
                gridCounter = 0;

                if (glitch.active && --glitch.gridsLeft <= 0)
                    glitch.active = false;

                if (! glitch.active && glitchAmount > 0.0f && rng.nextFloat() < 0.7f * glitchAmount)
                {
                    static const float sliceFractions[] = { 0.125f, 0.25f, 0.5f, 1.0f };
                    glitch.active = true;
                    glitch.sliceLength = std::max (32, (int) ((float) gridSamples * sliceFractions[rng.nextInt() % 4]));
                    glitch.start = (writePos - glitch.sliceLength + bufferSize) % bufferSize;
                    glitch.elapsed = 0.0f;
                    glitch.gridsLeft = 1 + (int) (rng.nextInt() % 2);
                    const float r = rng.nextFloat();
                    glitch.mode = r < 0.55f ? 0 : (r < 0.8f ? 1 : 2); // repeat, reverse, half speed
                }
            }

            glitchEnv += ((glitch.active ? 1.0f : 0.0f) - glitchEnv) * (1.0f - envGlide);

            // --- tape transport ----------------------------------------------------------
            wowPhase += 0.55 / sampleRate;
            flutterPhase += 8.7 / sampleRate;
            wowPhase -= std::floor (wowPhase);
            flutterPhase -= std::floor (flutterPhase);

            const float wowMod = 0.7f * std::sin (kTwoPiF * (float) wowPhase) + 0.3f * wowRandom.next (0.9f, fs);
            const float flutterMod = 0.6f * std::sin (kTwoPiF * (float) flutterPhase) + 0.4f * flutterRandom.next (19.0f, fs);
            const float wowDepth = 5.0e-3f * fs * wow, flutterDepth = 0.35e-3f * fs * flutter;
            centre += (wowDepth + flutterDepth + (wowDepth + flutterDepth > 0.0f ? 1.5f : 0.0f) - centre) * (1.0f - centreGlide);
            const float delay = centre + wowDepth * wowMod + flutterDepth * flutterMod;
            const bool transport = centre > 0.05f;

            pitchCents = transport ? 1200.0f * std::log2 (std::max (0.5f, 1.0f - (delay - lastDelay))) : 0.0f;
            lastDelay = delay;

            // --- dropouts ------------------------------------------------------------------
            if (dropRemaining > 0)
            {
                if (--dropRemaining == 0)
                    dropTarget = 1.0f;
            }
            else if (dropRate > 0.0f && rng.nextFloat() < dropRate)
            {
                dropTarget = dbToGain (-(8.0f + 30.0f * dropoutAmount * rng.nextFloat()));
                dropRemaining = (int) (sampleRate * (0.03 + 0.25 * rng.nextFloat()));
            }

            dropGain = dropTarget + (dropGain - dropTarget) * dropoutGlide;

            for (int ch = 0; ch < 2; ++ch)
            {
                auto& st = channels[(size_t) ch];
                auto& buffer = glitchBuffer[(size_t) ch];
                const float dry = chans[ch][i];
                buffer[(size_t) writePos] = dry;
                float y = dry;

                if (glitchEnv > 1.0e-4f)
                    y += glitchEnv * (readSlice (buffer) - y);

                auto& line = tape[(size_t) ch];
                line.push (y);

                if (transport)
                    y = line.read (std::max (1.0f, delay));

                if (worn)
                {
                    y = st.hp.process (hpCoeffs, y);
                    y = st.lp.process (lpCoeffs, y);
                    y = fastTanh (y * satDrive) / satDrive;
                }

                y *= dropGain;

                if (hissLevel > 0.0f)
                {
                    const float white = st.noise.nextBipolar();
                    st.hiss = white - 0.55f * st.hissPrev; // tilt the hiss towards the top end
                    st.hissPrev = white;
                    y += hissLevel * st.hiss;
                }

                if (crackle > 0.0f)
                {
                    const float r = st.noise.nextFloat();

                    if (r < bigRate)
                    {
                        st.clickEnv = (0.25f + 0.5f * st.noise.nextFloat()) * (0.3f + 0.7f * crackle);
                        st.clickDecay = onePoleCoeff (0.0015, sampleRate);
                    }
                    else if (r < bigRate + smallRate)
                    {
                        st.clickEnv = std::max (st.clickEnv, (0.02f + 0.15f * st.noise.nextFloat()) * (0.4f + 0.6f * crackle));
                        st.clickDecay = onePoleCoeff (0.0003, sampleRate);
                    }

                    if (st.clickEnv > 1.0e-5f)
                    {
                        const float click = st.clickEnv * st.noise.nextBipolar();
                        st.clickEnv *= st.clickDecay;
                        y += st.clickHp.highPass (click);
                    }
                }

                chans[ch][i] = dry + mix * (y - dry);
            }

            if (glitch.active || glitchEnv > 1.0e-4f)
                glitch.elapsed += 1.0f;

            writePos = (writePos + 1) % bufferSize;
        }
    }

private:
    struct ChannelState
    {
        SvfState hp, lp;
        Random noise { 0x1234567u };
        float hiss = 0.0f, hissPrev = 0.0f;
        float clickEnv = 0.0f, clickDecay = 0.0f;
        OnePole clickHp;
    };

    struct GlitchState
    {
        bool active = false;
        int start = 0, sliceLength = 1024, gridsLeft = 0, mode = 0;
        float elapsed = 0.0f;
    };

    float readSlice (const std::vector<float>& buffer) const noexcept
    {
        const int size = (int) buffer.size();
        const float len = (float) glitch.sliceLength;
        const float t = glitch.mode == 2 ? glitch.elapsed * 0.5f : glitch.elapsed;
        float pos = std::fmod (t, len);

        if (glitch.mode == 1)
            pos = len - 1.0f - pos;

        const int i0 = (int) pos;
        const float frac = pos - (float) i0;
        const float a = buffer[(size_t) ((glitch.start + i0) % size)];
        const float b = buffer[(size_t) ((glitch.start + i0 + 1) % size)];

        // short fades at the slice edges so repeats don't click
        const float edge = std::min (pos, len - pos);
        const float window = std::min (1.0f, edge / 48.0f);
        return (a + frac * (b - a)) * window;
    }

    double sampleRate = 44100.0, tempo = 120.0;
    float glide = 0.0f, centreGlide = 0.0f, dropoutGlide = 0.0f, envGlide = 0.0f;
    DegradeSettings settings;
    float wow = 0.0f, flutter = 0.0f, age = 0.0f, mix = 1.0f;
    double wowPhase = 0.0, flutterPhase = 0.0;
    SmoothRandom wowRandom { 777u }, flutterRandom { 4242u };
    float centre = 0.0f, lastDelay = 0.0f, pitchCents = 0.0f;
    float dropGain = 1.0f, dropTarget = 1.0f;
    int dropRemaining = 0;
    Random rng { 0xC0FFEEu };
    std::array<FractionalDelay, 2> tape;
    std::array<std::vector<float>, 2> glitchBuffer;
    int writePos = 0, gridCounter = 0;
    GlitchState glitch;
    float glitchEnv = 0.0f;
    std::array<ChannelState, 2> channels {};
};

} // namespace hl::dsp
