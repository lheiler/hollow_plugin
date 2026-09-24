// Headless test harness: renders audio through the real processor, checks invariants and writes
// editor screenshots. Usage: HollowSnapshot [outputDirectory] [--renders]

#include "plugin/PluginEditor.h"
#include "plugin/Presets.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>
#include <random>

using namespace juce;
using hl::HollowAudioProcessor;
namespace pid = hl::params::id;

namespace
{
int failures = 0;

void check (bool ok, const String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;

    if (! ok)
        ++failures;
}

/** Deterministic little "song": kick, snare, hats, bass, a plucked lead and a wide pad. */
class TestSong
{
public:
    explicit TestSong (double rate) : sampleRate (rate) {}

    void render (AudioBuffer<float>& buffer)
    {
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

        for (int i = 0; i < buffer.getNumSamples(); ++i, ++n)
        {
            const double t = (double) n / sampleRate;
            const double beat = t * 2.0; // 120 bpm
            const double inBeat = beat - std::floor (beat);
            const int beatIndex = (int) std::floor (beat);
            const double twoPi = MathConstants<double>::twoPi;

            kickPhase += twoPi * (45.0 + 90.0 * std::exp (-inBeat * 30.0)) / sampleRate;
            const double kick = 0.9 * std::sin (kickPhase) * std::exp (-inBeat * 7.0);

            const double snareEnv = (beatIndex % 2 == 1) ? std::exp (-inBeat * 18.0) : 0.0;
            const double snare = snareEnv * (0.35 * noise() + 0.25 * std::sin (twoPi * 185.0 * t));

            const double eighth = beat * 2.0 - std::floor (beat * 2.0);
            const double hatRaw = noise();
            const double hat = 0.12 * std::exp (-eighth * 40.0) * (hatRaw - hatPrev);
            hatPrev = hatRaw;

            static const double roots[] = { 55.0, 43.65, 49.0, 41.2 };
            const double root = roots[(beatIndex / 4) % 4];
            double bass = 0.0;

            for (int h = 1; h <= 6; ++h)
                bass += std::sin (twoPi * root * h * t) / h;

            bass *= 0.18;

            // Plucked lead on sixteenths: a decaying saw, the thing distortion loves
            static const double notes[] = { 4.0, 6.0, 8.0, 6.0, 9.0, 8.0, 6.0, 4.0 };
            const double sixteenth = beat * 4.0;
            const double inSixteenth = sixteenth - std::floor (sixteenth);
            const double noteFreq = root * notes[(int) sixteenth % 8];
            leadPhase += noteFreq / sampleRate;
            leadPhase -= std::floor (leadPhase);
            const double lead = 0.16 * (2.0 * leadPhase - 1.0) * std::exp (-inSixteenth * 6.0);

            double padL = 0.0, padR = 0.0;

            for (double ratio : { 2.0, 2.52, 3.0, 4.0 })
            {
                padL += std::sin (twoPi * root * ratio * 1.003 * t);
                padR += std::sin (twoPi * root * ratio * 0.997 * t + 0.5);
            }

            const double left = kick + snare + hat + bass + lead + 0.05 * padL;
            const double right = kick + snare - hat * 0.6 + bass + lead * 0.8 + 0.05 * padR;

            l[i] = (float) (0.5 * left);

            if (r != nullptr)
                r[i] = (float) (0.5 * right);
        }
    }

private:
    double noise() { return dist (rng); }

    double sampleRate;
    int64 n = 0;
    double kickPhase = 0.0, hatPrev = 0.0, leadPhase = 0.0;
    std::mt19937 rng { 1234 };
    std::uniform_real_distribution<double> dist { -1.0, 1.0 };
};

/** Stereo noise with the median long-term spectrum of released music at -18 LUFS: what the Auto Level
    estimate assumes, so the loudness checks measure the estimate rather than the test song's odd balance. */
class MusicNoise
{
public:
    explicit MusicNoise (double rate)
    {
        for (int ch = 0; ch < 2; ++ch)
            channels[(size_t) ch] = make (rate, 1234u + (uint32_t) ch);
    }

