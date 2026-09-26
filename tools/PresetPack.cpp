#include "PresetPack.h"

namespace hl::presets
{
namespace
{
    using namespace dsp;
    using Values = std::vector<std::pair<juce::String, float>>;

    // Plain values, exactly as the knobs show them (percent parameters are 0..100)
    template <typename Enum>
    float e (Enum value) { return (float) value; }

    float division (const char* name)
    {
        for (int i = 0; i < kNumDivisions; ++i)
            if (juce::String (divisionNames()[i]) == name)
                return (float) i;

        jassertfalse;
        return 9.0f;
    }

    using A = Algo;
    using F = FilterType;
    using I = IrType;
    using M = MotionMode;
    using S = LfoShape;

    constexpr auto L1 = sourceLfo1, L2 = sourceLfo2, Env = sourceEnvelope, M1 = sourceMacro1, M2 = sourceMacro2;

    struct Route
    {
        ModSource source;
        Destination destination;
        float amount; // percent of the destination's range
    };

    /** A preset on top of the defaults; routes fill the matrix slots in order. A few presets carry an output
    trim ("outGain") where the Auto Level estimate misses them; HollowSnapshot --write-pack measures that. */
    Preset make (const char* name, const char* category, Values values, std::initializer_list<Route> routes = {}, std::vector<int> order = {})
    {
        int slot = 0;

        for (const auto& r : routes)
        {
            jassert (slot < kModSlots);
            const auto n = juce::String (++slot);
            values.push_back ({ "mod" + n + "Src", e (r.source) });
            values.push_back ({ "mod" + n + "Dst", e (r.destination) });
            values.push_back ({ "mod" + n + "Amt", r.amount });
        }

        return { name, category, std::move (values), std::move (order) };
    }

    // Chain orders (leading modules; the rest follow in the default order)
    constexpr int f1 = moduleFilter1, trash = moduleTrash, f2 = moduleFilter2, conv = moduleConvolve,
                  motion = moduleMotion, degrade = moduleDegrade, dyn = moduleDynamics, echo = moduleEcho;
} // namespace

const std::vector<Preset>& pack()
{
    static const std::vector<Preset> list = {
        //==============================================================================================
        // Drive: saturation to fuzz, amps and exciters
        make ("Console Heat", "Drive",
              { { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 8 }, { "tr1Tone", 85 },
                { "dynOn", 1 }, { "dyThresh", -20 }, { "dyRatio", 2 }, { "dyAttack", 20 }, { "dyRelease", 150 } },
              { { M1, destTrashDrive, 30 }, { M2, destTrashTone, -35 } }),

        make ("Pushed Preamp", "Drive",
              { { "f1On", 1 }, { "f1Type", e (F::highPass12) }, { "f1Cutoff", 60 },
                { "tr1AlgoA", e (A::tube) }, { "tr1Drive", 16 }, { "tr1Bias", 15 }, { "tr1Tone", 75 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass12) }, { "f2Cutoff", 9500 }, { "f2Reso", 10 } },
              { { M1, destTrashDrive, 30 }, { M2, destTrashBias, 40 } }),

        make ("Tweed Breakup", "Drive",
              { { "tr1AlgoA", e (A::valve) }, { "tr1Drive", 18 }, { "tr1Tone", 70 },
                { "convOn", 1 }, { "cvIr", e (I::tweed) }, { "cvMix", 100 },
                { "envAttack", 5 }, { "envRelease", 120 } },
              { { Env, destTrashDrive, 12 }, { M1, destTrashDrive, 25 }, { M2, destConvMix, -50 } }),

        make ("Stack Crunch", "Drive",
              { { "f1On", 1 }, { "f1Type", e (F::highPass24) }, { "f1Cutoff", 90 },
                { "tr1AlgoA", e (A::asymClip) }, { "tr1Drive", 26 }, { "tr1Tone", 65 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass24) }, { "f2Cutoff", 6500 }, { "f2Reso", 10 },
                { "convOn", 1 }, { "cvIr", e (I::cab4x12) }, { "cvMix", 100 },
                { "dynOn", 1 }, { "dyThresh", -22 }, { "dyRatio", 3 }, { "dyAttack", 10 } },
              { { M1, destTrashDrive, 25 }, { M2, destConvMix, -60 } }),

