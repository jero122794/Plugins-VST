#include <studioui/EqCore.h>

namespace studioui
{

namespace
{
    constexpr std::array<float, EqCore::numBands> defaultFreqs { 30.0f, 100.0f, 250.0f, 600.0f, 1500.0f, 3500.0f, 8000.0f, 16000.0f };

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }
}

void EqCore::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const juce::String& prefix)
{
    using namespace juce;
    auto dB = AudioParameterFloatAttributes().withLabel ("dB")
                  .withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " dB"; })
                  .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); });
    auto ms = AudioParameterFloatAttributes().withLabel ("ms")
                  .withStringFromValueFunction ([] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + " ms"; })
                  .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); });
    auto hz = AudioParameterFloatAttributes().withLabel ("Hz")
                  .withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; })
                  .withValueFromStringFunction ([] (const String& t)
                  {
                      const float v = t.getFloatValue();   // acepta "2500", "2.5k", "2.5 kHz"
                      return t.containsIgnoreCase ("k") ? v * 1000.0f : v;
                  });
    auto qAttr = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); });

    for (int i = 0; i < numBands; ++i)
    {
        const String n = "EQ " + String (i + 1) + " ";
        const int defaultType = i == 0 ? 3 : (i == numBands - 1 ? 4 : 0);
        auto group = std::make_unique<AudioProcessorParameterGroup> (prefix + "band" + String (i + 1), n.trim(), "|");
        group->addChild (
            std::make_unique<AudioParameterBool>   (ParameterID { bandId (prefix, i, "on"), 1 }, n + "On", false),
            std::make_unique<AudioParameterChoice> (ParameterID { bandId (prefix, i, "type"), 1 }, n + "Type", typeNames, defaultType),
            std::make_unique<AudioParameterFloat>  (ParameterID { bandId (prefix, i, "freq"), 1 }, n + "Freq", skewed (10.0f, 30000.0f, 1000.0f), defaultFreqs[(size_t) i], hz),
            std::make_unique<AudioParameterFloat>  (ParameterID { bandId (prefix, i, "gain"), 1 }, n + "Gain", NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f, dB),
            std::make_unique<AudioParameterFloat>  (ParameterID { bandId (prefix, i, "q"), 1 }, n + "Q", skewed (0.1f, 18.0f, 1.0f), 0.707f, qAttr),
            std::make_unique<AudioParameterChoice> (ParameterID { bandId (prefix, i, "slope"), 1 }, n + "Slope", slopeNames, 1),
            std::make_unique<AudioParameterChoice> (ParameterID { bandId (prefix, i, "placement"), 1 }, n + "Placement", placementNames, 0),
            std::make_unique<AudioParameterFloat>  (ParameterID { bandId (prefix, i, "dynRange"), 1 }, n + "Dyn Range", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, dB),
            std::make_unique<AudioParameterFloat>  (ParameterID { bandId (prefix, i, "threshold"), 1 }, n + "Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -20.0f, dB),
            std::make_unique<AudioParameterFloat>  (ParameterID { bandId (prefix, i, "attack"), 1 }, n + "Attack", skewed (0.1f, 200.0f, 10.0f), 10.0f, ms),
            std::make_unique<AudioParameterFloat>  (ParameterID { bandId (prefix, i, "release"), 1 }, n + "Release", skewed (5.0f, 2000.0f, 150.0f), 150.0f, ms));
        layout.add (std::move (group));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { prefix + "output", 1 }, "EQ Output",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, dB));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { prefix + "hq", 1 }, "EQ HQ (2x)", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { prefix + "phase", 1 }, "EQ Phase",
                                                        StringArray { String::fromUTF8 ("Fase mínima"), "Fase lineal" }, 0));
}

// ----------------------------------------------------------------------------
// Fase lineal: un hilo revisa los parámetros cada 40 ms y, si cambian, rediseña el FIR.

class EqCore::IrThread final : public juce::Thread
{
public:
    explicit IrThread (EqCore& c) : juce::Thread ("EQ linear phase"), core (c) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            if (core.prepared.load() && core.isLinearPhase())
                core.designLinearIr();
            wait (40);
        }
    }
    EqCore& core;
    juce::String lastKey;
};

EqCore::EqCore() : irThread (std::make_unique<IrThread> (*this)) {}

EqCore::~EqCore()
{
    irThread->stopThread (2000);
}

int EqCore::getLatency() const noexcept
{
    if (isLinearPhase()) return linearLatency.load();
    return hqState == 1 && oversampler != nullptr ? juce::roundToInt (oversampler->getLatencyInSamples()) : 0;
}

