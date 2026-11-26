#include "PluginEditor.h"

using namespace juce;

namespace
{
    constexpr float startAngle = MathConstants<float>::pi * 1.25f;   // -135° desde las 12
    constexpr float endAngle   = MathConstants<float>::pi * 2.75f;   // +135°

    Point<float> polar (Point<float> c, float radius, float angle)
    {
        return { c.x + radius * std::sin (angle), c.y - radius * std::cos (angle) };
    }

    void drawScrew (Graphics& g, Point<float> c, float r, bool dark)
    {
        ColourGradient grad (Colour (dark ? 0xff6a6a6a : 0xffe8e8e8), c.x - r, c.y - r,
                             Colour (dark ? 0xff2a2a2a : 0xff8a8a8a), c.x + r, c.y + r, false);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        g.setColour (Colours::black.withAlpha (0.55f));
        g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 0.8f);
        g.drawLine (c.x - r * 0.65f, c.y - r * 0.25f, c.x + r * 0.65f, c.y + r * 0.25f, 1.4f);   // ranura
    }
}

hw::Palette hw::paletteFor (int model)
{
    if (model == 0)   // válvula: aluminio gris claro, grabado oscuro
        return { Colour (0xffd3d5d6), Colour (0xffb3b7ba), Colour (0xffa9adb0), Colour (0xff26282a), Colour (0x66ffffff), Colour (0xffb3261e), false };
    // transistor: negro anodizado, grabado blanco crema
    return { Colour (0xff222325), Colour (0xff141516), Colour (0xff1b1c1e), Colour (0xffe9e4d6), Colour (0x99000000), Colour (0xffd8a23a), true };
}

// ============================================================================
// Perillas

void HardwareLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                            float start, float end, Slider& s)
{
    const bool dark = (bool) s.getProperties()["dark"];
    const auto bounds = Rectangle<int> (x, y, w, h).toFloat();
    const float R = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);

    // Sombra
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (c.x - R + 3.0f, c.y - R + 5.0f, 2 * R, 2 * R);

    // Faldón acanalado
    ColourGradient skirt (Colour (0xff3b3b3d), c.x, c.y - R, Colour (0xff0c0c0d), c.x, c.y + R, false);
    g.setGradientFill (skirt);
    g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
    constexpr int flutes = 28;
    for (int i = 0; i < flutes; ++i)
    {
        const float a = angle + MathConstants<float>::twoPi * (float) i / (float) flutes;
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawLine (Line<float> (polar (c, R * 0.80f, a), polar (c, R * 0.98f, a)), 1.6f);
    }
    g.setColour (Colours::black);
    g.drawEllipse (c.x - R, c.y - R, 2 * R, 2 * R, 1.2f);

    // Tapa: baquelita brillante (válvula) o centro metálico moleteado (transistor)
    const float r = R * (dark ? 0.58f : 0.70f);
    if (dark)
    {
        ColourGradient metal (Colour (0xfff2f2f2), c.x - r * 0.6f, c.y - r * 0.6f, Colour (0xff6d6f72), c.x + r, c.y + r, true);
        g.setGradientFill (metal);
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        for (int i = 0; i < 48; ++i)   // moleteado
        {
            const float a = MathConstants<float>::twoPi * (float) i / 48.0f;
            g.setColour (Colours::black.withAlpha (0.18f));
            g.drawLine (Line<float> (polar (c, r * 0.86f, a), polar (c, r, a)), 1.0f);
        }
    }
    else
    {
        ColourGradient bakelite (Colour (0xff4a4a4d), c.x - r * 0.5f, c.y - r * 0.7f, Colour (0xff060606), c.x + r * 0.4f, c.y + r, true);
        g.setGradientFill (bakelite);
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        g.setColour (Colours::white.withAlpha (0.12f));   // brillo
        g.fillEllipse (c.x - r * 0.55f, c.y - r * 0.75f, r * 0.8f, r * 0.45f);
    }
    g.setColour (Colours::black.withAlpha (0.8f));
    g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.0f);

    // Indicador
    g.setColour (Colour (0xfff4f1e8));
    if (dark)
        g.drawLine (Line<float> (polar (c, r * 1.05f, angle), polar (c, R * 0.96f, angle)), 3.0f);
    else
        g.drawLine (Line<float> (polar (c, r * 0.25f, angle), polar (c, R * 0.96f, angle)), 3.0f);
}

