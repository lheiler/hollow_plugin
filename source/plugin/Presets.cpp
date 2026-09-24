#include "Presets.h"

namespace hl::presets
{
namespace
{
    using namespace dsp;

    // Plain values, exactly as the knobs show them (percent parameters are 0..100)
    float algo (Algo a) { return (float) a; }
    float filterType (FilterType t) { return (float) t; }
    float impulse (IrType t) { return (float) t; }
    float mode (MotionMode m) { return (float) m; }
    float shape (LfoShape s) { return (float) s; }
    float dest (Destination d) { return (float) d; }
    float source (ModSource s) { return (float) s; }
    float division (const char* name)
    {
        for (int i = 0; i < kNumDivisions; ++i)
            if (juce::String (divisionNames()[i]) == name)
                return (float) i;

        jassertfalse;
        return 9.0f;
    }
} // namespace

dsp::ModuleOrder completeOrder (const std::vector<int>& leading)
{
    dsp::ModuleOrder order {};
    std::array<bool, dsp::numModules> used {};
    size_t slot = 0;

    for (int id : leading)
        if (id >= 0 && id < dsp::numModules && ! used[(size_t) id])
        {
            order[slot++] = id;
            used[(size_t) id] = true;
        }

    for (int id = 0; id < dsp::numModules; ++id)
        if (! used[(size_t) id])
            order[slot++] = id;

    return order;
}

const std::vector<Preset>& all()
{
    static const std::vector<Preset> list = {
        { "Init", "Init", {}, {} },

        //==============================================================================================
        { "Warm Tube Glue", "Drive",
          { { "f1On", 1 }, { "f1Type", filterType (FilterType::highPass12) }, { "f1Cutoff", 35 },
            { "tr1AlgoA", algo (Algo::tube) }, { "tr1Drive", 9 }, { "tr1Tone", 80 }, { "tr1Mix", 75 },
            { "dynOn", 1 }, { "dyThresh", -22 }, { "dyRatio", 2.5f }, { "dyAttack", 15 }, { "dyRelease", 200 }, { "dyMakeup", 2 } }, {} },

        { "Fuzz Wall", "Drive",
          { { "f1On", 1 }, { "f1Type", filterType (FilterType::highPass24) }, { "f1Cutoff", 90 },
            { "tr1AlgoA", algo (Algo::fuzz) }, { "tr1Drive", 30 }, { "tr1Tone", 60 },
            { "f2On", 1 }, { "f2Type", filterType (FilterType::lowPass24) }, { "f2Cutoff", 5200 }, { "f2Reso", 25 },
            { "convOn", 1 }, { "cvIr", impulse (IrType::cab4x12) }, { "cvMix", 100 },
            { "dynOn", 1 }, { "dyThresh", -24 }, { "dyRatio", 6 }, { "dyMakeup", 4 } }, {} },

        { "Octave Scream", "Drive",
          { { "tr1AlgoA", algo (Algo::octaveFuzz) }, { "tr1AlgoB", algo (Algo::sputter) }, { "tr1Morph", 30 }, { "tr1Drive", 30 },
            { "f2On", 1 }, { "f2Type", filterType (FilterType::bandPass12) }, { "f2Cutoff", 1400 }, { "f2Reso", 55 }, { "f2Mix", 65 },
            { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/8") }, { "ecFeedback", 35 }, { "ecMix", 20 } }, {} },

        { "Multiband Crunch", "Drive",
          { { "trBands", 2 }, { "trX1", 180 }, { "trX2", 3200 },
            { "tr1AlgoA", algo (Algo::tube) }, { "tr1Drive", 10 },
            { "tr2AlgoA", algo (Algo::hardClip) }, { "tr2Drive", 24 },
            { "tr3AlgoA", algo (Algo::bitcrush) }, { "tr3Drive", 22 }, { "tr3Mix", 50 },
            { "dynOn", 1 }, { "dyThresh", -20 }, { "dyRatio", 3 } }, {} },

        { "Wavefolder Bass", "Drive",
          { { "tr1AlgoA", algo (Algo::sineFold) }, { "tr1AlgoB", algo (Algo::triFold) }, { "tr1Drive", 20 },
            { "lfo1Shape", shape (LfoShape::sine) }, { "lfo1Rate", 0.25f },
            { "mod1Src", source (sourceLfo1) }, { "mod1Dst", dest (destTrashMorph) }, { "mod1Amt", 60 },
            { "f2On", 1 }, { "f2Type", filterType (FilterType::lowPass24) }, { "f2Cutoff", 1200 }, { "f2Reso", 40 },
            { "mod2Src", source (sourceEnvelope) }, { "mod2Dst", dest (destF2Cutoff) }, { "mod2Amt", 35 } }, {} },

        //==============================================================================================
        { "VHS Memory", "Lo-Fi",
          { { "tr1AlgoA", algo (Algo::tape) }, { "tr1Drive", 8 },
            { "motionOn", 1 }, { "moMode", mode (MotionMode::chorus) }, { "moDepth", 30 }, { "moRate", 0.35f }, { "moMix", 30 },
            { "degradeOn", 1 }, { "dgWow", 45 }, { "dgFlutter", 30 }, { "dgAge", 55 }, { "dgNoise", 22 },
            { "f2On", 1 }, { "f2Type", filterType (FilterType::lowPass12) }, { "f2Cutoff", 7000 } }, {} },

        { "Dusty Vinyl", "Lo-Fi",
          { { "f1On", 1 }, { "f1Type", filterType (FilterType::highPass12) }, { "f1Cutoff", 60 },
            { "tr1AlgoA", algo (Algo::tape) }, { "tr1Drive", 6 },
            { "degradeOn", 1 }, { "dgCrackle", 45 }, { "dgNoise", 12 }, { "dgAge", 40 }, { "dgWow", 15 }, { "dgFlutter", 5 } }, {} },

        { "Broken Radio", "Lo-Fi",
          { { "f1On", 1 }, { "f1Type", filterType (FilterType::highPass24) }, { "f1Cutoff", 250 },
            { "tr1AlgoA", algo (Algo::muLaw) }, { "tr1Drive", 20 },
            { "convOn", 1 }, { "cvIr", impulse (IrType::radio) }, { "cvMix", 100 },
            { "degradeOn", 1 }, { "dgDropout", 45 }, { "dgNoise", 30 }, { "dgAge", 35 }, { "dgWow", 10 }, { "dgFlutter", 10 } }, {} },

        { "Telephone Ghost", "Lo-Fi",
          { { "tr1AlgoA", algo (Algo::diode) }, { "tr1Drive", 18 },
            { "convOn", 1 }, { "cvIr", impulse (IrType::telephone) }, { "cvMix", 100 },
            { "echoOn", 1 }, { "ecTime", 180 }, { "ecFeedback", 40 }, { "ecTone", -40 }, { "ecMix", 30 },
            { "degradeOn", 1 }, { "dgCrackle", 10 }, { "dgWow", 10 }, { "dgNoise", 15 }, { "dgAge", 20 } }, {} },

        { "8-Bit Arcade", "Lo-Fi",
          { { "tr1AlgoA", algo (Algo::bitcrush) }, { "tr1AlgoB", algo (Algo::decimate) }, { "tr1Morph", 50 }, { "tr1Drive", 32 },
            { "f2On", 1 }, { "f2Type", filterType (FilterType::lowPass12) }, { "f2Cutoff", 6500 },
            { "dynOn", 1 }, { "dyThresh", -18 }, { "dyRatio", 4 } }, {} },

        //==============================================================================================
        { "Rusty Chorus", "Motion",
          { { "tr1AlgoA", algo (Algo::tape) }, { "tr1Drive", 14 },
            { "motionOn", 1 }, { "moMode", mode (MotionMode::chorus) }, { "moDepth", 70 }, { "moRate", 0.4f }, { "moMix", 50 }, { "moSpread", 80 },
            { "degradeOn", 1 }, { "dgAge", 30 }, { "dgWow", 20 }, { "dgNoise", 5 }, { "dgFlutter", 10 } }, {} },

        { "Jet Flange Grit", "Motion",
          { { "tr1AlgoA", algo (Algo::hardClip) }, { "tr1Drive", 20 },
            { "motionOn", 1 }, { "moMode", mode (MotionMode::flanger) }, { "moDepth", 80 }, { "moFeedback", 70 }, { "moRate", 0.15f }, { "moMix", 50 } }, {} },

        { "Barberpole Shift", "Motion",
          { { "tr1AlgoA", algo (Algo::warm) }, { "tr1Drive", 6 },
            { "motionOn", 1 }, { "moMode", mode (MotionMode::freqShift) }, { "moFreq", 25 }, { "moFeedback", 70 }, { "moDepth", 0 }, { "moMix", 60 },
            { "echoOn", 1 }, { "ecTime", 300 }, { "ecFeedback", 55 }, { "ecMix", 35 } }, {} },

        { "Ring Bell Drone", "Motion",
          { { "tr1AlgoA", algo (Algo::warm) }, { "tr1Drive", 10 },
            { "motionOn", 1 }, { "moMode", mode (MotionMode::ringMod) }, { "moFreq", 330 }, { "moDepth", 15 }, { "moRate", 0.2f }, { "moMix", 55 },
            { "convOn", 1 }, { "cvIr", impulse (IrType::metalBowl) }, { "cvMix", 50 },
            { "echoOn", 1 }, { "ecTime", 600 }, { "ecFeedback", 50 }, { "ecMix", 25 } }, {} },

        { "Stutter Tremolo", "Motion",
          { { "tr1AlgoA", algo (Algo::warm) }, { "tr1Drive", 10 },
            { "motionOn", 1 }, { "moMode", mode (MotionMode::tremolo) }, { "moSync", 1 }, { "moDiv", division ("1/16") },
            { "moDepth", 90 }, { "moFeedback", 80 }, { "moSpread", 100 }, { "moMix", 100 } }, {} },

        //==============================================================================================
        { "Tin Can Vocal", "Space",
          { { "f1On", 1 }, { "f1Type", filterType (FilterType::highPass12) }, { "f1Cutoff", 220 },
            { "tr1AlgoA", algo (Algo::valve) }, { "tr1Drive", 16 },
            { "convOn", 1 }, { "cvIr", impulse (IrType::tinCan) }, { "cvMix", 70 },
            { "dynOn", 1 }, { "dyThresh", -20 }, { "dyRatio", 3 } }, {} },

        { "Spring Drip", "Space",
          { { "f1On", 1 }, { "f1Type", filterType (FilterType::highPass12) }, { "f1Cutoff", 120 },
            { "tr1AlgoA", algo (Algo::tube) }, { "tr1Drive", 8 },
            { "convOn", 1 }, { "cvIr", impulse (IrType::springTank) }, { "cvMix", 45 }, { "cvDamp", 20 } }, {} },

        { "Cistern Sludge", "Space",
          { { "convOn", 1 }, { "cvIr", impulse (IrType::cistern) }, { "cvMix", 55 }, { "cvDamp", 60 },
            { "tr1AlgoA", algo (Algo::fuzz) }, { "tr1Drive", 26 },
            { "f2On", 1 }, { "f2Type", filterType (FilterType::lowPass24) }, { "f2Cutoff", 3000 },
            { "dynOn", 1 }, { "dyThresh", -26 }, { "dyRatio", 5 } },
          { moduleConvolve, moduleTrash, moduleFilter2 } }, // the reverb goes *into* the fuzz

        { "Reverse Plate Swell", "Space",
          { { "tr1AlgoA", algo (Algo::tape) }, { "tr1Drive", 10 },
            { "convOn", 1 }, { "cvIr", impulse (IrType::steelPlate) }, { "cvReverse", 1 }, { "cvMix", 50 },
            { "echoOn", 1 }, { "ecTime", 420 }, { "ecFeedback", 30 }, { "ecMix", 20 }, { "ecTone", -40 } }, {} },

        { "Gong Bath", "Space",
          { { "tr1AlgoA", algo (Algo::warm) }, { "tr1Drive", 6 },
            { "convOn", 1 }, { "cvIr", impulse (IrType::gong) }, { "cvMix", 50 }, { "cvSize", 150 },
            { "motionOn", 1 }, { "moMode", mode (MotionMode::phaser) }, { "moRate", 0.08f }, { "moDepth", 70 }, { "moFeedback", 50 }, { "moMix", 50 },
            { "degradeOn", 1 }, { "dgWow", 20 }, { "dgAge", 20 }, { "dgNoise", 0 }, { "dgFlutter", 0 } }, {} },

        //==============================================================================================
        { "Glitch Machine", "Chaos",
          { { "tr1AlgoA", algo (Algo::bitFlip) }, { "tr1AlgoB", algo (Algo::decimate) }, { "tr1Drive", 14 },
            { "degradeOn", 1 }, { "dgGlitch", 70 }, { "dgWow", 0 }, { "dgFlutter", 0 }, { "dgAge", 10 }, { "dgNoise", 0 },
            { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/16") }, { "ecFeedback", 50 }, { "ecMix", 25 },
            { "lfo2Shape", shape (LfoShape::sampleHold) }, { "lfo2Sync", 1 }, { "lfo2Div", division ("1/16") },
            { "mod1Src", source (sourceLfo2) }, { "mod1Dst", dest (destTrashMorph) }, { "mod1Amt", 80 },
            { "f2On", 1 }, { "f2Type", filterType (FilterType::bandPass12) }, { "f2Cutoff", 2000 }, { "f2Reso", 40 }, { "f2Mix", 50 },
            { "mod2Src", source (sourceLfo2) }, { "mod2Dst", dest (destF2Cutoff) }, { "mod2Amt", 40 } }, {} },

        { "Runaway Echo", "Chaos",
          { { "tr1AlgoA", algo (Algo::warm) }, { "tr1Drive", 8 },
            { "echoOn", 1 }, { "ecTime", 420 }, { "ecFeedback", 110 }, { "ecDrive", 60 }, { "ecWobble", 50 }, { "ecTone", -30 }, { "ecMix", 45 },
            { "lfo1Shape", shape (LfoShape::smoothRandom) }, { "lfo1Rate", 0.3f },
            { "mod1Src", source (sourceLfo1) }, { "mod1Dst", dest (destEchoTime) }, { "mod1Amt", 8 } }, {} },

        { "Screaming Filter", "Chaos",
          { { "f1On", 1 }, { "f1Type", filterType (FilterType::lowPass24) }, { "f1Cutoff", 700 }, { "f1Reso", 95 }, { "f1Drive", 18 },
            { "lfo1Shape", shape (LfoShape::triangle) }, { "lfo1Rate", 0.2f },
            { "mod1Src", source (sourceLfo1) }, { "mod1Dst", dest (destF1Cutoff) }, { "mod1Amt", 45 },
            { "tr1AlgoA", algo (Algo::hardClip) }, { "tr1Drive", 18 },
            { "dynOn", 1 }, { "dyThresh", -20 }, { "dyRatio", 4 } }, {} },

        { "Dead Speaker", "Chaos",
          { { "tr1AlgoA", algo (Algo::sputter) }, { "tr1Drive", 28 }, { "tr1Bias", 40 },
            { "convOn", 1 }, { "cvIr", impulse (IrType::tornCone) }, { "cvMix", 100 },
            { "degradeOn", 1 }, { "dgDropout", 30 }, { "dgWow", 0 }, { "dgFlutter", 0 }, { "dgAge", 25 }, { "dgNoise", 5 } }, {} },

        { "Envelope Ghosts", "Chaos",
          { { "tr1AlgoA", algo (Algo::warm) }, { "tr1AlgoB", algo (Algo::westCoast) }, { "tr1Drive", 18 },
            { "f2On", 1 }, { "f2Type", filterType (FilterType::bandPass12) }, { "f2Cutoff", 600 }, { "f2Reso", 35 }, { "f2Mix", 80 },
            { "envAttack", 2 }, { "envRelease", 250 }, { "envGain", 6 },
            { "mod1Src", source (sourceEnvelope) }, { "mod1Dst", dest (destF2Cutoff) }, { "mod1Amt", 55 },
            { "mod2Src", source (sourceEnvelope) }, { "mod2Dst", dest (destTrashMorph) }, { "mod2Amt", 60 },
            { "convOn", 1 }, { "cvIr", impulse (IrType::closet) }, { "cvMix", 30 } }, {} },

        { "Shoegaze Wall", "Chaos",
          { { "motionOn", 1 }, { "moMode", mode (MotionMode::chorus) }, { "moDepth", 60 }, { "moMix", 40 }, { "moRate", 0.3f },
            { "tr1AlgoA", algo (Algo::fuzz) }, { "tr1Drive", 34 }, { "tr1Tone", 55 },
            { "f2On", 1 }, { "f2Type", filterType (FilterType::lowPass12) }, { "f2Cutoff", 7000 },
            { "convOn", 1 }, { "cvIr", impulse (IrType::concreteHall) }, { "cvMix", 45 }, { "cvSize", 150 },
            { "echoOn", 1 }, { "ecTime", 500 }, { "ecFeedback", 60 }, { "ecMix", 30 }, { "ecWobble", 40 },
            { "dynOn", 1 }, { "dyThresh", -26 }, { "dyRatio", 4 } },
          { moduleMotion, moduleTrash, moduleFilter2, moduleConvolve, moduleEcho } }, // chorus *into* the fuzz
    };

    return list;
}

} // namespace hl::presets
