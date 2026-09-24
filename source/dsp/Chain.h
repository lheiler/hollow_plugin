#pragma once

#include "Convolver.h"
#include "Degrade.h"
#include "Dynamics.h"
#include "Echo.h"
#include "Filter.h"
#include "Level.h"
#include "Loudness.h"
#include "Modulation.h"
#include "Motion.h"
#include "Trash.h"

namespace hl::dsp
{
//==============================================================================
/** Modules, in their default chain order. */
enum ModuleId
{
    moduleFilter1,
    moduleTrash,
    moduleFilter2,
    moduleConvolve,
    moduleMotion,
    moduleDegrade,
    moduleDynamics,
    moduleEcho,
    numModules
};

inline const char* moduleName (int id) noexcept
{
    static const char* const names[numModules] = { "Filter 1", "Trash", "Filter 2", "Convolve", "Motion", "Degrade", "Dynamics", "Echo" };
    return names[std::clamp (id, 0, numModules - 1)];
}

using ModuleOrder = std::array<int, numModules>;

inline ModuleOrder defaultModuleOrder() noexcept
{
    ModuleOrder o {};

    for (int i = 0; i < numModules; ++i)
        o[(size_t) i] = i;

    return o;
}

inline bool isValidOrder (const ModuleOrder& order) noexcept
{
    std::array<bool, numModules> seen {};

    for (auto id : order)
    {
        if (id < 0 || id >= numModules || seen[(size_t) id])
            return false;

        seen[(size_t) id] = true;
    }

    return true;
}

//==============================================================================
/** Parameter ranges. The plugin parameters are built from these so that modulation (which works
    in normalised space) moves exactly like the knobs do. Percent parameters use 0..1 here. */
namespace ranges
{
    inline constexpr Range gain { -24.0f, 24.0f };
    inline constexpr Range unit { 0.0f, 1.0f };
    inline constexpr Range bipolar { -1.0f, 1.0f };
    inline constexpr Range cutoff { 20.0f, 20000.0f, true };
    inline constexpr Range filterDrive { 0.0f, 24.0f };
    inline constexpr Range trashDrive { 0.0f, kMaxDriveDb };
    inline constexpr Range trashLevel { -24.0f, 12.0f };
    inline constexpr Range crossover { 40.0f, 12000.0f, true };
    inline constexpr Range motionRate { 0.02f, 20.0f, true };
    inline constexpr Range motionFreq { 20.0f, 5000.0f, true };
    inline constexpr Range convSize { 0.25f, 2.0f };
    inline constexpr Range dynThreshold { -60.0f, 0.0f };
    inline constexpr Range dynRatio { 1.0f, 20.0f, true };
    inline constexpr Range dynAttack { 0.1f, 200.0f, true };
    inline constexpr Range dynRelease { 5.0f, 2000.0f, true };
    inline constexpr Range dynMakeup { 0.0f, 24.0f };
    inline constexpr Range gate { -80.0f, 0.0f };
    inline constexpr Range echoTime { 5.0f, 2000.0f, true };
    inline constexpr Range echoFeedback { 0.0f, 1.2f };
    inline constexpr Range lfoRate { 0.01f, 30.0f, true };
    inline constexpr Range envAttack { 0.1f, 200.0f, true };
    inline constexpr Range envRelease { 5.0f, 2000.0f, true };
    inline constexpr Range envGain { -24.0f, 24.0f };
} // namespace ranges

//==============================================================================
enum ModSource { sourceNone, sourceLfo1, sourceLfo2, sourceEnvelope, sourceMacro1, sourceMacro2, numSources };
constexpr int kNumMacros = 2;

inline const char* sourceName (int s) noexcept
{
    static const char* const names[numSources] = { "Off", "LFO 1", "LFO 2", "Envelope", "Macro 1", "Macro 2" };
    return names[std::clamp (s, 0, numSources - 1)];
}

enum Destination
{
    destNone,
    destF1Cutoff, destF1Reso, destF1Drive, destF1Mix,
    destTrashDrive, destTrashMorph, destTrashBias, destTrashTone, destTrashMix,
    destF2Cutoff, destF2Reso, destF2Drive, destF2Mix,
    destConvMix,
    destMotionRate, destMotionDepth, destMotionFeedback, destMotionFreq, destMotionMix,
    destDegradeWow, destDegradeFlutter, destDegradeAge, destDegradeNoise, destDegradeCrackle, destDegradeDropout, destDegradeGlitch, destDegradeMix,
    destDynThreshold, destDynMix,
    destEchoTime, destEchoFeedback, destEchoTone, destEchoDrive, destEchoWobble, destEchoMix,
    destLfo1Rate, destLfo2Rate,
    destInputGain, destOutputGain, destGlobalMix,
    numDestinations
};

inline const char* destinationName (int d) noexcept
{
    static const char* const names[numDestinations] = {
        "Off",
        "Filter 1 Cutoff", "Filter 1 Reso", "Filter 1 Drive", "Filter 1 Mix",
        "Trash Drive", "Trash Morph", "Trash Bias", "Trash Tone", "Trash Mix",
        "Filter 2 Cutoff", "Filter 2 Reso", "Filter 2 Drive", "Filter 2 Mix",
        "Convolve Mix",
        "Motion Rate", "Motion Depth", "Motion Feedback", "Motion Freq", "Motion Mix",
        "Degrade Wow", "Degrade Flutter", "Degrade Age", "Degrade Noise", "Degrade Crackle", "Degrade Dropout", "Degrade Glitch", "Degrade Mix",
        "Dynamics Threshold", "Dynamics Mix",
        "Echo Time", "Echo Feedback", "Echo Tone", "Echo Drive", "Echo Wobble", "Echo Mix",
        "LFO 1 Rate", "LFO 2 Rate",
        "Input Gain", "Output Gain", "Global Mix",
    };
    return names[std::clamp (d, 0, numDestinations - 1)];
}

constexpr int kModSlots = 8;

struct ModSlot
{
    int source = sourceNone;
    int destination = destNone;
    float amount = 0.0f; // -1 .. 1 of the destination's full range
};

//==============================================================================
struct TransportInfo
{
    double bpm = 120.0;
    double ppq = 0.0;       // position at the start of the block, in quarter notes
    bool playing = false;
};

struct ChainSettings
{
    float inputGainDb = 0.0f, outputGainDb = 0.0f, mix = 1.0f;
    bool autoLevel = true;  // compensate the level change the settings are expected to cause
    bool clipGuard = true;  // soft ceiling at -0.3 dBFS
    int oversampling = 4;   // distortion quality: 1, 2, 4 or 8 (latency is the same for all)
    std::array<bool, numModules> enabled {};
    ModuleOrder order = defaultModuleOrder();

