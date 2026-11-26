// Plugin suelto genérico: el mismo código genera Limiter, BusComp, Saturator, Imager,
// Clipper y Meter. El módulo se elige en tiempo de compilación con MODULE_ID.
#include <mastering/Module.h>
#include <studioui/StudioLookAndFeel.h>

#ifndef MODULE_ID
 #error "Define MODULE_ID (por ejemplo \"limiter\")"
#endif

class ModulePluginProcessor final : public juce::AudioProcessor
{
public:
    ModulePluginProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          info (*mastering::findModule (MODULE_ID)),
          apvts (*this, nullptr, "Module", createLayout (info))
    {
        module = info.create();
        module->bind (apvts, {});
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout (const mastering::ModuleInfo& i)
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        i.addParameters (layout, {});
        return layout;
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        maxBlock = samplesPerBlock;
        module->prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()));
        setLatencySamples (module->getLatency());
    }
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto& out = layouts.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
        return layouts.getMainInputChannelSet() == out;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int numCh = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels(), 2);
        const int n = buffer.getNumSamples();
        for (int c = numCh; c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
        if (numCh == 0 || n == 0) return;
        auto* const* chans = buffer.getArrayOfWritePointers();
        for (int start = 0; start < n; start += maxBlock)
        {
            float* chunk[2] = { chans[0] + start, numCh > 1 ? chans[1] + start : nullptr };
            module->process (chunk, numCh, juce::jmin (maxBlock, n - start));
        }
        if (module->getLatency() != getLatencySamples()) setLatencySamples (module->getLatency());
    }
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

    void getStateInformation (juce::MemoryBlock& destData) override
    {
        if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    const mastering::ModuleInfo& info;
    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<mastering::Module> module;
    int maxBlock = 512;
};

class ModulePluginEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ModulePluginEditor (ModulePluginProcessor& p) : AudioProcessorEditor (&p), proc (p)
    {
        setLookAndFeel (&lnf);
        panel = p.module->createPanel();
        addAndMakeVisible (*panel);
        const auto id = p.info.id;
        if (id == "eq")         setSize (980, 640);
        else if (id == "meter") setSize (360, 640);
        else                    setSize (760, 380);
    }
    ~ModulePluginEditor() override { panel.reset(); setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (studioui::Colours::panel);
        g.setColour (studioui::Colours::text);
        g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
        g.drawText (juce::String (JucePlugin_Name).toUpperCase(), 20, 12, getWidth() - 40, 26, juce::Justification::centredLeft);
        g.setColour (studioui::Colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (proc.info.description, 20, 38, getWidth() - 40, 18, juce::Justification::centredLeft);
    }
    void resized() override { panel->setBounds (getLocalBounds().reduced (20, 12).withTrimmedTop (54)); }

private:
    ModulePluginProcessor& proc;
    studioui::StudioLookAndFeel lnf;
    std::unique_ptr<juce::Component> panel;
};

juce::AudioProcessorEditor* ModulePluginProcessor::createEditor() { return new ModulePluginEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ModulePluginProcessor(); }
