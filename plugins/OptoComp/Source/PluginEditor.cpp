#include "PluginEditor.h"

namespace Colours
{
    const juce::Colour panel   { 0xff1e1b18 };
    const juce::Colour face    { 0xff2a2622 };
    const juce::Colour cream   { 0xffe8dcc4 };
    const juce::Colour dim     { 0xff8a7f6e };
    const juce::Colour amber   { 0xffe0a040 };
    const juce::Colour hairline{ 0xff3a342e };
}

// ---------------------------------------------------------------------------

OptoLookAndFeel::OptoLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, Colours::cream);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Colours::dim);
    setColour (juce::ComboBox::backgroundColourId, Colours::face);
    setColour (juce::ComboBox::outlineColourId, Colours::hairline);
    setColour (juce::ComboBox::textColourId, Colours::cream);
    setColour (juce::ComboBox::arrowColourId, Colours::dim);
    setColour (juce::PopupMenu::backgroundColourId, Colours::face);
    setColour (juce::PopupMenu::textColourId, Colours::cream);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::hairline);
    setColour (juce::ToggleButton::textColourId, Colours::cream);
    setColour (juce::ToggleButton::tickColourId, Colours::amber);
    setColour (juce::ToggleButton::tickDisabledColourId, Colours::dim);
}

void OptoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float track = juce::jmax (2.0f, radius * 0.07f);

    juce::Path bg, val;
    bg.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    val.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
    g.setColour (Colours::hairline);
    g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (Colours::amber);
    g.strokePath (val, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobR = radius * 0.78f;
    g.setColour (Colours::face);
    g.fillEllipse (centre.x - knobR, centre.y - knobR, 2 * knobR, 2 * knobR);
    g.setColour (Colours::hairline);
    g.drawEllipse (centre.x - knobR, centre.y - knobR, 2 * knobR, 2 * knobR, 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-track * 0.5f, -knobR * 0.92f, track, knobR * 0.45f, track * 0.5f);
    g.setColour (Colours::cream);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

// ---------------------------------------------------------------------------

void GainReductionMeter::timerCallback()
{
    const float target = proc.getGainReductionDb();
    // Balística: sube rápido, baja como una aguja VU (~300 ms)
    shown += (target - shown) * (target > shown ? 0.6f : 0.12f);
    repaint();
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    constexpr float maxDb = 20.0f;
    auto r = getLocalBounds().toFloat();

    // Lectura numérica arriba, barra en medio, escala abajo
    g.setColour (Colours::cream);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (juce::String (shown, 1) + " dB GR", r.removeFromTop (22.0f), juce::Justification::centred);
    r.removeFromTop (6.0f);

    auto bar = r.removeFromTop (18.0f).reduced (12.0f, 0.0f);  // margen para las etiquetas de los extremos
    g.setColour (Colours::face);
    g.fillRoundedRectangle (bar, 3.0f);

    const float frac = juce::jlimit (0.0f, 1.0f, shown / maxDb);
    g.setColour (Colours::amber);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * frac), 3.0f);

    r.removeFromTop (4.0f);
    g.setFont (juce::FontOptions (11.0f));
    for (int db : { 0, 1, 3, 5, 7, 10, 15, 20 })
    {
        const float xPos = bar.getX() + bar.getWidth() * (float) db / maxDb;
        g.setColour (Colours::hairline);
        g.drawVerticalLine ((int) xPos, bar.getY(), bar.getBottom());
        g.setColour (Colours::dim);
        g.drawText (juce::String (db), juce::Rectangle<float> (xPos - 12.0f, r.getY(), 24.0f, 14.0f),
                    juce::Justification::centred);
    }
}

// ---------------------------------------------------------------------------

OptoCompEditor::OptoCompEditor (OptoCompProcessor& p)
    : AudioProcessorEditor (&p), proc (p), meter (p)
{
    setLookAndFeel (&lnf);

    setupKnob (gain, ParamID::gain, "GAIN", true, " dB");
    setupKnob (peak, ParamID::peakReduction, "PEAK REDUCTION", true, {});
    setupKnob (emphasis, ParamID::emphasis, "HF EMPHASIS", false, " %");
    setupKnob (color, ParamID::color, "COLOR", false, " %");
    setupKnob (mix, ParamID::mix, "MIX", false, " %");

    setupCombo (model, ParamID::model, modelAtt);
    setupCombo (mode, ParamID::mode, modeAtt);
    setupCombo (oversampling, ParamID::oversampling, osAtt);

    addAndMakeVisible (link);
    linkAtt = std::make_unique<ButtonAttachment> (proc.apvts, ParamID::link, link);

    addAndMakeVisible (meter);
    setSize (640, 400);
}

OptoCompEditor::~OptoCompEditor()
{
    setLookAndFeel (nullptr);
}

void OptoCompEditor::setupKnob (Knob& k, const char* paramId, const juce::String& name, bool big, const juce::String& suffix)
{
    k.slider.setTextValueSuffix (suffix);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    k.slider.setColour (juce::Slider::textBoxTextColourId, big ? Colours::cream : Colours::dim);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (big ? 13.0f : 11.0f, juce::Font::bold));
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<SliderAttachment> (proc.apvts, paramId, k.slider);
}

void OptoCompEditor::setupCombo (juce::ComboBox& box, const char* paramId, std::unique_ptr<ComboAttachment>& att)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (paramId)))
        box.addItemList (param->choices, 1);
    addAndMakeVisible (box);
    att = std::make_unique<ComboAttachment> (proc.apvts, paramId, box);
}

void OptoCompEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);
    g.setColour (Colours::cream);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("OPTOCOMP", 24, 16, 200, 28, juce::Justification::centredLeft);
    g.setColour (Colours::hairline);
    g.drawHorizontalLine (60, 24.0f, (float) getWidth() - 24.0f);
    g.drawHorizontalLine (292, 24.0f, (float) getWidth() - 24.0f);
}

void OptoCompEditor::resized()
{
    auto r = getLocalBounds().reduced (24, 16);

    auto header = r.removeFromTop (28);
    oversampling.setBounds (header.removeFromRight (70));
    header.removeFromRight (8);
    mode.setBounds (header.removeFromRight (110));
    header.removeFromRight (8);
    model.setBounds (header.removeFromRight (170));

    r.removeFromTop (28);
    auto main = r.removeFromTop (210);
    auto placeBig = [] (Knob& k, juce::Rectangle<int> area)
    {
        k.label.setBounds (area.removeFromTop (20));
        k.slider.setBounds (area);
    };
    placeBig (gain, main.removeFromLeft (180));
    placeBig (peak, main.removeFromRight (180));
    meter.setBounds (main.withSizeKeepingCentre (main.getWidth() - 40, 66));

    r.removeFromTop (18);
    auto bottom = r;
    link.setBounds (bottom.removeFromRight (120).withSizeKeepingCentre (120, 24));
    const int w = bottom.getWidth() / 3;
    for (auto* k : { &emphasis, &color, &mix })
    {
        auto area = bottom.removeFromLeft (w);
        k->label.setBounds (area.removeFromTop (16));
        k->slider.setBounds (area.withSizeKeepingCentre (w, area.getHeight()));
    }
}
