#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <studioui/EqCore.h>

class PrismaEQProcessor final : public juce::AudioProcessor
{
public:
    PrismaEQProcessor();

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

    juce::AudioProcessorValueTreeState apvts;
    studioui::EqCore core;

    // Atajos usados por los tests
    dspcore::EqBandParams readBand (int i) const noexcept { return core.readBand (i); }
    double getEqSampleRate() const noexcept { return core.getProcessingRate(); }

private:
    int maxBlock = 512;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismaEQProcessor)
};

namespace PrismaIDs
{
    inline juce::String band (int i, const char* name) { return studioui::EqCore::bandId ({}, i, name); }
    inline constexpr auto output = "output";
    inline constexpr auto hq     = "hq";
}
