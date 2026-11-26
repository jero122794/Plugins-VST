#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <dspcore/OptoCompressor.h>
#include <dspcore/Saturation.h>

#include <array>
#include <memory>

namespace ParamID
{
    inline constexpr auto model         = "model";
    inline constexpr auto peakReduction = "peakReduction";
    inline constexpr auto gain          = "gain";
    inline constexpr auto mode          = "mode";
    inline constexpr auto emphasis      = "emphasis";
    inline constexpr auto color         = "color";
    inline constexpr auto mix           = "mix";
    inline constexpr auto link          = "link";
    inline constexpr auto oversampling  = "oversampling";
    inline constexpr auto meterMode     = "meterMode";
}

class OptoCompProcessor final : public juce::AudioProcessor
{
public:
    OptoCompProcessor();
    ~OptoCompProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    float getGainReductionDb() const noexcept { return compressor.getGainReductionDb(); }
    float getOutputRms() const noexcept { return outputRms.load(); }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void updateOversampling (int choice);
    void processChunk (float* const* channels, int numCh, int numSamples);

    dspcore::OptoCompressor compressor;
    std::array<dspcore::OutputStage, 2> stages;

    // Índice 0 = 1x (sin sobremuestreo), 1 = 2x, 2 = 4x.
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers;
    int currentOs = -1;

    juce::dsp::DryWetMixer<float> dryWet { 64 };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> makeup { 1.0f };

    std::atomic<float>* pModel = nullptr;
    std::atomic<float>* pPeak  = nullptr;
    std::atomic<float>* pGain  = nullptr;
    std::atomic<float>* pMode  = nullptr;
    std::atomic<float>* pEmph  = nullptr;
    std::atomic<float>* pColor = nullptr;
    std::atomic<float>* pMix   = nullptr;
    std::atomic<float>* pLink  = nullptr;
    std::atomic<float>* pOs    = nullptr;
    std::atomic<float> outputRms { 0.0f };

    double baseSampleRate = 48000.0;
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptoCompProcessor)
};
