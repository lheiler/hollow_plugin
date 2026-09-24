#include "Parameters.h"

namespace hl::params
{
using namespace juce;

namespace
{
    constexpr int version = 1;

    /** JUCE range matching a DSP range (scaled, e.g. 0..1 -> 0..100 %). */
    NormalisableRange<float> toJuce (const dsp::Range& r, float scale = 1.0f)
    {
        if (! r.logarithmic)
            return { r.min * scale, r.max * scale };

        return { r.min * scale, r.max * scale,
                 [] (float start, float end, float v) { return start * std::pow (end / start, v); },
                 [] (float start, float end, float x) { return std::log (x / start) / std::log (end / start); },
                 nullptr };
    }

    float parseNumber (const String& text)
    {
        const auto t = text.trim().toLowerCase();
        const float value = t.getFloatValue();
        return t.containsChar ('k') ? value * 1000.0f : value;
    }

    using Attributes = AudioParameterFloatAttributes;

    Attributes withText (std::function<String (float)> toText)
    {
        return Attributes().withStringFromValueFunction ([f = std::move (toText)] (float v, int) { return f (v); })
                           .withValueFromStringFunction (parseNumber);
    }

    Attributes freqAttributes() { return withText ([] (float v) { return formatFrequency (v); }); }
    Attributes dbAttributes() { return withText ([] (float v) { return formatDecibels (v); }).withLabel ("dB"); }
    Attributes percentAttributes() { return withText ([] (float v) { return String (roundToInt (v)) + " %"; }); }

    Attributes msAttributes()
    {
        return withText ([] (float v) { return v < 10.0f ? String (v, 1) + " ms" : (v < 1000.0f ? String (roundToInt (v)) + " ms" : String (v / 1000.0f, 2) + " s"); });
    }

    Attributes hzAttributes()
    {
        return withText ([] (float v) { return v < 1.0f ? String (v, 2) + " Hz" : String (v, 1) + " Hz"; });
    }

    Attributes bipolarAttributes (const char* negative, const char* positive)
    {
        return withText ([negative, positive] (float v)
        {
            if (std::abs (v) < 0.5f)
                return String ("Neutral");

            return String (v < 0.0f ? negative : positive) + " " + String (roundToInt (std::abs (v))) + "%";
        });
    }

    std::unique_ptr<AudioParameterFloat> floatParam (const String& pid, const String& name, NormalisableRange<float> range, float def, Attributes attr)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { pid, version }, name, range, def, attr);
    }

    std::unique_ptr<AudioParameterBool> boolParam (const String& pid, const String& name, bool def)
    {
        return std::make_unique<AudioParameterBool> (ParameterID { pid, version }, name, def);
    }

    std::unique_ptr<AudioParameterChoice> choiceParam (const String& pid, const String& name, const StringArray& choices, int def)
    {
        return std::make_unique<AudioParameterChoice> (ParameterID { pid, version }, name, choices, def);
    }

    StringArray namesOf (const char* const* names, int count)
    {
        StringArray a;

        for (int i = 0; i < count; ++i)
            a.add (names[i]);

        return a;
    }

    StringArray algoNames()
    {
        StringArray a;

        for (int i = 0; i < dsp::kNumAlgos; ++i)
            a.add (dsp::algoInfo (i).name);

        return a;
    }

    StringArray impulseNames()
    {
        StringArray a;

        for (int i = 0; i < dsp::kNumImpulses; ++i)
            a.add (dsp::impulseInfo (i).name);

        return a;
    }

    StringArray divisions() { return namesOf (dsp::divisionNames(), dsp::kNumDivisions); }

    const char* const bandNames[] = { "Low", "Mid", "High" };

    struct DestinationParam
    {
        int destination;
        const char* id;
    };

    /** One representative parameter per destination (trash destinations map to band 1). */
    const std::vector<DestinationParam>& destinationTable()
    {
        static const std::vector<DestinationParam> table = {
            { dsp::destF1Cutoff, "f1Cutoff" }, { dsp::destF1Reso, "f1Reso" }, { dsp::destF1Drive, "f1Drive" }, { dsp::destF1Mix, "f1Mix" },
            { dsp::destTrashDrive, "tr1Drive" }, { dsp::destTrashMorph, "tr1Morph" }, { dsp::destTrashBias, "tr1Bias" },
            { dsp::destTrashTone, "tr1Tone" }, { dsp::destTrashMix, "tr1Mix" },
            { dsp::destF2Cutoff, "f2Cutoff" }, { dsp::destF2Reso, "f2Reso" }, { dsp::destF2Drive, "f2Drive" }, { dsp::destF2Mix, "f2Mix" },
            { dsp::destConvMix, "cvMix" },
            { dsp::destMotionRate, "moRate" }, { dsp::destMotionDepth, "moDepth" }, { dsp::destMotionFeedback, "moFeedback" },
            { dsp::destMotionFreq, "moFreq" }, { dsp::destMotionMix, "moMix" },
            { dsp::destDegradeWow, "dgWow" }, { dsp::destDegradeFlutter, "dgFlutter" }, { dsp::destDegradeAge, "dgAge" },
            { dsp::destDegradeNoise, "dgNoise" }, { dsp::destDegradeCrackle, "dgCrackle" }, { dsp::destDegradeDropout, "dgDropout" },
            { dsp::destDegradeGlitch, "dgGlitch" }, { dsp::destDegradeMix, "dgMix" },
            { dsp::destDynThreshold, "dyThresh" }, { dsp::destDynMix, "dyMix" },
            { dsp::destEchoTime, "ecTime" }, { dsp::destEchoFeedback, "ecFeedback" }, { dsp::destEchoTone, "ecTone" },
            { dsp::destEchoDrive, "ecDrive" }, { dsp::destEchoWobble, "ecWobble" }, { dsp::destEchoMix, "ecMix" },
            { dsp::destLfo1Rate, "lfo1Rate" }, { dsp::destLfo2Rate, "lfo2Rate" },
            { dsp::destInputGain, "inGain" }, { dsp::destOutputGain, "outGain" }, { dsp::destGlobalMix, "mix" },
        };
        return table;
    }
} // namespace

