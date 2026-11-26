#pragma once
//
// UI de OptoComp: frontal de rack de época. Diseño propio inspirado en el lenguaje
// visual del hardware de los 60–70 (frontal metálico, VU analógico, perillas
// acanaladas, interruptores de palanca). Sin marcas ni logotipos de terceros.
//
#include "PluginProcessor.h"
#include <studioui/StudioLookAndFeel.h>

namespace hw
{
    struct Palette
    {
        juce::Colour faceTop, faceBottom, ear, text, textShadow, accent;
        bool dark;
    };
    Palette paletteFor (int model);   // 0 = válvula (gris metálico), 1 = transistor (negro)
}

/** Perillas y palanca dibujadas como hardware. El estilo se elige con la propiedad "dark" del componente. */
class HardwareLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

/** Interruptor de palanca metálico de dos posiciones (arriba = on). */
class BatToggle final : public juce::Button
{
public:
    BatToggle() : juce::Button ("toggle") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/** VU analógico: escala lineal en tensión (0 VU al 71 %), aguja con física de muelle amortiguado. */
class VuMeter final : public juce::Component, private juce::Timer
{
public:
    explicit VuMeter (OptoCompProcessor&);
    void setDark (bool d) { dark = d; repaint(); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    static float deflection (float vu);   // VU → fracción del recorrido (0–1)

    OptoCompProcessor& proc;
    float position = 0.708f, velocity = 0.0f;
    bool dark = false;
};

class OptoCompEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OptoCompEditor (OptoCompProcessor&);
    ~OptoCompEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawKnobScale (juce::Graphics&, juce::Slider&, const hw::Palette&, bool tube);
    void drawEngraved (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float size, const hw::Palette&,
                       juce::Justification = juce::Justification::centred, bool bold = true);

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    OptoCompProcessor& proc;
    HardwareLookAndFeel hardwareLnf;
    studioui::StudioLookAndFeel barLnf;

    juce::Slider gain, peak, meterSelect;
    BatToggle limitSwitch;
    VuMeter vu;
    std::unique_ptr<SA> gainAtt, peakAtt, meterAtt;
    std::unique_ptr<BA> limitAtt;

    // Barra inferior (controles modernos)
    juce::TextButton tubeButton { "T-2  TUBE" }, solidButton { "T-3  SOLID STATE" };
    studioui::LabelledKnob emphasis { "HF EMPHASIS" }, color { "COLOR" }, mix { "MIX" };
    juce::ComboBox oversampling;
    juce::ToggleButton link { "Stereo link" };
    std::unique_ptr<SA> emphAtt, colorAtt, mixAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> osAtt;
    std::unique_ptr<BA> linkAtt;

    int model = -1;
    juce::Rectangle<int> rack, bar;
    juce::Point<float> lampCentre;
    bool lampOn = false;
};
