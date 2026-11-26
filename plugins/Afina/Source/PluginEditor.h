#pragma once

#include "PluginProcessor.h"
#include <studioui/StudioLookAndFeel.h>

/** Nota detectada en grande + medidor de desafinación (±50 cents). */
class NoteDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit NoteDisplay (AfinaProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    AfinaProcessor& proc;
    float inCents = 0.0f, outCents = 0.0f, shownIn = 0.0f, shownOut = 0.0f;
    int note = -1;
    bool voiced = false;
};

/** Historial de afinación sobre carriles de notas (estilo piano roll): entrada (gris) y salida corregida (color). */
class PitchHistory final : public juce::Component, private juce::Timer
{
public:
    explicit PitchHistory (AfinaProcessor& p) : proc (p) { in.fill (-1.0f); out.fill (-1.0f); startTimerHz (30); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    static constexpr int len = 240;   // 8 s
    AfinaProcessor& proc;
    std::array<float, len> in {}, out {};
    int pos = 0;
    float centreMidi = 60.0f;
};

/** Una octava de teclado: color = nota de la escala; clic = activar / desactivar a mano. */
class NoteKeyboard final : public juce::Component, private juce::Timer
{
public:
    explicit NoteKeyboard (AfinaProcessor& p) : proc (p) { startTimerHz (10); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<float> keyBounds (int pc) const;
    static bool isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
    AfinaProcessor& proc;
};

class AfinaEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AfinaEditor (AfinaProcessor&);
    ~AfinaEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    AfinaProcessor& proc;
    studioui::StudioLookAndFeel lnf;
    NoteDisplay display;
    PitchHistory history;
    NoteKeyboard keyboard;
    juce::ComboBox key, scale, range;
    juce::Label keyLabel, scaleLabel, rangeLabel;
    studioui::LabelledKnob retune { "VELOCIDAD" }, amount { "CANTIDAD" }, transpose { "TRANSPONER" };
    std::unique_ptr<CA> keyAtt, scaleAtt, rangeAtt;
    std::unique_ptr<SA> retuneAtt, amountAtt, transposeAtt;
};