    void render (AudioBuffer<float>& buffer)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i, pos = (pos + 1) % (int) channels[0].size())
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, i, channels[(size_t) jmin (ch, 1)][(size_t) pos]);
    }

private:
    static std::vector<float> make (double rate, uint32_t seed)
    {
        namespace ld = hl::dsp::loudness;
        const int order = 18;
        hl::dsp::Fft fft (order);
        const int size = fft.getSize();
        std::vector<std::complex<float>> data ((size_t) size);
        hl::dsp::Random rng (seed);

        for (auto& v : data)
            v = rng.nextBipolar();

        fft.forward (data.data());

        for (int k = 0; k < size; ++k)
        {
            const double f = jmax (1.0, (k <= size / 2 ? k : size - k) * rate / size);
            const double pos = jlimit (0.0, 60.0, 6.0 * std::log2 (f / 20.0));
            const int i0 = jmin ((int) pos, 59);
            const double tilted = ld::musicMedianDb[i0] + (pos - i0) * (ld::musicMedianDb[i0 + 1] - ld::musicMedianDb[i0]);
            data[(size_t) k] *= (float) std::pow (10.0, (tilted - 4.5 * std::log2 (f / 1000.0) - (f < 20.0 ? 40.0 : 0.0)) / 20.0);
        }

        fft.inverse (data.data());
        std::vector<float> out ((size_t) size);
        double sum = 0.0;

        for (int i = 0; i < size; ++i)
        {
            out[(size_t) i] = data[(size_t) i].real();
            sum += (double) out[(size_t) i] * out[(size_t) i];
        }

        // -18 dB RMS; K-weighting of this spectrum is within a fraction of a dB of unweighted
        const float gain = (float) (std::pow (10.0, -18.0 / 20.0) / std::sqrt (sum / size));

        for (auto& v : out)
            v *= gain;

        return out;
    }

    std::array<std::vector<float>, 2> channels;
    int pos = 0;
};

void set (AudioProcessorValueTreeState& s, const String& paramId, float plain)
{
    auto* p = s.getParameter (paramId);
    jassert (p != nullptr);
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

int presetIndex (const String& name)
{
    const auto& list = hl::presets::all();

    for (size_t i = 0; i < list.size(); ++i)
        if (name == list[i].name)
            return (int) i;

    jassertfalse;
    return 0;
}

/** ITU-R BS.1770 K-weighting (48 kHz coefficients): what "loudness" means for the preset balance. */
struct KWeighting
{
    struct Biquad
    {
        double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0;

        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
    Biquad highPass { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };
    double sum = 0.0;
    int64 count = 0;

    void add (float x) noexcept
    {
        const double y = highPass.process (shelf.process (x));
        sum += y * y;
        ++count;
    }

    double meanSquare() const noexcept { return sum / (double) jmax ((int64) 1, count); }
};

struct RenderStats
{
    bool finite = true;
    float peak = 0.0f;
    double rmsIn = 0.0, rmsOut = 0.0;
    double loudnessDiff = 0.0; // K-weighted, output minus input, in LU (skipping the first second)
    double seconds = 0.0, cpuSeconds = 0.0;
};

template <typename Source>
RenderStats render (HollowAudioProcessor& proc, Source& song, double rate, double seconds, int numChannels,
                    const std::function<void (int64)>& perBlock = {}, AudioBuffer<float>* capture = nullptr,
                    double measureFromSeconds = 1.0)
{
    RenderStats stats;
    const int blockSizes[] = { 512, 37, 1024, 256, 480, 2048 }; // includes blocks larger than prepared
    MidiBuffer midi;
    int64 done = 0, blockIndex = 0;
    const auto total = (int64) (seconds * rate);
    double sumIn = 0.0, sumOut = 0.0;
    std::array<KWeighting, 2> kIn, kOut;

    if (capture != nullptr)
        capture->setSize (2, (int) total);

    while (done < total)
    {
        const int n = (int) jmin ((int64) blockSizes[blockIndex++ % 6], total - done);
        AudioBuffer<float> buffer (numChannels, n);
        song.render (buffer);

        const bool measure = done >= (int64) (measureFromSeconds * rate);

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < n; ++i)
            {
                sumIn += (double) buffer.getSample (ch, i) * buffer.getSample (ch, i);

                if (measure)
                    kIn[(size_t) jmin (ch, 1)].add (buffer.getSample (ch, i));
            }

        if (perBlock)
            perBlock (done);

        const auto start = Time::getHighResolutionTicks();
        proc.processBlock (buffer, midi);
        stats.cpuSeconds += Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - start);

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float v = buffer.getSample (ch, i);
                stats.finite = stats.finite && std::isfinite (v);
                stats.peak = jmax (stats.peak, std::abs (v));
                sumOut += (double) v * v;

                if (measure)
                    kOut[(size_t) jmin (ch, 1)].add (v);

                if (capture != nullptr)
                    capture->setSample (jmin (ch, 1), (int) (done + i), v);
            }

        if (capture != nullptr && numChannels == 1)
            capture->copyFrom (1, (int) done, *capture, 0, (int) done, n);

        done += n;
    }

    stats.loudnessDiff = 10.0 * std::log10 (jmax (1.0e-20, kOut[0].meanSquare() + kOut[1].meanSquare())
                                            / jmax (1.0e-20, kIn[0].meanSquare() + kIn[1].meanSquare()));
    stats.seconds = seconds;
    stats.rmsIn = std::sqrt (sumIn / (double) jmax ((int64) 1, total * numChannels));
    stats.rmsOut = std::sqrt (sumOut / (double) jmax ((int64) 1, total * numChannels));
    return stats;
}

