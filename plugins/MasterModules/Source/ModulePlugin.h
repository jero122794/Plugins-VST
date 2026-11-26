#pragma once
// Plugin suelto genérico: el mismo código genera Limiter, BusComp, Saturator, Imager,
// Clipper y Meter. El módulo se elige con su id (en el plugin, con MODULE_ID al compilar).
#include <mastering/Module.h>
#include <mastering/Widgets.h>
#include <studioui/StudioLookAndFeel.h>

class ModulePluginProcessor final : public juce::AudioProcessor
{
public:
    explicit ModulePluginProcessor (const juce::String& moduleId)
        : AudioProcessor (BusesProperties()
                              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          info (*mastering::findModule (moduleId)),
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

        inPeak.store (std::max (inPeak.load(), buffer.getMagnitude (0, n)));
        auto* const* chans = buffer.getArrayOfWritePointers();
        for (int start = 0; start < n; start += maxBlock)
        {
            float* chunk[2] = { chans[0] + start, numCh > 1 ? chans[1] + start : nullptr };
            module->run (chunk, numCh, juce::jmin (maxBlock, n - start));
        }
        outPeak.store (std::max (outPeak.load(), buffer.getMagnitude (0, n)));
        if (module->getLatency() != getLatencySamples()) setLatencySamples (module->getLatency());
    }
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return title(); }
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

    juce::String title() const
    {
       #ifdef MODULE_ID
        return JucePlugin_Name;
       #else
        static const std::map<juce::String, juce::String> names {
            { "limiter", "MC Limiter" }, { "comp", "MC BusComp" }, { "sat", "MC Saturator" },
            { "imager", "MC Imager" }, { "clipper", "MC Clipper" }, { "meter", "MC Meter" } };
        auto it = names.find (info.id);
        return it != names.end() ? it->second : info.name;
       #endif
    }

    float takeInputPeak() noexcept  { return inPeak.exchange (0.0f); }
    float takeOutputPeak() noexcept { return outPeak.exchange (0.0f); }

    const mastering::ModuleInfo& info;
    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<mastering::Module> module;
    int maxBlock = 512;
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
};

class ModulePluginEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ModulePluginEditor (ModulePluginProcessor& p)
        : AudioProcessorEditor (&p), proc (p),
          inMeter ([&p] { return p.takeInputPeak(); }, "IN"),
          outMeter ([&p] { return p.takeOutputPeak(); }, "OUT")
    {
        setLookAndFeel (&lnf);
        panel = p.module->createPanel();
        addAndMakeVisible (*panel);
        addAndMakeVisible (inMeter);
        addAndMakeVisible (outMeter);
        const auto id = p.info.id;
        if (id == "meter") setSize (380, 660);
        else               setSize (860, 520);
    }
    ~ModulePluginEditor() override { panel.reset(); setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        using namespace studioui;
        g.fillAll (Colours::panel);
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
        g.drawText (proc.title().toUpperCase(), 20, 12, getWidth() - 40, 26, juce::Justification::centredLeft);
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (proc.info.description, 20, 38, getWidth() - 40, 18, juce::Justification::centredLeft);
        g.setColour (Colours::hairline);
        g.drawHorizontalLine (64, 20.0f, (float) getWidth() - 20.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 12).withTrimmedTop (64);
        const bool meters = proc.info.id != "meter";
        inMeter.setVisible (meters);
        outMeter.setVisible (meters);
        if (meters)
        {
            inMeter.setBounds (r.removeFromLeft (34));
            r.removeFromLeft (14);
            outMeter.setBounds (r.removeFromRight (34));
            r.removeFromRight (14);
        }
        panel->setBounds (r);
    }

private:
    studioui::DefaultLookAndFeelScope defaultLnf;
    ModulePluginProcessor& proc;
    studioui::StudioLookAndFeel lnf;
    std::unique_ptr<juce::Component> panel;
    mastering::LevelMeter inMeter, outMeter;
};

inline juce::AudioProcessorEditor* ModulePluginProcessor::createEditor() { return new ModulePluginEditor (*this); }
