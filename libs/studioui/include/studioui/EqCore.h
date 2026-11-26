#pragma once
//
// Núcleo de audio del EQ, compartido por PrismaEQ y el módulo EQ de la cadena de mastering:
// parámetros (con prefijo), DSP, sobremuestreo HQ y FIFOs del analizador.
//
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <dspcore/DynamicEq.h>

#include <array>
#include <atomic>
#include <memory>

namespace studioui
{

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

class EqCore
{
public:
    static constexpr int numBands = 8;
    using Eq = dspcore::DynamicEq<numBands>;

    inline static const juce::StringArray typeNames      { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" };
    inline static const juce::StringArray slopeNames     { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
    inline static const juce::StringArray placementNames { "Stereo", "Mid", "Side", "Left", "Right" };

    static juce::String bandId (const juce::String& prefix, int band, const char* name)
    {
        return prefix + "b" + juce::String (band + 1) + "_" + name;
    }

    static void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout&, const juce::String& prefix);

    void bind (juce::AudioProcessorValueTreeState&, const juce::String& prefix);
    void prepare (double sampleRate, int maxBlock, int numChannels);
    void process (float* const* ch, int numCh, int numSamples) noexcept;  // numSamples ≤ maxBlock

    int getLatency() const noexcept { return hqState == 1 ? juce::roundToInt (oversampler->getLatencyInSamples()) : 0; }

    dspcore::EqBandParams readBand (int i) const noexcept;
    float getDynamicGainDb (int band) const noexcept { return eq.band (band).getDynamicGainDb(); }
    double getProcessingRate() const noexcept { return eqRate.load(); }
    double getBaseRate() const noexcept { return baseRate; }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String prefix;
    AnalyzerFifo preFifo, postFifo;

private:
    void setHq (bool on);

    struct BandPtrs
    {
        std::atomic<float> *on, *type, *freq, *gain, *q, *slope, *placement, *dynRange, *threshold, *attack, *release;
    };
    std::array<BandPtrs, numBands> bandPtrs {};
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pHq = nullptr;

    Eq eq;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int hqState = -1;
    double baseRate = 48000.0;
    std::atomic<double> eqRate { 48000.0 };
};

} // namespace studioui
