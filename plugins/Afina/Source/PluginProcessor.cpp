#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace AfinaIDs;

juce::AudioProcessorValueTreeState::ParameterLayout AfinaProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { key, 1 }, "Key", keyNames, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { scale, 1 }, "Scale", scaleNames, 0));
    NormalisableRange<float> retuneRange (0.0f, 400.0f);
    retuneRange.setSkewForCentre (40.0f);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { retune, 1 }, "Retune Speed", retuneRange, 20.0f,
        AudioParameterFloatAttributes().withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? String ("0 ms") : String (roundToInt (v)) + " ms"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { amount, 1 }, "Amount", NormalisableRange<float> (0.0f, 100.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { transpose, 1 }, "Transpose", NormalisableRange<float> (-12.0f, 12.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("st").withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (roundToInt (v)) + " st"; })));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { range, 1 }, "Voice Range", rangeNames, 1));
    for (int i = 0; i < 12; ++i)
        l.add (std::make_unique<AudioParameterBool> (ParameterID { note (i), 1 }, "Note " + keyNames[i], true));
    return l;
}

AfinaProcessor::AfinaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Afina", createLayout())
{
    pKey = apvts.getRawParameterValue (key);
    pScale = apvts.getRawParameterValue (scale);
    pRetune = apvts.getRawParameterValue (retune);
    pAmount = apvts.getRawParameterValue (amount);
    pTranspose = apvts.getRawParameterValue (transpose);
    pRange = apvts.getRawParameterValue (range);
    for (int i = 0; i < 12; ++i) pNotes[(size_t) i] = apvts.getRawParameterValue (note (i));
}

std::uint16_t AfinaProcessor::scaleMask (int index) noexcept
{
    using S = dspcore::Scale;
    static constexpr std::uint16_t masks[] = { S::chromatic, S::major, S::minor, S::harmonicMinor, S::dorian,
                                               S::pentatonicMajor, S::pentatonicMinor, S::blues };
    return masks[juce::jlimit (0, 7, index)];
}

std::uint16_t AfinaProcessor::currentNotes() const noexcept
{
    std::uint16_t user = 0;
    for (int i = 0; i < 12; ++i)
        if (pNotes[(size_t) i]->load() > 0.5f) user |= (std::uint16_t) (1u << i);
    return dspcore::Scale::absolute (scaleMask ((int) pScale->load()), (int) pKey->load()) & user;
}

bool AfinaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void AfinaProcessor::prepareToPlay (double sampleRate, int)
{
    // Rango de voz → tono mínimo → latencia (la ventana de análisis debe abarcar dos periodos del tono más grave)
    static constexpr float minHz[] = { 150.0f, 80.0f, 60.0f };
    for (int r = 0; r < 3; ++r)
        correctors[(size_t) r].prepare (sampleRate, minHz[r], 1100.0f);
    activeRange.store ((int) pRange->load());
    setLatencySamples (correctors[(size_t) activeRange.load()].getLatency());
}

void AfinaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels(), 2);
    const int n = buffer.getNumSamples();
    for (int c = numCh; c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
    if (numCh == 0 || n == 0) return;

    const int r = juce::jlimit (0, 2, (int) pRange->load());
    if (r != activeRange.load())
    {
        activeRange.store (r);
        correctors[(size_t) r].reset();
        setLatencySamples (correctors[(size_t) r].getLatency());
    }
    auto& pc = correctors[(size_t) r];
    pc.setNotes (currentNotes());
    pc.setRetuneMs (pRetune->load());
    pc.setAmount (pAmount->load() / 100.0f);
    pc.setTransposeSemitones (pTranspose->load());

    float* l = buffer.getWritePointer (0);
    float* rr = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float x = rr ? 0.5f * (l[i] + rr[i]) : l[i];
        peak = std::max (peak, std::fabs (x));
        const float y = pc.process (x);
        l[i] = y;
        if (rr) rr[i] = y;
    }
    inputLevel.store (std::max (peak, inputLevel.load() * 0.8f));
}

juce::AudioProcessorEditor* AfinaProcessor::createEditor() { return new AfinaEditor (*this); }

void AfinaProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void AfinaProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AfinaProcessor(); }