String formatFrequency (float hz)
{
    if (hz >= 10000.0f)
        return String (hz / 1000.0f, 1) + " kHz";

    if (hz >= 1000.0f)
        return String (hz / 1000.0f, 2) + " kHz";

    return String (roundToInt (hz)) + " Hz";
}

String formatDecibels (float db, int decimals)
{
    if (std::abs (db) < 0.5f * std::pow (10.0f, (float) -decimals))
        db = 0.0f;

    return (db > 0.0f ? "+" : "") + String (db, decimals) + " dB";
}

int destinationForParameter (const String& paramId)
{
    // All three trash bands share the "Trash ..." destinations
    if (paramId.startsWith ("tr") && paramId.length() > 3 && CharacterFunctions::isDigit (paramId[2]))
    {
        const auto suffix = paramId.substring (3);

        for (const auto& d : destinationTable())
            if (String (d.id).startsWith ("tr1") && String (d.id).substring (3) == suffix)
                return d.destination;

        return dsp::destNone;
    }

    for (const auto& d : destinationTable())
        if (paramId == d.id)
            return d.destination;

    return dsp::destNone;
}

String parameterForDestination (int destination)
{
    for (const auto& d : destinationTable())
        if (d.destination == destination)
            return d.id;

    return {};
}

AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    namespace r = dsp::ranges;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Global -----------------------------------------------------------------------------------
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("global", "Global", "|");
        group->addChild (floatParam (id::inputGain, "Input Gain", toJuce (r::gain), 0.0f, dbAttributes()),
                         floatParam (id::outputGain, "Output Gain", toJuce (r::gain), 0.0f, dbAttributes()),
                         floatParam (id::mix, "Mix", toJuce (r::unit, 100.0f), 100.0f, percentAttributes()),
                         boolParam (id::bypass, "Bypass", false),
                         boolParam (id::autoLevel, "Auto Level", true),
                         boolParam (id::clipGuard, "Clip Guard", true));

        // Quality settings: saved with the session, not automatable
        const auto setting = AudioParameterChoiceAttributes().withAutomatable (false);
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { id::oversampling, version }, "Oversampling",
                                                                 StringArray { "1x", "2x", "4x", "8x" }, 2, setting),
                         std::make_unique<AudioParameterChoice> (ParameterID { id::renderOversampling, version }, "Render Oversampling",
                                                                 StringArray { "Same as live", "8x" }, 0, setting));

        for (int m = 0; m < dsp::numModules; ++m)
            group->addChild (boolParam (id::moduleOn (m), String (dsp::moduleName (m)) + " On", m == dsp::moduleTrash));

        layout.add (std::move (group));
    }

    // Filters ----------------------------------------------------------------------------------
    for (int f = 0; f < 2; ++f)
    {
        const auto name = "Filter " + String (f + 1) + " ";
        auto group = std::make_unique<AudioProcessorParameterGroup> ("filter" + String (f + 1), "Filter " + String (f + 1), "|");
        group->addChild (choiceParam (id::filter (f, "Type"), name + "Type", namesOf (dsp::filterTypeNames(), dsp::kNumFilterTypes),
                                      f == 0 ? (int) dsp::FilterType::highPass12 : (int) dsp::FilterType::lowPass24),
                         floatParam (id::filter (f, "Cutoff"), name + "Cutoff", toJuce (r::cutoff), f == 0 ? 80.0f : 6000.0f, freqAttributes()),
                         floatParam (id::filter (f, "Reso"), name + "Resonance", toJuce (r::unit, 100.0f), 20.0f, percentAttributes()),
                         floatParam (id::filter (f, "Drive"), name + "Drive", toJuce (r::filterDrive), 0.0f, dbAttributes()),
                         floatParam (id::filter (f, "Mix"), name + "Mix", toJuce (r::unit, 100.0f), 100.0f, percentAttributes()));
        layout.add (std::move (group));
    }

    // Trash ------------------------------------------------------------------------------------
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("trash", "Trash", "|");
        group->addChild (choiceParam (id::trashBands, "Trash Bands", { "1 Band", "2 Bands", "3 Bands" }, 0),
                         boolParam (id::trashAutoGain, "Trash Auto Gain", true),
                         floatParam (id::trashCrossover (0), "Trash Crossover 1", toJuce (r::crossover), 250.0f, freqAttributes()),
                         floatParam (id::trashCrossover (1), "Trash Crossover 2", toJuce (r::crossover), 2500.0f, freqAttributes()));

        const auto tone = withText ([] (float v)
        {
            return v >= 99.5f ? String ("Open") : formatFrequency ((float) dsp::trashToneCutoff (v * 0.01f));
        });

        const auto morph = withText ([] (float v)
        {
            if (v < 0.5f)
                return String ("A");

            if (v > 99.5f)
                return String ("B");

            return String (roundToInt (100.0f - v)) + " : " + String (roundToInt (v));
        });

        const int defaultsA[] = { (int) dsp::Algo::tube, (int) dsp::Algo::fuzz, (int) dsp::Algo::bitcrush };
        const int defaultsB[] = { (int) dsp::Algo::fuzz, (int) dsp::Algo::sineFold, (int) dsp::Algo::decimate };

        for (int b = 0; b < dsp::kTrashBands; ++b)
        {
            const auto name = String ("Trash ") + bandNames[b] + " ";
            group->addChild (choiceParam (id::trash (b, "AlgoA"), name + "Algorithm A", algoNames(), defaultsA[b]),
                             choiceParam (id::trash (b, "AlgoB"), name + "Algorithm B", algoNames(), defaultsB[b]),
                             floatParam (id::trash (b, "Morph"), name + "Morph", toJuce (r::unit, 100.0f), 0.0f, morph),
                             floatParam (id::trash (b, "Drive"), name + "Drive", toJuce (r::trashDrive), 12.0f, dbAttributes()),
                             floatParam (id::trash (b, "Bias"), name + "Bias", toJuce (r::bipolar, 100.0f), 0.0f, bipolarAttributes ("-", "+")),
                             floatParam (id::trash (b, "Tone"), name + "Tone", toJuce (r::unit, 100.0f), 100.0f, tone),
                             floatParam (id::trash (b, "Mix"), name + "Mix", toJuce (r::unit, 100.0f), 100.0f, percentAttributes()),
                             floatParam (id::trash (b, "Level"), name + "Level", toJuce (r::trashLevel), 0.0f, dbAttributes()));
        }

        layout.add (std::move (group));
    }

    // Convolve ---------------------------------------------------------------------------------
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("convolve", "Convolve", "|");
        group->addChild (choiceParam (id::convImpulse, "Convolve Impulse", impulseNames(), (int) dsp::IrType::cab1x12),
                         floatParam (id::convSize, "Convolve Size", toJuce (r::convSize, 100.0f), 100.0f, percentAttributes()),
                         floatParam (id::convDamp, "Convolve Damp", toJuce (r::unit, 100.0f), 30.0f, percentAttributes()),
                         boolParam (id::convReverse, "Convolve Reverse", false),
                         floatParam (id::convMix, "Convolve Mix", toJuce (r::unit, 100.0f), 60.0f, percentAttributes()));
        layout.add (std::move (group));
    }

    // Motion -----------------------------------------------------------------------------------
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("motion", "Motion", "|");
        group->addChild (choiceParam (id::motionMode, "Motion Mode", namesOf (dsp::motionModeNames(), dsp::kNumMotionModes), 0),
                         floatParam (id::motionRate, "Motion Rate", toJuce (r::motionRate), 0.6f, hzAttributes()),
                         boolParam (id::motionSync, "Motion Sync", false),
                         choiceParam (id::motionDivision, "Motion Division", divisions(), 11),
                         floatParam (id::motionDepth, "Motion Depth", toJuce (r::unit, 100.0f), 50.0f, percentAttributes()),
                         floatParam (id::motionFeedback, "Motion Feedback", toJuce (r::bipolar, 100.0f), 0.0f, bipolarAttributes ("-", "+")),
                         floatParam (id::motionFreq, "Motion Freq", toJuce (r::motionFreq), 440.0f, freqAttributes()),
                         floatParam (id::motionSpread, "Motion Spread", toJuce (r::unit, 100.0f), 50.0f, percentAttributes()),
                         floatParam (id::motionMix, "Motion Mix", toJuce (r::unit, 100.0f), 50.0f, percentAttributes()));
        layout.add (std::move (group));
    }

    // Degrade ----------------------------------------------------------------------------------
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("degrade", "Degrade", "|");
        group->addChild (floatParam (id::degradeWow, "Degrade Wow", toJuce (r::unit, 100.0f), 20.0f, percentAttributes()),
                         floatParam (id::degradeFlutter, "Degrade Flutter", toJuce (r::unit, 100.0f), 15.0f, percentAttributes()),
                         floatParam (id::degradeAge, "Degrade Age", toJuce (r::unit, 100.0f), 30.0f, percentAttributes()),
                         floatParam (id::degradeNoise, "Degrade Noise", toJuce (r::unit, 100.0f), 10.0f, percentAttributes()),
                         floatParam (id::degradeCrackle, "Degrade Crackle", toJuce (r::unit, 100.0f), 0.0f, percentAttributes()),
                         floatParam (id::degradeDropout, "Degrade Dropout", toJuce (r::unit, 100.0f), 0.0f, percentAttributes()),
                         floatParam (id::degradeGlitch, "Degrade Glitch", toJuce (r::unit, 100.0f), 0.0f, percentAttributes()),
                         floatParam (id::degradeMix, "Degrade Mix", toJuce (r::unit, 100.0f), 100.0f, percentAttributes()));
        layout.add (std::move (group));
    }

    // Dynamics ---------------------------------------------------------------------------------
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("dynamics", "Dynamics", "|");
        const auto gate = withText ([] (float v) { return v <= dsp::DynamicsModule::gateOffDb ? String ("Off") : formatDecibels (v); });
        group->addChild (floatParam (id::dynThreshold, "Dynamics Threshold", toJuce (r::dynThreshold), -18.0f, dbAttributes()),
                         floatParam (id::dynRatio, "Dynamics Ratio", toJuce (r::dynRatio), 4.0f, withText ([] (float v) { return String (v, 1) + ":1"; })),
                         floatParam (id::dynAttack, "Dynamics Attack", toJuce (r::dynAttack), 5.0f, msAttributes()),
                         floatParam (id::dynRelease, "Dynamics Release", toJuce (r::dynRelease), 120.0f, msAttributes()),
                         floatParam (id::dynMakeup, "Dynamics Makeup", toJuce (r::dynMakeup), 0.0f, dbAttributes()),
                         floatParam (id::dynGate, "Dynamics Gate", toJuce (r::gate), -80.0f, gate),
                         floatParam (id::dynMix, "Dynamics Mix", toJuce (r::unit, 100.0f), 100.0f, percentAttributes()));
        layout.add (std::move (group));
    }

    // Echo -------------------------------------------------------------------------------------
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("echo", "Echo", "|");
        group->addChild (floatParam (id::echoTime, "Echo Time", toJuce (r::echoTime), 350.0f, msAttributes()),
                         boolParam (id::echoSync, "Echo Sync", false),
                         choiceParam (id::echoDivision, "Echo Division", divisions(), 7),
                         floatParam (id::echoFeedback, "Echo Feedback", toJuce (r::echoFeedback, 100.0f), 45.0f, percentAttributes()),
                         floatParam (id::echoTone, "Echo Tone", toJuce (r::bipolar, 100.0f), -20.0f, bipolarAttributes ("Dark", "Thin")),
                         floatParam (id::echoDrive, "Echo Drive", toJuce (r::unit, 100.0f), 20.0f, percentAttributes()),
                         floatParam (id::echoWobble, "Echo Wobble", toJuce (r::unit, 100.0f), 20.0f, percentAttributes()),
                         boolParam (id::echoPingPong, "Echo Ping-Pong", false),
                         floatParam (id::echoMix, "Echo Mix", toJuce (r::unit, 100.0f), 30.0f, percentAttributes()));
        layout.add (std::move (group));
    }

    // Modulation -------------------------------------------------------------------------------
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("modulation", "Modulation", "|");

        for (int l = 0; l < 2; ++l)
        {
            const auto name = "LFO " + String (l + 1) + " ";
            group->addChild (choiceParam (id::lfo (l, "Shape"), name + "Shape", namesOf (dsp::lfoShapeNames(), dsp::kNumLfoShapes), l == 0 ? 0 : 6),
                             floatParam (id::lfo (l, "Rate"), name + "Rate", toJuce (r::lfoRate), l == 0 ? 0.5f : 2.0f, hzAttributes()),
                             boolParam (id::lfo (l, "Sync"), name + "Sync", false),
                             choiceParam (id::lfo (l, "Div"), name + "Division", divisions(), l == 0 ? 13 : 9));
        }

        group->addChild (floatParam (id::envAttack, "Envelope Attack", toJuce (r::envAttack), 5.0f, msAttributes()),
                         floatParam (id::envRelease, "Envelope Release", toJuce (r::envRelease), 150.0f, msAttributes()),
                         floatParam (id::envGain, "Envelope Gain", toJuce (r::envGain), 0.0f, dbAttributes()));

        for (int m = 0; m < dsp::kNumMacros; ++m)
            group->addChild (floatParam (id::macro (m), "Macro " + String (m + 1), toJuce (r::unit, 100.0f), 0.0f, percentAttributes()));

        StringArray sources, destinations;

        for (int s = 0; s < dsp::numSources; ++s)
            sources.add (dsp::sourceName (s));

        for (int d = 0; d < dsp::numDestinations; ++d)
            destinations.add (dsp::destinationName (d));

        for (int s = 0; s < dsp::kModSlots; ++s)
        {
            const auto name = "Mod " + String (s + 1) + " ";
            group->addChild (choiceParam (id::slot (s, "Src"), name + "Source", sources, 0),
                             choiceParam (id::slot (s, "Dst"), name + "Destination", destinations, 0),
                             floatParam (id::slot (s, "Amt"), name + "Amount", toJuce (r::bipolar, 100.0f), 0.0f,
                                         withText ([] (float v) { return (v > 0.0f ? "+" : "") + String (roundToInt (v)) + "%"; })));
        }

        layout.add (std::move (group));
    }

    return layout;
}

