#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace hl
{
namespace pid = params::id;

/** Synthesises and partitions convolution impulses so the audio thread never has to. */
class HollowAudioProcessor::ImpulseWorker final : public juce::Thread
{
public:
    explicit ImpulseWorker (dsp::ConvolveModule& m) : Thread ("Hollow impulse builder"), module (m) {}

    void run() override
    {
        while (! threadShouldExit())
            if (! module.service())
                wait (15);
    }

private:
    dsp::ConvolveModule& module;
};

HollowAudioProcessor::HollowAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "HollowState", params::createLayout()),
      binding (state),
      dice ((juce::int64) juce::Time::currentTimeMillis())
{
    storeOrderInState (dsp::defaultModuleOrder());
    chain.setSettings (binding.read());

    // The latency is the Trash oversampler's and depends neither on the sample rate nor on the chosen
    // factor (lower factors are padded): report it up front
    dsp::VariableOversampler oversampler;
    oversampler.prepare();
    setLatencySamples (oversampler.getLatency());
    worker = std::make_unique<ImpulseWorker> (chain.getConvolve());
    worker->startThread (juce::Thread::Priority::low);

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            state.addParameterListener (rp->paramID, this);

    addListener (this);
    history = capture();
}

HollowAudioProcessor::~HollowAudioProcessor()
{
    cancelPendingUpdate();
    removeListener (this);

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            state.removeParameterListener (rp->paramID, this);

    worker->stopThread (4000);
}

bool HollowAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void HollowAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxChunk = juce::jmax (64, samplesPerBlock);

    auto settings = binding.read (isNonRealtime());
    settings.order = getModuleOrder();
    chain.setSettings (settings);
    chain.prepare (sampleRate, maxChunk);

    scratchRight.assign ((size_t) maxChunk, 0.0f);
    mono.assign ((size_t) maxChunk, 0.0f);

    for (int ch = 0; ch < 2; ++ch)
    {
        dry[(size_t) ch].assign ((size_t) maxChunk, 0.0f);
        dryDelay[(size_t) ch].prepare (chain.getLatency());
        dryDelay[(size_t) ch].setDelay (chain.getLatency());
    }

    bypassMix.reset (sampleRate, 0.03);
    bypassMix.setCurrentAndTarget (binding.isBypassed() ? 0.0f : 1.0f);

    setLatencySamples (chain.getLatency());
}

void HollowAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numInputs = getTotalNumInputChannels();

    for (int ch = numInputs; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (numInputs == 0)
        return;

    auto settings = binding.read (isNonRealtime());
    settings.order = unpackOrder (packedOrder.load());
    chain.setSettings (settings);
    bypassMix.setTarget (binding.isBypassed() ? 0.0f : 1.0f);

    // Only when a setting changed (at most every ~20 ms while something is being automated)
    samplesSinceEstimate = juce::jmin (samplesSinceEstimate + buffer.getNumSamples(), 1 << 30);

    if (samplesSinceEstimate >= (int) (0.02 * getSampleRate()) && levelEstimateDirty.exchange (false))
    {
        chain.updateLevelEstimate();
        samplesSinceEstimate = 0;
    }

    dsp::TransportInfo transport;

    if (auto* playHead = getPlayHead())
    {
        if (auto pos = playHead->getPosition())
        {
            if (auto bpm = pos->getBpm())
                transport.bpm = juce::jlimit (20.0, 400.0, *bpm);

            if (auto ppq = pos->getPpqPosition())
            {
                transport.ppq = *ppq;
                transport.playing = pos->getIsPlaying();
            }
        }
    }

    meters.bpm = (float) transport.bpm;

    float* left = buffer.getWritePointer (0);
    float* right = numInputs > 1 ? buffer.getWritePointer (1) : nullptr;
    const double beatsPerSample = transport.bpm / 60.0 / juce::jmax (1.0, getSampleRate());

    for (int start = 0; start < buffer.getNumSamples(); start += maxChunk)
    {
        const int n = juce::jmin (maxChunk, buffer.getNumSamples() - start);
        auto t = transport;
        t.ppq += start * beatsPerSample;

        if (right != nullptr)
        {
            processChunk (left + start, right + start, n, t);
        }
        else
        {
            std::copy_n (left + start, n, scratchRight.data());
            processChunk (left + start, scratchRight.data(), n, t);

            for (int i = 0; i < n; ++i)
                left[start + i] = 0.5f * (left[start + i] + scratchRight[(size_t) i]);
        }
    }

    publishMeters();
}