    std::array<FilterSettings, 2> filters {};
    TrashSettings trash;
    ConvolveSettings convolve;
    MotionSettings motion;
    bool motionSync = false;
    int motionDivision = 9;
    DegradeSettings degrade;
    DynamicsSettings dynamics;
    EchoSettings echo;
    bool echoSync = false;
    int echoDivision = 7;

    std::array<LfoSettings, 2> lfos {};
    EnvelopeSettings envelope;
    std::array<float, kNumMacros> macros {};
    std::array<ModSlot, kModSlots> slots {};
};

/** Moves destination `d` by `off` (normalised) inside `s`. */
inline void applyModulation (ChainSettings& s, int d, float off) noexcept
{
    namespace r = ranges;
    auto& f1 = s.filters[0];
    auto& f2 = s.filters[1];

    const auto all = [&] (auto member, const Range& range)
    {
        for (auto& b : s.trash.bands)
            b.*member = range.offset (b.*member, off);
    };

    switch (d)
    {
        case destF1Cutoff:       f1.cutoff = r::cutoff.offset (f1.cutoff, off); break;
        case destF1Reso:         f1.reso = r::unit.offset (f1.reso, off); break;
        case destF1Drive:        f1.driveDb = r::filterDrive.offset (f1.driveDb, off); break;
        case destF1Mix:          f1.mix = r::unit.offset (f1.mix, off); break;
        case destTrashDrive:     all (&TrashBandSettings::driveDb, r::trashDrive); break;
        case destTrashMorph:     all (&TrashBandSettings::morph, r::unit); break;
        case destTrashBias:      all (&TrashBandSettings::bias, r::bipolar); break;
        case destTrashTone:      all (&TrashBandSettings::tone, r::unit); break;
        case destTrashMix:       all (&TrashBandSettings::mix, r::unit); break;
        case destF2Cutoff:       f2.cutoff = r::cutoff.offset (f2.cutoff, off); break;
        case destF2Reso:         f2.reso = r::unit.offset (f2.reso, off); break;
        case destF2Drive:        f2.driveDb = r::filterDrive.offset (f2.driveDb, off); break;
        case destF2Mix:          f2.mix = r::unit.offset (f2.mix, off); break;
        case destConvMix:        s.convolve.mix = r::unit.offset (s.convolve.mix, off); break;
        case destMotionRate:     s.motion.rateHz = r::motionRate.offset (s.motion.rateHz, off); break;
        case destMotionDepth:    s.motion.depth = r::unit.offset (s.motion.depth, off); break;
        case destMotionFeedback: s.motion.feedback = r::bipolar.offset (s.motion.feedback, off); break;
        case destMotionFreq:     s.motion.freqHz = r::motionFreq.offset (s.motion.freqHz, off); break;
        case destMotionMix:      s.motion.mix = r::unit.offset (s.motion.mix, off); break;
        case destDegradeWow:     s.degrade.wow = r::unit.offset (s.degrade.wow, off); break;
        case destDegradeFlutter: s.degrade.flutter = r::unit.offset (s.degrade.flutter, off); break;
        case destDegradeAge:     s.degrade.age = r::unit.offset (s.degrade.age, off); break;
        case destDegradeNoise:   s.degrade.noise = r::unit.offset (s.degrade.noise, off); break;
        case destDegradeCrackle: s.degrade.crackle = r::unit.offset (s.degrade.crackle, off); break;
        case destDegradeDropout: s.degrade.dropout = r::unit.offset (s.degrade.dropout, off); break;
        case destDegradeGlitch:  s.degrade.glitch = r::unit.offset (s.degrade.glitch, off); break;
        case destDegradeMix:     s.degrade.mix = r::unit.offset (s.degrade.mix, off); break;
        case destDynThreshold:   s.dynamics.thresholdDb = r::dynThreshold.offset (s.dynamics.thresholdDb, off); break;
        case destDynMix:         s.dynamics.mix = r::unit.offset (s.dynamics.mix, off); break;
        case destEchoTime:       s.echo.timeMs = r::echoTime.offset (s.echo.timeMs, off); break;
        case destEchoFeedback:   s.echo.feedback = r::echoFeedback.offset (s.echo.feedback, off); break;
        case destEchoTone:       s.echo.tone = r::bipolar.offset (s.echo.tone, off); break;
        case destEchoDrive:      s.echo.drive = r::unit.offset (s.echo.drive, off); break;
        case destEchoWobble:     s.echo.wobble = r::unit.offset (s.echo.wobble, off); break;
        case destEchoMix:        s.echo.mix = r::unit.offset (s.echo.mix, off); break;
        case destLfo1Rate:       s.lfos[0].rateHz = r::lfoRate.offset (s.lfos[0].rateHz, off); break;
        case destLfo2Rate:       s.lfos[1].rateHz = r::lfoRate.offset (s.lfos[1].rateHz, off); break;
        case destInputGain:      s.inputGainDb = r::gain.offset (s.inputGainDb, off); break;
        case destOutputGain:     s.outputGainDb = r::gain.offset (s.outputGainDb, off); break;
        case destGlobalMix:      s.mix = r::unit.offset (s.mix, off); break;
        default: break;
    }
}

/** The spectral pass for one fixed set of settings (see estimateLevelChangeDb). */
inline double estimateStaticLevelChangeDb (const ChainSettings& s, double rate, const loudness::ImpulseBands* impulse)
{
    namespace ld = loudness;
    auto spectrum = ld::musicAt (ld::referenceDb + s.inputGainDb);

    for (int id : s.order)
    {
        if (id < 0 || id >= numModules || ! s.enabled[(size_t) id])
            continue;

        switch (id)
        {
            case moduleFilter1:  ld::filterStage (spectrum, s.filters[0], rate); break;
            case moduleTrash:    ld::trashStage (spectrum, s.trash); break;
            case moduleFilter2:  ld::filterStage (spectrum, s.filters[1], rate); break;
            case moduleConvolve: ld::convolveStage (spectrum, s.convolve, impulse); break;
            case moduleMotion:   ld::motionStage (spectrum, s.motion); break;
            case moduleDegrade:  ld::degradeStage (spectrum, s.degrade); break;
            case moduleDynamics: ld::dynamicsStage (spectrum, s.dynamics); break;
            case moduleEcho:     ld::echoStage (spectrum, s.echo, rate); break;
            default: break;
        }
    }

    return ld::toDb (ld::loudnessPower (spectrum)) - ld::referenceDb;
}

/** How much louder (dB) the chain makes typical music, estimated stage by stage from the settings
    alone (see Loudness.h). Includes the input gain. `impulse` is the convolver's band response.

    Modulation that is always there counts too: the envelope sits at its typical value for music at the
    reference level, the macro at its knob, and LFO sweeps are averaged over their centre and extremes. */
inline double estimateLevelChangeDb (const ChainSettings& s, double rate, const loudness::ImpulseBands* impulse)
{
    const auto lfoDepth = [] (const LfoSettings& l)
    {
        switch ((LfoShape) std::clamp (l.shape, 0, kNumLfoShapes - 1))
        {
            case LfoShape::sine:         return 0.71f;
            case LfoShape::square:       return 1.0f;
            case LfoShape::smoothRandom: return 0.5f;
            default:                     return 0.58f; // triangle, ramps, sample & hold: uniform-ish
        }
    };

    const float envelope = std::clamp ((float) (loudness::referenceDb + s.inputGainDb + 8.0 + s.envelope.gainDb + 48.0) / 48.0f, 0.0f, 1.0f);
    bool usesLfo = false;

    const auto modulatedAt = [&] (float lfoSign)
    {
        std::array<float, numDestinations> offsets {};

        for (const auto& slot : s.slots)
        {
            if (slot.source <= sourceNone || slot.source >= numSources || slot.destination <= destNone || slot.destination >= numDestinations)
                continue;

            float value = 0.0f;

            switch (slot.source)
            {
                case sourceLfo1:     value = lfoSign * lfoDepth (s.lfos[0]); usesLfo = true; break;
                case sourceLfo2:     value = lfoSign * lfoDepth (s.lfos[1]); usesLfo = true; break;
                case sourceEnvelope: value = envelope; break;
                case sourceMacro1:   value = std::clamp (s.macros[0], 0.0f, 1.0f); break;
                case sourceMacro2:   value = std::clamp (s.macros[1], 0.0f, 1.0f); break;
                default: break;
            }

            offsets[(size_t) slot.destination] += slot.amount * value;
        }

        auto m = s;

        for (int d = 1; d < numDestinations; ++d)
            if (offsets[(size_t) d] != 0.0f)
                applyModulation (m, d, offsets[(size_t) d]);

        return estimateStaticLevelChangeDb (m, rate, impulse);
    };

    const double centre = modulatedAt (0.0f);

    if (! usesLfo)
        return centre;

    const double power = 0.5 * loudness::toPower (centre) + 0.25 * loudness::toPower (modulatedAt (1.0f)) + 0.25 * loudness::toPower (modulatedAt (-1.0f));
    return loudness::toDb (power);
}

/** What the modulation system is doing right now (for the UI). */
struct ModulationSnapshot
{
    std::array<float, numDestinations> offsets {};
    std::array<float, 2> lfoValues {};
    std::array<double, 2> lfoPhases {};
    float envelope = 0.0f;
    float motionPhase = 0.0f;
};

//==============================================================================
/** The whole effect: eight reorderable modules, modulation, gain staging and global mix.
    Latency is constant (the Trash oversampler) whether or not modules are enabled. */
class Chain
{
public:
    static constexpr int controlBlock = 32;

