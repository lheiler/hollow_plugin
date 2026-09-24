#pragma once

#include "dsp/Chain.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace hl::params
{
/** Parameter IDs. Changing any of these breaks saved sessions. */
namespace id
{
    inline const juce::String inputGain { "inGain" };
    inline const juce::String outputGain { "outGain" };
    inline const juce::String mix { "mix" };
    inline const juce::String bypass { "bypass" };
    inline const juce::String autoLevel { "autoLevel" };
    inline const juce::String clipGuard { "clipGuard" };
    inline const juce::String oversampling { "osLive" };        // 1x / 2x / 4x / 8x
    inline const juce::String renderOversampling { "osRender" }; // same as live / 8x

    inline juce::String moduleOn (int module)
    {
        static const char* const ids[] = { "f1On", "trashOn", "f2On", "convOn", "motionOn", "degradeOn", "dynOn", "echoOn" };
        return ids[module];
    }

    // Filters: suffix is one of Type, Cutoff, Reso, Drive, Mix
    inline juce::String filter (int index, const char* suffix) { return "f" + juce::String (index + 1) + suffix; }

    // Trash
    inline const juce::String trashBands { "trBands" };
    inline const juce::String trashAutoGain { "trAuto" };
    inline juce::String trashCrossover (int i) { return "trX" + juce::String (i + 1); }
    // suffix is one of AlgoA, AlgoB, Morph, Drive, Bias, Tone, Mix, Level
    inline juce::String trash (int band, const char* suffix) { return "tr" + juce::String (band + 1) + suffix; }

    // Convolve
    inline const juce::String convImpulse { "cvIr" };
    inline const juce::String convSize { "cvSize" };
    inline const juce::String convDamp { "cvDamp" };
    inline const juce::String convReverse { "cvReverse" };
    inline const juce::String convMix { "cvMix" };

    // Motion
    inline const juce::String motionMode { "moMode" };
    inline const juce::String motionRate { "moRate" };
    inline const juce::String motionSync { "moSync" };
    inline const juce::String motionDivision { "moDiv" };
    inline const juce::String motionDepth { "moDepth" };
    inline const juce::String motionFeedback { "moFeedback" };
    inline const juce::String motionFreq { "moFreq" };
    inline const juce::String motionSpread { "moSpread" };
    inline const juce::String motionMix { "moMix" };

    // Degrade
    inline const juce::String degradeWow { "dgWow" };
    inline const juce::String degradeFlutter { "dgFlutter" };
    inline const juce::String degradeAge { "dgAge" };
    inline const juce::String degradeNoise { "dgNoise" };
    inline const juce::String degradeCrackle { "dgCrackle" };
    inline const juce::String degradeDropout { "dgDropout" };
    inline const juce::String degradeGlitch { "dgGlitch" };
    inline const juce::String degradeMix { "dgMix" };

    // Dynamics
    inline const juce::String dynThreshold { "dyThresh" };
    inline const juce::String dynRatio { "dyRatio" };
    inline const juce::String dynAttack { "dyAttack" };
    inline const juce::String dynRelease { "dyRelease" };
    inline const juce::String dynMakeup { "dyMakeup" };
    inline const juce::String dynGate { "dyGate" };
    inline const juce::String dynMix { "dyMix" };

    // Echo
    inline const juce::String echoTime { "ecTime" };
    inline const juce::String echoSync { "ecSync" };
    inline const juce::String echoDivision { "ecDiv" };
    inline const juce::String echoFeedback { "ecFeedback" };
    inline const juce::String echoTone { "ecTone" };
    inline const juce::String echoDrive { "ecDrive" };
    inline const juce::String echoWobble { "ecWobble" };
    inline const juce::String echoPingPong { "ecPingPong" };
    inline const juce::String echoMix { "ecMix" };

    // Modulation: suffix is one of Shape, Rate, Sync, Div
    inline juce::String lfo (int index, const char* suffix) { return "lfo" + juce::String (index + 1) + suffix; }
    inline const juce::String envAttack { "envAttack" };
    inline const juce::String envRelease { "envRelease" };
    inline const juce::String envGain { "envGain" };
    inline juce::String macro (int index) { return index == 0 ? juce::String ("macro") : "macro" + juce::String (index + 1); }

    // Matrix: suffix is one of Src, Dst, Amt
    inline juce::String slot (int index, const char* suffix) { return "mod" + juce::String (index + 1) + suffix; }
} // namespace id

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** The modulation destination a parameter belongs to (dsp::destNone if it can't be modulated). */
int destinationForParameter (const juce::String& paramId);

/** The parameter that best represents a destination (for the matrix and knob rings). */
juce::String parameterForDestination (int destination);

juce::String formatFrequency (float hz);
juce::String formatDecibels (float db, int decimals = 1);

/** Caches raw parameter pointers so the audio thread can assemble DSP settings cheaply. */
class Binding
{
public:
    explicit Binding (juce::AudioProcessorValueTreeState& state);

    /** Everything except the module order (which lives in the state tree, not in parameters).
        `offline` = the host is rendering (may use a higher oversampling factor). */
    dsp::ChainSettings read (bool offline = false) const noexcept;

    bool isBypassed() const noexcept { return bypass->load() >= 0.5f; }

private:
    using Raw = std::atomic<float>*;

    struct FilterRaw { Raw type, cutoff, reso, drive, mix; };
    struct TrashBandRaw { Raw algoA, algoB, morph, drive, bias, tone, mix, level; };
    struct LfoRaw { Raw shape, rate, sync, division; };
    struct SlotRaw { Raw source, destination, amount; };

    Raw inputGain, outputGain, mix, bypass, autoLevel, clipGuard, oversampling, renderOversampling;
    std::array<Raw, dsp::numModules> moduleOn;
    std::array<FilterRaw, 2> filters;
    Raw trashBands, trashAutoGain;
    std::array<Raw, dsp::kTrashBands - 1> trashCrossovers;
    std::array<TrashBandRaw, dsp::kTrashBands> trashBandsRaw;
    Raw convImpulse, convSize, convDamp, convReverse, convMix;
    Raw motionMode, motionRate, motionSync, motionDivision, motionDepth, motionFeedback, motionFreq, motionSpread, motionMix;
    Raw degradeWow, degradeFlutter, degradeAge, degradeNoise, degradeCrackle, degradeDropout, degradeGlitch, degradeMix;
    Raw dynThreshold, dynRatio, dynAttack, dynRelease, dynMakeup, dynGate, dynMix;
    Raw echoTime, echoSync, echoDivision, echoFeedback, echoTone, echoDrive, echoWobble, echoPingPong, echoMix;
    std::array<LfoRaw, 2> lfos;
    Raw envAttack, envRelease, envGain;
    std::array<Raw, dsp::kNumMacros> macros;
    std::array<SlotRaw, dsp::kModSlots> slots;
};

} // namespace hl::params