void HollowAudioProcessor::processChunk (float* left, float* right, int n, const dsp::TransportInfo& transport) noexcept
{
    float* chans[2] = { left, right };

    for (int ch = 0; ch < 2; ++ch)
    {
        float pk = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            dry[(size_t) ch][(size_t) i] = dryDelay[(size_t) ch].process (chans[ch][i]);
            pk = std::max (pk, std::abs (chans[ch][i]));
        }

        detail::atomicMax (meters.inputPeak[(size_t) ch], pk);
    }

    chain.process (left, right, n, transport);

    // Global bypass crossfade against the latency-aligned dry signal
    if (bypassMix.isSmoothing() || bypassMix.getCurrent() < 1.0f)
    {
        for (int i = 0; i < n; ++i)
        {
            const float m = bypassMix.next();

            for (int ch = 0; ch < 2; ++ch)
            {
                const float d = dry[(size_t) ch][(size_t) i];
                chans[ch][i] = d + m * (chans[ch][i] - d);
            }
        }
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        float pk = 0.0f;

        for (int i = 0; i < n; ++i)
            pk = std::max (pk, std::abs (chans[ch][i]));

        detail::atomicMax (meters.outputPeak[(size_t) ch], pk);
    }

    for (int i = 0; i < n; ++i)
        mono[(size_t) i] = 0.5f * (left[i] + right[i]);

    const float* monoOut[] = { mono.data() };
    spectrumFifo.push (monoOut, n);

    const float* stereo[] = { left, right };
    scopeFifo.push (stereo, n);
}

void HollowAudioProcessor::publishMeters() noexcept
{
    const auto peaks = chain.getTrash().takePeaks();

    for (int b = 0; b < dsp::kTrashBands; ++b)
        detail::atomicMax (meters.trashPeaks[(size_t) b], peaks[(size_t) b]);

    detail::atomicMin (meters.dynReduction, chain.getDynamics().takeMaxReduction());
    detail::atomicMax (meters.dynInput, chain.getDynamics().takeInputPeak());
    meters.gateOpen = chain.getDynamics().isGateOpen();
    meters.degradePitch = chain.getDegrade().getPitchCents();
    meters.dropoutGain = chain.getDegrade().getDropoutGain();
    meters.glitching = chain.getDegrade().isGlitching();

    const auto& mod = chain.getModulation();

    for (int d = 0; d < dsp::numDestinations; ++d)
        meters.modOffsets[(size_t) d].store (mod.offsets[(size_t) d], std::memory_order_relaxed);

    for (int l = 0; l < 2; ++l)
    {
        meters.lfoValues[(size_t) l] = mod.lfoValues[(size_t) l];
        meters.lfoPhases[(size_t) l] = (float) mod.lfoPhases[(size_t) l];
    }

    meters.envelope = mod.envelope;
    meters.motionPhase = mod.motionPhase;
    meters.autoLevelDb = chain.getAutoLevelDb();
}

bool HollowAudioProcessor::waitForImpulse (int timeoutMs)
{
    const auto start = juce::Time::getMillisecondCounter();

    while (! chain.getConvolve().isSettled())
    {
        if ((int) (juce::Time::getMillisecondCounter() - start) > timeoutMs)
            return false;

        // An empty block lets the audio side collect the pending kernel
        juce::AudioBuffer<float> silence (2, 32);
        silence.clear();
        juce::MidiBuffer midi;
        processBlock (silence, midi);
        juce::Thread::sleep (5);
    }

    return true;
}

