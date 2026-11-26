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

/** Historial desplazable (8 s): nivel de entrada (relleno), salida (línea) y reducción de ganancia (desde arriba). */
class HistoryView final : public juce::Component, private juce::Timer
{
public:
    HistoryView (Module& m, bool showReduction, float reductionRangeDb = 12.0f)
        : module (m), showGr (showReduction), grRange (reductionRangeDb)
    {
        in.fill (-120.0f); out.fill (-120.0f); gr.fill (0.0f);
        startTimerHz (30);
    }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    static constexpr int len = 240;
    Module& module;
    bool showGr;
    float grRange;
    std::array<float, len> in {}, out {}, gr {};
    int pos = 0;
    std::uint32_t lastBlocks = 0;
};

/** Curva de transferencia entrada→salida (dB) con un punto que sigue el nivel de entrada en vivo. */
class TransferCurveView final : public juce::Component, private juce::Timer
{
public:
    TransferCurveView (std::function<float (float)> curve, std::function<float()> inputPeak, std::function<float()> ceilingDb = nullptr)
        : transfer (std::move (curve)), getInput (std::move (inputPeak)), ceiling (std::move (ceilingDb)) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override
    {
        const float p = getInput();
        live = p > live ? p : live * 0.92f;
        repaint();
    }
    std::function<float (float)> transfer;
    std::function<float()> getInput;
    std::function<float()> ceiling;
    float live = 0.0f;
};

/** FIFO estéreo lock-free audio → UI (para el goniómetro). */
class StereoScopeFifo
{
public:
    static constexpr int capacity = 1 << 13;
    void push (const float* l, const float* r, int n) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) { L[(size_t) (s1 + i)] = l[i]; R[(size_t) (s1 + i)] = r[i]; }
        for (int i = 0; i < n2; ++i) { L[(size_t) (s2 + i)] = l[n1 + i]; R[(size_t) (s2 + i)] = r[n1 + i]; }
        fifo.finishedWrite (n1 + n2);
    }
    template <typename Fn> void pull (Fn&& fn)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) fn (L[(size_t) (s1 + i)], R[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) fn (L[(size_t) (s2 + i)], R[(size_t) (s2 + i)]);
        fifo.finishedRead (n1 + n2);
    }
private:
    juce::AbstractFifo fifo { capacity };
    std::array<float, capacity> L {}, R {};
};

/** Goniómetro (vectorscopio): Mid vertical, Side horizontal. Mono = línea vertical; fase invertida = horizontal. */
class Goniometer final : public juce::Component, private juce::Timer
{
public:
    explicit Goniometer (StereoScopeFifo& f) : fifo (f) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    StereoScopeFifo& fifo;
    static constexpr int maxPoints = 2048;
    std::array<juce::Point<float>, maxPoints> pts {};
    int count = 0, write = 0;
    float correlation = 1.0f, gain = 1.0f;
};

/** Barra vertical de nivel (pico con caída y retención). */
class LevelMeter final : public juce::Component, private juce::Timer
{
public:
    LevelMeter (std::function<float()> peakSource, juce::String label) : source (std::move (peakSource)), text (std::move (label)) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override
    {
        const float db = juce::Decibels::gainToDecibels (source(), -90.0f);
        shown = db > shown ? db : shown - 0.8f;
        hold = db > hold ? db : hold - 0.15f;
        repaint();
    }
    std::function<float()> source;
    juce::String text;
    float shown = -90.0f, hold = -90.0f;
};

/**
    Panel generado a partir de una lista de parámetros: perilla para los continuos,
    desplegable para las opciones e interruptor para los booleanos.
*/
class ParamPanel : public juce::Component
{
public:
    struct Spec { juce::String id; juce::String label; };

    ParamPanel (APVTS& state, const std::vector<Spec>& specs, std::unique_ptr<juce::Component> meter = nullptr,
                std::unique_ptr<juce::Component> visual = nullptr);

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
    std::unique_ptr<juce::Component> meterComponent, visualComponent;
};

} // namespace mastering
