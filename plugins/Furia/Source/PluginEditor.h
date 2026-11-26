#pragma once
//
// UI de Furia: frontal negro con escalera de LEDs de GR, LEDs de ratio, bloques Detector / Audio
// y perillas plateadas con la escala impresa sobre la perilla. Diseño propio.
//
#include "PluginProcessor.h"

class FuriaKnobLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

/** Botón rectangular plateado (pulsador). */
class SilverButton final : public juce::Button
{
public:
    SilverButton() : juce::Button ("button") {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

class FuriaEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FuriaEditor (FuriaProcessor&);
    ~FuriaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void setParam (const char* id, float plain);
    float param (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    FuriaProcessor& proc;
    FuriaKnobLookAndFeel knobLnf;
    juce::Slider input, attack, release, output, mix;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAtt, attackAtt, releaseAtt, outputAtt, mixAtt;
    SilverButton ratioButton, detectorButton, audioButton;
    juce::ToggleButton powerSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt;

    // Zonas clicables
    std::array<juce::Rectangle<float>, 9> ratioLeds;
    std::array<juce::Rectangle<float>, 3> detectorLeds, audioLeds;
    juce::Rectangle<float> grArea, lampArea, powerArea;
    float shownGr = 0.0f, peakGr = 0.0f;
    int peakHoldTicks = 0;
    bool redlineHold = false, thdHold = false;
    int redlineTicks = 0, thdTicks = 0;
};
