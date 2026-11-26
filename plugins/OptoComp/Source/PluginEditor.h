#pragma once

#include "PluginProcessor.h"

class OptoLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    OptoLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

/** Medidor horizontal de reducción de ganancia (0–20 dB) con balística tipo VU. */
class GainReductionMeter final : public juce::Component, private juce::Timer
{
public:
    explicit GainReductionMeter (OptoCompProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    OptoCompProcessor& proc;
    float shown = 0.0f;
};

class OptoCompEditor final : public juce::AudioProcessorEditor
{
public:
    explicit OptoCompEditor (OptoCompProcessor&);
    ~OptoCompEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const char* paramId, const juce::String& name, bool big, const juce::String& suffix);
    void setupCombo (juce::ComboBox&, const char* paramId, std::unique_ptr<ComboAttachment>&);

    OptoCompProcessor& proc;
    OptoLookAndFeel lnf;

    Knob gain, peak, emphasis, color, mix;
    juce::ComboBox model, mode, oversampling;
    std::unique_ptr<ComboAttachment> modelAtt, modeAtt, osAtt;
    juce::ToggleButton link { "Stereo Link" };
    std::unique_ptr<ButtonAttachment> linkAtt;
    GainReductionMeter meter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptoCompEditor)
};