//==============================================================================
Binding::Binding (AudioProcessorValueTreeState& state)
{
    const auto get = [&state] (const String& pid)
    {
        auto* p = state.getRawParameterValue (pid);
        jassert (p != nullptr);
        return p;
    };

    inputGain = get (id::inputGain);
    outputGain = get (id::outputGain);
    mix = get (id::mix);
    bypass = get (id::bypass);
    autoLevel = get (id::autoLevel);
    clipGuard = get (id::clipGuard);
    oversampling = get (id::oversampling);
    renderOversampling = get (id::renderOversampling);

    for (int m = 0; m < dsp::numModules; ++m)
        moduleOn[(size_t) m] = get (id::moduleOn (m));

    for (int f = 0; f < 2; ++f)
        filters[(size_t) f] = { get (id::filter (f, "Type")), get (id::filter (f, "Cutoff")), get (id::filter (f, "Reso")),
                                get (id::filter (f, "Drive")), get (id::filter (f, "Mix")) };

    trashBands = get (id::trashBands);
    trashAutoGain = get (id::trashAutoGain);

    for (int i = 0; i < dsp::kTrashBands - 1; ++i)
        trashCrossovers[(size_t) i] = get (id::trashCrossover (i));

    for (int b = 0; b < dsp::kTrashBands; ++b)
        trashBandsRaw[(size_t) b] = { get (id::trash (b, "AlgoA")), get (id::trash (b, "AlgoB")), get (id::trash (b, "Morph")),
                                      get (id::trash (b, "Drive")), get (id::trash (b, "Bias")), get (id::trash (b, "Tone")),
                                      get (id::trash (b, "Mix")), get (id::trash (b, "Level")) };

    convImpulse = get (id::convImpulse);
    convSize = get (id::convSize);
    convDamp = get (id::convDamp);
    convReverse = get (id::convReverse);
    convMix = get (id::convMix);

    motionMode = get (id::motionMode);
    motionRate = get (id::motionRate);
    motionSync = get (id::motionSync);
    motionDivision = get (id::motionDivision);
    motionDepth = get (id::motionDepth);
    motionFeedback = get (id::motionFeedback);
    motionFreq = get (id::motionFreq);
    motionSpread = get (id::motionSpread);
    motionMix = get (id::motionMix);

    degradeWow = get (id::degradeWow);
    degradeFlutter = get (id::degradeFlutter);
    degradeAge = get (id::degradeAge);
    degradeNoise = get (id::degradeNoise);
    degradeCrackle = get (id::degradeCrackle);
    degradeDropout = get (id::degradeDropout);
    degradeGlitch = get (id::degradeGlitch);
    degradeMix = get (id::degradeMix);

    dynThreshold = get (id::dynThreshold);
    dynRatio = get (id::dynRatio);
    dynAttack = get (id::dynAttack);
    dynRelease = get (id::dynRelease);
    dynMakeup = get (id::dynMakeup);
    dynGate = get (id::dynGate);
    dynMix = get (id::dynMix);

    echoTime = get (id::echoTime);
    echoSync = get (id::echoSync);
    echoDivision = get (id::echoDivision);
    echoFeedback = get (id::echoFeedback);
    echoTone = get (id::echoTone);
    echoDrive = get (id::echoDrive);
    echoWobble = get (id::echoWobble);
    echoPingPong = get (id::echoPingPong);
    echoMix = get (id::echoMix);

    for (int l = 0; l < 2; ++l)
        lfos[(size_t) l] = { get (id::lfo (l, "Shape")), get (id::lfo (l, "Rate")), get (id::lfo (l, "Sync")), get (id::lfo (l, "Div")) };

    envAttack = get (id::envAttack);
    envRelease = get (id::envRelease);
    envGain = get (id::envGain);
    for (int m = 0; m < dsp::kNumMacros; ++m)
        macros[(size_t) m] = get (id::macro (m));

    for (int s = 0; s < dsp::kModSlots; ++s)
        slots[(size_t) s] = { get (id::slot (s, "Src")), get (id::slot (s, "Dst")), get (id::slot (s, "Amt")) };
}