    void prepare (double rate, int maxBlockSize)
    {
        sampleRate = rate;
        const int maxBlock = std::max (controlBlock, maxBlockSize);
        modulated = base;
        pushSettings (base, 1);

        for (auto& f : filters)
            f.prepare (rate, maxBlock);

        trash.prepare (rate, maxBlock);
        convolve.prepare (rate, maxBlock);
        motion.prepare (rate, maxBlock);
        degrade.prepare (rate, maxBlock);
        dynamics.prepare (rate, maxBlock);
        echo.prepare (rate, maxBlock);
        envelope.prepare (rate);
        loudness::ShaperTable::get(); // build the tables here, never on the audio thread
        loudness::Grid::get();
        AutoGainTable::get();
        levelTargetDb = levelDb = estimateTarget();

        latency = trash.getLatency();

        for (int ch = 0; ch < 2; ++ch)
        {
            dryDelay[(size_t) ch].prepare (latency);
            dryDelay[(size_t) ch].setDelay (latency);
            trashDelay[(size_t) ch].prepare (latency);
            trashDelay[(size_t) ch].setDelay (latency);
        }

        for (int id = 0; id < numModules; ++id)
        {
            auto& slot = slots[(size_t) id];
            slot.on.reset (rate, 0.02);
            slot.on.setCurrentAndTarget (base.enabled[(size_t) id] ? 1.0f : 0.0f);
            slot.wasActive = base.enabled[(size_t) id];
        }

        inputGain.snap (dbToGain (base.inputGainDb));
        outputGain.snap (dbToGain (base.outputGainDb + levelDb));
        mixRamp.snap (std::clamp (base.mix, 0.0f, 1.0f));

        scratch.assign ((size_t) (4 * controlBlock), 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& f : filters)
            f.reset();

        trash.reset();
        convolve.reset();
        motion.reset();
        degrade.reset();
        dynamics.reset();
        echo.reset();
        envelope.reset();

        for (auto& l : lfos)
            l.reset();

        for (auto& d : dryDelay)
            d.reset();

        for (auto& d : trashDelay)
            d.reset();
    }