// ============================================================================
// Palanca

void BatToggle::paintButton (Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float nut = std::min (b.getWidth(), b.getHeight()) * 0.22f;

    // Tuerca hexagonal cromada
    Path hex;
    for (int i = 0; i < 6; ++i)
    {
        const auto p = polar (c, nut, MathConstants<float>::pi / 6.0f + MathConstants<float>::pi / 3.0f * (float) i);
        if (i == 0) hex.startNewSubPath (p); else hex.lineTo (p);
    }
    hex.closeSubPath();
    g.setGradientFill (ColourGradient (Colour (0xfff0f0f0), c.x - nut, c.y - nut, Colour (0xff7a7c7f), c.x + nut, c.y + nut, false));
    g.fillPath (hex);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.strokePath (hex, PathStrokeType (1.0f));

    // Palanca cónica
    const bool up = getToggleState();
    const float len = b.getHeight() * 0.42f, tip = nut * 0.55f, base = nut * 0.32f;
    const float dir = up ? -1.0f : 1.0f;
    Path lever;
    lever.startNewSubPath (c.x - base, c.y);
    lever.lineTo (c.x - tip, c.y + dir * len);
    lever.lineTo (c.x + tip, c.y + dir * len);
    lever.lineTo (c.x + base, c.y);
    lever.closeSubPath();
    g.setGradientFill (ColourGradient (Colour (0xffffffff), c.x - tip, c.y, Colour (0xff8e9093), c.x + tip, c.y, false));
    g.fillPath (lever);
    g.fillEllipse (c.x - tip, c.y + dir * len - tip, 2 * tip, 2 * tip);
    g.setColour (Colours::black.withAlpha (highlighted ? 0.7f : 0.45f));
    g.strokePath (lever, PathStrokeType (0.8f));
}

// ============================================================================
// VU

VuMeter::VuMeter (OptoCompProcessor& p) : proc (p) { startTimerHz (60); }

float VuMeter::deflection (float vu)
{
    // Un VU es lineal en tensión: +3 VU = 100 % del recorrido, 0 VU = 70.8 %, -20 VU = 7.1 %
    const float v = std::pow (10.0f, jlimit (-30.0f, 4.0f, vu) / 20.0f);
    return jlimit (-0.02f, 1.03f, v / std::pow (10.0f, 3.0f / 20.0f));
}

void VuMeter::timerCallback()
{
    const int mode = (int) proc.apvts.getRawParameterValue (ParamID::meterMode)->load();
    float vu;
    if (mode == 0)
        vu = -proc.getGainReductionDb();                             // GR: en reposo marca 0 VU y cae con la compresión
    else
    {
        const float rmsDb = Decibels::gainToDecibels (proc.getOutputRms(), -80.0f);
        vu = rmsDb + (mode == 1 ? 18.0f : 12.0f);                    // +4: 0 VU = -18 dBFS RMS; +10: 6 dB menos sensible
    }
    const float target = deflection (vu);

    // Muelle amortiguado: f ≈ 2.1 Hz, ζ ≈ 0.81 → 99 % en ~300 ms con ~1 % de sobreimpulso (balística VU)
    constexpr float omega = 13.2f, zeta = 0.81f, dt = 1.0f / 240.0f;
    for (int i = 0; i < 4; ++i)
    {
        const float acc = omega * omega * (target - position) - 2.0f * zeta * omega * velocity;
        velocity += acc * dt;
        position += velocity * dt;
    }
    repaint();
}

