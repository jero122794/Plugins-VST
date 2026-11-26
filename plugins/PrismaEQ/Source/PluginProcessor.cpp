#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;
using namespace PrismaIDs;

namespace
{
    // Posiciones iniciales repartidas por el espectro (todas apagadas)
    constexpr std::array<float, numBands> defaultFreqs { 30.0f, 100.0f, 250.0f, 600.0f, 1500.0f, 3500.0f, 8000.0f, 16000.0f };

    juce::NormalisableRange<float> freqRange()
    {
        juce::NormalisableRange<float> r (10.0f, 30000.0f);
        r.setSkewForCentre (1000.0f);
        return r;
    }

    juce::NormalisableRange<float> qRange()
    {
        juce::NormalisableRange<float> r (0.1f, 18.0f);
        r.setSkewForCentre (1.0f);
        return r;
    }

    juce::NormalisableRange<float> msRange (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }
}

APVTS::ParameterLayout PrismaEQProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout layout;
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
                      // Acepta "2500", "2.5k", "2.5 kHz"
                      const float v = t.getFloatValue();
                      return t.containsIgnoreCase ("k") ? v * 1000.0f : v;
                  });
    auto qAttr = AudioParameterFloatAttributes()
                     .withStringFromValueFunction ([] (float v, int) { return String (v, 2); });

    for (int i = 0; i < numBands; ++i)
    {
        const String n = "Band " + String (i + 1) + " ";
        const int defaultType = i == 0 ? 3 : (i == numBands - 1 ? 4 : 0);
        auto group = std::make_unique<AudioProcessorParameterGroup> ("band" + String (i + 1), n.trim(), "|");
        group->addChild (
            std::make_unique<AudioParameterBool>   (ParameterID { band (i, "on"), 1 }, n + "On", false),
            std::make_unique<AudioParameterChoice> (ParameterID { band (i, "type"), 1 }, n + "Type", typeNames, defaultType),
            std::make_unique<AudioParameterFloat>  (ParameterID { band (i, "freq"), 1 }, n + "Freq", freqRange(), defaultFreqs[(size_t) i], hz),
            std::make_unique<AudioParameterFloat>  (ParameterID { band (i, "gain"), 1 }, n + "Gain", NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f, dB),
            std::make_unique<AudioParameterFloat>  (ParameterID { band (i, "q"), 1 }, n + "Q", qRange(), 0.707f, qAttr),
            std::make_unique<AudioParameterChoice> (ParameterID { band (i, "slope"), 1 }, n + "Slope", slopeNames, 1),
            std::make_unique<AudioParameterChoice> (ParameterID { band (i, "placement"), 1 }, n + "Placement", placementNames, 0),
            std::make_unique<AudioParameterFloat>  (ParameterID { band (i, "dynRange"), 1 }, n + "Dyn Range", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, dB),
            std::make_unique<AudioParameterFloat>  (ParameterID { band (i, "threshold"), 1 }, n + "Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -20.0f, dB),
            std::make_unique<AudioParameterFloat>  (ParameterID { band (i, "attack"), 1 }, n + "Attack", msRange (0.1f, 200.0f, 10.0f), 10.0f, ms),
            std::make_unique<AudioParameterFloat>  (ParameterID { band (i, "release"), 1 }, n + "Release", msRange (5.0f, 2000.0f, 150.0f), 150.0f, ms));
        layout.add (std::move (group));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { output, 1 }, "Output",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, dB));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { hq, 1 }, "HQ (2x)", false));
    return layout;
}

PrismaEQProcessor::PrismaEQProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PrismaEQ", createLayout())
{
    for (int i = 0; i < numBands; ++i)
    {
        auto get = [&] (const char* n) { return apvts.getRawParameterValue (band (i, n)); };
        bandPtrs[(size_t) i] = { get ("on"), get ("type"), get ("freq"), get ("gain"), get ("q"), get ("slope"),
                                 get ("placement"), get ("dynRange"), get ("threshold"), get ("attack"), get ("release") };
    }
    pOutput = apvts.getRawParameterValue (output);
    pHq = apvts.getRawParameterValue (hq);
}

dspcore::EqBandParams PrismaEQProcessor::readBand (int i) const noexcept
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

bool PrismaEQProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void PrismaEQProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    baseRate = sampleRate;
    maxBlock = samplesPerBlock;
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) juce::jmax (1, getTotalNumOutputChannels()), 1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    hqState = -1;
    setHq (pHq->load() > 0.5f);
}

void PrismaEQProcessor::setHq (bool on)
{
    if ((int) on == hqState)
        return;
    hqState = (int) on;
    const double rate = on ? baseRate * 2.0 : baseRate;
    eq.setOutputGainDb (pOutput->load());
    eq.prepare (rate);
    eqRate.store (rate);
    oversampler->reset();
    setLatencySamples (on ? juce::roundToInt (oversampler->getLatencyInSamples()) : 0);
}

void PrismaEQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels(), 2);
    const int numSamples = buffer.getNumSamples();
    for (int c = numCh; c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, numSamples);
    if (numCh == 0 || numSamples == 0)
        return;

    setHq (pHq->load() > 0.5f);
    for (int i = 0; i < numBands; ++i)
        eq.setBand (i, readBand (i));
    eq.setOutputGainDb (pOutput->load());

    auto* const* chans = buffer.getArrayOfWritePointers();
    for (int start = 0; start < numSamples; start += maxBlock)
    {
        float* chunk[2] = { chans[0] + start, numCh > 1 ? chans[1] + start : nullptr };
        processChunk (chunk, numCh, juce::jmin (maxBlock, numSamples - start));
    }
}

void PrismaEQProcessor::processChunk (float* const* ch, int numCh, int numSamples)
{
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

juce::AudioProcessorEditor* PrismaEQProcessor::createEditor()
{
    return new PrismaEQEditor (*this);
}

void PrismaEQProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PrismaEQProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PrismaEQProcessor();
}
