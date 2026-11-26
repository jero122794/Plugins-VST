#pragma once
//
// Estilo visual común para los plugins (requiere JUCE).
//
#include <juce_gui_basics/juce_gui_basics.h>
#include "Fonts.h"

namespace studioui
{

/** Literal UTF-8 → juce::String. JUCE interpreta los const char* como ASCII: sin esto, "Saturación" sale corrupto. */
inline juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

namespace Colours
{
    inline const juce::Colour panel    { 0xff17191c };
    inline const juce::Colour face     { 0xff23262b };
    inline const juce::Colour graphBg  { 0xff111315 };
    inline const juce::Colour grid     { 0xff262a30 };
    inline const juce::Colour hairline { 0xff343941 };
    inline const juce::Colour text     { 0xffe6e8eb };
    inline const juce::Colour dim      { 0xff8b929c };
    inline const juce::Colour accent   { 0xff5fb3ff };

    /** Paleta categórica para bandas: tonos separados, luminosidad similar. */
    inline juce::Colour band (int i)
    {
        static const juce::Colour c[] = { juce::Colour (0xffff6b6b), juce::Colour (0xffffa94d), juce::Colour (0xffffd43b),
                                          juce::Colour (0xff69db7c), juce::Colour (0xff38d9a9), juce::Colour (0xff4dabf7),
                                          juce::Colour (0xff9775fa), juce::Colour (0xfff783ac) };
        return c[(size_t) (i % 8)];
    }
}

class StudioLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StudioLookAndFeel()
    {
        using namespace juce;
        setColour (Slider::textBoxTextColourId, Colours::text);
        setColour (Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (Slider::textBoxHighlightColourId, Colours::hairline);
        setColour (Label::textColourId, Colours::dim);
        setColour (ComboBox::backgroundColourId, Colours::face);
        setColour (ComboBox::outlineColourId, Colours::hairline);
        setColour (ComboBox::textColourId, Colours::text);
        setColour (ComboBox::arrowColourId, Colours::dim);
        setColour (PopupMenu::backgroundColourId, Colours::face);
        setColour (PopupMenu::textColourId, Colours::text);
        setColour (PopupMenu::highlightedBackgroundColourId, Colours::hairline);
        setColour (PopupMenu::highlightedTextColourId, Colours::text);
        setColour (ToggleButton::textColourId, Colours::text);
        setColour (ToggleButton::tickColourId, Colours::accent);
        setColour (ToggleButton::tickDisabledColourId, Colours::dim);
        setColour (TextButton::buttonColourId, Colours::face);
        setColour (TextButton::textColourOffId, Colours::dim);
        setColour (TextButton::textColourOnId, Colours::text);
    }

    /**
        Toda fuente "por defecto" (las FontOptions sin tipografía explícita) se resuelve a IBM Plex:
        negrita → SemiBold, normal → Regular; monoespaciada → Plex Mono.
    */
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
    {
        if (f.getTypefaceName() == juce::Font::getDefaultMonospacedFontName())
            return monoTypeface();
        if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
            return sansTypeface (f.isBold() ? Weight::SemiBold : Weight::Regular);
        return juce::LookAndFeel_V4::getTypefaceForFont (f);
    }

    /** Los valores de las perillas, en Plex Mono: los dígitos tienen el mismo ancho y no saltan al girar. */
    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (juce::Font (mono (13.0f)));
        return l;
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return juce::Font (sans (std::min (14.0f, (float) box.getHeight() * 0.55f), Weight::Medium));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (sans (std::min (14.0f, (float) buttonHeight * 0.55f), Weight::Medium));
    }

    juce::Font getPopupMenuFont() override { return juce::Font (sans (14.0f)); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float track = juce::jmax (2.0f, radius * 0.1f);
        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);

        // Bipolar (p. ej. ganancia ±): el arco sale desde el centro
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float from = bipolar ? startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle) : startAngle;

        juce::Path bg, val;
        bg.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        val.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (Colours::hairline);
        g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (s.isEnabled() ? accent : Colours::dim);
        g.strokePath (val, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float knobR = radius * 0.72f;
        g.setColour (Colours::face);
        g.fillEllipse (centre.x - knobR, centre.y - knobR, 2 * knobR, 2 * knobR);

        juce::Path pointer;
        pointer.addRoundedRectangle (-track * 0.5f, -knobR, track, knobR * 0.5f, track * 0.5f);
        g.setColour (s.isEnabled() ? Colours::text : Colours::dim);
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
    }
};

/**
    Instala el estilo de la suite como LookAndFeel por defecto mientras exista algún editor:
    así cualquier texto (también los dibujados directamente con Graphics) usa las fuentes incrustadas.
    Cada editor tiene uno como primer miembro.
*/
class DefaultLookAndFeelScope
{
public:
    DefaultLookAndFeelScope()
    {
        ++liveScopes();
        juce::LookAndFeel::setDefaultLookAndFeel (&shared.get());
    }
    ~DefaultLookAndFeelScope()
    {
        if (--liveScopes() == 0)   // último editor cerrado: se devuelve el estilo por defecto de JUCE
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }
    StudioLookAndFeel& get() { return shared.get(); }

private:
    static int& liveScopes() { static int n = 0; return n; }   // solo en el hilo de mensajes
    juce::SharedResourcePointer<StudioLookAndFeel> shared;
};

/** Perilla con etiqueta encima y valor debajo. */
struct LabelledKnob : public juce::Component
{
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label label;

    explicit LabelledKnob (const juce::String& name)
    {
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (label);
        addAndMakeVisible (slider);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        slider.setBounds (r);
    }
};

} // namespace studioui