void VuMeter::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // Bisel
    g.setColour (Colour (0xff0d0d0e));
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (Colour (dark ? 0xff3a3b3d : 0xff5a5c5e));
    g.drawRoundedRectangle (b.reduced (1.5f), 7.0f, 2.0f);
    auto face = b.reduced (10.0f);

    // Esfera retroiluminada (cálida en el centro)
    ColourGradient glow (Colour (0xfffbe9b2), face.getCentreX(), face.getY() + face.getHeight() * 0.65f,
                         Colour (0xffd9b765), face.getX(), face.getY(), true);
    g.setGradientFill (glow);
    g.fillRoundedRectangle (face, 4.0f);

    const Point<float> pivot (face.getCentreX(), face.getBottom() + face.getHeight() * 0.32f);
    const float radius = face.getHeight() * 0.94f;
    constexpr float span = 0.80f;   // ±46°
    auto angleFor = [&] (float frac) { return -span + 2.0f * span * frac; };

    // Arcos de escala: negro hasta 0 VU, rojo de 0 a +3
    auto arc = [&] (float from, float to, float r, float thick, Colour col)
    {
        Path p;
        p.addCentredArc (pivot.x, pivot.y, r, r, 0.0f, angleFor (from), angleFor (to), true);
        g.setColour (col);
        g.strokePath (p, PathStrokeType (thick));
    };
    const float zero = deflection (0.0f);
    arc (deflection (-20.0f), zero, radius, 1.6f, Colour (0xff1d1a14));
    arc (zero, 1.0f, radius, 1.6f, Colour (0xffc0251c));
    arc (zero, 1.0f, radius - 6.0f, 9.0f, Colour (0xffc0251c));

    g.setFont (studioui::hardware (11.0f, studioui::Weight::SemiBold));
    for (int vu : { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 })
    {
        const float a = angleFor (deflection ((float) vu));
        const bool red = vu > 0;
        g.setColour (red ? Colour (0xffc0251c) : Colour (0xff1d1a14));
        g.drawLine (Line<float> (polar (pivot, radius, a), polar (pivot, radius + 9.0f, a)), vu == 0 ? 2.2f : 1.4f);
        const auto tp = polar (pivot, radius + 20.0f, a);
        g.drawText ((vu > 0 ? "+" : "") + String (std::abs (vu)), Rectangle<float> (26.0f, 14.0f).withCentre (tp), Justification::centred);
    }
    // Escala de porcentaje (inferior)
    g.setFont (studioui::hardware (8.5f, studioui::Weight::Medium));
    g.setColour (Colour (0xff1d1a14).withAlpha (0.75f));
    for (int pct = 0; pct <= 100; pct += 20)
    {
        const float a = angleFor (deflection (0.0f) * (float) pct / 100.0f);
        g.drawLine (Line<float> (polar (pivot, radius - 14.0f, a), polar (pivot, radius - 19.0f, a)), 1.0f);
        g.drawText (String (pct), Rectangle<float> (22.0f, 11.0f).withCentre (polar (pivot, radius - 27.0f, a)), Justification::centred);
    }
    g.setFont (studioui::hardware (15.0f, studioui::Weight::SemiBold));
    g.setColour (Colour (0xff1d1a14));
    g.drawText ("VU", Rectangle<float> (40.0f, 18.0f).withPosition (face.getX() + 10.0f, face.getY() + 8.0f), Justification::centredLeft);
    g.setColour (Colour (0xffc0251c));
    g.drawText ("VU", Rectangle<float> (40.0f, 18.0f).withPosition (face.getRight() - 50.0f, face.getY() + 8.0f), Justification::centredRight);

    const int mode = (int) proc.apvts.getRawParameterValue (ParamID::meterMode)->load();
    g.setFont (studioui::hardware (8.5f, studioui::Weight::SemiBold));
    g.setColour (Colour (0xff1d1a14).withAlpha (0.7f));
    g.drawText (mode == 0 ? "GAIN REDUCTION" : (mode == 1 ? "OUTPUT  +4" : "OUTPUT  +10"),
                face.withTrimmedTop (face.getHeight() * 0.66f).withHeight (12.0f), Justification::centred);

    // Aguja (con sombra)
    g.saveState();
    g.reduceClipRegion (face.toNearestInt());
    const float a = angleFor (position);
    const auto tip = polar (pivot, radius + 12.0f, a);
    g.setColour (Colours::black.withAlpha (0.18f));
    g.drawLine (Line<float> (pivot.translated (3.0f, 2.0f), tip.translated (3.0f, 2.0f)), 2.0f);
    g.setColour (Colour (0xff101010));
    g.drawLine (Line<float> (pivot, tip), 1.6f);
    g.restoreState();

    // Tapa del pivote
    auto cover = face.withTrimmedTop (face.getHeight() * 0.80f);
    g.setGradientFill (ColourGradient (Colour (0xff2c2c2e), cover.getX(), cover.getY(), Colour (0xff0e0e0f), cover.getX(), cover.getBottom(), false));
    g.fillRect (cover);

    // Reflejo del cristal
    ColourGradient glass (Colours::white.withAlpha (0.22f), face.getX(), face.getY(), Colours::white.withAlpha (0.0f),
                          face.getX() + face.getWidth() * 0.45f, face.getY() + face.getHeight() * 0.6f, false);
    g.setGradientFill (glass);
    g.fillRoundedRectangle (face, 4.0f);
}