//==============================================================================
float HollowAudioProcessor::getModulationOffset (int destination) const
{
    if (destination <= dsp::destNone || destination >= dsp::numDestinations)
        return 0.0f;

    return meters.modOffsets[(size_t) destination].load (std::memory_order_relaxed);
}

bool HollowAudioProcessor::isModulated (int destination) const
{
    if (destination <= dsp::destNone)
        return false;

    for (int s = 0; s < dsp::kModSlots; ++s)
    {
        const int src = (int) state.getRawParameterValue (pid::slot (s, "Src"))->load();
        const int dst = (int) state.getRawParameterValue (pid::slot (s, "Dst"))->load();
        const float amt = state.getRawParameterValue (pid::slot (s, "Amt"))->load();

        if (src != dsp::sourceNone && dst == destination && std::abs (amt) > 0.01f)
            return true;
    }

    return false;
}

void HollowAudioProcessor::addModulation (int destination, int source)
{
    for (int s = 0; s < dsp::kModSlots; ++s)
    {
        const int src = (int) state.getRawParameterValue (pid::slot (s, "Src"))->load();
        const int dst = (int) state.getRawParameterValue (pid::slot (s, "Dst"))->load();

        if (src == dsp::sourceNone || dst == dsp::destNone)
        {
            setPlain (pid::slot (s, "Src"), (float) source);
            setPlain (pid::slot (s, "Dst"), (float) destination);
            setPlain (pid::slot (s, "Amt"), source == dsp::sourceLfo1 || source == dsp::sourceLfo2 ? 25.0f : 40.0f);
            return;
        }
    }
}

void HollowAudioProcessor::clearModulation (int destination)
{
    for (int s = 0; s < dsp::kModSlots; ++s)
    {
        if ((int) state.getRawParameterValue (pid::slot (s, "Dst"))->load() == destination)
        {
            setPlain (pid::slot (s, "Src"), 0.0f);
            setPlain (pid::slot (s, "Dst"), 0.0f);
            setPlain (pid::slot (s, "Amt"), 0.0f);
        }
    }
}

//==============================================================================
void HollowAudioProcessor::setPlain (const juce::String& paramId, float plainValue)
{
    if (auto* p = state.getParameter (paramId))
    {
        const float normalised = p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (plainValue));
        p->beginChangeGesture();
        p->setValueNotifyingHost (normalised);
        p->endChangeGesture();
    }
}

bool HollowAudioProcessor::isGlobalSetting (const juce::String& paramId)
{
    return paramId == pid::bypass || paramId == pid::autoLevel || paramId == pid::clipGuard
        || paramId == pid::oversampling || paramId == pid::renderOversampling;
}

void HollowAudioProcessor::resetAllParameters()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! isGlobalSetting (rp->paramID))
            {
                rp->beginChangeGesture();
                rp->setValueNotifyingHost (rp->getDefaultValue());
                rp->endChangeGesture();
            }
}

int HollowAudioProcessor::getNumPrograms()
{
    return (int) presets::all().size();
}

void HollowAudioProcessor::setCurrentProgram (int index)
{
    if (juce::isPositiveAndBelow (index, getNumPrograms()))
        loadPreset (index);
}

const juce::String HollowAudioProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, getNumPrograms()))
        return presets::all()[(size_t) index].name;

    return {};
}

void HollowAudioProcessor::loadPreset (int index)
{
    const auto& list = presets::all();

    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;

    const auto& preset = list[(size_t) index];
    resetAllParameters();

    for (const auto& [paramId, value] : preset.values)
    {
        jassert (state.getParameter (paramId) != nullptr);
        setPlain (paramId, value);
    }

    setModuleOrder (presets::completeOrder (preset.order));
    currentPreset = index;
    getUiState().setProperty ("preset", preset.name, nullptr);
    getUiState().removeProperty ("presetFile", nullptr);
}