float getOutputGain (HollowAudioProcessor& proc)
{
    return proc.getState().getRawParameterValue (pid::outputGain)->load();
}

void prepare (HollowAudioProcessor& proc, double rate)
{
    proc.setPlayConfigDetails (2, 2, rate, 512);
    proc.prepareToPlay (rate, 512);
}

//==============================================================================
void testPresets (const File& outDir, bool writeRenders, bool calibrate)
{
    std::cout << "\n[presets: 6 s of music-spectrum noise (-18 LUFS) through every factory preset at 48 kHz]" << std::endl;
    const double rate = 48000.0;
    const auto& list = hl::presets::all();
    int finite = 0;
    float worstPeak = 0.0f, worstLevel = 0.0f;
    double worstCpu = 0.0;
    String worstCpuName;
    const File renderDir = outDir.getChildFile ("renders");

    if (writeRenders)
        renderDir.createDirectory();

    for (size_t i = 0; i < list.size(); ++i)
    {
        HollowAudioProcessor proc;
        proc.loadPreset ((int) i);
        prepare (proc, rate);
        proc.waitForImpulse (5000);

        MusicNoise noise (rate);
        TestSong song (rate);
        AudioBuffer<float> capture;
        const auto stats = writeRenders ? render (proc, song, rate, 6.0, 2, {}, &capture, 1.0)
                                        : render (proc, noise, rate, 6.0, 2, {}, nullptr, 1.0);
        const float levelDb = (float) stats.loudnessDiff;
        const double cpu = 100.0 * stats.cpuSeconds / stats.seconds;

        finite += stats.finite ? 1 : 0;
        worstPeak = jmax (worstPeak, stats.peak);
        worstLevel = jmax (worstLevel, std::abs (levelDb));

        if (cpu > worstCpu)
        {
            worstCpu = cpu;
            worstCpuName = list[i].name;
        }

        if (calibrate)
            std::cout << "        { \"" << list[i].name << "\", " << String (getOutputGain (proc) - levelDb, 1) << " }," << std::endl;

        std::cout << "        " << String (list[i].name).paddedRight (' ', 22) << " loudness " << String (levelDb, 1).paddedLeft (' ', 6)
                  << " LU   peak " << String (Decibels::gainToDecibels (stats.peak), 1).paddedLeft (' ', 6) << " dBFS   cpu "
                  << String (cpu, 2) << "%" << std::endl;

        if (writeRenders)
        {
            const auto file = renderDir.getChildFile (String (list[i].name).replaceCharacter (' ', '-').toLowerCase() + ".wav");
            file.deleteFile();

            if (auto stream = file.createOutputStream())
            {
                WavAudioFormat wav;
                std::unique_ptr<OutputStream> out (stream.release());

                if (auto writer = wav.createWriterFor (out, AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24)))
                    writer->writeFromAudioSampleBuffer (capture, 0, capture.getNumSamples());
            }
        }
    }

    check (finite == (int) list.size(), String (finite) + "/" + String ((int) list.size()) + " presets render finite audio");
    check (worstPeak <= hl::dsp::ClipGuard::ceiling, "no preset peaks above -0.3 dBFS (worst " + String (Decibels::gainToDecibels (worstPeak), 2) + " dBFS)");
    check (worstLevel < 4.0f, "every preset comes out within 4 LU of the input loudness (worst " + String (worstLevel, 2) + " LU)");
    std::cout << "        heaviest preset: " << worstCpuName << " at " << String (worstCpu, 2) << "% of one core" << std::endl;
}

