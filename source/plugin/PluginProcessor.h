#pragma once

#include "AudioFifo.h"
#include "ModulationHost.h"
#include "Parameters.h"
#include "Settings.h"

namespace hl
{
namespace detail
{
    inline void atomicMax (std::atomic<float>& a, float v) noexcept
    {
        auto cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }

    inline void atomicMin (std::atomic<float>& a, float v) noexcept
    {
        auto cur = a.load (std::memory_order_relaxed);
        while (v < cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
} // namespace detail

/** Values published by the audio thread for the UI. Peaks/reductions accumulate until taken. */
struct MeterValues
{
    std::array<std::atomic<float>, 2> inputPeak {}, outputPeak {};     // linear
    std::array<std::atomic<float>, dsp::kTrashBands> trashPeaks {};    // linear, band input
    std::atomic<float> dynReduction { 0.0f };                          // dB, <= 0
    std::atomic<float> dynInput { 0.0f };                              // linear peak
    std::atomic<bool> gateOpen { true };
    std::atomic<float> degradePitch { 0.0f }, dropoutGain { 1.0f };
    std::atomic<bool> glitching { false };
    std::array<std::atomic<float>, dsp::numDestinations> modOffsets {};
    std::array<std::atomic<float>, 2> lfoValues {}, lfoPhases {};
    std::atomic<float> envelope { 0.0f }, motionPhase { 0.0f };
    std::atomic<float> bpm { 120.0f };
    std::atomic<float> autoLevelDb { 0.0f };
};

class HollowAudioProcessor final : public juce::AudioProcessor,
                                   public ModulationHost,
                                   private juce::AudioProcessorValueTreeState::Listener,
                                   private juce::AudioProcessorListener,
                                   private juce::AsyncUpdater
{
public:
    HollowAudioProcessor();
    ~HollowAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Hollow"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return juce::jmax (0, currentPreset); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    MeterValues& getMeters() noexcept { return meters; }
    AudioFifo& getSpectrumFifo() noexcept { return spectrumFifo; }
    AudioFifo& getScopeFifo() noexcept { return scopeFifo; }

    /** Impulse currently loaded in the convolver (thread-safe, may lag a few ms). */
    std::shared_ptr<const dsp::StereoImpulse> getDisplayImpulse() const { return chain.getConvolve().getDisplayImpulse(); }

    /** Current (unmodulated) parameter values as DSP settings. Any thread. */
    dsp::ChainSettings readSettings() const noexcept { return binding.read(); }

    dsp::ModuleOrder getModuleOrder() const noexcept;
    void setModuleOrder (const dsp::ModuleOrder& order);

    /** Factory presets and the dice. Message thread. */
    void loadPreset (int index);
    void randomize();
    juce::String getPresetName() const;

    /** User presets: files in the preset folder (Documents/Hollow/Presets). Message thread. */
    juce::File getUserPresetFolder() const;
    void setUserPresetFolder (const juce::File& folder) { userPresetFolder = folder; } // tests
    juce::Array<juce::File> getUserPresets() const;
    juce::File saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    bool deleteUserPreset (const juce::File& file);

    /** Copies preset files into the preset folder (never overwrites: clashes get a " (2)" suffix,
        identical copies are skipped). Returns the files now in the folder, in order. */
    juce::Array<juce::File> importPresets (const juce::Array<juce::File>& files, juce::StringArray* problems = nullptr);
    static bool isPresetFile (const juce::File& file);

    /** The user preset file currently loaded (empty for factory presets and dice rolls). */
    juce::File getCurrentUserPreset() const;

    static constexpr const char* presetExtension = ".hollowpreset";

    /** Parameters that are settings rather than part of a sound (presets and dice leave them alone). */
    static bool isGlobalSetting (const juce::String& paramId);

    /** Undo / redo of whole actions: a knob drag, a preset load, a dice roll, a reorder. Message thread.
        Host automation doesn't create steps. */
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    void undo();
    void redo();

    /** Records any pending step now (normally happens a moment after a gesture ends). */
    void flushHistory() { handleUpdateNowIfNeeded(); }

    /** The distortion's current oversampling factor (1, 2, 4 or 8). */
    int getOversamplingFactor() const noexcept { return chain.getTrash().getOversampling(); }

    /** UI state stored alongside the parameters. */
    juce::ValueTree getUiState() { return state.state.getOrCreateChildWithName ("UI", nullptr); }

    // ModulationHost
    float getModulationOffset (int destination) const override;
    bool isModulated (int destination) const override;
    void addModulation (int destination, int source) override;
    void clearModulation (int destination) override;

    /** Waits until the convolver has picked up the current impulse (offline tools). */
    bool waitForImpulse (int timeoutMs);

    static constexpr int spectrumFifoFrames = 1 << 15;

private:
    class ImpulseWorker;

    /** A setting changed: the level estimate is redone on the next audio block. */
    void parameterChanged (const juce::String&, float) override { levelEstimateDirty = true; }

    // Undo history: every finished gesture (and every reorder) records a step once things settle
    struct Snapshot
    {
        std::vector<float> values;
        uint32_t order = 0;
        juce::String preset, presetFile;

        bool operator== (const Snapshot& o) const { return values == o.values && order == o.order && preset == o.preset && presetFile == o.presetFile; }
    };

    Snapshot capture() const;
    void apply (const Snapshot&);
    void commitHistory();
    void handleAsyncUpdate() override { commitHistory(); }
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override { triggerAsyncUpdate(); }

    void processChunk (float* left, float* right, int numSamples, const dsp::TransportInfo& transport) noexcept;
    void publishMeters() noexcept;
    void storeOrderInState (const dsp::ModuleOrder& order);
    void loadOrderFromState();
    void setPlain (const juce::String& paramId, float plainValue);
    void resetAllParameters();

    static uint32_t packOrder (const dsp::ModuleOrder& order) noexcept;
    static dsp::ModuleOrder unpackOrder (uint32_t packed) noexcept;

    juce::AudioProcessorValueTreeState state;
    params::Binding binding;

    dsp::Chain chain;

    MeterValues meters;
    AudioFifo spectrumFifo { 1, spectrumFifoFrames }, scopeFifo { 2, 1 << 14 };

    std::atomic<uint32_t> packedOrder { packOrder (dsp::defaultModuleOrder()) };
    std::atomic<bool> levelEstimateDirty { true };
    std::vector<Snapshot> undoStack, redoStack;
    Snapshot history;
    static constexpr size_t maxUndoSteps = 100;
    int samplesSinceEstimate = 1 << 30;

    int maxChunk = 512;
    std::array<std::vector<float>, 2> dry;
    std::vector<float> scratchRight, mono;
    std::array<dsp::DelayLine, 2> dryDelay;
    dsp::LinearSmoother bypassMix;

    int currentPreset = 0;
    juce::Random dice;
    juce::File userPresetFolder;
    juce::SharedResourcePointer<Settings> settings;
    std::unique_ptr<ImpulseWorker> worker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HollowAudioProcessor)
};

} // namespace hl
