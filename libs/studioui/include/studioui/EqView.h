#pragma once

#include <studioui/EqCore.h>
#include <studioui/StudioLookAndFeel.h>

namespace studioui
{

/** Gráfico interactivo: rejilla, analizador pre/post, curvas y nodos arrastrables. */
class EqGraph final : public juce::Component, private juce::Timer
{
public:
    EqGraph (EqCore&, std::function<void (int)> onSelect);

    void setSelected (int band) { selected = band; repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;

    static constexpr float minFreq = 20.0f, maxFreq = 20000.0f, rangeDb = 24.0f;

private:
    void timerCallback() override;

    struct Analyzer
    {
        static constexpr int order = 12, size = 1 << order;
        juce::dsp::FFT fft { order };
        juce::dsp::WindowingFunction<float> window { (size_t) size, juce::dsp::WindowingFunction<float>::hann };
        std::array<float, size> ring {};
        int writePos = 0;
        std::array<float, 2 * size> work {};
        std::array<float, size / 2> smoothedDb {};
        bool active = false;

        Analyzer() { smoothedDb.fill (-120.0f); }
        void consume (const float* d, int n);
        void compute();
    };

    float xForFreq (float f) const;
    float freqForX (float x) const;
    float yForDb (float db) const;
    float dbForY (float y) const;
    float yForAnalyzerDb (float db) const;
    juce::Point<float> nodePosition (int band) const;
    int hitTest (juce::Point<float>) const;
    juce::Path analyzerPath (const Analyzer&, bool closed) const;
    void setParam (int band, const char* name, float plainValue);
    void beginGesture (int band);
    void endGesture (int band);

    EqCore& core;
    std::function<void (int)> onSelect;
    Analyzer pre, post;
    juce::Rectangle<float> plot;

    int selected = 0, dragging = -1, hovered = -1;
    std::array<dspcore::EqBandParams, EqCore::numBands> params {};
    std::array<float, EqCore::numBands> dyn {};
};

/** Controles de la banda seleccionada. Reasigna los attachments al cambiar de banda. */
class BandPanel final : public juce::Component
{
public:
    explicit BandPanel (EqCore&);
    void showBand (int band);
    void refreshEnablement();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    EqCore& core;
    int band = 0;

    juce::ToggleButton on { "On" };
    juce::ComboBox type, slope, placement;
    LabelledKnob freq { "FREQ" }, gain { "GAIN" }, q { "Q" },
                           dynRange { "DYN RANGE" }, threshold { "THRESHOLD" }, attack { "ATTACK" }, release { "RELEASE" };

    std::unique_ptr<BA> onAtt;
    std::unique_ptr<CA> typeAtt, slopeAtt, placementAtt;
    std::unique_ptr<SA> freqAtt, gainAtt, qAtt, dynAtt, thrAtt, atkAtt, relAtt;
};

/** Vista completa del EQ: botones de banda, HQ, gráfico, panel de banda y salida. */
class EqView final : public juce::Component, private juce::Timer
{
public:
    explicit EqView (EqCore&);
    void resized() override;
    void selectBand (int band);

private:
    void timerCallback() override;

    EqCore& core;
    EqGraph graph;
    BandPanel panel;
    std::array<juce::TextButton, EqCore::numBands> bandButtons;
    LabelledKnob output { "OUTPUT" };
    juce::ToggleButton hq { "HQ" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hqAtt;
    int selected = 0;
};

} // namespace studioui
