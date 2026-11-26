#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    studioui::EqCore::addParameters (layout, {});   // sin prefijo: IDs "b1_freq", "output", "hq"
    return layout;
}

PrismaEQProcessor::PrismaEQProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PrismaEQ", createLayout())
{
    core.bind (apvts, {});
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
    maxBlock = samplesPerBlock;
    core.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()));
    setLatencySamples (core.getLatency());
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

    auto* const* chans = buffer.getArrayOfWritePointers();
    for (int start = 0; start < numSamples; start += maxBlock)
    {
        float* chunk[2] = { chans[0] + start, numCh > 1 ? chans[1] + start : nullptr };
        core.process (chunk, numCh, juce::jmin (maxBlock, numSamples - start));
    }
    if (getLatencySamples() != core.getLatency())
        setLatencySamples (core.getLatency());
}

juce::AudioProcessorEditor* PrismaEQProcessor::createEditor() { return new PrismaEQEditor (*this); }

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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PrismaEQProcessor(); }
