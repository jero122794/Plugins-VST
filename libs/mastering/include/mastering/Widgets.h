#pragma once
//
// Componentes de UI comunes a los módulos de mastering.
//
#include "Module.h"
#include <studioui/StudioLookAndFeel.h>

namespace mastering
{

/** Barra vertical de reducción de ganancia (o de cualquier valor en dB positivos). */
class ReductionMeter final : public juce::Component, private juce::Timer
{
public:
    ReductionMeter (std::function<float()> source, juce::String label, float maxDb = 12.0f)
        : getValue (std::move (source)), text (std::move (label)), range (maxDb)
    {
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override
    {
        const float v = getValue();
        shown = v > shown ? v : shown + 0.15f * (v - shown);
        peakHold = std::max (peakHold * 0.995f, v);
        repaint();
    }

    std::function<float()> getValue;
    juce::String text;
    float range, shown = 0.0f, peakHold = 0.0f;
};

/**
    Panel generado a partir de una lista de parámetros: perilla para los continuos,
    desplegable para las opciones e interruptor para los booleanos.
*/
class ParamPanel : public juce::Component
{
public:
    struct Spec { juce::String id; juce::String label; };

    ParamPanel (APVTS& state, const std::vector<Spec>& specs, std::unique_ptr<juce::Component> meter = nullptr);

    void resized() override;

private:
    struct Choice
    {
        juce::Label label;
        juce::ComboBox box;
        std::unique_ptr<APVTS::ComboBoxAttachment> att;
    };

    std::vector<std::unique_ptr<studioui::LabelledKnob>> knobs;
    std::vector<std::unique_ptr<APVTS::SliderAttachment>> knobAtts;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> toggleAtts;
    std::unique_ptr<juce::Component> meterComponent;
};

} // namespace mastering
