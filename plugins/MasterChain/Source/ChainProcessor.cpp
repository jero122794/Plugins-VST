#include "ChainProcessor.h"
#include "ChainEditor.h"

using namespace mastering;

namespace
{
    // Módulos activos por defecto: entrada, EQ (plano) y limitador. El resto, en bypass.
    bool defaultOn (const juce::String& id) { return id == "input" || id == "eq" || id == "limiter"; }
}

juce::AudioProcessorValueTreeState::ParameterLayout MasterChainProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto& info : moduleRegistry())
    {
        if (! info.inChain) continue;
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { info.prefix + "on", 1 },
                                                                info.name + " On", defaultOn (info.id)));
        info.addParameters (layout, info.prefix);
    }
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { compareId, 1 }, "A/B Compare (loudness matched)", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { platformId, 1 }, "Platform", platformNames(), 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { previewId, 1 }, "Platform Preview", false));
    return layout;
}

MasterChainProcessor::MasterChainProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MasterChain", createLayout())
{
    for (auto& info : moduleRegistry())
    {
        if (! info.inChain) continue;
        auto slot = std::make_unique<ModuleSlot> (info, info.create());
        slot->bind (apvts);
        slots.push_back (std::move (slot));
    }
    std::uint64_t packed = 0;
    for (int i = 0; i < (int) slots.size(); ++i) packed = withSlot (packed, i, i);
    packedOrder.store (packed);
    lastMovable = (int) slots.size() - 3;   // los dos últimos (limitador y dither) son fijos

    pCompare = apvts.getRawParameterValue (compareId);
    pPlatform = apvts.getRawParameterValue (platformId);
    pPreview = apvts.getRawParameterValue (previewId);
    storeOrder();
}

void MasterChainProcessor::moveModule (int position, int direction)
{
    const int other = position + direction;
    if (! isReorderable (position) || ! isReorderable (other))
        return;
    const auto packed = packedOrder.load();
    const int a = slotAt (packed, position), b = slotAt (packed, other);
    packedOrder.store (withSlot (withSlot (packed, position, b), other, a));
    storeOrder();
}

void MasterChainProcessor::storeOrder()
{
    juce::StringArray ids;
    for (int p = 0; p < (int) slots.size(); ++p)
        ids.add (slots[(size_t) slotAt (p)]->moduleInfo.id);
    apvts.state.setProperty ("order", ids.joinIntoString (","), nullptr);
}

bool MasterChainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void MasterChainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = samplesPerBlock;
    const int numCh = juce::jmax (1, getTotalNumOutputChannels());
    for (auto& s : slots) s->prepare (sampleRate, samplesPerBlock, numCh);
    inputMeter.prepare (sampleRate, numCh);
    outputMeter.prepare (sampleRate, numCh);
    for (auto& d : dryDelay) d.assign ((size_t) dryDelaySize, 0.0f);
    for (auto& s : scratch) s.assign ((size_t) samplesPerBlock, 0.0f);
    dryWrite = 0;
    compareFade = pCompare->load() > 0.5f ? 1.0f : 0.0f;

    totalLatency = 0;
    for (auto& s : slots) totalLatency += s->getLatency();
    setLatencySamples (totalLatency);
}

void MasterChainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
        processChunk (chunk, numCh, juce::jmin (maxBlock, numSamples - start));
    }

    int latency = 0;
    for (auto& s : slots) latency += s->getLatency();
    if (latency != totalLatency)
    {
        totalLatency = latency;
        setLatencySamples (totalLatency);   // p. ej. al cambiar el lookahead o el HQ del EQ
    }
}

void MasterChainProcessor::processChunk (float* const* ch, int numCh, int n)
{
    // Original, para el A/B
    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < n; ++i)
            dryDelay[(size_t) c][(size_t) ((dryWrite + i) & (dryDelaySize - 1))] = ch[c][i];

    inputMeter.process (ch, n);

    const auto ord = packedOrder.load();
    for (int p = 0; p < (int) slots.size(); ++p)
        slots[(size_t) slotAt (ord, p)]->process (ch, numCh, n);

    outputMeter.process (ch, n);
    correlation.process (ch, numCh, n);

    // --- A/B a igual sonoridad: el original sube/baja hasta la sonoridad del procesado
    const double sIn = inputMeter.getShortTerm(), sOut = outputMeter.getShortTerm();
    if (std::isfinite (sIn) && std::isfinite (sOut) && sIn > -60.0)
    {
        const float target = (float) std::pow (10.0, juce::jlimit (-24.0, 24.0, sOut - sIn) / 20.0);
        matchGain += (1.0f - std::exp (-(float) n / (float) fs)) * (target - matchGain);   // ~1 s
    }

    const float compareTarget = pCompare->load() > 0.5f ? 1.0f : 0.0f;
    const float fadeStep = (float) (1.0 / (0.02 * fs));
    const int readBase = dryWrite - totalLatency;
    if (compareFade > 0.0f || compareTarget > 0.0f)
    {
        for (int i = 0; i < n; ++i)
        {
            compareFade = compareTarget > compareFade ? std::min (1.0f, compareFade + fadeStep) : std::max (0.0f, compareFade - fadeStep);
            for (int c = 0; c < numCh; ++c)
            {
                const float dry = dryDelay[(size_t) c][(size_t) ((readBase + i) & (dryDelaySize - 1))] * matchGain;
                ch[c][i] += compareFade * (dry - ch[c][i]);
            }
        }
    }
    dryWrite = (dryWrite + n) & (dryDelaySize - 1);

    // --- Vista previa de la normalización de la plataforma (solo para escuchar: ¡desactivar al exportar!)
    const int platform = (int) pPlatform->load();
    float previewTarget = 1.0f;
    if (pPreview->load() > 0.5f && platform > 0 && std::isfinite (outputMeter.getIntegrated()))
        previewTarget = (float) std::pow (10.0, std::min (0.0, platformTarget (platform) - outputMeter.getIntegrated()) / 20.0);
    for (int i = 0; i < n; ++i)
    {
        previewGain += 0.0005f * (previewTarget - previewGain);
        for (int c = 0; c < numCh; ++c) ch[c][i] *= previewGain;
    }
}

juce::AudioProcessorEditor* MasterChainProcessor::createEditor() { return new MasterChainEditor (*this); }

void MasterChainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    storeOrder();
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MasterChainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // Restaurar el orden, respetando las posiciones fijas
    const auto ids = juce::StringArray::fromTokens (apvts.state.getProperty ("order").toString(), ",", {});
    if (ids.size() == (int) slots.size())
    {
        std::vector<int> newOrder;
        for (auto& id : ids)
            for (int s = 0; s < (int) slots.size(); ++s)
                if (slots[(size_t) s]->moduleInfo.id == id) newOrder.push_back (s);
        const bool valid = (int) newOrder.size() == (int) slots.size() && newOrder.front() == 0
                        && newOrder[newOrder.size() - 2] == (int) slots.size() - 2 && newOrder.back() == (int) slots.size() - 1;
        if (valid)
        {
            std::uint64_t packed = 0;
            for (int p = 0; p < (int) slots.size(); ++p) packed = withSlot (packed, p, newOrder[(size_t) p]);
            packedOrder.store (packed);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MasterChainProcessor(); }