// ============================================================================
// Editor

OptoCompEditor::OptoCompEditor (OptoCompProcessor& p) : AudioProcessorEditor (&p), proc (p), vu (p)
{
    setLookAndFeel (&barLnf);
    auto& s = proc.apvts;

    for (auto* k : { &gain, &peak, &meterSelect })
    {
        k->setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        k->setRotaryParameters (startAngle, endAngle, true);
        k->setLookAndFeel (&hardwareLnf);
        k->setMouseDragSensitivity (220);
        addAndMakeVisible (k);
    }
    meterSelect.setRotaryParameters (MathConstants<float>::pi * 1.6f, MathConstants<float>::pi * 2.4f, true);
    gain.setDoubleClickReturnValue (true, 0.0);
    peak.setDoubleClickReturnValue (true, 40.0);
    gainAtt  = std::make_unique<SA> (s, ParamID::gain, gain);
    peakAtt  = std::make_unique<SA> (s, ParamID::peakReduction, peak);
    meterAtt = std::make_unique<SA> (s, ParamID::meterMode, meterSelect);
    gain.setTooltip ("Gain (make-up)");
    peak.setTooltip ("Peak Reduction");

    addAndMakeVisible (limitSwitch);
    limitAtt = std::make_unique<BA> (s, ParamID::mode, limitSwitch);
    addAndMakeVisible (vu);

    // Barra inferior
    for (auto* b : { &tubeButton, &solidButton })
    {
        b->setClickingTogglesState (false);
        b->setRadioGroupId (1);
        addAndMakeVisible (b);
    }
    auto setModel = [this] (int m)
    {
        if (auto* param = proc.apvts.getParameter (ParamID::model))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) m));
    };
    tubeButton.onClick = [setModel] { setModel (0); };
    solidButton.onClick = [setModel] { setModel (1); };

    for (auto* k : { &emphasis, &color, &mix })
    {
        k->slider.setColour (Slider::rotarySliderFillColourId, studioui::Colours::accent);
        addAndMakeVisible (k);
    }
    for (auto* k : { &emphasis, &color, &mix }) k->slider.setTextValueSuffix (" %");
    emphAtt  = std::make_unique<SA> (s, ParamID::emphasis, emphasis.slider);
    colorAtt = std::make_unique<SA> (s, ParamID::color, color.slider);
    mixAtt   = std::make_unique<SA> (s, ParamID::mix, mix.slider);
    oversampling.addItemList ({ "Oversampling 1x", "Oversampling 2x", "Oversampling 4x" }, 1);
    addAndMakeVisible (oversampling);
    osAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, ParamID::oversampling, oversampling);
    addAndMakeVisible (link);
    linkAtt = std::make_unique<BA> (s, ParamID::link, link);

    setSize (1000, 400);
    timerCallback();
    startTimerHz (20);
}