void EqCore::designLinearIr()
{
    // Clave de los parámetros relevantes: solo se rediseña si algo cambió
    juce::String key;
    std::array<dspcore::EqBandParams, numBands> bands;
    for (int i = 0; i < numBands; ++i)
    {
        auto b = readBand (i);
        b.dynRangeDb = 0.0f;   // en fase lineal las bandas son estáticas
        bands[(size_t) i] = b;
        if (b.enabled)
            key << i << ':' << (int) b.type << ',' << b.freq << ',' << b.gainDb << ',' << b.q << ',' << b.slopeDbPerOct
                << ',' << (int) b.placement << ';';
    }
    const float outDb = pOutput->load();
    key << "out" << outDb << "fs" << baseRate;
    if (key == irThread->lastKey)
        return;
    irThread->lastKey = key;

    // Se procesa en M/S: una IR para Mid y otra para Side. Las bandas Stereo van a ambas;
    // Left/Right no existen en M/S y se tratan como Stereo.
    const double designRate = linearDesignRate();
    auto target = [&] (bool side)
    {
        return [&bands, designRate, outDb, side] (double f)
        {
            double d = outDb;
            for (auto& b : bands)
            {
                if ((b.placement == dspcore::Placement::Mid && side) || (b.placement == dspcore::Placement::Side && ! side))
                    continue;
                d += dspcore::EqBand::responseDb (b, designRate, f);
            }
            return d;
        };
    };
    const int taps = linearTaps();
    const auto hMid = dspcore::designLinearPhaseFir (target (false), baseRate, taps);
    const auto hSide = dspcore::designLinearPhaseFir (target (true), baseRate, taps);

    juce::AudioBuffer<float> ir (2, taps);
    std::copy (hMid.begin(), hMid.end(), ir.getWritePointer (0));
    std::copy (hSide.begin(), hSide.end(), ir.getWritePointer (1));
    const juce::ScopedLock sl (convolutionLock);
    if (convolution == nullptr) return;
    convolution->loadImpulseResponse (std::move (ir), baseRate, juce::dsp::Convolution::Stereo::yes,
                                      juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    linearLatency.store (convolution->getLatency() + taps / 2);   // la latencia real de la convolución, no la pedida
}

void EqCore::bind (juce::AudioProcessorValueTreeState& s, const juce::String& p)
{
    state = &s;
    prefix = p;
    for (int i = 0; i < numBands; ++i)
    {
        auto get = [&] (const char* n) { return s.getRawParameterValue (bandId (p, i, n)); };
        bandPtrs[(size_t) i] = { get ("on"), get ("type"), get ("freq"), get ("gain"), get ("q"), get ("slope"),
                                 get ("placement"), get ("dynRange"), get ("threshold"), get ("attack"), get ("release") };
    }
    pOutput = s.getRawParameterValue (p + "output");
    pHq = s.getRawParameterValue (p + "hq");
    pPhase = s.getRawParameterValue (p + "phase");
}

dspcore::EqBandParams EqCore::readBand (int i) const noexcept
{
    const auto& p = bandPtrs[(size_t) i];
    dspcore::EqBandParams b;
    b.enabled       = p.on->load() > 0.5f;
    b.type          = (dspcore::EqType) juce::jlimit (0, 5, (int) p.type->load());
    b.freq          = p.freq->load();
    b.gainDb        = p.gain->load();
    b.q             = p.q->load();
    b.slopeDbPerOct = 12 * (1 + juce::jlimit (0, 3, (int) p.slope->load()));
    b.placement     = (dspcore::Placement) juce::jlimit (0, 4, (int) p.placement->load());
    b.dynRangeDb    = p.dynRange->load();
    b.thresholdDb   = p.threshold->load();
    b.attackMs      = p.attack->load();
    b.releaseMs     = p.release->load();
    return b;
}

void EqCore::prepare (double sampleRate, int maxBlock, int numChannels)
{
    baseRate = sampleRate;
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) juce::jmax (1, numChannels), 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing ((size_t) maxBlock);
    hqState = -1;
    setHq (pHq->load() > 0.5f);

    prepared.store (false);
    numChannelsPrepared = numChannels;
    {
        const juce::ScopedLock sl (convolutionLock);
        convolution = std::make_unique<juce::dsp::Convolution> (juce::dsp::Convolution::Latency { convBlock });
        convolution->prepare ({ sampleRate, (juce::uint32) maxBlock, (juce::uint32) juce::jmax (1, numChannels) });
    }
    linearLatency.store (convolution->getLatency() + linearTaps() / 2);
    irThread->lastKey = {};
    prepared.store (true);
    if (! irThread->isThreadRunning())
        irThread->startThread (juce::Thread::Priority::low);
}

void EqCore::setHq (bool on)
{
    if ((int) on == hqState)
        return;
    hqState = (int) on;
    const double rate = on ? baseRate * 2.0 : baseRate;
    eq.setOutputGainDb (pOutput->load());
    eq.prepare (rate);
    eqRate.store (rate);
    oversampler->reset();
}

void EqCore::process (float* const* ch, int numCh, int numSamples) noexcept
{
    if (isLinearPhase())
    {
        preFifo.push (ch[0], numCh > 1 ? ch[1] : nullptr, numSamples);
        auto toMs = [&] (bool forward)
        {
            if (numCh < 2) return;
            for (int i = 0; i < numSamples; ++i)
            {
                const float a = ch[0][i], b = ch[1][i];
                ch[0][i] = forward ? 0.5f * (a + b) : a + b;   // L/R → M/S, o M/S → L/R
                ch[1][i] = forward ? 0.5f * (a - b) : a - b;
            }
        };
        toMs (true);
        juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) numSamples);
        convolution->process (juce::dsp::ProcessContextReplacing<float> (block));
        toMs (false);
        postFifo.push (ch[0], numCh > 1 ? ch[1] : nullptr, numSamples);
        return;
    }

    setHq (pHq->load() > 0.5f);
    for (int i = 0; i < numBands; ++i)
        eq.setBand (i, readBand (i));
    eq.setOutputGainDb (pOutput->load());

    preFifo.push (ch[0], numCh > 1 ? ch[1] : nullptr, numSamples);
    if (hqState == 1)
    {
        juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) numSamples);
        auto up = oversampler->processSamplesUp (block);
        eq.process (up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr, numCh, (int) up.getNumSamples());
        oversampler->processSamplesDown (block);
    }
    else
    {
        eq.process (ch[0], numCh > 1 ? ch[1] : nullptr, numCh, numSamples);
    }
    postFifo.push (ch[0], numCh > 1 ? ch[1] : nullptr, numSamples);
}

} // namespace studioui