void testRates()
{
    std::cout << "\n[sample rates: everything on]" << std::endl;

    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        HollowAudioProcessor proc;
        proc.loadPreset (presetIndex ("Shoegaze Wall"));
        set (proc.getState(), pid::moduleOn (hl::dsp::moduleDegrade), 1.0f);
        set (proc.getState(), pid::moduleOn (hl::dsp::moduleFilter1), 1.0f);
        set (proc.getState(), pid::trashBands, 2.0f);
        prepare (proc, rate);
        proc.waitForImpulse (5000);

        TestSong song (rate);
        const auto stats = render (proc, song, rate, 4.0, 2);
        const auto label = String (rate / 1000.0, 1) + " kHz";
        check (stats.finite && stats.peak < 8.0f, label + ": finite, peak " + String (Decibels::gainToDecibels (stats.peak), 1) + " dBFS");
        std::cout << "        latency " << proc.getLatencySamples() << " samples (" << String (1000.0 * proc.getLatencySamples() / rate, 2)
                  << " ms), CPU " << String (100.0 * stats.cpuSeconds / stats.seconds, 2) << "% of one core" << std::endl;
    }
}

void testAutomationStress()
{
    std::cout << "\n[automation stress]" << std::endl;
    const double rate = 48000.0;
    HollowAudioProcessor proc;
    prepare (proc, rate);

    std::mt19937 rng (99);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    auto params = proc.getParameters();

    TestSong song (rate);
    const auto stats = render (proc, song, rate, 8.0, 2, [&] (int64)
    {
        for (int k = 0; k < 12; ++k)
        {
            auto* p = params[(int) (u (rng) * (float) params.size()) % params.size()];

            const bool excluded = p == proc.getBypassParameter() || p == proc.getState().getParameter (pid::outputGain)
                               || p == proc.getState().getParameter (pid::inputGain) || p == proc.getState().getParameter (pid::clipGuard);

            if (! excluded)
                p->setValueNotifyingHost (u (rng));
        }

        if (u (rng) < 0.02f)
        {
            auto order = hl::dsp::defaultModuleOrder();
            std::shuffle (order.begin(), order.end(), rng);
            proc.setModuleOrder (order);
        }
    });

    check (stats.finite && stats.peak <= hl::dsp::ClipGuard::ceiling,
           "random parameter changes every block: finite and under the ceiling (peak " + String (Decibels::gainToDecibels (stats.peak), 2) + " dBFS)");
}