        make ("Diode Grind", "Drive",
              { { "trBands", 1 }, { "trX1", 220 },
                { "tr1AlgoA", e (A::warm) }, { "tr1AlgoB", e (A::warm) }, { "tr1Drive", 8 },
                { "tr2AlgoA", e (A::diode) }, { "tr2AlgoB", e (A::diode) }, { "tr2Drive", 28 }, { "tr2Tone", 60 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass24) }, { "f2Cutoff", 7500 } },
              { { M1, destTrashDrive, 20 }, { M2, destF2Cutoff, -25 } }),

        make ("Iron Glue", "Drive",
              { { "tr1AlgoA", e (A::transformer) }, { "tr1Drive", 18 }, { "tr1Tone", 80 },
                { "dynOn", 1 }, { "dyThresh", -26 }, { "dyRatio", 4 }, { "dyAttack", 10 }, { "dyRelease", 100 }, { "dyMakeup", 4 }, { "dyMix", 60 } },
              { { M1, destTrashDrive, 25 }, { M2, destDynMix, 40 } }),

        make ("Velvet Fuzz", "Drive",
              { { "f1On", 1 }, { "f1Type", e (F::highPass12) }, { "f1Cutoff", 100 },
                { "tr1AlgoA", e (A::fuzz) }, { "tr1Drive", 24 }, { "tr1Tone", 40 }, { "tr1Mix", 75 } },
              { { M1, destTrashTone, 45 }, { M2, destTrashMix, 25 } }),

        make ("Sine Clip Shine", "Drive",
              { { "f1On", 1 }, { "f1Type", e (F::highPass12) }, { "f1Cutoff", 120 },
                { "tr1AlgoA", e (A::sineClip) }, { "tr1AlgoB", e (A::cheby2) }, { "tr1Morph", 25 }, { "tr1Drive", 20 } },
              { { M1, destTrashMorph, 60 }, { M2, destTrashDrive, 20 } }),

        make ("Air Exciter", "Drive",
              { { "trBands", 1 }, { "trX1", 3000 },
                { "tr1AlgoA", e (A::warm) }, { "tr1AlgoB", e (A::warm) }, { "tr1Drive", 0 }, { "tr1Mix", 0 },
                { "tr2AlgoA", e (A::cheby3) }, { "tr2AlgoB", e (A::cheby5) }, { "tr2Drive", 18 }, { "tr2Mix", 50 },
                { "lfo1Shape", e (S::sine) }, { "lfo1Rate", 0.1f } },
              { { L1, destTrashMorph, 30 }, { M1, destTrashMix, 40 }, { M2, destTrashDrive, 20 } }),

        make ("Buzzsaw Lead", "Drive",
              { { "tr1AlgoA", e (A::buzz) }, { "tr1AlgoB", e (A::fuzz) }, { "tr1Morph", 40 }, { "tr1Drive", 32 }, { "tr1Tone", 55 },
                { "f2On", 1 }, { "f2Type", e (F::bandPass12) }, { "f2Cutoff", 1800 }, { "f2Reso", 30 }, { "f2Mix", 50 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/8D") }, { "ecFeedback", 30 }, { "ecMix", 15 } },
              { { M1, destF2Cutoff, 20 }, { M2, destEchoMix, 25 } }),

        //==============================================================================================
        // Bass: dirt on top, the low end kept whole
        make ("Sub Safe Grit", "Bass",
              { { "trBands", 1 }, { "trX1", 120 },
                { "tr1AlgoA", e (A::warm) }, { "tr1AlgoB", e (A::warm) }, { "tr1Drive", 4 },
                { "tr2AlgoA", e (A::fuzz) }, { "tr2AlgoB", e (A::fuzz) }, { "tr2Drive", 28 }, { "tr2Tone", 55 } },
              { { M1, destTrashDrive, 20 }, { M2, destTrashTone, 35 } }),

        make ("Reese Shredder", "Bass",
              { { "trBands", 2 }, { "trX1", 150 }, { "trX2", 1800 },
                { "tr1AlgoA", e (A::tube) }, { "tr1AlgoB", e (A::tube) }, { "tr1Drive", 6 },
                { "tr2AlgoA", e (A::hardClip) }, { "tr2Drive", 24 },
                { "tr3AlgoA", e (A::bitcrush) }, { "tr3Drive", 20 }, { "tr3Mix", 60 },
                { "motionOn", 1 }, { "moMode", e (M::chorus) }, { "moDepth", 40 }, { "moRate", 0.3f }, { "moMix", 30 }, { "moSpread", 100 } },
              { { M1, destMotionDepth, 50 }, { M2, destTrashDrive, 20 } }),

        make ("Wobble Folder", "Bass",
              { { "tr1AlgoA", e (A::sineFold) }, { "tr1AlgoB", e (A::triFold) }, { "tr1Drive", 22 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass24) }, { "f2Cutoff", 900 }, { "f2Reso", 55 },
                { "lfo1Shape", e (S::sine) }, { "lfo1Sync", 1 }, { "lfo1Div", division ("1/8") } },
              { { L1, destF2Cutoff, 30 }, { L1, destTrashMorph, 40 }, { M1, destF2Cutoff, 25 }, { M2, destTrashDrive, 20 } }),

        make ("Acid Squelch", "Bass",
              { { "f1On", 1 }, { "f1Type", e (F::lowPass24) }, { "f1Cutoff", 200 }, { "f1Reso", 80 }, { "f1Drive", 10 },
                { "tr1AlgoA", e (A::diode) }, { "tr1Drive", 20 },
                { "envAttack", 1 }, { "envRelease", 180 },
                { "dynOn", 1 }, { "dyThresh", -20 }, { "dyRatio", 3 },
                { "outGain", -4 } },
              { { Env, destF1Cutoff, 45 }, { M1, destF1Reso, 15 }, { M2, destTrashDrive, 25 } }),

        make ("Octave Up Bass", "Bass",
              { { "trBands", 1 }, { "trX1", 250 },
                { "tr1AlgoA", e (A::tube) }, { "tr1AlgoB", e (A::tube) }, { "tr1Drive", 4 },
                { "tr2AlgoA", e (A::octaveFuzz) }, { "tr2AlgoB", e (A::octaveFuzz) }, { "tr2Drive", 26 }, { "tr2Mix", 60 } },
              { { M1, destTrashMix, 40 }, { M2, destTrashTone, -40 } }),

        make ("Tape Bass Glue", "Bass",
              { { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 12 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass12) }, { "f2Cutoff", 5000 },
                { "degradeOn", 1 }, { "dgWow", 4 }, { "dgFlutter", 6 }, { "dgAge", 30 }, { "dgNoise", 0 },
                { "dynOn", 1 }, { "dyThresh", -24 }, { "dyRatio", 4 }, { "dyAttack", 30 }, { "dyRelease", 120 }, { "dyMakeup", 3 } },
              { { M1, destDegradeAge, 40 }, { M2, destTrashDrive, 20 } }),

        make ("Growl Formant", "Bass",
              { { "tr1AlgoA", e (A::fuzz) }, { "tr1Drive", 26 },
                { "f2On", 1 }, { "f2Type", e (F::vowel) }, { "f2Cutoff", 250 }, { "f2Reso", 60 }, { "f2Mix", 80 },
                { "lfo1Shape", e (S::triangle) }, { "lfo1Sync", 1 }, { "lfo1Div", division ("1/4") } },
              { { L1, destF2Cutoff, 30 }, { M1, destTrashDrive, 20 }, { M2, destF2Reso, 30 } }),

        make ("Distorted 808", "Bass",
              { { "trBands", 1 }, { "trX1", 90 },
                { "tr1AlgoA", e (A::warm) }, { "tr1AlgoB", e (A::warm) }, { "tr1Drive", 12 },
                { "tr2AlgoA", e (A::hardClip) }, { "tr2AlgoB", e (A::hardClip) }, { "tr2Drive", 30 }, { "tr2Tone", 60 },
                { "dynOn", 1 }, { "dyThresh", -18 }, { "dyRatio", 6 }, { "dyAttack", 2 }, { "dyRelease", 80 },
                { "outGain", -4 } },
              { { M1, destTrashDrive, 25 }, { M2, destTrashTone, -30 } }),

        make ("Rectified Rumble", "Bass",
              { { "tr1AlgoA", e (A::halfWave) }, { "tr1AlgoB", e (A::fullWave) }, { "tr1Morph", 50 }, { "tr1Drive", 18 }, { "tr1Mix", 60 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass24) }, { "f2Cutoff", 2500 }, { "f2Reso", 30 } },
              { { M1, destTrashMorph, 50 }, { M2, destF2Cutoff, 25 } }),

        make ("Crushed Low End", "Bass",
              { { "trBands", 1 }, { "trX1", 150 },
                { "tr1AlgoA", e (A::tube) }, { "tr1AlgoB", e (A::tube) }, { "tr1Drive", 4 },
                { "tr2AlgoA", e (A::bitcrush) }, { "tr2AlgoB", e (A::decimate) }, { "tr2Morph", 30 }, { "tr2Drive", 24 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass12) }, { "f2Cutoff", 9000 } },
              { { M1, destTrashMorph, 70 }, { M2, destF2Cutoff, -35 } }),

        //==============================================================================================
        // Drums: smash, crunch, rooms and dust
        make ("Parallel Smash", "Drums",
              { { "tr1AlgoA", e (A::hardClip) }, { "tr1Drive", 24 }, { "tr1Mix", 50 },
                { "dynOn", 1 }, { "dyThresh", -32 }, { "dyRatio", 10 }, { "dyAttack", 1 }, { "dyRelease", 60 }, { "dyMakeup", 6 }, { "dyMix", 50 } },
              { { M1, destDynMix, 50 }, { M2, destTrashDrive, 25 } }),

        make ("Room Crush", "Drums",
              { { "convOn", 1 }, { "cvIr", e (I::tiledRoom) }, { "cvMix", 35 }, { "cvSize", 80 },
                { "dynOn", 1 }, { "dyThresh", -34 }, { "dyRatio", 12 }, { "dyAttack", 5 }, { "dyRelease", 150 }, { "dyMakeup", 8 },
                { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 10 } },
              { { M1, destConvMix, 40 }, { M2, destDynThreshold, -20 } },
              { conv, dyn, trash }), // the room is squashed before it's saturated

        make ("Break Dust", "Drums",
              { { "f1On", 1 }, { "f1Type", e (F::highPass12) }, { "f1Cutoff", 40 },
                { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 14 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass12) }, { "f2Cutoff", 9000 },
                { "degradeOn", 1 }, { "dgWow", 5 }, { "dgFlutter", 8 }, { "dgAge", 45 }, { "dgNoise", 12 }, { "dgCrackle", 25 },
                { "dynOn", 1 }, { "dyThresh", -20 }, { "dyRatio", 4 }, { "dyAttack", 10 }, { "dyRelease", 100 } },
              { { M1, destDegradeAge, 40 }, { M2, destF2Cutoff, -30 } }),

        make ("Snare Fizz", "Drums",
              { { "trBands", 1 }, { "trX1", 1500 },
                { "tr1AlgoA", e (A::tube) }, { "tr1AlgoB", e (A::tube) }, { "tr1Drive", 6 },
                { "tr2AlgoA", e (A::fuzz) }, { "tr2AlgoB", e (A::fuzz) }, { "tr2Drive", 30 }, { "tr2Tone", 80 }, { "tr2Mix", 60 } },
              { { M1, destTrashMix, 40 }, { M2, destTrashDrive, 20 } }),

        make ("Gated Crunch", "Drums",
              { { "tr1AlgoA", e (A::asymClip) }, { "tr1Drive", 22 },
                { "convOn", 1 }, { "cvIr", e (I::concreteHall) }, { "cvMix", 45 }, { "cvSize", 70 },
                { "dynOn", 1 }, { "dyGate", -35 }, { "dyThresh", -20 }, { "dyRatio", 4 }, { "dyAttack", 1 }, { "dyRelease", 40 } },
              { { M1, destConvMix, 40 }, { M2, destTrashDrive, 20 } }),

        make ("Wobbly Tape Kit", "Drums",
              { { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 10 },
                { "degradeOn", 1 }, { "dgWow", 40 }, { "dgFlutter", 25 }, { "dgAge", 40 }, { "dgNoise", 6 } },
              { { M1, destDegradeWow, 40 }, { M2, destDegradeFlutter, 40 } }),

        make ("Cardboard Kit", "Drums",
              { { "tr1AlgoA", e (A::bitcrush) }, { "tr1Drive", 16 }, { "tr1Mix", 50 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass12) }, { "f2Cutoff", 6000 },
                { "convOn", 1 }, { "cvIr", e (I::cardboardBox) }, { "cvMix", 40 } },
              { { M1, destConvMix, 40 }, { M2, destTrashMix, 40 } }),

        make ("Transient Spray", "Drums",
              { { "tr1AlgoA", e (A::decimate) }, { "tr1Drive", 20 }, { "tr1Mix", 35 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/16") }, { "ecFeedback", 20 }, { "ecTone", 30 }, { "ecPingPong", 1 }, { "ecMix", 15 } },
              { { M1, destEchoMix, 30 }, { M2, destEchoFeedback, 40 } }),

        make ("Room Mic Fuzz", "Drums",
              { { "convOn", 1 }, { "cvIr", e (I::closet) }, { "cvMix", 50 },
                { "dynOn", 1 }, { "dyThresh", -30 }, { "dyRatio", 8 }, { "dyAttack", 3 }, { "dyRelease", 120 },
                { "tr1AlgoA", e (A::fuzz) }, { "tr1Drive", 28 }, { "tr1Mix", 60 } },
              { { M1, destTrashDrive, 25 }, { M2, destConvMix, 40 } },
              { conv, dyn, trash }), // a squashed room mic into a fuzz

        make ("Kick Thump", "Drums",
              { { "f1On", 1 }, { "f1Type", e (F::highPass12) }, { "f1Cutoff", 25 },
                { "trBands", 1 }, { "trX1", 150 },
                { "tr1AlgoA", e (A::tube) }, { "tr1AlgoB", e (A::tube) }, { "tr1Drive", 16 },
                { "tr2AlgoA", e (A::warm) }, { "tr2AlgoB", e (A::warm) }, { "tr2Drive", 6 },
                { "dynOn", 1 }, { "dyThresh", -16 }, { "dyRatio", 3 }, { "dyAttack", 20 }, { "dyRelease", 100 } },
              { { M1, destTrashDrive, 25 }, { M2, destDynThreshold, -15 } }),

        //==============================================================================================
        // Vocal: radios, megaphones, formants and ghosts
        make ("Radio Host", "Vocal",
              { { "f1On", 1 }, { "f1Type", e (F::highPass24) }, { "f1Cutoff", 300 },
                { "tr1AlgoA", e (A::diode) }, { "tr1Drive", 14 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass24) }, { "f2Cutoff", 3500 },
                { "convOn", 1 }, { "cvIr", e (I::radio) }, { "cvMix", 60 },
                { "degradeOn", 1 }, { "dgWow", 0 }, { "dgFlutter", 0 }, { "dgAge", 20 }, { "dgNoise", 8 } },
              { { M1, destTrashDrive, 25 }, { M2, destDegradeNoise, 30 } }),

        make ("Megaphone Rally", "Vocal",
              { { "f1On", 1 }, { "f1Type", e (F::highPass24) }, { "f1Cutoff", 400 },
                { "tr1AlgoA", e (A::hardClip) }, { "tr1Drive", 22 },
                { "convOn", 1 }, { "cvIr", e (I::megaphone) }, { "cvMix", 100 } },
              { { M1, destTrashDrive, 25 }, { M2, destConvMix, -40 } }),

        make ("Walkie Talkie", "Vocal",
              { { "f1On", 1 }, { "f1Type", e (F::bandPass24) }, { "f1Cutoff", 1800 }, { "f1Reso", 30 },
                { "tr1AlgoA", e (A::hardClip) }, { "tr1Drive", 28 },
                { "degradeOn", 1 }, { "dgWow", 0 }, { "dgFlutter", 0 }, { "dgAge", 30 }, { "dgNoise", 20 }, { "dgDropout", 20 },
                { "dynOn", 1 }, { "dyGate", -40 }, { "dyRatio", 1 } },
              { { M1, destDegradeDropout, 40 }, { M2, destDegradeNoise, 30 } }),

        make ("Formant Choir", "Vocal",
              { { "tr1AlgoA", e (A::warm) }, { "tr1Drive", 6 },
                { "f2On", 1 }, { "f2Type", e (F::vowel) }, { "f2Cutoff", 632 }, { "f2Reso", 50 }, { "f2Mix", 60 },
                { "motionOn", 1 }, { "moMode", e (M::chorus) }, { "moDepth", 60 }, { "moRate", 0.3f }, { "moMix", 50 }, { "moSpread", 100 },
                { "convOn", 1 }, { "cvIr", e (I::concreteHall) }, { "cvMix", 30 },
                { "lfo1Shape", e (S::triangle) }, { "lfo1Rate", 0.1f } },
              { { L1, destF2Cutoff, 25 }, { M1, destF2Mix, 40 }, { M2, destConvMix, 40 } }),

        make ("Robot Ring", "Vocal",
              { { "tr1AlgoA", e (A::bitcrush) }, { "tr1Drive", 14 }, { "tr1Mix", 40 },
                { "motionOn", 1 }, { "moMode", e (M::ringMod) }, { "moFreq", 90 }, { "moDepth", 0 }, { "moMix", 50 } },
              { { M1, destMotionFreq, 30 }, { M2, destMotionMix, 50 } }),

        make ("Harmonic Whisper", "Vocal",
              { { "f1On", 1 }, { "f1Type", e (F::highPass12) }, { "f1Cutoff", 200 },
                { "tr1AlgoA", e (A::cheby2) }, { "tr1Drive", 12 }, { "tr1Mix", 50 },
                { "convOn", 1 }, { "cvIr", e (I::springTank) }, { "cvMix", 25 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/4") }, { "ecFeedback", 30 }, { "ecTone", 20 }, { "ecMix", 20 } },
              { { M1, destEchoMix, 30 }, { M2, destTrashMix, 50 } }),

        make ("Distorted Double", "Vocal",
              { { "tr1AlgoA", e (A::tube) }, { "tr1Drive", 18 },
                { "motionOn", 1 }, { "moMode", e (M::chorus) }, { "moDepth", 30 }, { "moRate", 0.2f }, { "moMix", 50 }, { "moSpread", 100 } },
              { { M1, destTrashDrive, 25 }, { M2, destMotionDepth, 50 } }),

        make ("Tin Can Phone", "Vocal",
              { { "f1On", 1 }, { "f1Type", e (F::highPass12) }, { "f1Cutoff", 250 },
                { "tr1AlgoA", e (A::muLaw) }, { "tr1Drive", 18 },
                { "convOn", 1 }, { "cvIr", e (I::tinCan) }, { "cvMix", 80 },
                { "degradeOn", 1 }, { "dgWow", 0 }, { "dgFlutter", 0 }, { "dgAge", 10 }, { "dgNoise", 0 }, { "dgCrackle", 10 },
                { "outGain", -5 } },
              { { M1, destConvMix, 20 }, { M2, destTrashDrive, 20 } }),

        make ("Ghost Voice", "Vocal",
              { { "tr1AlgoA", e (A::warm) }, { "tr1Drive", 8 },
                { "motionOn", 1 }, { "moMode", e (M::freqShift) }, { "moFreq", 20 }, { "moDepth", 0 }, { "moFeedback", 40 }, { "moSpread", 100 }, { "moMix", 40 },
                { "convOn", 1 }, { "cvIr", e (I::steelPlate) }, { "cvReverse", 1 }, { "cvMix", 30 } },
              { { M1, destMotionFreq, 20 }, { M2, destConvMix, 40 } }),

        make ("Screamer Vocal", "Vocal",
              { { "tr1AlgoA", e (A::fuzz) }, { "tr1Drive", 30 }, { "tr1Tone", 55 }, { "tr1Mix", 60 },
                { "f2On", 1 }, { "f2Type", e (F::bandPass12) }, { "f2Cutoff", 1200 }, { "f2Mix", 60 },
                { "dynOn", 1 }, { "dyThresh", -24 }, { "dyRatio", 6 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/8") }, { "ecFeedback", 25 }, { "ecMix", 15 } },
              { { M1, destTrashMix, 40 }, { M2, destEchoMix, 30 } }),

        //==============================================================================================
        // Lo-Fi: tape, vinyl, samplers and old electronics
        make ("Cassette Deck", "Lo-Fi",
              { { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 10 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass12) }, { "f2Cutoff", 9000 },
                { "degradeOn", 1 }, { "dgWow", 25 }, { "dgFlutter", 20 }, { "dgAge", 45 }, { "dgNoise", 15 } },
              { { M1, destDegradeAge, 40 }, { M2, destDegradeWow, 40 } }),

        make ("Dub Plate", "Lo-Fi",
              { { "f1On", 1 }, { "f1Type", e (F::highPass12) }, { "f1Cutoff", 45 },
                { "tr1AlgoA", e (A::tube) }, { "tr1Drive", 8 },
                { "degradeOn", 1 }, { "dgWow", 8 }, { "dgFlutter", 4 }, { "dgAge", 30 }, { "dgNoise", 8 }, { "dgCrackle", 35 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/4D") }, { "ecFeedback", 40 }, { "ecTone", -50 }, { "ecMix", 20 } },
              { { M1, destEchoFeedback, 40 }, { M2, destEchoMix, 30 } }),

        make ("Sampler 12-Bit", "Lo-Fi",
              { { "tr1AlgoA", e (A::bitcrush) }, { "tr1AlgoB", e (A::decimate) }, { "tr1Morph", 40 }, { "tr1Drive", 10 }, { "tr1Mix", 60 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass24) }, { "f2Cutoff", 11000 } },
              { { M1, destTrashMorph, 60 }, { M2, destTrashDrive, 25 } }),

        make ("Answering Machine", "Lo-Fi",
              { { "tr1AlgoA", e (A::muLaw) }, { "tr1Drive", 16 },
                { "convOn", 1 }, { "cvIr", e (I::telephone) }, { "cvMix", 100 },
                { "degradeOn", 1 }, { "dgWow", 20 }, { "dgFlutter", 30 }, { "dgAge", 60 }, { "dgNoise", 25 }, { "dgDropout", 15 } },
              { { M1, destDegradeAge, 30 }, { M2, destDegradeDropout, 40 } }),

        make ("Warped Record", "Lo-Fi",
              { { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 6 },
                { "degradeOn", 1 }, { "dgWow", 60 }, { "dgFlutter", 10 }, { "dgAge", 35 }, { "dgNoise", 5 }, { "dgCrackle", 40 } },
              { { M1, destDegradeWow, 40 }, { M2, destDegradeCrackle, 40 } }),

        make ("Bedroom Demo", "Lo-Fi",
              { { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 12 },
                { "convOn", 1 }, { "cvIr", e (I::closet) }, { "cvMix", 40 },
                { "degradeOn", 1 }, { "dgWow", 10 }, { "dgFlutter", 10 }, { "dgAge", 30 }, { "dgNoise", 20 },
                { "dynOn", 1 }, { "dyThresh", -26 }, { "dyRatio", 5 }, { "dyAttack", 10 }, { "dyRelease", 150 } },
              { { M1, destDegradeNoise, 30 }, { M2, destConvMix, 40 } }),

        make ("Toy Keyboard", "Lo-Fi",
              { { "tr1AlgoA", e (A::decimate) }, { "tr1Drive", 26 }, { "tr1Tone", 60 },
                { "convOn", 1 }, { "cvIr", e (I::cardboardBox) }, { "cvMix", 40 },
                { "motionOn", 1 }, { "moMode", e (M::vibrato) }, { "moRate", 5 }, { "moDepth", 25 }, { "moMix", 100 } },
              { { M1, destMotionDepth, 50 }, { M2, destMotionRate, 20 } }),

        make ("Transistor Radio", "Lo-Fi",
              { { "tr1AlgoA", e (A::tube) }, { "tr1Drive", 18 },
                { "convOn", 1 }, { "cvIr", e (I::radio) }, { "cvMix", 100 },
                { "degradeOn", 1 }, { "dgWow", 0 }, { "dgFlutter", 0 }, { "dgAge", 25 }, { "dgNoise", 30 }, { "dgDropout", 25 }, { "dgCrackle", 10 },
                { "lfo2Shape", e (S::smoothRandom) }, { "lfo2Rate", 0.5f } },
              { { L2, destDegradeNoise, 20 }, { M1, destTrashDrive, 25 }, { M2, destDegradeDropout, 40 } }),

        make ("Faded Memory", "Lo-Fi",
              { { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 6 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass12) }, { "f2Cutoff", 3500 },
                { "convOn", 1 }, { "cvIr", e (I::concreteHall) }, { "cvReverse", 1 }, { "cvMix", 30 },
                { "degradeOn", 1 }, { "dgWow", 30 }, { "dgFlutter", 15 }, { "dgAge", 70 }, { "dgNoise", 10 }, { "dgDropout", 10 } },
              { { M1, destF2Cutoff, -30 }, { M2, destDegradeAge, 30 } }),

        make ("Crushed Chip", "Lo-Fi",
              { { "tr1AlgoA", e (A::bitcrush) }, { "tr1AlgoB", e (A::bitFlip) }, { "tr1Morph", 20 }, { "tr1Drive", 36 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass12) }, { "f2Cutoff", 5000 },
                { "dynOn", 1 }, { "dyThresh", -20 }, { "dyRatio", 4 } },
              { { M1, destTrashMorph, 60 }, { M2, destF2Cutoff, 30 } }),

        //==============================================================================================
        // Motion: phasers, flangers, rotors, shifters
        make ("Liquid Phaser", "Motion",
              { { "tr1AlgoA", e (A::warm) }, { "tr1Drive", 10 },
                { "motionOn", 1 }, { "moMode", e (M::phaser) }, { "moRate", 0.15f }, { "moDepth", 80 }, { "moFeedback", 60 }, { "moFreq", 800 }, { "moMix", 50 } },
              { { M1, destMotionFeedback, 35 }, { M2, destMotionRate, 30 } }),

        make ("Jet Engine", "Motion",
              { { "tr1AlgoA", e (A::fuzz) }, { "tr1Drive", 22 },
                { "motionOn", 1 }, { "moMode", e (M::flanger) }, { "moRate", 0.08f }, { "moDepth", 100 }, { "moFeedback", -70 }, { "moMix", 50 } },
              { { M1, destMotionRate, 30 }, { M2, destMotionFeedback, -30 } }),

        make ("Rotary Grit", "Motion",
              { { "tr1AlgoA", e (A::tube) }, { "tr1Drive", 16 },
                { "motionOn", 1 }, { "moMode", e (M::tremolo) }, { "moRate", 1.2f }, { "moDepth", 35 }, { "moFeedback", 0 }, { "moSpread", 100 }, { "moMix", 100 } },
              { { M1, destMotionRate, 25 }, { M2, destTrashDrive, 25 } }), // Macro 1: slow to fast rotor

        make ("Seasick Chorus", "Motion",
              { { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 8 },
                { "motionOn", 1 }, { "moMode", e (M::chorus) }, { "moRate", 1.8f }, { "moDepth", 100 }, { "moFeedback", 40 }, { "moSpread", 70 }, { "moMix", 60 },
                { "degradeOn", 1 }, { "dgWow", 30 }, { "dgFlutter", 0 }, { "dgAge", 0 }, { "dgNoise", 0 } },
              { { M1, destDegradeWow, 40 }, { M2, destMotionMix, 40 } }),

        make ("Upward Spiral", "Motion",
              { { "tr1AlgoA", e (A::warm) }, { "tr1Drive", 8 },
                { "motionOn", 1 }, { "moMode", e (M::freqShift) }, { "moFreq", 22 }, { "moDepth", 0 }, { "moFeedback", 80 }, { "moSpread", 0 }, { "moMix", 60 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/4") }, { "ecFeedback", 50 }, { "ecMix", 25 } },
              { { M1, destMotionFreq, 25 }, { M2, destEchoFeedback, 30 } }),

        make ("Metal Ring", "Motion",
              { { "motionOn", 1 }, { "moMode", e (M::ringMod) }, { "moFreq", 700 }, { "moDepth", 30 }, { "moRate", 0.3f }, { "moMix", 40 },
                { "tr1AlgoA", e (A::hardClip) }, { "tr1Drive", 18 },
                { "convOn", 1 }, { "cvIr", e (I::metalBowl) }, { "cvMix", 25 } },
              { { M1, destMotionFreq, 20 }, { M2, destConvMix, 40 } },
              { motion, trash }), // ring modulator into the clipper

        make ("Vibrato Tape", "Motion",
              { { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 10 },
                { "motionOn", 1 }, { "moMode", e (M::vibrato) }, { "moRate", 4.5f }, { "moDepth", 30 }, { "moMix", 100 },
                { "degradeOn", 1 }, { "dgWow", 0 }, { "dgFlutter", 20 }, { "dgAge", 20 }, { "dgNoise", 0 } },
              { { M1, destMotionDepth, 40 }, { M2, destMotionRate, -20 } }),

        make ("Phase Fuzz", "Motion",
              { { "motionOn", 1 }, { "moMode", e (M::phaser) }, { "moFreq", 400 }, { "moDepth", 90 }, { "moRate", 0.4f }, { "moFeedback", 70 }, { "moMix", 50 },
                { "tr1AlgoA", e (A::fuzz) }, { "tr1Drive", 28 } },
              { { M1, destTrashDrive, 20 }, { M2, destMotionRate, 30 } },
              { motion, trash }), // the phaser sweeps what the fuzz sees

        make ("Stereo Smear", "Motion",
              { { "f1On", 1 }, { "f1Type", e (F::highPass12) }, { "f1Cutoff", 100 },
                { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 6 },
                { "motionOn", 1 }, { "moMode", e (M::chorus) }, { "moRate", 0.25f }, { "moDepth", 50 }, { "moSpread", 100 }, { "moMix", 50 },
                { "echoOn", 1 }, { "ecTime", 25 }, { "ecFeedback", 20 }, { "ecTone", 0 }, { "ecWobble", 30 }, { "ecPingPong", 1 }, { "ecMix", 25 } },
              { { M1, destMotionMix, 40 }, { M2, destEchoMix, 30 } }),

        make ("Wah Drive", "Motion",
              { { "tr1AlgoA", e (A::fuzz) }, { "tr1Drive", 24 },
                { "f2On", 1 }, { "f2Type", e (F::bandPass12) }, { "f2Cutoff", 200 }, { "f2Reso", 70 }, { "f2Mix", 90 },
                { "envAttack", 3 }, { "envRelease", 120 } },
              { { Env, destF2Cutoff, 40 }, { M1, destF2Reso, 20 }, { M2, destTrashDrive, 20 } }),

        //==============================================================================================
        // Rhythm: everything locked to the song
        make ("Sixteenth Chop", "Rhythm",
              { { "motionOn", 1 }, { "moMode", e (M::tremolo) }, { "moSync", 1 }, { "moDiv", division ("1/16") },
                { "moDepth", 100 }, { "moFeedback", 100 }, { "moSpread", 0 }, { "moMix", 100 },
                { "tr1AlgoA", e (A::fuzz) }, { "tr1Drive", 18 } },
              { { M1, destTrashDrive, 25 }, { M2, destMotionDepth, -60 } },
              { motion, trash }), // chopped before the fuzz: gated fuzz edges

        make ("Dotted Pump", "Rhythm",
              { { "tr1AlgoA", e (A::warm) }, { "tr1Drive", 10 },
                { "motionOn", 1 }, { "moMode", e (M::tremolo) }, { "moSync", 1 }, { "moDiv", division ("1/8D") }, { "moDepth", 70 }, { "moFeedback", 30 }, { "moMix", 100 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/8D") }, { "ecFeedback", 40 }, { "ecMix", 20 } },
              { { M1, destMotionDepth, 30 }, { M2, destEchoMix, 30 } }),

        make ("Random Gates", "Rhythm",
              { { "tr1AlgoA", e (A::bitcrush) }, { "tr1Drive", 16 }, { "tr1Mix", 60 },
                { "motionOn", 1 }, { "moMode", e (M::tremolo) }, { "moSync", 1 }, { "moDiv", division ("1/16") }, { "moDepth", 50 }, { "moFeedback", 100 }, { "moMix", 100 },
                { "lfo2Shape", e (S::sampleHold) }, { "lfo2Sync", 1 }, { "lfo2Div", division ("1/16") } },
              { { L2, destMotionDepth, 50 }, { M1, destTrashMix, 40 }, { M2, destTrashDrive, 20 } }),

        make ("Stepped Filter", "Rhythm",
              { { "tr1AlgoA", e (A::diode) }, { "tr1Drive", 18 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass24) }, { "f2Cutoff", 1500 }, { "f2Reso", 60 },
                { "lfo2Shape", e (S::sampleHold) }, { "lfo2Sync", 1 }, { "lfo2Div", division ("1/8") } },
              { { L2, destF2Cutoff, 30 }, { M1, destF2Reso, 25 }, { M2, destF2Cutoff, 20 } }),

        make ("Triplet Bounce", "Rhythm",
              { { "tr1AlgoA", e (A::tube) }, { "tr1Drive", 12 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/8T") }, { "ecFeedback", 55 }, { "ecDrive", 40 }, { "ecPingPong", 1 }, { "ecMix", 30 } },
              { { M1, destEchoFeedback, 40 }, { M2, destEchoDrive, 40 } }),

        make ("Quarter Pump Filter", "Rhythm",
              { { "tr1AlgoA", e (A::warm) }, { "tr1Drive", 12 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass24) }, { "f2Cutoff", 800 }, { "f2Reso", 40 },
                { "lfo1Shape", e (S::rampUp) }, { "lfo1Sync", 1 }, { "lfo1Div", division ("1/4") } },
              { { L1, destF2Cutoff, 35 }, { M1, destF2Cutoff, 30 }, { M2, destTrashDrive, 20 } }),

        make ("Stutter 32", "Rhythm",
              { { "tr1AlgoA", e (A::bitFlip) }, { "tr1Drive", 12 }, { "tr1Mix", 50 },
                { "degradeOn", 1 }, { "dgGlitch", 60 }, { "dgWow", 0 }, { "dgFlutter", 0 }, { "dgAge", 10 }, { "dgNoise", 0 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/32") }, { "ecFeedback", 60 }, { "ecMix", 25 } },
              { { M1, destDegradeGlitch, 40 }, { M2, destEchoMix, 30 } }),

        make ("Half-Bar Crush", "Rhythm",
              { { "tr1AlgoA", e (A::hardClip) }, { "tr1Drive", 10 },
                { "lfo1Shape", e (S::square) }, { "lfo1Sync", 1 }, { "lfo1Div", division ("1/2") } },
              { { L1, destTrashDrive, 30 }, { M1, destTrashDrive, 20 } }),

        make ("Ring Steps", "Rhythm",
              { { "tr1AlgoA", e (A::warm) }, { "tr1Drive", 10 },
                { "motionOn", 1 }, { "moMode", e (M::ringMod) }, { "moFreq", 200 }, { "moDepth", 0 }, { "moMix", 50 },
                { "lfo2Shape", e (S::sampleHold) }, { "lfo2Sync", 1 }, { "lfo2Div", division ("1/8") } },
              { { L2, destMotionFreq, 30 }, { M1, destMotionMix, 40 }, { M2, destMotionFreq, 20 } }),

        make ("Pan Stutter", "Rhythm",
              { { "tr1AlgoA", e (A::tube) }, { "tr1Drive", 10 },
                { "motionOn", 1 }, { "moMode", e (M::tremolo) }, { "moSync", 1 }, { "moDiv", division ("1/16") }, { "moDepth", 90 }, { "moFeedback", 80 }, { "moSpread", 100 }, { "moMix", 100 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/16D") }, { "ecFeedback", 30 }, { "ecPingPong", 1 }, { "ecMix", 20 } },
              { { M1, destEchoMix, 30 }, { M2, destTrashDrive, 20 } }),

        //==============================================================================================
        // Space: springs, objects, rooms, often in odd places in the chain
        make ("Surf Spring", "Space",
              { { "f1On", 1 }, { "f1Type", e (F::highPass12) }, { "f1Cutoff", 80 },
                { "tr1AlgoA", e (A::fuzz) }, { "tr1Drive", 20 },
                { "convOn", 1 }, { "cvIr", e (I::springTank) }, { "cvMix", 50 }, { "cvDamp", 20 },
                { "motionOn", 1 }, { "moMode", e (M::tremolo) }, { "moRate", 6 }, { "moDepth", 40 }, { "moFeedback", 0 }, { "moMix", 100 } },
              { { M1, destConvMix, 40 }, { M2, destMotionDepth, 40 } }),

        make ("Distant Hall", "Space",
              { { "f1On", 1 }, { "f1Type", e (F::highPass12) }, { "f1Cutoff", 150 },
                { "tr1AlgoA", e (A::tube) }, { "tr1Drive", 16 },
                { "convOn", 1 }, { "cvIr", e (I::concreteHall) }, { "cvMix", 50 }, { "cvSize", 150 }, { "cvDamp", 40 } },
              { { M1, destConvMix, 40 }, { M2, destTrashDrive, 20 } }),

        make ("Bowl Resonance", "Space",
              { { "tr1AlgoA", e (A::cheby3) }, { "tr1AlgoB", e (A::cheby5) }, { "tr1Drive", 8 },
                { "convOn", 1 }, { "cvIr", e (I::metalBowl) }, { "cvMix", 60 }, { "cvSize", 80 } },
              { { M1, destConvMix, 30 }, { M2, destTrashMorph, 60 } }),

        make ("Pipe Dream", "Space",
              { { "tr1AlgoA", e (A::warm) }, { "tr1Drive", 8 },
                { "convOn", 1 }, { "cvIr", e (I::steelPipe) }, { "cvMix", 50 },
                { "motionOn", 1 }, { "moMode", e (M::flanger) }, { "moRate", 0.1f }, { "moDepth", 70 }, { "moFeedback", 50 }, { "moMix", 40 } },
              { { M1, destMotionFeedback, 30 }, { M2, destConvMix, 40 } }),

        make ("Glass Jar Bloom", "Space",
              { { "tr1AlgoA", e (A::cheby2) }, { "tr1Drive", 10 }, { "tr1Mix", 40 },
                { "convOn", 1 }, { "cvIr", e (I::glassJar) }, { "cvMix", 50 }, { "cvSize", 120 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/4") }, { "ecFeedback", 40 }, { "ecTone", 20 }, { "ecMix", 20 } },
              { { M1, destEchoFeedback, 30 }, { M2, destConvMix, 40 } }),

        make ("Tunnel Vision", "Space",
              { { "tr1AlgoA", e (A::fuzz) }, { "tr1Drive", 22 },
                { "convOn", 1 }, { "cvIr", e (I::tunnel) }, { "cvMix", 60 }, { "cvDamp", 30 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass24) }, { "f2Cutoff", 2000 }, { "f2Reso", 30 } },
              { { M1, destF2Cutoff, 35 }, { M2, destConvMix, 30 } },
              { f1, trash, conv, f2 }), // the filter closes over the tunnel

        make ("Reverse Cathedral", "Space",
              { { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 10 },
                { "convOn", 1 }, { "cvIr", e (I::concreteHall) }, { "cvReverse", 1 }, { "cvMix", 50 }, { "cvSize", 200 }, { "cvDamp", 20 },
                { "degradeOn", 1 }, { "dgWow", 10 }, { "dgFlutter", 0 }, { "dgAge", 10 }, { "dgNoise", 0 } },
              { { M1, destConvMix, 40 }, { M2, destDegradeWow, 30 } }),

        make ("Plate Into Fuzz", "Space",
              { { "convOn", 1 }, { "cvIr", e (I::steelPlate) }, { "cvMix", 70 },
                { "tr1AlgoA", e (A::fuzz) }, { "tr1Drive", 30 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass24) }, { "f2Cutoff", 5000 } },
              { { M1, destConvMix, 30 }, { M2, destF2Cutoff, 25 } },
              { conv, trash, f2 }), // the reverb tail gets fuzzed

        make ("Tiled Slapback", "Space",
              { { "tr1AlgoA", e (A::valve) }, { "tr1Drive", 14 },
                { "convOn", 1 }, { "cvIr", e (I::tiledRoom) }, { "cvMix", 30 },
                { "echoOn", 1 }, { "ecTime", 120 }, { "ecFeedback", 15 }, { "ecDrive", 50 }, { "ecWobble", 30 }, { "ecTone", -20 }, { "ecMix", 25 } },
              { { M1, destEchoMix, 30 }, { M2, destEchoFeedback, 40 } }),

        make ("Cistern Echoes", "Space",
              { { "tr1AlgoA", e (A::warm) }, { "tr1Drive", 6 },
                { "convOn", 1 }, { "cvIr", e (I::cistern) }, { "cvMix", 40 }, { "cvDamp", 50 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/2") }, { "ecFeedback", 55 }, { "ecTone", -50 }, { "ecMix", 25 } },
              { { M1, destConvMix, 40 }, { M2, destEchoFeedback, 30 } }),

        //==============================================================================================
        // Texture: fog, dust, rust and loops
        make ("Granular Fog", "Texture",
              { { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 8 },
                { "degradeOn", 1 }, { "dgGlitch", 30 }, { "dgWow", 20 }, { "dgFlutter", 30 }, { "dgAge", 40 }, { "dgNoise", 15 },
                { "convOn", 1 }, { "cvIr", e (I::concreteHall) }, { "cvMix", 50 }, { "cvSize", 150 },
                { "motionOn", 1 }, { "moMode", e (M::chorus) }, { "moDepth", 60 }, { "moMix", 40 } },
              { { M1, destDegradeGlitch, 50 }, { M2, destConvMix, 40 } },
              { f1, trash, f2, degrade, conv }), // glitches first, then the fog

        make ("Rust Bloom", "Texture",
              { { "echoOn", 1 }, { "ecTime", 600 }, { "ecFeedback", 50 }, { "ecWobble", 50 }, { "ecMix", 20 },
                { "convOn", 1 }, { "cvIr", e (I::springTank) }, { "cvMix", 40 },
                { "tr1AlgoA", e (A::sputter) }, { "tr1Drive", 24 }, { "tr1Bias", 25 }, { "tr1Mix", 60 } },
              { { M1, destTrashBias, 40 }, { M2, destEchoFeedback, 30 } },
              { f1, echo, conv, trash }), // repeats and spring sputter as they decay

        make ("Static Choir", "Texture",
              { { "tr1AlgoA", e (A::muLaw) }, { "tr1Drive", 14 },
                { "f2On", 1 }, { "f2Type", e (F::vowel) }, { "f2Cutoff", 632 }, { "f2Reso", 70 }, { "f2Mix", 70 },
                { "convOn", 1 }, { "cvIr", e (I::concreteHall) }, { "cvMix", 40 },
                { "degradeOn", 1 }, { "dgWow", 10 }, { "dgFlutter", 0 }, { "dgAge", 20 }, { "dgNoise", 25 },
                { "lfo2Shape", e (S::smoothRandom) }, { "lfo2Rate", 0.3f } },
              { { L2, destF2Cutoff, 15 }, { M1, destF2Cutoff, 30 }, { M2, destDegradeNoise, 30 } }),

        make ("Paper Tape Loop", "Texture",
              { { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 10 },
                { "degradeOn", 1 }, { "dgWow", 30 }, { "dgFlutter", 20 }, { "dgAge", 50 }, { "dgNoise", 10 },
                { "echoOn", 1 }, { "ecTime", 800 }, { "ecFeedback", 85 }, { "ecWobble", 70 }, { "ecDrive", 60 }, { "ecTone", -40 }, { "ecMix", 40 } },
              { { M1, destEchoFeedback, 25 }, { M2, destDegradeAge, 30 } }),

        make ("Blown Cone Crackle", "Texture",
              { { "tr1AlgoA", e (A::sputter) }, { "tr1Drive", 30 }, { "tr1Bias", 40 },
                { "convOn", 1 }, { "cvIr", e (I::tornCone) }, { "cvMix", 100 },
                { "degradeOn", 1 }, { "dgWow", 0 }, { "dgFlutter", 0 }, { "dgAge", 20 }, { "dgNoise", 5 }, { "dgCrackle", 15 }, { "dgDropout", 20 },
                { "lfo2Shape", e (S::smoothRandom) }, { "lfo2Rate", 1.5f } },
              { { L2, destTrashBias, 30 }, { M1, destTrashBias, 40 }, { M2, destDegradeDropout, 40 } }),

        make ("Frozen Shimmer", "Texture",
              { { "tr1AlgoA", e (A::warm) }, { "tr1Drive", 6 },
                { "motionOn", 1 }, { "moMode", e (M::freqShift) }, { "moFreq", 24 }, { "moDepth", 0 }, { "moFeedback", 60 }, { "moSpread", 0 }, { "moMix", 50 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/4") }, { "ecFeedback", 70 }, { "ecTone", 30 }, { "ecMix", 35 },
                { "convOn", 1 }, { "cvIr", e (I::steelPlate) }, { "cvMix", 30 } },
              { { M1, destEchoFeedback, 30 }, { M2, destMotionFreq, 20 } },
              { f1, trash, f2, motion, echo, conv }), // shifted, repeated, then smeared

        make ("Dust Storm", "Texture",
              { { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 6 },
                { "degradeOn", 1 }, { "dgWow", 5 }, { "dgFlutter", 5 }, { "dgAge", 50 }, { "dgNoise", 40 }, { "dgCrackle", 60 }, { "dgDropout", 20 },
                { "f2On", 1 }, { "f2Type", e (F::bandPass12) }, { "f2Cutoff", 2500 }, { "f2Mix", 50 },
                { "lfo2Shape", e (S::smoothRandom) }, { "lfo2Rate", 0.4f } },
              { { L2, destF2Cutoff, 30 }, { M1, destDegradeCrackle, 40 }, { M2, destF2Mix, 50 } },
              { f1, trash, degrade, f2 }), // the dust is filtered with the music

        make ("Comb Metallic", "Texture",
              { { "f1On", 1 }, { "f1Type", e (F::combPlus) }, { "f1Cutoff", 220 }, { "f1Reso", 80 },
                { "tr1AlgoA", e (A::hardClip) }, { "tr1Drive", 16 },
                { "lfo1Shape", e (S::sine) }, { "lfo1Rate", 0.07f } },
              { { L1, destF1Cutoff, 12 }, { M1, destF1Reso, 15 }, { M2, destF1Cutoff, 20 } }),

        make ("Underwater Radio", "Texture",
              { { "tr1AlgoA", e (A::warm) }, { "tr1Drive", 8 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass24) }, { "f2Cutoff", 600 }, { "f2Reso", 45 },
                { "convOn", 1 }, { "cvIr", e (I::radio) }, { "cvMix", 60 },
                { "motionOn", 1 }, { "moMode", e (M::vibrato) }, { "moRate", 0.8f }, { "moDepth", 60 }, { "moMix", 100 },
                { "degradeOn", 1 }, { "dgWow", 30 }, { "dgFlutter", 0 }, { "dgAge", 30 }, { "dgNoise", 5 } },
              { { M1, destF2Cutoff, 35 }, { M2, destMotionDepth, 40 } }),

        make ("Warm Blanket", "Texture",
              { { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 8 },
                { "f2On", 1 }, { "f2Type", e (F::lowPass12) }, { "f2Cutoff", 3000 }, { "f2Reso", 10 },
                { "convOn", 1 }, { "cvIr", e (I::closet) }, { "cvMix", 30 },
                { "degradeOn", 1 }, { "dgWow", 5 }, { "dgFlutter", 0 }, { "dgAge", 30 }, { "dgNoise", 5 },
                { "dynOn", 1 }, { "dyThresh", -26 }, { "dyRatio", 3 }, { "dyAttack", 20 }, { "dyRelease", 200 } },
              { { M1, destF2Cutoff, 30 }, { M2, destDegradeAge, 40 } }),

        //==============================================================================================
        // Chaos: everything at once, and things that shouldn't be connected
        make ("Everything Breaks", "Chaos",
              { { "f1On", 1 }, { "f1Type", e (F::highPass12) }, { "f1Cutoff", 60 },
                { "tr1AlgoA", e (A::fuzz) }, { "tr1Drive", 34 },
                { "f2On", 1 }, { "f2Type", e (F::bandPass12) }, { "f2Cutoff", 1500 }, { "f2Reso", 50 }, { "f2Mix", 50 },
                { "convOn", 1 }, { "cvIr", e (I::tornCone) }, { "cvMix", 50 },
                { "motionOn", 1 }, { "moMode", e (M::ringMod) }, { "moFreq", 150 }, { "moDepth", 50 }, { "moRate", 3 }, { "moMix", 30 },
                { "degradeOn", 1 }, { "dgGlitch", 40 }, { "dgDropout", 30 }, { "dgWow", 20 }, { "dgFlutter", 20 }, { "dgAge", 40 }, { "dgNoise", 10 },
                { "dynOn", 1 }, { "dyThresh", -30 }, { "dyRatio", 8 },
                { "echoOn", 1 }, { "ecTime", 300 }, { "ecFeedback", 95 }, { "ecDrive", 70 }, { "ecMix", 25 },
                { "lfo2Shape", e (S::sampleHold) }, { "lfo2Sync", 1 }, { "lfo2Div", division ("1/16") } },
              { { L2, destF2Cutoff, 35 }, { M1, destDegradeGlitch, 50 }, { M2, destEchoMix, 25 } }),

        make ("Bit Flip Storm", "Chaos",
              { { "tr1AlgoA", e (A::bitFlip) }, { "tr1AlgoB", e (A::decimate) }, { "tr1Morph", 50 }, { "tr1Drive", 30 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/32") }, { "ecFeedback", 60 }, { "ecMix", 25 },
                { "lfo2Shape", e (S::sampleHold) }, { "lfo2Sync", 1 }, { "lfo2Div", division ("1/32") } },
              { { L2, destTrashMorph, 50 }, { M1, destTrashDrive, 20 }, { M2, destEchoFeedback, 30 } }),

        make ("Feedback Scream", "Chaos",
              { { "tr1AlgoA", e (A::hardClip) }, { "tr1Drive", 20 },
                { "echoOn", 1 }, { "ecTime", 60 }, { "ecFeedback", 112 }, { "ecDrive", 80 }, { "ecTone", 30 }, { "ecWobble", 10 }, { "ecMix", 35 },
                { "f2On", 1 }, { "f2Type", e (F::bandPass12) }, { "f2Cutoff", 1500 }, { "f2Reso", 70 }, { "f2Mix", 70 },
                { "lfo1Shape", e (S::smoothRandom) }, { "lfo1Rate", 0.3f } },
              { { L1, destEchoTime, 10 }, { M1, destF2Cutoff, 30 }, { M2, destEchoMix, 30 } },
              { f1, trash, echo, f2 }), // the runaway loop through a screaming band-pass

        make ("Fold Mangler", "Chaos",
              { { "tr1AlgoA", e (A::westCoast) }, { "tr1AlgoB", e (A::sineFold) }, { "tr1Morph", 50 }, { "tr1Drive", 38 }, { "tr1Bias", 30 },
                { "lfo1Shape", e (S::triangle) }, { "lfo1Rate", 3 },
                { "lfo2Shape", e (S::sampleHold) }, { "lfo2Sync", 1 }, { "lfo2Div", division ("1/16") },
                { "outGain", -3.5f } },
              { { L1, destTrashMorph, 50 }, { L2, destTrashBias, 40 }, { M1, destTrashDrive, 10 }, { M2, destTrashTone, -40 } }),

        make ("Random Ring Chaos", "Chaos",
              { { "tr1AlgoA", e (A::buzz) }, { "tr1Drive", 20 },
                { "motionOn", 1 }, { "moMode", e (M::ringMod) }, { "moFreq", 300 }, { "moDepth", 80 }, { "moRate", 7 }, { "moMix", 60 },
                { "lfo2Shape", e (S::sampleHold) }, { "lfo2Sync", 1 }, { "lfo2Div", division ("1/16") } },
              { { L2, destMotionFreq, 30 }, { M1, destMotionMix, 40 }, { M2, destTrashDrive, 20 } }),

        make ("Collapse", "Chaos",
              { { "tr1AlgoA", e (A::fuzz) }, { "tr1Drive", 36 },
                { "dynOn", 1 }, { "dyThresh", -45 }, { "dyRatio", 20 }, { "dyAttack", 0.1f }, { "dyRelease", 400 }, { "dyMakeup", 18 },
                { "degradeOn", 1 }, { "dgDropout", 40 }, { "dgWow", 0 }, { "dgFlutter", 0 }, { "dgAge", 20 }, { "dgNoise", 0 },
                { "outGain", -3 } },
              { { M1, destDynThreshold, -25 }, { M2, destDegradeDropout, 40 } }),

        make ("Meltdown", "Chaos",
              { { "tr1AlgoA", e (A::tape) }, { "tr1Drive", 20 },
                { "degradeOn", 1 }, { "dgWow", 80 }, { "dgFlutter", 60 }, { "dgAge", 70 }, { "dgNoise", 10 },
                { "echoOn", 1 }, { "ecTime", 350 }, { "ecFeedback", 100 }, { "ecWobble", 100 }, { "ecMix", 30 } },
              { { M1, destDegradeWow, 20 }, { M2, destEchoFeedback, 15 } }),

        make ("Sputter Machine", "Chaos",
              { { "tr1AlgoA", e (A::sputter) }, { "tr1Drive", 40 }, { "tr1Bias", 60 },
                { "dynOn", 1 }, { "dyGate", -30 }, { "dyRatio", 1 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/16") }, { "ecFeedback", 40 }, { "ecMix", 25 } },
              { { M1, destTrashBias, 30 }, { M2, destEchoMix, 30 } }),

        make ("Glitch Cascade", "Chaos",
              { { "tr1AlgoA", e (A::bitcrush) }, { "tr1Drive", 14 },
                { "degradeOn", 1 }, { "dgGlitch", 90 }, { "dgWow", 0 }, { "dgFlutter", 0 }, { "dgAge", 10 }, { "dgNoise", 0 },
                { "motionOn", 1 }, { "moMode", e (M::freqShift) }, { "moFreq", 20 }, { "moDepth", 0 }, { "moFeedback", 50 }, { "moSpread", 100 }, { "moMix", 40 },
                { "echoOn", 1 }, { "ecSync", 1 }, { "ecDiv", division ("1/16T") }, { "ecFeedback", 70 }, { "ecPingPong", 1 }, { "ecMix", 30 } },
              { { M1, destMotionFreq, 30 }, { M2, destEchoFeedback, 30 } },
              { f1, trash, f2, conv, degrade, motion }), // stutters, then shifted, then echoed

        make ("Octave Implosion", "Chaos",
              { { "f1On", 1 }, { "f1Type", e (F::combMinus) }, { "f1Cutoff", 110 }, { "f1Reso", 70 },
                { "tr1AlgoA", e (A::octaveFuzz) }, { "tr1AlgoB", e (A::fullWave) }, { "tr1Morph", 30 }, { "tr1Drive", 40 },
                { "convOn", 1 }, { "cvIr", e (I::tunnel) }, { "cvMix", 30 },
                { "lfo2Shape", e (S::smoothRandom) }, { "lfo2Rate", 1.5f },
                { "outGain", 7 } },
              { { L2, destTrashMorph, 30 }, { M1, destF1Reso, 20 }, { M2, destConvMix, 40 } }),
    };

    return list;
}

} // namespace hl::presets