    /** Base (unmodulated) settings; call once per audio block before process(). */
    void setSettings (const ChainSettings& s) noexcept
    {
        base = s;

        if (! isValidOrder (base.order))
            base.order = defaultModuleOrder();
    }

    const ChainSettings& getSettings() const noexcept { return base; }

    int getLatency() const noexcept { return latency; }

    void process (float* left, float* right, int n, const TransportInfo& transport) noexcept
    {
        const double beatsPerSample = transport.bpm / 60.0 / sampleRate;

        // a newly built impulse changes the estimate (it arrives a moment after the knob moved)
        if (convolve.takeResponseChanged())
            levelTargetDb = estimateTarget();

        for (int start = 0; start < n; start += controlBlock)
        {
            const int m = std::min (controlBlock, n - start);
            TransportInfo t = transport;
            t.ppq = transport.ppq + start * beatsPerSample;
            processControlBlock (left + start, right + start, m, t);
        }
    }

    // Access for the UI, the worker thread and tests
    TrashModule& getTrash() noexcept { return trash; }
    const TrashModule& getTrash() const noexcept { return trash; }
    ConvolveModule& getConvolve() noexcept { return convolve; }
    const ConvolveModule& getConvolve() const noexcept { return convolve; }
    DegradeModule& getDegrade() noexcept { return degrade; }
    DynamicsModule& getDynamics() noexcept { return dynamics; }
    MotionModule& getMotion() noexcept { return motion; }
    const ModulationSnapshot& getModulation() const noexcept { return snapshot; }