//==============================================================================
juce::File HollowAudioProcessor::getUserPresetFolder() const
{
    return userPresetFolder != juce::File() ? userPresetFolder : settings->getPresetFolder();
}

bool HollowAudioProcessor::isPresetFile (const juce::File& file)
{
    if (! file.existsAsFile() || ! file.hasFileExtension (presetExtension) || file.getSize() > 1024 * 1024)
        return false;

    const auto xml = juce::XmlDocument::parse (file);
    return xml != nullptr && xml->hasTagName ("HollowPreset");
}

juce::Array<juce::File> HollowAudioProcessor::importPresets (const juce::Array<juce::File>& files, juce::StringArray* problems)
{
    juce::Array<juce::File> result;
    const auto folder = getUserPresetFolder();

    if (! folder.createDirectory())
    {
        if (problems != nullptr)
            problems->add ("can't create " + folder.getFullPathName());

        return result;
    }

    for (const auto& source : files)
    {
        if (! isPresetFile (source))
        {
            if (problems != nullptr)
                problems->add (source.getFileName() + " is not a Hollow preset");

            continue;
        }

        if (source.getParentDirectory() == folder)
        {
            result.add (source); // already there
            continue;
        }

        // Already imported (under any name)? Then just point at that copy
        juce::File identical;

        for (const auto& existing : getUserPresets())
            if (existing.getSize() == source.getSize() && existing.hasIdenticalContentTo (source))
                identical = existing;

        if (identical != juce::File())
        {
            result.add (identical);
            continue;
        }

        auto target = folder.getChildFile (source.getFileName());

        for (int n = 2; target.existsAsFile(); ++n)
            target = folder.getChildFile (source.getFileNameWithoutExtension() + " (" + juce::String (n) + ")" + presetExtension);

        if (source.copyFileTo (target))
            result.add (target);
        else if (problems != nullptr)
            problems->add ("couldn't copy " + source.getFileName());
    }

    return result;
}

juce::Array<juce::File> HollowAudioProcessor::getUserPresets() const
{
    auto files = getUserPresetFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + presetExtension);

    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0;
    });

    return files;
}

juce::File HollowAudioProcessor::saveUserPreset (const juce::String& name)
{
    const auto cleanName = juce::File::createLegalFileName (name.trim());

    if (cleanName.isEmpty())
        return {};

    const auto folder = getUserPresetFolder();

    if (! folder.createDirectory())
        return {};

    juce::XmlElement xml ("HollowPreset");
    xml.setAttribute ("name", name.trim());
    xml.setAttribute ("version", 1);
    xml.setAttribute ("moduleOrder", state.state.getProperty ("moduleOrder").toString());

    for (auto* p : getParameters())
    {
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (isGlobalSetting (rp->paramID))
                continue;

            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", rp->paramID);
            e->setAttribute ("value", rp->convertFrom0to1 (rp->getValue()));
        }
    }

    const auto file = folder.getChildFile (cleanName + presetExtension);

    if (! xml.writeTo (file))
        return {};

    getUiState().setProperty ("preset", name.trim(), nullptr);
    getUiState().setProperty ("presetFile", file.getFullPathName(), nullptr);
    currentPreset = -1;
    return file;
}

