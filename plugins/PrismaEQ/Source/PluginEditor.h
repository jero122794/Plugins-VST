#pragma once

#include "PluginProcessor.h"
#include <studioui/EqView.h>

class PrismaEQEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PrismaEQEditor (PrismaEQProcessor& p) : AudioProcessorEditor (&p), view (p.core)
    {
        setLookAndFeel (&lnf);
        addAndMakeVisible (view);
        setSize (980, 640);
    }
    ~PrismaEQEditor() override { setLookAndFeel (nullptr); }

    void selectBand (int b) { view.selectBand (b); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (studioui::Colours::panel);
        g.setColour (studioui::Colours::text);
        g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
        g.drawText ("PRISMA EQ", 20, 12, 160, 28, juce::Justification::centredLeft);
    }

    void resized() override { view.setBounds (getLocalBounds().reduced (20, 12)); }

private:
    studioui::StudioLookAndFeel lnf;
    studioui::EqView view;
};