OptoCompEditor::~OptoCompEditor()
{
    for (auto* k : { &gain, &peak, &meterSelect }) k->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void OptoCompEditor::timerCallback()
{
    const int m = (int) proc.apvts.getRawParameterValue (ParamID::model)->load();
    if (m != model)
    {
        model = m;
        const bool dark = m == 1;
        for (auto* k : { &gain, &peak, &meterSelect })
        {
            k->getProperties().set ("dark", dark);
            k->repaint();
        }
        vu.setDark (dark);
        tubeButton.setToggleState (m == 0, dontSendNotification);
        solidButton.setToggleState (m == 1, dontSendNotification);
        repaint();
    }
    const bool on = proc.getGainReductionDb() > 0.5f;
    if (on != lampOn) { lampOn = on; repaint (Rectangle<int> (16, 16).withCentre (lampCentre.toInt()).expanded (8)); }
    repaint (rack.withTrimmedTop (rack.getHeight() - 70));   // lecturas de valor bajo las perillas
}

void OptoCompEditor::drawEngraved (Graphics& g, const String& text, Rectangle<float> r, float size, const hw::Palette& pal,
                                   Justification just, bool bold)
{
    g.setFont (studioui::hardware (size, bold ? studioui::Weight::SemiBold : studioui::Weight::Medium));
    g.setColour (pal.textShadow);
    g.drawText (text, r.translated (0.0f, pal.dark ? -1.0f : 1.0f), just);
    g.setColour (pal.text);
    g.drawText (text, r, just);
}

void OptoCompEditor::drawKnobScale (Graphics& g, Slider& s, const hw::Palette& pal, bool tube)
{
    const auto c = s.getBounds().toFloat().getCentre();
    const float R = (float) s.getWidth() * 0.5f;
    g.setFont (studioui::hardware (tube ? 12.0f : 15.0f, studioui::Weight::SemiBold));
    const int steps = 10;
    for (int i = 0; i <= steps; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / (float) steps;
        g.setColour (pal.text);
        g.drawLine (Line<float> (polar (c, R + 3.0f, a), polar (c, R + (i % 5 == 0 ? 11.0f : 8.0f), a)), i % 5 == 0 ? 2.0f : 1.2f);
        const String label = tube ? String (i * 10) : String (i);
        g.drawText (label, Rectangle<float> (32.0f, 16.0f).withCentre (polar (c, R + 24.0f, a)), Justification::centred);
    }
}

void OptoCompEditor::paint (Graphics& g)
{
    const auto pal = hw::paletteFor (std::max (0, model));
    const bool tube = model != 1;

    // --- Orejas del rack
    g.fillAll (Colour (0xff0b0b0c));
    auto r = rack.toFloat();
    g.setColour (pal.ear);
    g.fillRect (r);
    for (float x : { r.getX() + 18.0f, r.getRight() - 18.0f })
        for (float y : { r.getY() + 46.0f, r.getBottom() - 46.0f })
        {
            g.setColour (Colour (0xff0b0b0c));
            g.fillRoundedRectangle (Rectangle<float> (12.0f, 26.0f).withCentre ({ x, y }), 6.0f);   // ranura
            drawScrew (g, { x, y }, 6.0f, pal.dark);
        }

    // --- Frontal
    auto face = r.reduced (36.0f, 0.0f);
    g.setGradientFill (ColourGradient (pal.faceTop, face.getX(), face.getY(), pal.faceBottom, face.getX(), face.getBottom(), false));
    g.fillRect (face);
    Random rng (7);   // cepillado horizontal sutil
    for (int i = 0; i < 260; ++i)
    {
        const float y = face.getY() + rng.nextFloat() * face.getHeight();
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (pal.dark ? 0.025f : 0.05f));
        g.drawHorizontalLine ((int) y, face.getX(), face.getRight());
    }
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawRect (face, 1.0f);
    for (auto p : { face.getTopLeft().translated (14, 14), face.getTopRight().translated (-14, 14),
                    face.getBottomLeft().translated (14, -14), face.getBottomRight().translated (-14, -14) })
        drawScrew (g, p, 5.0f, pal.dark);

    // --- Rótulos
    drawEngraved (g, "OPTOCOMP", { face.getX() + 40.0f, face.getY() + 22.0f, 220.0f, 30.0f }, 26.0f, pal, Justification::centredLeft);
    g.setColour (pal.accent);
    g.fillRect (face.getX() + 42.0f, face.getY() + 54.0f, 150.0f, 2.5f);
    drawEngraved (g, tube ? "LEVELING AMPLIFIER   T-2" : "AUDIO LEVELER   T-3",
                  { face.getX() + 40.0f, face.getY() + 60.0f, 260.0f, 18.0f }, 11.5f, pal, Justification::centredLeft);

    drawKnobScale (g, gain, pal, tube);
    drawKnobScale (g, peak, pal, tube);
    auto under = [&] (Slider& s, const String& name, const String& value)
    {
        auto b = s.getBounds().toFloat();
        drawEngraved (g, name, { b.getX() - 40.0f, b.getBottom() + 20.0f, b.getWidth() + 80.0f, 18.0f }, 13.0f, pal);
        drawEngraved (g, value, { b.getX() - 40.0f, b.getBottom() + 38.0f, b.getWidth() + 80.0f, 14.0f }, 10.5f, pal, Justification::centred, false);
    };
    under (gain, "GAIN", (proc.apvts.getRawParameterValue (ParamID::gain)->load() >= 0 ? "+" : "")
                          + String (proc.apvts.getRawParameterValue (ParamID::gain)->load(), 1) + " dB");
    under (peak, "PEAK REDUCTION", String (proc.apvts.getRawParameterValue (ParamID::peakReduction)->load(), 0));

    // Palanca Compress / Limit
    auto sw = limitSwitch.getBounds().toFloat();
    drawEngraved (g, "LIMIT", { sw.getX() - 20.0f, sw.getY() - 18.0f, sw.getWidth() + 40.0f, 14.0f }, 11.0f, pal);
    drawEngraved (g, "COMPRESS", { sw.getX() - 30.0f, sw.getBottom() + 4.0f, sw.getWidth() + 60.0f, 14.0f }, 11.0f, pal);

    // Selector del medidor
    auto ms = meterSelect.getBounds().toFloat();
    const auto mc = ms.getCentre();
    const float mr = ms.getWidth() * 0.5f;
    drawEngraved (g, "METER", { ms.getX() - 30.0f, ms.getY() - 40.0f, ms.getWidth() + 60.0f, 14.0f }, 11.0f, pal);
    const char* labels[] = { "GR", "+4", "+10" };
    for (int i = 0; i < 3; ++i)
    {
        const float a = MathConstants<float>::pi * (1.6f + 0.4f * (float) i);
        drawEngraved (g, labels[i], Rectangle<float> (30.0f, 14.0f).withCentre (polar (mc, mr + 16.0f, a)), 10.5f, pal);
    }

    // Piloto (se enciende con la compresión)
    const float lr = 7.0f;
    g.setColour (Colours::black);
    g.fillEllipse (lampCentre.x - lr - 2, lampCentre.y - lr - 2, 2 * lr + 4, 2 * lr + 4);
    const auto lampCol = tube ? Colour (0xffff3b2f) : Colour (0xffffb340);
    if (lampOn)
    {
        g.setColour (lampCol.withAlpha (0.25f));
        g.fillEllipse (lampCentre.x - lr * 2.2f, lampCentre.y - lr * 2.2f, lr * 4.4f, lr * 4.4f);
    }
    g.setGradientFill (ColourGradient (lampOn ? lampCol.brighter (0.6f) : lampCol.darker (1.6f), lampCentre.x - 2, lampCentre.y - 3,
                                       lampOn ? lampCol : lampCol.darker (2.5f), lampCentre.x + lr, lampCentre.y + lr, true));
    g.fillEllipse (lampCentre.x - lr, lampCentre.y - lr, 2 * lr, 2 * lr);
    drawEngraved (g, "GR", { lampCentre.x - 20.0f, lampCentre.y + 12.0f, 40.0f, 14.0f }, 10.0f, pal);

    // --- Barra inferior
    g.setColour (studioui::Colours::panel);
    g.fillRect (bar);
    g.setColour (studioui::Colours::dim);
    g.setFont (studioui::hardware (11.0f, studioui::Weight::SemiBold));
    g.drawText ("MODELO", bar.withWidth (90).withX (bar.getX() + 20), Justification::centredLeft);
}