bool HollowAudioProcessor::loadUserPreset (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! xml->hasTagName ("HollowPreset"))
        return false;

    resetAllParameters();

    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
    {
        const auto paramId = e->getStringAttribute ("id");

        if (state.getParameter (paramId) != nullptr && ! isGlobalSetting (paramId))
            setPlain (paramId, (float) e->getDoubleAttribute ("value"));
    }

    juce::StringArray items;
    items.addTokens (xml->getStringAttribute ("moduleOrder"), ",", {});
    dsp::ModuleOrder order = dsp::defaultModuleOrder();

    if (items.size() == dsp::numModules)
        for (int i = 0; i < dsp::numModules; ++i)
            order[(size_t) i] = items[i].getIntValue();

    setModuleOrder (dsp::isValidOrder (order) ? order : dsp::defaultModuleOrder());
    currentPreset = -1;
    getUiState().setProperty ("preset", xml->getStringAttribute ("name", file.getFileNameWithoutExtension()), nullptr);
    getUiState().setProperty ("presetFile", file.getFullPathName(), nullptr);
    return true;
}

bool HollowAudioProcessor::deleteUserPreset (const juce::File& file)
{
    if (! file.isAChildOf (getUserPresetFolder()) || ! file.hasFileExtension (presetExtension))
        return false;

    if (getCurrentUserPreset() == file)
        getUiState().removeProperty ("presetFile", nullptr);

    return file.deleteFile();
}

juce::File HollowAudioProcessor::getCurrentUserPreset() const
{
    const auto ui = state.state.getChildWithName ("UI");
    const auto path = ui.isValid() ? ui.getProperty ("presetFile").toString() : juce::String();
    return path.isNotEmpty() ? juce::File (path) : juce::File();
}

juce::String HollowAudioProcessor::getPresetName() const
{
    const auto ui = state.state.getChildWithName ("UI");
    return ui.isValid() ? ui.getProperty ("preset", "Init").toString() : juce::String ("Init");
}

