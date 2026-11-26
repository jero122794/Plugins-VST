#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <dspcore/DynamicEq.h>

#include <array>
#include <atomic>
#include <memory>

namespace PrismaIDs
{
    inline constexpr int numBands = 8;

    inline juce::String band (int i, const char* name) { return "b" + juce::String (i + 1) + "_" + name; }

    inline constexpr auto output = "output";
    inline constexpr auto hq     = "hq";

    inline const juce::StringArray typeNames      { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" };
    inline const juce::StringArray slopeNames     { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
    inline const juce::StringArray placementNames { "Stereo", "Mid", "Side", "Left", "Right" };
}

/** FIFO lock-free audio → UI para el analizador (mono, suma L+R). */
class AnalyzerFifo
{
public:
    static constexpr int capacity = 1 << 15;

    void push (const float* l, const float* r, int n) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        auto copy = [&] (int dst, int len, int srcOffset)
        {
            for (int i = 0; i < len; ++i)
                data[(size_t) (dst + i)] = r ? 0.5f * (l[srcOffset + i] + r[srcOffset + i]) : l[srcOffset + i];
        };
        copy (s1, n1, 0);
        copy (s2, n2, n1);
        fifo.finishedWrite (n1 + n2);  // si la UI no lee, se descartan muestras (no bloquea)
    }

    /** Lee todo lo disponible. Solo desde el hilo de UI. */
    template <typename Fn>
    void pull (Fn&& consume)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        if (n1 > 0) consume (data.data() + s1, n1);
        if (n2 > 0) consume (data.data() + s2, n2);
        fifo.finishedRead (n1 + n2);
    }

private:
    juce::AbstractFifo fifo { capacity };
    std::array<float, capacity> data {};
};

class PrismaEQProcessor final : public juce::AudioProcessor
{
public:
    using Eq = dspcore::DynamicEq<PrismaIDs::numBands>;

    PrismaEQProcessor();
    ~PrismaEQProcessor() override = default;

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

    /** Parámetros de una banda leídos de la APVTS (los usa también la UI para dibujar). */
    dspcore::EqBandParams readBand (int i) const noexcept;

    float getDynamicGainDb (int band) const noexcept { return eq.band (band).getDynamicGainDb(); }
    double getEqSampleRate() const noexcept { return eqRate.load(); }

    juce::AudioProcessorValueTreeState apvts;
    AnalyzerFifo preFifo, postFifo;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void processChunk (float* const* ch, int numCh, int numSamples);
    void setHq (bool on);

    struct BandPtrs
    {
        std::atomic<float> *on, *type, *freq, *gain, *q, *slope, *placement, *dynRange, *threshold, *attack, *release;
    };
    std::array<BandPtrs, PrismaIDs::numBands> bandPtrs {};
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pHq = nullptr;

    Eq eq;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int hqState = -1;
    double baseRate = 48000.0;
    std::atomic<double> eqRate { 48000.0 };
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismaEQProcessor)
};
