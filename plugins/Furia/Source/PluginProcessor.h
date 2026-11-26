#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <dspcore/Furia.h>

namespace FuriaIDs
{
    inline constexpr auto input = "input", attack = "attack", release = "release", output = "output", mix = "mix";
    inline constexpr auto ratio = "ratio", detHp = "detHp", detBand = "detBand", stereoLink = "link", audioHp = "audioHp", dist = "dist";
    inline constexpr auto power = "power";
    inline const juce::StringArray ratioNames { "1:1", "2:1", "3:1", "4:1", "6:1", "10:1", "20:1", "Nuke", "Opto" };
    inline const juce::StringArray distNames  { "Off", "Dist 2", "Dist 3" };
}

class FuriaProcessor final : public juce::AudioProcessor
{
public:
    FuriaProcessor();

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
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    float getGainReductionDb() const noexcept { return compressor.getGainReductionDb(); }
    bool isRedline() const noexcept { return redline.load(); }
    bool isThdLit() const noexcept  { return thd.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void processChunk (float* const* ch, int numCh, int n);

    dspcore::FuriaCompressor compressor;
    // Siempre sobremuestreado (latencia constante, se active o no la distorsión)
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    // Señal seca retrasada un número ENTERO de muestras = la latencia reportada al DAW
    // (la latencia del sobremuestreo IIR es fraccionaria; DryWetMixer la interpolaría y el bypass no sería exacto)
    std::array<std::vector<float>, 2> dryDelay, dryScratch;
    int dryWrite = 0, latency = 0;
    juce::SmoothedValue<float> wetMix { 1.0f };
    juce::SmoothedValue<float> outGain { 1.0f };
    int maxBlock = 512;
    std::atomic<bool> redline { false }, thd { false };

    std::atomic<float> *pInput {}, *pAttack {}, *pRelease {}, *pOutput {}, *pMix {}, *pRatio {}, *pDetHp {}, *pDetBand {},
                       *pLink {}, *pAudioHp {}, *pDist {}, *pPower {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FuriaProcessor)
};