void HollowAudioProcessor::randomize()
{
    auto& r = dice;
    const auto chance = [&r] (float p) { return r.nextFloat() < p; };
    const auto between = [&r] (float lo, float hi) { return lo + (hi - lo) * r.nextFloat(); };
    const auto pick = [&r] (std::initializer_list<int> items) { return *(items.begin() + r.nextInt ((int) items.size())); };

    // What the dice leaves alone (settings page)
    const bool shuffleOrder = settings->getDiceShufflesOrder();
    const bool changeModulation = settings->getDiceChangesModulation();
    const auto keptOrder = getModuleOrder();
    std::vector<std::pair<juce::String, float>> keptModulation;

    if (! changeModulation)
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (rp->paramID.startsWith ("mod") || rp->paramID.startsWith ("lfo") || rp->paramID.startsWith ("env") || rp->paramID.startsWith ("macro"))
                    keptModulation.emplace_back (rp->paramID, rp->convertFrom0to1 (rp->getValue()));

    resetAllParameters();

    // Trash is always in; 2-4 other modules join it
    std::vector<int> others;

    for (int m = 0; m < dsp::numModules; ++m)
        if (m != dsp::moduleTrash)
            others.push_back (m);

    for (size_t i = others.size() - 1; i > 0; --i)
        std::swap (others[i], others[(size_t) r.nextInt ((int) i + 1)]);

    const int extra = 2 + r.nextInt (3);
    std::array<bool, dsp::numModules> on {};
    on[dsp::moduleTrash] = true;

    for (int i = 0; i < extra; ++i)
        on[(size_t) others[(size_t) i]] = true;

    for (int m = 0; m < dsp::numModules; ++m)
        setPlain (pid::moduleOn (m), on[(size_t) m] ? 1.0f : 0.0f);

    // Trash: one to three bands of anything
    const int bands = chance (0.55f) ? 1 : (chance (0.5f) ? 2 : 3);
    setPlain (pid::trashBands, (float) (bands - 1));
    setPlain (pid::trashCrossover (0), between (120.0f, 600.0f));
    setPlain (pid::trashCrossover (1), between (1500.0f, 6000.0f));

    for (int b = 0; b < bands; ++b)
    {
        setPlain (pid::trash (b, "AlgoA"), (float) r.nextInt (dsp::kNumAlgos));
        setPlain (pid::trash (b, "AlgoB"), (float) r.nextInt (dsp::kNumAlgos));
        setPlain (pid::trash (b, "Morph"), chance (0.5f) ? 0.0f : between (0.0f, 100.0f));
        setPlain (pid::trash (b, "Drive"), between (4.0f, 34.0f));
        setPlain (pid::trash (b, "Bias"), chance (0.3f) ? between (-60.0f, 60.0f) : 0.0f);
        setPlain (pid::trash (b, "Tone"), chance (0.5f) ? 100.0f : between (40.0f, 100.0f));
        setPlain (pid::trash (b, "Mix"), between (50.0f, 100.0f));
    }

    for (int f = 0; f < 2; ++f)
    {
        const int type = f == 0 ? pick ({ 2, 3, 4, 7, 9 }) : pick ({ 0, 1, 4, 5, 6, 8, 9 });
        setPlain (pid::filter (f, "Type"), (float) type);
        setPlain (pid::filter (f, "Cutoff"), f == 0 ? between (60.0f, 900.0f) : between (500.0f, 9000.0f));
        setPlain (pid::filter (f, "Reso"), between (5.0f, 75.0f));
        setPlain (pid::filter (f, "Drive"), chance (0.4f) ? between (0.0f, 12.0f) : 0.0f);
    }

    setPlain (pid::convImpulse, (float) r.nextInt (dsp::kNumImpulses));
    setPlain (pid::convSize, between (50.0f, 160.0f));
    setPlain (pid::convDamp, between (0.0f, 80.0f));
    setPlain (pid::convReverse, chance (0.15f) ? 1.0f : 0.0f);
    setPlain (pid::convMix, between (25.0f, 90.0f));

    setPlain (pid::motionMode, (float) r.nextInt (dsp::kNumMotionModes));
    setPlain (pid::motionRate, between (0.05f, 6.0f));
    setPlain (pid::motionDepth, between (20.0f, 90.0f));
    setPlain (pid::motionFeedback, between (-60.0f, 70.0f));
    setPlain (pid::motionFreq, between (40.0f, 1500.0f));
    setPlain (pid::motionSpread, between (0.0f, 100.0f));
    setPlain (pid::motionMix, between (25.0f, 80.0f));

    for (const auto& idStr : { pid::degradeWow, pid::degradeFlutter, pid::degradeAge })
        setPlain (idStr, between (0.0f, 70.0f));

    setPlain (pid::degradeNoise, between (0.0f, 35.0f));
    setPlain (pid::degradeCrackle, chance (0.4f) ? between (0.0f, 50.0f) : 0.0f);
    setPlain (pid::degradeDropout, chance (0.3f) ? between (0.0f, 50.0f) : 0.0f);
    setPlain (pid::degradeGlitch, chance (0.3f) ? between (10.0f, 60.0f) : 0.0f);

    setPlain (pid::dynThreshold, between (-36.0f, -10.0f));
    setPlain (pid::dynRatio, between (2.0f, 10.0f));
    setPlain (pid::dynGate, chance (0.25f) ? between (-60.0f, -30.0f) : -80.0f);

    setPlain (pid::echoTime, between (60.0f, 700.0f));
    setPlain (pid::echoSync, chance (0.4f) ? 1.0f : 0.0f);
    setPlain (pid::echoDivision, (float) pick ({ 3, 5, 6, 7, 9 }));
    setPlain (pid::echoFeedback, between (20.0f, 85.0f));
    setPlain (pid::echoTone, between (-70.0f, 30.0f));
    setPlain (pid::echoDrive, between (0.0f, 70.0f));
    setPlain (pid::echoWobble, between (0.0f, 60.0f));
    setPlain (pid::echoPingPong, chance (0.4f) ? 1.0f : 0.0f);
    setPlain (pid::echoMix, between (15.0f, 45.0f));

    for (int l = 0; l < 2 && changeModulation; ++l)
    {
        setPlain (pid::lfo (l, "Shape"), (float) r.nextInt (dsp::kNumLfoShapes));
        setPlain (pid::lfo (l, "Rate"), between (0.05f, 4.0f));
    }

    for (const auto& [paramId, value] : keptModulation)
        setPlain (paramId, value);

    // A few modulation routes into things that are switched on
    std::vector<int> candidates;

    for (int d = 1; d < dsp::numDestinations; ++d)
    {
        const auto& p = params::parameterForDestination (d);
        const bool usable = (p.startsWith ("f1") && on[dsp::moduleFilter1]) || (p.startsWith ("tr") && on[dsp::moduleTrash])
                         || (p.startsWith ("f2") && on[dsp::moduleFilter2]) || (p.startsWith ("mo") && on[dsp::moduleMotion])
                         || (p.startsWith ("dg") && on[dsp::moduleDegrade]) || (p.startsWith ("ec") && on[dsp::moduleEcho]);

        if (usable)
            candidates.push_back (d);
    }

    const int routes = candidates.empty() || ! changeModulation ? 0 : 1 + r.nextInt (3);

    for (int s = 0; s < routes; ++s)
    {
        setPlain (pid::slot (s, "Src"), (float) (1 + r.nextInt (3)));
        setPlain (pid::slot (s, "Dst"), (float) candidates[(size_t) r.nextInt ((int) candidates.size())]);
        setPlain (pid::slot (s, "Amt"), (chance (0.5f) ? -1.0f : 1.0f) * between (10.0f, 45.0f));
    }

    // Shuffle the chain a little: that's where the unexpected combinations come from
    auto order = shuffleOrder ? dsp::defaultModuleOrder() : keptOrder;

    for (int swaps = shuffleOrder ? r.nextInt (4) : 0; swaps > 0; --swaps)
        std::swap (order[(size_t) r.nextInt (dsp::numModules)], order[(size_t) r.nextInt (dsp::numModules)]);

    setModuleOrder (order);
    currentPreset = -1;
    getUiState().setProperty ("preset", "Random #" + juce::String (r.nextInt (9000) + 1000), nullptr);
    getUiState().removeProperty ("presetFile", nullptr);
}