void testRandomize()
{
    std::cout << "\n[dice: 40 random patches]" << std::endl;
    const double rate = 48000.0;
    int ok = 0, matched = 0;
    float worst = 0.0f;
    double worstLevel = 0.0;
    StringArray levels;

    for (int roll = 0; roll < 40; ++roll)
    {
        HollowAudioProcessor proc;
        proc.randomize();
        prepare (proc, rate);
        proc.waitForImpulse (5000);
        MusicNoise noise (rate);
        const auto stats = render (proc, noise, rate, 6.0, 2, {}, nullptr, 1.0);
        worst = jmax (worst, stats.peak);
        worstLevel = jmax (worstLevel, std::abs (stats.loudnessDiff));
        ok += stats.finite ? 1 : 0;
        matched += std::abs (stats.loudnessDiff) < 6.0 ? 1 : 0;
        levels.add (String (stats.loudnessDiff, 1));
    }

    std::cout << "        loudness vs input (LU): " << levels.joinIntoString (" ") << std::endl;
    check (ok == 40, "all random patches render finite audio");
    check (matched >= 36, String (matched) + "/40 random patches within 6 LU of the input (worst " + String (worstLevel, 2) + " LU)");
    check (worst <= hl::dsp::ClipGuard::ceiling, "worst random peak " + String (Decibels::gainToDecibels (worst), 2) + " dBFS (ceiling -0.3)");
}

void testHotInput()
{
    std::cout << "\n[input knob pushed +24 dB]" << std::endl;
    const double rate = 48000.0;

    for (auto* name : { "Warm Tube Glue", "Fuzz Wall", "Init" })
    {
        HollowAudioProcessor proc;
        proc.loadPreset (presetIndex (name));
        set (proc.getState(), pid::inputGain, 24.0f);
        prepare (proc, rate);
        proc.waitForImpulse (5000);
        MusicNoise noise (rate);
        const auto stats = render (proc, noise, rate, 6.0, 2, {}, nullptr, 1.0);
        check (std::abs (stats.loudnessDiff) < 4.5 && stats.peak <= hl::dsp::ClipGuard::ceiling,
               String (name) + ": same loudness as the input (" + String (stats.loudnessDiff, 2) + " LU), peak "
                   + String (Decibels::gainToDecibels (stats.peak), 2) + " dBFS");
    }
}

void testStateRoundTrip()
{
    std::cout << "\n[state]" << std::endl;
    HollowAudioProcessor a;
    a.loadPreset (presetIndex ("Glitch Machine"));
    a.setModuleOrder ({ 3, 0, 1, 2, 4, 7, 6, 5 });

    MemoryBlock data;
    a.getStateInformation (data);

    HollowAudioProcessor b;
    b.setStateInformation (data.getData(), (int) data.getSize());

    int mismatches = 0;
    const auto pa = a.getParameters(), pb = b.getParameters();

    for (int i = 0; i < pa.size(); ++i)
        if (std::abs (pa[i]->getValue() - pb[i]->getValue()) > 1.0e-6f)
            ++mismatches;

    check (pa.size() == pb.size() && mismatches == 0, String (pa.size()) + " parameters restored (" + String (mismatches) + " mismatches)");
    check (b.getModuleOrder() == hl::dsp::ModuleOrder { 3, 0, 1, 2, 4, 7, 6, 5 }, "module order restored");
    check (b.getPresetName() == "Glitch Machine", "preset name restored");
}