    /** Settings as modulated in the most recent control block. */
    const ChainSettings& getModulatedSettings() const noexcept { return modulated; }

    /** Current Auto Level compensation in dB. */
    float getAutoLevelDb() const noexcept { return levelDb; }

    /** Re-estimates the level compensation from the current settings. Call when a setting changed
        (not per block: it is a guess from the knobs, it never listens to the audio). */
    void updateLevelEstimate() noexcept { levelTargetDb = estimateTarget(); }

    /** Estimated level change of the current settings, in dB (Auto Level compensates the opposite). */
    double estimateLevelChange() const
    {
        const auto impulse = convolve.getResponseBands();
        return estimateLevelChangeDb (base, sampleRate, &impulse);
    }

    /** Target compensation for the current settings (what getAutoLevelDb() glides towards). */
    float estimateTarget() const noexcept
    {
        return base.autoLevel ? (float) std::clamp (-estimateLevelChange(), -30.0, 30.0) : 0.0f;
    }

private:
    struct Slot
    {
        LinearSmoother on;
        bool wasActive = false;
    };

    void processControlBlock (float* left, float* right, int n, const TransportInfo& t) noexcept
    {
        // 1. Modulation --------------------------------------------------------------------
        modulated = base;
        std::array<float, numSources> sourceValues {};
        const auto lfoRate = [&] (const LfoSettings& l) { return l.sync ? t.bpm / 60.0 / divisionBeats (l.division) : (double) l.rateHz; };

        for (int i = 0; i < 2; ++i)
        {
            const auto& ls = base.lfos[(size_t) i];

            if (ls.sync && t.playing)
                lfos[(size_t) i].setPhase (t.ppq / divisionBeats (ls.division));

            sourceValues[(size_t) (sourceLfo1 + i)] = lfos[(size_t) i].value ((LfoShape) std::clamp (ls.shape, 0, kNumLfoShapes - 1));
        }

        sourceValues[sourceEnvelope] = envelope.getValue();
        sourceValues[sourceMacro1] = std::clamp (base.macros[0], 0.0f, 1.0f);
        sourceValues[sourceMacro2] = std::clamp (base.macros[1], 0.0f, 1.0f);

        snapshot.offsets.fill (0.0f);

        for (const auto& slot : base.slots)
        {
            if (slot.source <= sourceNone || slot.source >= numSources || slot.destination <= destNone
                || slot.destination >= numDestinations || slot.amount == 0.0f)
                continue;

            snapshot.offsets[(size_t) slot.destination] += slot.amount * sourceValues[(size_t) slot.source];
        }

        for (int d = 1; d < numDestinations; ++d)
            if (snapshot.offsets[(size_t) d] != 0.0f)
                applyModulation (modulated, d, snapshot.offsets[(size_t) d]);

        for (int i = 0; i < 2; ++i)
        {
            const auto& ls = modulated.lfos[(size_t) i];

            if (! (ls.sync && t.playing))
                lfos[(size_t) i].advance (n, lfoRate (ls), sampleRate);

            snapshot.lfoValues[(size_t) i] = sourceValues[(size_t) (sourceLfo1 + i)];
            snapshot.lfoPhases[(size_t) i] = lfos[(size_t) i].getPhase();
        }

        snapshot.envelope = sourceValues[sourceEnvelope];

        // Tempo sync
        if (modulated.motionSync)
        {
            const double beats = divisionBeats (modulated.motionDivision);
            modulated.motion.rateHz = (float) (t.bpm / 60.0 / beats);

            if (t.playing)
                motion.setPhase (t.ppq / beats);
        }

        if (modulated.echoSync)
            modulated.echo.timeMs = (float) std::min (2000.0, 60000.0 / t.bpm * divisionBeats (modulated.echoDivision));

        snapshot.motionPhase = (float) motion.getPhase();

        // 2. Push settings into the modules -------------------------------------------------
        pushSettings (modulated, n);
        degrade.setTempo (t.bpm);

        // 3. Audio ------------------------------------------------------------------------------
        float* dryL = scratch.data();
        float* dryR = dryL + controlBlock;
        float* modDryL = dryR + controlBlock;
        float* modDryR = modDryL + controlBlock;

        inputGain.setTarget (dbToGain (modulated.inputGainDb), n);

        for (int i = 0; i < n; ++i)
        {
            dryL[i] = dryDelay[0].process (left[i]);
            dryR[i] = dryDelay[1].process (right[i]);
            const float g = inputGain.next();
            left[i] *= g;
            right[i] *= g;
        }

        envelope.process (left, right, n, modulated.envelope);

        for (int id : base.order)
        {
            auto& slot = slots[(size_t) id];
            slot.on.setTarget (modulated.enabled[(size_t) id] ? 1.0f : 0.0f);
            const bool active = slot.on.isSmoothing() || slot.on.getCurrent() > 0.0f;
            const bool delayed = id == moduleTrash;

            if (! active)
            {
                slot.wasActive = false;

                if (delayed)
                    for (int i = 0; i < n; ++i)
                    {
                        left[i] = trashDelay[0].process (left[i]);
                        right[i] = trashDelay[1].process (right[i]);
                    }

                continue;
            }

            if (! slot.wasActive)
                resetModule (id);

            slot.wasActive = true;

            for (int i = 0; i < n; ++i)
            {
                modDryL[i] = delayed ? trashDelay[0].process (left[i]) : left[i];
                modDryR[i] = delayed ? trashDelay[1].process (right[i]) : right[i];
            }

            runModule (id, left, right, n);

            if (slot.on.isSmoothing() || slot.on.getCurrent() < 1.0f)
            {
                for (int i = 0; i < n; ++i)
                {
                    const float g = slot.on.next();
                    left[i] = modDryL[i] + g * (left[i] - modDryL[i]);
                    right[i] = modDryR[i] + g * (right[i] - modDryR[i]);
                }
            }
        }

        levelDb += (levelTargetDb - levelDb) * (1.0f - std::exp (-(float) n / (0.04f * (float) sampleRate)));
        outputGain.setTarget (dbToGain (modulated.outputGainDb + levelDb), n);
        mixRamp.setTarget (std::clamp (modulated.mix, 0.0f, 1.0f), n);

        for (int i = 0; i < n; ++i)
        {
            const float g = outputGain.next(), w = mixRamp.next();
            left[i] = dryL[i] + w * (left[i] * g - dryL[i]);
            right[i] = dryR[i] + w * (right[i] * g - dryR[i]);
        }

        if (modulated.clipGuard)
        {
            for (int i = 0; i < n; ++i)
            {
                left[i] = ClipGuard::process (left[i]);
                right[i] = ClipGuard::process (right[i]);
            }
        }
    }

