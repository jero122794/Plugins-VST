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