dsp::ChainSettings Binding::read (bool offline) const noexcept
{
    dsp::ChainSettings s;
    const auto on = [] (Raw r) { return r->load() >= 0.5f; };
    const auto index = [] (Raw r, int count) { return std::clamp ((int) std::lround (r->load()), 0, count - 1); };
    const auto pct = [] (Raw r) { return r->load() * 0.01f; };

    s.inputGainDb = inputGain->load();
    s.outputGainDb = outputGain->load();
    s.mix = pct (mix);
    s.autoLevel = on (autoLevel);
    s.clipGuard = on (clipGuard);
    s.oversampling = 1 << index (oversampling, 4);

    if (offline && index (renderOversampling, 2) == 1)
        s.oversampling = 8;

    for (int m = 0; m < dsp::numModules; ++m)
        s.enabled[(size_t) m] = on (moduleOn[(size_t) m]);

    for (int f = 0; f < 2; ++f)
    {
        const auto& r = filters[(size_t) f];
        auto& o = s.filters[(size_t) f];
        o.type = index (r.type, dsp::kNumFilterTypes);
        o.cutoff = r.cutoff->load();
        o.reso = pct (r.reso);
        o.driveDb = r.drive->load();
        o.mix = pct (r.mix);
    }

    s.trash.numBands = index (trashBands, dsp::kTrashBands) + 1;
    s.trash.autoGain = on (trashAutoGain);

    for (int i = 0; i < dsp::kTrashBands - 1; ++i)
        s.trash.crossovers[(size_t) i] = trashCrossovers[(size_t) i]->load();

    for (int b = 0; b < dsp::kTrashBands; ++b)
    {
        const auto& r = trashBandsRaw[(size_t) b];
        auto& o = s.trash.bands[(size_t) b];
        o.algoA = index (r.algoA, dsp::kNumAlgos);
        o.algoB = index (r.algoB, dsp::kNumAlgos);
        o.morph = pct (r.morph);
        o.driveDb = r.drive->load();
        o.bias = pct (r.bias);
        o.tone = pct (r.tone);
        o.mix = pct (r.mix);
        o.levelDb = r.level->load();
    }

    s.convolve.impulse = index (convImpulse, dsp::kNumImpulses);
    s.convolve.size = pct (convSize);
    s.convolve.damp = pct (convDamp);
    s.convolve.reverse = on (convReverse);
    s.convolve.mix = pct (convMix);

    s.motion.mode = index (motionMode, dsp::kNumMotionModes);
    s.motion.rateHz = motionRate->load();
    s.motion.depth = pct (motionDepth);
    s.motion.feedback = pct (motionFeedback);
    s.motion.freqHz = motionFreq->load();
    s.motion.spread = pct (motionSpread);
    s.motion.mix = pct (motionMix);
    s.motionSync = on (motionSync);
    s.motionDivision = index (motionDivision, dsp::kNumDivisions);

    s.degrade.wow = pct (degradeWow);
    s.degrade.flutter = pct (degradeFlutter);
    s.degrade.age = pct (degradeAge);
    s.degrade.noise = pct (degradeNoise);
    s.degrade.crackle = pct (degradeCrackle);
    s.degrade.dropout = pct (degradeDropout);
    s.degrade.glitch = pct (degradeGlitch);
    s.degrade.mix = pct (degradeMix);

    s.dynamics.thresholdDb = dynThreshold->load();
    s.dynamics.ratio = dynRatio->load();
    s.dynamics.attackMs = dynAttack->load();
    s.dynamics.releaseMs = dynRelease->load();
    s.dynamics.makeupDb = dynMakeup->load();
    s.dynamics.gateDb = dynGate->load();
    s.dynamics.mix = pct (dynMix);

    s.echo.timeMs = echoTime->load();
    s.echo.feedback = pct (echoFeedback);
    s.echo.tone = pct (echoTone);
    s.echo.drive = pct (echoDrive);
    s.echo.wobble = pct (echoWobble);
    s.echo.pingPong = on (echoPingPong);
    s.echo.mix = pct (echoMix);
    s.echoSync = on (echoSync);
    s.echoDivision = index (echoDivision, dsp::kNumDivisions);

    for (int l = 0; l < 2; ++l)
    {
        const auto& r = lfos[(size_t) l];
        auto& o = s.lfos[(size_t) l];
        o.shape = index (r.shape, dsp::kNumLfoShapes);
        o.rateHz = r.rate->load();
        o.sync = on (r.sync);
        o.division = index (r.division, dsp::kNumDivisions);
    }

    s.envelope.attackMs = envAttack->load();
    s.envelope.releaseMs = envRelease->load();
    s.envelope.gainDb = envGain->load();
    for (int m = 0; m < dsp::kNumMacros; ++m)
        s.macros[(size_t) m] = pct (macros[(size_t) m]);

    for (int i = 0; i < dsp::kModSlots; ++i)
    {
        const auto& r = slots[(size_t) i];
        auto& o = s.slots[(size_t) i];
        o.source = index (r.source, dsp::numSources);
        o.destination = index (r.destination, dsp::numDestinations);
        o.amount = pct (r.amount);
    }

    return s;
}

} // namespace hl::params
