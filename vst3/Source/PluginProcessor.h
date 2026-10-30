#pragma once
#include <JuceHeader.h>

// PyGamer SPX -- VST3 plugin wrapping the SPX90 II emulation core
// (SpxCore.h, adapted from the PyGamer firmware's spx_core.h).
// The core is included ONLY in PluginProcessor.cpp, so its static
// state lives in exactly one translation unit per binary.
class SpxAudioProcessor : public juce::AudioProcessor
{
public:
    SpxAudioProcessor();
    ~SpxAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PyGamer SPX"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- editor-facing accessors (read core state; message thread) --
    int          getCoreProgramNumber() const;
    juce::String getCoreProgramName() const;
    juce::String getCoreAlgoName() const;
    int          getCoreParamCount() const;
    juce::String getCoreParamName (int i) const;
    juce::String getCoreParamValueText (int i) const;
    int          getMeterLeds() const;
    juce::String getSourceName() const;
    bool         isFreezePlaying() const;
    void         triggerFromGui();
    int          storeFromGui();    // returns the user slot stored into
    void         recallFromGui();

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void applyGlobalsFromApvts();
    void applySnapshotToCore();
    void syncApvtsFromCore();
    float coreToNorm (int i) const;
    void applyNormToCore (int i, float norm);

    std::atomic<float>* programParam  = nullptr;
    std::atomic<float>* paramNorm[8]  = {};
    std::atomic<float>* balanceParam  = nullptr;
    std::atomic<float>* inGainParam   = nullptr;
    std::atomic<float>* outLevelParam = nullptr;
    std::atomic<float>* sourceParam   = nullptr;
    std::atomic<float>* bypassParam   = nullptr;

    int   coreRate = 0;
    float cachedNorm[8];
    float cachedBalance = -1.0f;
    int   cachedProg = -1;

    struct ProgSnapshot
    {
        bool  valid = false;
        int   algo = 0;
        char  name[17] = {};
        float p[8] = {};
        float bal = 0.5f;
    };
    ProgSnapshot pendingSnapshot;
    std::vector<unsigned char> pendingUserImage;
    bool havePendingUsers = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpxAudioProcessor)
};
