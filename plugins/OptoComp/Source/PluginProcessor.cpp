#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout OptoCompProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout l;

    auto pct = AudioParameterFloatAttributes().withLabel ("%");
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::model, 1 }, "Model",
                                                   StringArray { "T-2 Tube", "T-3 Solid State" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::peakReduction, 1 }, "Peak Reduction",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::gain, 1 }, "Gain",
                                                  NormalisableRange<float> (-20.0f, 30.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::mode, 1 }, "Mode",
                                                   StringArray { "Compress", "Limit" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::emphasis, 1 }, "HF Emphasis",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::color, 1 }, "Color",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::mix, 1 }, "Mix",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::link, 1 }, "Stereo Link", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::oversampling, 1 }, "Oversampling",
                                                   StringArray { "1x", "2x", "4x" }, 1));
    // Selector del VU, como en el hardware: reducción de ganancia o nivel de salida (+4 / +10)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::meterMode, 1 }, "Meter",
                                                   StringArray { "Gain Reduction", "Output +4", "Output +10" }, 0));
    return l;
}

OptoCompProcessor::OptoCompProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "OptoComp", createLayout())
{
    pModel = apvts.getRawParameterValue (ParamID::model);
    pPeak  = apvts.getRawParameterValue (ParamID::peakReduction);
    pGain  = apvts.getRawParameterValue (ParamID::gain);
    pMode  = apvts.getRawParameterValue (ParamID::mode);
    pEmph  = apvts.getRawParameterValue (ParamID::emphasis);
    pColor = apvts.getRawParameterValue (ParamID::color);
    pMix   = apvts.getRawParameterValue (ParamID::mix);
    pLink  = apvts.getRawParameterValue (ParamID::link);
    pOs    = apvts.getRawParameterValue (ParamID::oversampling);
}

bool OptoCompProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void OptoCompProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    baseSampleRate = sampleRate;
    maxBlock = samplesPerBlock;
    const auto numCh = (size_t) juce::jmax (1, getTotalNumOutputChannels());

    compressor.prepare (sampleRate);

    // Filtros IIR polifásicos: latencia baja, adecuada para un compresor en inserción.
    oversamplers[0].reset();
    for (size_t i = 1; i < oversamplers.size(); ++i)
    {
        oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>> (
            numCh, i, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversamplers[i]->initProcessing ((size_t) samplesPerBlock);
    }

    dryWet.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, (juce::uint32) numCh });
    dryWet.setMixingRule (juce::dsp::DryWetMixingRule::linear);

    makeup.reset (sampleRate, 0.05);
    makeup.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pGain->load()));

    currentOs = -1;
    updateOversampling ((int) pOs->load());
}

void OptoCompProcessor::updateOversampling (int choice)
{
    choice = juce::jlimit (0, 2, choice);
    if (choice == currentOs)
        return;
    currentOs = choice;

    const double factor = (double) (1 << choice);
    for (auto& s : stages)
    {
        s.prepare (baseSampleRate * factor);
        s.reset();
    }

    float latency = 0.0f;
    if (auto& os = oversamplers[(size_t) choice])
    {
        os->reset();
        latency = os->getLatencyInSamples();
    }
    dryWet.setWetLatency (latency);
    setLatencySamples (juce::roundToInt (latency));
}

void OptoCompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels(), 2);
    const int numSamples = buffer.getNumSamples();
    for (int c = numCh; c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, numSamples);
    if (numCh == 0 || numSamples == 0)
        return;

    // --- Parámetros
    const auto model = (int) pModel->load() == 0 ? dspcore::OptoModel::LA2A : dspcore::OptoModel::LA3A;
    compressor.setModel (model);
    compressor.setPeakReduction (pPeak->load());
    compressor.setMode ((int) pMode->load() == 0 ? dspcore::OptoMode::Compress : dspcore::OptoMode::Limit);
    compressor.setEmphasis (pEmph->load());
    compressor.setStereoLink (pLink->load() > 0.5f);
    for (auto& s : stages)
    {
        s.setType (model == dspcore::OptoModel::LA2A ? dspcore::StageType::Tube : dspcore::StageType::SolidState);
        s.setColor (pColor->load() / 100.0f);
    }
    updateOversampling ((int) pOs->load());
    makeup.setTargetValue (juce::Decibels::decibelsToGain (pGain->load()));
    dryWet.setWetMixProportion (pMix->load() / 100.0f);

    // El host puede mandar bloques mayores que los anunciados: se procesa por trozos.
    auto* const* chans = buffer.getArrayOfWritePointers();
    for (int start = 0; start < numSamples; start += maxBlock)
    {
        float* chunk[2] = { chans[0] + start, numCh > 1 ? chans[1] + start : nullptr };
        processChunk (chunk, numCh, juce::jmin (maxBlock, numSamples - start));
    }
}

void OptoCompProcessor::processChunk (float* const* channels, int numCh, int numSamples)
{
    juce::dsp::AudioBlock<float> block (channels, (size_t) numCh, (size_t) numSamples);
    dryWet.pushDrySamples (block);

    // --- 1) Atenuador óptico (frecuencia base: la célula no genera aliasing)
    compressor.process (channels, numCh, numSamples);

    // --- 2) Make-up (en el hardware, la ganancia va antes de la etapa de salida)
    if (makeup.isSmoothing())
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float g = makeup.getNextValue();
            for (int c = 0; c < numCh; ++c)
                channels[c][n] *= g;
        }
    }
    else
    {
        block.multiplyBy (makeup.getTargetValue());
    }

    // --- 3) Etapa de salida no lineal, sobremuestreada
    auto runStages = [this] (juce::dsp::AudioBlock<float>& b)
    {
        for (size_t c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getChannelPointer (c);
            auto& st = stages[c];
            for (size_t n = 0; n < b.getNumSamples(); ++n)
                d[n] = st.process (d[n]);
        }
    };

    if (auto& os = oversamplers[(size_t) currentOs])
    {
        auto up = os->processSamplesUp (block);
        runStages (up);
        os->processSamplesDown (block);
    }
    else
    {
        runStages (block);
    }

    dryWet.mixWetSamples (block);

    // Nivel RMS de salida para el VU (la balística se aplica en la UI)
    double sum = 0.0;
    for (int c = 0; c < numCh; ++c)
        for (int n = 0; n < numSamples; ++n) sum += (double) channels[c][n] * channels[c][n];
    outputRms.store ((float) std::sqrt (sum / (double) juce::jmax (1, numCh * numSamples)));
}

juce::AudioProcessorEditor* OptoCompProcessor::createEditor()
{
    return new OptoCompEditor (*this);
}

void OptoCompProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void OptoCompProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OptoCompProcessor();
}
