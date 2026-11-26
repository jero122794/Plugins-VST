#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace FuriaIDs;

juce::AudioProcessorValueTreeState::ParameterLayout FuriaProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto knob = [&] (const char* id, const char* name, float lo, float def)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, 10.0f, 0.01f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));
    };
    knob (input, "Input", 0.0f, 3.0f);
    knob (attack, "Attack", 1.0f, 5.0f);
    knob (release, "Release", 1.0f, 5.0f);
    knob (output, "Output", 0.0f, 5.0f);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { mix, 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f), 100.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ratio, 1 }, "Ratio", ratioNames, 3));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { detHp, 1 }, "Detector HP", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { detBand, 1 }, "Detector Band", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { stereoLink, 1 }, "Stereo Link", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { audioHp, 1 }, "Audio HP", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { dist, 1 }, "Distortion", distNames, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { power, 1 }, "Power", true));
    return l;
}

FuriaProcessor::FuriaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Furia", createLayout())
{
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pInput = g (input); pAttack = g (attack); pRelease = g (release); pOutput = g (output); pMix = g (mix);
    pRatio = g (ratio); pDetHp = g (detHp); pDetBand = g (detBand); pLink = g (stereoLink); pAudioHp = g (audioHp);
    pDist = g (dist); pPower = g (power);
}

bool FuriaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void FuriaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = samplesPerBlock;
    const auto numCh = (size_t) juce::jmax (1, getTotalNumOutputChannels());
    compressor.prepare (sampleRate);
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (numCh, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    latency = juce::roundToInt (oversampler->getLatencyInSamples());
    for (auto& d : dryDelay) d.assign ((size_t) latency + 1, 0.0f);
    for (auto& d : dryScratch) d.assign ((size_t) samplesPerBlock, 0.0f);
    dryWrite = 0;
    outGain.reset (sampleRate, 0.05);
    wetMix.reset (sampleRate, 0.03);
    setLatencySamples (latency);
}

void FuriaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels(), 2);
    const int n = buffer.getNumSamples();
    for (int c = numCh; c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
    if (numCh == 0 || n == 0) return;

    compressor.setInput (pInput->load());
    compressor.setOutput (pOutput->load());
    compressor.setAttack (pAttack->load());
    compressor.setRelease (pRelease->load());
    compressor.setRatio ((dspcore::FuriaCompressor::Ratio) juce::jlimit (0, 8, (int) pRatio->load()));
    compressor.setDetector (pDetHp->load() > 0.5f, pDetBand->load() > 0.5f, pLink->load() > 0.5f);
    compressor.setAudioHp (pAudioHp->load() > 0.5f);
    // Power apagado = bypass con la misma latencia (mezcla 100 % seca)
    wetMix.setTargetValue (pPower->load() > 0.5f ? pMix->load() / 100.0f : 0.0f);
    outGain.setTargetValue (compressor.getOutputGain());

    auto* const* chans = buffer.getArrayOfWritePointers();
    for (int start = 0; start < n; start += maxBlock)
    {
        float* chunk[2] = { chans[0] + start, numCh > 1 ? chans[1] + start : nullptr };
        processChunk (chunk, numCh, juce::jmin (maxBlock, n - start));
    }
}

void FuriaProcessor::processChunk (float* const* ch, int numCh, int n)
{
    juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
    const int size = latency + 1;
    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < n; ++i)
        {
            auto& d = dryDelay[(size_t) c];
            const int w = (dryWrite + i) % size;
            d[(size_t) w] = ch[c][i];
            dryScratch[(size_t) c][(size_t) i] = d[(size_t) ((w + 1) % size)];   // la muestra de hace `latency`
        }
    dryWrite = (dryWrite + n) % size;

    compressor.processDynamics (ch, numCh, n);

    // Pilotos: "1% THD" cuando la distorsión trabaja con nivel; la referencia (~-12 dBFS) es donde las curvas superan ~1 % de THD
    const auto mode = (dspcore::FuriaCompressor::Dist) juce::jlimit (0, 2, (int) pDist->load());
    const float preDist = block.findMinAndMax().getEnd();
    thd.store (mode != dspcore::FuriaCompressor::Dist::Off && std::max (preDist, -block.findMinAndMax().getStart()) > 0.25f);

    auto up = oversampler->processSamplesUp (block);
    if (mode != dspcore::FuriaCompressor::Dist::Off)
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* d = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i) d[i] = dspcore::FuriaCompressor::distort (d[i], mode);
        }
    oversampler->processSamplesDown (block);

    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue(), m = wetMix.getNextValue();
        for (int c = 0; c < numCh; ++c)
        {
            const float dry = dryScratch[(size_t) c][(size_t) i];
            ch[c][i] = dry + m * (ch[c][i] * g - dry);
        }
    }
    const auto range = block.findMinAndMax();
    redline.store (std::max (range.getEnd(), -range.getStart()) > 0.98f);
}

juce::AudioProcessorEditor* FuriaProcessor::createEditor() { return new FuriaEditor (*this); }

void FuriaProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void FuriaProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FuriaProcessor(); }