void testUserPresets (const File& outDir)
{
    std::cout << "\n[user presets]" << std::endl;
    const auto folder = outDir.getChildFile ("user-presets");
    folder.deleteRecursively();

    HollowAudioProcessor a;
    a.setUserPresetFolder (folder);
    a.loadPreset (presetIndex ("Glitch Machine"));
    set (a.getState(), pid::macro (1), 42.0f);
    set (a.getState(), pid::trash (1, "Drive"), 21.5f);
    a.setModuleOrder ({ 7, 6, 5, 4, 3, 2, 1, 0 });
    const auto file = a.saveUserPreset ("My Test Patch");
    check (file.existsAsFile() && file.getParentDirectory() == folder, "saved " + file.getFileName());

    HollowAudioProcessor b;
    b.setUserPresetFolder (folder);
    set (b.getState(), pid::clipGuard, 0.0f); // a global setting: presets must leave it alone
    check (b.getUserPresets().size() == 1, "the new preset is listed");
    check (b.loadUserPreset (b.getUserPresets()[0]), "loads");

    int mismatches = 0;

    for (auto* p : a.getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            if (! HollowAudioProcessor::isGlobalSetting (rp->paramID)
                && std::abs (rp->getValue() - b.getState().getParameter (rp->paramID)->getValue()) > 1.0e-5f)
                ++mismatches;

    check (mismatches == 0, "all sound parameters restored (" + String (mismatches) + " mismatches)");
    check (b.getModuleOrder() == hl::dsp::ModuleOrder { 7, 6, 5, 4, 3, 2, 1, 0 }, "chain order restored");
    check (b.getPresetName() == "My Test Patch" && b.getCurrentUserPreset() == file, "name and file shown");
    check (b.getState().getRawParameterValue (pid::clipGuard)->load() < 0.5f, "loading a preset keeps the global Clip Guard setting");

    // Import: from elsewhere, never overwriting, identical copies skipped, junk rejected
    const auto elsewhere = outDir.getChildFile ("to-import");
    elsewhere.deleteRecursively();
    elsewhere.createDirectory();
    const auto sameName = elsewhere.getChildFile ("My Test Patch.hollowpreset");
    set (a.getState(), pid::trash (0, "Drive"), 3.0f);
    a.setUserPresetFolder (elsewhere);
    a.saveUserPreset ("My Test Patch");                        // same name, different sound
    elsewhere.getChildFile ("junk.hollowpreset").replaceWithText ("not xml");
    StringArray problems;
    const auto imported = b.importPresets ({ sameName, file, elsewhere.getChildFile ("junk.hollowpreset") }, &problems);
    check (imported.size() == 2 && imported[0].getFileName() == "My Test Patch (2).hollowpreset" && imported[1] == file,
           "import keeps both same-named presets and skips identical copies");
    check (problems.size() == 1 && problems[0].contains ("junk"), "import rejects non-presets (" + problems.joinIntoString ("; ") + ")");
    check (b.importPresets ({ sameName }).getFirst().getFileName() == "My Test Patch (2).hollowpreset", "importing again doesn't duplicate");

    check (b.deleteUserPreset (file) && b.getUserPresets().size() == 1, "deletes");
    folder.deleteRecursively();
    elsewhere.deleteRecursively();
}

void testUndo()
{
    std::cout << "\n[undo / redo]" << std::endl;
    HollowAudioProcessor proc;

    const auto gesture = [&] (const String& paramId, float plain)
    {
        auto* p = proc.getState().getParameter (paramId);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
        proc.flushHistory(); // (a plugin records it on the next message loop turn)
    };

    const auto drive = [&] { return proc.getState().getRawParameterValue (pid::trash (0, "Drive"))->load(); };
    gesture (pid::trash (0, "Drive"), 30.0f);
    gesture (pid::trash (0, "Drive"), 40.0f);
    proc.loadPreset (presetIndex ("Fuzz Wall"));
    proc.flushHistory();
    proc.setModuleOrder ({ 7, 6, 5, 4, 3, 2, 1, 0 });
    proc.flushHistory();

    proc.undo();
    check (proc.getModuleOrder() == hl::dsp::defaultModuleOrder() && proc.getPresetName() == "Fuzz Wall", "undo reverts the reorder only");
    proc.undo();
    check (std::abs (drive() - 40.0f) < 0.01f && proc.getPresetName() != "Fuzz Wall", "undo reverts a whole preset load in one step");
    proc.undo();
    check (std::abs (drive() - 30.0f) < 0.01f, "undo reverts a knob gesture");
    proc.redo();
    proc.redo();
    check (proc.getPresetName() == "Fuzz Wall" && std::abs (drive() - 30.0f) < 0.01f, "redo re-applies the preset");
    gesture (pid::trash (0, "Mix"), 50.0f);
    check (! proc.canRedo(), "a new change clears redo");
}

void testOversampling()
{
    std::cout << "\n[oversampling]" << std::endl;
    const double rate = 48000.0;

    for (int choice = 0; choice < 4; ++choice)
    {
        HollowAudioProcessor proc;
        proc.loadPreset (presetIndex ("Multiband Crunch"));
        set (proc.getState(), pid::oversampling, (float) choice);
        prepare (proc, rate);
        TestSong song (rate);
        const auto stats = render (proc, song, rate, 3.0, 2);
        check (stats.finite && proc.getLatencySamples() == 68 && proc.getOversamplingFactor() == (1 << choice),
               String (1 << choice) + "x: finite, latency " + String (proc.getLatencySamples()) + " samples, CPU "
                   + String (100.0 * stats.cpuSeconds / stats.seconds, 2) + "%");
    }

    HollowAudioProcessor proc;
    set (proc.getState(), pid::oversampling, 1.0f);       // 2x live
    set (proc.getState(), pid::renderOversampling, 1.0f); // 8x when rendering
    proc.setNonRealtime (true);
    prepare (proc, rate);
    TestSong song (rate);
    render (proc, song, rate, 0.1, 2);
    const int offline = proc.getOversamplingFactor();
    proc.setNonRealtime (false);
    render (proc, song, rate, 0.1, 2);
    check (offline == 8 && proc.getOversamplingFactor() == 2, "render uses 8x, playback 2x (" + String (offline) + "x / "
                                                                   + String (proc.getOversamplingFactor()) + "x)");
}

void testBypassAndMono()
{
    std::cout << "\n[bypass / mono]" << std::endl;
    const double rate = 48000.0;

    {
        HollowAudioProcessor proc;
        proc.loadPreset (presetIndex ("Fuzz Wall"));
        prepare (proc, rate);
        set (proc.getState(), pid::bypass, 1.0f);

        TestSong warm (rate);
        render (proc, warm, rate, 0.5, 2);

        const int latency = proc.getLatencySamples();
        const int n = 8192;
        AudioBuffer<float> in (2, n);
        TestSong song (rate);
        song.render (in);
        AudioBuffer<float> io (in);
        MidiBuffer midi;

        for (int start = 0; start < n; start += 512)
        {
            AudioBuffer<float> block (io.getArrayOfWritePointers(), 2, start, 512);
            proc.processBlock (block, midi);
        }

        float maxErr = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = latency + 1024; i < n; ++i)
                maxErr = jmax (maxErr, std::abs (io.getSample (ch, i) - in.getSample (ch, i - latency)));

        check (maxErr < 1.0e-6f, "bypass = input delayed by reported latency (" + String (latency) + " samples, max err " + String (maxErr) + ")");
    }

    {
        HollowAudioProcessor proc;
        AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (AudioChannelSet::mono());
        mono.outputBuses.add (AudioChannelSet::mono());
        check (proc.setBusesLayout (mono), "mono layout accepted");
        proc.loadPreset (presetIndex ("Ring Bell Drone"));
        proc.prepareToPlay (rate, 512);
        TestSong song (rate);
        const auto stats = render (proc, song, rate, 3.0, 1);
        check (stats.finite && stats.peak < 8.0f, "mono processing finite");
    }
}