//==============================================================================
uint32_t HollowAudioProcessor::packOrder (const dsp::ModuleOrder& order) noexcept
{
    uint32_t packed = 0;

    for (int i = 0; i < dsp::numModules; ++i)
        packed |= (uint32_t) (order[(size_t) i] & 7) << (3 * i);

    return packed;
}

dsp::ModuleOrder HollowAudioProcessor::unpackOrder (uint32_t packed) noexcept
{
    dsp::ModuleOrder order {};

    for (int i = 0; i < dsp::numModules; ++i)
        order[(size_t) i] = (int) ((packed >> (3 * i)) & 7);

    return dsp::isValidOrder (order) ? order : dsp::defaultModuleOrder();
}

dsp::ModuleOrder HollowAudioProcessor::getModuleOrder() const noexcept
{
    return unpackOrder (packedOrder.load());
}

void HollowAudioProcessor::setModuleOrder (const dsp::ModuleOrder& order)
{
    if (! dsp::isValidOrder (order))
        return;

    packedOrder = packOrder (order);
    storeOrderInState (order);
    levelEstimateDirty = true;
    triggerAsyncUpdate(); // a reorder is an undo step too
}

//==============================================================================
HollowAudioProcessor::Snapshot HollowAudioProcessor::capture() const
{
    Snapshot s;

    for (auto* p : getParameters())
        if (p != getBypassParameter())
            s.values.push_back (p->getValue());

    s.order = packedOrder.load();
    const auto ui = state.state.getChildWithName ("UI");

    if (ui.isValid())
    {
        s.preset = ui.getProperty ("preset").toString();
        s.presetFile = ui.getProperty ("presetFile").toString();
    }

    return s;
}