void OptoCompEditor::resized()
{
    auto b = getLocalBounds();
    bar = b.removeFromBottom (86);
    rack = b;

    const auto face = rack.reduced (36, 0);
    const int cy = face.getY() + 172;
    gain.setBounds (Rectangle<int> (108, 108).withCentre ({ face.getX() + 232, cy }));
    peak.setBounds (Rectangle<int> (108, 108).withCentre ({ face.getRight() - 232, cy }));
    vu.setBounds (Rectangle<int> (262, 162).withCentre ({ face.getCentreX(), cy - 10 }));
    limitSwitch.setBounds (Rectangle<int> (40, 64).withCentre ({ face.getX() + 70, cy + 58 }));
    meterSelect.setBounds (Rectangle<int> (52, 52).withCentre ({ face.getRight() - 78, cy - 34 }));
    lampCentre = { (float) face.getRight() - 78.0f, (float) cy + 58.0f };

    auto row = bar.reduced (20, 8);
    row.removeFromLeft (70);
    tubeButton.setBounds (row.removeFromLeft (120).withSizeKeepingCentre (120, 30));
    row.removeFromLeft (6);
    solidButton.setBounds (row.removeFromLeft (150).withSizeKeepingCentre (150, 30));
    row.removeFromLeft (30);
    for (auto* k : { &emphasis, &color, &mix })
    {
        k->setBounds (row.removeFromLeft (96));
        row.removeFromLeft (6);
    }
    row.removeFromLeft (20);
    link.setBounds (row.removeFromRight (110).withSizeKeepingCentre (110, 26));
    row.removeFromRight (10);
    oversampling.setBounds (row.removeFromRight (160).withSizeKeepingCentre (160, 28));
}