    void pushSettings (const ChainSettings& s, int blockLength) noexcept
    {
        filters[0].setSettings (s.filters[0]);
        filters[1].setSettings (s.filters[1]);
        trash.setOversampling (s.oversampling);
        trash.setSettings (s.trash, blockLength);
        convolve.setSettings (s.convolve);
        motion.setSettings (s.motion);
        degrade.setSettings (s.degrade);
        dynamics.setSettings (s.dynamics);
        echo.setSettings (s.echo);
    }

    void runModule (int id, float* left, float* right, int n) noexcept
    {
        switch (id)
        {
            case moduleFilter1:  filters[0].process (left, right, n); break;
            case moduleTrash:    trash.process (left, right, n); break;
            case moduleFilter2:  filters[1].process (left, right, n); break;
            case moduleConvolve: convolve.process (left, right, n); break;
            case moduleMotion:   motion.process (left, right, n); break;
            case moduleDegrade:  degrade.process (left, right, n); break;
            case moduleDynamics: dynamics.process (left, right, n); break;
            case moduleEcho:     echo.process (left, right, n); break;
            default: break;
        }
    }

    void resetModule (int id) noexcept
    {
        switch (id)
        {
            case moduleFilter1:  filters[0].reset(); break;
            case moduleTrash:    trash.reset(); break;
            case moduleFilter2:  filters[1].reset(); break;
            case moduleConvolve: convolve.reset(); break;
            case moduleMotion:   motion.reset(); break;
            case moduleDegrade:  degrade.reset(); break;
            case moduleDynamics: dynamics.reset(); break;
            case moduleEcho:     echo.reset(); break;
            default: break;
        }
    }

    double sampleRate = 44100.0;
    int latency = 0;
    ChainSettings base, modulated;
    ModulationSnapshot snapshot;

    std::array<FilterModule, 2> filters;
    TrashModule trash;
    ConvolveModule convolve;
    MotionModule motion;
    DegradeModule degrade;
    DynamicsModule dynamics;
    EchoModule echo;

    std::array<Lfo, 2> lfos { Lfo (0xA11CEu), Lfo (0xB0B5u) };
    EnvelopeFollower envelope;
    float levelDb = 0.0f, levelTargetDb = 0.0f;
    std::array<Slot, numModules> slots {};
    std::array<DelayLine, 2> dryDelay, trashDelay;
    BlockRamp inputGain, outputGain, mixRamp;
    std::vector<float> scratch;
};

} // namespace hl::dsp