void HollowAudioProcessor::apply (const Snapshot& s)
{
    size_t i = 0;

    for (auto* p : getParameters())
    {
        if (p == getBypassParameter())
            continue;

        if (i < s.values.size() && std::abs (p->getValue() - s.values[i]) > 1.0e-7f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (s.values[i]);
            p->endChangeGesture();
        }

        ++i;
    }

    packedOrder = s.order;
    storeOrderInState (unpackOrder (s.order));
    levelEstimateDirty = true;

    auto ui = getUiState();
    ui.setProperty ("preset", s.preset, nullptr);

    if (s.presetFile.isNotEmpty())
        ui.setProperty ("presetFile", s.presetFile, nullptr);
    else
        ui.removeProperty ("presetFile", nullptr);

    cancelPendingUpdate(); // restoring history isn't a new step
}

void HollowAudioProcessor::commitHistory()
{
    auto now = capture();

    if (now == history)
        return;

    undoStack.push_back (std::move (history));

    if (undoStack.size() > maxUndoSteps)
        undoStack.erase (undoStack.begin());

    history = std::move (now);
    redoStack.clear();
}

void HollowAudioProcessor::undo()
{
    commitHistory(); // anything still pending becomes the step we're undoing

    if (undoStack.empty())
        return;

    redoStack.push_back (std::move (history));
    history = std::move (undoStack.back());
    undoStack.pop_back();
    apply (history);
}

void HollowAudioProcessor::redo()
{
    commitHistory();

    if (redoStack.empty())
        return;

    undoStack.push_back (std::move (history));
    history = std::move (redoStack.back());
    redoStack.pop_back();
    apply (history);
}

void HollowAudioProcessor::storeOrderInState (const dsp::ModuleOrder& order)
{
    juce::StringArray items;

    for (auto id : order)
        items.add (juce::String (id));

    state.state.setProperty ("moduleOrder", items.joinIntoString (","), nullptr);
}

void HollowAudioProcessor::loadOrderFromState()
{
    juce::StringArray items;
    items.addTokens (state.state.getProperty ("moduleOrder").toString(), ",", {});

    dsp::ModuleOrder order = dsp::defaultModuleOrder();

    if (items.size() == dsp::numModules)
        for (int i = 0; i < dsp::numModules; ++i)
            order[(size_t) i] = items[i].getIntValue();

    if (! dsp::isValidOrder (order))
        order = dsp::defaultModuleOrder();

    packedOrder = packOrder (order);
    storeOrderInState (order);
}

//==============================================================================
void HollowAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HollowAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (state.state.getType()))
        {
            state.replaceState (juce::ValueTree::fromXml (*xml));
            loadOrderFromState();

            // replaceState() skips parameters whose snapped value didn't change, which leaves e.g. a bool
            // at a raw 0.85 instead of the stored 1. Push the exact stored values.
            for (auto* p : getParameters())
            {
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    const auto child = state.state.getChildWithProperty ("id", rp->paramID);

                    if (child.isValid() && child.hasProperty ("value"))
                    {
                        const float normalised = rp->convertTo0to1 ((float) child["value"]);

                        if (std::abs (rp->getValue() - normalised) > 1.0e-6f)
                            rp->setValueNotifyingHost (normalised);
                    }
                }
            }

            // A loaded session starts a fresh history
            cancelPendingUpdate();
            undoStack.clear();
            redoStack.clear();
            history = capture();

            // The host's "program" follows the preset the session was saved with
            currentPreset = -1;
            const auto name = getPresetName();
            const auto& list = presets::all();

            for (size_t i = 0; i < list.size(); ++i)
                if (name == list[i].name)
                    currentPreset = (int) i;
        }
    }
}

juce::AudioProcessorParameter* HollowAudioProcessor::getBypassParameter() const
{
    return state.getParameter (pid::bypass);
}

juce::AudioProcessorEditor* HollowAudioProcessor::createEditor()
{
    return new HollowAudioProcessorEditor (*this);
}

} // namespace hl

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new hl::HollowAudioProcessor();
}
