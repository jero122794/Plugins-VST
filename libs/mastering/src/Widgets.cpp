#include <mastering/Widgets.h>

namespace mastering
{

using namespace studioui;

void ReductionMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (text, r.removeFromTop (16.0f), juce::Justification::centred);
    auto value = r.removeFromBottom (18.0f);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String (shown, 1) + " dB", value, juce::Justification::centred);

    auto bar = r.reduced (r.getWidth() * 0.5f - 8.0f, 4.0f);
    g.setColour (Colours::face);
    g.fillRoundedRectangle (bar, 3.0f);

    // Escala hacia abajo: 0 dB arriba (como los medidores de GR de hardware)
    const float frac = juce::jlimit (0.0f, 1.0f, shown / range);
    g.setColour (Colours::accent);
    g.fillRoundedRectangle (bar.withHeight (bar.getHeight() * frac), 3.0f);
    const float holdY = bar.getY() + bar.getHeight() * juce::jlimit (0.0f, 1.0f, peakHold / range);
    g.setColour (Colours::text.withAlpha (0.7f));
    g.drawHorizontalLine ((int) holdY, bar.getX(), bar.getRight());

    g.setFont (juce::FontOptions (9.0f));
    for (float db = 0.0f; db <= range; db += range / 4.0f)
    {
        const float y = bar.getY() + bar.getHeight() * db / range;
        g.setColour (Colours::dim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (bar.getRight() + 3.0f, y - 6.0f, 20.0f, 12.0f), juce::Justification::centredLeft);
    }
}

ParamPanel::ParamPanel (APVTS& state, const std::vector<Spec>& specs, std::unique_ptr<juce::Component> meter)
    : meterComponent (std::move (meter))
{
    for (auto& s : specs)
    {
        auto* p = state.getParameter (s.id);
        jassert (p != nullptr);
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p))
        {
            auto c = std::make_unique<Choice>();
            c->label.setText (s.label, juce::dontSendNotification);
            c->label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            c->box.addItemList (choice->choices, 1);
            addAndMakeVisible (c->label);
            addAndMakeVisible (c->box);
            c->att = std::make_unique<APVTS::ComboBoxAttachment> (state, s.id, c->box);
            choices.push_back (std::move (c));
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)
        {
            auto t = std::make_unique<juce::ToggleButton> (s.label);
            addAndMakeVisible (*t);
            toggleAtts.push_back (std::make_unique<APVTS::ButtonAttachment> (state, s.id, *t));
            toggles.push_back (std::move (t));
        }
        else
        {
            auto k = std::make_unique<LabelledKnob> (s.label);
            k->slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
            addAndMakeVisible (*k);
            knobAtts.push_back (std::make_unique<APVTS::SliderAttachment> (state, s.id, k->slider));
            knobs.push_back (std::move (k));
        }
    }
    if (meterComponent)
        addAndMakeVisible (*meterComponent);
}

void ParamPanel::resized()
{
    auto r = getLocalBounds();
    if (meterComponent)
        meterComponent->setBounds (r.removeFromRight (80).reduced (0, 8));

    if (! choices.empty() || ! toggles.empty())
    {
        auto left = r.removeFromLeft (180).reduced (0, 8);
        for (auto& c : choices)
        {
            c->label.setBounds (left.removeFromTop (18));
            c->box.setBounds (left.removeFromTop (26));
            left.removeFromTop (10);
        }
        for (auto& t : toggles)
        {
            t->setBounds (left.removeFromTop (26));
            left.removeFromTop (6);
        }
        r.removeFromLeft (16);
    }

    // Perillas en rejilla: hasta 4 por fila
    const int n = (int) knobs.size();
    if (n == 0) return;
    const int cols = std::min (4, n), rows = (n + cols - 1) / cols;
    const int cellW = std::min (130, r.getWidth() / cols), cellH = std::min (150, r.getHeight() / rows);
    for (int i = 0; i < n; ++i)
    {
        const int row = i / cols, col = i % cols;
        knobs[(size_t) i]->setBounds (r.getX() + col * cellW, r.getY() + row * cellH, cellW, cellH);
    }
}

} // namespace mastering
