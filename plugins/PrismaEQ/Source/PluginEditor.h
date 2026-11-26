#pragma once

#include "PluginProcessor.h"
#include <studioui/StudioLookAndFeel.h>

/** Gráfico interactivo: rejilla, analizador pre/post, curvas y nodos arrastrables. */
class EqGraph final : public juce::Component, private juce::Timer
{
public:
    EqGraph (PrismaEQProcessor&, std::function<void (int)> onSelect);

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

    PrismaEQProcessor& proc;
    std::function<void (int)> onSelect;
    Analyzer pre, post;
    juce::Rectangle<float> plot;

    int selected = 0, dragging = -1, hovered = -1;
    std::array<dspcore::EqBandParams, PrismaIDs::numBands> params {};
    std::array<float, PrismaIDs::numBands> dyn {};
};

/** Controles de la banda seleccionada. Reasigna los attachments al cambiar de banda. */
class BandPanel final : public juce::Component
{
public:
    explicit BandPanel (PrismaEQProcessor&);
    void showBand (int band);
    void refreshEnablement();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    PrismaEQProcessor& proc;
    int band = 0;

    juce::ToggleButton on { "On" };
    juce::ComboBox type, slope, placement;
    studioui::LabelledKnob freq { "FREQ" }, gain { "GAIN" }, q { "Q" },
                           dynRange { "DYN RANGE" }, threshold { "THRESHOLD" }, attack { "ATTACK" }, release { "RELEASE" };

    std::unique_ptr<BA> onAtt;
    std::unique_ptr<CA> typeAtt, slopeAtt, placementAtt;
    std::unique_ptr<SA> freqAtt, gainAtt, qAtt, dynAtt, thrAtt, atkAtt, relAtt;
};

class PrismaEQEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PrismaEQEditor (PrismaEQProcessor&);
    ~PrismaEQEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void selectBand (int band);

private:
    void timerCallback() override;

    PrismaEQProcessor& proc;
    studioui::StudioLookAndFeel lnf;
    EqGraph graph;
    BandPanel panel;
    std::array<juce::TextButton, PrismaIDs::numBands> bandButtons;
    studioui::LabelledKnob output { "OUTPUT" };
    juce::ToggleButton hq { "HQ" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hqAtt;
    int selected = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismaEQEditor)
};