void saveSnapshot (Component& c, const File& file)
{
    // 2x, so the images stay crisp on high-resolution screens (README, docs)
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
    file.deleteFile();
    FileOutputStream stream (file);
    check (stream.openedOk() && PNGImageFormat().writeImageToStream (image, stream), "wrote " + file.getFileName());
}

void writeScreenshots (const File& outDir)
{
    std::cout << "\n[screenshots]" << std::endl;
    const double rate = 48000.0;

    struct Shot { const char* preset; int module; const char* file; };
    const Shot shots[] = {
        { "Glitch Machine", hl::dsp::moduleTrash, "0-hero.png" },
        { "Multiband Crunch", hl::dsp::moduleTrash, "1-trash.png" },
        { "Screaming Filter", hl::dsp::moduleFilter1, "2-filter.png" },
        { "Spring Drip", hl::dsp::moduleConvolve, "3-convolve.png" },
        { "Jet Flange Grit", hl::dsp::moduleMotion, "4-motion.png" },
        { "Glitch Machine", hl::dsp::moduleDegrade, "5-degrade.png" },
        { "Fuzz Wall", hl::dsp::moduleDynamics, "6-dynamics.png" },
        { "Runaway Echo", hl::dsp::moduleEcho, "7-echo.png" },
        { "Wavefolder Bass", hl::dsp::moduleTrash, "8-wavefolder.png" },
    };

    for (const auto& shot : shots)
    {
        HollowAudioProcessor proc;
        proc.loadPreset (presetIndex (shot.preset));
        proc.getUiState().setProperty ("module", shot.module, nullptr);
        prepare (proc, rate);
        proc.waitForImpulse (5000);

        std::unique_ptr<AudioProcessorEditor> holder (proc.createEditorAndMakeActive());
        auto* editor = dynamic_cast<hl::HollowAudioProcessorEditor*> (holder.get());

        if (editor == nullptr)
        {
            check (false, "editor created");
            return;
        }

        editor->setSize (1320, 840);
        editor->selectModule (shot.module);
        TestSong song (rate);
        const int framesPerTick = (int) (rate / 30.0);

        // ~4 s of audio with UI frames in between, like a real host would do
        for (int tick = 0; tick < 120; ++tick)
        {
            AudioBuffer<float> buffer (2, framesPerTick);
            song.render (buffer);
            MidiBuffer midi;
            proc.processBlock (buffer, midi);
            editor->tick();
        }

        saveSnapshot (*editor, outDir.getChildFile (shot.file));
    }

    // The menu page and the save prompt
    HollowAudioProcessor proc;
    proc.loadPreset (presetIndex ("Shoegaze Wall"));
    prepare (proc, rate);
    proc.waitForImpulse (5000);
    std::unique_ptr<AudioProcessorEditor> holder (proc.createEditorAndMakeActive());

    if (auto* editor = dynamic_cast<hl::HollowAudioProcessorEditor*> (holder.get()))
    {
        editor->setSize (1320, 840);
        TestSong song (rate);

        for (int tick = 0; tick < 60; ++tick)
        {
            AudioBuffer<float> buffer (2, (int) (rate / 30.0));
            song.render (buffer);
            MidiBuffer midi;
            proc.processBlock (buffer, midi);
            editor->tick();
        }

        editor->showMenu (true);
        editor->tick();
        saveSnapshot (*editor, outDir.getChildFile ("9-menu.png"));
        editor->showMenu (false);
        editor->showSaveDialog();
        saveSnapshot (*editor, outDir.getChildFile ("10-save.png"));
    }
}
} // namespace

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI juce;

    File outDir = File::getCurrentWorkingDirectory().getChildFile ("snapshots");
    bool renders = false, calibrate = false;

    for (int i = 1; i < argc; ++i)
    {
        const auto arg = String (CharPointer_UTF8 (argv[i]));

        if (arg == "--renders")
            renders = true;
        else if (arg == "--calibrate")
            calibrate = true;
        else
            outDir = File (arg);
    }

    outDir.createDirectory();

    testPresets (outDir, renders, calibrate);

    if (calibrate)
        return 0;

    testRates();
    testAutomationStress();
    testRandomize();
    testHotInput();
    testStateRoundTrip();
    testUserPresets (outDir);
    testOversampling();
    testUndo();
    testBypassAndMono();
    writeScreenshots (outDir);

    std::cout << "\n" << (failures == 0 ? "ALL PASSED" : String (failures) + " FAILURE(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
